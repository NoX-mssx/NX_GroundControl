#include "CameraControl.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QJsonDocument>
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

// Uniview LAPI. A single-sensor camera is channel 0. Exposure.DayNight.Mode: 0 automatic, 1 colour (day),
// 2 black and white (night). LampCtrl: Type 2 is the infrared lamp (1 is white light), Mode 1 is manual
// brightness, levels 0..1000.
constexpr int kUniviewChannel = 0;
constexpr int kUniviewDayMode = 1;
constexpr int kUniviewNightMode = 2;
constexpr int kUniviewLampType = 2;
constexpr int kUniviewLampManualMode = 1;
constexpr int kUniviewLampLevel = 1000;

QString univiewPath(const QString &resource)
{
    return QStringLiteral("/LAPI/V1.0/Channels/%1/Image/%2").arg(kUniviewChannel).arg(resource);
}

/// Whether a LAPI reply reports success. Replies wrap their result in "Response"; an empty body is a success.
bool lapiSucceeded(const QJsonObject &root, QString *error)
{
    const QJsonObject response = root.value(QStringLiteral("Response")).toObject();
    const int responseCode = response.value(QStringLiteral("ResponseCode")).toInt(0);
    const int statusCode = response.value(QStringLiteral("StatusCode")).toInt(0);
    if ((responseCode == 0) && (statusCode == 0)) {
        return true;
    }
    QString text = response.value(QStringLiteral("StatusString")).toString();
    if (text.isEmpty()) {
        text = response.value(QStringLiteral("ResponseString")).toString();
    }
    *error = text.isEmpty() ? CameraControl::tr("the camera did not accept the request") : text;
    return false;
}

/// The data of a LAPI GET reply, empty if there is none. Some firmware sends the data without the wrapper.
QJsonObject lapiData(const QByteArray &body, QString *error)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        return QJsonObject();
    }
    const QJsonObject root = document.object();
    if (!root.contains(QStringLiteral("Response"))) {
        return root;
    }
    if (!lapiSucceeded(root, error)) {
        return QJsonObject();
    }
    return root.value(QStringLiteral("Response")).toObject().value(QStringLiteral("Data")).toObject();
}

/// Why a camera request failed, in a few words for the operator
QString failureReason(const QNetworkReply *reply)
{
    switch (reply->error()) {
    case QNetworkReply::ConnectionRefusedError:
        return CameraControl::tr("connection refused");
    case QNetworkReply::HostNotFoundError:
        return CameraControl::tr("address not found");
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:     // the transfer timeout aborts the request
        return CameraControl::tr("no answer");
    case QNetworkReply::AuthenticationRequiredError:
        return CameraControl::tr("wrong user name or password");
    default:
        break;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    return (status > 0) ? QStringLiteral("HTTP %1").arg(status) : reply->errorString();
}

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
    const Job job{QStringLiteral("%1: %2").arg(target.name, commandLabel(command)), target.host, command};

    _begin();
    if (target.host.isEmpty()) {
        _finish(job, false, tr("the camera has no IP address in the vehicle model"));
        return;
    }

    const QString type = camera.value(QStringLiteral("type")).toString();
    const bool illuminator = (command == QLatin1String("irOn")) || (command == QLatin1String("irOff"));
    const bool uniview = type.startsWith(QLatin1String("UNV"));
    if (type.startsWith(QLatin1String("Dahua"))) {
        _runDahua(target, command, job);
    } else if (type.startsWith(QLatin1String("Hikvision")) && (command != QLatin1String("reboot"))) {
        _runHikvision(target, command, job);
    } else if ((uniview && (command != QLatin1String("reboot"))) || illuminator) {
        _runUniview(target, command, job, uniview);
    } else {
        _runOnvif(target, command, job);
    }
}

void CameraControl::_runDahua(const Camera &camera, const QString &command, const Job &job)
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
        _get(camera, url, [this, job, remaining, accepted](bool ok, const QByteArray &body) {
            if (ok && body.trimmed().startsWith("OK")) {
                *accepted = true;
            }
            if (--(*remaining) == 0) {
                _finish(job, *accepted, *accepted ? QString() : tr("the camera did not accept the request"));
            }
        });
    }
}

void CameraControl::_runHikvision(const Camera &camera, const QString &command, const Job &job)
{
    if ((command == QLatin1String("day")) || (command == QLatin1String("night"))) {
        _hikvisionSetIrcut(camera, command, [this, camera, command, job](bool ok, const QString &error) {
            if (ok) {
                _finish(job, true);
                return;
            }
            qCWarning(NxCameraControlLog) << job.label << "through ISAPI failed:" << error << "- trying ONVIF";
            _runOnvif(camera, command, job);
        });
        return;
    }

    // The light only: day/night is a command of its own, so the operator chooses, say, night without the light
    _hikvisionSetLight(camera, command == QLatin1String("irOn"), [this, job](bool ok, const QString &error) {
        _finish(job, ok, error);
    });
}

void CameraControl::_hikvisionSetIrcut(const Camera &camera, const QString &filterType, const Done &done)
{
    // Read and written back with only the type changed, as the fields differ between models. Should the
    // read fail, the settings the old ground station sends are written instead.
    const QUrl url(QStringLiteral("http://%1/ISAPI/Image/channels/1/IrcutFilter").arg(camera.host));
    _get(camera, url, [this, camera, filterType, done, url](bool ok, const QByteArray &body) {
        static const QRegularExpression typeElement(QStringLiteral("<IrcutFilterType>[^<]*</IrcutFilterType>"));
        QString settings = QString::fromUtf8(body);
        if (ok && settings.contains(typeElement)) {
            settings.replace(typeElement, QStringLiteral("<IrcutFilterType>%1</IrcutFilterType>").arg(filterType));
        } else {
            settings = QStringLiteral(
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<IrcutFilter version=\"2.0\" xmlns=\"http://www.isapi.org/ver20/XMLSchema\">"
                "<IrcutFilterType>%1</IrcutFilterType>"
                "<nightToDayFilterLevel>4</nightToDayFilterLevel>"
                "<Schedule><scheduleType>day</scheduleType>"
                "<TimeRange><beginTime>06:00:00</beginTime><endTime>18:00:00</endTime></TimeRange></Schedule>"
                "</IrcutFilter>").arg(filterType);
        }
        _put(camera, url, settings.toUtf8(), QStringLiteral("application/xml"), [done](bool putOk, const QByteArray &putBody) {
            const QString detail = xmlText(putBody, QLatin1String("subStatusCode"));
            done(putOk, putOk ? QString() : (detail.isEmpty() ? tr("the camera did not accept the request") : detail));
        });
    });
}

void CameraControl::_hikvisionSetLight(const Camera &camera, bool on, const Done &done)
{
    // ISAPI keeps the illuminator under SupplementLight. The current settings are read and written back
    // with only the mode changed, because the accepted fields differ between models.
    const QUrl url(QStringLiteral("http://%1/ISAPI/Image/channels/1/SupplementLight").arg(camera.host));
    _get(camera, url, [this, camera, on, done, url](bool ok, const QByteArray &body) {
        static const QRegularExpression modeElement(QStringLiteral("<supplementLightMode>[^<]*</supplementLightMode>"));
        QString settings = QString::fromUtf8(body);
        if (!ok || !settings.contains(modeElement)) {
            done(false, tr("the camera has no switchable illuminator; it follows day/night mode"));
            return;
        }

        settings.replace(modeElement, QStringLiteral("<supplementLightMode>%1</supplementLightMode>")
                                          .arg(on ? QStringLiteral("irLight") : QStringLiteral("close")));
        if (on) {
            // A brightness of 0 would leave the light dark in manual brightness mode.
            static const QRegularExpression zeroBrightness(QStringLiteral("<irLightBrightness>0</irLightBrightness>"));
            settings.replace(zeroBrightness, QStringLiteral("<irLightBrightness>100</irLightBrightness>"));
        }

        _put(camera, url, settings.toUtf8(), QStringLiteral("application/xml"), [done](bool putOk, const QByteArray &putBody) {
            const QString detail = xmlText(putBody, QLatin1String("subStatusCode"));
            done(putOk, putOk ? QString() : (detail.isEmpty() ? tr("the camera did not accept the request") : detail));
        });
    });
}

void CameraControl::_runUniview(const Camera &camera, const QString &command, const Job &job, bool uniview)
{
    if ((command == QLatin1String("day")) || (command == QLatin1String("night"))) {
        _univiewSetDayNight(camera, command == QLatin1String("day"), [this, camera, command, job](bool ok, const QString &error) {
            if (ok) {
                _finish(job, true);
                return;
            }
            qCWarning(NxCameraControlLog) << job.label << "through LAPI failed:" << error << "- trying ONVIF";
            _runOnvif(camera, command, job);
        });
        return;
    }

    // The lamp only: day/night is a command of its own. The lamp lights in black and white (night) mode.
    _univiewSetLamp(camera, command == QLatin1String("irOn"), [this, job, uniview](bool ok, const QString &error) {
        _finish(job, ok, ok ? QString() : (uniview ? error : tr("not available for this camera type; the illuminator follows day/night mode")));
    });
}

void CameraControl::_univiewSetLamp(const Camera &camera, bool on, const Done &done)
{
    _univiewUpdate(camera, {univiewPath(QStringLiteral("LampCtrl"))}, [on](QJsonObject &lamp) {
        lamp.insert(QStringLiteral("Enabled"), on ? 1 : 0);
        lamp.insert(QStringLiteral("Type"), kUniviewLampType);
        lamp.insert(QStringLiteral("Mode"), kUniviewLampManualMode);
        lamp.insert(QStringLiteral("NearLevel"), on ? kUniviewLampLevel : 0);
        lamp.insert(QStringLiteral("MiddleLevel"), 0);
        lamp.insert(QStringLiteral("FarLevel"), 0);
        lamp.insert(QStringLiteral("SuperFarLevel"), 0);
        return true;
    }, done);
}

void CameraControl::_univiewSetDayNight(const Camera &camera, bool day, const Done &done)
{
    // Newer firmware keeps the exposure settings under Image/Exposure, older under Image/Advanced/Exposure.
    // They are written back whole: the camera rejects a DayNight object sent on its own.
    const QStringList paths{univiewPath(QStringLiteral("Exposure")), univiewPath(QStringLiteral("Advanced/Exposure"))};
    _univiewUpdate(camera, paths, [day](QJsonObject &exposure) {
        QJsonObject dayNight = exposure.value(QStringLiteral("DayNight")).toObject();
        if (dayNight.isEmpty()) {
            return false;
        }
        dayNight.insert(QStringLiteral("Mode"), day ? kUniviewDayMode : kUniviewNightMode);
        exposure.insert(QStringLiteral("DayNight"), dayNight);
        return true;
    }, done);
}

void CameraControl::_univiewUpdate(const Camera &camera, const QStringList &paths, const LapiChange &change, const Done &done)
{
    if (paths.isEmpty()) {
        done(false, tr("the camera does not answer Uniview (LAPI) requests"));
        return;
    }

    const QUrl url(QStringLiteral("http://%1%2").arg(camera.host, paths.first()));
    _get(camera, url, [this, camera, paths, change, done, url](bool ok, const QByteArray &body) {
        QString error;
        QJsonObject data = ok ? lapiData(body, &error) : QJsonObject();
        if (data.isEmpty()) {
            if (paths.count() > 1) {
                _univiewUpdate(camera, paths.mid(1), change, done);
            } else {
                done(false, error.isEmpty() ? tr("the camera does not answer Uniview (LAPI) requests") : error);
            }
            return;
        }
        if (!change(data)) {
            done(false, tr("the camera does not have this setting"));
            return;
        }

        _put(camera, url, QJsonDocument(data).toJson(QJsonDocument::Compact), QStringLiteral("application/json"),
             [done](bool putOk, const QByteArray &putBody) {
            if (!putOk) {
                done(false, tr("the camera did not accept the request"));
                return;
            }
            QString putError;
            const QJsonDocument reply = QJsonDocument::fromJson(putBody);
            const bool accepted = !reply.isObject() || lapiSucceeded(reply.object(), &putError);
            done(accepted, putError);
        });
    });
}

void CameraControl::_runOnvif(const Camera &camera, const QString &command, const Job &job)
{
    if ((command == QLatin1String("irOn")) || (command == QLatin1String("irOff"))) {
        _finish(job, false, tr("not available for this camera type; the illuminator follows day/night mode"));
        return;
    }

    const QUrl deviceUrl(QStringLiteral("http://%1/onvif/device_service").arg(camera.host));
    const QString timeRequest = QStringLiteral("<GetSystemDateAndTime xmlns=\"http://www.onvif.org/ver10/device/wsdl\"/>");

    _soap(camera, deviceUrl, timeRequest, false, 0, [this, camera, command, job, deviceUrl](bool ok, const QByteArray &body) {
        if (!ok) {
            _finish(job, false, tr("the camera does not answer ONVIF requests"));
            return;
        }
        const qint64 offset = parseClockOffsetSecs(body);

        if (command == QLatin1String("reboot")) {
            const QString request = QStringLiteral("<SystemReboot xmlns=\"http://www.onvif.org/ver10/device/wsdl\"/>");
            _soap(camera, deviceUrl, request, true, offset, [this, job](bool rebootOk, const QByteArray &rebootBody) {
                _finish(job, rebootOk, rebootOk ? QString() : xmlText(rebootBody, QLatin1String("Text")));
            });
            return;
        }

        _onvifSetIrCutFilter(camera, offset, command == QLatin1String("day"), job);
    });
}

void CameraControl::_onvifSetIrCutFilter(const Camera &camera, qint64 clockOffsetSecs, bool day, const Job &job)
{
    const QUrl deviceUrl(QStringLiteral("http://%1/onvif/device_service").arg(camera.host));
    const QString capabilitiesRequest = QStringLiteral(
        "<GetCapabilities xmlns=\"http://www.onvif.org/ver10/device/wsdl\"><Category>All</Category></GetCapabilities>");

    _soap(camera, deviceUrl, capabilitiesRequest, true, clockOffsetSecs,
          [this, camera, clockOffsetSecs, day, job](bool ok, const QByteArray &body) {
        const QUrl mediaUrl(xmlText(body, QLatin1String("XAddr"), QLatin1String("Media")));
        const QUrl imagingUrl(xmlText(body, QLatin1String("XAddr"), QLatin1String("Imaging")));
        if (!ok || !mediaUrl.isValid() || mediaUrl.isEmpty() || !imagingUrl.isValid() || imagingUrl.isEmpty()) {
            const QString fault = xmlText(body, QLatin1String("Text"));
            _finish(job, false, fault.isEmpty() ? tr("the camera did not report its imaging service") : fault);
            return;
        }

        const QString sourcesRequest = QStringLiteral("<GetVideoSources xmlns=\"http://www.onvif.org/ver10/media/wsdl\"/>");
        _soap(camera, mediaUrl, sourcesRequest, true, clockOffsetSecs,
              [this, camera, clockOffsetSecs, day, job, imagingUrl](bool sourcesOk, const QByteArray &sourcesBody) {
            const QString token = xmlAttribute(sourcesBody, QLatin1String("VideoSources"), QLatin1String("token"));
            if (!sourcesOk || token.isEmpty()) {
                _finish(job, false, tr("the camera did not report a video source"));
                return;
            }

            // IR cut filter ON = filter in the light path = colour (day); OFF = black and white (night).
            const QString settingsRequest = QStringLiteral(
                "<SetImagingSettings xmlns=\"http://www.onvif.org/ver20/imaging/wsdl\">"
                "<VideoSourceToken>%1</VideoSourceToken>"
                "<ImagingSettings><IrCutFilter xmlns=\"http://www.onvif.org/ver10/schema\">%2</IrCutFilter></ImagingSettings>"
                "<ForcePersistence>true</ForcePersistence>"
                "</SetImagingSettings>").arg(token.toHtmlEscaped(), day ? QStringLiteral("ON") : QStringLiteral("OFF"));
            _soap(camera, imagingUrl, settingsRequest, true, clockOffsetSecs, [this, job](bool setOk, const QByteArray &setBody) {
                _finish(job, setOk, setOk ? QString() : xmlText(setBody, QLatin1String("Text")));
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
    (void) connect(networkReply, &QNetworkReply::finished, this, [this, host = camera.host, networkReply, reply]() {
        const QByteArray body = networkReply->readAll();
        _noteRequest(host, networkReply);
        reply(networkReply->error() == QNetworkReply::NoError, body);
        networkReply->deleteLater();
    });
}

void CameraControl::_put(const Camera &camera, const QUrl &url, const QByteArray &body, const QString &contentType, const Reply &reply)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(kTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);

    QNetworkReply *const networkReply = _network->put(request, body);
    networkReply->setProperty("nxUser", camera.user);
    networkReply->setProperty("nxPassword", camera.password);
    (void) connect(networkReply, &QNetworkReply::finished, this, [this, host = camera.host, networkReply, reply]() {
        const QByteArray responseBody = networkReply->readAll();
        _noteRequest(host, networkReply);
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
    (void) connect(networkReply, &QNetworkReply::finished, this, [this, host = camera.host, networkReply, reply]() {
        const QByteArray responseBody = networkReply->readAll();
        _noteRequest(host, networkReply);
        reply(networkReply->error() == QNetworkReply::NoError, responseBody);
        networkReply->deleteLater();
    });
}

void CameraControl::_noteRequest(const QString &host, const QNetworkReply *reply)
{
    if (reply->error() == QNetworkReply::NoError) {
        _lastFailure.remove(host);
        return;
    }
    const char *method = (reply->operation() == QNetworkAccessManager::GetOperation)   ? "GET"
                         : (reply->operation() == QNetworkAccessManager::PutOperation) ? "PUT"
                                                                                       : "POST";
    qCWarning(NxCameraControlLog) << "Camera request failed:" << method << reply->url().toDisplayString(QUrl::RemoveUserInfo)
                                  << "HTTP" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                                  << reply->errorString();
    // A refused login says the most; a later fallback request (ONVIF after ISAPI) does not replace it
    const QString loginRefused = tr("wrong user name or password");
    if (_lastFailure.value(host) != loginRefused) {
        _lastFailure.insert(host, (reply->error() == QNetworkReply::AuthenticationRequiredError) ? loginRefused : failureReason(reply));
    }
}

void CameraControl::_begin()
{
    if (_pending++ == 0) {
        emit busyChanged();
    }
}

void CameraControl::_finish(const Job &job, bool ok, const QString &detail)
{
    // Why the camera's last request failed tells the operator more than which step gave up
    const QString why = _lastFailure.take(job.host);
    const QString reason = (ok || why.isEmpty()) ? detail : (detail.isEmpty() ? why : QStringLiteral("%1 (%2)").arg(detail, why));
    _status = ok ? tr("%1 - done").arg(job.label)
                 : (reason.isEmpty() ? tr("%1 - failed").arg(job.label) : tr("%1 - failed: %2").arg(job.label, reason));
    if (!ok) {
        qCWarning(NxCameraControlLog) << _status;
    }
    emit statusChanged();
    if (!ok) {
        emit commandFailed(_status);
    }
    emit commandFinished(job.host, job.command, ok);

    if (--_pending == 0) {
        emit busyChanged();
    }
}
