// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import InterActive
import InterActive.Foundation as IA

Item {
    id: root
    default property alias control: controlLane.data
    property string label: ""
    property string description: ""
    property bool stacked: width < 300

    implicitWidth: layout.implicitWidth
    implicitHeight: layout.implicitHeight
    data: GridLayout {
        id: layout
        width: root.width
        columns: root.stacked ? 1 : 2
        columnSpacing: IA.Appearance.metrics.spacingL
        rowSpacing: IA.Appearance.metrics.spacingXS
        ColumnLayout {
            Layout.fillWidth: true
            spacing: IA.Appearance.metrics.spacingXS
            IALabel {
                Layout.fillWidth: true
                text: root.label
            }
            IALabel {
                Layout.fillWidth: true
                visible: text.length > 0
                text: root.description
                fontPixelSize: IA.Appearance.typography.supportingSize
                foregroundColor: IA.Appearance.colors.secondaryText
                wrapMode: Text.Wrap
            }
        }
        RowLayout {
            id: controlLane
            Layout.alignment: Qt.AlignVCenter
            spacing: IA.Appearance.metrics.spacingS
        }
    }
}
