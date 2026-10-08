#include "CameraControl.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QRandomGenerator>
#include <QtCore/QRegularExpression>
#include <QtCore/QStringList>
#include <QtCore/QTimeZone>
#include <QtCore/QXmlStreamReader>
#include <QtNetwork/QAuthenticator>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <memory>

QGC_LOGGING_CATEGORY(NxCameraControlLog, "VehicleModels.CameraControl")

namespace {

constexpr int kTimeoutMs = 8000;
constexpr const char *kAuthAttemptedProperty = "nxAuthAttempted";

QString commandLabel(const QString &command)
{
    if (command == QLatin1String("day")) {
        return CameraControl::tr("Day Mode");
    }
    if (command == QLatin1String("night")) {
        return CameraControl::tr("Night Mode");
    }
    if (command == QLatin1String("irOn")) {
        return CameraControl::tr("IR Light On");
    }
    if (command == QLatin1String("irOff")) {
        return CameraControl::tr("IR Light Off");
    }
    return CameraControl::tr("Reboot");
}

/// Text of the first element with the given local name, ignoring namespaces.
QString xmlText(const QByteArray &xml, QLatin1String localName, QLatin1String parentLocalName = QLatin1String())
{
    QXmlStreamReader reader(xml);
    QStringList path;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString name = reader.name().toString();
            if ((name == localName) && (parentLocalName.isEmpty() || (!path.isEmpty() && (path.last() == parentLocalName)))) {
                return reader.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
            }
            path.append(name);
        } else if (token == QXmlStreamReader::EndElement) {
            if (!path.isEmpty()) {
                path.removeLast();
            }
        }
    }
    return QString();
}

/// Value of an attribute of the first element with the given local name.
QString xmlAttribute(const QByteArray &xml, QLatin1String localName, QLatin1String attribute)
{
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        if ((reader.readNext() == QXmlStreamReader::StartElement) && (reader.name() == localName)) {
            return reader.attributes().value(attribute).toString();
        }
    }
    return QString();
}

/// Camera clock minus this computer's clock, from a GetSystemDateAndTime response. Cameras on a vehicle
/// often have no time source, and WS-Security rejects tokens whose timestamp is far from the camera's own.
qint64 parseClockOffsetSecs(const QByteArray &xml)
{
    QXmlStreamReader reader(xml);
    bool inUtc = false;
    QHash<QString, int> fields;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString name = reader.name().toString();
            if (name == QLatin1String("UTCDateTime")) {
                inUtc = true;
            } else if (inUtc && (name != QLatin1String("Time")) && (name != QLatin1String("Date"))) {
                fields[name] = reader.readElementText().toInt();
            }
        } else if ((token == QXmlStreamReader::EndElement) && (reader.name() == QLatin1String("UTCDateTime"))) {
            break;
        }
    }

    const QDateTime cameraTime(QDate(fields.value(QStringLiteral("Year")), fields.value(QStringLiteral("Month")), fields.value(QStringLiteral("Day"))),
                               QTime(fields.value(QStringLiteral("Hour")), fields.value(QStringLiteral("Minute")), fields.value(QStringLiteral("Second"))),
                               QTimeZone::utc());
    return cameraTime.isValid() ? QDateTime::currentDateTimeUtc().secsTo(cameraTime) : 0;
}

QString soapEnvelope(const QString &header, const QString &body)
{
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                          "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">"
                          "<s:Header>%1</s:Header><s:Body>%2</s:Body></s:Envelope>").arg(header, body);
}

} // namespace

CameraControl::CameraControl(QObject *parent)
    : QObject(parent)
    , _network(new QNetworkAccessManager(this))
{
    // Digest (Dahua CGI) credentials are supplied once per request; a second challenge means they are wrong.
    (void) connect(_network, &QNetworkAccessManager::authenticationRequired, this,
                   [](QNetworkReply *reply, QAuthenticator *authenticator) {
        if (reply->property(kAuthAttemptedProperty).toBool()) {
            return;
        }
        reply->setProperty(kAuthAttemptedProperty, true);
        authenticator->setUser(reply->property("nxUser").toString());
        authenticator->setPassword(reply->property("nxPassword").toString());
    });
}

QByteArray CameraControl::passwordDigest(const QByteArray &nonce, const QByteArray &created, const QByteArray &password)
{
    return QCryptographicHash::hash(nonce + created + password, QCryptographicHash::Sha1).toBase64();
}

void CameraControl::run(const QVariantMap &camera, const QString &command)
{
    const Camera target{
        camera.value(QStringLiteral("name")).toString(),
        camera.value(QStringLiteral("ip")).toString().trimmed(),
        camera.value(QStringLiteral("user")).toString(),
        camera.value(QStringLiteral("password")).toString(),
    };
    const QString label = QStringLiteral("%1: %2").arg(target.name, commandLabel(command));

    _begin();
    if (target.host.isEmpty()) {
        _finish(label, false, tr("the camera has no IP address in the vehicle model"));
        return;
    }

    const QString type = camera.value(QStringLiteral("type")).toString();
    const bool illuminator = (command == QLatin1String("irOn")) || (command == QLatin1String("irOff"));
    if (type.startsWith(QLatin1String("Dahua"))) {
        _runDahua(target, command, label);
    } else if (illuminator && type.startsWith(QLatin1String("Hikvision"))) {
        _runHikvisionIlluminator(target, command == QLatin1String("irOn"), label);
    } else {
        _runOnvif(target, command, label);
    }
}

void CameraControl::_runDahua(const Camera &camera, const QString &command, const QString &label)
{
    QStringList queries;
    if ((command == QLatin1String("day")) || (command == QLatin1String("night"))) {
        // Mode 0 pins the camera to the profile in Config[0]: 0 = day profile, 1 = night profile.
        queries.append(QStringLiteral("/cgi-bin/configManager.cgi?action=setConfig&VideoInMode[0].Mode=0&VideoInMode[0].Config[0]=%1")
                           .arg((command == QLatin1String("day")) ? 0 : 1));
    } else if ((command == QLatin1String("irOn")) || (command == QLatin1String("irOff"))) {
        // Newer firmware keeps the illuminator per profile (day, night, normal) under Lighting_V2, older
        // firmware under Lighting. Both are sent; the camera rejects the one it does not have.
        const QString mode = (command == QLatin1String("irOn")) ? QStringLiteral("Manual") : QStringLiteral("Off");
        queries.append(QStringLiteral("/cgi-bin/configManager.cgi?action=setConfig"
                                      "&Lighting_V2[0][0][0].Mode=%1&Lighting_V2[0][1][0].Mode=%1&Lighting_V2[0][2][0].Mode=%1").arg(mode));
        queries.append(QStringLiteral("/cgi-bin/configManager.cgi?action=setConfig&Lighting[0][0].Mode=%1").arg(mode));
    } else {
        queries.append(QStringLiteral("/cgi-bin/magicBox.cgi?action=reboot"));
    }

    // The command succeeded if the camera accepted at least one of its requests.
    auto remaining = std::make_shared<int>(queries.count());
    auto accepted = std::make_shared<bool>(false);
    for (const QString &query : std::as_const(queries)) {
        const QUrl url(QStringLiteral("http://%1%2").arg(camera.host, query));
        _get(camera, url, [this, label, remaining, accepted](bool ok, const QByteArray &body) {
            if (ok && body.trimmed().startsWith("OK")) {
                *accepted = true;
            }
            if (--(*remaining) == 0) {
                _finish(label, *accepted, *accepted ? QString() : tr("the camera did not accept the request"));
            }
        });
    }
}

void CameraControl::_runHikvisionIlluminator(const Camera &camera, bool on, const QString &label)
{
    // ISAPI keeps the illuminator under SupplementLight. The current settings are read and written back
    // with only the mode changed, because the accepted fields differ between models.
    const QUrl url(QStringLiteral("http://%1/ISAPI/Image/channels/1/SupplementLight").arg(camera.host));
    _get(camera, url, [this, camera, on, label, url](bool ok, const QByteArray &body) {
        static const QRegularExpression modeElement(QStringLiteral("<supplementLightMode>[^<]*</supplementLightMode>"));
        QString settings = QString::fromUtf8(body);
        if (!ok || !settings.contains(modeElement)) {
            _finish(label, false, tr("the camera has no switchable illuminator; it follows day/night mode"));
            return;
        }

        settings.replace(modeElement, QStringLiteral("<supplementLightMode>%1</supplementLightMode>")
                                          .arg(on ? QStringLiteral("irLight") : QStringLiteral("close")));
        if (on) {
            // A brightness of 0 would leave the light dark in manual brightness mode.
            static const QRegularExpression zeroBrightness(QStringLiteral("<irLightBrightness>0</irLightBrightness>"));
            settings.replace(zeroBrightness, QStringLiteral("<irLightBrightness>100</irLightBrightness>"));
        }

        _put(camera, url, settings.toUtf8(), [this, label](bool putOk, const QByteArray &putBody) {
            const QString detail = xmlText(putBody, QLatin1String("subStatusCode"));
            _finish(label, putOk, putOk ? QString() : (detail.isEmpty() ? tr("the camera did not accept the request") : detail));
        });
    });
}

void CameraControl::_runOnvif(const Camera &camera, const QString &command, const QString &label)
{
    if ((command == QLatin1String("irOn")) || (command == QLatin1String("irOff"))) {
        _finish(label, false, tr("not available for this camera type; the illuminator follows day/night mode"));
        return;
    }

    const QUrl deviceUrl(QStringLiteral("http://%1/onvif/device_service").arg(camera.host));
    const QString timeRequest = QStringLiteral("<GetSystemDateAndTime xmlns=\"http://www.onvif.org/ver10/device/wsdl\"/>");

    _soap(camera, deviceUrl, timeRequest, false, 0, [this, camera, command, label, deviceUrl](bool ok, const QByteArray &body) {
        if (!ok) {
            _finish(label, false, tr("the camera does not answer ONVIF requests"));
            return;
        }
        const qint64 offset = parseClockOffsetSecs(body);

        if (command == QLatin1String("reboot")) {
            const QString request = QStringLiteral("<SystemReboot xmlns=\"http://www.onvif.org/ver10/device/wsdl\"/>");
            _soap(camera, deviceUrl, request, true, offset, [this, label](bool rebootOk, const QByteArray &rebootBody) {
                _finish(label, rebootOk, rebootOk ? QString() : xmlText(rebootBody, QLatin1String("Text")));
            });
            return;
        }

        _onvifSetIrCutFilter(camera, offset, command == QLatin1String("day"), label);
    });
}

void CameraControl::_onvifSetIrCutFilter(const Camera &camera, qint64 clockOffsetSecs, bool day, const QString &label)
{
    const QUrl deviceUrl(QStringLiteral("http://%1/onvif/device_service").arg(camera.host));
    const QString capabilitiesRequest = QStringLiteral(
        "<GetCapabilities xmlns=\"http://www.onvif.org/ver10/device/wsdl\"><Category>All</Category></GetCapabilities>");

    _soap(camera, deviceUrl, capabilitiesRequest, true, clockOffsetSecs,
          [this, camera, clockOffsetSecs, day, label](bool ok, const QByteArray &body) {
        const QUrl mediaUrl(xmlText(body, QLatin1String("XAddr"), QLatin1String("Media")));
        const QUrl imagingUrl(xmlText(body, QLatin1String("XAddr"), QLatin1String("Imaging")));
        if (!ok || !mediaUrl.isValid() || mediaUrl.isEmpty() || !imagingUrl.isValid() || imagingUrl.isEmpty()) {
            const QString fault = xmlText(body, QLatin1String("Text"));
            _finish(label, false, fault.isEmpty() ? tr("the camera did not report its imaging service") : fault);
            return;
        }

        const QString sourcesRequest = QStringLiteral("<GetVideoSources xmlns=\"http://www.onvif.org/ver10/media/wsdl\"/>");
        _soap(camera, mediaUrl, sourcesRequest, true, clockOffsetSecs,
              [this, camera, clockOffsetSecs, day, label, imagingUrl](bool sourcesOk, const QByteArray &sourcesBody) {
            const QString token = xmlAttribute(sourcesBody, QLatin1String("VideoSources"), QLatin1String("token"));
            if (!sourcesOk || token.isEmpty()) {
                _finish(label, false, tr("the camera did not report a video source"));
                return;
            }

            // IR cut filter ON = filter in the light path = colour (day); OFF = black and white (night).
            const QString settingsRequest = QStringLiteral(
                "<SetImagingSettings xmlns=\"http://www.onvif.org/ver20/imaging/wsdl\">"
                "<VideoSourceToken>%1</VideoSourceToken>"
                "<ImagingSettings><IrCutFilter xmlns=\"http://www.onvif.org/ver10/schema\">%2</IrCutFilter></ImagingSettings>"
                "<ForcePersistence>true</ForcePersistence>"
                "</SetImagingSettings>").arg(token.toHtmlEscaped(), day ? QStringLiteral("ON") : QStringLiteral("OFF"));
            _soap(camera, imagingUrl, settingsRequest, true, clockOffsetSecs, [this, label](bool setOk, const QByteArray &setBody) {
                _finish(label, setOk, setOk ? QString() : xmlText(setBody, QLatin1String("Text")));
            });
        });
    });
}

void CameraControl::_get(const Camera &camera, const QUrl &url, const Reply &reply)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(kTimeoutMs);

    QNetworkReply *const networkReply = _network->get(request);
    networkReply->setProperty("nxUser", camera.user);
    networkReply->setProperty("nxPassword", camera.password);
    (void) connect(networkReply, &QNetworkReply::finished, this, [networkReply, reply]() {
        const QByteArray body = networkReply->readAll();
        reply(networkReply->error() == QNetworkReply::NoError, body);
        networkReply->deleteLater();
    });
}

void CameraControl::_put(const Camera &camera, const QUrl &url, const QByteArray &body, const Reply &reply)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(kTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/xml"));

    QNetworkReply *const networkReply = _network->put(request, body);
    networkReply->setProperty("nxUser", camera.user);
    networkReply->setProperty("nxPassword", camera.password);
    (void) connect(networkReply, &QNetworkReply::finished, this, [networkReply, reply]() {
        const QByteArray responseBody = networkReply->readAll();
        reply(networkReply->error() == QNetworkReply::NoError, responseBody);
        networkReply->deleteLater();
    });
}

void CameraControl::_soap(const Camera &camera, const QUrl &url, const QString &body, bool authenticate,
                          qint64 clockOffsetSecs, const Reply &reply)
{
    QString header;
    if (authenticate) {
        QByteArray nonce(16, Qt::Uninitialized);
        QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(nonce.data()), nonce.size() / static_cast<qsizetype>(sizeof(quint32)));
        const QByteArray created = QDateTime::currentDateTimeUtc().addSecs(clockOffsetSecs).toString(QStringLiteral("yyyy-MM-ddTHH:mm:ssZ")).toLatin1();

        header = QStringLiteral(
            "<Security s:mustUnderstand=\"1\" xmlns=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\">"
            "<UsernameToken><Username>%1</Username>"
            "<Password Type=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">%2</Password>"
            "<Nonce EncodingType=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary\">%3</Nonce>"
            "<Created xmlns=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\">%4</Created>"
            "</UsernameToken></Security>")
            .arg(camera.user.toHtmlEscaped(),
                 QString::fromLatin1(passwordDigest(nonce, created, camera.password.toUtf8())),
                 QString::fromLatin1(nonce.toBase64()),
                 QString::fromLatin1(created));
    }

    QNetworkRequest request(url);
    request.setTransferTimeout(kTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/soap+xml; charset=utf-8"));

    QNetworkReply *const networkReply = _network->post(request, soapEnvelope(header, body).toUtf8());
    (void) connect(networkReply, &QNetworkReply::finished, this, [networkReply, reply]() {
        const QByteArray responseBody = networkReply->readAll();
        reply(networkReply->error() == QNetworkReply::NoError, responseBody);
        networkReply->deleteLater();
    });
}

void CameraControl::_begin()
{
    if (_pending++ == 0) {
        emit busyChanged();
    }
}

void CameraControl::_finish(const QString &label, bool ok, const QString &detail)
{
    _status = ok ? tr("%1 - done").arg(label)
                 : (detail.isEmpty() ? tr("%1 - failed").arg(label) : tr("%1 - failed: %2").arg(label, detail));
    if (!ok) {
        qCWarning(NxCameraControlLog) << _status;
    }
    emit statusChanged();

    if (--_pending == 0) {
        emit busyChanged();
    }
}
