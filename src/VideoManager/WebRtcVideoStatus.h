#pragma once

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtQmlIntegration/QtQmlIntegration>

class QJSEngine;
class QQmlEngine;

/// Problems of the WebRTC (WHEP) video streams, for the "!" of the drive screen. The receiver reconnects by
/// itself, every 2 s, so a reason is passed on once per outage of a stream, not on every attempt.
class WebRtcVideoStatus : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static WebRtcVideoStatus* instance();
    static WebRtcVideoStatus* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    /// @param stream short name of the stream, the path of its WHEP address ("front-sd")
    void reportProblem(const QString& stream, const QString& reason);
    /// Frames arrive again: the next problem of @p stream is reported anew
    void reportWorking(const QString& stream);

signals:
    void problem(const QString& stream, const QString& reason);

private:
    explicit WebRtcVideoStatus(QObject* parent = nullptr);

    QHash<QString, QSet<QString>> _reported;
};
