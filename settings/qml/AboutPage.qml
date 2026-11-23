pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
import AerialLock.Settings
import InterActive
import InterActive.Foundation as IA

PageScaffold {
    id: root
    required property SettingsController controller
    readonly property var facts: controller.about
    readonly property var tray: controller.tray
    title: "About"

    RowLayout {
        Layout.fillWidth: true
        spacing: IA.Appearance.metrics.spacingL
        Image {
            objectName: "aboutIcon"
            Layout.preferredWidth: 56
            Layout.preferredHeight: 56
            source: "../assets/icons/128.png"
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            IALabel { text: "Aerial"; fontPixelSize: 16; fontWeight: IA.Appearance.typography.emphasisWeight }
            IALabel { text: "Lock screen with aerial videos" }
            IALabel {
                text: root.facts.release
                fontPixelSize: IA.Appearance.typography.supportingSize
                foregroundColor: IA.Appearance.colors.secondaryText
            }
        }
    }
    SettingsSection {
        Layout.fillWidth: true
        title: "This app"
        SettingRow {
            objectName: "aboutTrayRow"
            label: "Show tray icon"
            description: root.tray.note ?? "Status and quick actions in the panel, from login."
            error: root.tray.error ?? ""
            IASwitch {
                objectName: "about-tray"
                Accessible.name: "Show tray icon"
                enabled: !(root.tray.pending ?? false) && (root.tray.available || checked)
                checked: root.tray.on || root.tray.persistent
                onToggled: {
                    const value = checked;
                    checked = Qt.binding(() => root.tray.on || root.tray.persistent);
                    root.controller.showTray(value);
                }
            }
        }
    }
    SettingsSection {
        Layout.fillWidth: true
        title: "Service"
        description: "Downloads and settings keep working when this window is closed."
        SettingRow {
            objectName: "aboutServiceRow"
            label: "Background service"
            description: root.facts.serviceNote ? root.facts.serviceState + "\n" + root.facts.serviceNote : root.facts.serviceState
            error: root.facts.restartError
            IAButton {
                objectName: "aboutRestart"
                visible: root.facts.canRestart
                enabled: !root.facts.restarting
                text: enabled ? "Restart" : "Restarting..."
                onClicked: root.controller.dispatchAction("restart-service")
            }
        }
        PathRow { target: "config"; label: "Settings file" }
        PathRow { target: "cache"; label: "Video cache" }
    }
    SettingsSection {
        Layout.fillWidth: true
        title: "Credits"
        TargetRow {
            target: "legal"
            label: "Apple aerial videos"
            description: "Made by Apple and downloaded straight from Apple. Not affiliated with Apple."
            actionText: "Legal notes"
        }
        TargetRow {
            target: "docs"
            label: "Inspired by Aerial for macOS"
            description: "By John Coates, Guillaume Louel and contributors."
            actionText: "Documentation"
        }
    }
    component PathRow: Rectangle {
        id: pathRow
        required property string target
        required property string label
        readonly property string path: root.facts[target]
        readonly property string error: root.facts.errors[target] ?? ""
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: Math.max(pathBody.implicitHeight + 2 * IA.Appearance.metrics.spacingS + 2, 46)
        color: IA.Appearance.colors.content
        ColumnLayout {
            id: pathBody
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: IA.Appearance.metrics.spacingXS
            RowLayout {
                Layout.fillWidth: true
                spacing: IA.Appearance.metrics.spacingL
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: IA.Appearance.metrics.spacingXS
                    IALabel { text: pathRow.label }
                    IALabel {
                        id: pathText
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: pathRow.path || "Path unavailable"
                        textFormat: Text.PlainText
                        fontPixelSize: IA.Appearance.typography.supportingSize
                        foregroundColor: IA.Appearance.colors.secondaryText
                        elide: Text.ElideMiddle
                        HoverHandler { id: pathHover }
                        C.ToolTip.visible: pathText.truncated && pathHover.hovered
                        C.ToolTip.text: pathRow.path
                    }
                }
                IAButton {
                    objectName: "about-" + pathRow.target
                    visible: pathRow.path !== ""
                    icon.source: Qt.resolvedUrl("icons/folder.svg")
                    text: "Open folder"
                    onClicked: root.controller.openAboutTarget(pathRow.target)
                }
            }
            IALabel {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: pathRow.error !== ""
                text: pathRow.error
                textFormat: Text.PlainText
                foregroundColor: IA.Appearance.colors.errorText
                fontPixelSize: IA.Appearance.typography.supportingSize
                wrapMode: Text.Wrap
            }
        }
    }
    component TargetRow: SettingRow {
        id: row
        required property string target
        required property string actionText
        error: root.facts.errors[target] ?? ""
        IAButton {
            objectName: "about-" + row.target
            icon.source: Qt.resolvedUrl("icons/external.svg")
            text: row.actionText
            onClicked: root.controller.openAboutTarget(row.target)
        }
    }
}
