import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
import AerialLock.Settings
import "components" as UI
UI.Sheet {
    id: root
    required property SourcesController sources
    readonly property var draft: sources.draft
    title: draft.editing ? "Edit source" : "Add source"
    visible: sources.sheetOpen
    closable: !draft.pending
    onClosed: sources.dismiss()
    Rectangle {
        enabled: !root.draft.pending
        Layout.fillWidth: true
        implicitHeight: segments.implicitHeight + 6
        color: "transparent"
        radius: 10
        border.color: UI.Style.line
        GridLayout {
            id: segments
            anchors.fill: parent
            anchors.margins: 3
            columns: choices.count === 4 ? 2 : 3
            columnSpacing: 3
            rowSpacing: 3
            Repeater {
                id: choices
                model: [{ title: "Online pack", value: "community", glyph: "world" }, { title: "Folder", value: "folder", glyph: "folder" }, { title: "Video file", value: "file", glyph: "movie" }].concat(root.sources.appleAvailable && !root.draft.editing ? [{ title: "Apple TV", value: "defaultApple", glyph: "tv" }] : [])
                C.AbstractButton {
                    id: segment
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: 32
                    padding: 5
                    checked: root.draft.type === modelData.value || (modelData.value === "community" && root.draft.type === "apple")
                    text: modelData.title
                    background: Rectangle { radius: 7; color: segment.checked ? UI.Style.selection : "transparent"; border.width: segment.visualFocus ? 2 : 0; border.color: UI.Style.accent }
                    contentItem: RowLayout {
                        spacing: 8
                        Item { Layout.fillWidth: true }
                        UI.Glyph { name: segment.modelData.glyph; implicitWidth: 16; implicitHeight: 16; color: segment.checked ? UI.Style.selectionText : UI.Style.text }
                        UI.Label { text: segment.text; font.weight: Font.Medium; color: segment.checked ? UI.Style.selectionText : UI.Style.text }
                        Item { Layout.fillWidth: true }
                    }
                    onClicked: root.sources.field("type", modelData.value)
                }
            }
        }
    }
    UI.Field {
        Layout.fillWidth: true
        objectName: "sourceName"
        enabled: !root.draft.pending
        title: "Name"
        text: root.draft.name || ""
        edited: value => root.sources.field("name", value)
    }
    UI.Field {
        Layout.fillWidth: true
        objectName: "sourceAddress"
        enabled: !root.draft.pending
        visible: root.draft.type === "apple" || root.draft.type === "community"
        title: "Address"
        text: root.draft.source || ""
        edited: value => root.sources.field("source", value)
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.draft.type === "folder" || root.draft.type === "file"
        enabled: !root.draft.pending
        UI.Label { text: root.draft.type === "folder" ? "Folder" : "Video file"; font.weight: Font.Medium }
        UI.Label { Layout.fillWidth: true; text: root.draft.source || "Choose a local folder or file"; elide: Text.ElideMiddle }
        UI.Button { text: "Choose..."; onClicked: root.sources.choosePath() }
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.draft.type === "apple" || root.draft.type === "community"
        enabled: !root.draft.pending
        spacing: 6
        UI.Label { text: "Format"; font.weight: Font.Medium }
        UI.Select {
            Layout.fillWidth: true
            objectName: "sourceFormat"
            Accessible.name: "Format"
            model: [{ label: "Aerial community pack", token: "community" }, { label: "Apple aerials", token: "apple" }]
            currentIndex: root.draft.type === "apple" ? 1 : 0
            onActivated: root.sources.field("type", currentValue)
        }
        UI.Label { text: "Highest quality"; font.weight: Font.Medium; Layout.topMargin: 8 }
        UI.Select {
            Layout.fillWidth: true
            objectName: "sourceQuality"
            Accessible.name: "Highest quality"
            model: [{ label: "As set in Downloads", token: "0" }, { label: "1080p", token: "1080" }, { label: "4K", token: "2160" }]
            currentIndex: root.draft.maxResolution === 1080 ? 1 : root.draft.maxResolution === 2160 ? 2 : 0
            inherited: !root.draft.maxResolution
            onActivated: root.sources.field("maxResolution", Number(currentValue))
        }
    }
    UI.Label { Layout.fillWidth: true; visible: text.length > 0; text: root.draft.error || ""; wrapMode: Text.WordWrap }
    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        UI.Button { visible: !root.draft.pending; text: "Cancel"; onClicked: root.close() }
        UI.Label { visible: root.draft.pending; text: "Saving..." }
        UI.Button { visible: !root.draft.pending; text: root.draft.editing ? "Save" : "Add source"; glyph: root.draft.editing ? "" : "plus"; primary: true; onClicked: root.sources.save() }
    }
}
