#include "RtspTalkClient.h"

#include "QGCLoggingCategory.h"
#include "TalkAudio.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QRandomGenerator>
#include <QtCore/QRegularExpression>
#include <QtNetwork/QTcpSocket>

QGC_LOGGING_CATEGORY(NxRtspTalkLog, "VehicleModels.RtspTalk")

namespace {

constexpr int kSetupTimeoutMs = 10000;
constexpr const char *kBackchannel = "www.onvif.org/ver20/backchannel";
/// Audio waiting to be written beyond this is dropped, so a slow link does not delay the voice
constexpr qint64 kMaxQueuedBytes = 32 * 1024;

QByteArray md5Hex(const QString &text)
{
    return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Md5).toHex();
}

/// "key=value" and key="value" pairs of an authentication header
QMap<QString, QString> authParameters(const QString &header)
{
    QMap<QString, QString> parameters;
    static const QRegularExpression pair(QStringLiteral("(\\w+)=(?:\"([^\"]*)\"|([^,\\s]*))"));
    auto it = pair.globalMatch(header);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        parameters.insert(match.captured(1).toLower(), match.captured(2).isEmpty() ? match.captured(3) : match.captured(2));
    }
    return parameters;
}

QString withoutCredentials(const QUrl &url)
{
    QUrl clean = url;
    clean.setUserInfo(QString());
    return clean.toString(QUrl::FullyEncoded);
}

}  // namespace

RtspTalkClient::RtspTalkClient(QObject *parent)
    : QObject(parent)
{
    _timeout.setSingleShot(true);
    (void) connect(&_timeout, &QTimer::timeout, this, [this]() { _fail(tr("the camera did not answer")); });
    (void) connect(&_keepAlive, &QTimer::timeout, this, [this]() {
        if (_state == State::Playing) {
            _send("OPTIONS", _url, {}, [](const Response &) {});
        }
    });
}

RtspTalkClient::~RtspTalkClient()
{
    _close();
}

void RtspTalkClient::start(const QUrl &url, const QString &user, const QString &password)
{
    _close();

    _url = url;
    _user = user.isEmpty() ? QUrl::fromPercentEncoding(url.userName(QUrl::FullyEncoded).toUtf8()) : user;
    _password = user.isEmpty() ? QUrl::fromPercentEncoding(url.password(QUrl::FullyEncoded).toUtf8()) : password;
    _url.setUserInfo(QString());
    if (_url.port() < 0) {
        _url.setPort(554);
    }
    if ((_url.scheme() != QLatin1String("rtsp")) || _url.host().isEmpty()) {
        qCInfo(NxRtspTalkLog) << "Talk address is not RTSP";
        emit failed(tr("the talk address is not an RTSP address"));
        return;
    }

    _authScheme.clear();
    _session.clear();
    _cseq = 0;
    _nonceCount = 0;
    _in.clear();
    _pcm.clear();
    _firstPacket = true;
    _sinceLastPacket.invalidate();
    _sequence = static_cast<quint16>(QRandomGenerator::global()->generate());
    _timestamp = QRandomGenerator::global()->generate();
    _ssrc = QRandomGenerator::global()->generate();

    _socket = new QTcpSocket(this);
    (void) connect(_socket, &QTcpSocket::connected, this, [this]() { _describe(); });
    (void) connect(_socket, &QTcpSocket::readyRead, this, &RtspTalkClient::_onReadyRead);
    (void) connect(_socket, &QTcpSocket::errorOccurred, this, [this]() {
        if (_socket) {
            _fail(tr("no connection to the camera: %1").arg(_socket->errorString()));
        }
    });
    (void) connect(_socket, &QTcpSocket::disconnected, this, [this]() {
        if (_state != State::Idle) {
            _fail(tr("the camera closed the connection"));
        }
    });
    _state = State::Describing;
    _timeout.start(kSetupTimeoutMs);
    _socket->connectToHost(_url.host(), static_cast<quint16>(_url.port()));
}

void RtspTalkClient::stop()
{
    if (_socket && (_state == State::Playing) && !_session.isEmpty()) {
        // No answer is awaited: the camera frees its speaker when the connection closes anyway
        _handler = nullptr;
        _send("TEARDOWN", QUrl(_base), {}, [](const Response &) {});
    }
    _close();
}

void RtspTalkClient::_close()
{
    _timeout.stop();
    _keepAlive.stop();
    _state = State::Idle;
    if (_socket) {
        QTcpSocket *socket = _socket;
        _socket = nullptr;
        socket->disconnect(this);
        socket->flush();
        socket->disconnectFromHost();
        socket->deleteLater();
    }
    _handler = nullptr;
}

void RtspTalkClient::_fail(const QString &message)
{
    if ((_state == State::Idle) && !_socket) {
        return;
    }
    qCInfo(NxRtspTalkLog) << "Talk failed:" << message;
    _close();
    emit failed(message);
}

void RtspTalkClient::_send(const QByteArray &method, const QUrl &url, const QList<QPair<QString, QString>> &headers,
                           const Handler &handler)
{
    if (!_socket) {
        return;
    }
    _method = method;
    _requestUrl = url;
    _requestHeaders = headers;
    _handler = handler;
    _authTried = false;
    _resend();
}

void RtspTalkClient::_resend()
{
    if (!_socket) {
        return;
    }
    const QString uri = withoutCredentials(_requestUrl);
    QByteArray request = _method + ' ' + uri.toUtf8() + " RTSP/1.0\r\n";
    request += "CSeq: " + QByteArray::number(++_cseq) + "\r\n";
    request += "User-Agent: NX Rover\r\n";
    if ((_method == "DESCRIBE") || (_method == "SETUP") || (_method == "PLAY")) {
        request += "Require: " + QByteArray(kBackchannel) + "\r\n";
    }
    if (!_session.isEmpty() && (_method != "DESCRIBE")) {
        request += "Session: " + _session.toUtf8() + "\r\n";
    }
    for (const auto &header : std::as_const(_requestHeaders)) {
        request += header.first.toUtf8() + ": " + header.second.toUtf8() + "\r\n";
    }
    const QByteArray authorization = _authorization(_method, uri);
    if (!authorization.isEmpty()) {
        request += "Authorization: " + authorization + "\r\n";
    }
    request += "\r\n";
    _socket->write(request);
}

QByteArray RtspTalkClient::_authorization(const QByteArray &method, const QString &uri)
{
    if (_authScheme == QLatin1String("basic")) {
        return "Basic " + QString(_user + QLatin1Char(':') + _password).toUtf8().toBase64();
    }
    if (_authScheme != QLatin1String("digest")) {
        return QByteArray();
    }

    const QByteArray ha1 = md5Hex(QStringLiteral("%1:%2:%3").arg(_user, _realm, _password));
    const QByteArray ha2 = md5Hex(QStringLiteral("%1:%2").arg(QString::fromLatin1(method), uri));
    QString header = QStringLiteral("Digest username=\"%1\", realm=\"%2\", nonce=\"%3\", uri=\"%4\"").arg(_user, _realm, _nonce, uri);
    QByteArray response;
    if (_qopAuth) {
        const QString nc = QStringLiteral("%1").arg(++_nonceCount, 8, 16, QLatin1Char('0'));
        const QString cnonce = QString::number(QRandomGenerator::global()->generate(), 16);
        response = md5Hex(QStringLiteral("%1:%2:%3:%4:auth:%5").arg(QString::fromLatin1(ha1), _nonce, nc, cnonce, QString::fromLatin1(ha2)));
        header += QStringLiteral(", qop=auth, nc=%1, cnonce=\"%2\"").arg(nc, cnonce);
    } else {
        response = md5Hex(QStringLiteral("%1:%2:%3").arg(QString::fromLatin1(ha1), _nonce, QString::fromLatin1(ha2)));
    }
    header += QStringLiteral(", response=\"%1\"").arg(QString::fromLatin1(response));
    if (!_opaque.isEmpty()) {
        header += QStringLiteral(", opaque=\"%1\"").arg(_opaque);
    }
    return header.toUtf8();
}

bool RtspTalkClient::_challenge(const Response &response)
{
    QString basic;
    QString digest;
    const QList<QString> challenges = response.headers.values(QStringLiteral("www-authenticate"));
    for (const QString &challenge : challenges) {
        if (challenge.startsWith(QLatin1String("Digest"), Qt::CaseInsensitive)) {
            digest = challenge.mid(6);
        } else if (challenge.startsWith(QLatin1String("Basic"), Qt::CaseInsensitive)) {
            basic = challenge.mid(5);
        }
    }
    if (!digest.isEmpty()) {
        const QMap<QString, QString> parameters = authParameters(digest);
        _authScheme = QStringLiteral("digest");
        _realm = parameters.value(QStringLiteral("realm"));
        _nonce = parameters.value(QStringLiteral("nonce"));
        _opaque = parameters.value(QStringLiteral("opaque"));
        _qopAuth = parameters.value(QStringLiteral("qop")).split(QLatin1Char(',')).contains(QLatin1String("auth"));
        _nonceCount = 0;
        return true;
    }
    if (!basic.isEmpty()) {
        _authScheme = QStringLiteral("basic");
        return true;
    }
    return false;
}

void RtspTalkClient::_onReadyRead()
{
    if (!_socket) {
        return;
    }
    _in += _socket->readAll();

    while (!_in.isEmpty() && _socket) {
        if (_in.at(0) == '$') {
            // Interleaved data (RTCP from the camera): not needed
            if (_in.size() < 4) {
                return;
            }
            const int length = (static_cast<quint8>(_in.at(2)) << 8) | static_cast<quint8>(_in.at(3));
            if (_in.size() < (4 + length)) {
                return;
            }
            _in.remove(0, 4 + length);
            continue;
        }
        if (_in.startsWith("\r\n")) {
            _in.remove(0, 2);
            continue;
        }

        const qsizetype headerEnd = _in.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            return;
        }
        const QList<QByteArray> lines = _in.left(headerEnd).split('\n');
        Response response;
        const QList<QByteArray> status = lines.first().trimmed().split(' ');
        response.status = (status.count() >= 2) ? status.at(1).toInt() : 0;
        for (qsizetype i = 1; i < lines.count(); i++) {
            const qsizetype colon = lines.at(i).indexOf(':');
            if (colon > 0) {
                response.headers.insert(QString::fromLatin1(lines.at(i).left(colon)).trimmed().toLower(),
                                        QString::fromLatin1(lines.at(i).mid(colon + 1)).trimmed());
            }
        }
        const int bodyLength = response.header(QStringLiteral("content-length")).toInt();
        if (_in.size() < (headerEnd + 4 + bodyLength)) {
            return;
        }
        response.body = _in.mid(headerEnd + 4, bodyLength);
        _in.remove(0, headerEnd + 4 + bodyLength);
        if (!lines.first().startsWith("RTSP/")) {
            // A request from the camera (some send GET_PARAMETER or ANNOUNCE): not needed
            continue;
        }
        _onResponse(response);
    }
}

void RtspTalkClient::_onResponse(const Response &response)
{
    if (!_handler) {
        return;
    }
    if ((response.status == 401) && !_authTried && !_user.isEmpty() && _challenge(response)) {
        _authTried = true;
        _resend();
        return;
    }
    if (response.status == 401) {
        _fail(tr("the camera did not accept the user name or password"));
        return;
    }
    const Handler handler = _handler;
    _handler = nullptr;
    handler(response);
}

void RtspTalkClient::_describe()
{
    _send("DESCRIBE", _url, {{QStringLiteral("Accept"), QStringLiteral("application/sdp")}},
          [this](const Response &response) { _onDescribe(response); });
}

void RtspTalkClient::_onDescribe(const Response &response)
{
    if (response.status != 200) {
        _fail(tr("the camera refused the stream (RTSP %1)").arg(response.status));
        return;
    }

    _base = response.header(QStringLiteral("content-base"));
    if (_base.isEmpty()) {
        _base = response.header(QStringLiteral("content-location"));
    }
    if (_base.isEmpty()) {
        _base = withoutCredentials(_url);
    }

    // The track the camera takes audio on is the send-only audio media
    bool inAudio = false;
    bool sendOnly = false;
    bool found = false;
    int payload = -1;
    QString control;
    QMap<int, QString> encodings;
    const QStringList lines = QString::fromUtf8(response.body).split(QLatin1Char('\n'));
    const auto finishMedia = [&]() {
        if (inAudio && sendOnly && !found && (payload >= 0)) {
            found = true;
            _payloadType = payload;
            _controlUrl = control;
        }
    };
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QLatin1String("m="))) {
            finishMedia();
            inAudio = line.startsWith(QLatin1String("m=audio"));
            sendOnly = false;
            control.clear();
            const QStringList parts = line.split(QLatin1Char(' '));
            payload = (inAudio && (parts.count() >= 4)) ? parts.at(3).toInt() : -1;
        } else if (line.startsWith(QLatin1String("a=sendonly"))) {
            sendOnly = true;
        } else if (line.startsWith(QLatin1String("a=control:"))) {
            control = line.mid(10);
        } else if (line.startsWith(QLatin1String("a=rtpmap:"))) {
            const QStringList parts = line.mid(9).split(QLatin1Char(' '));
            if (parts.count() >= 2) {
                encodings.insert(parts.at(0).toInt(), parts.at(1).section(QLatin1Char('/'), 0, 0).toUpper());
            }
        }
    }
    finishMedia();

    if (!found) {
        _fail(tr("the camera has no speaker input (no audio backchannel offered)"));
        return;
    }
    QString encoding = encodings.value(_payloadType);
    if (encoding.isEmpty()) {
        encoding = (_payloadType == 0) ? QStringLiteral("PCMU") : ((_payloadType == 8) ? QStringLiteral("PCMA") : QString());
    }
    if ((encoding != QLatin1String("PCMA")) && (encoding != QLatin1String("PCMU"))) {
        _fail(tr("the camera's speaker wants %1; only G.711 is supported (set the camera's audio output to G.711)")
                  .arg(encoding.isEmpty() ? tr("an unknown audio format") : encoding));
        return;
    }
    _ulaw = (encoding == QLatin1String("PCMU"));

    // _controlUrl holds the backchannel track's control attribute here
    QString setupUrl;
    if (_controlUrl.isEmpty() || (_controlUrl == QLatin1String("*"))) {
        setupUrl = _base;
    } else if (_controlUrl.startsWith(QLatin1String("rtsp://"), Qt::CaseInsensitive)) {
        setupUrl = _controlUrl;
    } else {
        setupUrl = _base + (_base.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + _controlUrl;
    }
    _controlUrl = setupUrl;

    _state = State::SettingUp;
    _send("SETUP", QUrl(setupUrl),
          {{QStringLiteral("Transport"), QStringLiteral("RTP/AVP/TCP;unicast;interleaved=0-1")}},
          [this](const Response &setup) { _onSetup(setup); });
}

void RtspTalkClient::_onSetup(const Response &response)
{
    if (response.status != 200) {
        _fail(tr("the camera refused the audio channel (RTSP %1)").arg(response.status));
        return;
    }
    const QString session = response.header(QStringLiteral("session"));
    _session = session.section(QLatin1Char(';'), 0, 0).trimmed();
    static const QRegularExpression timeout(QStringLiteral("timeout=(\\d+)"));
    const QRegularExpressionMatch timeoutMatch = timeout.match(session);
    if (timeoutMatch.hasMatch()) {
        _keepAliveSecs = qBound(5, timeoutMatch.captured(1).toInt() / 2, 30);
    }

    const QString transport = response.header(QStringLiteral("transport"));
    static const QRegularExpression interleaved(QStringLiteral("interleaved=(\\d+)"));
    const QRegularExpressionMatch match = interleaved.match(transport);
    if (!transport.contains(QLatin1String("TCP"), Qt::CaseInsensitive) && !match.hasMatch()) {
        _fail(tr("the camera does not take audio over the RTSP connection"));
        return;
    }
    _rtpChannel = match.hasMatch() ? match.captured(1).toInt() : 0;

    _state = State::StartingPlay;
    _send("PLAY", QUrl(_base), {{QStringLiteral("Range"), QStringLiteral("npt=0.000-")}},
          [this](const Response &play) { _onPlay(play); });
}

void RtspTalkClient::_onPlay(const Response &response)
{
    if (response.status != 200) {
        _fail(tr("the camera would not start the audio channel (RTSP %1)").arg(response.status));
        return;
    }
    _timeout.stop();
    _state = State::Playing;
    _keepAlive.start(_keepAliveSecs * 1000);
    qCInfo(NxRtspTalkLog) << "Talking to" << _url.host() << "as" << (_ulaw ? "PCMU" : "PCMA") << "on channel" << _rtpChannel;
    emit ready();
    _sendPackets();
}

void RtspTalkClient::sendAudio(const QByteArray &pcm)
{
    if (_state != State::Playing) {
        return;
    }
    // A new talk spurt: the time stamps skip the silence and the first packet is marked
    constexpr qint64 kPauseMs = 200;
    if (_sinceLastPacket.isValid() && (_sinceLastPacket.elapsed() > kPauseMs)) {
        _timestamp += static_cast<quint32>((_sinceLastPacket.elapsed() * TalkAudio::kSampleRate) / 1000);
        _firstPacket = true;
        // What was left of the last spurt, less than a packet, is stale now
        _pcm.clear();
        _sinceLastPacket.invalidate();
    }
    _pcm += pcm;
    _sendPackets();
}

void RtspTalkClient::_sendPackets()
{
    constexpr int kPacketBytes = TalkAudio::kPacketSamples * 2;
    while (_socket && (_pcm.size() >= kPacketBytes)) {
        if (_socket->bytesToWrite() > kMaxQueuedBytes) {
            _pcm.clear();
            return;
        }
        const int16_t *samples = reinterpret_cast<const int16_t *>(_pcm.constData());
        const int payloadSize = TalkAudio::kPacketSamples;
        QByteArray frame(4 + 12 + payloadSize, 0);
        quint8 *p = reinterpret_cast<quint8 *>(frame.data());
        p[0] = '$';
        p[1] = static_cast<quint8>(_rtpChannel);
        p[2] = static_cast<quint8>((12 + payloadSize) >> 8);
        p[3] = static_cast<quint8>((12 + payloadSize) & 0xFF);
        p[4] = 0x80;
        p[5] = static_cast<quint8>((_firstPacket ? 0x80 : 0) | _payloadType);
        p[6] = static_cast<quint8>(_sequence >> 8);
        p[7] = static_cast<quint8>(_sequence & 0xFF);
        p[8] = static_cast<quint8>(_timestamp >> 24);
        p[9] = static_cast<quint8>(_timestamp >> 16);
        p[10] = static_cast<quint8>(_timestamp >> 8);
        p[11] = static_cast<quint8>(_timestamp);
        p[12] = static_cast<quint8>(_ssrc >> 24);
        p[13] = static_cast<quint8>(_ssrc >> 16);
        p[14] = static_cast<quint8>(_ssrc >> 8);
        p[15] = static_cast<quint8>(_ssrc);
        for (int i = 0; i < payloadSize; i++) {
            p[16 + i] = _ulaw ? TalkAudio::encodeUlaw(samples[i]) : TalkAudio::encodeAlaw(samples[i]);
        }
        _socket->write(frame);
        _pcm.remove(0, kPacketBytes);
        _sequence++;
        _timestamp += TalkAudio::kPacketSamples;
        _firstPacket = false;
        _sinceLastPacket.start();
    }
}
