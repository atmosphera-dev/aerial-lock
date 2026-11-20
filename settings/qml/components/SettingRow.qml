import QtQuick
import QtQuick.Layouts
Item {
    id: root
    property string title
    property string description
    property string error
    property string glyph
    property bool separator: false
    property bool verticalSeparator: false
    property bool compact: false
    default property alias controls: slot.data
    implicitHeight: Math.max(compact ? 50 : 58, row.implicitHeight + (compact ? 16 : 24))
    Rectangle { width: parent.width; height: 1; color: Style.line; visible: root.separator }
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Style.line; visible: root.verticalSeparator }
    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.topMargin: root.compact ? 8 : 12
        anchors.bottomMargin: root.compact ? 8 : 12
        spacing: root.compact ? 12 : 16
        Glyph { visible: root.glyph.length > 0; name: root.glyph }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label { Layout.fillWidth: true; visible: text.length > 0; text: root.title; font.weight: Font.Medium }
            Label { Layout.fillWidth: true; visible: text.length > 0; text: root.description; font.pixelSize: 13; wrapMode: Text.WordWrap }
            Label { Layout.fillWidth: true; visible: text.length > 0; text: root.error; font.pixelSize: 13; wrapMode: Text.WordWrap }
        }
        RowLayout { id: slot; spacing: 10 }
    }
}
