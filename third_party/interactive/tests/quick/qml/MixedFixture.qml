// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as C
import InterActive
import InterActive.Foundation as IA

// A neutral consumer, not a copy of the gallery's application state.
Item {
    id: fixture
    width: 640
    height: 780
    property bool rtl: false
    property string committedName: "Maya Chen"
    property string selectedRecord: "No record selected"
    property alias editor: editor
    property alias applyButton: applyButton
    property alias disabledButton: disabledButton
    property alias check: check
    property alias radioOne: radioOne
    property alias radioTwo: radioTwo
    property alias toggle: toggle
    property alias tabs: tabs
    property alias secondTab: secondTab
    property alias slider: slider
    property alias progress: progress
    property alias delegate: delegate
    property alias committedSummary: committedSummary
    property alias choiceSummary: choiceSummary
    property alias radioSummary: radioSummary
    property alias switchSummary: switchSummary
    property alias tabSummary: tabSummary
    property alias valueSummary: valueSummary
    property alias recordSummary: recordSummary

    LayoutMirroring.enabled: rtl
    LayoutMirroring.childrenInherit: true

    C.ButtonGroup { id: radioGroup }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: IA.Appearance.metrics.spacingL
        spacing: IA.Appearance.metrics.spacingS

        IALabel { text: "Local preferences" }

        RowLayout {
            Layout.fillWidth: true
            spacing: IA.Appearance.metrics.spacingS
            IATextField {
                id: editor
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredHeight: Math.max(editor.implicitHeight, applyButton.implicitHeight)
                text: "Maya Chen"
                selectByMouse: true
                Accessible.name: "Display name"
            }
            IAButton {
                id: applyButton
                Layout.preferredHeight: Math.max(editor.implicitHeight, applyButton.implicitHeight)
                text: "Apply"
                onClicked: fixture.committedName = editor.text
            }
        }
        IALabel {
            id: committedSummary
            text: "Saved: " + fixture.committedName
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        IAButton {
            id: disabledButton
            text: "Disabled apply"
            enabled: false
            onClicked: fixture.committedName = "Disabled action activated"
        }
        GridLayout {
            Layout.fillWidth: true
            columns: fixture.width < 600 ? 1 : 2
            IACheckBox { id: check; text: "Alerts" }
            IALabel { id: choiceSummary; text: check.checked ? "Alerts enabled" : "Alerts disabled" }
        }
        ColumnLayout {
            Layout.fillWidth: true
            RowLayout {
                IARadioButton { id: radioOne; text: "Quiet"; checked: true; C.ButtonGroup.group: radioGroup }
                IARadioButton { id: radioTwo; text: "All"; C.ButtonGroup.group: radioGroup }
            }
            IALabel { id: radioSummary; text: radioTwo.checked ? "All updates" : "Quiet updates" }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: fixture.width < 600 ? 1 : 2
            IASwitch { id: toggle; text: "Sync" }
            IALabel { id: switchSummary; text: toggle.checked ? "Sync enabled" : "Sync paused" }
        }
        IATabBar {
            id: tabs
            Layout.fillWidth: true
            IATabButton { text: "Overview" }
            IATabButton { id: secondTab; text: "Details" }
        }
        IALabel { id: tabSummary; text: tabs.currentIndex === 0 ? "Overview preview" : "Details preview" }
        IASlider {
            id: slider
            Layout.fillWidth: true
            from: 0
            to: 100
            value: 37
            stepSize: 1
            Accessible.name: "Preview intensity"
        }
        IAProgressBar {
            id: progress
            Layout.fillWidth: true
            from: 0
            to: 100
            value: slider.value
        }
        IALabel { id: valueSummary; text: "Intensity: " + Math.round(slider.value) + "%" }
        IAItemDelegate {
            id: delegate
            Layout.fillWidth: true
            text: "Blue category"
            onClicked: fixture.selectedRecord = "Blue category selected"
        }
        IALabel { id: recordSummary; text: fixture.selectedRecord }
        Item { Layout.fillHeight: true }
    }
}
