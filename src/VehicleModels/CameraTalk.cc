#include "CameraTalk.h"

#include "QGCLoggingCategory.h"
#include "RtspTalkClient.h"
#include "TalkAudio.h"

#include <QtMultimedia/QAudioDevice>
#include <QtMultimedia/QAudioFormat>
#include <QtMultimedia/QAudioSource>
#include <QtMultimedia/QMediaDevices>

QGC_LOGGING_CATEGORY(NxCameraTalkLog, "VehicleModels.CameraTalk")

namespace {
/// How long the camera stays connected after the talk button is let go
constexpr int kIdleMs = 20000;
}  // namespace

struct CameraTalk::Resampling {
    TalkAudio::Resampler resampler;
};

CameraTalk::CameraTalk(QObject *parent)
    : QObject(parent)
    , _client(new RtspTalkClient(this))
    , _resampling(std::make_unique<Resampling>())
{
    (void) connect(_client, &RtspTalkClient::ready, this, &CameraTalk::_startCapture);
    (void) connect(_client, &RtspTalkClient::failed, this, &CameraTalk::_fail);

    _idle = new QTimer(this);
    _idle->setSingleShot(true);
    _idle->setInterval(kIdleMs);
    (void) connect(_idle, &QTimer::timeout, this, &CameraTalk::stop);
}

CameraTalk::~CameraTalk()
{
    stop();
}

void CameraTalk::press(const QVariantMap &camera)
{
    if (_active) {
        return;
    }

    QString address = camera.value(QStringLiteral("talkUrl")).toString().trimmed();
    if (address.isEmpty()) {
        address = camera.value(QStringLiteral("mainUrl")).toString().trimmed();
    }
    if (address.isEmpty()) {
        emit failed(tr("the camera has no RTSP address to talk through"));
        return;
    }
    if (QMediaDevices::defaultAudioInput().isNull()) {
        emit failed(tr("no microphone found on this computer"));
        return;
    }

    _idle->stop();
    _active = true;
    emit activeChanged();

    const QString user = camera.value(QStringLiteral("user")).toString();
    const QString password = camera.value(QStringLiteral("password")).toString();
    const QString session = address + QLatin1Char('\n') + user + QLatin1Char('\n') + password;
    if (_client->playing() && (session == _session)) {
        _startCapture();
        return;
    }
    _session = session;
    _client->start(QUrl(address), user, password);
}

void CameraTalk::release()
{
    if (!_active) {
        return;
    }
    _active = false;
    _stopCapture();
    emit activeChanged();
    if (_client->playing()) {
        // Kept for the next press, but not for long: a camera holds its speaker channel for one client
        _idle->start();
    } else {
        // Let go before the camera was ready
        _client->stop();
    }
}

void CameraTalk::stop()
{
    const bool wasActive = _active;
    _active = false;
    _idle->stop();
    _stopCapture();
    _client->stop();
    _session.clear();
    if (wasActive) {
        emit activeChanged();
    }
}

void CameraTalk::_stopCapture()
{
    _device = nullptr;
    if (_source) {
        // Deleted later: this may run from the source's own state signal
        QAudioSource *const source = _source;
        _source = nullptr;
        source->disconnect(this);
        source->stop();
        source->deleteLater();
    }
}

void CameraTalk::_fail(const QString &message)
{
    // A camera dropping a connection kept between presses is no news: the next press connects again
    const bool talking = _active;
    stop();
    if (talking) {
        emit failed(message);
    }
}

void CameraTalk::_startCapture()
{
    if (!_active) {
        return;
    }

    // The microphone is opened the way it prefers and converted here, as few accept 8 kHz mono
    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    const QAudioFormat format = device.preferredFormat();
    TalkAudio::SampleFormat sampleFormat = TalkAudio::SampleFormat::Int16;
    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8:
        sampleFormat = TalkAudio::SampleFormat::UInt8;
        break;
    case QAudioFormat::Int16:
        sampleFormat = TalkAudio::SampleFormat::Int16;
        break;
    case QAudioFormat::Int32:
        sampleFormat = TalkAudio::SampleFormat::Int32;
        break;
    case QAudioFormat::Float:
        sampleFormat = TalkAudio::SampleFormat::Float;
        break;
    default:
        _fail(tr("the microphone uses an audio format that is not supported"));
        return;
    }
    _resampling->resampler.configure(format.sampleRate(), format.channelCount(), sampleFormat);

    _source = new QAudioSource(device, format, this);
    (void) connect(_source, &QAudioSource::stateChanged, this, [this](QAudio::State state) {
        if ((state == QAudio::StoppedState) && _source && (_source->error() != QAudio::NoError)) {
            _fail(tr("the microphone stopped working"));
        }
    });
    _device = _source->start();
    if (!_device) {
        _fail(tr("the microphone could not be opened"));
        return;
    }
    qCInfo(NxCameraTalkLog) << "Microphone" << device.description() << format.sampleRate() << "Hz" << format.channelCount() << "ch";
    (void) connect(_device, &QIODevice::readyRead, this, &CameraTalk::_onAudio);
}

void CameraTalk::_onAudio()
{
    if (!_device || !_active) {
        return;
    }
    const QByteArray captured = _device->readAll();
    if (!captured.isEmpty()) {
        _client->sendAudio(_resampling->resampler.process(captured));
    }
}
