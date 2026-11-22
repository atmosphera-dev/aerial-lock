// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick

Rectangle {
    id: root
    required property color remainderColor
    required property color fillColor
    property real position: 0
    property bool horizontal: true
    property bool mirrored: false
    property bool fillVisible: true
    objectName: "iaTrack"
    radius: Math.min(width, height) / 2
    color: remainderColor
    Rectangle {
        objectName: "iaFill"
        visible: root.fillVisible
        width: root.horizontal ? root.width * root.position : root.width
        height: root.horizontal ? root.height : root.height * root.position
        x: root.horizontal && root.mirrored ? root.width - width : 0
        y: root.horizontal ? 0 : root.height - height
        radius: root.radius
        color: root.fillColor
    }
}
