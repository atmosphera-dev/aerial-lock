import QtQuick
import QtQuick.Layouts
ColumnLayout {
    id: root
    property string title
    property string description
    property string observation
    default property alias contents: body.data
    spacing: 10
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        Label { Layout.fillWidth: true; text: root.title; font.pixelSize: 16; font.bold: true }
        Flow {
            id: information
            Layout.fillWidth: true
            spacing: 4
            visible: root.description.length > 0 || root.observation.length > 0
            Label { width: Math.min(implicitWidth, information.width); visible: text.length > 0; text: root.description; font.pixelSize: 13; wrapMode: Text.WordWrap }
            Label { width: Math.min(implicitWidth, information.width); visible: text.length > 0; text: root.observation; font.pixelSize: 13; color: Style.muted; wrapMode: Text.WordWrap }
        }
    }
    ColumnLayout { id: body; Layout.fillWidth: true; spacing: 0 }
}
