import QtQuick
import QtQuick.Layouts
import AerialLock.Settings
import "components" as UI
UI.Page {
    id: root
    required property SettingsController controller
    title: "About"
    RowLayout {
        Layout.fillWidth: true
        spacing: 18
        UI.AppIcon { Layout.preferredWidth: 60; Layout.preferredHeight: 60 }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            UI.Label { text: "Aerial"; font.pixelSize: 20; font.bold: true }
            UI.Label { Layout.fillWidth: true; text: "Lock screen with aerial videos"; font.pixelSize: 13 }
            UI.Label { Layout.fillWidth: true; text: root.controller.about.version; font.pixelSize: 13; color: UI.Style.muted }
        }
    }
    UI.Section {
        Layout.fillWidth: true
        title: "Tray icon"
        description: root.controller.tray.message
        UI.Group {
            Layout.fillWidth: true
            visible: root.controller.tray.available || root.controller.tray.on || root.controller.tray.persistent
            UI.SettingRow {
                Layout.fillWidth: true
                title: "Show tray icon"
                description: "Status and quick actions when the window is closed."
                UI.Toggle { objectName: "about-tray"; text: "Show tray icon"; observed: root.controller.tray.on || root.controller.tray.persistent; onRequested: value => root.controller.showTray(value) }
            }
        }
        UI.Label { Layout.fillWidth: true; visible: text.length > 0; text: root.controller.systemd.error; wrapMode: Text.WordWrap }
    }
    UI.Section {
        Layout.fillWidth: true
        title: "Service"
        description: "Downloads and settings belong to the aerial service. They keep working when this window is closed."
        UI.Group {
            Layout.fillWidth: true
            UI.SettingRow {
                Layout.fillWidth: true
                title: "Background service"
                description: "aerial, connected to this profile"
                UI.Label { text: root.controller.about.state }
            }
            UI.SettingRow {
                Layout.fillWidth: true
                separator: true
                title: "Settings file"
                description: root.controller.about.config || ""
                UI.Button { text: "Open folder"; glyph: "folder"; onClicked: root.controller.openSettingsFolder() }
            }
            UI.SettingRow {
                Layout.fillWidth: true
                separator: true
                title: "Video cache"
                description: root.controller.about.cache || ""
                UI.Button { text: "Open folder"; glyph: "folder"; onClicked: root.controller.openCacheFolder() }
            }
        }
        UI.Button { text: "Other service paths"; onClicked: pathsSheet.open() }
    }
    UI.Section {
        Layout.fillWidth: true
        title: "Credits"
        UI.Group {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 10
                UI.Label { Layout.fillWidth: true; text: "Apple's aerial videos are produced by Apple. aerial-lock downloads them directly from Apple's content delivery network onto your computer. We do not host, mirror or redistribute them. aerial-lock is not affiliated with Apple."; font.pixelSize: 13; wrapMode: Text.WordWrap }
                UI.Label { Layout.fillWidth: true; text: "Inspired by Aerial for macOS, started by John Coates and maintained by Guillaume Louel and contributors."; font.pixelSize: 13; wrapMode: Text.WordWrap }
                UI.Label { Layout.fillWidth: true; text: "Theme adaptations and bundled font credits are in Themes/ATTRIBUTION.md and the third-party notices in the theme bundle."; font.pixelSize: 13; wrapMode: Text.WordWrap }
                Flow {
                    Layout.fillWidth: true
                    spacing: 10
                    UI.Button { link: true; text: "Documentation"; onClicked: root.controller.portal.openLink("https://github.com/alexindigo/aerial-lock#readme") }
                    UI.Button { link: true; text: "Aerial legal notes"; onClicked: root.controller.portal.openLink("https://aerialscreensaver.github.io/legal.html") }
                }
            }
        }
    }
    UI.Sheet {
        id: pathsSheet
        title: "Service paths"
        Repeater {
            model: [{ key: "bundle", title: "Theme bundle" }, { key: "state", title: "Activity state" }, { key: "runtime", title: "Runtime directory" }]
            ColumnLayout {
                id: pathRow
                required property var modelData
                Layout.fillWidth: true
                UI.Label { Layout.fillWidth: true; text: pathRow.modelData.title; font.weight: Font.Medium }
                UI.Label { Layout.fillWidth: true; text: root.controller.about.paths[pathRow.modelData.key] || ""; font.pixelSize: 13; wrapMode: Text.WordWrap }
            }
        }
        UI.Button { Layout.alignment: Qt.AlignRight; text: "Close"; onClicked: pathsSheet.close() }
    }
}
