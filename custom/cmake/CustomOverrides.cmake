# NX GroundControl: ArduRover-only ground station for Windows.

# The app name doubles as the CMake project name, the executable name, the install directory and the
# installer's uninstall registry key; the org name is the settings directory under %APPDATA%. Both must
# differ from stock QGroundControl so the installer never removes or reuses another QGC installation.
set(QGC_APP_NAME "NX-GroundControl" CACHE STRING "App Name" FORCE)
set(QGC_ORG_NAME "NX-GroundControl" CACHE STRING "Org Name" FORCE)
set(QGC_APP_DESCRIPTION "Ground control station for ArduRover ground vehicles" CACHE STRING "Application description" FORCE)

set(QGC_DISABLE_PX4_PLUGIN_FACTORY ON CACHE BOOL "Disable PX4 Plugin Factory" FORCE)
# Replaced by NxFirmwarePluginFactory, which only accepts ArduRover vehicles.
set(QGC_DISABLE_APM_PLUGIN_FACTORY ON CACHE BOOL "Disable APM Plugin Factory" FORCE)
