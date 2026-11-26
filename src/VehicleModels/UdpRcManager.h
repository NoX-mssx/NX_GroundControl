#pragma once

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtQmlIntegration/QtQmlIntegration>
#include <map>
#include <memory>

#include "UdpRcStream.h"

class LinkConfiguration;
class QJSEngine;
class QQmlEngine;
class Vehicle;

/// NX: runs a UDP RC stream (UdpRcStream) for every link that is on and whose vehicle model is of type
/// "UDP RC (TX12 Ethernet module)". The stream starts with the link, keeps going whether the vehicle answers or
/// not, and ends with about a second of neutral and disarm; the tunnel of the link waits for that second.
/// Such a vehicle gets no joystick, arm or mode commands over MAVLink.
class UdpRcManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// A stream runs (for the active vehicle's link, or the only one)
    Q_PROPERTY(bool active READ active NOTIFY statusChanged)
    /// The active vehicle is driven by a stream: its arm switch only shows the state, its modes are hidden
    Q_PROPERTY(bool controlsActiveVehicle READ controlsActiveVehicle NOTIFY statusChanged)
    Q_PROPERTY(int rateHz READ rateHz NOTIFY statusChanged)
    Q_PROPERTY(int targetRateHz READ targetRateHz NOTIFY statusChanged)
    Q_PROPERTY(int errors READ errors NOTIFY statusChanged)
    Q_PROPERTY(bool joystick READ joystick NOTIFY statusChanged)
    Q_PROPERTY(bool armSent READ armSent NOTIFY statusChanged)
    Q_PROPERTY(bool throttleCentered READ throttleCentered NOTIFY statusChanged)
    /// "", "switch" (arm switch on since the start: switch it off and on) or "throttle" (centre the throttle)
    Q_PROPERTY(QString armBlock READ armBlock NOTIFY statusChanged)
    /// Commands sent, -100..100 %
    Q_PROPERTY(int throttlePct READ throttlePct NOTIFY statusChanged)
    Q_PROPERTY(int steerPct READ steerPct NOTIFY statusChanged)
    /// Arm has been sent for 2 s and the vehicle does not report armed
    Q_PROPERTY(bool armMismatch READ armMismatch NOTIFY statusChanged)
    /// Link name and "address:port" of the stream shown
    Q_PROPERTY(QString linkName READ linkName NOTIFY statusChanged)
    Q_PROPERTY(QString target READ target NOTIFY statusChanged)
    /// State of the stream shown: 0 fine, 1 warning (orange), 2 critical (red)
    Q_PROPERTY(int level READ level NOTIFY statusChanged)

public:
    static UdpRcManager* instance();
    static UdpRcManager* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    /// Thread safe: a stream drives the vehicle of this link, so MAVLink must not
    static bool controlsLink(const LinkConfiguration* config);

    /// The top bar: arm or disarm the active vehicle's stream from the screen, besides the arm switch
    Q_INVOKABLE void setArmed(bool arm);

    /// The model settings page: why these RC settings ("rcControl" of a model) cannot be used, empty if they can
    Q_INVOKABLE QString rcProblem(const QVariantMap& rcControl) const;
    /// The settings a model without any get (the script's defaults), "rcControl" form
    Q_INVOKABLE QVariantMap rcDefaults() const;

    /// LinkManager: a link came up / is going away. Returns how long the tunnel has to stay up (ms).
    void linkConnected(LinkConfiguration* config);
    int linkClosing(LinkConfiguration* config);
    /// LinkManager at exit: the last second of every stream, waited for
    void stopAllBlocking();

    bool active() const { return _shown.running; }

    bool controlsActiveVehicle() const { return _controlsActiveVehicle; }

    int rateHz() const { return qRound(_shown.rateHz); }

    int targetRateHz() const { return _shownTargetRate; }

    int errors() const { return static_cast<int>(_shown.errors); }

    bool joystick() const { return _shown.joystick; }

    bool armSent() const { return _shown.armed; }

    bool throttleCentered() const { return _shown.throttleCentered; }

    QString armBlock() const;

    int throttlePct() const { return qRound(_shown.throttle * 100.0); }

    int steerPct() const { return qRound(_shown.steer * 100.0); }

    int level() const { return _level; }

    bool armMismatch() const { return _armMismatch; }

    QString linkName() const { return _linkName; }

    QString target() const { return _target; }

signals:
    void statusChanged();
    /// For the "!": level 1 orange, 2 red
    void problem(const QString& text, int level);

private:
    explicit UdpRcManager(QObject* parent = nullptr);
    ~UdpRcManager() override;

    struct Entry
    {
        QPointer<LinkConfiguration> config;
        std::unique_ptr<UdpRcStream> stream;
        UdpRcSettings settings;
        QString name;
        // Problem tracking, so each is reported once per episode
        bool hadJoystick = false;
        qint64 armSentSinceMs = -1;
        bool armMismatchReported = false;
        qint64 throttleOffSinceMs = -1;
        bool throttleReported = false;
        qint64 slowSinceMs = -1;
        bool slowReported = false;
        quint64 seenErrors = 0;
        qint64 lastErrorMs = -1;
        bool errorsReported = false;
        bool guardReported = false;
        quint64 seenScreenArmRefusals = 0;
        bool stoppedReported = false;
    };

    void _reevaluate();
    void _start(LinkConfiguration* config, const UdpRcSettings& settings);
    void _stop(LinkConfiguration* config, bool graceful);
    UdpRcSettings _settingsFor(const LinkConfiguration* config) const;
    /// Null while a host name is being looked up (the lookup re-evaluates when it ends)
    QHostAddress _targetFor(const LinkConfiguration* config, const UdpRcSettings& settings);
    Vehicle* _vehicleFor(const LinkConfiguration* config) const;
    void _poll();
    /// Reports new problems of a stream; returns its level (0 fine, 1 warning, 2 critical)
    int _check(Entry& entry, const UdpRcStream::Status& status, qint64 now);
    void _updateControlledLinks();

    std::map<LinkConfiguration*, Entry> _entries;
    /// Streams in their last second; deleted once their thread ends
    QList<UdpRcStream*> _finishing;
    /// Until when the last second of a link's stream runs (monotonic ms): its tunnel waits for that
    QHash<const LinkConfiguration*, qint64> _finishingUntil;
    QHash<QString, QHostAddress> _resolved;
    QSet<QString> _resolving;
    /// Links whose stream could not start, so the reason is logged once
    QSet<const LinkConfiguration*> _warned;
    bool _shuttingDown = false;
    QTimer _pollTimer;
    UdpRcStream::Status _shown;
    int _shownTargetRate = 0;
    bool _controlsActiveVehicle = false;
    int _level = 0;
    bool _armMismatch = false;
    QString _linkName;
    QString _target;
};
