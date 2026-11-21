import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
C.AbstractButton {
    id: root
    implicitHeight: 54
    padding: 12
    background: Rectangle { color: Style.card; border.color: root.visualFocus ? Style.selection : Style.line; radius: 12 }
    contentItem: RowLayout {
        spacing: 8
        Glyph { name: "download" }
        Label { Layout.fillWidth: true; text: root.text; font.pixelSize: 13; font.weight: Font.Medium }
    }
}
