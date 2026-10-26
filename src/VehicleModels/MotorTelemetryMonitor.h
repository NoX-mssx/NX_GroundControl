#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtQmlIntegration/QtQmlIntegration>

#include "MAVLinkLib.h"
#include "Vehicle.h"

/// Watches the temperature and current of the left and right motor controllers (ESC telemetry from the
/// autopilot) against the thresholds of the active vehicle model.
///
/// The configuration is the "motorTelemetry" block of the active model, bound from QML:
///     { enabled, leftEsc, rightEsc, tempWarn, tempCrit, currentWarn, currentCrit }
/// ESC numbers start at 1; a threshold of 0 means "not set".
class MotorTelemetryMonitor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantMap  config      READ config     WRITE setConfig NOTIFY configChanged)
    /// true when the model asks for motor telemetry and a vehicle is connected
    Q_PROPERTY(bool         active      READ active     NOTIFY stateChanged)
    /// [left, right], each { side, esc, hasData, temperature, current, voltage, rpm, temperatureLevel, currentLevel }
    Q_PROPERTY(QVariantList motors      READ motors     NOTIFY stateChanged)
    /// Problems, each { side, kind: "temperature" | "current" | "noData", value, threshold, level }
    Q_PROPERTY(QVariantList alerts      READ alerts     NOTIFY stateChanged)
    /// Worst level among the alerts: 0 none, 1 warning, 2 critical
    Q_PROPERTY(int          level       READ level      NOTIFY stateChanged)
    /// The model shows the controller temperature (off for controllers that report none): no readout and no alarms
    Q_PROPERTY(bool         showTemperature READ showTemperature NOTIFY configChanged)

public:
    explicit MotorTelemetryMonitor(QObject *parent = nullptr);

    QVariantMap config() const { return _config; }
    void setConfig(const QVariantMap &config);

    bool active() const;
    QVariantList motors() const { return _motors; }
    QVariantList alerts() const { return _alerts; }
    int level() const { return _level; }
    bool showTemperature() const { return _config.value(QStringLiteral("showTemperature"), true).toBool(); }

signals:
    void configChanged();
    void stateChanged();

private slots:
    void _activeVehicleChanged(Vehicle *vehicle);
    void _messageReceived(const mavlink_message_t &message);
    void _evaluate();

private:
    struct Side {
        int temperatureLevel = 0;
        int currentLevel = 0;
    };

    static int _levelWithHysteresis(double value, double warn, double crit, double hysteresis, int previous);

    QPointer<Vehicle> _vehicle;
    QVariantMap _config;
    QTimer _staleTimer;
    QElapsedTimer _sinceTelemetry;      ///< Since the last ESC telemetry message, or since the vehicle appeared
    Side _sides[2];

    QVariantList _motors;
    QVariantList _alerts;
    int _level = 0;

    /// Telemetry comes every 0.5-1 s; this long without it counts as lost
    static constexpr int kStaleMs = 3000;
    static constexpr double kTemperatureHysteresis = 3.0;
    /// Current hysteresis as a share of the threshold
    static constexpr double kCurrentHysteresis = 0.05;
};
