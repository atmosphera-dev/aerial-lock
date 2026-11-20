import QtQuick
import QtQuick.Controls as C
C.Switch {
    id: root
    property bool observed: false
    signal requested(bool value)
    checked: observed
    onToggled: {
        const value = checked;
        checked = Qt.binding(() => root.observed);
        requested(value);
    }
    implicitWidth: 40
    implicitHeight: 34
    padding: 0
    spacing: 0
    indicator: Rectangle {
        implicitWidth: 40
        implicitHeight: 22
        anchors.verticalCenter: parent.verticalCenter
        radius: 11
        color: root.checked ? Style.accent : "transparent"
        border.width: 1.5
        border.color: root.checked ? Style.accent : Style.muted
        Row {
            anchors.fill: parent
            anchors.margins: 3
            layoutDirection: root.checked ? Qt.RightToLeft : Qt.LeftToRight
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: root.checked ? 15 : 13
                height: width
                radius: width / 2
                color: root.checked ? Style.accentText : Style.muted
            }
        }
    }
    contentItem: Item {}
    background: Rectangle {
        radius: 6
        color: "transparent"
        border.width: root.visualFocus ? 2 : 0
        border.color: Style.selection
    }
    Accessible.name: text
}
