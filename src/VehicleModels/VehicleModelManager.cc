#include "VehicleModelManager.h"
#include "QGCLoggingCategory.h"
#include "SecretProtector.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QUrl>

QGC_LOGGING_CATEGORY(VehicleModelManagerLog, "VehicleModels.VehicleModelManager")

namespace {

constexpr const char *kFileName = "VehicleModels.json";
constexpr const char *kModelsKey = "models";
constexpr const char *kNameKey = "name";
constexpr const char *kCamerasKey = "cameras";
constexpr const char *kPasswordKey = "password";
constexpr const char *kPasswordProtectedKey = "passwordProtected";

struct CameraType {
    const char *name;
    const char *mainPath;
    const char *secondaryPath;
};

// Stream paths are the vendors' documented defaults; a camera with a changed channel layout needs the
// URLs edited by hand, which the settings page allows.
constexpr CameraType kCameraTypes[] = {
    {"Generic", "", ""},
    {"Dahua (DH-IPC-HFW2449T)", "/cam/realmonitor?channel=1&subtype=0", "/cam/realmonitor?channel=1&subtype=1"},
    {"Dahua (DH-IPC-HDW5541TM)", "/cam/realmonitor?channel=1&subtype=0", "/cam/realmonitor?channel=1&subtype=1"},
    {"UNV (Ultra 265)", "/media/video1", "/media/video2"},
    {"Hikvision (DS-2CD)", "/Streaming/Channels/101", "/Streaming/Channels/102"},
};

/// Swaps the camera password field between its in-memory and on-disk form.
QVariantList convertCameraPasswords(const QVariantList &cameras, bool forDisk)
{
    QVariantList converted;
    for (const QVariant &cameraVar : cameras) {
        QVariantMap camera = cameraVar.toMap();
        if (forDisk) {
            camera[kPasswordProtectedKey] = SecretProtector::protect(camera.value(kPasswordKey).toString());
            camera.remove(kPasswordKey);
        } else {
            camera[kPasswordKey] = SecretProtector::unprotect(camera.value(kPasswordProtectedKey).toString());
            camera.remove(kPasswordProtectedKey);
        }
        converted.append(camera);
    }
    return converted;
}

} // namespace

VehicleModelManager::VehicleModelManager(QObject *parent)
    : QObject(parent)
{
    _load();
}

QStringList VehicleModelManager::modelNames() const
{
    QStringList names;
    for (const QVariant &modelVar : _models) {
        names.append(modelVar.toMap().value(kNameKey).toString());
    }
    return names;
}

QStringList VehicleModelManager::cameraTypes() const
{
    QStringList types;
    for (const CameraType &type : kCameraTypes) {
        types.append(QString::fromLatin1(type.name));
    }
    return types;
}

QVariantMap VehicleModelManager::model(const QString &name) const
{
    const int index = _indexOf(name);
    return (index < 0) ? QVariantMap() : _models.at(index).toMap();
}

QString VehicleModelManager::saveModel(const QString &originalName, const QVariantMap &model)
{
    const QString name = model.value(kNameKey).toString().trimmed();
    if (name.isEmpty()) {
        return tr("Enter a model name.");
    }

    const int originalIndex = _indexOf(originalName);
    const int nameIndex = _indexOf(name);
    if ((nameIndex >= 0) && (nameIndex != originalIndex)) {
        return tr("A model named \"%1\" already exists.").arg(name);
    }

    QVariantMap stored = model;
    stored[kNameKey] = name;

    const QVariantList previous = _models;
    if (originalIndex >= 0) {
        _models[originalIndex] = stored;
    } else {
        _models.append(stored);
    }

    if (!_save()) {
        _models = previous;
        return tr("Could not write the vehicle models file.");
    }

    emit modelsChanged();
    return QString();
}

void VehicleModelManager::deleteModel(const QString &name)
{
    const int index = _indexOf(name);
    if (index < 0) {
        return;
    }

    const QVariantList previous = _models;
    _models.removeAt(index);
    if (!_save()) {
        _models = previous;
        return;
    }

    emit modelsChanged();
}

QVariantMap VehicleModelManager::cameraUrls(const QString &type, const QString &ip, const QString &user,
                                            const QString &password) const
{
    QVariantMap urls = {{QStringLiteral("mainUrl"), QString()}, {QStringLiteral("secondaryUrl"), QString()}};

    for (const CameraType &cameraType : kCameraTypes) {
        if ((type != QString::fromLatin1(cameraType.name)) || (cameraType.mainPath[0] == '\0') || ip.isEmpty()) {
            continue;
        }

        QString credentials;
        if (!user.isEmpty()) {
            // Credentials are percent-encoded so characters like '@' or ':' in a password keep the URL valid.
            credentials = QString::fromLatin1(QUrl::toPercentEncoding(user));
            if (!password.isEmpty()) {
                credentials += QLatin1Char(':') + QString::fromLatin1(QUrl::toPercentEncoding(password));
            }
            credentials += QLatin1Char('@');
        }

        const QString base = QStringLiteral("rtsp://%1%2:554").arg(credentials, ip);
        urls[QStringLiteral("mainUrl")] = base + QString::fromLatin1(cameraType.mainPath);
        urls[QStringLiteral("secondaryUrl")] = base + QString::fromLatin1(cameraType.secondaryPath);
        break;
    }

    return urls;
}

int VehicleModelManager::_indexOf(const QString &name) const
{
    if (name.isEmpty()) {
        return -1;
    }

    for (int i = 0; i < _models.count(); i++) {
        if (_models.at(i).toMap().value(kNameKey).toString() == name) {
            return i;
        }
    }
    return -1;
}

QString VehicleModelManager::_filePath() const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QString::fromLatin1(kFileName));
}

void VehicleModelManager::_load()
{
    QFile file(_filePath());
    if (!file.exists()) {
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(VehicleModelManagerLog) << "Unable to open" << file.fileName() << file.errorString();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(VehicleModelManagerLog) << "Unable to parse" << file.fileName() << parseError.errorString();
        return;
    }

    _models.clear();
    const QVariantList stored = document.object().value(QLatin1String(kModelsKey)).toArray().toVariantList();
    for (const QVariant &modelVar : stored) {
        QVariantMap model = modelVar.toMap();
        model[kCamerasKey] = convertCameraPasswords(model.value(kCamerasKey).toList(), false);
        _models.append(model);
    }
}

bool VehicleModelManager::_save() const
{
    QVariantList stored;
    for (const QVariant &modelVar : _models) {
        QVariantMap model = modelVar.toMap();
        model[kCamerasKey] = convertCameraPasswords(model.value(kCamerasKey).toList(), true);
        stored.append(model);
    }

    const QString path = _filePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qCWarning(VehicleModelManagerLog) << "Unable to create directory for" << path;
        return false;
    }

    QJsonObject root;
    root[QLatin1String(kModelsKey)] = QJsonArray::fromVariantList(stored);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(VehicleModelManagerLog) << "Unable to open" << path << file.errorString();
        return false;
    }
    (void) file.write(QJsonDocument(root).toJson());
    if (!file.commit()) {
        qCWarning(VehicleModelManagerLog) << "Unable to write" << path << file.errorString();
        return false;
    }
    return true;
}
