// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQml.Models
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.Control {
    id: control
    property color colorValue: IA.Appearance.colors.accent
    property string label: "Color"
    property var presets: []
    property bool customColors: true
    signal colorSelected(color value)
    signal defaultRequested()
    signal customColorRequested()
    property alias backgroundColor: appearance.backgroundColor
    property alias foregroundColor: appearance.foregroundColor
    property alias hoverBackgroundColor: appearance.hoverBackgroundColor
    property alias disabledBackgroundColor: appearance.disabledBackgroundColor
    property alias disabledForegroundColor: appearance.disabledForegroundColor
    property alias inactiveBackgroundColor: appearance.inactiveBackgroundColor
    property alias inactiveForegroundColor: appearance.inactiveForegroundColor
    property alias borderColor: appearance.borderColor
    property alias disabledBorderColor: appearance.disabledBorderColor
    property alias focusColor: appearance.focusColor
    property alias fontFamily: appearance.fontFamily
    property alias fontPixelSize: appearance.fontPixelSize
    property alias fontWeight: appearance.fontWeight
    property alias minimumHeight: appearance.minimumHeight
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    function resetAppearance(): void { appearance.resetOverrides(); }
    readonly property IA.ControlAppearance _appearance: IA.ControlAppearance {
        id: appearance
        source: IA.Appearance
        family: "button"
    }
    readonly property real _inset: IA.Appearance.metrics.spacingXS
    readonly property IA.stateColors _wheelColors: appearance.stateTable.colors(
        IA.ControlState.of(control.enabled, custom.down, custom.hovered, false, control.Window.active, true))
    Accessible.role: Accessible.Grouping
    Accessible.name: label
    focusPolicy: Qt.NoFocus
    implicitHeight: minimumHeight
    implicitWidth: 2 * implicitHeight
    padding: 0
    font: appearance.resolvedFont

    // Rotation is event-owned, not a binding overwritten by cancellation.
    function _updateRotation(): void {
        if (IA.Appearance.reducedMotion) {
            spin.enabled = false;
            // A write through the disabled Behavior synchronously cancels its
            // owned job. Calling stop() on its non-root animation is invalid.
            rainbow.rotation = 0;
        } else {
            spin.enabled = true;
            rainbow.rotation = custom.visible && custom.hovered ? 180 : 0;
        }
    }
    readonly property Connections _motion: Connections {
        target: IA.Appearance
        function onReducedMotionChanged(): void { control._updateRotation(); }
    }
    Component.onCompleted: _updateRotation()

    contentItem: Item {
        T.AbstractButton {
            id: sampleButton
            objectName: "iaColorWellSampleButton"
            anchors.fill: parent
            padding: control._inset
            hoverEnabled: true
            font: control.font
            Accessible.name: control.label + " presets"
            onClicked: quickPalette.visible ? quickPalette.close() : quickPalette.open()
            contentItem: Rectangle {
                objectName: "iaColorWellSample"
                radius: height / 2
                color: control.colorValue
                border.width: IA.Appearance.metrics.borderWidth
                border.color: sampleSurface.colors.borderColor
                Accessible.ignored: true
            }
            background: IA.ControlBackground {
                id: sampleSurface
                resolvedAppearance: appearance
                radius: height / 2
                pressInset: true
                controlEnabled: control.enabled
                down: sampleButton.down
                hovered: sampleButton.hovered
                active: control.Window.active
                focused: sampleButton.visualFocus
            }
        }
        T.AbstractButton {
            id: custom
            objectName: "iaColorWellCustomButton"
            anchors.top: parent.top
            anchors.topMargin: control._inset
            anchors.right: parent.right
            anchors.rightMargin: control._inset
            height: Math.max(0, parent.height - 2 * control._inset)
            width: height
            visible: control.customColors
            hoverEnabled: true
            padding: 0
            font: control.font
            Accessible.name: "Choose custom " + control.label.toLowerCase() + " color"
            Accessible.ignored: !visible
            containmentMask: QtObject {
                function contains(point: point): bool {
                    const radius = custom.width / 2;
                    const dx = point.x - radius, dy = point.y - radius;
                    return dx * dx + dy * dy <= radius * radius;
                }
            }
            onHoveredChanged: control._updateRotation()
            onVisibleChanged: control._updateRotation()
            onClicked: {
                quickPalette.close();
                control.customColorRequested();
            }
            contentItem: Item {
                Image {
                    id: rainbow
                    objectName: "iaColorWellRainbow"
                    anchors.fill: parent
                    anchors.margins: IA.Appearance.metrics.borderWidth
                    source: "qrc:/qt/qml/InterActive/Foundation/icons/color-wheel.svg"
                    sourceSize.width: width * Screen.devicePixelRatio
                    sourceSize.height: height * Screen.devicePixelRatio
                    fillMode: Image.PreserveAspectFit
                    Accessible.ignored: true
                    Behavior on rotation {
                        id: spin
                        NumberAnimation {
                            id: rotationAnimation
                            objectName: "iaColorWellRotation"
                            duration: IA.Appearance.motion.feedbackDuration
                        }
                    }
                }
                Rectangle {
                    objectName: "iaColorWellRim"
                    anchors.fill: parent
                    color: IA.Appearance.colors.transparent
                    radius: height / 2
                    border.color: custom.visualFocus ? control._wheelColors.focusedBorderColor
                        : control._wheelColors.foregroundColor
                    border.width: custom.visualFocus ? control._wheelColors.focusedBorderWidth
                        : IA.Appearance.metrics.borderWidth
                    Accessible.ignored: true
                }
            }
        }
    }
    readonly property IAMenu _palette: IAMenu {
        id: quickPalette
        objectName: "iaColorWellMenu"
        parent: control
        y: control.height
        fontFamily: control.fontFamily
        fontPixelSize: control.fontPixelSize
        fontWeight: control.fontWeight
        backgroundColor: control.backgroundColor
        foregroundColor: control.foregroundColor
        borderColor: control.borderColor
        disabledBorderColor: control.disabledBorderColor
        Item {
            implicitHeight: heading.implicitHeight + 2 * IA.Appearance.metrics.spacingS
            IALabel {
                id: heading
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: IA.Appearance.metrics.spacingS
                text: control.label
                foregroundColor: control.foregroundColor
                fontFamily: control.fontFamily
                fontPixelSize: control.fontPixelSize
                fontWeight: IA.Appearance.typography.emphasisWeight
                elide: Text.ElideRight
            }
        }
        Instantiator {
            model: control.presets
            delegate: IAMenuItem {
                required property var modelData
                objectName: "iaColorWellPreset_" + modelData.hex.slice(1).toLowerCase()
                text: modelData.name + " " + modelData.hex
                fontFamily: control.fontFamily
                fontPixelSize: control.fontPixelSize
                fontWeight: control.fontWeight
                foregroundColor: control.foregroundColor
                onTriggered: control.colorSelected(modelData.hex)
            }
            onObjectAdded: (index, object) => quickPalette.insertItem(index + 1, object)
            onObjectRemoved: (index, object) => quickPalette.removeItem(object)
        }
        IAMenuItem {
            objectName: "iaColorWellDefault"
            text: "Default"
            fontFamily: control.fontFamily
            fontPixelSize: control.fontPixelSize
            fontWeight: control.fontWeight
            foregroundColor: control.foregroundColor
            onTriggered: control.defaultRequested()
        }
    }
}
