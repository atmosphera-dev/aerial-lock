import QtQuick
import QtQuick.Layouts
import InterActive.Components
import InterActive.Foundation as IA

Rectangle {
    id: root
    property alias label: row.label
    property alias description: row.description
    property alias stacked: row.stacked
    default property alias control: row.control
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    implicitHeight: Math.max(row.implicitHeight + 2 * IA.Appearance.metrics.spacingS + 2, 46)
    color: IA.Appearance.colors.content
    IASettingsRow {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        stacked: false
    }
}
