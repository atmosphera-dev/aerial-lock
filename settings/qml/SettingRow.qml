import QtQuick
import QtQuick.Layouts
import InterActive
import InterActive.Components
import InterActive.Foundation as IA

Rectangle {
    id: root
    property alias label: row.label
    property alias description: row.description
    property alias stacked: row.stacked
    property string error: ""
    default property alias control: row.control
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    implicitHeight: Math.max(body.implicitHeight + 2 * IA.Appearance.metrics.spacingS + 2, 46)
    color: IA.Appearance.colors.content
    ColumnLayout {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: IA.Appearance.metrics.spacingXS
        IASettingsRow {
            id: row
            Layout.fillWidth: true
            stacked: false
        }
        IALabel {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            visible: root.error.length > 0
            text: root.error
            textFormat: Text.PlainText
            foregroundColor: IA.Appearance.colors.errorText
            fontPixelSize: IA.Appearance.typography.supportingSize
            wrapMode: Text.Wrap
        }
    }
}
