#pragma once

#include <QtCore/QCryptographicHash>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QVariantMap>
#include <QtQmlIntegration/QtQmlIntegration>

class QFile;
class QJSEngine;
class QNetworkReply;
class QQmlEngine;

/// Updates of NX Rover through the room server (NX Hub).
///
/// A server admin publishes an installer on the hub; every computer registered with the hub (admins and
/// operators) checks it at start and in Settings → Updates. The installer is downloaded next to the program's
/// data, continued where it stopped after a lost connection, and checked against the SHA-256 the hub gives.
/// Installing closes NX Rover and starts the installer with /UPDATE (no questions, the installed folder, the
/// window closes when done); Windows asks for permission, and NX Rover starts again afterwards.
///
/// The version of an installer is read from its file properties (VS_VERSION_INFO), by the hub and here.
class AppUpdater : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    /// Version of this program, "1.0.16"
    Q_PROPERTY(QString      currentVersion  READ currentVersion                 CONSTANT)
    /// "idle" (not checked), "checking", "noServer", "upToDate", "available", "downloading", "installing", "error"
    Q_PROPERTY(QString      state           READ state                          NOTIFY stateChanged)
    /// A newer version on the hub: { version, size, notes, published (seconds since 1970) }; empty when none
    Q_PROPERTY(QVariantMap  update          READ update                         NOTIFY stateChanged)
    /// What went wrong last (check, download or install), empty when nothing
    Q_PROPERTY(QString      error           READ error                          NOTIFY stateChanged)
    /// Download progress 0..1 and bytes received
    Q_PROPERTY(double       progress        READ progress                       NOTIFY progressChanged)
    Q_PROPERTY(qint64       received        READ received                       NOTIFY progressChanged)

    /// This computer is a server admin of a hub, so it may publish installers
    Q_PROPERTY(bool         canPublish      READ canPublish                     NOTIFY publishChanged)
    /// What that hub has published: { available, version, size, notes, published, by, maxUpload }; empty until
    /// a check answered
    Q_PROPERTY(QVariantMap  published       READ published                      NOTIFY publishChanged)
    /// The installer chosen for publishing: { path, name, size, version (empty: the file carries no version) }
    Q_PROPERTY(QVariantMap  installer       READ installer                      NOTIFY publishChanged)
    /// "idle", "uploading", "done" or "error"
    Q_PROPERTY(QString      publishState    READ publishState                   NOTIFY publishChanged)
    Q_PROPERTY(QString      publishError    READ publishError                   NOTIFY publishChanged)
    Q_PROPERTY(double       publishProgress READ publishProgress                NOTIFY publishProgressChanged)

public:
    explicit AppUpdater(QObject *parent = nullptr);
    ~AppUpdater() override;

    static AppUpdater *instance();
    static AppUpdater *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    /// < 0, 0 or > 0 as version a is older, the same or newer than b ("v1.0.16", "1.0.16.0", "1.0.16-rc1")
    static int compareVersions(const QString &a, const QString &b);
    /// Product version from the first bytes of a Windows program ("1.0.16"), empty when it carries none
    static QString programVersion(const QByteArray &head);

    QString currentVersion() const;
    QString state() const { return _state; }
    QVariantMap update() const { return _update; }
    QString error() const { return _error; }
    double progress() const;
    qint64 received() const { return _received; }
    bool canPublish() const;
    QVariantMap published() const { return _published; }
    QVariantMap installer() const { return _installer; }
    QString publishState() const { return _publishState; }
    QString publishError() const { return _publishError; }
    double publishProgress() const { return _publishProgress; }

    /// Asks every hub this computer is registered with for its update
    Q_INVOKABLE void check();
    /// Downloads the update (continuing an earlier download) and installs it; NX Rover then closes
    Q_INVOKABLE void install();
    /// Stops the download; what arrived is kept and continued next time
    Q_INVOKABLE void cancel();

    /// Server admin: picks the installer to publish (a local path or a file URL) and reads its version
    Q_INVOKABLE void chooseInstaller(const QString &fileOrUrl);
    Q_INVOKABLE void publish(const QString &notes);
    Q_INVOKABLE void cancelPublish();
    /// Server admin: removes the published installer from the hub
    Q_INVOKABLE void unpublish();

signals:
    void stateChanged();
    void progressChanged();
    void publishChanged();
    void publishProgressChanged();
    /// The check at start found a newer version: the main window offers it
    void offerUpdate();
    /// The installer is on its way: the main window closes NX Rover the usual way
    void readyToQuit();

private:
    void _setState(const QString &state, const QString &error = QString());
    void _checkFinished();
    QString _adminServer() const;
    QString _downloadDirectory() const;
    QString _installerPath(const QString &version) const;
    void _removeOldDownloads() const;
    void _downloadFinished();
    void _startInstaller(const QString &path);
    void _setPublish(const QString &state, const QString &error = QString());

    QString _state = QStringLiteral("idle");
    QString _error;
    QVariantMap _update;            ///< plus server and sha256, which QML does not need
    bool _startupCheck = false;
    int _checksRunning = 0;
    QVariantMap _found;             ///< newest update the running check has seen
    QString _checkError;
    bool _checkAnswered = false;

    QPointer<QNetworkReply> _download;
    QFile *_downloadFile = nullptr;
    QCryptographicHash _downloadHash{QCryptographicHash::Sha256};
    qint64 _received = 0;
    bool _downloadCancelled = false;
    bool _downloadStarted = false;

    QVariantMap _published;
    QVariantMap _installer;
    QString _publishState = QStringLiteral("idle");
    QString _publishError;
    double _publishProgress = 0;
    QPointer<QNetworkReply> _upload;

    static constexpr int kStartupCheckDelayMs = 5000;
    static constexpr int kCheckTimeoutMs = 15000;
    /// Download and upload: the request is dropped after this long without a byte
    static constexpr int kTransferIdleMs = 60000;
    static constexpr qint64 kVersionSearchBytes = 1024 * 1024;
};
