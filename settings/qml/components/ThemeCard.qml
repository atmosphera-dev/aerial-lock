import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
C.AbstractButton {
    id: root
    property url preview
    implicitWidth: 200
    implicitHeight: (previewImage.visible ? availableWidth * 10 / 16 : 0) + 40 + topPadding + bottomPadding
    padding: 1
    background: Rectangle {
        radius: 12
        color: Style.card
        border.color: root.checked || root.visualFocus ? Style.accent : Style.line
        border.width: root.checked || root.visualFocus ? 2 : 1
    }
    contentItem: ColumnLayout {
        spacing: 0
        Image {
            id: previewImage
            Layout.fillWidth: true
            Layout.preferredHeight: width * 10 / 16
            source: root.preview
            fillMode: Image.PreserveAspectCrop
            visible: status === Image.Ready
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.minimumHeight: 40
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Label { Layout.fillWidth: true; text: root.text; font.weight: Font.Medium }
            Glyph { visible: root.checked; name: "checkcircle"; color: Style.accent }
        }
    }
}
