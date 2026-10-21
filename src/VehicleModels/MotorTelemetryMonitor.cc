#include "MotorTelemetryMonitor.h"
#include "EscStatusFactGroupListModel.h"
#include "MultiVehicleManager.h"
#include "QmlObjectListModel.h"
#include "Vehicle.h"

#include <algorithm>

MotorTelemetryMonitor::MotorTelemetryMonitor(QObject *parent)
    : QObject(parent)
{
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
    _sinceTelemetry.start();
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

void MotorTelemetryMonitor::_messageReceived(const mavlink_message_t &message)
{
    // Emitted after the vehicle's fact groups took the message, so the ESC values are already current
    if ((message.msgid != MAVLINK_MSG_ID_ESC_TELEMETRY_1_TO_4) && (message.msgid != MAVLINK_MSG_ID_ESC_TELEMETRY_5_TO_8)) {
        return;
    }
    _sinceTelemetry.start();
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
        const bool stale = _sinceTelemetry.elapsed() > kStaleMs;

        const QString sideNames[2] = {QStringLiteral("left"), QStringLiteral("right")};
        const int escNumbers[2] = {
            _config.value(QStringLiteral("leftEsc"), 1).toInt(),
            _config.value(QStringLiteral("rightEsc"), 2).toInt(),
        };

        QmlObjectListModel *const escs = _vehicle->escs();
        for (int s = 0; s < 2; s++) {
            EscStatusFactGroup *group = nullptr;
            for (int i = 0; escs && (i < escs->count()); i++) {
                auto *const candidate = qobject_cast<EscStatusFactGroup *>(escs->get(i));
                if (candidate && (candidate->id()->rawValue().toInt() == (escNumbers[s] - 1))) {
                    group = candidate;
                    break;
                }
            }

            const bool hasData = group && group->telemetryAvailable() && !stale;
            QVariantMap motor{
                {QStringLiteral("side"), sideNames[s]},
                {QStringLiteral("esc"), escNumbers[s]},
                {QStringLiteral("hasData"), hasData},
            };

            if (hasData) {
                const double temperature = group->temperature()->rawValue().toDouble() / 100.0;
                const double current = group->current()->rawValue().toDouble();
                Side &side = _sides[s];
                const double currentHysteresis = std::max(0.5, std::max(currentWarn, currentCrit) * kCurrentHysteresis);
                side.temperatureLevel = _levelWithHysteresis(temperature, tempWarn, tempCrit, kTemperatureHysteresis, side.temperatureLevel);
                side.currentLevel = _levelWithHysteresis(current, currentWarn, currentCrit, currentHysteresis, side.currentLevel);

                motor.insert(QStringLiteral("temperature"), temperature);
                motor.insert(QStringLiteral("current"), current);
                motor.insert(QStringLiteral("voltage"), group->voltage()->rawValue().toDouble());
                motor.insert(QStringLiteral("rpm"), group->rpm()->rawValue().toInt());
                motor.insert(QStringLiteral("temperatureLevel"), side.temperatureLevel);
                motor.insert(QStringLiteral("currentLevel"), side.currentLevel);

                if (side.temperatureLevel > 0) {
                    alerts.append(QVariantMap{
                        {QStringLiteral("side"), sideNames[s]},
                        {QStringLiteral("kind"), QStringLiteral("temperature")},
                        {QStringLiteral("value"), temperature},
                        {QStringLiteral("threshold"), (side.temperatureLevel >= 2) ? tempCrit : tempWarn},
                        {QStringLiteral("level"), side.temperatureLevel},
                    });
                }
                if (side.currentLevel > 0) {
                    alerts.append(QVariantMap{
                        {QStringLiteral("side"), sideNames[s]},
                        {QStringLiteral("kind"), QStringLiteral("current")},
                        {QStringLiteral("value"), current},
                        {QStringLiteral("threshold"), (side.currentLevel >= 2) ? currentCrit : currentWarn},
                        {QStringLiteral("level"), side.currentLevel},
                    });
                }
                level = std::max({level, side.temperatureLevel, side.currentLevel});
            } else {
                _sides[s] = Side();
                motor.insert(QStringLiteral("temperatureLevel"), 0);
                motor.insert(QStringLiteral("currentLevel"), 0);
            }
            motors.append(motor);
        }

        // Nothing at all for a while: the CAN bus, the stream rate or the controllers themselves
        if (stale) {
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
