target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        SecretProtector.cc
        SecretProtector.h
        VehicleModelManager.cc
        VehicleModelManager.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    # CryptProtectData / CryptUnprotectData
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE crypt32)
endif()
