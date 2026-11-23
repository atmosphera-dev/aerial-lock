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
        spacing: IA.Appearance.metrics.spacingM
        IA.Icon {
            objectName: "iaBannerIcon"
            visible: root.iconName.length > 0 || root.iconSource.toString().length > 0
            Layout.preferredWidth: IA.Appearance.metrics.iconSize
            Layout.preferredHeight: bannerTitle.visible ? bannerTitle.implicitHeight : IA.Appearance.metrics.iconSize
            Layout.alignment: Qt.AlignTop
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
            spacing: 2
            IALabel {
                id: bannerTitle
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
                fontPixelSize: IA.Appearance.typography.supportingSize
                foregroundColor: IA.Appearance.colors.secondaryText
                wrapMode: Text.Wrap
            }
        }
        IAButton {
            objectName: "iaBannerAction"
            visible: root.actionText.length > 0
            Layout.minimumWidth: implicitWidth
            Layout.alignment: Qt.AlignVCenter
            text: root.actionText
            onClicked: root.actionRequested()
        }
    }
}
