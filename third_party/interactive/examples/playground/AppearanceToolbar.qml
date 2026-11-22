// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import InterActive
import InterActive.Foundation as IA

IAToolBar {
    id: root
    objectName: "appearanceToolbar"
    property PlaygroundState state: PlaygroundState
    signal accentRequested(Item anchor)
    signal appearanceRequested()
    contentItem: RowLayout {
        spacing: IA.Appearance.metrics.spacingS
        IAToolButton {
            objectName: "appearanceScheme"
            text: IA.Appearance.scheme === "dark" ? "Dark" : "Light"
            Accessible.name: text + " appearance; switch to " + (IA.Appearance.scheme === "dark" ? "light" : "dark")
            display: T.AbstractButton.IconOnly
            icon.source: IA.Appearance.scheme === "dark" ? "icons/moon.svg" : "icons/sun.svg"
            Layout.alignment: Qt.AlignVCenter
            onClicked: root.state.setAppearanceOption("scheme", IA.Appearance.scheme === "dark" ? "light" : "dark")
        }
        IAToolButton {
            id: accentButton
            objectName: "toolbarAccentButton"
            Accessible.name: "Choose accent color"
            Accessible.description: String(IA.Appearance.colors.accent).toUpperCase() + ". Choose a preset or Custom for the live dialog."
            Layout.alignment: Qt.AlignVCenter
            onClicked: root.accentRequested(accentButton)
            // ToolButton's padding and the centered icon-sized slot own equal
            // clearance; the swatch never expands to fill the content area.
            contentItem: Item {
                implicitWidth: IA.Appearance.metrics.iconSize
                implicitHeight: IA.Appearance.metrics.iconSize
                Rectangle {
                    objectName: "toolbarAccentSwatch"
                    anchors.centerIn: parent
                    width: IA.Appearance.metrics.iconSize
                    height: width
                    radius: IA.Appearance.metrics.controlRadius
                    color: IA.Appearance.colors.accent
                    Accessible.ignored: true
                }
            }
            Keys.onReturnPressed: root.accentRequested(accentButton)
            Keys.onEnterPressed: root.accentRequested(accentButton)
        }
        IAToolButton {
            objectName: "appearanceOpen"
            text: "Appearance"
            Accessible.name: "Open Appearance window"
            display: T.AbstractButton.IconOnly
            icon.source: "icons/sliders.svg"
            Layout.alignment: Qt.AlignVCenter
            onClicked: root.appearanceRequested()
        }
        IALabel {
            objectName: "toolbarStatus"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter
            text: root.state.savedAccentKeys.length + " saved accents"
            foregroundColor: IA.Appearance.colors.secondaryText
            fontPixelSize: IA.Appearance.typography.supportingSize
            horizontalAlignment: Text.AlignRight
        }
    }
}
