# SPDX-License-Identifier: LGPL-3.0-or-later
qt_add_executable(interactive_perf tst_perf.cpp)
target_link_libraries(interactive_perf PRIVATE
    InterActive::Foundation InterActive::Controls Qt6::Test Qt6::Quick)
qt_add_qml_module(interactive_perf
    URI InterActive.Perf
    DEPENDENCIES QtQuick
    QML_FILES Scene.qml
)
add_test(NAME interactive.perf COMMAND interactive_perf)
set_tests_properties(interactive.perf PROPERTIES LABELS interactive-perf TIMEOUT 600)
