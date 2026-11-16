#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtQmlIntegration/QtQmlIntegration>

#include "MAVLinkLib.h"
#include "Vehicle.h"

/// Watches the left and right motor controllers (ESC telemetry from the autopilot) against the thresholds of
/// the active vehicle model.
///
/// The telemetry is read from the messages themselves: ESC_TELEMETRY_1_TO_4 / 5_TO_8 (ArduPilot), and
/// ESC_STATUS (current, voltage, RPM) with ESC_INFO (temperature, failure flags, error count) as other
/// firmwares send them. Each of the two parts of a motor's readings has its own age, so a value whose message
/// stopped shows as missing on its own rather than frozen at its last reading.
///
/// The configuration is the "motorTelemetry" block of the active model, bound from QML:
///     { enabled, leftEsc, rightEsc, showTemperature, tempWarn, tempCrit, currentWarn, currentCrit }
/// ESC numbers start at 1 (ESC 1 is index 0 in the messages); a threshold of 0 means "not set".
class MotorTelemetryMonitor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantMap  config      READ config     WRITE setConfig NOTIFY configChanged)
    /// true when the model asks for motor telemetry and a vehicle is connected
    Q_PROPERTY(bool         active      READ active     NOTIFY stateChanged)
    /// [left, right], each { side, esc, hasData, temperatureLevel, currentLevel } and, only while they are
    /// current: temperature (°C), current (A), voltage (V), rpm, failureFlags, errorCount
    Q_PROPERTY(QVariantList motors      READ motors     NOTIFY stateChanged)
    /// Problems, each { side, kind: "temperature" | "current" | "failure" | "noData", value, threshold, level };
    /// for "failure" the value is the ESC failure flags (MAVLink ESC_FAILURE_FLAGS)
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
    /// A controller reported more errors than before; @p side "left" or "right"
    void errorCountIncreased(const QString &side, int added, int total);

private slots:
    void _activeVehicleChanged(Vehicle *vehicle);
    void _messageReceived(const mavlink_message_t &message);
    void _evaluate();

private:
    /// What is known of one controller, by its index in the messages
    struct Esc {
        double current = 0;
        double voltage = 0;
        int rpm = 0;
        qint64 statusAt = -1;           ///< _clock time of current, voltage and RPM; -1 never
        double temperature = 0;         ///< °C
        qint64 temperatureAt = -1;
        int failureFlags = 0;
        qint64 failureAt = -1;
        quint32 errorCount = 0;
        qint64 errorCountAt = -1;
        bool hasErrorBase = false;      ///< errorCount holds a reading to count new errors from
    };
    struct Side {
        int temperatureLevel = 0;
        int currentLevel = 0;
    };

    static int _levelWithHysteresis(double value, double warn, double crit, double hysteresis, int previous);
    bool _fresh(qint64 at) const;
    void _setTemperature(int index, double celsius);
    void _setErrorCount(int index, quint32 count);
    /// 0 left, 1 right, -1 neither, for the ESC at @p index
    int _sideOf(int index) const;

    QPointer<Vehicle> _vehicle;
    QVariantMap _config;
    QTimer _staleTimer;
    QElapsedTimer _clock;
    qint64 _vehicleAt = 0;              ///< _clock time the vehicle appeared: no "no data" alarm before kStaleMs
    QHash<int, Esc> _escs;
    Side _sides[2];

    QVariantList _motors;
    QVariantList _alerts;
    int _level = 0;

    /// Telemetry comes every 0.1-1 s; a reading this old is not shown any more
    static constexpr qint64 kStaleMs = 4000;
    static constexpr double kTemperatureHysteresis = 3.0;
    /// Current hysteresis as a share of the threshold
    static constexpr double kCurrentHysteresis = 0.05;
    /// ESC_INFO temperature meaning "not supplied by the controller"
    static constexpr int kTemperatureNotSupplied = 32767;
};
