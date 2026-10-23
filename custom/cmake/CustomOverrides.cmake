# NX Rover: ArduRover-only ground station for Windows.

# The app name doubles as the CMake project name, the executable name, the install directory and the
# installer's uninstall registry key; the org name is the settings directory under %APPDATA%. Both must
# differ from stock QGroundControl and from the earlier NX-GroundControl build, so the installer never removes or
# reuses another installation and NX Rover starts with its own, empty settings.
set(QGC_APP_NAME "NX-Rover" CACHE STRING "App Name" FORCE)
set(QGC_ORG_NAME "NX-Rover" CACHE STRING "Org Name" FORCE)
set(QGC_APP_DESCRIPTION "Ground control station for ArduRover ground vehicles" CACHE STRING "Application description" FORCE)

set(QGC_WINDOWS_ICON_PATH "${CMAKE_SOURCE_DIR}/${QGC_CUSTOM_DIR}/deploy/windows/NxGroundControl.ico" CACHE FILEPATH "Windows Icon Path" FORCE)
# NX Rover banner in the installer header (150 x 57, made from deploy/branding/nx-rover-wide-dark.svg)
set(QGC_WINDOWS_INSTALL_HEADER_PATH "${CMAKE_SOURCE_DIR}/${QGC_CUSTOM_DIR}/deploy/windows/installheader.bmp" CACHE FILEPATH "Windows installer header image" FORCE)

# Without this the app runs as "<name> Daily", with a separate settings directory to match.
set(QGC_STABLE_BUILD ON CACHE BOOL "Stable release build" FORCE)

set(QGC_DISABLE_PX4_PLUGIN_FACTORY ON CACHE BOOL "Disable PX4 Plugin Factory" FORCE)
# Replaced by NxFirmwarePluginFactory, which only accepts ArduRover vehicles.
set(QGC_DISABLE_APM_PLUGIN_FACTORY ON CACHE BOOL "Disable APM Plugin Factory" FORCE)
