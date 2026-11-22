// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import InterActive
import InterActive.Foundation as IA

IAPane {
    id: root
    property string title: ""
    property string message: ""
    property int validation: IA.Presentation.None
    property string iconName: ""
    property url iconSource
    property string actionText: ""
    signal actionRequested()

    backgroundColor: validation === IA.Presentation.Invalid ? IA.Appearance.colors.errorSurface
        : validation === IA.Presentation.Warning ? IA.Appearance.colors.warningSurface
        : IA.Appearance.colors.canvas
    contentItem: RowLayout {
        spacing: IA.Appearance.metrics.spacingS
        IA.Icon {
            objectName: "iaBannerIcon"
            visible: root.iconName.length > 0 || root.iconSource.toString().length > 0
            Layout.preferredWidth: IA.Appearance.metrics.iconSize
            Layout.preferredHeight: IA.Appearance.metrics.iconSize
            Layout.alignment: Qt.AlignVCenter
            name: root.iconName
            source: IA.IconSource.resolve(root, root.iconSource)
            color: root.validation === IA.Presentation.Invalid ? IA.Appearance.colors.errorText
                : root.validation === IA.Presentation.Warning ? IA.Appearance.colors.warningText
                : root.foregroundColor
        }
        ColumnLayout {
            objectName: "iaBannerText"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: IA.Appearance.metrics.spacingXS
            IALabel {
                objectName: "iaBannerTitle"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: text.length > 0
                text: root.title
                textFormat: Text.PlainText
                wrapMode: Text.NoWrap
                fontWeight: IA.Appearance.typography.emphasisWeight
            }
            IALabel {
                objectName: "iaBannerMessage"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.message
                textFormat: Text.PlainText
                foregroundColor: root.validation === IA.Presentation.Invalid ? IA.Appearance.colors.errorText
                    : root.validation === IA.Presentation.Warning ? IA.Appearance.colors.warningText
                    : IA.Appearance.colors.secondaryText
                wrapMode: Text.Wrap
            }
        }
        IAButton {
            objectName: "iaBannerAction"
            visible: root.actionText.length > 0
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: implicitWidth
            Layout.alignment: Qt.AlignVCenter
            text: root.actionText
            onClicked: root.actionRequested()
        }
    }
}
