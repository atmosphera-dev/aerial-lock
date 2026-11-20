import QtQuick
import QtQuick.Controls as C
C.AbstractButton {
    id: root
    property string glyph: "info"
    implicitWidth: 34
    implicitHeight: 34
    contentItem: Item { Glyph { anchors.centerIn: parent; name: root.glyph } }
    background: Rectangle {
        color: root.hovered ? Style.card : "transparent"
        radius: 8
        border.width: root.visualFocus ? 2 : 0
        border.color: Style.selection
    }
    Accessible.name: text
    C.ToolTip {
        visible: root.hovered
        text: root.text
        contentItem: Label { text: root.text }
    }
}
