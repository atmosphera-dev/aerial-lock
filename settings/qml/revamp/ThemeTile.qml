import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Templates as T
import InterActive
import InterActive.Foundation as IA

T.AbstractButton {
    id: root
    property url preview
    Accessible.role: Accessible.RadioButton
    Accessible.name: text
    Accessible.checkable: true
    Accessible.checked: checked
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitHeight: body.implicitHeight
    contentItem: ColumnLayout {
        id: body
        spacing: IA.Appearance.metrics.spacingS
        Item {
            id: thumbnail
            objectName: "themeThumbnail"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.round(width * 10 / 16)
            visible: image.status === Image.Ready
            Image {
                id: image
                source: root.preview
                sourceSize: Qt.size(Math.max(1, Math.ceil(thumbnail.width * root.Screen.devicePixelRatio)),
                    Math.max(1, Math.ceil(thumbnail.height * root.Screen.devicePixelRatio)))
                visible: false
            }
            Shape {
                anchors.fill: parent
                ShapePath {
                    strokeWidth: -1
                    fillItem: image
                    fillTransform: PlanarTransform.fromScale(thumbnail.width / Math.max(1, image.implicitWidth),
                        thumbnail.height / Math.max(1, image.implicitHeight))
                    PathRectangle {
                        width: thumbnail.width
                        height: thumbnail.height
                        radius: IA.Appearance.metrics.popupRadius
                    }
                }
            }
            Rectangle {
                anchors.fill: parent
                radius: IA.Appearance.metrics.popupRadius
                color: IA.Appearance.colors.transparent
                border.width: root.checked || root.visualFocus ? 2 : IA.Appearance.metrics.hairlineWidth
                border.color: root.checked || root.visualFocus ? IA.Appearance.colors.accent
                    : root.hovered ? IA.Appearance.colors.outline : IA.Appearance.colors.separator
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: IA.Appearance.metrics.spacingXS
            IALabel {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.text
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                fontWeight: root.checked ? IA.Appearance.typography.emphasisWeight : IA.Appearance.typography.controlWeight
                foregroundColor: root.visualFocus && !thumbnail.visible ? IA.Appearance.colors.accentText : IA.Appearance.colors.text
            }
            IA.Icon {
                objectName: "selectedThemeIcon"
                visible: root.checked
                source: Qt.resolvedUrl("icons/checkcircle.svg")
                color: IA.Appearance.colors.accentText
            }
        }
    }
}
