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

# NX Rover's own version, shown in the app, the .exe properties and the installer, instead of the QGroundControl
# tag (v5.1.5-...) the fork is based on. Raise it for each release.
set(QGC_APP_VERSION_OVERRIDE "1.0.15" CACHE STRING "NX Rover version" FORCE)

# Without this the app runs as "<name> Daily", with a separate settings directory to match.
set(QGC_STABLE_BUILD ON CACHE BOOL "Stable release build" FORCE)

set(QGC_DISABLE_PX4_PLUGIN_FACTORY ON CACHE BOOL "Disable PX4 Plugin Factory" FORCE)
# Replaced by NxFirmwarePluginFactory, which only accepts ArduRover vehicles.
set(QGC_DISABLE_APM_PLUGIN_FACTORY ON CACHE BOOL "Disable APM Plugin Factory" FORCE)

# Debug symbols of the release executable (NX-Rover.pdb, published by the CI build as its own artifact): with them
# a crash dump from the CrashLogs folder shows where the program crashed. The code is already compiled with
# debug info (/Z7, see cmake/Toolchain.cmake); /DEBUG makes the linker keep it in a .pdb next to the .exe, and
# /OPT:REF /OPT:ICF keep the release optimisations that /DEBUG would otherwise switch off. Set before project(),
# where the MSVC platform module appends its own release flags (/INCREMENTAL:NO) to these.
if(CMAKE_HOST_WIN32)
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE_INIT "/DEBUG /OPT:REF /OPT:ICF")
endif()
