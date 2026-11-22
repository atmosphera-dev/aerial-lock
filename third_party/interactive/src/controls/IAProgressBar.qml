// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.ProgressBar {
    id: control
    property alias backgroundColor: appearance.backgroundColor
    property alias fillColor: appearance.fillColor
    property alias foregroundColor: appearance.foregroundColor
    property alias disabledBackgroundColor: appearance.disabledBackgroundColor
    property alias disabledForegroundColor: appearance.disabledForegroundColor
    property alias controlRadius: appearance.controlRadius
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    function resetAppearance(): void { appearance.resetOverrides(); }
    IA.ControlAppearance {
        id: appearance
        source: IA.Appearance
        family: "progress"
        readonly property IA.stateColors colors: stateTable.colors(
            IA.ControlState.of(control.enabled, false, false, false, true, false))
    }
    implicitWidth: IA.Appearance.metrics.progressWidth
    implicitHeight: IA.Appearance.metrics.progressHeight + topPadding + bottomPadding
    contentItem: IA.ValueTrack {
        id: lane
        clip: true
        position: control.position
        mirrored: control.mirrored
        fillVisible: !control.indeterminate
        remainderColor: appearance.colors.trackColor
        fillColor: appearance.colors.fillColor
        radius: control.controlRadius
        Rectangle {
            id: busy
            property real phase: 0
            objectName: "iaBusySegment"
            visible: control.indeterminate
            width: Math.min(lane.width, IA.Appearance.metrics.busySize)
            height: lane.height
            radius: control.controlRadius
            color: appearance.colors.fillColor
            // A centered segment remains distinct from a zero or complete value without motion.
            x: IA.Appearance.reducedMotion || IA.Appearance.motion.busyDuration === 0 ? (lane.width - width) / 2
                : (control.mirrored ? 1 - phase : phase) * (lane.width + width) - width
            NumberAnimation {
                target: busy
                property: "phase"
                from: 0
                to: 1
                duration: IA.Appearance.motion.busyDuration
                running: control.indeterminate && control.visible && control.enabled
                         && !IA.Appearance.reducedMotion && IA.Appearance.motion.busyDuration > 0
                loops: Animation.Infinite
            }
        }
    }
}
