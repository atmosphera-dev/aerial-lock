pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import InterActive
import InterActive.Foundation as IA

SettingRow {
    id: root
    required property var controls
    required property string prefix
    required property var choices
    signal requested(string key, bool value)
    stacked: true
    error: choices.map(choice => controls[prefix + choice.key].error).filter((text, index, all) => text && all.indexOf(text) === index).join("\n")
    GridLayout {
        columns: root.width >= 390 ? 3 : 2
        columnSpacing: IA.Appearance.metrics.spacingXL
        rowSpacing: 0
        Repeater {
            model: root.choices
            IACheckBox {
                id: check
                required property var modelData
                readonly property string key: root.prefix + modelData.key
                readonly property var descriptor: root.controls[key]
                objectName: key
                text: modelData.label
                leftPadding: 0
                checked: descriptor.on
                onToggled: {
                    const value = checked;
                    checked = Qt.binding(() => check.descriptor.on);
                    root.requested(key, value);
                }
            }
        }
    }
}
