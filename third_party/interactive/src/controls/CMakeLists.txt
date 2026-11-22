# SPDX-License-Identifier: LGPL-3.0-or-later
qt_add_qml_module(interactive_controls SHARED
    URI InterActive
    VERSION 1.0
    DEPENDENCIES QtQuick QtQuick.Shapes TARGET interactive_foundation
    SOURCES DialogButtonBoxLayout.h DialogButtonBoxLayout.cpp
    QML_FILES
        IAButton.qml IAToolButton.qml IALabel.qml IATextField.qml IAComboBox.qml IAColorWell.qml
        IAColorPlane.qml
        IACheckBox.qml IARadioButton.qml IASwitch.qml
        IATabBar.qml IATabButton.qml IASlider.qml IAProgressBar.qml
        IASegmentedControl.qml IASegmentedButton.qml
        IAItemDelegate.qml IAScrollBar.qml IAScrollView.qml
        IAPane.qml IAFrame.qml IAToolBar.qml IAToolSeparator.qml
        IAMenu.qml IAMenuItem.qml IAMenuSeparator.qml
        IAPopup.qml IADialog.qml IADialogButtonBox.qml IAToolTip.qml
)
add_library(InterActive::Controls ALIAS interactive_controls)
set_target_properties(interactive_controls PROPERTIES EXPORT_NAME Controls)
target_link_libraries(interactive_controls PUBLIC Qt6::Quick PRIVATE InterActive::Foundation)
