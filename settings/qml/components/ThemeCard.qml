import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
import QtQuick.Shapes
C.AbstractButton {
    id: root
    property url preview
    implicitWidth: 200
    implicitHeight: (previewImage.status === Image.Ready ? availableWidth * 10 / 16 : 0) + 40 + topPadding + bottomPadding
    padding: 1
    background: Rectangle {
        radius: 12
        color: Style.card
        border.color: root.checked || root.visualFocus ? Style.accent : Style.line
        border.width: root.checked || root.visualFocus ? 2 : 1
    }
    contentItem: ColumnLayout {
        spacing: 0
        Item {
            id: picture
            Layout.fillWidth: true
            Layout.preferredHeight: width * 10 / 16
            visible: previewImage.status === Image.Ready
            Image {
                id: previewImage
                source: root.preview
                sourceSize: Qt.size(Math.max(1, Math.ceil(root.availableWidth * root.Screen.devicePixelRatio)), Math.max(1, Math.ceil(root.availableWidth * 10 / 16 * root.Screen.devicePixelRatio)))
                visible: false
            }
            Shape {
                anchors.fill: parent
                ShapePath {
                    strokeWidth: -1
                    fillItem: previewImage
                    fillTransform: PlanarTransform.fromScale(picture.width / Math.max(1, previewImage.implicitWidth), picture.height / Math.max(1, previewImage.implicitHeight))
                    PathRectangle { width: picture.width; height: picture.height; topLeftRadius: 11; topRightRadius: 11; bottomLeftRadius: 0; bottomRightRadius: 0 }
                }
            }
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
