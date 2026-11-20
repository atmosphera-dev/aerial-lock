import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
ColumnLayout {
    id: root
    property string title
    property alias text: input.text
    required property var edited
    spacing: 6
    Label { Layout.fillWidth: true; text: root.title; font.weight: Font.Medium }
    C.TextField {
        id: input
        objectName: "input"
        Layout.fillWidth: true
        implicitHeight: 34
        leftPadding: 12
        rightPadding: 12
        font.pixelSize: 14
        color: Style.text
        selectionColor: Style.selection
        selectedTextColor: Style.selectionText
        Accessible.name: root.title
        onTextEdited: root.edited(text)
        background: Rectangle { radius: 8; color: Style.card; border.color: input.activeFocus ? Style.selection : Style.line; border.width: input.activeFocus ? 2 : 1 }
    }
}
