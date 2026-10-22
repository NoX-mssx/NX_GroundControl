#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtQmlIntegration/QtQmlIntegration>

class QJSEngine;
class QNetworkReply;
class QQmlEngine;

/// Plays the radio of the current room: the live MP3 stream the room server makes of the admin's playlist.
///
/// The stream is read with the room manager's pinned HTTPS connection and fed to a small GStreamer pipeline
/// (decoder, volume, default audio device), the same way camera audio is played. Pause stops the download;
/// play joins the broadcast where it is now. The radio plays while the room has it on air, the radio is
/// switched on in the camera menu and the user pressed play; a lost connection is retried by itself.
class RoomRadio : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    /// false when this build cannot play audio (no GStreamer)
    Q_PROPERTY(bool     supported   READ supported                      CONSTANT)
    /// The current room has its radio on air
    Q_PROPERTY(bool     available   READ available                      NOTIFY availableChanged)
    /// Shown on the main screen (camera menu switch); remembered
    Q_PROPERTY(bool     enabled     READ enabled    WRITE setEnabled    NOTIFY enabledChanged)
    /// The user pressed play; remembered
    Q_PROPERTY(bool     playing     READ playing    WRITE setPlaying    NOTIFY playingChanged)
    /// 0..1; remembered
    Q_PROPERTY(double   volume      READ volume     WRITE setVolume     NOTIFY volumeChanged)
    /// "stopped", "connecting" or "playing"
    Q_PROPERTY(QString  status      READ status                         NOTIFY statusChanged)
    Q_PROPERTY(QString  nowPlaying  READ nowPlaying                     NOTIFY availableChanged)

public:
    explicit RoomRadio(QObject *parent = nullptr);
    ~RoomRadio() override;

    static RoomRadio *instance();
    static RoomRadio *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    static bool supported();
    bool available() const { return _available; }
    bool enabled() const { return _enabled; }
    void setEnabled(bool enabled);
    bool playing() const { return _playing; }
    void setPlaying(bool playing);
    double volume() const { return _volume; }
    void setVolume(double volume);
    QString status() const { return _status; }
    QString nowPlaying() const { return _nowPlaying; }

    Q_INVOKABLE void toggle() { setPlaying(!_playing); }

signals:
    void availableChanged();
    void enabledChanged();
    void playingChanged();
    void volumeChanged();
    void statusChanged();

private:
    void _radioChanged();
    void _update();
    void _start();
    void _stop();
    void _restartLater(int delayMs);
    void _setStatus(const QString &status);
    void _readStream();
    void _streamFinished();
    void _checkPipeline();
    bool _createPipeline();
    void _destroyPipeline();
    void _applyVolume();

    bool _available = false;
    bool _enabled = true;
    bool _playing = false;
    double _volume = 0.6;
    QString _status = QStringLiteral("stopped");
    QString _nowPlaying;

    QPointer<QNetworkReply> _reply;
    void *_pipeline = nullptr;      ///< GstElement *
    void *_source = nullptr;        ///< appsrc
    void *_volumeElement = nullptr;
    QTimer _watchdog;               ///< bus messages and stalls
    QTimer _retry;
    QTimer _titleRefresh;           ///< asks the room server what plays now
    QElapsedTimer _lastData;

    static constexpr int kStallMs = 12000;
};
