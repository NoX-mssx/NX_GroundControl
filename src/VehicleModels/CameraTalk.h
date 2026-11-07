#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QVariantMap>
#include <QtQmlIntegration/QtQmlIntegration>

#include <memory>

class QAudioSource;
class QIODevice;
class RtspTalkClient;

/// Sends the operator's microphone to the speaker of a camera (see RtspTalkClient), while a talk button is held.
/// One camera at a time. The connection to the camera is opened on the first press and kept for a while after
/// the release, so the next press starts talking at once.
class CameraTalk : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    /// The talk button is held: the microphone is on, or the camera is still being connected
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    explicit CameraTalk(QObject *parent = nullptr);
    ~CameraTalk() override;

    bool active() const { return _active; }

    /// The talk button went down
    /// @param camera a camera of the vehicle model; its "talkUrl" (or else "mainUrl") is the camera's RTSP address
    Q_INVOKABLE void press(const QVariantMap &camera);
    /// The talk button was let go: the microphone goes off
    Q_INVOKABLE void release();
    /// Closes the connection to the camera too
    Q_INVOKABLE void stop();

signals:
    void activeChanged();
    /// Talking is not possible: the microphone or the camera could not be used, or the camera dropped the connection
    void failed(const QString &message);

private:
    void _startCapture();
    void _stopCapture();
    void _onAudio();
    void _fail(const QString &message);

    RtspTalkClient *_client = nullptr;
    QAudioSource *_source = nullptr;
    QIODevice *_device = nullptr;
    struct Resampling;
    std::unique_ptr<Resampling> _resampling;
    QTimer *_idle = nullptr;
    QString _session;
    bool _active = false;
};
