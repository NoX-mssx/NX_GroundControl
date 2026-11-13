#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <QtCore/QVariantMap>
#include <QtQmlIntegration/QtQmlIntegration>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

/// Sends control commands to the IP cameras of a vehicle model (see VehicleModelManager for the camera map).
///
/// Day/night and the illuminator are separate commands: switching the light never changes the mode.
///
/// TRGN-Video cameras are driven through the board's HTTPS API (see _runTrgn): its address, user name and
/// password are the camera's IP, user and password fields.
///
/// Dahua cameras are driven through their HTTP CGI interface, which covers day/night profiles and the
/// illuminator. Hikvision cameras are driven through ISAPI: the IR cut filter for day/night and the supplement
/// light, falling back to ONVIF for day/night (ONVIF is off by default on Hikvision). Uniview (UNV) cameras
/// are driven through their LAPI interface for day/night and the infrared lamp, falling back to ONVIF for
/// day/night. Every other camera type goes through ONVIF, which only standardises the IR cut filter
/// (day/night) and reboot; the illuminator of generic cameras is tried through LAPI, as many are Uniview inside.
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

    /// The picture settings a TRGN-Video camera gets with each day/night switch, with their default values
    Q_INVOKABLE QVariantMap trgnImageDefaults() const;

    /// WS-Security UsernameToken digest: Base64(SHA-1(nonce + created + password))
    static QByteArray passwordDigest(const QByteArray &nonce, const QByteArray &created, const QByteArray &password);

signals:
    void statusChanged();
    void busyChanged();
    /// A command did not go through; the drive screen shows it under the "!" in the top bar
    void commandFailed(const QString &message);
    /// A command ended; @p host is the camera's address as given in the vehicle model
    void commandFinished(const QString &host, const QString &command, bool ok);

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
    void _runHikvision(const Camera &camera, const QString &command, const Job &job);
    /// @param filterType "day" or "night"
    void _hikvisionSetIrcut(const Camera &camera, const QString &filterType, const Done &done);
    void _hikvisionSetLight(const Camera &camera, bool on, const Done &done);
    void _runOnvif(const Camera &camera, const QString &command, const Job &job);
    void _onvifSetIrCutFilter(const Camera &camera, qint64 clockOffsetSecs, bool day, const Job &job);
    /// @param uniview the camera type says Uniview; otherwise a failure reads as "not available"
    void _runUniview(const Camera &camera, const QString &command, const Job &job, bool uniview);
    void _univiewSetLamp(const Camera &camera, bool on, const Done &done);
    void _univiewSetDayNight(const Camera &camera, bool day, const Done &done);
    /// Reads a LAPI resource, changes its data and writes it back; @p paths are tried in turn until one reads
    void _univiewUpdate(const Camera &camera, const QStringList &paths, const LapiChange &change, const Done &done);

    /// The board API behind TRGN-Video cameras: address (https://host:port), user name and password
    struct TrgnApi {
        QString base;
        QString user;
        QString password;
    };
    /// @param fatal the API cannot be used at all (no answer, or the login failed): the other cameras are not tried
    using TrgnDone = std::function<void(bool ok, bool fatal, const QString &error)>;
    /// Day/night on every TRGN-Video camera of the vehicle behind the same API, as the board's interface does
    void _runTrgn(const QVariantMap &camera, const QString &command, const Job &job);
    /// Sends the picture settings and then day/night to @p cameras from @p index on, one after the other
    void _trgnDayNight(const TrgnApi &api, const QVariantList &cameras, int index, bool night, const Job &job,
                       const QStringList &failed);
    void _trgnLogin(const TrgnApi &api, const TrgnDone &done);
    /// POSTs @p body to the API, logging in first when there is no session; a session the API refuses
    /// (401) is renewed and the request repeated once
    void _trgnPost(const TrgnApi &api, const QString &path, const QJsonObject &body, const TrgnDone &done,
                   bool retried = false);
    QNetworkReply *_trgnSend(const TrgnApi &api, const QString &path, const QJsonObject &body, const QByteArray &token);

    /// HTTP GET with digest authentication
    void _get(const Camera &camera, const QUrl &url, const Reply &reply);
    /// Remembers why the last request to @p host failed (empty: it did not), for the message of a failed command
    void _noteRequest(const QString &host, const QNetworkReply *reply);

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
    QHash<QString, QString> _lastFailure;     ///< Why the last request to a camera host failed
    QHash<QString, QByteArray> _trgnTokens;   ///< auth_token per board API and user, for the session
};
