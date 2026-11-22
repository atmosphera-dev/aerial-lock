// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root
    required property ControlAppearance resolvedAppearance
    property bool controlEnabled: true
    property bool down: false
    property bool hovered: false
    property bool selected: false
    property bool active: true
    property bool focused: false
    property bool outlined: false
    property bool mirrored: false
    readonly property stateColors colors: frame.colors
    readonly property color foregroundColor: frame.foregroundColor
    readonly property bool marked: selected && controlEnabled
    readonly property real capExtent: Math.min(Math.max(0, width), colors.focusedBorderWidth + 2)
    readonly property real split: mirrored ? width - capExtent : capExtent
    objectName: "iaBackground"

    // Only the shared split clips inside the frame. The other edges extend
    // past the full native rectangle, preserving its outer antialias fringe.
    Item {
        id: trailing
        readonly property real direction: root.marked && root.mirrored ? -1 : 1
        x: root.marked ? root.split : 0
        y: -root.height
        width: root.marked ? 2 * root.width : root.width
        height: 3 * root.height
        clip: root.marked
        transform: Scale { xScale: trailing.direction }
        ControlBackground {
            id: frame
            objectName: "iaSegmentFrame"
            transform: Scale { xScale: trailing.direction }
            x: trailing.direction * -trailing.x
            y: -trailing.y
            width: root.width
            height: root.height
            resolvedAppearance: root.resolvedAppearance
            controlEnabled: root.controlEnabled
            down: root.down
            hovered: root.hovered
            selected: root.selected
            active: root.active
            focused: root.focused
            outlined: root.outlined
        }
    }
    Item {
        id: leading
        readonly property real direction: root.mirrored ? 1 : -1
        x: root.split
        y: -root.height
        width: 2 * root.width
        height: 3 * root.height
        visible: root.marked
        clip: true
        transform: Scale { xScale: leading.direction }
        ControlBackground {
            objectName: "iaSegmentCap"
            transform: Scale { xScale: leading.direction }
            x: leading.direction * -leading.x
            y: -leading.y
            width: root.width
            height: root.height
            resolvedAppearance: root.resolvedAppearance
            controlEnabled: root.controlEnabled
            down: root.down
            hovered: root.hovered
            selected: root.selected
            active: root.active
            focused: root.focused
            outlined: root.outlined
            color: Appearance.colors.accent
        }
    }
}
