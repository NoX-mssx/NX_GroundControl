#include "NxFirmwarePluginFactory.h"
#include "ArduRoverFirmwarePlugin.h"

NxFirmwarePluginFactory NxFirmwarePluginFactoryImp;

NxFirmwarePluginFactory::NxFirmwarePluginFactory()
    : FirmwarePluginFactory(nullptr)
{
}

QList<QGCMAVLink::FirmwareClass_t> NxFirmwarePluginFactory::supportedFirmwareClasses() const
{
    return {QGCMAVLink::FirmwareClassArduPilot};
}

QList<QGCMAVLink::VehicleClass_t> NxFirmwarePluginFactory::supportedVehicleClasses() const
{
    return {QGCMAVLink::VehicleClassRoverBoat};
}

FirmwarePlugin *NxFirmwarePluginFactory::firmwarePluginForAutopilot(MAV_AUTOPILOT autopilotType, MAV_TYPE vehicleType)
{
    if ((autopilotType != MAV_AUTOPILOT_ARDUPILOTMEGA) || (vehicleType != MAV_TYPE_GROUND_ROVER)) {
        return nullptr;
    }

    if (!_roverPluginInstance) {
        _roverPluginInstance = new ArduRoverFirmwarePlugin;
    }
    return _roverPluginInstance;
}
