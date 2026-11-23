import QtQuick
import InterActive

SettingRow {
    id: root
    required property var descriptor
    signal toggled(bool value)
    signal chosen(var token)
    error: descriptor.error || ""
    IASwitch {
        objectName: "preferenceSwitch"
        visible: !root.descriptor.custom
        Accessible.name: root.label
        checked: root.descriptor.on ?? false
        onToggled: {
            const value = checked;
            checked = Qt.binding(() => root.descriptor.on ?? false);
            root.toggled(value);
        }
    }
    SettingsChoice {
        objectName: "preferenceChoice"
        visible: !!root.descriptor.custom
        Accessible.name: root.label
        descriptor: root.descriptor
        onRequested: token => root.chosen(token)
    }
}
