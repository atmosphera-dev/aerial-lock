import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
C.AbstractButton {
    id: root
    property bool inherited: false
    property color swatch: "black"
    implicitWidth: Math.max(156, valueLabel.implicitWidth + 74)
    implicitHeight: 34
    padding: 1
    background: Rectangle { color: Style.window; radius: 8; border.width: root.visualFocus ? 2 : 1; border.color: root.visualFocus ? Style.selection : Style.line }
    contentItem: RowLayout {
        spacing: 10
        Rectangle { Layout.leftMargin: 11; implicitWidth: 18; implicitHeight: 18; radius: 5; color: root.swatch; border.color: Style.line }
        Label { id: valueLabel; Layout.fillWidth: true; text: root.text; color: root.inherited ? Style.muted : Style.text }
        Item {
            Layout.preferredWidth: root.availableHeight
            Layout.fillHeight: true
            Glyph { objectName: "chevron"; anchors.centerIn: parent; width: 16; height: 16; name: "chevron" }
        }
    }
}
