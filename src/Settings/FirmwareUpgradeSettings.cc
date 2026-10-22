#include "FirmwareUpgradeSettings.h"

DECLARE_SETTINGGROUP(FirmwareUpgrade, "FirmwareUpgrade")
{
    // NX only flashes rover firmware
    if (apmVehicleType()->rawValue() != apmVehicleType()->rawDefaultValue()) {
        apmVehicleType()->setRawValue(apmVehicleType()->rawDefaultValue());
    }
}

DECLARE_SETTINGSFACT(FirmwareUpgradeSettings, defaultFirmwareType)
DECLARE_SETTINGSFACT(FirmwareUpgradeSettings, apmChibiOS)
DECLARE_SETTINGSFACT(FirmwareUpgradeSettings, apmVehicleType)
