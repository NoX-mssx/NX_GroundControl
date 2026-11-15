#include "StarlinkPosition.h"
#include "Fact.h"
#include "FactGroup.h"
#include "FlyViewSettings.h"
#include "LinkConfiguration.h"
#include "LinkInterface.h"
#include "MultiVehicleManager.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"

#include <QtCore/QtEndian>
#include <QtCore/QUrl>
#include <QtNetwork/QAbstractNetworkCache>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <cstring>

QGC_LOGGING_CATEGORY(StarlinkPositionLog, "LinkTunnel.StarlinkPosition")

namespace {

// Field numbers of the dish API (spacex/api/device/device.proto, common.proto)
constexpr quint32 kRequestGetLocation = 1017;   // Request.get_location
constexpr quint32 kResponseStatus = 2;          // Response.status
constexpr quint32 kResponseGetLocation = 1017;  // Response.get_location
constexpr quint32 kLocationLla = 1;             // GetLocationResponse.lla
constexpr quint32 kStatusCode = 1;              // Status.code
constexpr quint32 kStatusMessage = 2;           // Status.message
constexpr quint32 kLlaLat = 1;
constexpr quint32 kLlaLon = 2;
constexpr quint32 kLlaAlt = 3;

constexpr int kWireVarint = 0;
constexpr int kWireFixed64 = 1;
constexpr int kWireBytes = 2;
constexpr int kWireFixed32 = 5;

/// One field of a protobuf message: just enough to read the dish's answer
struct ProtoField {
    quint32 number = 0;
    int wire = -1;
    quint64 value = 0;      ///< varint, or the raw bits of a fixed64/fixed32
    QByteArray bytes;       ///< length-delimited
};

bool readVarint(const QByteArray &data, qsizetype *pos, quint64 *value)
{
    quint64 result = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (*pos >= data.size()) {
            return false;
        }
        const quint8 byte = static_cast<quint8>(data.at((*pos)++));
        result |= static_cast<quint64>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            *value = result;
            return true;
        }
    }
    return false;
}

bool readField(const QByteArray &data, qsizetype *pos, ProtoField *field)
{
    quint64 key = 0;
    if (!readVarint(data, pos, &key)) {
        return false;
    }
    field->number = static_cast<quint32>(key >> 3);
    field->wire = static_cast<int>(key & 7);
    field->bytes.clear();
    switch (field->wire) {
    case kWireVarint:
        return readVarint(data, pos, &field->value);
    case kWireFixed64:
        if ((data.size() - *pos) < 8) {
            return false;
        }
        field->value = qFromLittleEndian<quint64>(data.constData() + *pos);
        *pos += 8;
        return true;
    case kWireBytes: {
        quint64 length = 0;
        if (!readVarint(data, pos, &length) || (length > static_cast<quint64>(data.size() - *pos))) {
            return false;
        }
        field->bytes = data.mid(*pos, static_cast<qsizetype>(length));
        *pos += static_cast<qsizetype>(length);
        return true;
    }
    case kWireFixed32:
        if ((data.size() - *pos) < 4) {
            return false;
        }
        field->value = qFromLittleEndian<quint32>(data.constData() + *pos);
        *pos += 4;
        return true;
    default:
        return false;
    }
}

double fixed64ToDouble(quint64 bits)
{
    double value = 0;
    static_assert(sizeof(value) == sizeof(bits));
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

/// A cache that keeps nothing. Without any cache Qt tries to turn caching on again when the gRPC trailers
/// arrive after the data, and logs an error for every answer; with one, caching counts as on from the first
/// headers, and a POST is never stored anyway.
class NoCache final : public QAbstractNetworkCache
{
public:
    explicit NoCache(QObject *parent) : QAbstractNetworkCache(parent) {}

    QNetworkCacheMetaData metaData(const QUrl &) override { return QNetworkCacheMetaData(); }
    void updateMetaData(const QNetworkCacheMetaData &) override {}
    QIODevice *data(const QUrl &) override { return nullptr; }
    bool remove(const QUrl &) override { return false; }
    qint64 cacheSize() const override { return 0; }
    QIODevice *prepare(const QNetworkCacheMetaData &) override { return nullptr; }
    void insert(QIODevice *) override {}
    void clear() override {}
};

/// The dish API address of a link: its Starlink address, or else its ping address on the default port
QString dishUrl(const LinkConfiguration *config, int defaultPort)
{
    if (!config) {
        return QString();
    }
    QString address = config->starlinkAddress().trimmed();
    if (address.isEmpty()) {
        address = config->pingAddress().trimmed();
    }
    if (address.isEmpty()) {
        return QString();
    }
    if (!address.contains(QLatin1String("://"))) {
        address.prepend(QStringLiteral("http://"));
    }
    const QUrl url = QUrl::fromUserInput(address);
    if (!url.isValid() || url.host().isEmpty()) {
        return QString();
    }
    return QStringLiteral("http://%1:%2/SpaceX.API.Device.Device/Handle").arg(url.host()).arg(url.port(defaultPort));
}

/// Why a request to the dish failed, in a few words for the operator
QString networkFailure(const QNetworkReply *reply)
{
    switch (reply->error()) {
    case QNetworkReply::ConnectionRefusedError:
        return StarlinkPosition::tr("connection refused");
    case QNetworkReply::HostNotFoundError:
        return StarlinkPosition::tr("address not found");
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:     // the transfer timeout aborts the request
        return StarlinkPosition::tr("no answer");
    default:
        break;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    return (status > 0) ? QStringLiteral("HTTP %1").arg(status) : reply->errorString();
}

} // namespace

StarlinkPosition::StarlinkPosition(QObject *parent)
    : QObject(parent)
    , _network(new QNetworkAccessManager(this))
{
    // The dish is reached through the vehicle's tunnel, never through a proxy of this computer
    _network->setProxy(QNetworkProxy::NoProxy);
    _network->setCache(new NoCache(_network));
    _clock.start();

    _timer.setInterval(kPollIntervalMs);
    (void) connect(&_timer, &QTimer::timeout, this, &StarlinkPosition::_tick);

    if (SettingsManager *const settings = SettingsManager::instance()) {
        _sourceFact = settings->flyViewSettings()->gpsSource();
        (void) connect(_sourceFact, &Fact::rawValueChanged, this, &StarlinkPosition::_tick);
    }

    (void) connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged,
                   this, &StarlinkPosition::_activeVehicleChanged);
    _activeVehicleChanged(MultiVehicleManager::instance()->activeVehicle());
}

QString StarlinkPosition::dishAddress() const
{
    const QUrl url(_url);
    return _url.isEmpty() ? QString() : QStringLiteral("%1:%2").arg(url.host()).arg(url.port(kDefaultPort));
}

bool StarlinkPosition::starlinkFresh() const
{
    return _fix.isValid() && (_fixAt >= 0) && ((_clock.elapsed() - _fixAt) <= kFreshMs);
}

QByteArray StarlinkPosition::locationRequest()
{
    // Request { get_location (1017) = GetLocationRequest {} }: key 1017 << 3 | 2 as a varint, then length 0
    QByteArray message;
    quint64 key = (static_cast<quint64>(kRequestGetLocation) << 3) | kWireBytes;
    while (key >= 0x80) {
        message.append(static_cast<char>((key & 0x7F) | 0x80));
        key >>= 7;
    }
    message.append(static_cast<char>(key));
    message.append('\0');

    // gRPC frame: not compressed, 4-byte big-endian length, the message
    QByteArray frame(5, '\0');
    qToBigEndian<quint32>(static_cast<quint32>(message.size()), frame.data() + 1);
    return frame + message;
}

QGeoCoordinate StarlinkPosition::parseLocationResponse(const QByteArray &body, QString *error)
{
    // The first data frame holds the Response message (grpc-web would add a trailer frame, flag 0x80)
    QByteArray message;
    bool found = false;
    for (qsizetype pos = 0; (pos + 5) <= body.size();) {
        const quint8 flags = static_cast<quint8>(body.at(pos));
        const quint32 length = qFromBigEndian<quint32>(body.constData() + pos + 1);
        if (length > static_cast<quint32>(body.size() - pos - 5)) {
            break;
        }
        if ((flags & 0x80) == 0) {
            message = body.mid(pos + 5, length);
            found = true;
            break;
        }
        pos += 5 + static_cast<qsizetype>(length);
    }
    if (!found) {
        *error = tr("the dish gave an empty answer");
        return QGeoCoordinate();
    }

    QByteArray location;
    bool hasLocation = false;
    ProtoField field;
    for (qsizetype pos = 0; pos < message.size();) {
        if (!readField(message, &pos, &field)) {
            *error = tr("the dish's answer could not be read");
            return QGeoCoordinate();
        }
        if ((field.number == kResponseStatus) && (field.wire == kWireBytes)) {
            quint64 code = 0;
            QString text;
            ProtoField statusField;
            for (qsizetype statusPos = 0; statusPos < field.bytes.size();) {
                if (!readField(field.bytes, &statusPos, &statusField)) {
                    break;
                }
                if ((statusField.number == kStatusCode) && (statusField.wire == kWireVarint)) {
                    code = statusField.value;
                } else if ((statusField.number == kStatusMessage) && (statusField.wire == kWireBytes)) {
                    text = QString::fromUtf8(statusField.bytes);
                }
            }
            if (code != 0) {
                *error = tr("the dish does not give its position: %1")
                             .arg(text.isEmpty() ? QStringLiteral("status %1").arg(code) : text);
                return QGeoCoordinate();
            }
        } else if ((field.number == kResponseGetLocation) && (field.wire == kWireBytes)) {
            location = field.bytes;
            hasLocation = true;
        }
    }

    QByteArray lla;
    bool hasLla = false;
    for (qsizetype pos = 0; hasLocation && (pos < location.size());) {
        if (!readField(location, &pos, &field)) {
            break;
        }
        if ((field.number == kLocationLla) && (field.wire == kWireBytes)) {
            lla = field.bytes;
            hasLla = true;
        }
    }

    double lat = qQNaN();
    double lon = qQNaN();
    double alt = qQNaN();
    for (qsizetype pos = 0; hasLla && (pos < lla.size());) {
        if (!readField(lla, &pos, &field)) {
            break;
        }
        if (field.wire != kWireFixed64) {
            continue;
        }
        if (field.number == kLlaLat) {
            lat = fixed64ToDouble(field.value);
        } else if (field.number == kLlaLon) {
            lon = fixed64ToDouble(field.value);
        } else if (field.number == kLlaAlt) {
            alt = fixed64ToDouble(field.value);
        }
    }

    const QGeoCoordinate coordinate = qIsNaN(alt) ? QGeoCoordinate(lat, lon) : QGeoCoordinate(lat, lon, alt);
    if (!coordinate.isValid()) {
        *error = tr("the dish's answer has no position");
        return QGeoCoordinate();
    }
    error->clear();
    return coordinate;
}

void StarlinkPosition::_activeVehicleChanged(Vehicle *vehicle)
{
    if (_vehicle) {
        (void) disconnect(_vehicle->vehicleLinkManager(), nullptr, this, nullptr);
        _vehicle->setExternalPosition(QGeoCoordinate());
    }

    _vehicle = vehicle;
    _fix = QGeoCoordinate();
    _fixAt = -1;
    _autoOnDish = false;
    _roverFixSince = -1;
    _roverFixLostAt = -1;
    _reportedError.clear();
    // The old address goes first, so the aborted request reads as stale rather than as a failure
    _url.clear();
    if (_reply) {
        _reply->abort();
    }

    if (_vehicle) {
        (void) connect(_vehicle->vehicleLinkManager(), &VehicleLinkManager::primaryLinkChanged,
                       this, &StarlinkPosition::_updateAddress);
        _timer.start();
    } else {
        _timer.stop();
    }
    _updateAddress();
}

void StarlinkPosition::_updateAddress()
{
    QString url;
    if (_vehicle) {
        const SharedLinkInterfacePtr link = _vehicle->vehicleLinkManager()->primaryLink().lock();
        url = dishUrl(link ? link->linkConfiguration().get() : nullptr, kDefaultPort);
    }
    if (url != _url) {
        _url = url;
        _fix = QGeoCoordinate();
        _fixAt = -1;
        _setError(QString());
        if (_reply) {
            _reply->abort();
        }
        emit stateChanged();
    }
    _tick();
}

int StarlinkPosition::_source() const
{
    return _sourceFact ? _sourceFact->rawValue().toInt() : Rover;
}

bool StarlinkPosition::_roverHasFix() const
{
    if (!_vehicle) {
        return false;
    }
    const Fact *const lock = _vehicle->gpsFactGroup()->getFact(QStringLiteral("lock"));
    // GPS_FIX_TYPE_3D_FIX and better
    return lock && (lock->rawValue().toInt() >= 3);
}

void StarlinkPosition::_tick()
{
    // Rover alone needs nothing from the dish; Auto keeps asking so the position is there when the GPS drops
    if (_vehicle && (_source() != Rover)) {
        _poll();
    } else {
        _setError(QString());
    }
    _update();
}

void StarlinkPosition::_poll()
{
    if (_reply) {
        return;
    }
    if (_url.isEmpty()) {
        _setError(tr("the link has no Starlink or ping address"));
        return;
    }

    QNetworkRequest request{QUrl(_url)};
    // gRPC is HTTP/2; the dish speaks it without TLS, so it is used from the first byte
    request.setAttribute(QNetworkRequest::Http2DirectAttribute, true);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/grpc"));
    request.setRawHeader("te", "trailers");
    request.setTransferTimeout(kTimeoutMs);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);

    QNetworkReply *const reply = _network->post(request, locationRequest());
    _reply = reply;
    (void) connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (_reply == reply) {
            _reply = nullptr;
        }
        // Answers to an address no longer in use are dropped
        if (reply->url() != QUrl(_url)) {
            return;
        }

        QString error;
        QGeoCoordinate coordinate;
        const QByteArray grpcStatus = reply->rawHeader("grpc-status");
        if (reply->error() != QNetworkReply::NoError) {
            error = networkFailure(reply);
        } else if (!grpcStatus.isEmpty() && (grpcStatus != "0")) {
            // An error comes as headers only, with the reason in grpc-message
            QString message = QUrl::fromPercentEncoding(reply->rawHeader("grpc-message"));
            if (message.isEmpty()) {
                message = QStringLiteral("gRPC status %1").arg(QString::fromLatin1(grpcStatus));
            }
            error = tr("the dish does not give its position: %1").arg(message);
        } else {
            coordinate = parseLocationResponse(reply->readAll(), &error);
        }

        if (coordinate.isValid()) {
            _fix = coordinate;
            _fixAt = _clock.elapsed();
            _setError(QString());
        } else {
            _setError(error);
        }
        _update();
    });
}

void StarlinkPosition::_setError(const QString &error)
{
    if (error == _error) {
        return;
    }
    if (!error.isEmpty()) {
        qCWarning(StarlinkPositionLog) << "Starlink position:" << error << QUrl(_url).toDisplayString(QUrl::RemovePath);
    }
    _error = error;
    emit stateChanged();
}

void StarlinkPosition::_update()
{
    const int source = _source();
    const qint64 now = _clock.elapsed();

    const bool roverFix = _roverHasFix();
    if (roverFix) {
        _roverFixLostAt = -1;
        if (_roverFixSince < 0) {
            _roverFixSince = now;
        }
    } else {
        _roverFixSince = -1;
        if (_roverFixLostAt < 0) {
            _roverFixLostAt = now;
        }
    }

    if (source == Auto) {
        if (!_autoOnDish && !roverFix && ((now - _roverFixLostAt) >= kAutoToDishMs)) {
            _autoOnDish = true;
        } else if (_autoOnDish && roverFix && ((now - _roverFixSince) >= kAutoToRoverMs)) {
            _autoOnDish = false;
        }
    } else {
        _autoOnDish = false;
    }

    const bool wantDish = _vehicle && ((source == Starlink) || ((source == Auto) && _autoOnDish));
    const bool fresh = starlinkFresh();
    const bool useDish = wantDish && fresh;

    if (_vehicle) {
        if (useDish) {
            _vehicle->setExternalPosition(_fix);
        } else if (_usingStarlink) {
            _vehicle->setExternalPosition(QGeoCoordinate());
        }
    }

    // The "!" hears once about each reason the dish cannot be used while it is wanted
    if (wantDish && !fresh && !_error.isEmpty()) {
        if (_error != _reportedError) {
            _reportedError = _error;
            emit problem(_error);
        }
    } else if (fresh) {
        _reportedError.clear();
    }

    const bool changed = (useDish != _usingStarlink) || (fresh != _fresh);
    _usingStarlink = useDish;
    _fresh = fresh;
    if (changed) {
        emit stateChanged();
    }
}
