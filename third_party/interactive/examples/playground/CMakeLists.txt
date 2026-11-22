# SPDX-License-Identifier: LGPL-3.0-or-later
qt_add_qml_module(interactive_playground_scene SHARED
    URI InterActive.Playground
    VERSION 1.0
    DEPENDENCIES
        TARGET interactive_foundation
        TARGET interactive_controls
        TARGET interactive_components
    SOURCES PlaygroundState.h PlaygroundState.cpp AppearanceTraceController.h AppearanceTraceController.cpp
        PaletteSampleWriter.h PaletteSampleWriter.cpp PaletteSampleHistory.h PaletteSampleHistory.cpp
    QML_FILES Main.qml FoundationsPage.qml AppearanceToolbar.qml AppearanceEditor.qml PaletteColorPicker.qml AccentPaletteMenu.qml Specimen.qml
    RESOURCES icons/sun.svg icons/moon.svg icons/sliders.svg
)
include(GenerateExportHeader)
generate_export_header(interactive_playground_scene)
target_include_directories(interactive_playground_scene PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(interactive_playground_scene PUBLIC
    Qt6::Quick InterActive::Foundation
    PRIVATE InterActive::Controls InterActive::Components)

qt_add_executable(interactive_playground main.cpp)
target_link_libraries(interactive_playground PRIVATE
    interactive_playground_scene InterActive::Foundation Qt6::Quick Qt6::Gui)
set_target_properties(interactive_playground PROPERTIES
    OUTPUT_NAME interactive-playground)
# The gallery and its generated plugin are deliberately not installed.
