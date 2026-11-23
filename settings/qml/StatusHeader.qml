import QtQuick
import QtQuick.Layouts
import InterActive
import InterActive.Foundation as IA

ColumnLayout {
    id: root
    Layout.minimumWidth: 0
    required property var facts
    signal actionRequested(string actionId)
    spacing: IA.Appearance.metrics.spacingM
    RowLayout {
        Layout.fillWidth: true
        spacing: IA.Appearance.metrics.spacingL
        Rectangle {
            Layout.preferredWidth: 44
            Layout.preferredHeight: 44
            Layout.alignment: Qt.AlignTop
            radius: IA.Appearance.metrics.popupRadius
            color: IA.Appearance.colors.control
            IA.Icon {
                anchors.centerIn: parent
                width: 22
                height: 22
                source: Qt.resolvedUrl("icons/sky.svg")
                color: IA.Appearance.colors.accentText
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 2
            IALabel {
                objectName: "overviewHeadline"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.facts.headline
                fontPixelSize: 16
                fontWeight: IA.Appearance.typography.emphasisWeight
            }
            IALabel {
                objectName: "overviewDetail"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: text.length > 0
                text: root.facts.detail
                fontPixelSize: IA.Appearance.typography.supportingSize
                foregroundColor: IA.Appearance.colors.secondaryText
                wrapMode: Text.Wrap
            }
        }
    }
    IAProgressBar {
        objectName: "overviewProgress"
        Layout.fillWidth: true
        visible: root.facts.downloading
        indeterminate: !root.facts.progressKnown
        value: root.facts.progress
    }
    // Native layout reflows the two complete labels, never truncating actions.
    GridLayout {
        Layout.alignment: Qt.AlignLeft
        columns: primary.implicitWidth + secondary.implicitWidth + columnSpacing > root.width ? 1 : 2
        columnSpacing: IA.Appearance.metrics.spacingS
        rowSpacing: IA.Appearance.metrics.spacingS
        IAButton {
            id: primary
            objectName: "overviewPrimary"
            visible: root.facts.primaryActionId.length > 0
            text: root.facts.primaryActionLabel
            icon.source: Qt.resolvedUrl(root.facts.downloading ? "icons/x.svg" : "icons/download.svg")
            onClicked: root.actionRequested(root.facts.primaryActionId)
        }
        IAButton {
            id: secondary
            objectName: "overviewSecondary"
            visible: root.facts.secondaryActionId.length > 0
            text: root.facts.secondaryActionLabel
            icon.source: Qt.resolvedUrl(root.facts.secondaryActionId === "resume-downloads" ? "icons/play.svg" : "icons/pause.svg")
            onClicked: root.actionRequested(root.facts.secondaryActionId)
        }
    }
}
