# Aerial's private deployment adapter. Upstream install/export rules stay intact.
function(aerial_add_interactive)
    set(INTERACTIVE_BUILD_PLAYGROUND OFF CACHE BOOL "" FORCE)
    set(INTERACTIVE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    # InterActive's project-scoped qml/bin variables cannot escape this function.
    add_subdirectory("${AERIAL_SETTINGS_SOURCE}/third_party/interactive"
        "${CMAKE_BINARY_DIR}/interactive")
    set(runtime_files)
    foreach(target IN ITEMS interactive_foundation interactive_controls interactive_components)
        qt_query_qml_module(${target} TARGET_PATH module_path PLUGIN_TARGET plugin
            QMLDIR qmldir TYPEINFO typeinfo QML_FILES qml_files
            QML_FILES_DEPLOY_PATHS qml_paths RESOURCES resources RESOURCES_DEPLOY_PATHS resource_paths)
        set(module_install "${AERIAL_INTERACTIVE_ROOT}/qml/${module_path}")
        file(RELATIVE_PATH plugin_to_lib "${module_install}" "${AERIAL_INTERACTIVE_ROOT}/lib")
        set_target_properties(${target} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/interactive/lib"
            INSTALL_RPATH "$ORIGIN")
        set_target_properties(${plugin} PROPERTIES INSTALL_RPATH "$ORIGIN/${plugin_to_lib}")
        install(TARGETS ${target} LIBRARY DESTINATION "${AERIAL_INTERACTIVE_ROOT}/lib" COMPONENT aerial-settings)
        install(TARGETS ${plugin} LIBRARY DESTINATION "${module_install}" COMPONENT aerial-settings)
        install(FILES "${qmldir}" "${typeinfo}" DESTINATION "${module_install}" COMPONENT aerial-settings)
        foreach(kind IN ITEMS qml resource)
            if(kind STREQUAL "qml")
                set(inputs "${qml_files}")
                set(outputs "${qml_paths}")
            else()
                set(inputs "${resources}")
                set(outputs "${resource_paths}")
            endif()
            list(LENGTH inputs count)
            if(count GREATER 0)
                math(EXPR last "${count} - 1")
                foreach(index RANGE ${last})
                    list(GET inputs ${index} input)
                    list(GET outputs ${index} output)
                    get_filename_component(directory "${output}" DIRECTORY)
                    install(FILES "${input}" DESTINATION "${module_install}/${directory}" COMPONENT aerial-settings)
                endforeach()
            endif()
        endforeach()
        # A module-local link also lets tooling resolve its backing library.
        set(link_script "${CMAKE_BINARY_DIR}/interactive/${target}-link.cmake")
        file(GENERATE OUTPUT "${link_script}" CONTENT
"set(link \"\$ENV{DESTDIR}${module_install}/$<TARGET_FILE_NAME:${target}>\")
file(REMOVE \"\${link}\")
file(CREATE_LINK \"${plugin_to_lib}/$<TARGET_FILE_NAME:${target}>\" \"\${link}\" SYMBOLIC RESULT result)
if(NOT result STREQUAL \"0\")
    message(FATAL_ERROR \"Private module link: \${result}\")
endif()
list(APPEND CMAKE_INSTALL_MANIFEST_FILES \"${module_install}/$<TARGET_FILE_NAME:${target}>\")
")
        install(SCRIPT "${link_script}" COMPONENT aerial-settings)
        list(APPEND runtime_files "$<TARGET_FILE:${target}>" "$<TARGET_FILE:${plugin}>" "${qmldir}" "${typeinfo}")
    endforeach()
    # Exact target-derived receipt inputs, not a recursive build-output glob.
    string(REPLACE ";" "\n" runtime_files "${runtime_files}")
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/interactive/runtime-files.txt" CONTENT "${runtime_files}\n")
    install(FILES "${AERIAL_SETTINGS_SOURCE}/third_party/interactive/COPYING"
        "${AERIAL_SETTINGS_SOURCE}/third_party/interactive/COPYING.LESSER"
        "${AERIAL_SETTINGS_SOURCE}/third_party/interactive/LICENSE.md"
        DESTINATION "${CMAKE_INSTALL_PREFIX}/share/licenses/aerial-lock/interactive" COMPONENT aerial-settings)
    install(FILES "${AERIAL_SETTINGS_SOURCE}/third_party/interactive/third_party/material-color-utilities/LICENSE"
        "${AERIAL_SETTINGS_SOURCE}/third_party/interactive/third_party/material-color-utilities/README.md"
        DESTINATION "${CMAKE_INSTALL_PREFIX}/share/licenses/aerial-lock/interactive/material-color-utilities" COMPONENT aerial-settings)
endfunction()
