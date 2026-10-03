target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        IcmpPing.cc
        IcmpPing.h
        LinkQualityMonitor.cc
        LinkQualityMonitor.h
        WireGuardTunnel.cc
        WireGuardTunnel.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    # IcmpSendEcho, service control manager, ShellExecuteEx
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE iphlpapi ws2_32 advapi32 shell32)
endif()
