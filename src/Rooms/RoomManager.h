#pragma once

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtQmlIntegration/QtQmlIntegration>

#include <functional>

class LinkConfiguration;
class QIODevice;
class QJSEngine;
class QNetworkAccessManager;
class QNetworkReply;
class QQmlEngine;

/// Client of NX Hub, the room server on the VPS.
///
/// A room joins one admin and the operators who entered its token and were let in. The admin publishes vehicles
/// to the room (a link, its vehicle model and the server side of its WireGuard tunnel) and hands them to
/// operators. An operator only sees the vehicles given to him: they show up as links that are never saved and
/// whose settings are not shown; the tunnel is built with this computer's own WireGuard key.
///
/// Before connecting to a vehicle the operator "occupies" it on the hub, which puts this computer's key on the
/// vehicle's operator address on the VPS; a second operator is told the vehicle is busy. While connected the
/// manager keeps the occupation alive and gives the vehicle back when the link closes.
///
/// The hub is reached over HTTPS with its own certificate; the tokens carry its address and the certificate
/// fingerprint, and every connection is checked against that fingerprint.
class RoomManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_MOC_INCLUDE("LinkConfiguration.h")
    Q_PROPERTY(QString      deviceName      READ deviceName         WRITE setDeviceName NOTIFY deviceChanged)
    /// true when this computer may create rooms on a hub
    Q_PROPERTY(bool         serverAdmin     READ serverAdmin        NOTIFY roomsChanged)
    /// Hubs this computer is registered with: [{ key, serverAdmin }]
    Q_PROPERTY(QVariantList servers         READ servers            NOTIFY roomsChanged)
    /// [{ key, server, name, role, status, enabled, inviteToken }]; status "pending", "active" or "rejected"
    Q_PROPERTY(QVariantList rooms           READ rooms              NOTIFY roomsChanged)
    /// Key of the room the settings and connect menu show, empty for this computer's own links
    Q_PROPERTY(QString      currentRoom     READ currentRoom        WRITE setCurrentRoom NOTIFY currentRoomChanged)
    Q_PROPERTY(QVariantMap  currentRoomInfo READ currentRoomInfo    NOTIFY currentRoomChanged)
    /// "admin", "operator" or "" (no room, or the room is not active yet)
    Q_PROPERTY(QString      currentRole     READ currentRole        NOTIFY currentRoomChanged)
    Q_PROPERTY(bool         operatorMode    READ operatorMode       NOTIFY currentRoomChanged)
    /// Vehicles of the current room:
    ///     [{ boardId, name, occupiedBy, occupiedByMe, grantedTo, hasLink }]
    /// hasLink (admin): one of this computer's links is published as this vehicle
    Q_PROPERTY(QVariantList boards          READ boards             NOTIFY boardsChanged)
    /// Admin: operators of the current room
    ///     [{ deviceId, name, role, enabled, lastSeen, deviceCode, boards, activeBoard, isMe }]
    Q_PROPERTY(QVariantList members         READ members            NOTIFY membersChanged)
    /// Admin: join requests waiting for an answer: [{ deviceId, name, requested, deviceCode }]
    Q_PROPERTY(QVariantList requests        READ requests           NOTIFY membersChanged)
    /// Vehicle this computer is connected to through a room, empty when none
    Q_PROPERTY(QString      activeBoard     READ activeBoard        NOTIFY activeBoardChanged)
    Q_PROPERTY(QString      connectingBoard READ connectingBoard    NOTIFY activeBoardChanged)
    /// First characters of this computer's WireGuard public key, shown to the admin with a join request
    Q_PROPERTY(QString      deviceCode      READ deviceCode         NOTIFY deviceChanged)
    Q_PROPERTY(bool         busy            READ busy               NOTIFY busyChanged)
    Q_PROPERTY(QString      lastError       READ lastError          NOTIFY lastErrorChanged)
    /// Radio of the current room:
    ///     { supported, onAir, convert, now: { trackId, name, duration, elapsed } or null,
    ///       admin only: tracks: [{ trackId, name, duration, size }], listeners, bytes, maxBytes, maxUpload }
    /// supported is false while unknown and for a hub without radio.
    Q_PROPERTY(QVariantMap  radio           READ radio              NOTIFY radioChanged)
    /// Admin: tracks being uploaded: { active, index, count, name, progress (0..1) }
    Q_PROPERTY(QVariantMap  radioUpload     READ radioUpload        NOTIFY radioUploadChanged)
    /// Admin: video settings of one operator, after loadMemberVideo:
    ///     { deviceId, loading, error, reported: { setting: value } or null (what his NX Rover uses),
    ///       pending: { setting: value } or null (last sent), applied (he applied what was sent last),
    ///       reportedAt, pendingAt, appliedAt (seconds since 1970) }
    Q_PROPERTY(QVariantMap  memberVideo     READ memberVideo        NOTIFY memberVideoChanged)
    /// Names of the video settings an admin can send (VideoSettings facts, in the order of the Video page)
    Q_PROPERTY(QStringList  videoSettingKeys READ videoSettingKeys  CONSTANT)

public:
    explicit RoomManager(QObject *parent = nullptr);
    ~RoomManager() override;

    static RoomManager *instance();
    static RoomManager *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    /// Name of the WireGuard tunnel installed for a room vehicle, empty when not installed
    static QString tunnelForBoard(const QString &boardId);

    QString deviceName() const { return _deviceName; }
    void setDeviceName(const QString &name);
    bool serverAdmin() const;
    QVariantList servers() const;
    QVariantList rooms() const { return _rooms; }
    QString currentRoom() const { return _currentRoom; }
    void setCurrentRoom(const QString &roomKey);
    QVariantMap currentRoomInfo() const { return _roomEntry(_currentRoom); }
    QString currentRole() const;
    bool operatorMode() const { return currentRole() == QLatin1String("operator"); }
    QVariantList boards() const { return _boards; }
    QVariantList members() const { return _members; }
    QVariantList requests() const { return _requests; }
    QString activeBoard() const { return _activeBoard; }
    QString connectingBoard() const { return _connectingBoard; }
    QString deviceCode() const { return _deviceCode; }
    bool busy() const { return _pending > 0; }
    QString lastError() const { return _lastError; }
    QVariantMap radio() const { return _radio; }
    QVariantMap radioUpload() const { return _radioUpload; }
    QVariantMap memberVideo() const { return _memberVideo; }
    static QStringList videoSettingKeys();

    /// Opens the live radio stream of the current room; the caller owns the reply. nullptr when not in a room.
    QNetworkReply *openRadioStream();

    /// Admin: registers this computer with a hub using the server key printed at installation
    Q_INVOKABLE void addServerKey(const QString &token);
    /// Admin: creates a room on a hub this computer is registered with and switches to it
    Q_INVOKABLE void createRoom(const QString &serverKey, const QString &name);
    /// Operator: sends a join request with a room token
    Q_INVOKABLE void joinRoom(const QString &token);
    Q_INVOKABLE void leaveRoom(const QString &roomKey);
    Q_INVOKABLE void refresh();

    // Admin, current room
    Q_INVOKABLE void renameRoom(const QString &name);
    Q_INVOKABLE void approveRequest(const QString &deviceId, bool asAdmin);
    Q_INVOKABLE void rejectRequest(const QString &deviceId);
    Q_INVOKABLE void setMemberEnabled(const QString &deviceId, bool enabled);
    Q_INVOKABLE void removeMember(const QString &deviceId);
    Q_INVOKABLE void setBoardGranted(const QString &deviceId, const QString &boardId, bool granted);
    Q_INVOKABLE void freeBoard(const QString &boardId);
    /// Removes a vehicle from the room (for vehicles whose link was deleted while the hub was not reachable)
    Q_INVOKABLE void deleteBoard(const QString &boardId);
    Q_INVOKABLE void rotateInvite();
    /// Uploads the links of the rooms this computer administers whose link, model or tunnel changed since the
    /// last upload. Called after the links, models or tunnels are saved.
    Q_INVOKABLE void publishRoomLinks();
    /// Removes the vehicle of a link from its room, before the link is deleted
    Q_INVOKABLE void unpublishLink(LinkConfiguration *config);

    // Radio of the current room
    Q_INVOKABLE void refreshRadio();
    Q_INVOKABLE void setRadioOnAir(bool onAir);
    Q_INVOKABLE void nextRadioTrack();
    Q_INVOKABLE void deleteRadioTrack(const QString &trackId);
    Q_INVOKABLE void moveRadioTrack(const QString &trackId, int position);
    /// Uploads audio files (local paths or file URLs) one after the other
    Q_INVOKABLE void uploadRadioTracks(const QVariantList &files);
    Q_INVOKABLE void cancelRadioUpload();

    // Operator
    Q_INVOKABLE void connectBoard(const QString &boardId);
    Q_INVOKABLE void disconnectBoard();

    /// Admin: loads what video settings an operator uses and what was sent to him (memberVideo); called again for
    /// the same operator it refreshes without showing loading
    Q_INVOKABLE void loadMemberVideo(const QString &deviceId);
    /// Admin: sends video settings to one operator. His NX Rover applies them once, within a refresh (15 s);
    /// he may change them afterwards.
    Q_INVOKABLE void sendMemberVideo(const QString &deviceId, const QVariantMap &settings);
    /// This computer's values of the settings an admin can send
    Q_INVOKABLE QVariantMap localVideoSettings() const;

    Q_INVOKABLE void clearError();

signals:
    void deviceChanged();
    void roomsChanged();
    void currentRoomChanged();
    void boardsChanged();
    void membersChanged();
    void activeBoardChanged();
    void busyChanged();
    void lastErrorChanged();
    void radioChanged();
    void radioUploadChanged();
    void memberVideoChanged();
    /// Connecting to a room vehicle failed or the vehicle was taken back; shown to the user as a dialog
    void connectFailed(const QString &message);
    /// Something the user asked for went through (e.g. a join request was sent)
    void info(const QString &message);

private:
    struct Server {
        QString host;
        int port = 0;
        QString fingerprint;
        QString token;
        QString deviceId;
        bool serverAdmin = false;
        QString key() const { return host + QLatin1Char(':') + QString::number(port); }
    };
    using Done = std::function<void(int status, const QJsonObject &reply)>;

    void _load();
    void _saveServers() const;
    void _saveRooms() const;
    void _saveTunnels() const;
    void _setError(const QString &error);
    void _setPending(int delta);

    Server *_server(const QString &serverKey);
    /// The hub a token points to, added when new; the fingerprint is taken from the token
    QString _serverFromToken(const QJsonObject &token);
    static QJsonObject _parseToken(const QString &token, const QString &prefix);
    static QString _roomKey(const QString &serverKey, const QString &roomId);
    static QString _serverKeyOf(const QString &roomKey);
    static QString _roomIdOf(const QString &roomKey);
    QVariantMap _roomEntry(const QString &roomKey) const;
    void _setServerRooms(const QString &serverKey, const QJsonArray &rooms);
    void _updateRoomEntry(const QString &roomKey, const QVariantMap &values);
    void _removeRoomEntry(const QString &roomKey);

    void _request(const QString &serverKey, const QString &method, const QString &path, const QJsonObject &body,
                  const Done &done);
    /// Starts a request to a hub with its certificate pinned. timeoutMs 0: no inactivity timeout.
    QNetworkReply *_send(const QString &serverKey, const QByteArray &method, const QString &path,
                         const QByteArray &contentType, const QByteArray &data, QIODevice *device, int timeoutMs,
                         const QList<QPair<QByteArray, QByteArray>> &headers = {});
    /// Calls done with the status and JSON reply when a request finishes
    void _whenFinished(QNetworkReply *reply, const Done &done);
    /// Request on the current room as admin; replies with { members } or { requests } update the lists
    void _roomRequest(const QString &method, const QString &subPath, const QJsonObject &body,
                      const std::function<void()> &after = nullptr);
    QString _errorText(int status, const QJsonObject &reply) const;

    /// This computer's WireGuard key, created on first use (WireGuard for Windows must be installed)
    QString _devicePrivateKey(QString *error);
    QString _devicePublicKey(QString *error);

    void _refreshCurrentRoom();
    void _setRadio(int status, const QJsonObject &reply);
    void _radioRequest(const QString &method, const QString &subPath, const QJsonObject &body);
    void _setRadioUpload(const QVariantMap &upload);
    /// Reports this computer's video settings to the current room and applies, once, what the admin sent
    void _syncVideoSettings();
    void _watchVideoSettings();
    void _setMemberVideo(const QString &deviceId, const QJsonObject &reply);
    void _startNextUpload();
    /// Refreshes the current room shortly, once for a burst of changes
    void _scheduleRoomRefresh();
    void _setBoards(const QJsonArray &boards);
    void _setMembers(const QJsonArray &members);
    void _setRequests(const QJsonArray &requests);
    void _rebuildRoomLinks();
    void _removeRoomLinks();
    LinkConfiguration *_createRoomLink(const QString &boardId, const QString &name, const QVariantMap &bundle) const;
    LinkConfiguration *_roomLink(const QString &boardId) const;
    /// Hands the models of the room vehicles to the model manager (which QML may create after the links)
    void _applyRoomModels();

    QJsonObject _packBundle(LinkConfiguration *config, QString *error) const;
    QString _ensureTunnel(const QString &boardId, const QVariantMap &bundle);
    void _startLease(const QString &boardId, const QString &serverKey, const QString &leaseId, int heartbeatSeconds,
                     LinkConfiguration *config);
    void _endLease(bool releaseOnHub);
    void _releaseBlocking();
    void _heartbeat();
    /// Takes the vehicle again after the hub let the occupation lapse (e.g. the internet was down for a while)
    void _reoccupy();
    void _roomLinkChanged();
    void _failConnect(const QString &message);

    QNetworkAccessManager *_network = nullptr;
    QTimer _refreshTimer;
    QTimer _heartbeatTimer;
    QTimer _roomRefreshTimer;
    QTimer _videoSyncTimer;                         ///< Reports a changed video setting, once for a burst of changes
    bool _videoWatched = false;
    QVariantMap _memberVideo;

    QString _deviceName;
    QString _deviceCode;
    QHash<QString, Server> _servers;
    QVariantList _rooms;
    QString _currentRoom;
    QVariantList _boards;
    QVariantList _members;
    QVariantList _requests;
    QHash<QString, QVariantMap> _boardBundles;      ///< Operator: vehicles of the current room by id, as sent by the hub
    QHash<QString, QString> _boardNames;
    QHash<QString, int> _boardVersions;
    QHash<QString, int> _linkVersions;              ///< Vehicle version each room link was built from
    QVariantMap _roomModels;                        ///< "room:<vehicle id>" -> vehicle model
    QVariantMap _tunnels;                           ///< boardId -> { tunnel, hash }
    QHash<QString, QString> _published;             ///< boardId -> hash of the last upload

    QString _activeBoard;
    QString _connectingBoard;
    QString _leaseId;
    QString _leaseServer;
    QPointer<LinkConfiguration> _activeConfig;
    bool _activeLinkSeen = false;
    bool _shuttingDown = false;

    QVariantMap _radio;
    QVariantMap _radioUpload;
    QStringList _uploadQueue;
    QString _uploadRoom;
    int _uploadCount = 0;
    QPointer<QNetworkReply> _uploadReply;

    int _pending = 0;
    QString _lastError;

    static constexpr int kRefreshMs = 15000;
    static constexpr int kRequestTimeoutMs = 15000;
    /// An upload waits for the server to convert the file after the last byte
    static constexpr int kUploadTimeoutMs = 300000;
};
