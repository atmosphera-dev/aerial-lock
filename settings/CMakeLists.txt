cmake_minimum_required(VERSION 3.21)
project(aerial_settings LANGUAGES CXX)
find_package(Qt6 6.12.0 REQUIRED COMPONENTS Core Gui Widgets DBus Qml Quick QuickControls2)
get_filename_component(AERIAL_SETTINGS_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(AERIAL_SETTINGS_BINDIR "${CMAKE_INSTALL_PREFIX}/bin" CACHE PATH "Settings executable directory")
set(AERIAL_INTERACTIVE_ROOT "${CMAKE_INSTALL_PREFIX}/lib/aerial-lock/interactive" CACHE PATH "Private InterActive root")
include(interactive-runtime.cmake)
aerial_add_interactive()
if(NOT TARGET aerial-client)
    if(AERIAL_CLIENT_ARCHIVE)
        add_library(aerial-client STATIC IMPORTED)
        set_target_properties(aerial-client PROPERTIES IMPORTED_LOCATION "${AERIAL_CLIENT_ARCHIVE}"
            INTERFACE_INCLUDE_DIRECTORIES "${AERIAL_SETTINGS_SOURCE}" INTERFACE_LINK_LIBRARIES "Qt6::Core;Qt6::DBus")
    else()
        add_subdirectory("${AERIAL_SETTINGS_SOURCE}/common/client" "${CMAKE_CURRENT_BINARY_DIR}/client")
    endif()
endif()
add_library(aerial-settings-core STATIC settings-controller.cpp settings-controller.h window-controller.cpp window-controller.h
    sources-controller.cpp sources-controller.h cache-controller.cpp cache-controller.h theme-catalog.cpp theme-catalog.h
    portal-client.cpp portal-client.h systemd-controller.cpp systemd-controller.h
    status-model.cpp status-model.h notices-model.cpp notices-model.h appearance-bridge.cpp appearance-bridge.h
    tray-types.cpp tray-types.h menu-exporter.cpp menu-exporter.h tray-controller.cpp tray-controller.h)
set_target_properties(aerial-settings-core PROPERTIES AUTOMOC ON CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
target_link_libraries(aerial-settings-core PUBLIC aerial-client Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2
    InterActive::Foundation InterActive::Controls InterActive::Components)
target_include_directories(aerial-settings-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
qt_add_resources(settings_resources ui.qrc)
add_library(aerial-settings-resources OBJECT ${settings_resources})
set_target_properties(aerial-settings-resources PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
target_link_libraries(aerial-settings-resources PRIVATE Qt6::Core)
add_executable(aerial-settings main.cpp)
set_target_properties(aerial-settings PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(aerial-settings PRIVATE aerial-settings-core aerial-settings-resources)
file(RELATIVE_PATH settings_to_lib "${AERIAL_SETTINGS_BINDIR}" "${AERIAL_INTERACTIVE_ROOT}/lib")
file(RELATIVE_PATH settings_to_qml "${AERIAL_SETTINGS_BINDIR}" "${AERIAL_INTERACTIVE_ROOT}/qml")
set_target_properties(aerial-settings PROPERTIES INSTALL_RPATH "$ORIGIN/${settings_to_lib}")
target_compile_definitions(aerial-settings-core PUBLIC AERIAL_INTERACTIVE_INSTALLED_QML="${settings_to_qml}")
install(TARGETS aerial-settings RUNTIME DESTINATION "${AERIAL_SETTINGS_BINDIR}" COMPONENT aerial-settings)
# Persist the exact component payload outside the build tree. Install rules
# above have already appended their actual destinations, including module links.
install(CODE "
    list(JOIN CMAKE_INSTALL_MANIFEST_FILES \"\\n\" payload)
    set(receipt \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/licenses/aerial-lock/interactive/install-manifest.txt\")
    file(WRITE \"\${receipt}.new\" \"\${payload}\\n\")
    file(RENAME \"\${receipt}.new\" \"\${receipt}\")
" COMPONENT aerial-settings)
if(AERIAL_COMPANION_DATADIR)
    target_compile_definitions(aerial-settings PRIVATE AERIAL_COMPANION_DATADIR="${AERIAL_COMPANION_DATADIR}")
endif()
