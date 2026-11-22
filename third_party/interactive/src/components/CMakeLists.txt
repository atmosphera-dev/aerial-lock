# SPDX-License-Identifier: LGPL-3.0-or-later
qt_add_qml_module(interactive_components SHARED
    URI InterActive.Components
    VERSION 1.0
    DEPENDENCIES TARGET interactive_foundation TARGET interactive_controls
    SOURCES ColorEditState.h ColorEditState.cpp
    QML_FILES
        IAColorEditor.qml
        IAColorDialog.qml
        IAFieldRow.qml
        IASettingsRow.qml
        IAStatusBanner.qml
        IAEmptyState.qml
)
add_library(InterActive::Components ALIAS interactive_components)
set_target_properties(interactive_components PROPERTIES EXPORT_NAME Components)
target_link_libraries(interactive_components PUBLIC Qt6::Quick
    PRIVATE InterActive::Controls InterActive::Foundation)
