#pragma once

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtQmlIntegration/QtQmlIntegration>

#include <atomic>

#include "Vehicle.h"

/// Measures the round trip time to the active vehicle's router and, when the link asks for it, scales
/// joystick throttle down while that time is high.
///
/// The address and thresholds come from the link configuration of the active vehicle's primary link.
/// The throttle scale is read by the joystick thread through throttleScale().
class LinkQualityMonitor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    /// true when the active link has a ping address configured
    Q_PROPERTY(bool monitoring          READ monitoring         NOTIFY configurationChanged)
    /// Averaged round trip time in milliseconds, -1 while there is no reply
    Q_PROPERTY(int  pingMs              READ pingMs             NOTIFY pingChanged)
    Q_PROPERTY(bool throttleLimitEnabled READ throttleLimitEnabled NOTIFY configurationChanged)
    /// Throttle currently allowed, 0-100
    Q_PROPERTY(int  throttlePercent     READ throttlePercent    NOTIFY throttlePercentChanged)

public:
    explicit LinkQualityMonitor(QObject *parent = nullptr);

    bool monitoring() const { return !_pingAddress.isEmpty(); }
    int pingMs() const { return _pingMs; }
    bool throttleLimitEnabled() const { return _throttleLimitEnabled; }
    int throttlePercent() const;

    /// Factor in 0..1 to apply to joystick throttle. Safe to call from any thread.
    static float throttleScale() { return _throttleScale.load(std::memory_order_relaxed); }

signals:
    void configurationChanged();
    void pingChanged();
    void throttlePercentChanged();

private slots:
    void _activeVehicleChanged(Vehicle *vehicle);
    void _updateConfiguration();
    void _sendPing();
    void _pingFinished(const QString &address, int roundTripMs);
    void _rampThrottle();

private:
    void _reset();
    float _targetScale() const;

    QPointer<Vehicle> _activeVehicle;
    QTimer _pingTimer;
    QTimer _rampTimer;

    QString _pingAddress;
    bool _throttleLimitEnabled = false;
    int _pingThreshold1Ms = 0;
    int _throttlePercent1 = 100;
    int _pingThreshold2Ms = 0;
    int _throttlePercent2 = 100;

    bool _pingInFlight = false;
    QList<int> _samples;            ///< Most recent round trip times, -1 for a lost ping
    int _pingMs = -1;
    int _limitLevel = 0;            ///< 0: no limit, 1: above threshold 1, 2: above threshold 2

    static std::atomic<float> _throttleScale;

    static constexpr int kPingIntervalMs = 1000;
    static constexpr int kPingTimeoutMs = 900;
    static constexpr int kSampleCount = 5;
    /// Consecutive lost pings after which throttle is cut to zero
    static constexpr int kLostPingCutoff = 3;
    /// A limit is only lifted once the ping is this far below its threshold, so it does not flap
    static constexpr int kHysteresisMs = 50;
    static constexpr int kRampIntervalMs = 100;
    /// Scale change per ramp step: a full 0..1 swing takes two seconds
    static constexpr float kRampStep = 0.05f;
};
