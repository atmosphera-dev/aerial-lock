// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import InterActive
import InterActive.Foundation as IA

Item {
    id: root
    default property alias editor: editorLane.data
    property string label: ""
    property string description: ""
    property string message: ""
    property int validation: IA.Presentation.None

    implicitWidth: layout.implicitWidth
    implicitHeight: layout.implicitHeight
    data: ColumnLayout {
        id: layout
        width: root.width
        spacing: IA.Appearance.metrics.spacingXS
        IALabel {
            Layout.fillWidth: true
            text: root.label
            fontWeight: IA.Appearance.typography.emphasisWeight
        }
        RowLayout {
            id: editorLane
            Layout.fillWidth: true
            spacing: IA.Appearance.metrics.spacingS
        }
        IALabel {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.message.length > 0 ? root.message : root.description
            fontPixelSize: IA.Appearance.typography.supportingSize
            foregroundColor: root.validation === IA.Presentation.Invalid ? IA.Appearance.colors.errorText
                : root.validation === IA.Presentation.Warning ? IA.Appearance.colors.warningText
                : IA.Appearance.colors.secondaryText
            wrapMode: Text.Wrap
        }
    }
}
