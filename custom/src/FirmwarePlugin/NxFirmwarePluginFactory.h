#pragma once

#include "FirmwarePluginFactory.h"
#include "QGCMAVLink.h"

class ArduRoverFirmwarePlugin;
class FirmwarePlugin;

/// Only ArduPilot ground rovers are supported, which lets QGC drop the UI for other firmware and
/// vehicle types. Any other vehicle falls back to the generic firmware plugin.
class NxFirmwarePluginFactory : public FirmwarePluginFactory
{
    Q_OBJECT

public:
    NxFirmwarePluginFactory();

    QList<QGCMAVLink::FirmwareClass_t> supportedFirmwareClasses() const final;
    QList<QGCMAVLink::VehicleClass_t> supportedVehicleClasses() const final;
    FirmwarePlugin *firmwarePluginForAutopilot(MAV_AUTOPILOT autopilotType, MAV_TYPE vehicleType) final;

private:
    ArduRoverFirmwarePlugin *_roverPluginInstance = nullptr;
};

extern NxFirmwarePluginFactory NxFirmwarePluginFactoryImp;
