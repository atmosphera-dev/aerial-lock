import QtQuick
import QtQuick.Layouts
import InterActive
import InterActive.Foundation as IA

ColumnLayout {
    id: root
    Layout.minimumWidth: 0
    property string title: ""
    property string description: ""
    property string observation: ""
    default property alias rows: list.data
    spacing: IA.Appearance.metrics.spacingXS
    IALabel {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        text: root.title
        fontWeight: IA.Appearance.typography.emphasisWeight
    }
    IALabel {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        visible: text.length > 0
        text: root.description
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        fontPixelSize: IA.Appearance.typography.supportingSize
        foregroundColor: IA.Appearance.colors.secondaryText
    }
    IALabel {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        visible: text.length > 0
        text: root.observation
        fontPixelSize: IA.Appearance.typography.supportingSize
        foregroundColor: IA.Appearance.colors.secondaryText
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: IA.Appearance.metrics.spacingXS
        implicitHeight: list.implicitHeight + 2 * IA.Appearance.metrics.hairlineWidth
        color: IA.Appearance.colors.separator
        ColumnLayout {
            id: list
            y: IA.Appearance.metrics.hairlineWidth
            width: parent.width
            spacing: IA.Appearance.metrics.hairlineWidth
        }
    }
}
