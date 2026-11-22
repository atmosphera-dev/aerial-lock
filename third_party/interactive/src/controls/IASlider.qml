// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.Slider {
    id: control
    property alias backgroundColor: appearance.backgroundColor
    property alias fillColor: appearance.fillColor
    property alias foregroundColor: appearance.foregroundColor
    property alias hoverBackgroundColor: appearance.hoverBackgroundColor
    property alias pressedBackgroundColor: appearance.pressedBackgroundColor
    property alias disabledBackgroundColor: appearance.disabledBackgroundColor
    property alias disabledForegroundColor: appearance.disabledForegroundColor
    property alias borderColor: appearance.borderColor
    property alias disabledBorderColor: appearance.disabledBorderColor
    property alias focusColor: appearance.focusColor
    property alias controlRadius: appearance.controlRadius
    property alias minimumHeight: appearance.minimumHeight
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    function resetAppearance(): void { appearance.resetOverrides(); }
    IA.ControlAppearance {
        id: appearance
        source: IA.Appearance
        family: "slider"
        readonly property IA.stateColors colors: stateTable.colors(
            IA.ControlState.of(control.enabled, false, false, false, true, false))
    }
    implicitWidth: Math.max(horizontal ? IA.Appearance.metrics.sliderLength : minimumHeight,
                            implicitHandleWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(horizontal ? minimumHeight : IA.Appearance.metrics.sliderLength,
                             implicitHandleHeight + topPadding + bottomPadding)
    horizontalPadding: appearance.verticalPadding
    verticalPadding: appearance.verticalPadding
    hoverEnabled: true

    background: Item {
        id: lane
        x: control.leftPadding
        y: control.topPadding
        width: control.availableWidth
        height: control.availableHeight
        IA.ValueTrack {
            id: track
            anchors.centerIn: parent
            width: control.horizontal ? Math.max(0, lane.width - thumb.width) : IA.Appearance.metrics.trackHeight
            height: control.horizontal ? IA.Appearance.metrics.trackHeight : Math.max(0, lane.height - thumb.height)
            horizontal: control.horizontal
            mirrored: control.mirrored
            position: control.position
            remainderColor: appearance.colors.trackColor
            fillColor: appearance.colors.fillColor
        }
    }
    handle: IA.ControlBackground {
        id: thumb
        objectName: "iaThumb"
        resolvedAppearance: appearance
        color: colors.thumbColor
        implicitWidth: IA.Appearance.metrics.sliderThumbSize
        implicitHeight: IA.Appearance.metrics.sliderThumbSize
        radius: width / 2
        controlEnabled: control.enabled
        down: control.pressed
        hovered: control.hovered
        focused: control.visualFocus
        // visualPosition already accounts for RTL. Keep one structural center
        // in both orientations; only the normalized value displaces the thumb.
        LayoutMirroring.enabled: false
        anchors.centerIn: lane
        anchors.horizontalCenterOffset: control.horizontal ? (control.visualPosition - 0.5) * track.width : 0
        anchors.verticalCenterOffset: control.horizontal ? 0 : (control.visualPosition - 0.5) * track.height
    }
}
