import QtQuick
import QtQuick.Layouts
import AerialLock.Settings
import "components" as UI
UI.Page {
    id: root
    required property SettingsController controller
    title: "Sources"
    headingAction: Component {
        UI.Button { text: "Add source"; glyph: "plus"; primary: true; onClicked: root.controller.sources.begin("") }
    }
    UI.Section {
        Layout.fillWidth: true
        description: "Videos play in this order. Online packs download into the cache; folders and files play from where they are."
        UI.Group {
            Layout.fillWidth: true
            Repeater {
                model: root.controller.sources.rows
                UI.SourceRow {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    enabled: !root.controller.sources.busy
                    sourceId: modelData.id
                    title: modelData.name
                    description: modelData.description
                    glyph: modelData.glyph
                    on: modelData.enabled
                    canRetry: modelData.canRetry
                    separator: index > 0
                    toggle: (id, enabled) => root.controller.sources.toggle(id, enabled)
                    edit: id => root.controller.sources.begin(id)
                    remove: id => root.controller.sources.remove(id)
                    retry: id => root.controller.sources.retry(id)
                    beginDrag: id => root.controller.sources.beginReorder(id)
                    dropBefore: id => root.controller.sources.dropBefore(id)
                }
            }
            UI.SettingRow {
                Layout.fillWidth: true
                visible: root.controller.sources.rows.length === 0
                title: "No video sources"
                description: "Add an online pack, folder or video file. The lock screen uses your background color until a video is ready."
            }
            DropArea {
                Layout.fillWidth: true
                implicitHeight: 12
                keys: ["aerial-source"]
                onDropped: root.controller.sources.dropBefore("")
            }
        }
        UI.Label { Layout.fillWidth: true; visible: text.length > 0; text: root.controller.sources.error; wrapMode: Text.WordWrap }
    }
}
