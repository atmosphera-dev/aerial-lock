// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import InterActive
import InterActive.Foundation as IA

Item {
    id: root
    default property alias content: specimens.data
    property string title: ""
    property string description: ""

    implicitWidth: layout.implicitWidth
    implicitHeight: layout.implicitHeight
    data: ColumnLayout {
        id: layout
        width: root.width
        spacing: IA.Appearance.metrics.spacingM
        IA.Separator {
            Layout.fillWidth: true
        }
        IALabel {
            Layout.fillWidth: true
            text: root.title
            fontPixelSize: IA.Appearance.typography.headingSize
            fontWeight: IA.Appearance.typography.emphasisWeight
            wrapMode: Text.Wrap
        }
        IALabel {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.description
            foregroundColor: IA.Appearance.colors.secondaryText
            wrapMode: Text.Wrap
        }
        ColumnLayout {
            id: specimens
            Layout.fillWidth: true
            spacing: IA.Appearance.metrics.spacingM
        }
    }
}
