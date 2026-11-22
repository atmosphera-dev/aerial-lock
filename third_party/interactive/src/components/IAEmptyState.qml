// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import InterActive
import InterActive.Foundation as IA

IAPane {
    id: root
    property string title: ""
    property string description: ""
    property string actionText: ""
    signal actionRequested()

    contentItem: ColumnLayout {
        spacing: IA.Appearance.metrics.spacingS
        IALabel {
            Layout.fillWidth: true
            text: root.title
            fontWeight: IA.Appearance.typography.emphasisWeight
        }
        IALabel {
            Layout.fillWidth: true
            text: root.description
            foregroundColor: IA.Appearance.colors.secondaryText
            wrapMode: Text.Wrap
        }
        IAButton {
            visible: root.actionText.length > 0
            text: root.actionText
            onClicked: root.actionRequested()
        }
    }
}
