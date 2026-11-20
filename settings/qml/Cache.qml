import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
import AerialLock.Settings
import "components" as UI
UI.Page {
    id: root
    required property SettingsController controller
    signal downloadsRequested()
    title: "Cache & status"
    readonly property var facts: controller.cache.facts
    UI.Group {
        Layout.fillWidth: true
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            spacing: 20
            Rectangle {
                implicitWidth: 44; implicitHeight: 44; radius: 12; color: UI.Style.window
                UI.Glyph { anchors.centerIn: parent; name: root.facts.downloading ? "download" : "clock" }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                UI.Label { Layout.fillWidth: true; text: root.facts.title; font.pixelSize: 17; font.bold: true }
                C.ProgressBar {
                    Layout.fillWidth: true
                    visible: root.facts.downloading
                    indeterminate: !root.facts.progressKnown
                    value: root.facts.progress
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.facts.downloading
                    UI.Label { Layout.fillWidth: true; text: "From " + root.facts.source; font.pixelSize: 13 }
                    UI.Label { text: root.facts.received + " of " + root.facts.total; font.pixelSize: 13 }
                }
                UI.Label { Layout.fillWidth: true; visible: !root.facts.downloading; text: "Fetch now ignores connection, power and pause rules, never the cache limit or a source's quality band."; font.pixelSize: 13; wrapMode: Text.WordWrap }
                UI.Button { visible: !root.facts.downloading; text: "Change in Downloads"; onClicked: root.downloadsRequested() }
            }
            UI.Button { text: root.facts.downloading ? "Stop download" : "Fetch now"; glyph: root.facts.downloading ? "x" : "download"; primary: !root.facts.downloading; onClicked: root.controller.cache.fetchOrStop() }
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 18
        Repeater {
            model: [{ text: root.facts.ready, glyph: "movie" }, { text: root.facts.nextCheck, glyph: "clock" }, { text: root.facts.lastCheck, glyph: "refresh" }]
            RowLayout {
                id: fact
                required property var modelData
                spacing: 8
                UI.Glyph { name: fact.modelData.glyph; implicitWidth: 14; implicitHeight: 14 }
                UI.Label { text: fact.modelData.text; font.pixelSize: 13 }
            }
        }
    }
    UI.Section {
        Layout.fillWidth: true
        title: "Cache"
        UI.Group {
            Layout.fillWidth: true
            UI.CacheMeter { Layout.fillWidth: true; Layout.margins: 16; usage: root.facts.usage; free: root.facts.free; segments: root.facts.segments; legend: root.facts.legend }
            Repeater {
                model: [{ key: "cache.maxBytes", title: "Cache limit", description: "Downloads stop at this size. Your own folders and files don't count." }, { key: "cache.rotationDays", title: "Swap in new videos", description: "Once the cache is full, the least recently played videos are replaced." }]
                UI.SettingRow {
                    id: row
                    required property var modelData
                    Layout.fillWidth: true
                    title: modelData.title
                    description: modelData.description
                    separator: true
                    error: root.controller.controls[modelData.key].error
                    UI.Select {
                        objectName: row.modelData.key
                        model: root.controller.controls[row.modelData.key].choices
                        currentIndex: root.controller.controls[row.modelData.key].selected
                        inherited: root.controller.controls[row.modelData.key].inherited
                        Accessible.name: row.title
                        onActivated: root.controller.choose(row.modelData.key, currentValue)
                    }
                }
            }
            UI.SettingRow {
                Layout.fillWidth: true
                separator: true
                title: "Free up space"
                description: "Shows what can go first: leftovers and anything over the limit. Videos the lock screen is using stay."
                UI.Button { text: "Clean up"; onClicked: root.controller.cache.prepareCleanup() }
            }
        }
    }
    UI.Section {
        Layout.fillWidth: true
        title: "Recent activity"
        UI.Group {
            Layout.fillWidth: true
            Repeater {
                model: root.controller.cache.activity
                UI.SettingRow {
                    id: activityRow
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    separator: index > 0
                    title: modelData.text
                    glyph: modelData.glyph
                    compact: true
                    UI.Label { text: activityRow.modelData.time; font.pixelSize: 13 }
                }
            }
            UI.SettingRow { Layout.fillWidth: true; visible: root.controller.cache.activity.length === 0; title: "No recent activity"; description: "Checks and downloads will appear here." }
        }
    }
    UI.Label { Layout.fillWidth: true; text: root.controller.cache.error; visible: text.length > 0; wrapMode: Text.WordWrap }
}
