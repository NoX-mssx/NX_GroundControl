#include "GstWhepSource.h"

#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QHash>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMetaEnum>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <atomic>
#include <functional>
#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#include <gst/webrtc/webrtc.h>
#include <memory>

#include "QGCLoggingCategory.h"
#include "WebRtcVideoStatus.h"

QGC_LOGGING_CATEGORY(GstWhepSourceLog, "Video.GStreamer.GstWhepSource")

namespace GStreamer::Whep {

namespace {

/// ICE gathering not finished by then: the offer goes with the candidates gathered so far
constexpr int kOfferFallbackMs = 2000;
/// The server answers an on-demand stream (HD) only once the camera stream runs, which takes 5–10 s
constexpr int kPostTimeoutMs = 20000;
constexpr int kDeleteTimeoutMs = 3000;
/// ICE "disconnected" that lasts this long ends the session; shorter ones are dropouts it rides out
constexpr int kDisconnectedGraceMs = 2000;
constexpr int kStatsIntervalMs = 10000;

constexpr const char* kContextKey = "qgc-whep-context";

/// What the receiver may get from MediaMTX. Several H.264 profiles are offered because the server matches the
/// profile of the offer against its own (42e01f, as browsers offer); level-asymmetry-allowed takes any level.
constexpr const char* kVideoCaps =
    "application/x-rtp,media=video,encoding-name=H264,clock-rate=90000,payload=96,packetization-mode=(string)1,"
    "profile-level-id=(string)42e01f,level-asymmetry-allowed=(string)1,rtcp-fb-nack-pli=(boolean)true;"
    "application/x-rtp,media=video,encoding-name=H264,clock-rate=90000,payload=97,packetization-mode=(string)1,"
    "profile-level-id=(string)42001f,level-asymmetry-allowed=(string)1,rtcp-fb-nack-pli=(boolean)true;"
    "application/x-rtp,media=video,encoding-name=H264,clock-rate=90000,payload=98,packetization-mode=(string)1,"
    "profile-level-id=(string)4d001f,level-asymmetry-allowed=(string)1,rtcp-fb-nack-pli=(boolean)true;"
    "application/x-rtp,media=video,encoding-name=H264,clock-rate=90000,payload=99,packetization-mode=(string)1,"
    "profile-level-id=(string)640032,level-asymmetry-allowed=(string)1,rtcp-fb-nack-pli=(boolean)true";
/// G.711 from a Raspberry camera, Opus later; AAC cannot travel over WebRTC.
constexpr const char* kAudioCaps =
    "application/x-rtp,media=audio,encoding-name=OPUS,clock-rate=48000,payload=111,encoding-params=(string)2,"
    "minptime=(string)10,useinbandfec=(string)1;"
    "application/x-rtp,media=audio,encoding-name=PCMU,clock-rate=8000,payload=0;"
    "application/x-rtp,media=audio,encoding-name=PCMA,clock-rate=8000,payload=8";

/// Messages for the "!" of the drive screen
struct WhepText
{
    Q_DECLARE_TR_FUNCTIONS(WhepSource)
};

/// Shared by the element callbacks (GStreamer threads) and the session (GUI thread)
struct Shared
{
    explicit Shared(quint64 sessionId) : id(sessionId) {}

    const quint64 id;
    std::atomic_bool offerRequested{false};
    std::atomic_bool offerSent{false};
    /// RTP packets with the marker bit: one per video frame
    std::atomic<quint64> videoFrames{0};
    std::atomic<quint64> audioPackets{0};
};

/// qdata of the webrtcbin: the session closes once the element is gone
struct ElementContext
{
    std::shared_ptr<Shared> shared;
};

/// Video numbers of one get-stats answer; the counters run from the start of the session
struct StatsSample
{
    bool valid = false;
    quint64 bytesReceived = 0;
    quint64 packetsReceived = 0;
    quint64 lost = 0;   ///< given up by the jitter buffer
    quint64 late = 0;   ///< came after their playout time, dropped
    quint64 nacks = 0;  ///< retransmissions asked for
    quint64 pli = 0;    ///< key frames asked for
    double jitterMs = -1;
    QString path;       ///< protocol and server address the media comes from
};

void postToGui(std::function<void()> fn)
{
    if (QCoreApplication* const app = QCoreApplication::instance()) {
        QMetaObject::invokeMethod(app, std::move(fn), Qt::QueuedConnection);
    }
}

/// The receiver stops on a pipeline error and starts the stream again
void postElementError(GstElement* element, const QString& message)
{
    GError* const error =
        g_error_new_literal(GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_OPEN_READ, message.toUtf8().constData());
    (void) gst_element_post_message(element, gst_message_new_error(GST_OBJECT(element), error, "WHEP"));
    g_error_free(error);
}

QString networkReason(QNetworkReply::NetworkError error)
{
    switch (error) {
        case QNetworkReply::ConnectionRefusedError:
            return WhepText::tr("connection refused");
        case QNetworkReply::HostNotFoundError:
            return WhepText::tr("address not found");
        case QNetworkReply::OperationCanceledError:
        case QNetworkReply::TimeoutError:
            return WhepText::tr("no answer");
        case QNetworkReply::UnknownNetworkError:
        case QNetworkReply::NetworkSessionFailedError:
            return WhepText::tr("no connection");
        default:
            return QString::fromLatin1(QMetaEnum::fromType<QNetworkReply::NetworkError>().valueToKey(error));
    }
}

/// MediaMTX answers errors with {"status":"error","error":"..."}
QString serverMessage(const QByteArray& body)
{
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    QString message = object.value(QStringLiteral("error")).toString();
    if (message.isEmpty()) {
        message = QString::fromUtf8(body).section(QLatin1Char('\n'), 0, 0).trimmed();
    }
    return message.left(120);
}

class Session;

QHash<quint64, Session*>& sessions()
{
    static QHash<quint64, Session*> s_sessions;
    return s_sessions;
}

/// Runs @p fn on the GUI thread with the session, if it is still open
void withSession(quint64 id, std::function<void(Session&)> fn);

/// One WHEP session: the HTTP side of one webrtcbin. Lives on the GUI thread; reaches the element through a
/// weak reference, so it never keeps it alive. Closing sends DELETE for the server's session.
class Session : public QObject
{
    Q_DECLARE_TR_FUNCTIONS(WhepSource)

public:
    Session(std::shared_ptr<Shared> shared, GWeakRef* element, const QUrl& endpoint, const QByteArray& authorization,
            const QString& stream)
        : _shared(std::move(shared)),
          _element(element),
          _endpoint(endpoint),
          _authorization(authorization),
          _stream(stream)
    {
        _nam = new QNetworkAccessManager(this);
        // The server is reached through the tunnel; a system proxy has no route there
        _nam->setProxy(QNetworkProxy::NoProxy);

        _offerFallback.setSingleShot(true);
        _offerFallback.setInterval(kOfferFallbackMs);
        (void) connect(&_offerFallback, &QTimer::timeout, this, [this]() { _sendOfferFromElement(true); });

        _disconnected.setSingleShot(true);
        _disconnected.setInterval(kDisconnectedGraceMs);
        (void) connect(&_disconnected, &QTimer::timeout, this, [this]() { _fail(tr("connection lost")); });

        _stats.setInterval(kStatsIntervalMs);
        (void) connect(&_stats, &QTimer::timeout, this, &Session::_requestStats);

        _age.start();
    }

    ~Session() override
    {
        g_weak_ref_clear(_element);
        delete _element;
    }

    /// The offer is set on the element; ICE gathering runs
    void localDescriptionSet()
    {
        if (!_shared->offerSent.load()) {
            _offerFallback.start();
        }
    }

    void sendOffer(const QByteArray& sdp, bool complete)
    {
        _offerFallback.stop();
        if (_closing || _postReply || _answered) {
            return;
        }

        QNetworkRequest request(_endpoint);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/sdp"));
        request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/sdp"));
        if (!_authorization.isEmpty()) {
            request.setRawHeader(QByteArrayLiteral("Authorization"), _authorization);
        }
        request.setTransferTimeout(kPostTimeoutMs);
        _postSentMs = _age.elapsed();
        _postReply = _nam->post(request, sdp);
        (void) connect(_postReply, &QNetworkReply::finished, this, &Session::_postFinished);
        qCInfo(GstWhepSourceLog).noquote()
            << "WHEP" << _stream << "offer sent to" << _endpoint.toDisplayString() << "after" << _postSentMs << "ms"
            << (complete ? QString() : QStringLiteral("(ICE gathering unfinished)"));
        qCDebug(GstWhepSourceLog).noquote() << "WHEP offer:" << sdp;
    }

    void remoteDescriptionFailed(const QString& error)
    {
        _fail(tr("the server's answer was not accepted: %1").arg(error));
    }

    void iceConnectionState(int state)
    {
        switch (state) {
            case GST_WEBRTC_ICE_CONNECTION_STATE_CONNECTED:
            case GST_WEBRTC_ICE_CONNECTION_STATE_COMPLETED:
                _disconnected.stop();
                if (!_connected) {
                    _connected = true;
                    qCInfo(GstWhepSourceLog).noquote()
                        << "WHEP" << _stream << "connected after" << _age.elapsed() << "ms";
                }
                break;
            case GST_WEBRTC_ICE_CONNECTION_STATE_DISCONNECTED:
                if (_connected && !_disconnected.isActive()) {
                    qCInfo(GstWhepSourceLog).noquote() << "WHEP" << _stream << "ICE disconnected";
                    _disconnected.start();
                }
                break;
            case GST_WEBRTC_ICE_CONNECTION_STATE_FAILED:
                // Before the first connection: the answer named only addresses or a UDP port this computer cannot reach
                _fail(_connected ? tr("connection lost")
                                 : tr("no media connection (ICE failed): the server's UDP port is not reachable"));
                break;
            default:
                break;
        }
    }

    void connectionFailed() { _fail(_connected ? tr("connection lost") : tr("no media connection (DTLS)")); }

    void firstFrame()
    {
        qCInfo(GstWhepSourceLog).noquote() << "WHEP" << _stream << "first video frame after" << _age.elapsed() << "ms";
        WebRtcVideoStatus::instance()->reportWorking(_stream);
    }

    void statsReceived(const StatsSample& sample)
    {
        const quint64 frames = _shared->videoFrames.load();
        const double seconds = static_cast<double>(_age.elapsed() - _lastStatsMs) / 1000.0;
        _lastStatsMs = _age.elapsed();
        if (!sample.valid || (seconds <= 0)) {
            return;
        }
        const auto delta = [](quint64 now, quint64 before) { return now - qMin(now, before); };
        const auto real = [](quint64 value) { return static_cast<double>(value); };
        const quint64 packets = delta(sample.packetsReceived, _last.packetsReceived);
        const quint64 lost = delta(sample.lost, _last.lost);
        const double lossPercent = (packets + lost) > 0 ? (100.0 * real(lost) / real(packets + lost)) : 0.0;
        const double kbps = real(delta(sample.bytesReceived, _last.bytesReceived)) * 8.0 / 1000.0 / seconds;
        const double fps = real(delta(frames, _lastFrames)) / seconds;
        const auto ms = [](double value) {
            return (value >= 0) ? QStringLiteral("%1 ms").arg(qRound(value)) : QStringLiteral("n/a");
        };
        qCInfo(GstWhepSourceLog).noquote()
            << QStringLiteral(
                   "WHEP %1: %2 fps, %3 kbit/s, lost %4 (%5%), late %6, nack %7, pli %8, rtt %9, jitter %10, %11")
                   .arg(_stream)
                   .arg(fps, 0, 'f', 1)
                   .arg(qRound(kbps))
                   .arg(lost)
                   .arg(lossPercent, 0, 'f', 1)
                   .arg(delta(sample.late, _last.late))
                   .arg(delta(sample.nacks, _last.nacks))
                   .arg(delta(sample.pli, _last.pli))
                   .arg(ms(_rttMs), ms(sample.jitterMs), sample.path);
        _last = sample;
        _lastFrames = frames;
    }

    /// The element is gone: DELETE the server's session (after the offer's answer, if it is still on its way)
    void close()
    {
        _closing = true;
        _offerFallback.stop();
        _disconnected.stop();
        _stats.stop();
        if (_postReply) {
            return;  // _postFinished goes on from here
        }
        _deleteResource();
    }

private:
    GstElement* _elementRef() const { return static_cast<GstElement*>(g_weak_ref_get(_element)); }

    void _sendOfferFromElement(bool fallback)
    {
        if (_shared->offerSent.exchange(true)) {
            return;
        }
        GstElement* const webrtc = _elementRef();
        if (!webrtc) {
            return;
        }
        GstWebRTCSessionDescription* description = nullptr;
        g_object_get(webrtc, "local-description", &description, nullptr);
        gst_object_unref(webrtc);
        if (!description) {
            _fail(tr("no offer"));
            return;
        }
        gchar* const text = gst_sdp_message_as_text(description->sdp);
        gst_webrtc_session_description_free(description);
        sendOffer(QByteArray(text), !fallback);
        g_free(text);
    }

    void _postFinished()
    {
        QNetworkReply* const reply = _postReply;
        _postReply = nullptr;
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QByteArray location = reply->rawHeader(QByteArrayLiteral("Location"));
        if (!location.isEmpty()) {
            _resource = _endpoint.resolved(QUrl::fromEncoded(location));
        }

        if (_closing) {
            _deleteResource();
            return;
        }
        if (status == 0) {
            _fail(tr("WHEP server: %1").arg(networkReason(reply->error())));
            return;
        }
        if ((status != 201) && (status != 200)) {
            QString reason;
            switch (status) {
                case 404:
                    reason = tr("the server has no such stream (404)");
                    break;
                case 401:
                case 403:
                    reason = tr("access denied (%1)").arg(status);
                    break;
                case 405:
                    reason = tr("not a WHEP address (405)");
                    break;
                default:
                    reason = tr("the server refused (%1)").arg(status);
                    break;
            }
            const QString message = serverMessage(body);
            _fail(message.isEmpty() ? reason : QStringLiteral("%1: %2").arg(reason, message));
            return;
        }

        qCInfo(GstWhepSourceLog).noquote()
            << "WHEP" << _stream << "answer" << status << "after" << (_age.elapsed() - _postSentMs) << "ms, session"
            << (_resource.isValid() ? _resource.path() : QStringLiteral("without address"));
        qCDebug(GstWhepSourceLog).noquote() << "WHEP answer:" << body;
        _answered = true;

        GstSDPMessage* sdp = nullptr;
        if ((gst_sdp_message_new_from_text(body.constData(), &sdp) != GST_SDP_OK) || !sdp) {
            if (sdp) {
                gst_sdp_message_free(sdp);
            }
            _fail(tr("the server's answer is not SDP"));
            return;
        }
        GstWebRTCSessionDescription* const answer = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, sdp);
        GstElement* const webrtc = _elementRef();
        if (!webrtc) {
            gst_webrtc_session_description_free(answer);
            return;
        }
        GstPromise* const promise =
            gst_promise_new_with_change_func(&Session::_onRemoteDescriptionSet, new quint64(_shared->id),
                                             [](gpointer id) { delete static_cast<quint64*>(id); });
        g_signal_emit_by_name(webrtc, "set-remote-description", answer, promise);
        gst_webrtc_session_description_free(answer);
        gst_object_unref(webrtc);
        _stats.start();
    }

    static void _onRemoteDescriptionSet(GstPromise* promise, gpointer data)
    {
        const quint64 id = *static_cast<quint64*>(data);
        QString error;
        if (gst_promise_wait(promise) != GST_PROMISE_RESULT_REPLIED) {
            error = QStringLiteral("no reply");
        } else if (const GstStructure* const reply = gst_promise_get_reply(promise)) {
            GError* gerror = nullptr;
            if (gst_structure_get(reply, "error", G_TYPE_ERROR, &gerror, nullptr) && gerror) {
                error = QString::fromUtf8(gerror->message);
                g_error_free(gerror);
            }
        }
        gst_promise_unref(promise);
        if (!error.isEmpty()) {
            withSession(id, [error](Session& session) { session.remoteDescriptionFailed(error); });
        }
    }

    void _requestStats()
    {
        _measureRtt();
        GstElement* const webrtc = _elementRef();
        if (!webrtc) {
            return;
        }
        GstPromise* const promise = gst_promise_new_with_change_func(
            &Session::_onStats, new quint64(_shared->id), [](gpointer id) { delete static_cast<quint64*>(id); });
        g_signal_emit_by_name(webrtc, "get-stats", nullptr, promise);
        gst_object_unref(webrtc);
    }

    /// RTCP gives a receiver no round trip, and MediaMTX does not use RTX, so it is timed on the signalling
    /// connection instead: an OPTIONS request (answered at once, without a session) on the same path as the media
    void _measureRtt()
    {
        if (_rttReply) {
            return;
        }
        QNetworkRequest request(_endpoint);
        request.setTransferTimeout(kStatsIntervalMs);
        _rttTimer.start();
        _rttReply = _nam->sendCustomRequest(request, QByteArrayLiteral("OPTIONS"));
        (void) connect(_rttReply, &QNetworkReply::finished, this, [this]() {
            const bool ok = (_rttReply->error() == QNetworkReply::NoError);
            _rttMs = ok ? static_cast<double>(_rttTimer.elapsed()) : -1;
            _rttReply->deleteLater();
            _rttReply = nullptr;
        });
    }

    static void _onStats(GstPromise* promise, gpointer data)
    {
        const quint64 id = *static_cast<quint64*>(data);
        StatsSample sample;
        if (gst_promise_wait(promise) == GST_PROMISE_RESULT_REPLIED) {
            if (const GstStructure* const reply = gst_promise_get_reply(promise)) {
                sample = _parseStats(reply);
            }
        }
        gst_promise_unref(promise);
        withSession(id, [sample](Session& session) { session.statsReceived(sample); });
    }

    static StatsSample _parseStats(const GstStructure* reply)
    {
        StatsSample sample;
        QHash<QString, QString> candidates;  // remote candidate id → "udp 10.0.0.1:8189"
        QString selectedRemote;
        const int fields = gst_structure_n_fields(reply);
        for (int i = 0; i < fields; i++) {
            const GValue* const value = gst_structure_get_value(reply, gst_structure_nth_field_name(reply, i));
            if (!value || !GST_VALUE_HOLDS_STRUCTURE(value)) {
                continue;
            }
            const GstStructure* const stat = gst_value_get_structure(value);
            GstWebRTCStatsType type;
            if (!gst_structure_get(stat, "type", GST_TYPE_WEBRTC_STATS_TYPE, &type, nullptr)) {
                continue;
            }
            if ((type == GST_WEBRTC_STATS_INBOUND_RTP) &&
                (g_strcmp0(gst_structure_get_string(stat, "kind"), "video") == 0)) {
                guint64 u64 = 0;
                guint u32 = 0;
                gdouble dbl = 0;
                sample.valid = true;
                if (gst_structure_get_uint64(stat, "bytes-received", &u64)) {
                    sample.bytesReceived = u64;
                }
                if (gst_structure_get_uint64(stat, "packets-received", &u64)) {
                    sample.packetsReceived = u64;
                }
                if (gst_structure_get_uint(stat, "pli-count", &u32)) {
                    sample.pli = u32;
                }
                if (gst_structure_get_double(stat, "jitter", &dbl)) {
                    sample.jitterMs = dbl * 1000.0;
                }
                GstStructure* jitterBuffer = nullptr;
                if (gst_structure_get(stat, "gst-rtpjitterbuffer-stats", GST_TYPE_STRUCTURE, &jitterBuffer, nullptr) &&
                    jitterBuffer) {
                    guint64 number = 0;
                    if (gst_structure_get_uint64(jitterBuffer, "num-lost", &number)) {
                        sample.lost = number;
                    }
                    if (gst_structure_get_uint64(jitterBuffer, "num-late", &number)) {
                        sample.late = number;
                    }
                    if (gst_structure_get_uint64(jitterBuffer, "rtx-count", &number)) {
                        sample.nacks = number;
                    }
                    gst_structure_free(jitterBuffer);
                }
            } else if (type == GST_WEBRTC_STATS_REMOTE_CANDIDATE) {
                guint port = 0;
                (void) gst_structure_get_uint(stat, "port", &port);
                candidates.insert(QString::fromUtf8(gst_structure_get_string(stat, "id")),
                                  QStringLiteral("%1 %2:%3")
                                      .arg(QString::fromUtf8(gst_structure_get_string(stat, "protocol")),
                                           QString::fromUtf8(gst_structure_get_string(stat, "address")))
                                      .arg(port));
            } else if (type == GST_WEBRTC_STATS_CANDIDATE_PAIR) {
                selectedRemote = QString::fromUtf8(gst_structure_get_string(stat, "remote-candidate-id"));
            }
        }
        sample.path = candidates.value(selectedRemote, QStringLiteral("path unknown"));
        return sample;
    }

    void _fail(const QString& reason)
    {
        if (_failed || _closing) {
            return;
        }
        _failed = true;
        _offerFallback.stop();
        _disconnected.stop();
        qCWarning(GstWhepSourceLog).noquote() << "WHEP" << _stream << "failed:" << reason;
        WebRtcVideoStatus::instance()->reportProblem(_stream, reason);
        if (GstElement* const webrtc = _elementRef()) {
            postElementError(webrtc, QStringLiteral("WHEP %1: %2").arg(_stream, reason));
            gst_object_unref(webrtc);
        }
    }

    void _deleteResource()
    {
        if (!_resource.isValid()) {
            deleteLater();
            return;
        }
        QNetworkRequest request(_resource);
        if (!_authorization.isEmpty()) {
            request.setRawHeader(QByteArrayLiteral("Authorization"), _authorization);
        }
        request.setTransferTimeout(kDeleteTimeoutMs);
        QNetworkReply* const reply = _nam->deleteResource(request);
        const QString path = _resource.path();
        (void) connect(reply, &QNetworkReply::finished, this, [this, reply, path]() {
            qCInfo(GstWhepSourceLog).noquote()
                << "WHEP" << _stream << "session" << path
                << "closed:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                << ((reply->error() == QNetworkReply::NoError) ? QString() : networkReason(reply->error()));
            reply->deleteLater();
            deleteLater();
        });
        _resource.clear();
    }

    std::shared_ptr<Shared> _shared;
    GWeakRef* _element;
    const QUrl _endpoint;
    const QByteArray _authorization;
    const QString _stream;

    QNetworkAccessManager* _nam = nullptr;
    QNetworkReply* _postReply = nullptr;
    QNetworkReply* _rttReply = nullptr;
    QElapsedTimer _rttTimer;
    double _rttMs = -1;
    QUrl _resource;
    QTimer _offerFallback;
    QTimer _disconnected;
    QTimer _stats;
    QElapsedTimer _age;
    qint64 _postSentMs = 0;
    bool _answered = false;
    bool _connected = false;
    bool _failed = false;
    bool _closing = false;

    StatsSample _last;
    quint64 _lastFrames = 0;
    qint64 _lastStatsMs = 0;
};

void withSession(quint64 id, std::function<void(Session&)> fn)
{
    postToGui([id, fn = std::move(fn)]() {
        if (Session* const session = sessions().value(id)) {
            fn(*session);
        }
    });
}

// ---- element side: GStreamer threads ----

void sendOfferFromElement(GstElement* webrtc, Shared& shared)
{
    if (shared.offerSent.exchange(true)) {
        return;
    }
    GstWebRTCSessionDescription* description = nullptr;
    g_object_get(webrtc, "local-description", &description, nullptr);
    if (!description) {
        postElementError(webrtc, QStringLiteral("WHEP: no local description"));
        return;
    }
    gchar* const text = gst_sdp_message_as_text(description->sdp);
    gst_webrtc_session_description_free(description);
    const QByteArray sdp(text);
    g_free(text);
    withSession(shared.id, [sdp](Session& session) { session.sendOffer(sdp, true); });
}

void onOfferCreated(GstPromise* promise, gpointer data)
{
    GstElement* const webrtc = static_cast<GstElement*>(data);
    GstWebRTCSessionDescription* offer = nullptr;
    QString error;
    if (gst_promise_wait(promise) != GST_PROMISE_RESULT_REPLIED) {
        error = QStringLiteral("no reply");
    } else if (const GstStructure* const reply = gst_promise_get_reply(promise)) {
        GError* gerror = nullptr;
        if (gst_structure_get(reply, "error", G_TYPE_ERROR, &gerror, nullptr) && gerror) {
            error = QString::fromUtf8(gerror->message);
            g_error_free(gerror);
        } else {
            (void) gst_structure_get(reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, nullptr);
        }
    }
    gst_promise_unref(promise);

    auto* const ctx = static_cast<ElementContext*>(g_object_get_data(G_OBJECT(webrtc), kContextKey));
    if (!offer || !ctx) {
        postElementError(webrtc, QStringLiteral("WHEP: offer not created %1").arg(error));
        return;
    }
    g_signal_emit_by_name(webrtc, "set-local-description", offer, nullptr);
    gst_webrtc_session_description_free(offer);

    withSession(ctx->shared->id, [](Session& session) { session.localDescriptionSet(); });
    // Gathering may have completed already: nothing else would send the offer then
    GstWebRTCICEGatheringState gathering = GST_WEBRTC_ICE_GATHERING_STATE_NEW;
    g_object_get(webrtc, "ice-gathering-state", &gathering, nullptr);
    if (gathering == GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE) {
        sendOfferFromElement(webrtc, *ctx->shared);
    }
}

void onNegotiationNeeded(GstElement* webrtc, gpointer data)
{
    auto* const ctx = static_cast<ElementContext*>(data);
    if (ctx->shared->offerRequested.exchange(true)) {
        return;
    }
    GstPromise* const promise = gst_promise_new_with_change_func(onOfferCreated, webrtc, nullptr);
    g_signal_emit_by_name(webrtc, "create-offer", nullptr, promise);
}

void onGatheringState(GstElement* webrtc, GParamSpec*, gpointer data)
{
    auto* const ctx = static_cast<ElementContext*>(data);
    GstWebRTCICEGatheringState state = GST_WEBRTC_ICE_GATHERING_STATE_NEW;
    g_object_get(webrtc, "ice-gathering-state", &state, nullptr);
    if (state != GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE) {
        return;
    }
    // The local description holds all candidates now: the offer goes whole, no trickle ICE
    GstWebRTCSessionDescription* description = nullptr;
    g_object_get(webrtc, "local-description", &description, nullptr);
    if (!description) {
        return;  // gathering finished before the offer was set; onOfferCreated sends it
    }
    gst_webrtc_session_description_free(description);
    sendOfferFromElement(webrtc, *ctx->shared);
}

void onIceConnectionState(GstElement* webrtc, GParamSpec*, gpointer data)
{
    auto* const ctx = static_cast<ElementContext*>(data);
    GstWebRTCICEConnectionState state = GST_WEBRTC_ICE_CONNECTION_STATE_NEW;
    g_object_get(webrtc, "ice-connection-state", &state, nullptr);
    withSession(ctx->shared->id, [state](Session& session) { session.iceConnectionState(state); });
}

void onConnectionState(GstElement* webrtc, GParamSpec*, gpointer data)
{
    auto* const ctx = static_cast<ElementContext*>(data);
    GstWebRTCPeerConnectionState state = GST_WEBRTC_PEER_CONNECTION_STATE_NEW;
    g_object_get(webrtc, "connection-state", &state, nullptr);
    if (state == GST_WEBRTC_PEER_CONNECTION_STATE_FAILED) {
        withSession(ctx->shared->id, [](Session& session) { session.connectionFailed(); });
    }
}

bool rtpMarker(GstBuffer* buffer)
{
    guint8 second = 0;
    return (gst_buffer_extract(buffer, 1, &second, 1) == 1) && (second & 0x80);
}

GstPadProbeReturn countVideoFrames(GstPad*, GstPadProbeInfo* info, gpointer data)
{
    Shared& shared = **static_cast<std::shared_ptr<Shared>*>(data);
    quint64 frames = 0;
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
        frames = rtpMarker(GST_PAD_PROBE_INFO_BUFFER(info)) ? 1 : 0;
    } else if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER_LIST) {
        GstBufferList* const list = GST_PAD_PROBE_INFO_BUFFER_LIST(info);
        for (guint i = 0; i < gst_buffer_list_length(list); i++) {
            frames += rtpMarker(gst_buffer_list_get(list, i)) ? 1 : 0;
        }
    }
    if ((frames > 0) && (shared.videoFrames.fetch_add(frames) == 0)) {
        withSession(shared.id, [](Session& session) { session.firstFrame(); });
    }
    return GST_PAD_PROBE_OK;
}

/// Runs after the source factory linked the pad: counts video frames, and gives a pad left unlinked (audio that is
/// not played) a fakesink, since an unlinked pad would stop webrtcbin's stream.
void onPadAdded(GstElement* webrtc, GstPad* pad, gpointer data)
{
    auto* const ctx = static_cast<ElementContext*>(data);
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC) {
        return;
    }

    bool video = false;
    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps) {
        caps = gst_pad_query_caps(pad, nullptr);
    }
    if (caps) {
        if (gst_caps_get_size(caps) > 0) {
            const gchar* const media = gst_structure_get_string(gst_caps_get_structure(caps, 0), "media");
            video = (g_strcmp0(media, "video") == 0);
        }
        gchar* const text = gst_caps_to_string(caps);
        qCInfo(GstWhepSourceLog) << "WHEP stream pad" << GST_PAD_NAME(pad) << (text ? text : "");
        g_free(text);
        gst_caps_unref(caps);
    }

    if (video) {
        (void) gst_pad_add_probe(
            pad, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST),
            countVideoFrames, new std::shared_ptr<Shared>(ctx->shared),
            [](gpointer p) { delete static_cast<std::shared_ptr<Shared>*>(p); });
    }

    if (gst_pad_is_linked(pad)) {
        return;
    }
    GstObject* const parent = gst_object_get_parent(GST_OBJECT(webrtc));
    if (!parent) {
        return;
    }
    GstElement* const sink = gst_element_factory_make("fakesink", nullptr);
    if (sink) {
        g_object_set(sink, "sync", FALSE, "async", FALSE, nullptr);
        if (gst_bin_add(GST_BIN(parent), sink)) {
            GstPad* const sinkPad = gst_element_get_static_pad(sink, "sink");
            if (gst_pad_link(pad, sinkPad) != GST_PAD_LINK_OK) {
                qCWarning(GstWhepSourceLog) << "WHEP: could not park pad" << GST_PAD_NAME(pad);
            }
            gst_object_unref(sinkPad);
            (void) gst_element_sync_state_with_parent(sink);
        } else {
            gst_object_unref(sink);
        }
    }
    gst_object_unref(parent);
}

void destroyContext(gpointer data)
{
    auto* const ctx = static_cast<ElementContext*>(data);
    const quint64 id = ctx->shared->id;
    delete ctx;
    postToGui([id]() {
        if (Session* const session = sessions().take(id)) {
            session->close();
        }
    });
}

bool addTransceiver(GstElement* webrtc, const char* capsText)
{
    GstCaps* const caps = gst_caps_from_string(capsText);
    if (!caps) {
        return false;
    }
    GstWebRTCRTPTransceiver* transceiver = nullptr;
    g_signal_emit_by_name(webrtc, "add-transceiver", GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY, caps, &transceiver);
    gst_caps_unref(caps);
    if (!transceiver) {
        return false;
    }
    // NACK: lost packets are asked again within the jitter buffer's time, as browsers do
    g_object_set(transceiver, "do-nack", TRUE, nullptr);
    gst_object_unref(transceiver);
    return true;
}

/// The jitter buffer asks for a packet again once a later one shows the gap, not when the next one is merely
/// due: on a jittery link (Starlink) the timer would ask for many packets that are only a little late.
void onNewJitterBuffer(GstElement*, GstElement* jitterBuffer, guint, guint, gpointer)
{
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(jitterBuffer), "rtx-next-seqnum")) {
        g_object_set(jitterBuffer, "rtx-next-seqnum", FALSE, nullptr);
    }
}

void setIfExists(GObject* object, const char* property, const GValue* value)
{
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(object), property)) {
        g_object_set_property(object, property, value);
    }
}

}  // namespace

bool isWhepUrl(const QString& uri)
{
    const QString scheme = QUrl(uri.trimmed()).scheme().toLower();
    return (scheme == QLatin1String("http")) || (scheme == QLatin1String("https")) ||
           (scheme == QLatin1String("whep")) || (scheme == QLatin1String("wheps"));
}

QUrl endpointUrl(const QString& uri)
{
    QUrl url(uri.trimmed());
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("whep")) {
        url.setScheme(QStringLiteral("http"));
    } else if (scheme == QLatin1String("wheps")) {
        url.setScheme(QStringLiteral("https"));
    }
    url.setUserInfo(QString());
    // The MediaMTX page of a stream (http://server:8889/path) takes its WHEP endpoint below it
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    if (!path.endsWith(QLatin1String("/whep"), Qt::CaseInsensitive)) {
        path += QStringLiteral("/whep");
    }
    url.setPath(path);
    return url;
}

QString streamName(const QString& uri)
{
    QString path = endpointUrl(uri).path();
    path.chop(5);  // "/whep"
    while (path.startsWith(QLatin1Char('/'))) {
        path.remove(0, 1);
    }
    return path.isEmpty() ? endpointUrl(uri).host() : path;
}

GstElement* createSource(const QString& uri, bool audio, unsigned latencyMs, bool dropLate)
{
    const QUrl url(uri.trimmed());
    const QUrl endpoint = endpointUrl(uri);
    if (endpoint.host().isEmpty()) {
        qCCritical(GstWhepSourceLog) << "Invalid WHEP address" << endpoint.toDisplayString();
        return nullptr;
    }

    GstElement* const webrtc = gst_element_factory_make("webrtcbin", "source");
    if (!webrtc) {
        qCCritical(GstWhepSourceLog) << "webrtcbin is missing (GStreamer plugins webrtc, nice, dtls, srtp)";
        return nullptr;
    }

    g_object_set(webrtc, "bundle-policy", GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE, "latency", latencyMs, nullptr);
    // Media over UDP only: over TCP every lost packet would hold up the ones after it, as with RTSP over TCP
    GObject* ice = nullptr;
    g_object_get(webrtc, "ice-agent", &ice, nullptr);
    if (ice) {
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(ice), "ice-tcp")) {
            g_object_set(ice, "ice-tcp", FALSE, nullptr);
        }
        g_object_unref(ice);
    }

    // The jitter buffers inside webrtcbin: late packets are dropped on request, and the video is not delayed to
    // match the audio (an offset beyond 1 ms between the two is ignored)
    if (GstElement* const rtpbin = gst_bin_get_by_name(GST_BIN(webrtc), "rtpbin")) {
        GValue value = G_VALUE_INIT;
        g_value_init(&value, G_TYPE_BOOLEAN);
        g_value_set_boolean(&value, dropLate ? TRUE : FALSE);
        setIfExists(G_OBJECT(rtpbin), "drop-on-latency", &value);
        g_value_unset(&value);
        g_value_init(&value, G_TYPE_INT64);
        g_value_set_int64(&value, 1000000);
        setIfExists(G_OBJECT(rtpbin), "max-ts-offset", &value);
        g_value_unset(&value);
        (void) g_signal_connect(rtpbin, "new-jitterbuffer", G_CALLBACK(onNewJitterBuffer), nullptr);
        gst_object_unref(rtpbin);
    } else {
        qCDebug(GstWhepSourceLog) << "webrtcbin has no rtpbin named rtpbin; jitter buffer left at its defaults";
    }

    if (!addTransceiver(webrtc, kVideoCaps)) {
        qCCritical(GstWhepSourceLog) << "webrtcbin: adding the video transceiver failed";
        gst_object_unref(webrtc);
        return nullptr;
    }
    if (audio && !addTransceiver(webrtc, kAudioCaps)) {
        qCWarning(GstWhepSourceLog) << "webrtcbin: adding the audio transceiver failed; video only";
    }

    static std::atomic<quint64> s_nextId{1};
    auto shared = std::make_shared<Shared>(s_nextId.fetch_add(1));
    auto* const ctx = new ElementContext{shared};
    g_object_set_data_full(G_OBJECT(webrtc), kContextKey, ctx, destroyContext);

    (void) g_signal_connect(webrtc, "on-negotiation-needed", G_CALLBACK(onNegotiationNeeded), ctx);
    (void) g_signal_connect(webrtc, "notify::ice-gathering-state", G_CALLBACK(onGatheringState), ctx);
    (void) g_signal_connect(webrtc, "notify::ice-connection-state", G_CALLBACK(onIceConnectionState), ctx);
    (void) g_signal_connect(webrtc, "notify::connection-state", G_CALLBACK(onConnectionState), ctx);
    (void) g_signal_connect_after(webrtc, "pad-added", G_CALLBACK(onPadAdded), ctx);

    // MediaMTX takes user and password in Basic authorization; they stay out of the address and the log
    QByteArray authorization;
    if (!url.userName().isEmpty()) {
        authorization = QByteArrayLiteral("Basic ") +
                        (url.userName(QUrl::FullyDecoded) + QLatin1Char(':') + url.password(QUrl::FullyDecoded))
                            .toUtf8()
                            .toBase64();
    }

    auto* const weak = new GWeakRef;
    g_weak_ref_init(weak, webrtc);
    const QString stream = streamName(uri);
    postToGui([shared, weak, endpoint, authorization, stream]() {
        sessions().insert(shared->id, new Session(shared, weak, endpoint, authorization, stream));
    });

    qCInfo(GstWhepSourceLog).noquote() << "WHEP source for" << endpoint.toDisplayString() << "audio" << audio
                                       << "latency" << latencyMs << "ms";
    return webrtc;
}

}  // namespace GStreamer::Whep
