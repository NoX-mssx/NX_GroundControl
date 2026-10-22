#include "RoomManager.h"
#include "LinkConfiguration.h"
#include "LinkInterface.h"
#include "LinkManager.h"
#include "QGCLoggingCategory.h"
#include "SecretProtector.h"
#include "TCPLink.h"
#include "UDPLink.h"
#include "VehicleModelManager.h"
#include "WireGuardTunnel.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QRandomGenerator>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QSettings>
#include <QtCore/QSysInfo>
#include <QtCore/QUrl>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QSslCertificate>
#include <QtNetwork/QSslConfiguration>
#include <QtNetwork/QSslSocket>
#include <QtQml/QJSEngine>

QGC_LOGGING_CATEGORY(RoomManagerLog, "Rooms.RoomManager")

namespace {

constexpr const char *kGroup = "NxRooms";
constexpr const char *kDeviceNameKey = "deviceName";
constexpr const char *kDeviceKeyKey = "deviceKey";
constexpr const char *kDevicePublicKeyKey = "devicePublicKey";
constexpr const char *kServersKey = "servers";
constexpr const char *kRoomsKey = "rooms";
constexpr const char *kCurrentRoomKey = "currentRoom";
constexpr const char *kTunnelsKey = "tunnels";

constexpr const char *kServerTokenPrefix = "nxs1.";
constexpr const char *kRoomTokenPrefix = "nxr1.";
/// Links and models of room vehicles refer to them as "room:<vehicle id>"
constexpr const char *kRoomRefPrefix = "room:";

/// Last byte of the address an operator has in a vehicle's tunnel
constexpr quint32 kOperatorHost = 252;

RoomManager *s_instance = nullptr;

QString hashOf(const QJsonObject &object)
{
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact),
                                                        QCryptographicHash::Sha256).toHex());
}

QString jsonText(const QJsonValue &value)
{
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
}

QJsonDocument parseJsonText(const QString &text)
{
    return QJsonDocument::fromJson(text.toUtf8());
}

/// The admin's tunnel address moved to the operator host of the same network: 10.30.1.2/24 -> 10.30.1.252/24
QString operatorAddress(const QString &address)
{
    const QString first = address.split(QLatin1Char(',')).constFirst().trimmed();
    const QStringList parts = first.split(QLatin1Char('/'));
    const QHostAddress ip(parts.constFirst());
    if (ip.protocol() != QAbstractSocket::IPv4Protocol) {
        return QString();
    }
    const QHostAddress host((ip.toIPv4Address() & 0xFFFFFF00u) | kOperatorHost);
    return host.toString() + QLatin1Char('/') + ((parts.size() > 1) ? parts.at(1) : QStringLiteral("32"));
}

QString newBoardId()
{
    return QStringLiteral("b%1").arg(QRandomGenerator::system()->generate64(), 16, 16, QLatin1Char('0'));
}

SharedLinkConfigurationPtr sharedConfig(const LinkConfiguration *config)
{
    for (const SharedLinkConfigurationPtr &shared : LinkManager::instance()->linkConfigurationList()) {
        if (shared.get() == config) {
            return shared;
        }
    }
    return SharedLinkConfigurationPtr();
}

} // namespace

RoomManager::RoomManager(QObject *parent)
    : QObject(parent)
    , _network(new QNetworkAccessManager(this))
{
    s_instance = this;
    // The hub is always reached directly: a proxy would see the device tokens
    _network->setProxy(QNetworkProxy::NoProxy);

    _load();

    _refreshTimer.setInterval(kRefreshMs);
    (void) connect(&_refreshTimer, &QTimer::timeout, this, &RoomManager::refresh);
    _refreshTimer.start();

    (void) connect(&_heartbeatTimer, &QTimer::timeout, this, &RoomManager::_heartbeat);

    _roomRefreshTimer.setSingleShot(true);
    _roomRefreshTimer.setInterval(300);
    (void) connect(&_roomRefreshTimer, &QTimer::timeout, this, &RoomManager::_refreshCurrentRoom);

    if (QCoreApplication::instance()) {
        (void) connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &RoomManager::_releaseBlocking);
    }

    QTimer::singleShot(0, this, &RoomManager::refresh);
}

RoomManager::~RoomManager()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

RoomManager *RoomManager::instance()
{
    if (!s_instance) {
        (void) new RoomManager(QCoreApplication::instance());
    }
    return s_instance;
}

RoomManager *RoomManager::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    RoomManager *const manager = instance();
    QJSEngine::setObjectOwnership(manager, QJSEngine::CppOwnership);
    return manager;
}

// ---- storage -------------------------------------------------------------------------------------------------

void RoomManager::_load()
{
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));

    _deviceName = settings.value(QLatin1String(kDeviceNameKey)).toString().trimmed();
    if (_deviceName.isEmpty()) {
        _deviceName = QSysInfo::machineHostName();
    }
    _deviceCode = settings.value(QLatin1String(kDevicePublicKeyKey)).toString().left(8);

    const QJsonArray servers = parseJsonText(settings.value(QLatin1String(kServersKey)).toString()).array();
    for (const QJsonValue &value : servers) {
        const QJsonObject object = value.toObject();
        Server server;
        server.host = object.value(QLatin1String("host")).toString();
        server.port = object.value(QLatin1String("port")).toInt();
        server.fingerprint = object.value(QLatin1String("fingerprint")).toString();
        server.token = SecretProtector::unprotect(object.value(QLatin1String("token")).toString());
        server.deviceId = object.value(QLatin1String("deviceId")).toString();
        server.serverAdmin = object.value(QLatin1String("serverAdmin")).toBool();
        if (!server.host.isEmpty() && (server.port > 0)) {
            _servers.insert(server.key(), server);
        }
    }

    _rooms = parseJsonText(settings.value(QLatin1String(kRoomsKey)).toString()).array().toVariantList();
    _currentRoom = settings.value(QLatin1String(kCurrentRoomKey)).toString();
    if (_roomEntry(_currentRoom).isEmpty()) {
        _currentRoom.clear();
    }
    _tunnels = parseJsonText(settings.value(QLatin1String(kTunnelsKey)).toString()).object().toVariantMap();
}

void RoomManager::_saveServers() const
{
    QJsonArray servers;
    for (const Server &server : _servers) {
        servers.append(QJsonObject{
            {QStringLiteral("host"), server.host},
            {QStringLiteral("port"), server.port},
            {QStringLiteral("fingerprint"), server.fingerprint},
            {QStringLiteral("token"), SecretProtector::protect(server.token)},
            {QStringLiteral("deviceId"), server.deviceId},
            {QStringLiteral("serverAdmin"), server.serverAdmin},
        });
    }
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    settings.setValue(QLatin1String(kServersKey), jsonText(servers));
}

void RoomManager::_saveRooms() const
{
    // The invite tokens are only kept in memory: they come back with every refresh
    QJsonArray rooms;
    for (const QVariant &roomVar : _rooms) {
        QVariantMap room = roomVar.toMap();
        (void) room.remove(QStringLiteral("inviteToken"));
        rooms.append(QJsonObject::fromVariantMap(room));
    }
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    settings.setValue(QLatin1String(kRoomsKey), jsonText(rooms));
    settings.setValue(QLatin1String(kCurrentRoomKey), _currentRoom);
}

void RoomManager::_saveTunnels() const
{
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    settings.setValue(QLatin1String(kTunnelsKey), jsonText(QJsonObject::fromVariantMap(_tunnels)));
}

QString RoomManager::tunnelForBoard(const QString &boardId)
{
    QVariantMap tunnels;
    if (s_instance) {
        tunnels = s_instance->_tunnels;
    } else {
        QSettings settings;
        settings.beginGroup(QLatin1String(kGroup));
        tunnels = parseJsonText(settings.value(QLatin1String(kTunnelsKey)).toString()).object().toVariantMap();
    }
    return tunnels.value(boardId).toMap().value(QStringLiteral("tunnel")).toString();
}

// ---- simple state --------------------------------------------------------------------------------------------

void RoomManager::_setError(const QString &error)
{
    if (error != _lastError) {
        _lastError = error;
        emit lastErrorChanged();
    }
    if (!error.isEmpty()) {
        qCWarning(RoomManagerLog) << error;
    }
}

void RoomManager::clearError()
{
    _setError(QString());
}

void RoomManager::_setPending(int delta)
{
    const bool wasBusy = busy();
    _pending = qMax(0, _pending + delta);
    if (wasBusy != busy()) {
        emit busyChanged();
    }
}

void RoomManager::setDeviceName(const QString &name)
{
    const QString trimmed = name.trimmed().left(64);
    if (trimmed.isEmpty() || (trimmed == _deviceName)) {
        return;
    }
    _deviceName = trimmed;
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    settings.setValue(QLatin1String(kDeviceNameKey), _deviceName);
    emit deviceChanged();

    for (const Server &server : std::as_const(_servers)) {
        if (!server.token.isEmpty()) {
            _request(server.key(), QStringLiteral("PATCH"), QStringLiteral("/v1/me"), QJsonObject{{QStringLiteral("name"), _deviceName}},
                     [](int, const QJsonObject &) {});
        }
    }
}

bool RoomManager::serverAdmin() const
{
    for (const Server &server : _servers) {
        if (server.serverAdmin && !server.token.isEmpty()) {
            return true;
        }
    }
    return false;
}

QVariantList RoomManager::servers() const
{
    QVariantList servers;
    for (const Server &server : _servers) {
        if (!server.token.isEmpty()) {
            servers.append(QVariantMap{{QStringLiteral("key"), server.key()}, {QStringLiteral("serverAdmin"), server.serverAdmin}});
        }
    }
    return servers;
}

QString RoomManager::currentRole() const
{
    const QVariantMap room = _roomEntry(_currentRoom);
    if ((room.value(QStringLiteral("status")).toString() != QLatin1String("active")) || !room.value(QStringLiteral("enabled")).toBool()) {
        return QString();
    }
    return room.value(QStringLiteral("role")).toString();
}

void RoomManager::setCurrentRoom(const QString &roomKey)
{
    if (roomKey == _currentRoom) {
        return;
    }
    if (!_activeBoard.isEmpty() || !_connectingBoard.isEmpty()) {
        _setError(tr("Disconnect from the vehicle before switching rooms."));
        // Puts a room selector that already moved back on the current room
        emit currentRoomChanged();
        return;
    }
    if (!roomKey.isEmpty() && _roomEntry(roomKey).isEmpty()) {
        return;
    }

    _currentRoom = roomKey;
    _saveRooms();
    _boardBundles.clear();
    _boardNames.clear();
    _boardVersions.clear();
    _boards.clear();
    _members.clear();
    _requests.clear();
    _removeRoomLinks();
    _setRadio(0, QJsonObject());
    emit currentRoomChanged();
    emit boardsChanged();
    emit membersChanged();
    _refreshCurrentRoom();
}

// ---- rooms list ----------------------------------------------------------------------------------------------

QString RoomManager::_roomKey(const QString &serverKey, const QString &roomId)
{
    return serverKey + QLatin1Char('/') + roomId;
}

QString RoomManager::_serverKeyOf(const QString &roomKey)
{
    return roomKey.section(QLatin1Char('/'), 0, 0);
}

QString RoomManager::_roomIdOf(const QString &roomKey)
{
    return roomKey.section(QLatin1Char('/'), 1);
}

QVariantMap RoomManager::_roomEntry(const QString &roomKey) const
{
    if (roomKey.isEmpty()) {
        return QVariantMap();
    }
    for (const QVariant &roomVar : _rooms) {
        const QVariantMap room = roomVar.toMap();
        if (room.value(QStringLiteral("key")).toString() == roomKey) {
            return room;
        }
    }
    return QVariantMap();
}

void RoomManager::_setServerRooms(const QString &serverKey, const QJsonArray &rooms)
{
    QVariantList updated;
    for (const QVariant &roomVar : std::as_const(_rooms)) {
        if (roomVar.toMap().value(QStringLiteral("server")).toString() != serverKey) {
            updated.append(roomVar);
        }
    }
    for (const QJsonValue &value : rooms) {
        const QJsonObject room = value.toObject();
        updated.append(QVariantMap{
            {QStringLiteral("key"), _roomKey(serverKey, room.value(QLatin1String("room_id")).toString())},
            {QStringLiteral("server"), serverKey},
            {QStringLiteral("name"), room.value(QLatin1String("name")).toString()},
            {QStringLiteral("role"), room.value(QLatin1String("role")).toString()},
            {QStringLiteral("status"), room.value(QLatin1String("status")).toString()},
            {QStringLiteral("enabled"), room.value(QLatin1String("enabled")).toBool()},
            {QStringLiteral("inviteToken"), room.value(QLatin1String("invite_token")).toString()},
        });
    }
    if (updated == _rooms) {
        return;
    }

    const QString previousRole = currentRole();
    _rooms = updated;
    _saveRooms();
    emit roomsChanged();

    if (!_currentRoom.isEmpty() && _roomEntry(_currentRoom).isEmpty()) {
        // This computer was removed from the room; its vehicles go with it
        if (!_activeBoard.isEmpty()) {
            _endLease(false);
        }
        _currentRoom.clear();
        _saveRooms();
        _boardBundles.clear();
        _boards.clear();
        _members.clear();
        _requests.clear();
        _removeRoomLinks();
        emit boardsChanged();
        emit membersChanged();
    }
    emit currentRoomChanged();
    if (currentRole() != previousRole) {
        _scheduleRoomRefresh();
    }
}

void RoomManager::_updateRoomEntry(const QString &roomKey, const QVariantMap &values)
{
    bool found = false;
    for (QVariant &roomVar : _rooms) {
        QVariantMap room = roomVar.toMap();
        if (room.value(QStringLiteral("key")).toString() == roomKey) {
            for (auto it = values.cbegin(); it != values.cend(); ++it) {
                room[it.key()] = it.value();
            }
            roomVar = room;
            found = true;
            break;
        }
    }
    if (!found) {
        QVariantMap room = values;
        room[QStringLiteral("key")] = roomKey;
        room[QStringLiteral("server")] = _serverKeyOf(roomKey);
        _rooms.append(room);
    }
    _saveRooms();
    emit roomsChanged();
    emit currentRoomChanged();
}

void RoomManager::_removeRoomEntry(const QString &roomKey)
{
    for (qsizetype i = 0; i < _rooms.count(); i++) {
        if (_rooms.at(i).toMap().value(QStringLiteral("key")).toString() == roomKey) {
            _rooms.removeAt(i);
            break;
        }
    }
    _saveRooms();
    emit roomsChanged();
}

// ---- tokens and servers --------------------------------------------------------------------------------------

QJsonObject RoomManager::_parseToken(const QString &token, const QString &prefix)
{
    // Tokens may be pasted with spaces or line breaks from a chat window
    QString compact = token;
    compact.remove(QRegularExpression(QStringLiteral("\\s")));
    if (!compact.startsWith(prefix)) {
        return QJsonObject();
    }
    const QByteArray payload = QByteArray::fromBase64(compact.mid(prefix.size()).toLatin1(),
                                                      QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QJsonObject object = QJsonDocument::fromJson(payload).object();
    static const QRegularExpression fingerprintRe(QStringLiteral("^[0-9a-fA-F]{64}$"));
    if (object.value(QLatin1String("h")).toString().isEmpty() || (object.value(QLatin1String("p")).toInt() <= 0) ||
        !fingerprintRe.match(object.value(QLatin1String("f")).toString()).hasMatch()) {
        return QJsonObject();
    }
    return object;
}

QString RoomManager::_serverFromToken(const QJsonObject &token)
{
    Server server;
    server.host = token.value(QLatin1String("h")).toString();
    server.port = token.value(QLatin1String("p")).toInt();
    const QString key = server.key();
    if (Server *existing = _server(key)) {
        // A token is handed over by someone the user trusts; the newest one names the hub's certificate
        existing->fingerprint = token.value(QLatin1String("f")).toString().toLower();
        return key;
    }
    server.fingerprint = token.value(QLatin1String("f")).toString().toLower();
    _servers.insert(key, server);
    return key;
}

RoomManager::Server *RoomManager::_server(const QString &serverKey)
{
    const auto it = _servers.find(serverKey);
    return (it == _servers.end()) ? nullptr : &it.value();
}

// ---- HTTPS ---------------------------------------------------------------------------------------------------

QNetworkReply *RoomManager::_send(const QString &serverKey, const QByteArray &method, const QString &path,
                                  const QByteArray &contentType, const QByteArray &data, QIODevice *device, int timeoutMs,
                                  const QList<QPair<QByteArray, QByteArray>> &headers)
{
    const Server *const server = _server(serverKey);
    if (!server) {
        return nullptr;
    }

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(server->host);
    url.setPort(server->port);
    url.setPath(path);

    QNetworkRequest request(url);
    request.setTransferTimeout(timeoutMs);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    if (!contentType.isEmpty()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromLatin1(contentType));
    }
    if (!server->token.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + server->token.toUtf8());
    }
    for (const auto &header : headers) {
        request.setRawHeader(header.first, header.second);
    }
    // The hub's certificate is self-signed: it is checked against the fingerprint from the token instead
    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(ssl);

    QNetworkReply *reply = nullptr;
    if (device) {
        request.setHeader(QNetworkRequest::ContentLengthHeader, device->size());
        reply = _network->sendCustomRequest(request, method, device);
    } else {
        reply = _network->sendCustomRequest(request, method, data);
    }

    const QString fingerprint = server->fingerprint;
    // Emitted after the TLS handshake and before any data (the token) is sent
    (void) connect(reply, &QNetworkReply::encrypted, reply, [reply, fingerprint]() {
        const QString actual = QString::fromLatin1(reply->sslConfiguration().peerCertificate().digest(QCryptographicHash::Sha256).toHex());
        if (actual.compare(fingerprint, Qt::CaseInsensitive) != 0) {
            qCWarning(RoomManagerLog) << "Certificate fingerprint mismatch for" << reply->url().host();
            reply->setProperty("nxPinFailed", true);
            reply->abort();
        }
    });
    return reply;
}

void RoomManager::_whenFinished(QNetworkReply *reply, const Done &done)
{
    _setPending(1);
    (void) connect(reply, &QNetworkReply::finished, this, [this, reply, done]() {
        reply->deleteLater();
        _setPending(-1);

        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonObject json;
        if (reply->property("nxPinFailed").toBool()) {
            status = 0;
            json = QJsonObject{{QStringLiteral("error"), QStringLiteral("pin_failed")}};
        } else if (status == 0) {
            json = QJsonObject{{QStringLiteral("error"), QStringLiteral("network")}, {QStringLiteral("message"), reply->errorString()}};
        } else {
            json = QJsonDocument::fromJson(reply->readAll()).object();
        }
        qCDebug(RoomManagerLog) << reply->operation() << reply->url().path() << status << json.value(QLatin1String("error")).toString();
        done(status, json);
    });
}

void RoomManager::_request(const QString &serverKey, const QString &method, const QString &path, const QJsonObject &body,
                           const Done &done)
{
    const QByteArray data = (method == QLatin1String("GET")) ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply *const reply = _send(serverKey, method.toLatin1(), path, QByteArrayLiteral("application/json"), data, nullptr,
                                       kRequestTimeoutMs);
    if (!reply) {
        done(0, QJsonObject{{QStringLiteral("error"), QStringLiteral("no_server")}});
        return;
    }
    _whenFinished(reply, done);
}

QString RoomManager::_errorText(int status, const QJsonObject &reply) const
{
    const QString code = reply.value(QLatin1String("error")).toString();
    if (code == QLatin1String("pin_failed")) {
        return tr("The server's certificate does not match the token. The connection was refused for safety.");
    }
    if ((code == QLatin1String("network")) || (status == 0)) {
        return tr("The room server is not reachable (%1).").arg(reply.value(QLatin1String("message")).toString());
    }
    if (code == QLatin1String("busy")) {
        const QString holder = reply.value(QLatin1String("holder")).toString();
        return holder.isEmpty()
            ? tr("Cannot connect to the vehicle. It is in use by another connection.")
            : tr("Cannot connect to the vehicle. It is occupied by another operator: %1.").arg(holder);
    }
    static const QHash<QString, const char *> messages = {
        {QStringLiteral("bad_invite"), QT_TR_NOOP("The room token is not valid. Ask the admin for a new one.")},
        {QStringLiteral("bad_secret"), QT_TR_NOOP("Wrong server key.")},
        {QStringLiteral("rate_limited"), QT_TR_NOOP("Too many attempts. Try again in a minute.")},
        {QStringLiteral("no_access"), QT_TR_NOOP("No access: the admin has not given you this vehicle or has turned your access off.")},
        {QStringLiteral("not_admin"), QT_TR_NOOP("Only the room admin can do this.")},
        {QStringLiteral("not_server_admin"), QT_TR_NOOP("Only a server admin can create rooms. Add the server key first.")},
        {QStringLiteral("no_tunnel"), QT_TR_NOOP("The vehicle's tunnel was not found on the server. Ask the admin to check the vehicle's WireGuard settings.")},
        {QStringLiteral("no_lease"), QT_TR_NOOP("The vehicle is no longer reserved for you.")},
        {QStringLiteral("unauthorized"), QT_TR_NOOP("The server does not know this computer. Enter the room token again.")},
        {QStringLiteral("no_board"), QT_TR_NOOP("The vehicle was removed from the room.")},
        {QStringLiteral("no_room"), QT_TR_NOOP("The room was not found.")},
        {QStringLiteral("bad_key"), QT_TR_NOOP("The server did not accept this computer's WireGuard key.")},
        {QStringLiteral("other_room"), QT_TR_NOOP("This vehicle already belongs to another room.")},
        {QStringLiteral("self"), QT_TR_NOOP("You cannot take away your own admin rights.")},
        {QStringLiteral("bad_address"), QT_TR_NOOP("The tunnel address of the vehicle is not valid.")},
        {QStringLiteral("bad_audio"), QT_TR_NOOP("The file could not be read as audio.")},
        {QStringLiteral("mp3_only"), QT_TR_NOOP("Only MP3 files can be added: ffmpeg is not installed on the server.")},
        {QStringLiteral("too_large"), QT_TR_NOOP("The file is too large.")},
        {QStringLiteral("quota"), QT_TR_NOOP("The room's playlist is full. Delete some tracks first.")},
        {QStringLiteral("radio_off"), QT_TR_NOOP("The radio of this room is off.")},
        {QStringLiteral("no_track"), QT_TR_NOOP("The track was not found.")},
    };
    if (const auto it = messages.constFind(code); it != messages.cend()) {
        return tr(it.value());
    }
    const QString message = reply.value(QLatin1String("message")).toString();
    return message.isEmpty() ? tr("Server error (%1).").arg(status) : message;
}

// ---- this computer's WireGuard key ---------------------------------------------------------------------------

QString RoomManager::_devicePrivateKey(QString *error)
{
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    QString key = SecretProtector::unprotect(settings.value(QLatin1String(kDeviceKeyKey)).toString());
    if (!key.isEmpty()) {
        return key;
    }

    key = WireGuardTunnel::generatePrivateKey();
    const QString publicKey = WireGuardTunnel::publicKey(key);
    if (key.isEmpty() || publicKey.isEmpty()) {
        if (error) {
            *error = tr("WireGuard for Windows is not installed. It is needed to connect to the vehicles of a room.");
        }
        return QString();
    }
    settings.setValue(QLatin1String(kDeviceKeyKey), SecretProtector::protect(key));
    settings.setValue(QLatin1String(kDevicePublicKeyKey), publicKey);
    _deviceCode = publicKey.left(8);
    emit deviceChanged();

    // A new key (e.g. the stored one could not be decrypted any more) has to reach the hubs before the next connection
    for (const Server &server : std::as_const(_servers)) {
        if (!server.token.isEmpty()) {
            _request(server.key(), QStringLiteral("PATCH"), QStringLiteral("/v1/me"),
                     QJsonObject{{QStringLiteral("wg_public_key"), publicKey}}, [](int, const QJsonObject &) {});
        }
    }
    return key;
}

QString RoomManager::_devicePublicKey(QString *error)
{
    if (_devicePrivateKey(error).isEmpty()) {
        return QString();
    }
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    return settings.value(QLatin1String(kDevicePublicKeyKey)).toString();
}

// ---- joining and rooms ---------------------------------------------------------------------------------------

void RoomManager::addServerKey(const QString &token)
{
    const QJsonObject parsed = _parseToken(token, QLatin1String(kServerTokenPrefix));
    if (parsed.isEmpty() || parsed.value(QLatin1String("s")).toString().isEmpty()) {
        _setError(tr("This is not a server key. A server key starts with %1").arg(QLatin1String(kServerTokenPrefix)));
        return;
    }
    QString error;
    const QString publicKey = _devicePublicKey(&error);
    if (publicKey.isEmpty()) {
        _setError(error);
        return;
    }

    const QString key = _serverFromToken(parsed);
    const bool known = !_server(key)->token.isEmpty();
    const QJsonObject body{
        {QStringLiteral("server_secret"), parsed.value(QLatin1String("s")).toString()},
        {QStringLiteral("name"), _deviceName},
        {QStringLiteral("wg_public_key"), publicKey},
    };
    _request(key, QStringLiteral("POST"), QStringLiteral("/v1/server/register"), body, [this, key, known](int status, const QJsonObject &reply) {
        Server *const server = _server(key);
        if (!server) {
            return;
        }
        if (status != 200) {
            if (!known) {
                (void) _servers.remove(key);
            }
            _setError(_errorText(status, reply));
            return;
        }
        const QString newToken = reply.value(QLatin1String("device_token")).toString();
        if (!newToken.isEmpty()) {
            server->token = newToken;
        }
        server->deviceId = reply.value(QLatin1String("device_id")).toString();
        server->serverAdmin = true;
        _saveServers();
        _setError(QString());
        emit roomsChanged();
        emit info(tr("The server %1 was added. You can create rooms on it now.").arg(key));
        refresh();
    });
}

void RoomManager::createRoom(const QString &serverKey, const QString &name)
{
    QString key = serverKey;
    if (key.isEmpty()) {
        for (const Server &server : std::as_const(_servers)) {
            if (server.serverAdmin && !server.token.isEmpty()) {
                key = server.key();
                break;
            }
        }
    }
    if (key.isEmpty()) {
        _setError(tr("Add the server key first: rooms are created on your server."));
        return;
    }
    if (name.trimmed().isEmpty()) {
        _setError(tr("Enter a room name."));
        return;
    }

    _request(key, QStringLiteral("POST"), QStringLiteral("/v1/rooms"), QJsonObject{{QStringLiteral("name"), name.trimmed()}},
             [this, key](int status, const QJsonObject &reply) {
        if (status != 200) {
            _setError(_errorText(status, reply));
            return;
        }
        const QString roomKey = _roomKey(key, reply.value(QLatin1String("room_id")).toString());
        _updateRoomEntry(roomKey, QVariantMap{
            {QStringLiteral("name"), reply.value(QLatin1String("name")).toString()},
            {QStringLiteral("role"), QStringLiteral("admin")},
            {QStringLiteral("status"), QStringLiteral("active")},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("inviteToken"), reply.value(QLatin1String("invite_token")).toString()},
        });
        _setError(QString());
        setCurrentRoom(roomKey);
        emit info(tr("Room %1 was created. Give its token to the operators.").arg(reply.value(QLatin1String("name")).toString()));
    });
}

void RoomManager::joinRoom(const QString &token)
{
    const QJsonObject parsed = _parseToken(token, QLatin1String(kRoomTokenPrefix));
    if (parsed.isEmpty() || parsed.value(QLatin1String("r")).toString().isEmpty() || parsed.value(QLatin1String("k")).toString().isEmpty()) {
        _setError(tr("This is not a room token. A room token starts with %1").arg(QLatin1String(kRoomTokenPrefix)));
        return;
    }
    QString error;
    const QString publicKey = _devicePublicKey(&error);
    if (publicKey.isEmpty()) {
        _setError(error);
        return;
    }

    const QString key = _serverFromToken(parsed);
    const bool known = !_server(key)->token.isEmpty();
    const QJsonObject body{
        {QStringLiteral("room_id"), parsed.value(QLatin1String("r")).toString()},
        {QStringLiteral("invite_secret"), parsed.value(QLatin1String("k")).toString()},
        {QStringLiteral("name"), _deviceName},
        {QStringLiteral("wg_public_key"), publicKey},
    };
    _request(key, QStringLiteral("POST"), QStringLiteral("/v1/join"), body, [this, key, known](int status, const QJsonObject &reply) {
        Server *const server = _server(key);
        if (!server) {
            return;
        }
        if (status != 200) {
            if (!known) {
                (void) _servers.remove(key);
            }
            _setError(_errorText(status, reply));
            return;
        }
        const QString newToken = reply.value(QLatin1String("device_token")).toString();
        if (!newToken.isEmpty()) {
            server->token = newToken;
        }
        server->deviceId = reply.value(QLatin1String("device_id")).toString();
        _saveServers();

        const QString roomKey = _roomKey(key, reply.value(QLatin1String("room_id")).toString());
        const QString roomName = reply.value(QLatin1String("room_name")).toString();
        const QString roomStatus = reply.value(QLatin1String("status")).toString();
        _updateRoomEntry(roomKey, QVariantMap{
            {QStringLiteral("name"), roomName},
            {QStringLiteral("role"), reply.value(QLatin1String("role")).toString()},
            {QStringLiteral("status"), roomStatus},
            {QStringLiteral("enabled"), true},
        });
        _setError(QString());
        if (_activeBoard.isEmpty()) {
            setCurrentRoom(roomKey);
        }
        emit info((roomStatus == QLatin1String("active"))
                      ? tr("You are in room %1.").arg(roomName)
                      : tr("The request to join %1 was sent. The admin has to let you in.").arg(roomName));
    });
}

void RoomManager::leaveRoom(const QString &roomKey)
{
    if (_roomEntry(roomKey).isEmpty()) {
        return;
    }
    if ((roomKey == _currentRoom) && (!_activeBoard.isEmpty() || !_connectingBoard.isEmpty())) {
        _setError(tr("Disconnect from the vehicle before leaving the room."));
        return;
    }

    _request(_serverKeyOf(roomKey), QStringLiteral("POST"), QStringLiteral("/v1/rooms/%1/leave").arg(_roomIdOf(roomKey)), QJsonObject(),
             [](int, const QJsonObject &) {});
    if (roomKey == _currentRoom) {
        setCurrentRoom(QString());
    }
    _removeRoomEntry(roomKey);
}

void RoomManager::refresh()
{
    for (const Server &server : std::as_const(_servers)) {
        if (server.token.isEmpty()) {
            continue;
        }
        const QString key = server.key();
        _request(key, QStringLiteral("GET"), QStringLiteral("/v1/me"), QJsonObject(), [this, key](int status, const QJsonObject &reply) {
            Server *const server = _server(key);
            if (!server) {
                return;
            }
            if (status == 200) {
                const bool wasAdmin = server->serverAdmin;
                server->deviceId = reply.value(QLatin1String("device_id")).toString();
                server->serverAdmin = reply.value(QLatin1String("server_admin")).toBool();
                if (wasAdmin != server->serverAdmin) {
                    _saveServers();
                }
                _setServerRooms(key, reply.value(QLatin1String("rooms")).toArray());
                if (_serverKeyOf(_currentRoom) == key) {
                    _scheduleRoomRefresh();
                }
            } else if (status == 401) {
                // The hub no longer knows this computer (e.g. its database was set up again)
                server->token.clear();
                server->serverAdmin = false;
                _saveServers();
                _setServerRooms(key, QJsonArray());
                _setError(_errorText(status, reply));
            }
            // Other failures (no internet) keep the rooms as they were
        });
    }
    if (_currentRoom.isEmpty()) {
        _refreshCurrentRoom();
    }
}

void RoomManager::_scheduleRoomRefresh()
{
    if (!_shuttingDown) {
        _roomRefreshTimer.start();
    }
}

void RoomManager::_refreshCurrentRoom()
{
    const QString role = currentRole();
    if (_currentRoom.isEmpty() || role.isEmpty()) {
        _setBoards(QJsonArray());
        _setMembers(QJsonArray());
        _setRequests(QJsonArray());
        _setRadio(0, QJsonObject());
        return;
    }
    refreshRadio();

    const QString roomKey = _currentRoom;
    const QString serverKey = _serverKeyOf(roomKey);
    const QString roomPath = QStringLiteral("/v1/rooms/") + _roomIdOf(roomKey);
    _request(serverKey, QStringLiteral("GET"), roomPath + QStringLiteral("/boards"), QJsonObject(), [this, roomKey](int status, const QJsonObject &reply) {
        if (roomKey != _currentRoom) {
            return;
        }
        if (status == 200) {
            _setBoards(reply.value(QLatin1String("boards")).toArray());
        } else if ((status == 403) || (status == 404)) {
            _setBoards(QJsonArray());
        }
    });
    if (role != QLatin1String("admin")) {
        return;
    }
    _request(serverKey, QStringLiteral("GET"), roomPath + QStringLiteral("/requests"), QJsonObject(), [this, roomKey](int status, const QJsonObject &reply) {
        if ((roomKey == _currentRoom) && (status == 200)) {
            _setRequests(reply.value(QLatin1String("requests")).toArray());
        }
    });
    _request(serverKey, QStringLiteral("GET"), roomPath + QStringLiteral("/members"), QJsonObject(), [this, roomKey](int status, const QJsonObject &reply) {
        if ((roomKey == _currentRoom) && (status == 200)) {
            _setMembers(reply.value(QLatin1String("members")).toArray());
        }
    });
}

void RoomManager::_setBoards(const QJsonArray &boards)
{
    const bool admin = (currentRole() == QLatin1String("admin"));

    QSet<QString> linkedIds;
    if (admin) {
        for (const SharedLinkConfigurationPtr &config : LinkManager::instance()->linkConfigurationList()) {
            if (config && !config->isDynamic() && (config->room() == _currentRoom) && !config->roomBoardId().isEmpty()) {
                linkedIds.insert(config->roomBoardId());
            }
        }
    }

    QVariantList list;
    QHash<QString, QVariantMap> bundles;
    QHash<QString, QString> names;
    QHash<QString, int> versions;
    for (const QJsonValue &value : boards) {
        const QJsonObject board = value.toObject();
        const QString id = board.value(QLatin1String("board_id")).toString();
        const QString name = board.value(QLatin1String("name")).toString();
        list.append(QVariantMap{
            {QStringLiteral("boardId"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("occupiedBy"), board.value(QLatin1String("occupied_by")).toString()},
            {QStringLiteral("occupiedByMe"), board.value(QLatin1String("occupied_by_me")).toBool()},
            {QStringLiteral("grantedTo"), board.value(QLatin1String("granted_to")).toArray().toVariantList()},
            {QStringLiteral("hasLink"), linkedIds.contains(id)},
        });
        if (!admin) {
            bundles.insert(id, board.value(QLatin1String("bundle")).toObject().toVariantMap());
            names.insert(id, name);
            versions.insert(id, board.value(QLatin1String("version")).toInt());
        }
    }

    if (list != _boards) {
        _boards = list;
        emit boardsChanged();
    }
    _boardBundles = bundles;
    _boardNames = names;
    _boardVersions = versions;

    if (operatorMode()) {
        _rebuildRoomLinks();
    } else {
        _removeRoomLinks();
    }
}

void RoomManager::_setMembers(const QJsonArray &members)
{
    QVariantList list;
    for (const QJsonValue &value : members) {
        const QJsonObject member = value.toObject();
        list.append(QVariantMap{
            {QStringLiteral("deviceId"), member.value(QLatin1String("device_id")).toString()},
            {QStringLiteral("name"), member.value(QLatin1String("name")).toString()},
            {QStringLiteral("role"), member.value(QLatin1String("role")).toString()},
            {QStringLiteral("enabled"), member.value(QLatin1String("enabled")).toBool()},
            {QStringLiteral("lastSeen"), member.value(QLatin1String("last_seen")).toDouble()},
            {QStringLiteral("deviceCode"), member.value(QLatin1String("device_code")).toString()},
            {QStringLiteral("boards"), member.value(QLatin1String("boards")).toArray().toVariantList()},
            {QStringLiteral("activeBoard"), member.value(QLatin1String("active_board")).toString()},
            {QStringLiteral("isMe"), member.value(QLatin1String("is_me")).toBool()},
        });
    }
    if (list != _members) {
        _members = list;
        emit membersChanged();
    }
}

void RoomManager::_setRequests(const QJsonArray &requests)
{
    QVariantList list;
    for (const QJsonValue &value : requests) {
        const QJsonObject request = value.toObject();
        list.append(QVariantMap{
            {QStringLiteral("deviceId"), request.value(QLatin1String("device_id")).toString()},
            {QStringLiteral("name"), request.value(QLatin1String("name")).toString()},
            {QStringLiteral("requested"), request.value(QLatin1String("requested")).toDouble()},
            {QStringLiteral("deviceCode"), request.value(QLatin1String("device_code")).toString()},
        });
    }
    if (list != _requests) {
        _requests = list;
        emit membersChanged();
    }
}

// ---- admin actions on the current room -----------------------------------------------------------------------

void RoomManager::_roomRequest(const QString &method, const QString &subPath, const QJsonObject &body, const std::function<void()> &after)
{
    if (_currentRoom.isEmpty()) {
        return;
    }
    const QString roomKey = _currentRoom;
    _request(_serverKeyOf(roomKey), method, QStringLiteral("/v1/rooms/") + _roomIdOf(roomKey) + subPath, body,
             [this, roomKey, after](int status, const QJsonObject &reply) {
        if (status != 200) {
            _setError(_errorText(status, reply));
            return;
        }
        if (roomKey != _currentRoom) {
            return;
        }
        if (reply.contains(QLatin1String("members"))) {
            _setMembers(reply.value(QLatin1String("members")).toArray());
        }
        if (reply.contains(QLatin1String("requests"))) {
            _setRequests(reply.value(QLatin1String("requests")).toArray());
        }
        if (reply.contains(QLatin1String("invite_token"))) {
            _updateRoomEntry(roomKey, QVariantMap{
                {QStringLiteral("name"), reply.value(QLatin1String("name")).toString()},
                {QStringLiteral("inviteToken"), reply.value(QLatin1String("invite_token")).toString()},
            });
        }
        if (after) {
            after();
        }
        _scheduleRoomRefresh();
    });
}

void RoomManager::renameRoom(const QString &name)
{
    if (!name.trimmed().isEmpty()) {
        _roomRequest(QStringLiteral("PATCH"), QString(), QJsonObject{{QStringLiteral("name"), name.trimmed()}});
    }
}

void RoomManager::approveRequest(const QString &deviceId, bool asAdmin)
{
    _roomRequest(QStringLiteral("POST"), QStringLiteral("/requests/") + deviceId,
                 QJsonObject{{QStringLiteral("action"), QStringLiteral("approve")},
                             {QStringLiteral("role"), asAdmin ? QStringLiteral("admin") : QStringLiteral("operator")}});
}

void RoomManager::rejectRequest(const QString &deviceId)
{
    _roomRequest(QStringLiteral("POST"), QStringLiteral("/requests/") + deviceId,
                 QJsonObject{{QStringLiteral("action"), QStringLiteral("reject")}});
}

void RoomManager::setMemberEnabled(const QString &deviceId, bool enabled)
{
    _roomRequest(QStringLiteral("PATCH"), QStringLiteral("/members/") + deviceId, QJsonObject{{QStringLiteral("enabled"), enabled}});
}

void RoomManager::removeMember(const QString &deviceId)
{
    _roomRequest(QStringLiteral("DELETE"), QStringLiteral("/members/") + deviceId, QJsonObject());
}

void RoomManager::setBoardGranted(const QString &deviceId, const QString &boardId, bool granted)
{
    _roomRequest(granted ? QStringLiteral("PUT") : QStringLiteral("DELETE"),
                 QStringLiteral("/members/%1/boards/%2").arg(deviceId, boardId), QJsonObject());
}

void RoomManager::freeBoard(const QString &boardId)
{
    _roomRequest(QStringLiteral("POST"), QStringLiteral("/boards/%1/free").arg(boardId), QJsonObject());
}

void RoomManager::deleteBoard(const QString &boardId)
{
    _roomRequest(QStringLiteral("DELETE"), QStringLiteral("/boards/") + boardId, QJsonObject(), [this, boardId]() {
        (void) _published.remove(boardId);
    });
}

void RoomManager::rotateInvite()
{
    _roomRequest(QStringLiteral("POST"), QStringLiteral("/invite"), QJsonObject(),
                 [this]() { emit info(tr("The room has a new token. The old one no longer works.")); });
}

// ---- radio -----------------------------------------------------------------------------------------------------

void RoomManager::_setRadio(int status, const QJsonObject &reply)
{
    QVariantMap radio;
    if (status == 200) {
        radio[QStringLiteral("supported")] = true;
        radio[QStringLiteral("onAir")] = reply.value(QLatin1String("on_air")).toBool();
        radio[QStringLiteral("convert")] = reply.value(QLatin1String("convert")).toBool();
        const QJsonObject now = reply.value(QLatin1String("now")).toObject();
        if (!now.isEmpty()) {
            radio[QStringLiteral("now")] = QVariantMap{
                {QStringLiteral("trackId"), now.value(QLatin1String("track_id")).toString()},
                {QStringLiteral("name"), now.value(QLatin1String("name")).toString()},
                {QStringLiteral("duration"), now.value(QLatin1String("duration")).toDouble()},
                {QStringLiteral("elapsed"), now.value(QLatin1String("elapsed")).toDouble()},
            };
        }
        if (reply.contains(QLatin1String("tracks"))) {
            QVariantList tracks;
            for (const QJsonValue &value : reply.value(QLatin1String("tracks")).toArray()) {
                const QJsonObject track = value.toObject();
                tracks.append(QVariantMap{
                    {QStringLiteral("trackId"), track.value(QLatin1String("track_id")).toString()},
                    {QStringLiteral("name"), track.value(QLatin1String("name")).toString()},
                    {QStringLiteral("duration"), track.value(QLatin1String("duration")).toDouble()},
                    {QStringLiteral("size"), track.value(QLatin1String("size")).toDouble()},
                });
            }
            radio[QStringLiteral("tracks")] = tracks;
            radio[QStringLiteral("listeners")] = reply.value(QLatin1String("listeners")).toInt();
            radio[QStringLiteral("bytes")] = reply.value(QLatin1String("bytes")).toDouble();
            radio[QStringLiteral("maxBytes")] = reply.value(QLatin1String("max_bytes")).toDouble();
            radio[QStringLiteral("maxUpload")] = reply.value(QLatin1String("max_upload")).toDouble();
        }
    } else if (status != 0) {
        // A hub without radio answers 404; any other answer leaves the radio unknown
        radio[QStringLiteral("supported")] = false;
    } else if (!_currentRoom.isEmpty() && !reply.isEmpty()) {
        // No connection to the hub: keep what was known
        return;
    }
    if (radio != _radio) {
        _radio = radio;
        emit radioChanged();
    }
}

void RoomManager::refreshRadio()
{
    if (_currentRoom.isEmpty() || currentRole().isEmpty()) {
        _setRadio(0, QJsonObject());
        return;
    }
    const QString roomKey = _currentRoom;
    _request(_serverKeyOf(roomKey), QStringLiteral("GET"), QStringLiteral("/v1/rooms/%1/radio").arg(_roomIdOf(roomKey)), QJsonObject(),
             [this, roomKey](int status, const QJsonObject &reply) {
        if (roomKey == _currentRoom) {
            _setRadio(status, reply);
        }
    });
}

void RoomManager::_radioRequest(const QString &method, const QString &subPath, const QJsonObject &body)
{
    if (_currentRoom.isEmpty()) {
        return;
    }
    const QString roomKey = _currentRoom;
    _request(_serverKeyOf(roomKey), method, QStringLiteral("/v1/rooms/%1/radio%2").arg(_roomIdOf(roomKey), subPath), body,
             [this, roomKey](int status, const QJsonObject &reply) {
        if (status != 200) {
            _setError(_errorText(status, reply));
            return;
        }
        if (roomKey == _currentRoom) {
            _setRadio(status, reply);
        }
    });
}

void RoomManager::setRadioOnAir(bool onAir)
{
    _radioRequest(QStringLiteral("PATCH"), QString(), QJsonObject{{QStringLiteral("on_air"), onAir}});
}

void RoomManager::nextRadioTrack()
{
    _radioRequest(QStringLiteral("POST"), QStringLiteral("/next"), QJsonObject());
}

void RoomManager::deleteRadioTrack(const QString &trackId)
{
    _radioRequest(QStringLiteral("DELETE"), QStringLiteral("/tracks/") + trackId, QJsonObject());
}

void RoomManager::moveRadioTrack(const QString &trackId, int position)
{
    _radioRequest(QStringLiteral("POST"), QStringLiteral("/tracks/%1/move").arg(trackId), QJsonObject{{QStringLiteral("position"), position}});
}

void RoomManager::_setRadioUpload(const QVariantMap &upload)
{
    if (upload != _radioUpload) {
        _radioUpload = upload;
        emit radioUploadChanged();
    }
}

void RoomManager::uploadRadioTracks(const QVariantList &files)
{
    if (currentRole() != QLatin1String("admin")) {
        return;
    }
    if (!_uploadQueue.isEmpty() || _uploadReply) {
        if (_uploadRoom != _currentRoom) {
            _setError(tr("Wait until the tracks of the other room are uploaded."));
            return;
        }
    } else {
        _uploadRoom = _currentRoom;
        _uploadCount = 0;
    }
    for (const QVariant &file : files) {
        const QUrl url(file.toString());
        const QString path = url.isLocalFile() ? url.toLocalFile() : file.toString();
        if (!path.isEmpty()) {
            _uploadQueue.append(path);
            _uploadCount++;
        }
    }
    if (!_uploadReply) {
        _startNextUpload();
    }
}

void RoomManager::cancelRadioUpload()
{
    _uploadQueue.clear();
    _uploadRoom.clear();
    if (_uploadReply) {
        _uploadReply->abort();
    }
    _setRadioUpload(QVariantMap());
}

void RoomManager::_startNextUpload()
{
    if (_uploadQueue.isEmpty() || _roomEntry(_uploadRoom).isEmpty()) {
        _uploadQueue.clear();
        _setRadioUpload(QVariantMap());
        return;
    }

    const QString path = _uploadQueue.takeFirst();
    const int index = _uploadCount - static_cast<int>(_uploadQueue.size());
    const QFileInfo info(path);
    const double maxUpload = _radio.value(QStringLiteral("maxUpload")).toDouble();
    if ((maxUpload > 0) && (info.size() > maxUpload)) {
        _setError(tr("%1 is larger than %2 MB and was not uploaded.").arg(info.fileName()).arg(qRound(maxUpload / (1024 * 1024))));
        _startNextUpload();
        return;
    }
    QFile *const file = new QFile(path);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        _setError(tr("Could not open %1.").arg(info.fileName()));
        _startNextUpload();
        return;
    }

    const QString roomKey = _uploadRoom;
    QNetworkReply *const reply = _send(_serverKeyOf(roomKey), "PUT", QStringLiteral("/v1/rooms/%1/radio/tracks").arg(_roomIdOf(roomKey)),
                                       QByteArrayLiteral("application/octet-stream"), QByteArray(), file, kUploadTimeoutMs,
                                       {{QByteArrayLiteral("X-Name"), QUrl::toPercentEncoding(info.fileName())}});
    if (!reply) {
        delete file;
        _uploadQueue.clear();
        _setRadioUpload(QVariantMap());
        return;
    }
    file->setParent(reply);
    _uploadReply = reply;

    const QVariantMap progress{
        {QStringLiteral("active"), true},
        {QStringLiteral("index"), index},
        {QStringLiteral("count"), _uploadCount},
        {QStringLiteral("name"), info.completeBaseName()},
        {QStringLiteral("progress"), 0.0},
    };
    _setRadioUpload(progress);
    (void) connect(reply, &QNetworkReply::uploadProgress, this, [this, progress](qint64 sent, qint64 total) {
        QVariantMap updated = progress;
        // The last part of the bar stands for the conversion on the server
        updated[QStringLiteral("progress")] = (total > 0) ? (0.9 * static_cast<double>(sent) / static_cast<double>(total)) : 0.0;
        _setRadioUpload(updated);
    });
    _whenFinished(reply, [this, roomKey, name = info.fileName()](int status, const QJsonObject &reply) {
        _uploadReply.clear();
        if (status == 200) {
            if (roomKey == _currentRoom) {
                _setRadio(status, reply);
            }
        } else if (!_uploadRoom.isEmpty()) {
            _setError(tr("%1: %2").arg(name, _errorText(status, reply)));
        }
        _startNextUpload();
    });
}

QNetworkReply *RoomManager::openRadioStream()
{
    if (_currentRoom.isEmpty() || currentRole().isEmpty()) {
        return nullptr;
    }
    // A live stream has no end: no inactivity timeout here, the player watches for stalls itself
    return _send(_serverKeyOf(_currentRoom), "GET", QStringLiteral("/v1/rooms/%1/radio/stream").arg(_roomIdOf(_currentRoom)),
                 QByteArray(), QByteArray(), nullptr, 0);
}

// ---- publishing the admin's links ----------------------------------------------------------------------------

QJsonObject RoomManager::_packBundle(LinkConfiguration *config, QString *error) const
{
    QJsonObject link;
    if (const UDPConfiguration *const udp = qobject_cast<const UDPConfiguration *>(config)) {
        link[QStringLiteral("type")] = QStringLiteral("udp");
        link[QStringLiteral("localPort")] = udp->localPort();
        link[QStringLiteral("hosts")] = QJsonArray::fromStringList(udp->hostList());
    } else if (const TCPConfiguration *const tcp = qobject_cast<const TCPConfiguration *>(config)) {
        link[QStringLiteral("type")] = QStringLiteral("tcp");
        link[QStringLiteral("host")] = tcp->host();
        link[QStringLiteral("port")] = tcp->port();
    } else {
        *error = tr("Link \"%1\": only UDP and TCP links can be given to operators.").arg(config->name());
        return QJsonObject();
    }
    link[QStringLiteral("highLatency")] = config->isHighLatency();
    link[QStringLiteral("pingAddress")] = config->pingAddress();
    link[QStringLiteral("boardId")] = config->boardId();
    link[QStringLiteral("throttleLimitEnabled")] = config->throttleLimitEnabled();
    link[QStringLiteral("pingThreshold1Ms")] = config->pingThreshold1Ms();
    link[QStringLiteral("throttlePercent1")] = config->throttlePercent1();
    link[QStringLiteral("pingThreshold2Ms")] = config->pingThreshold2Ms();
    link[QStringLiteral("throttlePercent2")] = config->throttlePercent2();

    QJsonObject bundle{{QStringLiteral("link"), link}};

    if (!config->vehicleModel().isEmpty() && VehicleModelManager::instance()) {
        const QVariantMap model = VehicleModelManager::instance()->model(config->vehicleModel());
        if (!model.isEmpty()) {
            bundle[QStringLiteral("model")] = QJsonObject::fromVariantMap(model);
        }
    }

    if (config->wireGuardProfile().isEmpty()) {
        *error = tr("Link \"%1\" has no WireGuard tunnel, so operators cannot reach the vehicle. Choose a tunnel for it.").arg(config->name());
        return QJsonObject();
    }
    const QVariantMap profile = WireGuardTunnel::profileSettings(config->wireGuardProfile());
    if (profile.isEmpty()) {
        *error = tr("The WireGuard tunnel \"%1\" of link \"%2\" was not found.").arg(config->wireGuardProfile(), config->name());
        return QJsonObject();
    }
    const QString address = operatorAddress(profile.value(QStringLiteral("address")).toString());
    if (address.isEmpty()) {
        *error = tr("The WireGuard tunnel \"%1\" has no IPv4 address.").arg(config->wireGuardProfile());
        return QJsonObject();
    }
    // Only the server side of the tunnel: the operator brings his own key, and the hub sets no preshared key
    QJsonObject wireguard{{QStringLiteral("address"), address}};
    for (const char *key : {"dns", "mtu", "peerPublicKey", "endpoint", "allowedIps", "keepalive"}) {
        const QString value = profile.value(QLatin1String(key)).toString().trimmed();
        if (!value.isEmpty()) {
            wireguard[QLatin1String(key)] = value;
        }
    }
    bundle[QStringLiteral("wireguard")] = wireguard;
    return bundle;
}

void RoomManager::publishRoomLinks()
{
    bool idsChanged = false;
    QSet<QString> seenIds;
    QStringList errors;

    for (const SharedLinkConfigurationPtr &shared : LinkManager::instance()->linkConfigurationList()) {
        LinkConfiguration *const config = shared.get();
        if (!config || config->isDynamic() || config->room().isEmpty()) {
            continue;
        }
        const QVariantMap room = _roomEntry(config->room());
        if ((room.value(QStringLiteral("role")).toString() != QLatin1String("admin")) ||
            (room.value(QStringLiteral("status")).toString() != QLatin1String("active"))) {
            continue;
        }

        QString error;
        const QJsonObject bundle = _packBundle(config, &error);
        if (!error.isEmpty()) {
            errors.append(error);
            continue;
        }
        // A copied link carries the id of its original; it becomes a vehicle of its own
        if (config->roomBoardId().isEmpty() || seenIds.contains(config->roomBoardId())) {
            config->setRoomBoardId(newBoardId());
            idsChanged = true;
        }
        const QString boardId = config->roomBoardId();
        seenIds.insert(boardId);

        const QJsonObject body{{QStringLiteral("name"), config->name()}, {QStringLiteral("bundle"), bundle}};
        const QString roomKey = config->room();
        const QString hash = hashOf(QJsonObject{{QStringLiteral("room"), roomKey}, {QStringLiteral("body"), body}});
        if (_published.value(boardId) == hash) {
            continue;
        }
        _request(_serverKeyOf(roomKey), QStringLiteral("PUT"), QStringLiteral("/v1/rooms/%1/boards/%2").arg(_roomIdOf(roomKey), boardId), body,
                 [this, boardId, hash, roomKey](int status, const QJsonObject &reply) {
            if (status != 200) {
                _setError(_errorText(status, reply));
                return;
            }
            _published.insert(boardId, hash);
            if (roomKey == _currentRoom) {
                _scheduleRoomRefresh();
            }
        });
    }

    if (idsChanged) {
        LinkManager::instance()->saveLinkConfigurationList();
    }
    if (!errors.isEmpty()) {
        _setError(errors.join(QLatin1Char('\n')));
    }
}

void RoomManager::unpublishLink(LinkConfiguration *config)
{
    if (!config || config->room().isEmpty() || config->roomBoardId().isEmpty()) {
        return;
    }
    if (_roomEntry(config->room()).value(QStringLiteral("role")).toString() != QLatin1String("admin")) {
        return;
    }
    const QString roomKey = config->room();
    const QString boardId = config->roomBoardId();
    (void) _published.remove(boardId);
    _request(_serverKeyOf(roomKey), QStringLiteral("DELETE"), QStringLiteral("/v1/rooms/%1/boards/%2").arg(_roomIdOf(roomKey), boardId),
             QJsonObject(), [this, roomKey](int status, const QJsonObject &reply) {
        if ((status != 200) && (reply.value(QLatin1String("error")).toString() != QLatin1String("no_board"))) {
            _setError(_errorText(status, reply));
        }
        if (roomKey == _currentRoom) {
            _scheduleRoomRefresh();
        }
    });
}

// ---- the operator's room links -------------------------------------------------------------------------------

LinkConfiguration *RoomManager::_roomLink(const QString &boardId) const
{
    for (const SharedLinkConfigurationPtr &config : LinkManager::instance()->linkConfigurationList()) {
        if (config && config->fromRoom() && (config->roomBoardId() == boardId)) {
            return config.get();
        }
    }
    return nullptr;
}

LinkConfiguration *RoomManager::_createRoomLink(const QString &boardId, const QString &name, const QVariantMap &bundle) const
{
    const QVariantMap link = bundle.value(QStringLiteral("link")).toMap();
    LinkConfiguration *config = nullptr;
    if (link.value(QStringLiteral("type")).toString() == QLatin1String("tcp")) {
        TCPConfiguration *const tcp = new TCPConfiguration(name);
        tcp->setHost(link.value(QStringLiteral("host")).toString());
        tcp->setPort(static_cast<quint16>(link.value(QStringLiteral("port")).toUInt()));
        config = tcp;
    } else {
        UDPConfiguration *const udp = new UDPConfiguration(name);
        udp->setLocalPort(static_cast<quint16>(link.value(QStringLiteral("localPort")).toUInt()));
        for (const QVariant &host : link.value(QStringLiteral("hosts")).toList()) {
            udp->addHost(host.toString());
        }
        config = udp;
    }

    const QString ref = QLatin1String(kRoomRefPrefix) + boardId;
    config->setHighLatency(link.value(QStringLiteral("highLatency")).toBool());
    config->setVehicleModel(bundle.value(QStringLiteral("model")).toMap().isEmpty() ? QString() : ref);
    config->setTunnelSettings(bundle.value(QStringLiteral("wireguard")).toMap().isEmpty() ? QString() : ref,
                              link.value(QStringLiteral("pingAddress")).toString(),
                              link.value(QStringLiteral("boardId")).toString(),
                              link.value(QStringLiteral("throttleLimitEnabled")).toBool(),
                              link.value(QStringLiteral("pingThreshold1Ms"), 300).toInt(),
                              link.value(QStringLiteral("throttlePercent1"), 50).toInt(),
                              link.value(QStringLiteral("pingThreshold2Ms"), 600).toInt(),
                              link.value(QStringLiteral("throttlePercent2"), 25).toInt());
    config->setFromRoom(boardId);
    return config;
}

void RoomManager::_rebuildRoomLinks()
{
    LinkManager *const linkManager = LinkManager::instance();
    QSet<QString> present;

    for (const SharedLinkConfigurationPtr &shared : linkManager->linkConfigurationList()) {
        LinkConfiguration *const config = shared.get();
        if (!config || !config->fromRoom()) {
            continue;
        }
        const QString id = config->roomBoardId();
        const bool available = _boardBundles.contains(id);
        if (available && (_linkVersions.value(id) == _boardVersions.value(id))) {
            present.insert(id);
            continue;
        }
        if (config->link() && available) {
            // Changed by the admin while connected: rebuilt once the link closes
            present.insert(id);
            continue;
        }
        if (id == _activeBoard) {
            // The vehicle was taken away from this operator while connected
            _endLease(false);
            emit connectFailed(tr("The admin took vehicle %1 away from you.").arg(config->name()));
        }
        (void) _linkVersions.remove(id);
        linkManager->removeConfiguration(config);
    }

    QVariantMap models;
    for (auto it = _boardBundles.cbegin(); it != _boardBundles.cend(); ++it) {
        const QVariantMap model = it.value().value(QStringLiteral("model")).toMap();
        if (!model.isEmpty()) {
            models.insert(QLatin1String(kRoomRefPrefix) + it.key(), model);
        }
        if (present.contains(it.key())) {
            continue;
        }
        LinkConfiguration *const config = _createRoomLink(it.key(), _boardNames.value(it.key()), it.value());
        (void) linkManager->addConfiguration(config);
        _linkVersions.insert(it.key(), _boardVersions.value(it.key()));
    }

    _roomModels = models;
    _applyRoomModels();
}

void RoomManager::_applyRoomModels()
{
    if (VehicleModelManager::instance()) {
        VehicleModelManager::instance()->setRoomModels(_roomModels);
    }
}

void RoomManager::_removeRoomLinks()
{
    LinkManager *const linkManager = LinkManager::instance();
    if (!linkManager) {
        return;
    }
    for (const SharedLinkConfigurationPtr &shared : linkManager->linkConfigurationList()) {
        if (shared && shared->fromRoom()) {
            if (shared->roomBoardId() == _activeBoard) {
                _endLease(true);
            }
            linkManager->removeConfiguration(shared.get());
        }
    }
    _linkVersions.clear();
    _roomModels.clear();
    _applyRoomModels();
}

// ---- connecting as an operator -------------------------------------------------------------------------------

QString RoomManager::_ensureTunnel(const QString &boardId, const QVariantMap &bundle)
{
    QVariantMap settings = bundle.value(QStringLiteral("wireguard")).toMap();
    if (settings.isEmpty()) {
        return QString();
    }
    QString error;
    const QString privateKey = _devicePrivateKey(&error);
    if (privateKey.isEmpty()) {
        return error;
    }

    QJsonObject hashed = QJsonObject::fromVariantMap(settings);
    hashed[QStringLiteral("devicePublicKey")] = _devicePublicKey(nullptr);
    const QString hash = hashOf(hashed);

    const QVariantMap stored = _tunnels.value(boardId).toMap();
    QString tunnel = stored.value(QStringLiteral("tunnel")).toString();
    if (tunnel.isEmpty()) {
        tunnel = WireGuardTunnel::tunnelNameForLink(QStringLiteral("room-") + boardId);
    }
    if ((stored.value(QStringLiteral("hash")).toString() == hash) && (WireGuardTunnel::state(tunnel) != WireGuardTunnel::State::NotInstalled)) {
        return QString();
    }

    // Registering the tunnel with Windows asks for administrator rights once per vehicle (and again when the
    // admin changes its tunnel)
    settings[QStringLiteral("privateKey")] = privateKey;
    (void) settings.remove(QStringLiteral("presharedKey"));
    const QString installError = WireGuardTunnel::installFromSettings(tunnel, settings);
    if (!installError.isEmpty()) {
        return installError;
    }
    _tunnels.insert(boardId, QVariantMap{{QStringLiteral("tunnel"), tunnel}, {QStringLiteral("hash"), hash}});
    _saveTunnels();
    return QString();
}

void RoomManager::connectBoard(const QString &boardId)
{
    if (!operatorMode() || !_connectingBoard.isEmpty() || (boardId == _activeBoard)) {
        return;
    }
    if (!_roomLink(boardId) || !_boardBundles.contains(boardId)) {
        emit connectFailed(tr("This vehicle is no longer available."));
        return;
    }
    QString error;
    if (_devicePublicKey(&error).isEmpty()) {
        emit connectFailed(error);
        return;
    }
    // One vehicle at a time: the hub gives back any other vehicle this computer holds
    if (!_activeBoard.isEmpty()) {
        disconnectBoard();
    }

    _connectingBoard = boardId;
    emit activeBoardChanged();

    const QString serverKey = _serverKeyOf(_currentRoom);
    _request(serverKey, QStringLiteral("POST"), QStringLiteral("/v1/boards/%1/occupy").arg(boardId), QJsonObject(),
             [this, boardId, serverKey](int status, const QJsonObject &reply) {
        const QString leaseId = reply.value(QLatin1String("lease_id")).toString();
        const auto giveBack = [this, serverKey, boardId, leaseId]() {
            _request(serverKey, QStringLiteral("POST"), QStringLiteral("/v1/boards/%1/release").arg(boardId),
                     QJsonObject{{QStringLiteral("lease_id"), leaseId}}, [](int, const QJsonObject &) {});
        };

        if (_connectingBoard != boardId) {
            // Cancelled while waiting
            if (status == 200) {
                giveBack();
            }
            return;
        }
        if (status != 200) {
            _failConnect(_errorText(status, reply));
            return;
        }

        LinkConfiguration *const config = _roomLink(boardId);
        if (!config) {
            giveBack();
            _failConnect(tr("This vehicle is no longer available."));
            return;
        }
        const QString tunnelError = _ensureTunnel(boardId, _boardBundles.value(boardId));
        if (!tunnelError.isEmpty()) {
            giveBack();
            _failConnect(tunnelError);
            return;
        }
        _applyRoomModels();
        SharedLinkConfigurationPtr shared = sharedConfig(config);
        if (!shared || !LinkManager::instance()->createConnectedLink(shared)) {
            // LinkManager has told the user what went wrong
            giveBack();
            _failConnect(QString());
            return;
        }
        _connectingBoard.clear();
        _startLease(boardId, serverKey, leaseId, reply.value(QLatin1String("heartbeat_s")).toInt(20), config);
    });
}

void RoomManager::_failConnect(const QString &message)
{
    _connectingBoard.clear();
    emit activeBoardChanged();
    if (!message.isEmpty()) {
        emit connectFailed(message);
    }
}

void RoomManager::disconnectBoard()
{
    if (!_connectingBoard.isEmpty()) {
        _connectingBoard.clear();
        emit activeBoardChanged();
    }
    if (_activeBoard.isEmpty()) {
        return;
    }
    LinkConfiguration *const config = _activeConfig;
    _endLease(true);
    if (config) {
        LinkManager::instance()->disconnectLinkConfiguration(config);
    }
}

void RoomManager::_startLease(const QString &boardId, const QString &serverKey, const QString &leaseId, int heartbeatSeconds,
                              LinkConfiguration *config)
{
    _activeBoard = boardId;
    _leaseServer = serverKey;
    _leaseId = leaseId;
    _activeConfig = config;
    _activeLinkSeen = (config->link() != nullptr);
    (void) connect(config, &LinkConfiguration::linkChanged, this, &RoomManager::_roomLinkChanged, Qt::UniqueConnection);
    _heartbeatTimer.start(qBound(5, heartbeatSeconds, 60) * 1000);
    emit activeBoardChanged();
    _scheduleRoomRefresh();
}

void RoomManager::_endLease(bool releaseOnHub)
{
    _heartbeatTimer.stop();
    if (_activeConfig) {
        (void) disconnect(_activeConfig, &LinkConfiguration::linkChanged, this, &RoomManager::_roomLinkChanged);
    }
    if (releaseOnHub && !_leaseId.isEmpty()) {
        _request(_leaseServer, QStringLiteral("POST"), QStringLiteral("/v1/boards/%1/release").arg(_activeBoard),
                 QJsonObject{{QStringLiteral("lease_id"), _leaseId}}, [](int, const QJsonObject &) {});
    }
    const bool wasActive = !_activeBoard.isEmpty();
    _activeBoard.clear();
    _leaseId.clear();
    _leaseServer.clear();
    _activeConfig.clear();
    _activeLinkSeen = false;
    if (!wasActive) {
        return;
    }
    emit activeBoardChanged();
    _scheduleRoomRefresh();
    // Links of vehicles the admin changed during the connection are rebuilt now, outside the link's signals
    QTimer::singleShot(0, this, [this]() {
        if (operatorMode() && !_shuttingDown) {
            _rebuildRoomLinks();
        }
    });
}

void RoomManager::_roomLinkChanged()
{
    if (!_activeConfig) {
        _endLease(true);
        return;
    }
    if (_activeConfig->link()) {
        _activeLinkSeen = true;
        return;
    }
    if (_activeLinkSeen) {
        _endLease(true);
    }
}

void RoomManager::_heartbeat()
{
    if (_leaseId.isEmpty()) {
        _heartbeatTimer.stop();
        return;
    }
    const QString leaseId = _leaseId;
    _request(_leaseServer, QStringLiteral("POST"), QStringLiteral("/v1/boards/%1/heartbeat").arg(_activeBoard),
             QJsonObject{{QStringLiteral("lease_id"), leaseId}}, [this, leaseId](int status, const QJsonObject &) {
        if ((leaseId != _leaseId) || (status == 200) || (status == 0)) {
            // Without internet the hub keeps the vehicle for a while; the next heartbeat tries again
            return;
        }
        _reoccupy();
    });
}

void RoomManager::_reoccupy()
{
    const QString boardId = _activeBoard;
    const QString leaseId = _leaseId;
    _request(_leaseServer, QStringLiteral("POST"), QStringLiteral("/v1/boards/%1/occupy").arg(boardId), QJsonObject(),
             [this, boardId, leaseId](int status, const QJsonObject &reply) {
        if ((boardId != _activeBoard) || (leaseId != _leaseId)) {
            return;
        }
        if (status == 200) {
            qCInfo(RoomManagerLog) << "Vehicle" << boardId << "taken again after the occupation lapsed";
            _leaseId = reply.value(QLatin1String("lease_id")).toString();
            return;
        }
        if (status == 0) {
            return;
        }
        LinkConfiguration *const config = _activeConfig;
        const QString name = config ? config->name() : boardId;
        _endLease(false);
        if (config) {
            LinkManager::instance()->disconnectLinkConfiguration(config);
        }
        emit connectFailed(tr("The connection to %1 was closed. %2").arg(name, _errorText(status, reply)));
    });
}

void RoomManager::_releaseBlocking()
{
    _shuttingDown = true;
    _refreshTimer.stop();
    _roomRefreshTimer.stop();
    if (!_leaseId.isEmpty()) {
        _endLease(true);
    }
    if (!busy()) {
        return;
    }
    // On exit the vehicle is given back right away (the closing window has usually sent that already), so the
    // next operator does not have to wait for the occupation to lapse
    QEventLoop loop;
    (void) connect(this, &RoomManager::busyChanged, &loop, [this, &loop]() {
        if (!busy()) {
            loop.quit();
        }
    });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    (void) loop.exec();
}
