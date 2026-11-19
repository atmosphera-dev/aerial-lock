cmake_minimum_required(VERSION 3.16)
project(aerial_lock_tests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(AERIAL_LOCK_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH
    "aerial-lock source tree used by the test fixtures")
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Test DBus Network)

add_executable(native-input-probe native-input-probe.cpp)
target_link_libraries(native-input-probe PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)

enable_testing()
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/local-config.moc"
    COMMAND Qt6::moc "${AERIAL_LOCK_SOURCE}/native/local-config.h" -o "${CMAKE_CURRENT_BINARY_DIR}/local-config.moc"
    DEPENDS "${AERIAL_LOCK_SOURCE}/native/local-config.h")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/config-repository.moc"
    COMMAND Qt6::moc "${AERIAL_LOCK_SOURCE}/companion/config-repository.h" -o "${CMAKE_CURRENT_BINARY_DIR}/config-repository.moc"
    DEPENDS "${AERIAL_LOCK_SOURCE}/companion/config-repository.h")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/companion-config-probe.moc"
    COMMAND Qt6::moc "${CMAKE_CURRENT_SOURCE_DIR}/companion-config-probe.cpp" -o "${CMAKE_CURRENT_BINARY_DIR}/companion-config-probe.moc"
    DEPENDS companion-config-probe.cpp)
add_executable(companion-config-probe companion-config-probe.cpp
    "${AERIAL_LOCK_SOURCE}/common/config-document.cpp" "${AERIAL_LOCK_SOURCE}/native/local-config.cpp"
    "${AERIAL_LOCK_SOURCE}/companion/config-repository.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/local-config.moc" "${CMAKE_CURRENT_BINARY_DIR}/config-repository.moc"
    "${CMAKE_CURRENT_BINARY_DIR}/companion-config-probe.moc")
target_include_directories(companion-config-probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(companion-config-probe PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
add_test(NAME companion-config COMMAND companion-config-probe "${AERIAL_LOCK_SOURCE}")
find_program(QDBUSXML2CPP qdbusxml2cpp PATHS /usr/lib/qt6/bin REQUIRED)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/companion_client.cpp" "${CMAKE_CURRENT_BINARY_DIR}/companion_client.h"
    COMMAND "${QDBUSXML2CPP}" -m -N -c CompanionClient -p "${CMAKE_CURRENT_BINARY_DIR}/companion_client" -i common/dbus-types.h "${AERIAL_LOCK_SOURCE}/dbus/org.aeriallock.Companion1.xml"
    DEPENDS "${AERIAL_LOCK_SOURCE}/dbus/org.aeriallock.Companion1.xml")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/moc_companion_client.cpp"
    COMMAND Qt6::moc "${CMAKE_CURRENT_BINARY_DIR}/companion_client.h" -o "${CMAKE_CURRENT_BINARY_DIR}/moc_companion_client.cpp"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/companion_client.h")
add_executable(companion-api-probe companion-api-probe.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/companion_client.cpp" "${CMAKE_CURRENT_BINARY_DIR}/moc_companion_client.cpp")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/moc_companion_client.cpp" PROPERTIES HEADER_FILE_ONLY TRUE)
target_include_directories(companion-api-probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${AERIAL_LOCK_SOURCE}")
target_link_libraries(companion-api-probe PRIVATE Qt6::Core Qt6::DBus Qt6::Test)
add_test(NAME companion-api COMMAND dbus-run-session -- "$<TARGET_FILE:companion-api-probe>"
    "${AERIAL_LOCK_SOURCE}/companion/aerial-companion" "${AERIAL_LOCK_SOURCE}")
foreach(stem IN ITEMS companion-client companion-hint-probe)
    if(stem STREQUAL "companion-client")
        set(input "${AERIAL_LOCK_SOURCE}/native/companion-client.h")
    else()
        set(input "${CMAKE_CURRENT_SOURCE_DIR}/companion-hint-probe.cpp")
    endif()
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${stem}.moc"
        COMMAND Qt6::moc "${input}" -o "${CMAKE_CURRENT_BINARY_DIR}/${stem}.moc" DEPENDS "${input}")
endforeach()
add_executable(companion-hint-probe companion-hint-probe.cpp "${AERIAL_LOCK_SOURCE}/native/companion-client.cpp"
    "${AERIAL_LOCK_SOURCE}/common/config-document.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/companion-client.moc" "${CMAKE_CURRENT_BINARY_DIR}/companion-hint-probe.moc")
target_include_directories(companion-hint-probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)
target_link_libraries(companion-hint-probe PRIVATE Qt6::Core Qt6::DBus Qt6::Test PkgConfig::DBUS)
add_test(NAME companion-hint COMMAND dbus-run-session -- "$<TARGET_FILE:companion-hint-probe>")
add_executable(auth-client-probe auth-client-probe.c "${AERIAL_LOCK_SOURCE}/native/auth-client.c")
set_property(TARGET auth-client-probe PROPERTY C_STANDARD 11)
target_link_options(auth-client-probe PRIVATE "-Wl,--wrap=send")
add_test(NAME auth-client COMMAND auth-client-probe "${CMAKE_CURRENT_SOURCE_DIR}/auth-fake-helper.py")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/power-actions.moc"
    COMMAND Qt6::moc "${AERIAL_LOCK_SOURCE}/native/power-actions.h" -o "${CMAKE_CURRENT_BINARY_DIR}/power-actions.moc"
    DEPENDS "${AERIAL_LOCK_SOURCE}/native/power-actions.h")
add_executable(power-actions-probe power-actions-probe.cpp "${AERIAL_LOCK_SOURCE}/native/power-actions.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/power-actions.moc")
target_include_directories(power-actions-probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(power-actions-probe PRIVATE Qt6::Core Qt6::DBus Qt6::Test)
add_executable(power-actions-bus-fixture power-actions-bus-fixture.cpp)
target_link_libraries(power-actions-bus-fixture PRIVATE Qt6::Core Qt6::DBus)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/power-actions-controller.moc"
    COMMAND Qt6::moc "${CMAKE_CURRENT_SOURCE_DIR}/power-actions-controller.cpp"
        -o "${CMAKE_CURRENT_BINARY_DIR}/power-actions-controller.moc"
    DEPENDS power-actions-controller.cpp)
add_library(power-actions-controller MODULE power-actions-controller.cpp
    "${AERIAL_LOCK_SOURCE}/native/power-actions.cpp" "${CMAKE_CURRENT_BINARY_DIR}/power-actions.moc"
    "${CMAKE_CURRENT_BINARY_DIR}/power-actions-controller.moc")
set_target_properties(power-actions-controller PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/power-test/AerialLock/PowerTest")
target_include_directories(power-actions-controller PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(power-actions-controller PRIVATE Qt6::Qml Qt6::DBus)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/power-test/AerialLock/PowerTest")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/power-test/AerialLock/PowerTest/qmldir"
    "module AerialLock.PowerTest\nplugin power-actions-controller\n")
add_library(auth-pam-fixture MODULE auth-pam-fixture.c)
add_library(auth-timezone-hammer SHARED auth-timezone-hammer.c)
target_link_libraries(auth-timezone-hammer PRIVATE pthread)
target_link_libraries(auth-pam-fixture PRIVATE pam)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/auth-context.moc"
    COMMAND Qt6::moc "${AERIAL_LOCK_SOURCE}/native/auth-context.h" -o "${CMAKE_CURRENT_BINARY_DIR}/auth-context.moc"
    DEPENDS "${AERIAL_LOCK_SOURCE}/native/auth-context.h")
add_executable(auth-context-probe auth-context-probe.cpp "${AERIAL_LOCK_SOURCE}/native/auth-context.cpp"
    "${AERIAL_LOCK_SOURCE}/native/auth-client.c"
    "${CMAKE_CURRENT_BINARY_DIR}/auth-context.moc")
target_include_directories(auth-context-probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(auth-context-probe PRIVATE Qt6::Core Qt6::Test)
add_test(NAME auth-context COMMAND auth-context-probe "${CMAKE_CURRENT_SOURCE_DIR}/auth-fake-helper.py")
add_test(NAME native-input-probe
    COMMAND native-input-probe "${AERIAL_LOCK_SOURCE}/Tests/auth-input-fixture.qml")
add_test(NAME native-input-migration
    COMMAND native-input-probe --migration "${AERIAL_LOCK_SOURCE}/Tests/auth-input-fixture.qml")
add_test(NAME native-input-sink-panel
    COMMAND native-input-probe --sink-panel "${AERIAL_LOCK_SOURCE}/Tests/auth-input-fixture.qml")
add_executable(cross-window-authority cross-window-authority.cpp)
target_link_libraries(cross-window-authority PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
foreach(mode IN ITEMS natural empty-state same-window focus-out)
    add_test(NAME native-authority-${mode}
        COMMAND cross-window-authority "${mode}" "${AERIAL_LOCK_SOURCE}/Tests/cross-window-authority.qml")
endforeach()
add_executable(surface-input-filter-probe surface-input-filter-probe.cpp)
target_link_libraries(surface-input-filter-probe PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test)
add_test(NAME surface-input-filter COMMAND surface-input-filter-probe)
set_tests_properties(surface-input-filter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(connection-loss-probe connection-loss-probe.cpp)
target_link_libraries(connection-loss-probe PRIVATE Qt6::Core pthread)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME power-actions COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-power-actions.py"
    "$<TARGET_FILE:power-actions-probe>" "$<TARGET_FILE:power-actions-bus-fixture>")
add_test(NAME power-vm-discovery COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-power-vm.py")
add_test(NAME shadow-fixture COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-shadow-fixture.py")
add_test(NAME auth-helper COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-auth-helper.py"
    "${AERIAL_LOCK_SOURCE}/native/aerial-lock-auth" "$<TARGET_FILE:auth-pam-fixture>")
add_test(NAME surface-input-teardown
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-surface-input-teardown.py")
# VM integration drivers validate an operator-supplied owned-instance manifest
# (Tests/README.md "Owned manifest"); its regression runs only when one is given.
set(AERIAL_LOCK_VM_RECORD "" CACHE FILEPATH "Operator-supplied owned VM/session manifest")
if(AERIAL_LOCK_VM_RECORD)
    add_test(NAME owned-instance
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-owned-instance.py"
                --vm-record "${AERIAL_LOCK_VM_RECORD}")
endif()
add_test(NAME panel-output-paths
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-panel-output-paths.py")
add_test(NAME driver-wait-order
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-driver-wait-order.py")
add_test(NAME process-state
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-process-state.py")
add_test(NAME blocked-latch
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-blocked-latch.py"
            "$<TARGET_FILE:connection-loss-probe>")
find_package(PkgConfig REQUIRED)
pkg_check_modules(ARCHIVE REQUIRED IMPORTED_TARGET libarchive)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/companion-content-probe.moc"
    COMMAND Qt6::moc "${CMAKE_CURRENT_SOURCE_DIR}/companion-content-probe.cpp" -o "${CMAKE_CURRENT_BINARY_DIR}/companion-content-probe.moc"
    DEPENDS companion-content-probe.cpp)
add_executable(companion-content-probe companion-content-probe.cpp
    "${AERIAL_LOCK_SOURCE}/common/owned-directory.cpp" "${AERIAL_LOCK_SOURCE}/common/content-snapshot.cpp"
    "${AERIAL_LOCK_SOURCE}/companion/source-resolver.cpp" "${AERIAL_LOCK_SOURCE}/companion/cache-store.cpp"
    "${AERIAL_LOCK_SOURCE}/companion/planner-state.cpp" "${AERIAL_LOCK_SOURCE}/companion/acquisition-planner.cpp"
    "${AERIAL_LOCK_SOURCE}/native/local-content.cpp" "${CMAKE_CURRENT_BINARY_DIR}/local-content.moc"
    "${CMAKE_CURRENT_BINARY_DIR}/companion-content-probe.moc")
target_include_directories(companion-content-probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(companion-content-probe PRIVATE AERIAL_TEST_SOURCE="${AERIAL_LOCK_SOURCE}")
target_link_libraries(companion-content-probe PRIVATE Qt6::Core Qt6::Test PkgConfig::ARCHIVE)
add_test(NAME companion-provider COMMAND companion-content-probe resolver selectedTarMember)
add_test(NAME companion-cache COMMAND companion-content-probe directoryDurability uncertainCacheCommit cacheAccountingAndOwnership loweredCapReclamation corruptPointerRecovery crashTransactions reconciliationOwnership recoveryReaderProtection)
add_test(NAME companion-snapshot COMMAND companion-content-probe snapshotPinningAndRestart nonblockingAndCorruption nativeEpisodeGeneration localFallbackInventory)
add_test(NAME companion-planner COMMAND companion-content-probe plannerStorageRecovery plannerDurability plannerSelectionAndCadence)
# Explicit VM acceptance only: needs a dedicated small loop-mounted filesystem.
# It must not fill the caller's normal workspace or the CI container filesystem.
add_executable(companion-disk-probe companion-disk-probe.cpp
    "${AERIAL_LOCK_SOURCE}/common/owned-directory.cpp" "${AERIAL_LOCK_SOURCE}/common/content-snapshot.cpp"
    "${AERIAL_LOCK_SOURCE}/companion/cache-store.cpp" "${AERIAL_LOCK_SOURCE}/companion/source-resolver.cpp")
target_link_libraries(companion-disk-probe PRIVATE Qt6::Core PkgConfig::ARCHIVE)
target_link_options(companion-disk-probe PRIVATE "-Wl,--wrap=write")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/local-content.moc"
    COMMAND Qt6::moc "${AERIAL_LOCK_SOURCE}/native/local-content.h" -o "${CMAKE_CURRENT_BINARY_DIR}/local-content.moc"
    DEPENDS "${AERIAL_LOCK_SOURCE}/native/local-content.h")
foreach(stem IN ITEMS http-operation local-probe transfer manifest-store transfer-queue policy-monitor)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${stem}.moc"
        COMMAND Qt6::moc "${AERIAL_LOCK_SOURCE}/companion/${stem}.h" -o "${CMAKE_CURRENT_BINARY_DIR}/${stem}.moc"
        DEPENDS "${AERIAL_LOCK_SOURCE}/companion/${stem}.h")
endforeach()
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/companion-transfer-probe.moc"
    COMMAND Qt6::moc "${CMAKE_CURRENT_SOURCE_DIR}/companion-transfer-probe.cpp" -o "${CMAKE_CURRENT_BINARY_DIR}/companion-transfer-probe.moc"
    DEPENDS companion-transfer-probe.cpp)
add_executable(companion-transfer-probe companion-transfer-probe.cpp
    "${AERIAL_LOCK_SOURCE}/common/owned-directory.cpp" "${AERIAL_LOCK_SOURCE}/common/content-snapshot.cpp"
    "${AERIAL_LOCK_SOURCE}/companion/cache-store.cpp" "${AERIAL_LOCK_SOURCE}/companion/source-resolver.cpp"
    "${AERIAL_LOCK_SOURCE}/companion/http-operation.cpp" "${AERIAL_LOCK_SOURCE}/companion/local-probe.cpp"
    "${AERIAL_LOCK_SOURCE}/companion/transfer.cpp" "${CMAKE_CURRENT_BINARY_DIR}/http-operation.moc"
    "${AERIAL_LOCK_SOURCE}/companion/manifest-store.cpp" "${CMAKE_CURRENT_BINARY_DIR}/manifest-store.moc"
    "${AERIAL_LOCK_SOURCE}/companion/planner-state.cpp" "${AERIAL_LOCK_SOURCE}/companion/acquisition-planner.cpp"
    "${AERIAL_LOCK_SOURCE}/companion/transfer-queue.cpp" "${CMAKE_CURRENT_BINARY_DIR}/transfer-queue.moc"
    "${AERIAL_LOCK_SOURCE}/companion/policy-monitor.cpp" "${CMAKE_CURRENT_BINARY_DIR}/policy-monitor.moc"
    "${AERIAL_LOCK_SOURCE}/companion/config-repository.cpp" "${CMAKE_CURRENT_BINARY_DIR}/config-repository.moc"
    "${AERIAL_LOCK_SOURCE}/common/config-document.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/local-probe.moc" "${CMAKE_CURRENT_BINARY_DIR}/transfer.moc"
    "${CMAKE_CURRENT_BINARY_DIR}/companion-transfer-probe.moc")
target_include_directories(companion-transfer-probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(companion-transfer-probe PRIVATE AERIAL_TEST_SOURCE="${AERIAL_LOCK_SOURCE}")
target_link_libraries(companion-transfer-probe PRIVATE Qt6::Core Qt6::DBus Qt6::Network Qt6::Test PkgConfig::ARCHIVE)
# Discover every Qt slot, but give it one suite owner. Running the unfiltered
# probe here would repeat the policy/control matrix before those suites run.
foreach(suite IN ITEMS transfer policy control)
    add_test(NAME companion-${suite} COMMAND dbus-run-session -- "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/run-companion-suite.py" "${suite}"
        "$<TARGET_FILE:companion-transfer-probe>" "${AERIAL_LOCK_SOURCE}/companion/aerial-media-probe")
endforeach()
add_test(NAME companion-queue COMMAND dbus-run-session -- "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-companion-queue.py"
    "${AERIAL_LOCK_SOURCE}/companion/aerial-companion" "${AERIAL_LOCK_SOURCE}")
add_test(NAME companion-owner COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-companion-owner.py"
    "${AERIAL_LOCK_SOURCE}/companion/aerial-companion" "${AERIAL_LOCK_SOURCE}")
add_test(NAME companion-install COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-companion-install.py"
    "${AERIAL_LOCK_SOURCE}")
add_executable(companion-http-probe companion-http-probe.cpp
    "${AERIAL_LOCK_SOURCE}/companion/http-operation.cpp" "${CMAKE_CURRENT_BINARY_DIR}/http-operation.moc"
    "${AERIAL_LOCK_SOURCE}/companion/source-resolver.cpp" "${AERIAL_LOCK_SOURCE}/common/content-snapshot.cpp"
    "${AERIAL_LOCK_SOURCE}/common/owned-directory.cpp")
target_include_directories(companion-http-probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(companion-http-probe PRIVATE Qt6::Core Qt6::Network PkgConfig::ARCHIVE)
add_test(NAME companion-http COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-companion-http.py"
    "$<TARGET_FILE:companion-http-probe>")
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
add_executable(emergency-unit emergency-unit.c "${AERIAL_LOCK_SOURCE}/emergency/input.c" "${AERIAL_LOCK_SOURCE}/emergency/render.c")
target_link_libraries(emergency-unit PRIVATE PkgConfig::WAYLAND PkgConfig::XKBCOMMON)
add_test(NAME emergency-unit COMMAND emergency-unit)
add_executable(emergency-settings-probe emergency-settings-probe.cpp "${AERIAL_LOCK_SOURCE}/common/config-document.cpp")
target_link_libraries(emergency-settings-probe PRIVATE Qt6::Core Qt6::Qml)
add_test(NAME emergency-settings COMMAND emergency-settings-probe "${AERIAL_LOCK_SOURCE}")
add_test(NAME emergency-integration-contract COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-emergency-integration.py")
target_link_libraries(connection-loss-probe PRIVATE Qt6::Gui PkgConfig::WAYLAND)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/aerial-lock-supervisor.moc"
    COMMAND Qt6::moc "${AERIAL_LOCK_SOURCE}/supervisor/aerial-lock-supervisor.cpp" -o "${CMAKE_CURRENT_BINARY_DIR}/aerial-lock-supervisor.moc"
    DEPENDS "${AERIAL_LOCK_SOURCE}/supervisor/aerial-lock-supervisor.cpp")
set(SUPERVISOR_LOCAL_SOURCES "${AERIAL_LOCK_SOURCE}/common/config-document.cpp"
    "${AERIAL_LOCK_SOURCE}/common/owned-directory.cpp" "${AERIAL_LOCK_SOURCE}/common/content-snapshot.cpp")
add_executable(supervisor-test "${AERIAL_LOCK_SOURCE}/supervisor/aerial-lock-supervisor.cpp" "${CMAKE_CURRENT_BINARY_DIR}/aerial-lock-supervisor.moc" ${SUPERVISOR_LOCAL_SOURCES})
target_include_directories(supervisor-test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(supervisor-test PRIVATE AERIAL_LOCK_DEFAULT_EMERGENCY="${CMAKE_CURRENT_SOURCE_DIR}/supervisor-emergency-stub.py")
target_link_libraries(supervisor-test PRIVATE Qt6::Core Qt6::DBus PkgConfig::WAYLAND)
add_library(guard-logger-trap SHARED guard-logger-trap.cpp)
target_link_libraries(guard-logger-trap PRIVATE Qt6::Core ${CMAKE_DL_LIBS})
add_test(NAME supervisor-session
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-supervisor-session.py"
            "$<TARGET_FILE:supervisor-test>")
add_test(NAME companion-episode COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-companion-episode.py"
    "$<TARGET_FILE:supervisor-test>")
add_test(NAME supervisor-emergency COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-supervisor-emergency.py" "$<TARGET_FILE:supervisor-test>")
add_test(NAME supervisor-output
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-supervisor-output.py"
            "$<TARGET_FILE:supervisor-test>")
add_test(NAME supervisor-hyprland
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-supervisor-hyprland.py"
            "$<TARGET_FILE:supervisor-test>")
add_executable(supervisor-terminal-probe supervisor-terminal-probe.cpp ${SUPERVISOR_LOCAL_SOURCES})
target_link_libraries(supervisor-terminal-probe PRIVATE Qt6::Core Qt6::DBus PkgConfig::WAYLAND)
target_compile_definitions(supervisor-terminal-probe PRIVATE AERIAL_LOCK_DEFAULT_EMERGENCY="${CMAKE_CURRENT_SOURCE_DIR}/supervisor-emergency-stub.py")
add_test(NAME supervisor-release
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-supervisor-release.py"
            "$<TARGET_FILE:supervisor-test>"
            "$<TARGET_FILE:supervisor-terminal-probe>")
add_test(NAME supervisor-failures
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-supervisor-failures.py"
            "$<TARGET_FILE:supervisor-test>"
            "${AERIAL_LOCK_SOURCE}/Scripts/aerial-lock-run")
add_test(NAME script-arguments
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-script-arguments.py"
            "${AERIAL_LOCK_SOURCE}")
# Preserve the original incremental sequence without placing every full rebuild
# under one CTest deadline. Fixture prerequisites also support focused -R runs.
set(build_config_workspace "${CMAKE_CURRENT_BINARY_DIR}/build-config-workspace")
set(build_config_fixtures)
set(build_config_previous)
foreach(phase IN ITEMS paths dev flags install lto)
    add_test(NAME build-config-${phase}
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-build-config.py"
                "${AERIAL_LOCK_SOURCE}" --case "${phase}" --workspace "${build_config_workspace}")
    set_tests_properties(build-config-${phase} PROPERTIES FIXTURES_SETUP build-config-${phase})
    if(build_config_previous)
        set_tests_properties(build-config-${phase} PROPERTIES FIXTURES_REQUIRED "${build_config_previous}")
    endif()
    list(APPEND build_config_fixtures build-config-${phase})
    set(build_config_previous build-config-${phase})
endforeach()
add_test(NAME build-config-cleanup
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-build-config.py"
            "${AERIAL_LOCK_SOURCE}" --case cleanup --workspace "${build_config_workspace}")
set_tests_properties(build-config-cleanup PROPERTIES FIXTURES_CLEANUP "${build_config_fixtures}")
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
add_library(full-caller-faults SHARED full-caller-faults.c)
target_link_libraries(full-caller-faults PRIVATE PkgConfig::MPV pthread ${CMAKE_DL_LIBS})
find_program(QMLTESTRUNNER qmltestrunner PATHS /usr/lib/qt6/bin /usr/lib64/qt6/bin REQUIRED)
add_test(NAME media-fallback
    COMMAND "${QMLTESTRUNNER}" -input "${AERIAL_LOCK_SOURCE}/Tests/media-fallback-fault.qml"
            MediaFallbackFault::test_core_failure_keeps_solid_scenery_and_reports_once)
set_tests_properties(media-fallback PROPERTIES ENVIRONMENT
    "LD_PRELOAD=$<TARGET_FILE:full-caller-faults>;AERIAL_TEST_FULL_CALLER_FAULT=core-create;QT_QPA_PLATFORM=offscreen")
# Needs a platform that renders video (e.g. inside a Wayland session). On a
# headless QPA plugin or a non-OpenGL Qt Quick backend the case skips, and CTest
# reports it as skipped rather than passed.
add_test(NAME media-fallback-render
    COMMAND "${QMLTESTRUNNER}" -input "${AERIAL_LOCK_SOURCE}/Tests/media-fallback-fault.qml"
            MediaFallbackFault::test_render_failure_stops_hidden_playback)
set_tests_properties(media-fallback-render PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP +: ")
# Native desktop input and pointer probes used by the VM output-failover drivers.
add_executable(desktop-input-probe desktop-input-probe.cpp)
target_link_libraries(desktop-input-probe PRIVATE Qt6::Gui Qt6::Quick)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
pkg_get_variable(EMERGENCY_PROTOCOLS_DIR wayland-protocols pkgdatadir)
set(EMERGENCY_XML "${EMERGENCY_PROTOCOLS_DIR}/staging/ext-session-lock/ext-session-lock-v1.xml")
foreach(kind IN ITEMS client server)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/emergency-${kind}.h"
        COMMAND "${WAYLAND_SCANNER}" ${kind}-header "${EMERGENCY_XML}" "${CMAKE_CURRENT_BINARY_DIR}/emergency-${kind}.h"
        DEPENDS "${EMERGENCY_XML}")
endforeach()
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/emergency-protocol.c"
    COMMAND "${WAYLAND_SCANNER}" private-code "${EMERGENCY_XML}" "${CMAKE_CURRENT_BINARY_DIR}/emergency-protocol.c"
    DEPENDS "${EMERGENCY_XML}")
add_executable(emergency-peer emergency-peer.c "${CMAKE_CURRENT_BINARY_DIR}/emergency-server.h" "${CMAKE_CURRENT_BINARY_DIR}/emergency-protocol.c")
target_include_directories(emergency-peer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(emergency-peer PRIVATE PkgConfig::WAYLAND_SERVER PkgConfig::XKBCOMMON)
add_executable(emergency-test-client "${AERIAL_LOCK_SOURCE}/emergency/aerial-lock-emergency.c"
    "${AERIAL_LOCK_SOURCE}/emergency/wayland.c" "${AERIAL_LOCK_SOURCE}/emergency/render.c" "${AERIAL_LOCK_SOURCE}/emergency/input.c"
    "${AERIAL_LOCK_SOURCE}/native/auth-client.c" "${CMAKE_CURRENT_BINARY_DIR}/emergency-client.h" "${CMAKE_CURRENT_BINARY_DIR}/emergency-protocol.c")
# The production include name stays identical; only this build's helper path differs.
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-client.h"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_BINARY_DIR}/emergency-client.h" "${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-client.h"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/emergency-client.h")
target_sources(emergency-test-client PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-client.h")
target_include_directories(emergency-test-client PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(emergency-test-client PRIVATE AERIAL_LOCK_AUTH_HELPER="${CMAKE_CURRENT_SOURCE_DIR}/emergency-fake-helper.py")
target_link_libraries(emergency-test-client PRIVATE PkgConfig::WAYLAND PkgConfig::XKBCOMMON)
add_test(NAME emergency-peer COMMAND emergency-peer "$<TARGET_FILE:emergency-test-client>")
set_tests_properties(emergency-peer PROPERTIES TIMEOUT 90)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/virtual-pointer-client.h"
    COMMAND "${WAYLAND_SCANNER}" client-header "${CMAKE_CURRENT_SOURCE_DIR}/virtual-pointer.xml"
            "${CMAKE_CURRENT_BINARY_DIR}/virtual-pointer-client.h"
    DEPENDS virtual-pointer.xml)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/virtual-pointer-protocol.c"
    COMMAND "${WAYLAND_SCANNER}" private-code "${CMAKE_CURRENT_SOURCE_DIR}/virtual-pointer.xml"
            "${CMAKE_CURRENT_BINARY_DIR}/virtual-pointer-protocol.c"
    DEPENDS virtual-pointer.xml)
add_executable(virtual-pointer-probe virtual-pointer-probe.c
    "${CMAKE_CURRENT_BINARY_DIR}/virtual-pointer-client.h"
    "${CMAKE_CURRENT_BINARY_DIR}/virtual-pointer-protocol.c")
target_include_directories(virtual-pointer-probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(virtual-pointer-probe PRIVATE PkgConfig::WAYLAND)

# Fake services run only on caller-created private buses.
add_executable(idle-bus-fixture idle-bus-fixture.cpp)
target_link_libraries(idle-bus-fixture PRIVATE Qt6::Core Qt6::DBus)
add_test(NAME idle-config
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-idle-config.py"
            "${AERIAL_LOCK_SOURCE}/idle/aerial-idle" "$<TARGET_FILE:companion-config-probe>")
add_test(NAME idle-shell-dir
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-idle-shell-dir.py"
            "${AERIAL_LOCK_SOURCE}")
add_test(NAME idle-logind
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-idle-logind.py"
            "${AERIAL_LOCK_SOURCE}/idle/aerial-idle"
            "$<TARGET_FILE:supervisor-test>"
            "$<TARGET_FILE:idle-bus-fixture>")
add_test(NAME fixture-publication
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test-fixture-publication.py")

pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
set(IDLE_INHIBIT_XML "${WAYLAND_PROTOCOLS_DIR}/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml")
set(IDLE_XDG_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
foreach(protocol IN ITEMS idle-inhibit idle-xdg-shell)
    if(protocol STREQUAL "idle-inhibit")
        set(xml "${IDLE_INHIBIT_XML}")
    else()
        set(xml "${IDLE_XDG_XML}")
    endif()
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${protocol}-client.h"
        COMMAND "${WAYLAND_SCANNER}" client-header "${xml}" "${CMAKE_CURRENT_BINARY_DIR}/${protocol}-client.h"
        DEPENDS "${xml}")
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${protocol}-protocol.c"
        COMMAND "${WAYLAND_SCANNER}" private-code "${xml}" "${CMAKE_CURRENT_BINARY_DIR}/${protocol}-protocol.c"
        DEPENDS "${xml}")
endforeach()
add_executable(idle-inhibit-probe idle-inhibit-probe.c
    "${CMAKE_CURRENT_BINARY_DIR}/idle-inhibit-client.h"
    "${CMAKE_CURRENT_BINARY_DIR}/idle-inhibit-protocol.c"
    "${CMAKE_CURRENT_BINARY_DIR}/idle-xdg-shell-client.h"
    "${CMAKE_CURRENT_BINARY_DIR}/idle-xdg-shell-protocol.c")
target_include_directories(idle-inhibit-probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(idle-inhibit-probe PRIVATE PkgConfig::WAYLAND)
add_executable(emergency-render-preview emergency-render-preview.c "${AERIAL_LOCK_SOURCE}/emergency/render.c"
    "${CMAKE_CURRENT_BINARY_DIR}/idle-xdg-shell-client.h" "${CMAKE_CURRENT_BINARY_DIR}/idle-xdg-shell-protocol.c")
target_include_directories(emergency-render-preview PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(emergency-render-preview PRIVATE PkgConfig::WAYLAND PkgConfig::XKBCOMMON)
