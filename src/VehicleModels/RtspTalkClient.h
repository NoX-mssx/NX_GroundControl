#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QList>
#include <QtCore/QMultiMap>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QUrl>

#include <functional>

class QTcpSocket;

/// Sends audio to an IP camera's speaker over the ONVIF RTSP backchannel: the camera offers a send-only
/// audio track in its SDP when asked for it with the backchannel Require header, and takes G.711 RTP
/// interleaved on the RTSP connection.
class RtspTalkClient : public QObject
{
    Q_OBJECT

public:
    explicit RtspTalkClient(QObject *parent = nullptr);
    ~RtspTalkClient() override;

    /// @param url the camera's RTSP address; credentials in it are used when @p user is empty
    void start(const QUrl &url, const QString &user, const QString &password);
    void stop();
    bool playing() const { return _state == State::Playing; }

    /// Audio to play: 16 bit little endian mono at 8 kHz. Dropped until the camera is ready for it.
    void sendAudio(const QByteArray &pcm);

signals:
    /// The camera accepted the audio track; sendAudio() now reaches its speaker
    void ready();
    /// The connection could not be set up or was lost; the client is stopped
    void failed(const QString &message);

private:
    enum class State { Idle, Describing, SettingUp, StartingPlay, Playing };

    struct Response {
        int status = 0;
        QMultiMap<QString, QString> headers;
        QByteArray body;

        QString header(const QString &name) const { return headers.value(name.toLower()); }
    };
    using Handler = std::function<void(const Response &)>;

    void _send(const QByteArray &method, const QUrl &url, const QList<QPair<QString, QString>> &headers, const Handler &handler);
    void _resend();
    QByteArray _authorization(const QByteArray &method, const QString &uri);
    bool _challenge(const Response &response);
    void _onReadyRead();
    void _onResponse(const Response &response);
    void _describe();
    void _onDescribe(const Response &response);
    void _onSetup(const Response &response);
    void _onPlay(const Response &response);
    void _fail(const QString &message);
    void _close();
    void _sendPackets();

    QTcpSocket *_socket = nullptr;
    QTimer _timeout;
    QTimer _keepAlive;
    State _state = State::Idle;
    QByteArray _in;

    QUrl _url;
    QString _user;
    QString _password;
    int _cseq = 0;

    // The request in flight, kept to repeat it with credentials
    QByteArray _method;
    QUrl _requestUrl;
    QList<QPair<QString, QString>> _requestHeaders;
    Handler _handler;
    bool _authTried = false;

    // Authentication offered by the camera
    QString _authScheme;
    QString _realm;
    QString _nonce;
    QString _opaque;
    bool _qopAuth = false;
    int _nonceCount = 0;

    // Session
    QString _session;
    QString _base;
    QString _controlUrl;
    int _payloadType = 8;
    bool _ulaw = false;
    int _rtpChannel = 0;
    int _keepAliveSecs = 25;

    // Outgoing audio
    QByteArray _pcm;
    quint16 _sequence = 0;
    quint32 _timestamp = 0;
    quint32 _ssrc = 0;
    bool _firstPacket = true;
    /// Since the last packet went out; after a pause the time stamps jump by the pause, as RTP wants
    QElapsedTimer _sinceLastPacket;
};
