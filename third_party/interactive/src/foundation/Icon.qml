// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick

Item {
    id: root
    property string name
    property url source
    property color color: Appearance.colors.text
    property bool cache: true
    property bool mirrored: false
    readonly property bool available: themed.available || image.status === Image.Ready
    implicitWidth: Appearance.metrics.iconSize
    implicitHeight: Appearance.metrics.iconSize

    ThemeIconItem {
        id: themed
        anchors.fill: parent
        name: root.name
        color: root.color
        visible: available
        transform: Scale {
            origin.x: root.width / 2
            xScale: root.mirrored ? -1 : 1
        }
    }
    Image {
        id: image
        anchors.fill: parent
        source: themed.available ? "" : root.source
        sourceSize.width: root.width * Screen.devicePixelRatio
        sourceSize.height: root.height * Screen.devicePixelRatio
        fillMode: Image.PreserveAspectFit
        cache: root.cache
        mirror: root.mirrored
        visible: !themed.available && root.color.a === 0
    }
    ShaderEffect {
        anchors.centerIn: parent
        width: image.paintedWidth
        height: image.paintedHeight
        visible: !themed.available && image.status === Image.Ready && root.color.a !== 0
        property var source: image
        property color tint: root.color
        property real mirrorSource: root.mirrored ? 1 : 0
        fragmentShader: "qrc:/qt/qml/InterActive/Foundation/shaders/icon-tint.frag.qsb"
    }
}
