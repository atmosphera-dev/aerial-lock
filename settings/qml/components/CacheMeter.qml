import QtQuick
import QtQuick.Layouts
ColumnLayout {
    id: root
    required property string usage
    required property string free
    required property var segments
    required property var legend
    spacing: 10
    RowLayout {
        Layout.fillWidth: true
        Label { Layout.fillWidth: true; text: root.usage; font.pixelSize: 15; font.bold: true }
        Label { text: root.free; font.pixelSize: 13 }
    }
    Rectangle {
        id: meter
        Layout.fillWidth: true
        implicitHeight: 12
        radius: 6
        color: Style.line
        clip: true
        Row {
            Repeater {
                model: root.segments
                Rectangle {
                    required property var modelData
                    width: meter.width * modelData.fraction
                    height: meter.height
                    color: modelData.kind === "videos" ? Style.accent : modelData.kind === "partial" ? Style.selection : Style.muted
                }
            }
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 20
        Repeater {
            model: root.legend
            RowLayout {
                id: legendItem
                required property var modelData
                spacing: 7
                Rectangle { implicitWidth: 10; implicitHeight: 10; radius: 3; color: modelData.kind === "videos" ? Style.accent : modelData.kind === "partial" ? Style.selection : modelData.kind === "reserved" ? Style.muted : Style.line }
                Label { text: legendItem.modelData.label; font.pixelSize: 13 }
            }
        }
    }
}
