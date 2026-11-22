// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    id: root
    required property ControlAppearance resolvedAppearance
    property bool controlEnabled: true
    property bool down: false
    property bool hovered: false
    property bool selected: false
    property bool active: true
    property bool focused: false
    property bool outlined: true
    property bool pressInset: false
    readonly property bool showsPressedFill: down && !pressInset
    readonly property real contentShift: pressInset && down && controlEnabled ? Appearance.metrics.pressContentShift : 0
    readonly property stateColors colors: resolvedAppearance.stateTable.colors(
        ControlState.of(controlEnabled, down, hovered, selected, active, pressInset))
    readonly property color foregroundColor: colors.foregroundColor
    objectName: "iaBackground"
    radius: resolvedAppearance.controlRadius
    color: colors.backgroundColor
    border.color: focused ? colors.focusedBorderColor : colors.borderColor
    border.width: focused ? colors.focusedBorderWidth : outlined ? Appearance.metrics.borderWidth : 0
    Rectangle {
        id: inset
        objectName: "iaPressInset"
        anchors.fill: parent
        anchors.margins: root.border.width
        radius: Math.max(0, root.radius - root.border.width)
        visible: root.pressInset && root.down && root.controlEnabled
        Accessible.ignored: true
        // Keep the named inset item for introspection, but no unused gradient/stops.
        property bool complete: false
        property Gradient pressGradient: null
        function updateGradient(): void {
            if (!complete)
                return;
            if (pressGradient) {
                gradient = null;
                pressGradient.destroy();
                pressGradient = null;
            }
            if (root.pressInset) {
                pressGradient = gradientComponent.createObject(inset) as Gradient;
                if (!pressGradient)
                    throw new Error(gradientComponent.errorString());
                gradient = pressGradient;
            }
        }
        Component.onCompleted: { complete = true; updateGradient(); }
    }
    onPressInsetChanged: inset.updateGradient()
    Component {
        id: gradientComponent
        Gradient {
            GradientStop { position: 0; color: Qt.rgba(0, 0, 0, Appearance.motion.pressInsetOpacity) }
            GradientStop {
                position: Math.min(0.5, Appearance.metrics.pressInsetDepth / Math.max(1, inset.height))
                color: Appearance.colors.transparent
            }
        }
    }
}
