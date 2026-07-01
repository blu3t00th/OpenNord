set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "/usr/bin/x86_64-w64-mingw32-objdump")

set(_root "${CMAKE_INSTALL_PREFIX}")
set(_runtime_dir "/usr/x86_64-w64-mingw32/bin")
set(_qt_libraries
    "${_root}/Qt6Concurrent.dll"
    "${_root}/Qt6Core.dll"
    "${_root}/Qt6Gui.dll"
    "${_root}/Qt6Network.dll"
    "${_root}/Qt6Widgets.dll"
)
set(_qt_plugins
    "${_root}/imageformats/qgif.dll"
    "${_root}/imageformats/qico.dll"
    "${_root}/imageformats/qjpeg.dll"
    "${_root}/networkinformation/qnetworklistmanager.dll"
    "${_root}/platforms/qwindows.dll"
    "${_root}/styles/qmodernwindowsstyle.dll"
    "${_root}/tls/qcertonlybackend.dll"
    "${_root}/tls/qschannelbackend.dll"
)

file(GET_RUNTIME_DEPENDENCIES
    RESOLVED_DEPENDENCIES_VAR _resolved
    UNRESOLVED_DEPENDENCIES_VAR _unresolved
    EXECUTABLES "${_root}/OpenNord.exe" "${_root}/OpenNordService.exe"
    LIBRARIES ${_qt_libraries}
    MODULES ${_qt_plugins}
    DIRECTORIES "${_root}" "${_runtime_dir}"
)

foreach(_dependency IN LISTS _resolved)
    file(REAL_PATH "${_dependency}" _real_dependency)
    file(INSTALL DESTINATION "${_root}" TYPE SHARED_LIBRARY FILES "${_real_dependency}")
endforeach()

set(_system_dependencies)
foreach(_dependency IN LISTS _unresolved)
    if(NOT EXISTS "${_root}/${_dependency}")
        list(APPEND _system_dependencies "${_dependency}")
    endif()
endforeach()
if(_system_dependencies)
    list(SORT _system_dependencies)
    message(STATUS "Windows system DLLs left for the target OS: ${_system_dependencies}")
endif()
