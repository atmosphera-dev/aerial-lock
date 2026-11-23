import QtQuick
import QtQuick.Layouts
import InterActive
import InterActive.Foundation as IA

Rectangle {
    id: root
    property var pages: []
    property string current: "overview"
    signal selected(string key)
    color: IA.Appearance.colors.canvas

    ColumnLayout {
        objectName: "sidebarColumn"
        anchors.fill: parent
        anchors.margins: Insets.edge
        spacing: 2
        RowLayout {
            objectName: "appBand"
            Layout.fillWidth: true
            Layout.preferredHeight: IA.Appearance.metrics.controlHeight
            Layout.bottomMargin: IA.Appearance.metrics.spacingL
            spacing: IA.Appearance.metrics.spacingS
            Image {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                source: "../../assets/icons/64.png"
                sourceSize: Qt.size(56, 56)
            }
            IALabel {
                Layout.fillWidth: true
                text: "Aerial"
                fontPixelSize: 15
                fontWeight: IA.Appearance.typography.emphasisWeight
            }
        }
        Repeater {
            model: root.pages
            IAItemDelegate {
                required property var modelData
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                objectName: "navigation-" + modelData.key
                text: modelData.title
                icon.source: modelData.icon
                highlighted: root.current === modelData.key
                onClicked: root.selected(modelData.key)
            }
        }
        Item { Layout.fillHeight: true }
    }
}
