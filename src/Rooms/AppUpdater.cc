#include "AppUpdater.h"

#include "AppMessages.h"
#include "QGCLoggingCategory.h"
#include "RoomManager.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QtEndian>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtQml/QJSEngine>

QGC_LOGGING_CATEGORY(AppUpdaterLog, "Rooms.AppUpdater")

namespace {

AppUpdater *s_instance = nullptr;

const QString kInstallerPrefix = QStringLiteral("NX-Rover-");
const QString kInstallerSuffix = QStringLiteral("-setup.exe");

/// Status and JSON body of a finished hub request; status 0 with an "error" when the hub was not reached
void readReply(QNetworkReply *reply, int &status, QJsonObject &json)
{
    status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->property("nxPinFailed").toBool()) {
        status = 0;
        json = QJsonObject{{QStringLiteral("error"), QStringLiteral("pin_failed")}};
    } else if (status == 0) {
        json = QJsonObject{{QStringLiteral("error"), QStringLiteral("network")},
                           {QStringLiteral("message"), reply->errorString()}};
    } else {
        json = QJsonDocument::fromJson(reply->readAll()).object();
    }
}

QString sha256Of(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    (void) hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}

/// The hub's answer about its update, for QML
QVariantMap publishedView(const QJsonObject &json)
{
    return QVariantMap{
        {QStringLiteral("available"), json.value(QLatin1String("available")).toBool()},
        {QStringLiteral("version"), json.value(QLatin1String("version")).toString()},
        {QStringLiteral("size"), json.value(QLatin1String("size")).toDouble()},
        {QStringLiteral("notes"), json.value(QLatin1String("notes")).toString()},
        {QStringLiteral("published"), json.value(QLatin1String("published")).toDouble()},
        {QStringLiteral("by"), json.value(QLatin1String("by")).toString()},
        {QStringLiteral("maxUpload"), json.value(QLatin1String("max_upload")).toDouble()},
    };
}

}  // namespace

AppUpdater::AppUpdater(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
    (void) connect(RoomManager::instance(), &RoomManager::roomsChanged, this, &AppUpdater::publishChanged);

    if (QGC::runningUnitTests()) {
        return;
    }
    _removeOldDownloads();
    // The room manager has its hubs from the settings; a moment after start the network is usually up as well
    QTimer::singleShot(kStartupCheckDelayMs, this, [this]() {
        _startupCheck = true;
        check();
    });
}

AppUpdater::~AppUpdater()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

AppUpdater *AppUpdater::instance()
{
    if (!s_instance) {
        (void) new AppUpdater(QCoreApplication::instance());
    }
    return s_instance;
}

AppUpdater *AppUpdater::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    AppUpdater *const updater = instance();
    QJSEngine::setObjectOwnership(updater, QJSEngine::CppOwnership);
    return updater;
}

// ---- versions ------------------------------------------------------------------------------------------------

int AppUpdater::compareVersions(const QString &a, const QString &b)
{
    static const QRegularExpression numbers(QStringLiteral("^v?(\\d+(?:\\.\\d+)*)"));
    const QStringList left = numbers.match(a.trimmed()).captured(1).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const QStringList right = numbers.match(b.trimmed()).captured(1).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    for (qsizetype i = 0; i < qMax(left.size(), right.size()); ++i) {
        const int l = (i < left.size()) ? left.at(i).toInt() : 0;
        const int r = (i < right.size()) ? right.at(i).toInt() : 0;
        if (l != r) {
            return (l < r) ? -1 : 1;
        }
    }
    return 0;
}

QString AppUpdater::programVersion(const QByteArray &head)
{
    // Same reading as the hub: the UTF-16 key VS_VERSION_INFO, then within a few bytes the fixed part (signature,
    // structure version, file version, product version)
    if (!head.startsWith("MZ")) {
        return QString();
    }
    const QString keyText = QStringLiteral("VS_VERSION_INFO");
    const QByteArray key(reinterpret_cast<const char *>(keyText.utf16()), keyText.size() * 2);
    static const char signature[] = {'\xBD', '\x04', '\xEF', '\xFE'};
    const QByteArray fixedInfo(signature, sizeof(signature));

    qsizetype keyAt = head.indexOf(key);
    while (keyAt >= 0) {
        const qsizetype at = head.indexOf(fixedInfo, keyAt);
        if ((at >= 0) && (at <= keyAt + key.size() + 16) && (at + 24 <= head.size())) {
            const quint32 productMs = qFromLittleEndian<quint32>(head.constData() + at + 16);
            const quint32 productLs = qFromLittleEndian<quint32>(head.constData() + at + 20);
            if (productMs || productLs) {
                QString version = QStringLiteral("%1.%2.%3").arg(productMs >> 16).arg(productMs & 0xFFFF).arg(productLs >> 16);
                if (productLs & 0xFFFF) {
                    version += QStringLiteral(".%1").arg(productLs & 0xFFFF);
                }
                return version;
            }
        }
        keyAt = head.indexOf(key, keyAt + 2);
    }
    return QString();
}

QString AppUpdater::currentVersion() const
{
    static const QRegularExpression numbers(QStringLiteral("^v?(\\d+(?:\\.\\d+)*)"));
    const QString version = numbers.match(QCoreApplication::applicationVersion()).captured(1);
    return version.isEmpty() ? QCoreApplication::applicationVersion() : version;
}

// ---- checking ------------------------------------------------------------------------------------------------

void AppUpdater::_setState(const QString &state, const QString &error)
{
    _state = state;
    _error = error;
    emit stateChanged();
}

double AppUpdater::progress() const
{
    const double size = _update.value(QStringLiteral("size")).toDouble();
    return (size > 0) ? qMin(1.0, static_cast<double>(_received) / size) : 0.0;
}

bool AppUpdater::canPublish() const
{
    return !_adminServer().isEmpty();
}

QString AppUpdater::_adminServer() const
{
    const QVariantList servers = RoomManager::instance()->servers();
    for (const QVariant &server : servers) {
        const QVariantMap entry = server.toMap();
        if (entry.value(QStringLiteral("serverAdmin")).toBool()) {
            return entry.value(QStringLiteral("key")).toString();
        }
    }
    return QString();
}

void AppUpdater::check()
{
    if ((_checksRunning > 0) || _download || (_state == QLatin1String("installing"))) {
        return;
    }
    RoomManager *const rooms = RoomManager::instance();
    const QVariantList servers = rooms->servers();
    if (servers.isEmpty()) {
        _update.clear();
        _startupCheck = false;
        _setState(QStringLiteral("noServer"));
        return;
    }

    _checksRunning = static_cast<int>(servers.size());
    _checkError.clear();
    _checkAnswered = false;
    _found.clear();
    _setState(QStringLiteral("checking"));

    const QString admin = _adminServer();
    for (const QVariant &server : servers) {
        const QString key = server.toMap().value(QStringLiteral("key")).toString();
        QNetworkReply *const reply = rooms->_send(key, "GET", QStringLiteral("/v1/update"), QByteArray(), QByteArray(),
                                                  nullptr, kCheckTimeoutMs);
        if (!reply) {
            --_checksRunning;
            continue;
        }
        (void) connect(reply, &QNetworkReply::finished, this, [this, reply, key, admin]() {
            reply->deleteLater();
            int status = 0;
            QJsonObject json;
            readReply(reply, status, json);
            if (status == 200) {
                _checkAnswered = true;
                if (key == admin) {
                    _published = publishedView(json);
                    emit publishChanged();
                }
                const QString version = json.value(QLatin1String("version")).toString();
                if (json.value(QLatin1String("available")).toBool() && (compareVersions(version, currentVersion()) > 0) &&
                    (_found.isEmpty() || (compareVersions(version, _found.value(QStringLiteral("version")).toString()) > 0))) {
                    _found = QVariantMap{
                        {QStringLiteral("version"), version},
                        {QStringLiteral("size"), json.value(QLatin1String("size")).toDouble()},
                        {QStringLiteral("notes"), json.value(QLatin1String("notes")).toString()},
                        {QStringLiteral("published"), json.value(QLatin1String("published")).toDouble()},
                        {QStringLiteral("sha256"), json.value(QLatin1String("sha256")).toString().toLower()},
                        {QStringLiteral("server"), key},
                    };
                }
            } else {
                _checkError = RoomManager::instance()->_errorText(status, json);
                qCWarning(AppUpdaterLog) << "Update check failed:" << _checkError;
            }
            if (--_checksRunning == 0) {
                _checkFinished();
            }
        });
    }
    if (_checksRunning == 0) {
        _checkFinished();
    }
}

void AppUpdater::_checkFinished()
{
    const bool startup = _startupCheck;
    _startupCheck = false;
    _update = _found;
    _found.clear();
    if (!_update.isEmpty()) {
        qCWarning(AppUpdaterLog) << "Update available:" << _update.value(QStringLiteral("version")).toString();
        _setState(QStringLiteral("available"));
        if (startup) {
            emit offerUpdate();
        }
    } else if (_checkAnswered) {
        _setState(QStringLiteral("upToDate"));
    } else {
        _setState(QStringLiteral("error"), _checkError.isEmpty() ? tr("The room server is not reachable.") : _checkError);
    }
}

// ---- downloading and installing ------------------------------------------------------------------------------

QString AppUpdater::_downloadDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/updates");
}

QString AppUpdater::_installerPath(const QString &version) const
{
    return _downloadDirectory() + QLatin1Char('/') + kInstallerPrefix + version + kInstallerSuffix;
}

void AppUpdater::_removeOldDownloads() const
{
    // Installers of this version and older are done with, finished or not
    const QFileInfoList files = QDir(_downloadDirectory()).entryInfoList(
        QStringList{kInstallerPrefix + QStringLiteral("*") + kInstallerSuffix + QStringLiteral("*")}, QDir::Files);
    for (const QFileInfo &file : files) {
        QString version = file.fileName().mid(kInstallerPrefix.size());
        version = version.left(version.indexOf(kInstallerSuffix));
        if (compareVersions(version, currentVersion()) <= 0) {
            (void) QFile::remove(file.absoluteFilePath());
        }
    }
}

void AppUpdater::install()
{
    if ((_state != QLatin1String("available")) || _update.isEmpty() || _download) {
        return;
    }
    const QString version = _update.value(QStringLiteral("version")).toString();
    const qint64 size = static_cast<qint64>(_update.value(QStringLiteral("size")).toDouble());
    const QString sha256 = _update.value(QStringLiteral("sha256")).toString();
    const QString path = _installerPath(version);
    (void) QDir().mkpath(_downloadDirectory());

    // Downloaded before and intact (e.g. the permission request was turned down): install it again
    if ((QFileInfo(path).size() == size) && (sha256Of(path) == sha256)) {
        _startInstaller(path);
        return;
    }
    (void) QFile::remove(path);

    _downloadFile = new QFile(path + QStringLiteral(".part"), this);
    if (!_downloadFile->open(QIODevice::ReadWrite)) {
        const QString error = _downloadFile->errorString();
        delete _downloadFile;
        _downloadFile = nullptr;
        _setState(QStringLiteral("available"), tr("The update cannot be saved: %1").arg(error));
        return;
    }
    // Continue a download that was cut off: what arrived counts toward the checksum
    qint64 offset = _downloadFile->size();
    if (offset > size) {
        (void) _downloadFile->resize(0);
        offset = 0;
    }
    _downloadHash.reset();
    (void) _downloadFile->seek(0);
    (void) _downloadHash.addData(_downloadFile);
    (void) _downloadFile->seek(offset);
    _received = offset;
    emit progressChanged();
    if (offset == size) {
        _downloadFinished();
        return;
    }

    QList<QPair<QByteArray, QByteArray>> headers{{QByteArrayLiteral("X-Expect-Sha256"), sha256.toLatin1()}};
    if (offset > 0) {
        headers.append({QByteArrayLiteral("Range"), QByteArrayLiteral("bytes=") + QByteArray::number(offset) + '-'});
    }
    QNetworkReply *const reply = RoomManager::instance()->_send(_update.value(QStringLiteral("server")).toString(), "GET",
                                                                QStringLiteral("/v1/update/installer"), QByteArray(),
                                                                QByteArray(), nullptr, kTransferIdleMs, headers);
    if (!reply) {
        _downloadFile->close();
        delete _downloadFile;
        _downloadFile = nullptr;
        _setState(QStringLiteral("available"), tr("The room server is not known any more."));
        return;
    }
    _download = reply;
    _downloadCancelled = false;
    _downloadStarted = false;
    qCWarning(AppUpdaterLog) << "Downloading update" << version << "from byte" << offset;

    (void) connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if ((status != 200) && (status != 206)) {
            return;  // an error answer, read when finished
        }
        if (!_downloadStarted) {
            _downloadStarted = true;
            if ((status == 200) && (_downloadFile->pos() > 0)) {
                // The whole file came instead of the rest: start over
                (void) _downloadFile->resize(0);
                (void) _downloadFile->seek(0);
                _downloadHash.reset();
                _received = 0;
            }
        }
        const QByteArray data = reply->readAll();
        if (_downloadFile->write(data) != data.size()) {
            reply->setProperty("nxWriteFailed", _downloadFile->errorString());
            reply->abort();
            return;
        }
        _downloadHash.addData(data);
        _received += data.size();
        emit progressChanged();
    });
    (void) connect(reply, &QNetworkReply::finished, this, &AppUpdater::_downloadFinished);
    _setState(QStringLiteral("downloading"));
}

void AppUpdater::cancel()
{
    if (_download) {
        _downloadCancelled = true;
        _download->abort();
    }
}

void AppUpdater::_downloadFinished()
{
    QString failure;
    bool cancelled = false;
    bool startOver = false;
    bool recheck = false;
    if (QNetworkReply *const reply = _download.data()) {
        _download.clear();
        reply->deleteLater();
        int status = 0;
        QJsonObject json;
        const bool transferred = (reply->error() == QNetworkReply::NoError);
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (!transferred || ((httpStatus != 200) && (httpStatus != 206))) {
            readReply(reply, status, json);
            const QString writeFailed = reply->property("nxWriteFailed").toString();
            if (_downloadCancelled) {
                cancelled = true;
            } else if (!writeFailed.isEmpty()) {
                failure = tr("The update cannot be saved: %1").arg(writeFailed);
            } else if (status == 409) {
                failure = tr("Another update was published meanwhile.");
                startOver = true;
                recheck = true;
            } else if (status == 416) {
                startOver = true;
                failure = tr("The download has to start again. Press Update.");
            } else if ((status == 200) || (status == 206)) {
                failure = tr("The download stopped (%1). Press Update to continue it.").arg(reply->errorString());
            } else {
                failure = tr("The download stopped: %1 Press Update to continue it.")
                              .arg(RoomManager::instance()->_errorText(status, json));
            }
        }
    }

    const qint64 size = static_cast<qint64>(_update.value(QStringLiteral("size")).toDouble());
    const QString partPath = _downloadFile ? _downloadFile->fileName() : QString();
    if (_downloadFile) {
        _downloadFile->close();
        delete _downloadFile;
        _downloadFile = nullptr;
    }
    if (failure.isEmpty() && !cancelled) {
        const QString digest = QString::fromLatin1(_downloadHash.result().toHex());
        if ((_received != size) || (digest != _update.value(QStringLiteral("sha256")).toString())) {
            failure = tr("The downloaded file is damaged. Press Update to download it again.");
            startOver = true;
        }
    }
    if (cancelled || !failure.isEmpty()) {
        if (startOver) {
            (void) QFile::remove(partPath);
        }
        _received = 0;
        emit progressChanged();
        qCWarning(AppUpdaterLog) << "Update download ended:" << (cancelled ? QStringLiteral("cancelled") : failure);
        _setState(QStringLiteral("available"), failure);
        if (recheck) {
            check();
        }
        return;
    }

    const QString path = _installerPath(_update.value(QStringLiteral("version")).toString());
    (void) QFile::remove(path);
    if (!QFile::rename(partPath, path)) {
        _setState(QStringLiteral("available"), tr("The update cannot be saved."));
        return;
    }
    _startInstaller(path);
}

void AppUpdater::_startInstaller(const QString &path)
{
#ifndef Q_OS_WIN
    qCWarning(AppUpdaterLog) << "Downloaded update" << path << "; installing works on Windows only";
    _setState(QStringLiteral("available"), tr("Updates install on Windows only."));
    return;
#endif
    // NX Rover must be gone before the installer replaces it, and must start again as the user, not as the
    // administrator the installer runs as. A small PowerShell script waits for it, starts the installer
    // (Windows asks for permission), waits for it and starts NX Rover again, updated or not.
    const auto quoted = [](QString text) {
        return QLatin1Char('\'') + text.replace(QLatin1Char('\''), QStringLiteral("''")) + QLatin1Char('\'');
    };
    const QString installer = quoted(QDir::toNativeSeparators(path));
    const QString program = quoted(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    const QString script = QStringLiteral("$ErrorActionPreference = 'SilentlyContinue'\n"
                                          "Wait-Process -Id %1 -Timeout 120\n"
                                          "try { Start-Process -FilePath %2 -ArgumentList '/UPDATE' -Verb RunAs -Wait }"
                                          " catch { }\n"
                                          "Start-Process -FilePath %3\n")
                               .arg(QCoreApplication::applicationPid())
                               .arg(installer, program);
    const QByteArray utf16(reinterpret_cast<const char *>(script.utf16()), script.size() * 2);
    const QString powershell = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows")) +
                               QStringLiteral("\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    const QStringList arguments{QStringLiteral("-NoProfile"),      QStringLiteral("-NonInteractive"),
                                QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                                QStringLiteral("-WindowStyle"),     QStringLiteral("Hidden"),
                                QStringLiteral("-EncodedCommand"),  QString::fromLatin1(utf16.toBase64())};
    QProcess process;
    process.setProgram(powershell);
    process.setArguments(arguments);
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= 0x08000000;  // CREATE_NO_WINDOW: no console window flashes up
    });
#endif
    if (!process.startDetached()) {
        _setState(QStringLiteral("available"), tr("The installer could not be started."));
        return;
    }
    qCWarning(AppUpdaterLog) << "Installing update" << _update.value(QStringLiteral("version")).toString() << "from" << path;
    _setState(QStringLiteral("installing"));
    // A moment to read what happens next, then NX Rover closes the usual way
    QTimer::singleShot(2500, this, &AppUpdater::readyToQuit);
}

// ---- publishing ----------------------------------------------------------------------------------------------

void AppUpdater::_setPublish(const QString &state, const QString &error)
{
    _publishState = state;
    _publishError = error;
    emit publishChanged();
}

void AppUpdater::chooseInstaller(const QString &fileOrUrl)
{
    if (_upload) {
        return;
    }
    const QUrl url(fileOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : fileOrUrl;
    const QFileInfo info(path);
    QFile file(path);
    QString version;
    if (file.open(QIODevice::ReadOnly)) {
        version = programVersion(file.read(kVersionSearchBytes));
    }
    _installer = QVariantMap{
        {QStringLiteral("path"), path},
        {QStringLiteral("name"), info.fileName()},
        {QStringLiteral("size"), static_cast<double>(info.size())},
        {QStringLiteral("version"), version},
    };
    _setPublish(QStringLiteral("idle"));
}

void AppUpdater::publish(const QString &notes)
{
    const QString server = _adminServer();
    if (_upload || server.isEmpty() || _installer.isEmpty()) {
        return;
    }
    if (_installer.value(QStringLiteral("version")).toString().isEmpty()) {
        _setPublish(QStringLiteral("error"), tr("This file carries no version: it is not an NX Rover installer, or it was "
                                                "built before updates were supported."));
        return;
    }
    const double maxUpload = _published.value(QStringLiteral("maxUpload")).toDouble();
    if ((maxUpload > 0) && (_installer.value(QStringLiteral("size")).toDouble() > maxUpload)) {
        _setPublish(QStringLiteral("error"),
                    tr("The installer is larger than the server accepts (%1 MB).").arg(qRound(maxUpload / (1024 * 1024))));
        return;
    }
    QFile *const file = new QFile(_installer.value(QStringLiteral("path")).toString());
    if (!file->open(QIODevice::ReadOnly)) {
        const QString error = file->errorString();
        delete file;
        _setPublish(QStringLiteral("error"), tr("The file cannot be read: %1").arg(error));
        return;
    }
    QNetworkReply *const reply = RoomManager::instance()->_send(server, "PUT", QStringLiteral("/v1/update/installer"),
                                                                QByteArrayLiteral("application/octet-stream"), QByteArray(),
                                                                file, kTransferIdleMs,
                                                                {{QByteArrayLiteral("X-Notes"),
                                                                  QUrl::toPercentEncoding(notes.trimmed())}});
    if (!reply) {
        delete file;
        _setPublish(QStringLiteral("error"), tr("The room server is not known any more."));
        return;
    }
    file->setParent(reply);
    _upload = reply;
    _publishProgress = 0;
    emit publishProgressChanged();
    _setPublish(QStringLiteral("uploading"));

    (void) connect(reply, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64 total) {
        // Qt reports 0 of 0 once the request is done; the bar keeps its last value
        if (total > 0) {
            _publishProgress = static_cast<double>(sent) / static_cast<double>(total);
            emit publishProgressChanged();
        }
    });
    (void) connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        _upload.clear();
        int status = 0;
        QJsonObject json;
        readReply(reply, status, json);
        if (status == 200) {
            _published = publishedView(json);
            _published[QStringLiteral("by")] = RoomManager::instance()->deviceName();
            qCWarning(AppUpdaterLog) << "Published update" << _published.value(QStringLiteral("version")).toString();
            _setPublish(QStringLiteral("done"));
            // This computer may be behind the version it just published
            check();
        } else if (reply->error() == QNetworkReply::OperationCanceledError) {
            _setPublish(QStringLiteral("idle"));
        } else {
            const QString code = json.value(QLatin1String("error")).toString();
            QString error;
            if (code == QLatin1String("not_installer")) {
                error = tr("The server did not accept the file: it is not a Windows program.");
            } else if (code == QLatin1String("no_version")) {
                error = tr("The server did not accept the file: it carries no version.");
            } else if (code == QLatin1String("not_server_admin")) {
                error = tr("Only a server admin can publish updates.");
            } else {
                error = RoomManager::instance()->_errorText(status, json);
            }
            _setPublish(QStringLiteral("error"), error);
        }
    });
}

void AppUpdater::cancelPublish()
{
    if (_upload) {
        _upload->abort();
    }
}

void AppUpdater::unpublish()
{
    const QString server = _adminServer();
    if (_upload || server.isEmpty()) {
        return;
    }
    RoomManager::instance()->_request(server, QStringLiteral("DELETE"), QStringLiteral("/v1/update"), QJsonObject(),
                                      [this](int status, const QJsonObject &json) {
        if (status == 200) {
            _published = publishedView(json);
            _setPublish(QStringLiteral("idle"));
        } else {
            _setPublish(QStringLiteral("error"), RoomManager::instance()->_errorText(status, json));
        }
    });
}
