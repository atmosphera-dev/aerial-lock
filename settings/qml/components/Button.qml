import QtQuick
import QtQuick.Controls as C
C.Button {
    id: root
    property bool primary: false
    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    implicitHeight: 34
    leftPadding: 14
    rightPadding: 14
    contentItem: Label {
        text: root.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.weight: Font.Medium
        color: root.primary ? Style.accentText : Style.text
    }
    background: Rectangle {
        color: root.primary ? Style.accent : "transparent"
        border.color: root.visualFocus ? Style.selection : root.primary ? Style.accent : Style.line
        border.width: root.visualFocus ? 2 : 1
        radius: 8
    }
}
