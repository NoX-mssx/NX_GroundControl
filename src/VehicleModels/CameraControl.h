#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtCore/QVariantMap>
#include <QtQmlIntegration/QtQmlIntegration>

#include <functional>

class QNetworkAccessManager;

/// Sends control commands to the IP cameras of a vehicle model (see VehicleModelManager for the camera map).
///
/// Dahua cameras are driven through their HTTP CGI interface, which covers day/night profiles and the
/// illuminator. Every other camera type goes through ONVIF, which only standardises the IR cut filter
/// (day/night) and reboot; the illuminator of Hikvision cameras is switched through ISAPI, on other
/// types it is reported as unsupported.
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

private:
    struct Camera {
        QString name;
        QString host;
        QString user;
        QString password;
    };
    using Reply = std::function<void(bool ok, const QByteArray &body)>;

    void _runDahua(const Camera &camera, const QString &command, const QString &label);
    void _runHikvisionIlluminator(const Camera &camera, bool on, const QString &label);
    void _runOnvif(const Camera &camera, const QString &command, const QString &label);
    void _onvifSetIrCutFilter(const Camera &camera, qint64 clockOffsetSecs, bool day, const QString &label);

    /// HTTP GET with digest authentication
    void _get(const Camera &camera, const QUrl &url, const Reply &reply);
    /// HTTP PUT of an XML body with digest authentication
    void _put(const Camera &camera, const QUrl &url, const QByteArray &body, const Reply &reply);
    /// SOAP request; with @p authenticate a WS-Security header for the camera's clock is added
    void _soap(const Camera &camera, const QUrl &url, const QString &body, bool authenticate, qint64 clockOffsetSecs,
               const Reply &reply);

    void _begin();
    void _finish(const QString &label, bool ok, const QString &detail = QString());

    QNetworkAccessManager *_network = nullptr;
    QString _status;
    int _pending = 0;
};
