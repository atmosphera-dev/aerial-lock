import QtQuick
import QtQuick.Layouts
import InterActive
import InterActive.Components
import InterActive.Foundation as IA

PageScaffold {
    id: root
    required property var facts
    required property var notices
    required property var activity
    signal actionRequested(string actionId, string sourceId)
    title: "Overview"
    Repeater {
        model: root.notices
        IAStatusBanner {
            required property var modelData
            objectName: "notice-" + modelData.id
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            title: modelData.title
            message: modelData.message
            validation: modelData.severity === "error" ? IA.Presentation.Invalid
                : modelData.severity === "warning" ? IA.Presentation.Warning : IA.Presentation.None
            iconSource: Qt.resolvedUrl("icons/alert.svg")
            actionText: modelData.actionLabel
            onActionRequested: root.actionRequested(modelData.actionId, modelData.sourceId)
        }
    }
    StatusHeader {
        objectName: "overviewStatus"
        Layout.fillWidth: true
        facts: root.facts
        onActionRequested: actionId => root.actionRequested(actionId, "")
    }
    SettingsSection {
        objectName: "overviewVideos"
        Layout.fillWidth: true
        title: "Videos"
        SettingRow {
            label: "Ready to play"
            IALabel { objectName: "overviewReady"; text: root.facts.readyCountText }
        }
        SettingRow {
            label: "Next check"
            IALabel { objectName: "overviewNextCheck"; text: root.facts.nextCheckText }
        }
        SettingRow {
            label: "Storage"
            description: root.facts.storageDescription
            StorageMeter { objectName: "overviewStorage"; facts: root.facts }
        }
    }
    SettingsSection {
        Layout.fillWidth: true
        title: "Recent activity"
        objectName: "overviewActivity"
        SettingRow {
            visible: root.activity.length === 0
            label: "No recent activity"
            description: "Downloads and source checks will appear here."
        }
        Repeater {
            model: root.activity
            SettingRow {
                required property var modelData
                objectName: "activity-" + modelData.path
                label: modelData.label
                description: modelData.time
                IALabel {
                    visible: !modelData.error
                    text: modelData.state
                    foregroundColor: IA.Appearance.colors.secondaryText
                }
                IAButton {
                    objectName: "activityAction"
                    visible: modelData.error
                    text: modelData.actionLabel
                    onClicked: root.actionRequested(modelData.actionId, modelData.sourceId)
                }
            }
        }
    }
}
