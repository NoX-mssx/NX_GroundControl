#include "MotorTelemetryMonitor.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"

#include <algorithm>

MotorTelemetryMonitor::MotorTelemetryMonitor(QObject *parent)
    : QObject(parent)
{
    _clock.start();
    _staleTimer.setInterval(1000);
    (void) connect(&_staleTimer, &QTimer::timeout, this, &MotorTelemetryMonitor::_evaluate);

    (void) connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged,
                   this, &MotorTelemetryMonitor::_activeVehicleChanged);
    _activeVehicleChanged(MultiVehicleManager::instance()->activeVehicle());
}

bool MotorTelemetryMonitor::active() const
{
    return _vehicle && _config.value(QStringLiteral("enabled")).toBool();
}

void MotorTelemetryMonitor::setConfig(const QVariantMap &config)
{
    if (config == _config) {
        return;
    }
    _config = config;
    _sides[0] = Side();
    _sides[1] = Side();
    emit configChanged();
    _evaluate();
}

void MotorTelemetryMonitor::_activeVehicleChanged(Vehicle *vehicle)
{
    if (_vehicle) {
        (void) disconnect(_vehicle, nullptr, this, nullptr);
    }
    _vehicle = vehicle;
    _vehicleAt = _clock.elapsed();
    _escs.clear();
    _sides[0] = Side();
    _sides[1] = Side();

    if (_vehicle) {
        (void) connect(_vehicle, &Vehicle::mavlinkMessageReceived, this, &MotorTelemetryMonitor::_messageReceived);
        _staleTimer.start();
    } else {
        _staleTimer.stop();
    }
    _evaluate();
}

bool MotorTelemetryMonitor::_fresh(qint64 at) const
{
    return (at >= 0) && ((_clock.elapsed() - at) <= kStaleMs);
}

int MotorTelemetryMonitor::_sideOf(int index) const
{
    if (index == (_config.value(QStringLiteral("leftEsc"), 1).toInt() - 1)) {
        return 0;
    }
    if (index == (_config.value(QStringLiteral("rightEsc"), 2).toInt() - 1)) {
        return 1;
    }
    return -1;
}

void MotorTelemetryMonitor::_setTemperature(int index, double celsius)
{
    Esc &esc = _escs[index];
    esc.temperature = celsius;
    esc.temperatureAt = _clock.elapsed();
}

void MotorTelemetryMonitor::_setErrorCount(int index, quint32 count)
{
    Esc &esc = _escs[index];
    // The first reading is where counting starts; a lower one (the controller restarted) starts it again
    if (esc.hasErrorBase && (count > esc.errorCount) && active()) {
        const int side = _sideOf(index);
        if (side >= 0) {
            emit errorCountIncreased(side == 0 ? QStringLiteral("left") : QStringLiteral("right"),
                                     static_cast<int>(count - esc.errorCount), static_cast<int>(count));
        }
    }
    esc.errorCount = count;
    esc.hasErrorBase = true;
    esc.errorCountAt = _clock.elapsed();
}

void MotorTelemetryMonitor::_messageReceived(const mavlink_message_t &message)
{
    const qint64 now = _clock.elapsed();

    switch (message.msgid) {
    case MAVLINK_MSG_ID_ESC_STATUS: {
        mavlink_esc_status_t status{};
        mavlink_msg_esc_status_decode(&message, &status);
        for (int i = 0; i < 4; i++) {
            Esc &esc = _escs[status.index + i];
            esc.current = status.current[i];
            esc.voltage = status.voltage[i];
            esc.rpm = status.rpm[i];
            esc.statusAt = now;
        }
        break;
    }
    case MAVLINK_MSG_ID_ESC_INFO: {
        mavlink_esc_info_t info{};
        mavlink_msg_esc_info_decode(&message, &info);
        for (int i = 0; i < 4; i++) {
            const int index = info.index + i;
            // Entries past the number of controllers hold no data
            if (index >= info.count) {
                break;
            }
            if (info.temperature[i] != kTemperatureNotSupplied) {
                _setTemperature(index, info.temperature[i] / 100.0);      // centi-degrees
            }
            Esc &esc = _escs[index];
            esc.failureFlags = info.failure_flags[i];
            esc.failureAt = now;
            _setErrorCount(index, info.error_count[i]);
        }
        break;
    }
    case MAVLINK_MSG_ID_ESC_TELEMETRY_1_TO_4:
    case MAVLINK_MSG_ID_ESC_TELEMETRY_5_TO_8: {
        // Both have the same layout; the second covers ESC 5-8
        const bool first = (message.msgid == MAVLINK_MSG_ID_ESC_TELEMETRY_1_TO_4);
        uint8_t temperature[4]{};
        uint16_t voltage[4]{};
        uint16_t current[4]{};
        uint16_t rpm[4]{};
        if (first) {
            mavlink_esc_telemetry_1_to_4_t telemetry{};
            mavlink_msg_esc_telemetry_1_to_4_decode(&message, &telemetry);
            std::copy(std::begin(telemetry.temperature), std::end(telemetry.temperature), temperature);
            std::copy(std::begin(telemetry.voltage), std::end(telemetry.voltage), voltage);
            std::copy(std::begin(telemetry.current), std::end(telemetry.current), current);
            std::copy(std::begin(telemetry.rpm), std::end(telemetry.rpm), rpm);
        } else {
            mavlink_esc_telemetry_5_to_8_t telemetry{};
            mavlink_msg_esc_telemetry_5_to_8_decode(&message, &telemetry);
            std::copy(std::begin(telemetry.temperature), std::end(telemetry.temperature), temperature);
            std::copy(std::begin(telemetry.voltage), std::end(telemetry.voltage), voltage);
            std::copy(std::begin(telemetry.current), std::end(telemetry.current), current);
            std::copy(std::begin(telemetry.rpm), std::end(telemetry.rpm), rpm);
        }
        const int firstIndex = first ? 0 : 4;
        for (int i = 0; i < 4; i++) {
            Esc &esc = _escs[firstIndex + i];
            // Voltage and current arrive in hundredths. ArduPilot puts its signed temperature into the unsigned
            // byte without a bound, so below zero it wraps (-1 C arrives as 255): read as signed it covers
            // -128..127 C, every real controller and motor.
            esc.current = current[i] / 100.0;
            esc.voltage = voltage[i] / 100.0;
            esc.rpm = rpm[i];
            esc.statusAt = now;
            _setTemperature(firstIndex + i, static_cast<int8_t>(temperature[i]));
        }
        break;
    }
    default:
        return;
    }

    _evaluate();
}

int MotorTelemetryMonitor::_levelWithHysteresis(double value, double warn, double crit, double hysteresis, int previous)
{
    // A level is entered at its threshold and only left once the value is below it by the hysteresis,
    // so a value hovering at the threshold does not make the warning flash.
    auto above = [value, hysteresis](double threshold, bool wasAbove) {
        if (threshold <= 0) {
            return false;
        }
        return wasAbove ? (value > (threshold - hysteresis)) : (value >= threshold);
    };
    if (above(crit, previous >= 2)) {
        return 2;
    }
    if (above(warn, previous >= 1)) {
        return 1;
    }
    return 0;
}

void MotorTelemetryMonitor::_evaluate()
{
    QVariantList motors;
    QVariantList alerts;
    int level = 0;

    if (active()) {
        const double tempWarn = _config.value(QStringLiteral("tempWarn")).toDouble();
        const double tempCrit = _config.value(QStringLiteral("tempCrit")).toDouble();
        const double currentWarn = _config.value(QStringLiteral("currentWarn")).toDouble();
        const double currentCrit = _config.value(QStringLiteral("currentCrit")).toDouble();
        const double currentHysteresis = std::max(0.5, std::max(currentWarn, currentCrit) * kCurrentHysteresis);

        const QString sideNames[2] = {QStringLiteral("left"), QStringLiteral("right")};
        const int escNumbers[2] = {
            _config.value(QStringLiteral("leftEsc"), 1).toInt(),
            _config.value(QStringLiteral("rightEsc"), 2).toInt(),
        };

        bool anyData = false;
        for (int s = 0; s < 2; s++) {
            const Esc esc = _escs.value(escNumbers[s] - 1);
            Side &side = _sides[s];
            const bool hasStatus = _fresh(esc.statusAt);
            const bool hasTemperature = _fresh(esc.temperatureAt) && showTemperature();
            const bool hasFailure = _fresh(esc.failureAt);
            const bool hasErrorCount = _fresh(esc.errorCountAt);
            const bool hasData = hasStatus || hasTemperature || hasFailure;
            anyData = anyData || hasStatus || _fresh(esc.temperatureAt) || hasFailure;

            QVariantMap motor{
                {QStringLiteral("side"), sideNames[s]},
                {QStringLiteral("esc"), escNumbers[s]},
                {QStringLiteral("hasData"), hasData},
            };

            // A reading that stopped coming is left out, so it shows as missing instead of its last value
            if (hasStatus) {
                motor.insert(QStringLiteral("current"), esc.current);
                motor.insert(QStringLiteral("voltage"), esc.voltage);
                motor.insert(QStringLiteral("rpm"), esc.rpm);
                side.currentLevel = _levelWithHysteresis(esc.current, currentWarn, currentCrit, currentHysteresis,
                                                         side.currentLevel);
            } else {
                side.currentLevel = 0;
            }
            if (hasTemperature) {
                motor.insert(QStringLiteral("temperature"), esc.temperature);
                side.temperatureLevel = _levelWithHysteresis(esc.temperature, tempWarn, tempCrit, kTemperatureHysteresis,
                                                             side.temperatureLevel);
            } else {
                side.temperatureLevel = 0;
            }
            if (hasFailure) {
                motor.insert(QStringLiteral("failureFlags"), esc.failureFlags);
            }
            if (hasErrorCount) {
                motor.insert(QStringLiteral("errorCount"), static_cast<qint64>(esc.errorCount));
            }
            motor.insert(QStringLiteral("temperatureLevel"), side.temperatureLevel);
            motor.insert(QStringLiteral("currentLevel"), side.currentLevel);

            if (side.temperatureLevel > 0) {
                alerts.append(QVariantMap{
                    {QStringLiteral("side"), sideNames[s]},
                    {QStringLiteral("kind"), QStringLiteral("temperature")},
                    {QStringLiteral("value"), esc.temperature},
                    {QStringLiteral("threshold"), (side.temperatureLevel >= 2) ? tempCrit : tempWarn},
                    {QStringLiteral("level"), side.temperatureLevel},
                });
            }
            if (side.currentLevel > 0) {
                alerts.append(QVariantMap{
                    {QStringLiteral("side"), sideNames[s]},
                    {QStringLiteral("kind"), QStringLiteral("current")},
                    {QStringLiteral("value"), esc.current},
                    {QStringLiteral("threshold"), (side.currentLevel >= 2) ? currentCrit : currentWarn},
                    {QStringLiteral("level"), side.currentLevel},
                });
            }
            // The controller itself reports a failure: critical while the flags stay set
            if (hasFailure && (esc.failureFlags != 0)) {
                alerts.append(QVariantMap{
                    {QStringLiteral("side"), sideNames[s]},
                    {QStringLiteral("kind"), QStringLiteral("failure")},
                    {QStringLiteral("value"), esc.failureFlags},
                    {QStringLiteral("threshold"), 0},
                    {QStringLiteral("level"), 2},
                });
                level = 2;
            }
            level = std::max({level, side.temperatureLevel, side.currentLevel});
            motors.append(motor);
        }

        // Nothing at all for a while: the CAN bus, the stream rate or the controllers themselves
        if (!anyData && ((_clock.elapsed() - _vehicleAt) > kStaleMs)) {
            alerts.append(QVariantMap{
                {QStringLiteral("side"), QString()},
                {QStringLiteral("kind"), QStringLiteral("noData")},
                {QStringLiteral("value"), 0},
                {QStringLiteral("threshold"), 0},
                {QStringLiteral("level"), 1},
            });
            level = std::max(level, 1);
        }
    }

    if ((motors == _motors) && (alerts == _alerts) && (level == _level)) {
        return;
    }
    _motors = motors;
    _alerts = alerts;
    _level = level;
    emit stateChanged();
}
