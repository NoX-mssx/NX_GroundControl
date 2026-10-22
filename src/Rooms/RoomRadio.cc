#include "RoomRadio.h"
#include "QGCLoggingCategory.h"
#include "RoomManager.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QSettings>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtQml/QJSEngine>

#ifdef QGC_GST_STREAMING
#include <gst/gst.h>
#endif

QGC_LOGGING_CATEGORY(RoomRadioLog, "Rooms.RoomRadio")

namespace {

constexpr const char *kGroup = "NxRadio";

RoomRadio *s_instance = nullptr;

#ifdef QGC_GST_STREAMING
// MP3 in from the network, out to the default audio device. Like the camera audio, the sink does not sync to
// the pipeline clock: the server sends in real time and the first seconds it sends at once are the cushion.
constexpr const char *kPipeline =
    "appsrc name=radiosrc format=bytes is-live=false stream-type=stream max-bytes=4000000 block=false "
    "caps=\"audio/mpeg,mpegversion=(int)1\" ! decodebin ! audioconvert ! audioresample ! "
    "volume name=radiovolume ! autoaudiosink sync=false";
#endif

} // namespace

RoomRadio::RoomRadio(QObject *parent)
    : QObject(parent)
{
    s_instance = this;

    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    _enabled = settings.value(QStringLiteral("enabled"), true).toBool();
    _playing = settings.value(QStringLiteral("playing"), false).toBool();
    _volume = qBound(0.0, settings.value(QStringLiteral("volume"), 0.6).toDouble(), 1.0);

    _watchdog.setInterval(250);
    (void) connect(&_watchdog, &QTimer::timeout, this, &RoomRadio::_checkPipeline);
    _retry.setSingleShot(true);
    (void) connect(&_retry, &QTimer::timeout, this, &RoomRadio::_update);
    _titleRefresh.setInterval(5000);
    (void) connect(&_titleRefresh, &QTimer::timeout, RoomManager::instance(), &RoomManager::refreshRadio);

    RoomManager *const rooms = RoomManager::instance();
    (void) connect(rooms, &RoomManager::radioChanged, this, &RoomRadio::_radioChanged);
    (void) connect(rooms, &RoomManager::currentRoomChanged, this, &RoomRadio::_radioChanged);
    _radioChanged();
}

RoomRadio::~RoomRadio()
{
    _stop();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

RoomRadio *RoomRadio::instance()
{
    if (!s_instance) {
        (void) new RoomRadio(QCoreApplication::instance());
    }
    return s_instance;
}

RoomRadio *RoomRadio::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine);
    Q_UNUSED(jsEngine);

    RoomRadio *const radio = instance();
    QJSEngine::setObjectOwnership(radio, QJSEngine::CppOwnership);
    return radio;
}

bool RoomRadio::supported()
{
#ifdef QGC_GST_STREAMING
    return true;
#else
    return false;
#endif
}

void RoomRadio::setEnabled(bool enabled)
{
    if (enabled == _enabled) {
        return;
    }
    _enabled = enabled;
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    settings.setValue(QStringLiteral("enabled"), _enabled);
    emit enabledChanged();
    _update();
}

void RoomRadio::setPlaying(bool playing)
{
    if (playing == _playing) {
        return;
    }
    _playing = playing;
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    settings.setValue(QStringLiteral("playing"), _playing);
    emit playingChanged();
    _update();
}

void RoomRadio::setVolume(double volume)
{
    volume = qBound(0.0, volume, 1.0);
    if (qFuzzyCompare(volume + 1.0, _volume + 1.0)) {
        return;
    }
    _volume = volume;
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    settings.setValue(QStringLiteral("volume"), _volume);
    emit volumeChanged();
    _applyVolume();
}

void RoomRadio::_setStatus(const QString &status)
{
    if (status != _status) {
        _status = status;
        emit statusChanged();
    }
}

void RoomRadio::_radioChanged()
{
    RoomManager *const rooms = RoomManager::instance();
    const QVariantMap radio = rooms->radio();
    const bool available = !rooms->currentRole().isEmpty() && radio.value(QStringLiteral("onAir")).toBool();
    const QString title = radio.value(QStringLiteral("now")).toMap().value(QStringLiteral("name")).toString();
    if ((available != _available) || (title != _nowPlaying)) {
        _available = available;
        _nowPlaying = title;
        emit availableChanged();
    }
    _update();
}

void RoomRadio::_update()
{
    // While the widget is shown, what plays is asked more often than the room list refreshes
    if (_available && _enabled) {
        if (!_titleRefresh.isActive()) {
            _titleRefresh.start();
        }
    } else {
        _titleRefresh.stop();
    }

    const bool wanted = supported() && _available && _enabled && _playing;
    if (!wanted) {
        _retry.stop();
        _stop();
        _setStatus(QStringLiteral("stopped"));
        return;
    }
    if (!_reply && !_retry.isActive()) {
        _start();
    }
}

void RoomRadio::_start()
{
    QNetworkReply *const reply = RoomManager::instance()->openRadioStream();
    if (!reply) {
        _restartLater(5000);
        return;
    }
    if (!_createPipeline()) {
        reply->abort();
        reply->deleteLater();
        qCWarning(RoomRadioLog) << "No audio pipeline; the radio cannot play";
        _restartLater(30000);
        return;
    }
    _reply = reply;
    (void) connect(reply, &QNetworkReply::readyRead, this, &RoomRadio::_readStream);
    (void) connect(reply, &QNetworkReply::finished, this, &RoomRadio::_streamFinished);
    _lastData.start();
    _watchdog.start();
    _setStatus(QStringLiteral("connecting"));
}

void RoomRadio::_stop()
{
    _watchdog.stop();
    if (_reply) {
        QNetworkReply *const reply = _reply;
        _reply.clear();
        (void) disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    _destroyPipeline();
}

void RoomRadio::_restartLater(int delayMs)
{
    _retry.start(delayMs);
    _setStatus(QStringLiteral("connecting"));
}

void RoomRadio::_readStream()
{
    if (!_reply || (_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)) {
        return;
    }
    const QByteArray data = _reply->readAll();
    if (data.isEmpty() || !_source) {
        return;
    }
    _lastData.restart();
#ifdef QGC_GST_STREAMING
    GstBuffer *const buffer = gst_buffer_new_allocate(nullptr, static_cast<gsize>(data.size()), nullptr);
    (void) gst_buffer_fill(buffer, 0, data.constData(), static_cast<gsize>(data.size()));
    GstFlowReturn result = GST_FLOW_OK;
    // The action signal keeps the caller's reference
    g_signal_emit_by_name(_source, "push-buffer", buffer, &result);
    gst_buffer_unref(buffer);
#endif
    _setStatus(QStringLiteral("playing"));
}

void RoomRadio::_streamFinished()
{
    if (!_reply) {
        return;
    }
    const int status = _reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qCInfo(RoomRadioLog) << "Radio stream ended" << status << _reply->errorString();
    _stop();
    if ((status == 401) || (status == 403) || (status == 409)) {
        // Off air or no access any more: the room state says what to do next
        RoomManager::instance()->refreshRadio();
        _restartLater(10000);
        return;
    }
    _restartLater((status == 200) ? 1000 : 5000);
}

void RoomRadio::_checkPipeline()
{
    // Nothing on air (empty playlist) sends no data either; only a stall during a track restarts the stream
    const bool trackOnAir = !_nowPlaying.isEmpty();
    if (_reply && trackOnAir && _lastData.isValid() && (_lastData.elapsed() > kStallMs)) {
        qCInfo(RoomRadioLog) << "Radio stream stalled, reconnecting";
        _stop();
        _restartLater(1000);
        return;
    }
#ifdef QGC_GST_STREAMING
    if (!_pipeline) {
        return;
    }
    bool failed = false;
    GstBus *const bus = gst_element_get_bus(static_cast<GstElement *>(_pipeline));
    while (GstMessage *const message = gst_bus_pop_filtered(bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            GError *error = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_error(message, &error, &debug);
            qCWarning(RoomRadioLog) << "Radio audio error:" << (error ? error->message : "unknown") << (debug ? debug : "");
            g_clear_error(&error);
            g_free(debug);
        }
        failed = true;
        gst_message_unref(message);
    }
    gst_object_unref(bus);
    if (failed) {
        _stop();
        _restartLater(5000);
    }
#endif
}

bool RoomRadio::_createPipeline()
{
#ifdef QGC_GST_STREAMING
    if (!gst_is_initialized()) {
        return false;
    }
    GError *error = nullptr;
    GstElement *const pipeline = gst_parse_launch(kPipeline, &error);
    if (!pipeline) {
        qCWarning(RoomRadioLog) << "Radio pipeline:" << (error ? error->message : "unknown error");
        g_clear_error(&error);
        return false;
    }
    if (error) {
        // A missing optional element; the pipeline may still work
        qCInfo(RoomRadioLog) << "Radio pipeline:" << error->message;
        g_clear_error(&error);
    }
    _pipeline = pipeline;
    _source = gst_bin_get_by_name(GST_BIN(pipeline), "radiosrc");
    _volumeElement = gst_bin_get_by_name(GST_BIN(pipeline), "radiovolume");
    if (!_source) {
        _destroyPipeline();
        return false;
    }
    _applyVolume();
    (void) gst_element_set_state(pipeline, GST_STATE_PLAYING);
    return true;
#else
    return false;
#endif
}

void RoomRadio::_destroyPipeline()
{
#ifdef QGC_GST_STREAMING
    if (!_pipeline) {
        return;
    }
    (void) gst_element_set_state(static_cast<GstElement *>(_pipeline), GST_STATE_NULL);
    if (_source) {
        gst_object_unref(_source);
    }
    if (_volumeElement) {
        gst_object_unref(_volumeElement);
    }
    gst_object_unref(_pipeline);
#endif
    _pipeline = nullptr;
    _source = nullptr;
    _volumeElement = nullptr;
}

void RoomRadio::_applyVolume()
{
#ifdef QGC_GST_STREAMING
    if (_volumeElement) {
        // Squared, so the slider feels even to the ear
        g_object_set(_volumeElement, "volume", static_cast<gdouble>(_volume * _volume), nullptr);
    }
#endif
}
