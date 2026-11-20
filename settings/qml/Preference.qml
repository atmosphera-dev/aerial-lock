import QtQuick
import "components" as UI
UI.SettingRow {
    id: root
    required property var descriptor
    required property var submit
    property var choose: submit
    error: descriptor.error || ""
    UI.Toggle {
        objectName: "toggle"
        visible: !root.descriptor.custom
        observed: !!root.descriptor.on
        text: root.title
        onRequested: value => root.submit(value)
    }
    UI.Select {
        visible: !!root.descriptor.custom
        model: root.descriptor.choices
        currentIndex: root.descriptor.selected
        inherited: root.descriptor.inherited
        onActivated: root.choose(currentValue)
        Accessible.name: root.title
    }
}
