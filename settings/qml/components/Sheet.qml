import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
C.Popup {
    id: root
    property string title
    property bool closable: true
    default property alias contents: body.data
    modal: true
    focus: true
    C.Overlay.modal: Rectangle { color: "#88000000" }
    anchors.centerIn: C.Overlay.overlay
    width: Math.min(500, parent.width - 32)
    padding: 0
    closePolicy: closable ? C.Popup.CloseOnEscape : C.Popup.NoAutoClose
    background: Rectangle { radius: 16; color: Style.window; border.color: Style.line }
    contentItem: ColumnLayout {
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 54
            color: Style.card
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Style.line }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 10
                Label { Layout.fillWidth: true; text: root.title; font.pixelSize: 17; font.bold: true }
                IconButton { visible: root.closable; glyph: "x"; text: "Close"; onClicked: root.close() }
            }
        }
        ColumnLayout { id: body; Layout.fillWidth: true; Layout.margins: 20; spacing: 14 }
    }
}
