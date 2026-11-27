target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        AdminWatch.cc
        AdminWatch.h
        AppUpdater.cc
        AppUpdater.h
        RoomManager.cc
        RoomManager.h
        RoomRadio.cc
        RoomRadio.h
        RoomScope.cc
        RoomScope.h
        RoomVideoSettings.cc
        RoomVideoSettings.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
