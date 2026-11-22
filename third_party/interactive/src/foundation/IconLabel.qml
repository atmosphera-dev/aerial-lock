// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

Item {
    id: root
    property string text
    property string iconName
    property url iconSource
    property real iconWidth: Appearance.metrics.iconSize
    property real iconHeight: Appearance.metrics.iconSize
    property color iconColor: color
    property bool iconCache: true
    property int display: T.AbstractButton.TextBesideIcon
    property bool mirrored: false
    property real spacing: Appearance.metrics.spacing
    property real contentShift: 0
    property font font: Qt.font({ family: Appearance.typography.family,
                                  pixelSize: Appearance.typography.controlSize,
                                  weight: Appearance.typography.controlWeight })
    property color color: Appearance.colors.text
    property int horizontalAlignment: Text.AlignHCenter
    readonly property bool hasIcon: iconName.length > 0 || iconSource.toString().length > 0
    readonly property bool showsIcon: hasIcon && display !== T.AbstractButton.TextOnly
    readonly property bool showsText: text.length > 0 && display !== T.AbstractButton.IconOnly
    readonly property bool stacked: display === T.AbstractButton.TextUnderIcon

    // The caller supplies direction; do not mirror the explicitly arranged lanes twice.
    LayoutMirroring.enabled: false
    LayoutMirroring.childrenInherit: true

    implicitWidth: layout.implicitWidth
    implicitHeight: layout.implicitHeight
    baselineOffset: label.y + layout.y + label.baselineOffset
    transform: Translate { y: root.contentShift }

    GridLayout {
        id: layout
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(root.width, implicitWidth)
        x: root.horizontalAlignment === Text.AlignHCenter ? (root.width - width) / 2
           : root.horizontalAlignment === Text.AlignRight ? root.width - width : 0
        columns: root.stacked ? 1 : 2
        rowSpacing: root.spacing
        columnSpacing: root.spacing
        layoutDirection: root.mirrored ? Qt.RightToLeft : Qt.LeftToRight
        Icon {
            visible: root.showsIcon
            Layout.preferredWidth: root.iconWidth
            Layout.preferredHeight: root.iconHeight
            Layout.alignment: Qt.AlignCenter
            name: root.iconName
            source: root.iconSource
            color: root.iconColor
            cache: root.iconCache
        }
        Text {
            id: label
            objectName: "iaLabel"
            visible: root.showsText
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter
            text: root.text
            font: root.font
            color: root.color
            textFormat: Text.PlainText
            elide: Text.ElideRight
            horizontalAlignment: root.stacked ? Text.AlignHCenter
                                 : root.mirrored ? Text.AlignRight : Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
        }
    }
}
