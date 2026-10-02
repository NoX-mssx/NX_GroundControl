#pragma once

#include <QtQmlIntegration/QtQmlIntegration>

#include "SettingsGroup.h"

class BatteryIndicatorSettings : public SettingsGroup
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
public:
    BatteryIndicatorSettings(QObject* parent = nullptr);

    DEFINE_SETTING_NAME_GROUP()

    DEFINE_SETTINGFACT(valueDisplay)            // Battery value display mode
    DEFINE_SETTINGFACT(threshold1)              // First threshold for battery level
    DEFINE_SETTINGFACT(threshold2)              // Second threshold for battery level
    DEFINE_SETTINGFACT(consolidateMultipleBatteries)
    DEFINE_SETTINGFACT(percentFromVoltage)      // Derive charge percentage from the voltage range below
    DEFINE_SETTINGFACT(percentVoltageMin)       // Voltage shown as 0%
    DEFINE_SETTINGFACT(percentVoltageMax)       // Voltage shown as 100%

    Q_INVOKABLE void setThreshold1(int value);  // Set threshold1 with validation
    Q_INVOKABLE void setThreshold2(int value);  // Set threshold2 with validation

private:
    void validateThreshold1();
    void validateThreshold2();
    void _threshold1Changed();
    void _threshold2Changed();
};
