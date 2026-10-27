target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        AppUpdater.cc
        AppUpdater.h
        RoomManager.cc
        RoomManager.h
        RoomRadio.cc
        RoomRadio.h
        RoomVideoSettings.cc
        RoomVideoSettings.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
