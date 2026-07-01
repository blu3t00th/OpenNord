cmake_minimum_required(VERSION 3.24)

project(OpenNordWindows VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(OPENNORD_BUILD_TESTS "Build unit tests" ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network)

add_library(opennord_common STATIC
    src/common/Logging.cpp
    src/common/Logging.h
    src/common/Models.cpp
    src/common/Models.h
    src/common/OpenVpnConfig.cpp
    src/common/OpenVpnConfig.h
    src/common/Protocol.cpp
    src/common/Protocol.h
    src/common/WireGuardConfig.cpp
    src/common/WireGuardConfig.h
)
target_link_libraries(opennord_common PUBLIC Qt6::Core Qt6::Network)
target_include_directories(opennord_common PUBLIC src)
target_compile_definitions(opennord_common PUBLIC
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    OPENNORD_VERSION="${PROJECT_VERSION}"
)

if(WIN32)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Concurrent Widgets)

    add_library(opennord_windows STATIC
        src/windows/Security.cpp
        src/windows/Security.h
        src/windows/WinHandle.h
    )
    target_link_libraries(opennord_windows PUBLIC Qt6::Core advapi32 bcrypt crypt32 userenv)
    target_include_directories(opennord_windows PUBLIC src)

    add_library(opennord_api STATIC
        src/api/NordApiClient.cpp
        src/api/NordApiClient.h
        src/api/ResponseVerifier.cpp
        src/api/ResponseVerifier.h
    )
    target_link_libraries(opennord_api PUBLIC opennord_common opennord_windows Qt6::Network bcrypt crypt32)
    target_include_directories(opennord_api PUBLIC src)

    add_executable(OpenNordService
        src/service/ClientContext.h
        src/service/NamedPipeServer.cpp
        src/service/NamedPipeServer.h
        src/service/OpenVpnTunnel.cpp
        src/service/OpenVpnTunnel.h
        src/service/SecureStore.cpp
        src/service/SecureStore.h
        src/service/ServiceMain.cpp
        src/service/VpnController.cpp
        src/service/VpnController.h
        src/service/WindowsService.cpp
        src/service/WindowsService.h
        src/service/WireGuardTunnel.cpp
        src/service/WireGuardTunnel.h
    )
    target_link_libraries(OpenNordService PRIVATE
        opennord_common opennord_api opennord_windows Qt6::Core Qt6::Network
        advapi32 shell32
    )
    target_compile_definitions(OpenNordService PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

    qt_add_executable(OpenNord WIN32
        src/gui/MainWindow.cpp
        src/gui/MainWindow.h
        src/gui/RpcClient.cpp
        src/gui/RpcClient.h
        src/gui/main.cpp
        resources/opennord.rc
    )
    target_link_libraries(OpenNord PRIVATE
        opennord_common opennord_windows Qt6::Core Qt6::Concurrent Qt6::Network Qt6::Widgets shell32
    )
    target_include_directories(OpenNord PRIVATE resources)
    target_compile_definitions(OpenNord PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

    install(TARGETS OpenNord OpenNordService RUNTIME DESTINATION .)

    if(MINGW)
        get_filename_component(OPENNORD_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
        set(OPENNORD_MINGW_RUNTIME_DIR "${OPENNORD_QT_PREFIX}/bin")
        set(OPENNORD_QT_BINARY_DIR "${OPENNORD_QT_PREFIX}/lib/qt6/bin")
        set(OPENNORD_QT_PLUGIN_DIR "${OPENNORD_QT_PREFIX}/lib/qt6/plugins")

        install(FILES
            "${OPENNORD_QT_BINARY_DIR}/Qt6Concurrent.dll"
            "${OPENNORD_QT_BINARY_DIR}/Qt6Core.dll"
            "${OPENNORD_QT_BINARY_DIR}/Qt6Gui.dll"
            "${OPENNORD_QT_BINARY_DIR}/Qt6Network.dll"
            "${OPENNORD_QT_BINARY_DIR}/Qt6Widgets.dll"
            DESTINATION .
        )
        install(FILES
            "${OPENNORD_QT_PLUGIN_DIR}/imageformats/qgif.dll"
            "${OPENNORD_QT_PLUGIN_DIR}/imageformats/qico.dll"
            "${OPENNORD_QT_PLUGIN_DIR}/imageformats/qjpeg.dll"
            DESTINATION imageformats
        )
        install(FILES "${OPENNORD_QT_PLUGIN_DIR}/networkinformation/qnetworklistmanager.dll" DESTINATION networkinformation)
        install(FILES "${OPENNORD_QT_PLUGIN_DIR}/platforms/qwindows.dll" DESTINATION platforms)
        install(FILES "${OPENNORD_QT_PLUGIN_DIR}/styles/qmodernwindowsstyle.dll" DESTINATION styles)
        install(FILES
            "${OPENNORD_QT_PLUGIN_DIR}/tls/qcertonlybackend.dll"
            "${OPENNORD_QT_PLUGIN_DIR}/tls/qschannelbackend.dll"
            DESTINATION tls
        )

        configure_file(cmake/InstallMinGWRuntime.cmake.in InstallMinGWRuntime.cmake @ONLY)
        install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/InstallMinGWRuntime.cmake")
    endif()
endif()

if(OPENNORD_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    qt_add_executable(opennord_tests
        tests/CommonTests.cpp
    )
    target_link_libraries(opennord_tests PRIVATE opennord_common Qt6::Test)
    add_test(NAME opennord_tests COMMAND opennord_tests)
endif()
