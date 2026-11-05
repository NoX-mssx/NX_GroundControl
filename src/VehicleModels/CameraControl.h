#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <QtCore/QVariantMap>
#include <QtQmlIntegration/QtQmlIntegration>

#include <functional>

class QNetworkAccessManager;

/// Sends control commands to the IP cameras of a vehicle model (see VehicleModelManager for the camera map).
///
/// Dahua cameras are driven through their HTTP CGI interface, which covers day/night profiles and the
/// illuminator. Uniview (UNV) cameras are driven through their LAPI interface for day/night and the
/// illuminator, falling back to ONVIF for day/night. Every other camera type goes through ONVIF, which
/// only standardises the IR cut filter (day/night) and reboot; the illuminator of Hikvision cameras is
/// switched through ISAPI, and that of generic cameras is tried through LAPI, as many are Uniview inside.
class CameraControl : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    /// Outcome of the most recent command, for display
    Q_PROPERTY(QString  status  READ status NOTIFY statusChanged)
    Q_PROPERTY(bool     busy    READ busy   NOTIFY busyChanged)

public:
    explicit CameraControl(QObject *parent = nullptr);

    QString status() const { return _status; }
    bool busy() const { return _pending > 0; }

    /// @param command one of "day", "night", "irOn", "irOff", "reboot"
    Q_INVOKABLE void run(const QVariantMap &camera, const QString &command);

    /// WS-Security UsernameToken digest: Base64(SHA-1(nonce + created + password))
    static QByteArray passwordDigest(const QByteArray &nonce, const QByteArray &created, const QByteArray &password);

signals:
    void statusChanged();
    void busyChanged();
    /// A command did not go through; the drive screen shows it under the "!" in the top bar
    void commandFailed(const QString &message);
    /// A command ended; @p host is the camera's address as given in the vehicle model
    void commandFinished(const QString &host, const QString &command, bool ok);
    /// The camera switched its day/night mode along with its illuminator; @p command is "day" or "night"
    void dayNightFollowed(const QString &host, const QString &command);

private:
    struct Camera {
        QString name;
        QString host;
        QString user;
        QString password;
    };
    /// One command on its way: what the user sees of it, and what it was
    struct Job {
        QString label;
        QString host;
        QString command;
    };
    using Reply = std::function<void(bool ok, const QByteArray &body)>;
    using Done = std::function<void(bool ok, const QString &error)>;
    /// Edits the data of a LAPI resource before it is written back; false if the data lacks what it changes
    using LapiChange = std::function<bool(QJsonObject &data)>;

    void _runDahua(const Camera &camera, const QString &command, const Job &job);
    void _runHikvisionIlluminator(const Camera &camera, bool on, const Job &job);
    void _runOnvif(const Camera &camera, const QString &command, const Job &job);
    void _onvifSetIrCutFilter(const Camera &camera, qint64 clockOffsetSecs, bool day, const Job &job);
    /// @param uniview the camera type says Uniview; otherwise a failure reads as "not available"
    void _runUniview(const Camera &camera, const QString &command, const Job &job, bool uniview);
    void _univiewSetLamp(const Camera &camera, bool on, const Done &done);
    void _univiewSetDayNight(const Camera &camera, bool day, const Done &done);
    /// Reads a LAPI resource, changes its data and writes it back; @p paths are tried in turn until one reads
    void _univiewUpdate(const Camera &camera, const QStringList &paths, const LapiChange &change, const Done &done);

    /// HTTP GET with digest authentication
    void _get(const Camera &camera, const QUrl &url, const Reply &reply);
    /// HTTP PUT with digest authentication
    void _put(const Camera &camera, const QUrl &url, const QByteArray &body, const QString &contentType, const Reply &reply);
    /// SOAP request; with @p authenticate a WS-Security header for the camera's clock is added
    void _soap(const Camera &camera, const QUrl &url, const QString &body, bool authenticate, qint64 clockOffsetSecs,
               const Reply &reply);

    void _begin();
    void _finish(const Job &job, bool ok, const QString &detail = QString());

    QNetworkAccessManager *_network = nullptr;
    QString _status;
    int _pending = 0;
};
