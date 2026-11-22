# SPDX-License-Identifier: LGPL-3.0-or-later
qt_add_executable(interactive_quick_tests runner.cpp ../shared/TestTraceSetup.h ../shared/TestTraceSetup.cpp)
set_target_properties(interactive_quick_tests PROPERTIES OUTPUT_NAME interactive-quick-tests)
target_include_directories(interactive_quick_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../shared")
target_link_libraries(interactive_quick_tests PRIVATE
    InterActive::Foundation InterActive::Controls InterActive::Components Qt6::QuickTest Qt6::Quick Qt6::Gui Qt6::Test)
qt_add_qml_module(interactive_quick_tests
    URI InterActive.Tests
    DEPENDENCIES QtQuick
    SOURCES TestProbe.h TestProbe.cpp DocumentProbe.h DocumentProbe.cpp
    QML_FILES
        qml/MixedFixture.qml
        qml/PopupFixture.qml
        qml/tst_controls.qml
        qml/tst_combo_box.qml
        qml/tst_combo_box_capture.qml
        qml/tst_geometry.qml
        qml/tst_choice_alignment.qml
        qml/tst_segmented.qml
        qml/tst_segment_marker.qml
        qml/tst_color_well.qml
        qml/tst_color_picker.qml
        qml/tst_color_picker_capture.qml
        qml/tst_color_well_capture.qml
        qml/tst_popups.qml
        qml/tst_dialog_header.qml
        qml/tst_status_banner.qml
        qml/tst_popup_direction.qml
        qml/tst_dialogbuttonbox.qml
        qml/tst_icons.qml
        qml/tst_states.qml
        qml/tst_capture.qml
        qml/tst_solid.qml
        qml/PaletteStateFixture.qml
        qml/tst_palette_state.qml
        qml/LongDocumentFixture.qml
        qml/tst_long_document.qml
        qml/iconconsumer/IconConsumer.qml
    RESOURCES qml/iconconsumer/icons/consumer-mark.svg
)
add_test(NAME interactive.qt-version COMMAND interactive_quick_tests --qt-version)
add_test(NAME interactive.controls COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_controls.qml")
add_test(NAME interactive.combo-box COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_combo_box.qml" -maxwarnings 0)
set_tests_properties(interactive.combo-box PROPERTIES LABELS interactive-overview TIMEOUT 180 RUN_SERIAL TRUE)
add_test(NAME interactive.combo-box-capture COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_combo_box_capture.qml" -maxwarnings 0)
set_tests_properties(interactive.combo-box-capture PROPERTIES LABELS interactive-combo-box-capture TIMEOUT 180 RUN_SERIAL TRUE)
add_test(NAME interactive.geometry COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_geometry.qml")
add_test(NAME interactive.choice-alignment COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_choice_alignment.qml" -maxwarnings 0)
set_tests_properties(interactive.choice-alignment PROPERTIES LABELS interactive-overview TIMEOUT 180 RUN_SERIAL TRUE)
add_test(NAME interactive.segmented COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_segmented.qml")
add_test(NAME interactive.segment-marker COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_segment_marker.qml" -maxwarnings 0)
set_tests_properties(interactive.segment-marker PROPERTIES LABELS interactive-overview TIMEOUT 180 RUN_SERIAL TRUE)
add_test(NAME interactive.color-well COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_color_well.qml")
add_test(NAME interactive.color-picker COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_color_picker.qml" -maxwarnings 0)
set_tests_properties(interactive.color-picker PROPERTIES LABELS interactive-overview TIMEOUT 180 RUN_SERIAL TRUE)
add_test(NAME interactive.color-picker-capture COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_color_picker_capture.qml" -maxwarnings 0)
set_tests_properties(interactive.color-picker-capture PROPERTIES LABELS interactive-color-picker-capture TIMEOUT 300 RUN_SERIAL TRUE)
set_tests_properties(interactive.color-well PROPERTIES LABELS interactive-overview TIMEOUT 180 RUN_SERIAL TRUE)
add_test(NAME interactive.color-well-capture COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_color_well_capture.qml")
set_tests_properties(interactive.color-well-capture PROPERTIES LABELS interactive-color-well-capture TIMEOUT 180 RUN_SERIAL TRUE)
add_test(NAME interactive.popups COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_popups.qml")
set_tests_properties(interactive.popups PROPERTIES RUN_SERIAL TRUE)
add_test(NAME interactive.dialog-header COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_dialog_header.qml" -maxwarnings 0)
set_tests_properties(interactive.dialog-header PROPERTIES RUN_SERIAL TRUE TIMEOUT 180 LABELS interactive-overview)
add_test(NAME interactive.status-banner COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_status_banner.qml" -maxwarnings 0)
set_tests_properties(interactive.status-banner PROPERTIES RUN_SERIAL TRUE TIMEOUT 180 LABELS interactive-overview)
add_test(NAME interactive.popup-direction COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_popup_direction.qml" -maxwarnings 0)
set_tests_properties(interactive.popup-direction PROPERTIES LABELS interactive-overview TIMEOUT 180 RUN_SERIAL TRUE)
set_tests_properties(interactive.popup-direction PROPERTIES
    FAIL_REGULAR_EXPRESSION "items in the process of being created at engine destruction")
add_test(NAME interactive.dialogbuttonbox COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_dialogbuttonbox.qml")
set_tests_properties(interactive.dialogbuttonbox PROPERTIES RUN_SERIAL TRUE)
add_test(NAME interactive.icons COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_icons.qml")
add_test(NAME interactive.states COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_states.qml")
add_test(NAME interactive.solid COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_solid.qml")
add_test(NAME interactive.palette-state COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_palette_state.qml")
add_test(NAME interactive.long-document COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_long_document.qml")
set_tests_properties(interactive.qt-version interactive.controls interactive.geometry interactive.segmented interactive.popups interactive.dialogbuttonbox interactive.icons interactive.states interactive.solid interactive.palette-state interactive.long-document
    PROPERTIES LABELS interactive-overview TIMEOUT 180)
add_test(NAME interactive.capture COMMAND interactive_quick_tests
    -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_capture.qml")
set_tests_properties(interactive.capture PROPERTIES LABELS interactive-capture TIMEOUT 600)
target_compile_definitions(interactive_quick_tests PRIVATE
    IA_EXPECTED_QT_VERSION="${Qt6_VERSION}")

if(INTERACTIVE_BUILD_PLAYGROUND)
    target_compile_definitions(interactive_quick_tests PRIVATE IA_TEST_PLAYGROUND=1)
    target_link_libraries(interactive_quick_tests PRIVATE interactive_playground_scene InterActive::Components)
    qt_target_qml_sources(interactive_quick_tests QML_FILES qml/tst_playground.qml qml/tst_appearance_capture.qml)
    add_test(NAME interactive.playground COMMAND interactive_quick_tests
        -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_playground.qml")
    set_tests_properties(interactive.playground PROPERTIES LABELS interactive-overview TIMEOUT 180)
    set_tests_properties(interactive.playground PROPERTIES RUN_SERIAL TRUE)
    add_test(NAME interactive.appearance-capture COMMAND interactive_quick_tests
        -input "${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_appearance_capture.qml")
    set_tests_properties(interactive.appearance-capture PROPERTIES LABELS interactive-appearance-capture TIMEOUT 180 RUN_SERIAL TRUE)
endif()
