// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick

// Shared visual lane; the Qt template remains the owner of all interaction.
Text {
    id: root
    required property ControlAppearance resolvedAppearance
    property int kind: ChoiceIndicator.Check
    property bool checked: false
    property bool partial: false
    property bool controlEnabled: true
    property bool down: false
    property bool hovered: false
    property bool active: true
    property bool focused: false
    property bool mirrored: false
    property int indicatorPosition: Presentation.Leading
    readonly property bool indicatorOnLeft: (indicatorPosition === Presentation.Leading) !== mirrored
    property real spacing: resolvedAppearance.spacing
    property real visualPosition: checked ? 1 : 0
    readonly property alias indicator: mark
    objectName: "iaLabel"
    LayoutMirroring.enabled: false
    leftPadding: indicatorOnLeft ? mark.width + (text.length ? spacing : 0) : 0
    rightPadding: !indicatorOnLeft ? mark.width + (text.length ? spacing : 0) : 0
    color: resolvedAppearance.stateTable.colors(
        ControlState.of(controlEnabled, false, false, false, true, false)).foregroundColor
    textFormat: Text.PlainText
    elide: Text.ElideRight
    horizontalAlignment: mirrored ? Text.AlignRight : Text.AlignLeft
    verticalAlignment: Text.AlignVCenter

    ChoiceIndicator {
        id: mark
        LayoutMirroring.enabled: !root.indicatorOnLeft
        resolvedAppearance: root.resolvedAppearance
        kind: root.kind
        checked: root.checked
        partial: root.partial
        controlEnabled: root.controlEnabled
        down: root.down
        hovered: root.hovered
        active: root.active
        focused: root.focused
        visualPosition: root.visualPosition
        anchors.left: root.left
        anchors.verticalCenter: root.verticalCenter
    }
}
