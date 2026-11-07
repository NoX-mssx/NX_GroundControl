target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        CameraControl.cc
        CameraControl.h
        CameraTalk.cc
        CameraTalk.h
        MotorTelemetryMonitor.cc
        MotorTelemetryMonitor.h
        RtspTalkClient.cc
        RtspTalkClient.h
        SecretProtector.cc
        SecretProtector.h
        TalkAudio.h
        VehicleModelManager.cc
        VehicleModelManager.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    # CryptProtectData / CryptUnprotectData
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE crypt32)
endif()
