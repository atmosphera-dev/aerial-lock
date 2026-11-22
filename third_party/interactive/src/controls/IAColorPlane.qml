// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QtQuick.Shapes
import InterActive.Foundation as IA

T.Control {
    id: control
    property real hue: 0
    property real saturation: 0
    property real lightness: 0
    readonly property bool pressed: pointer.active || drag.active
    signal positionEdited(real saturation, real lightness)
    property alias borderColor: appearance.borderColor
    property alias focusColor: appearance.focusColor
    property alias controlRadius: appearance.controlRadius
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    function resetAppearance(): void { appearance.resetOverrides(); }
    readonly property IA.ControlAppearance _appearance: IA.ControlAppearance {
        id: appearance
        source: IA.Appearance
        family: "field"
    }
    implicitWidth: 256
    implicitHeight: implicitWidth
    // Match ControlBackground's normal outline and its resolved focus extent.
    // Neither the data geometry nor the cache size changes with focus.
    padding: Math.max(IA.Appearance.metrics.borderWidth, frame.colors.focusedBorderWidth)
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Grouping
    Accessible.name: "Saturation and lightness"
    Accessible.focusable: true
    Accessible.description: "Saturation " + Math.round(saturation * 100) + " percent, lightness "
        + Math.round(lightness * 100) + " percent"
    function editAt(position: point): void {
        if (availableWidth <= 0 || availableHeight <= 0) return;
        positionEdited(Math.max(0, Math.min(1, 1 - (position.y - topPadding) / availableHeight)),
                       Math.max(0, Math.min(1, (position.x - leftPadding) / availableWidth)));
    }
    Keys.onPressed: event => {
        const step = event.modifiers & Qt.ShiftModifier ? 0.1 : 0.01;
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            positionEdited(saturation, Math.max(0, Math.min(1,
                lightness + (event.key === Qt.Key_Right ? step : -step))));
        } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            positionEdited(Math.max(0, Math.min(1,
                saturation + (event.key === Qt.Key_Up ? step : -step))), lightness);
        } else {
            event.accepted = false;
            return;
        }
        event.accepted = true;
    }
    contentItem: Item {
        id: viewport
        clip: true
        // Physical data axes do not mirror with the ordinary surrounding layout.
        LayoutMirroring.enabled: false
        Item {
            id: source
            anchors.fill: parent
            IA.ColorPlaneSurface {
                objectName: "iaColorPlaneSurface"
                anchors.fill: parent
                hue: control.hue
                Accessible.ignored: true
            }
            Rectangle {
                objectName: "iaColorPlaneMarker"
                x: control.lightness * parent.width - width / 2
                y: (1 - control.saturation) * parent.height - height / 2
                width: IA.Appearance.metrics.sliderThumbSize
                height: width
                radius: width / 2
                color: "transparent"
                border.color: "black"
                border.width: 2
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: width / 2
                    color: "transparent"
                    border.color: "white"
                    border.width: 2
                }
                Accessible.ignored: true
            }
        }
        ShaderEffectSource {
            id: texture
            objectName: "iaColorPlaneTexture"
            anchors.fill: parent
            sourceItem: source
            hideSource: true
            live: true
            visible: false
            Accessible.ignored: true
        }
        Shape {
            objectName: "iaColorPlaneShape"
            anchors.fill: parent
            // Qt's curve renderer antialiases the path locally, without MSAA.
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeWidth: -1
                fillItem: texture
                PathRectangle {
                    width: viewport.width
                    height: viewport.height
                    radius: Math.max(0, control.controlRadius - control.padding)
                }
            }
            Accessible.ignored: true
        }
    }
    background: IA.ControlBackground {
        id: frame
        objectName: "iaColorPlaneFrame"
        resolvedAppearance: appearance
        controlEnabled: control.enabled
        focused: control.visualFocus
    }
    PointHandler {
        id: pointer
        parent: control
        target: null
        acceptedButtons: Qt.LeftButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.TouchScreen | PointerDevice.Stylus
        onActiveChanged: {
            if (active) {
                control.forceActiveFocus(Qt.MouseFocusReason);
                control.editAt(point.position);
            }
        }
        onPointChanged: { if (active && !drag.active) control.editAt(point.position); }
    }
    DragHandler {
        id: drag
        parent: control
        target: null
        acceptedButtons: Qt.LeftButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.TouchScreen | PointerDevice.Stylus
        minimumPointCount: 1
        maximumPointCount: 1
        dragThreshold: 0
        grabPermissions: PointerHandler.CanTakeOverFromItems | PointerHandler.ApprovesCancellation
        onActiveChanged: { if (active) control.editAt(centroid.position); }
        onCentroidChanged: { if (active) control.editAt(centroid.position); }
    }
}
