message(STATUS "QGC: Adding NX GroundControl custom plugin")

set(CUSTOM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/NxCorePlugin.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/NxCorePlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/FirmwarePlugin/NxFirmwarePluginFactory.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/FirmwarePlugin/NxFirmwarePluginFactory.h
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/FirmwarePlugin
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_DEFINITIONS
    QGC_CUSTOM_BUILD
    CUSTOMHEADER="NxCorePlugin.h"
    CUSTOMCLASS=NxCorePlugin
    CACHE INTERNAL "" FORCE
)
