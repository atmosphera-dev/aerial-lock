import QtQuick
import QtQuick.Layouts
import AerialLock.Settings
import "components" as UI
UI.Page {
    id: root
    required property SettingsController controller
    title: "Lock screen"
    UI.Section {
        Layout.fillWidth: true
        title: "Theme"
        description: "Changes the whole lock screen. Applies from the next lock."
        GridLayout {
            Layout.fillWidth: true
            columns: width >= 600 ? 3 : 2
            columnSpacing: 12
            rowSpacing: 12
            Repeater {
                model: root.controller.themes.rows
                UI.ThemeCard {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 200
                    text: modelData.title
                    preview: modelData.preview
                    checked: root.controller.controls.theme.value === modelData.name
                    onClicked: root.controller.edit("theme", modelData.name)
                }
            }
        }
        UI.Label { Layout.fillWidth: true; visible: text.length > 0; text: root.controller.controls.theme.error; wrapMode: Text.WordWrap }
    }
    UI.Section {
        Layout.fillWidth: true
        title: "Appearance"
        UI.Group {
            Layout.fillWidth: true
            UI.SettingRow {
                Layout.fillWidth: true
                title: "Background color"
                description: "Shown while no video is ready."
                error: root.controller.controls.backgroundColor.error
                UI.ValueButton {
                    objectName: "backgroundColor"
                    text: root.controller.controls.backgroundColor.value
                    swatch: root.controller.controls.backgroundColor.color
                    inherited: root.controller.controls.backgroundColor.inherited
                    onClicked: { colorField.text = text; colorSheet.open(); }
                }
            }
            Preference {
                Layout.fillWidth: true
                objectName: "powerActions"
                separator: true
                title: "Power buttons"
                description: "Show suspend, restart and shut down on the lock screen."
                descriptor: root.controller.controls["powerActions.enabled"]
                submit: value => root.controller.edit("powerActions.enabled", value)
            }
        }
    }
    UI.Section {
        Layout.fillWidth: true
        title: "Locking"
        description: "Handled by the aerial-idle service, which reads these when it starts."
        observation: root.controller.systemd.idle.observation
        UI.Group {
            Layout.fillWidth: true
            visible: root.controller.systemd.idle.message.length > 0
            UI.SettingRow {
                Layout.fillWidth: true
                description: root.controller.systemd.idle.message
                glyph: "info"
                UI.Button {
                    visible: root.controller.systemd.idle.action.length > 0
                    primary: true
                    text: root.controller.systemd.idle.actionLabel
                    onClicked: {
                        if (root.controller.systemd.idle.action === "enableStart") idleSheet.open();
                        else root.controller.systemd.operate("aerial-idle.service", root.controller.systemd.idle.action);
                    }
                }
            }
        }
        UI.Group {
            Layout.fillWidth: true
            UI.SettingRow {
                Layout.fillWidth: true
                title: "Lock after inactivity"
                description: "Locks when the keyboard and mouse haven't been used for this long."
                error: root.controller.controls["idle.timeoutSeconds"].error
                UI.Select {
                    objectName: "idleTimeout"
                    model: root.controller.controls["idle.timeoutSeconds"].choices
                    currentIndex: root.controller.controls["idle.timeoutSeconds"].selected
                    inherited: root.controller.controls["idle.timeoutSeconds"].inherited
                    Accessible.name: "Lock after inactivity"
                    onActivated: root.controller.choose("idle.timeoutSeconds", currentValue)
                }
            }
            Repeater {
                model: [{ key: "respectInhibitors", title: "Wait while video plays", description: "Apps that keep the screen awake, like video players, delay locking." }, { key: "lockOnSuspend", title: "Lock before sleep", description: "The screen locks before the computer suspends." }, { key: "logindLockUnlock", title: "Follow system lock requests", description: "Lock and unlock when the session is asked to, for example by loginctl." }]
                Preference {
                    required property var modelData
                    Layout.fillWidth: true
                    objectName: "idle-" + modelData.key
                    separator: true
                    title: modelData.title
                    description: modelData.description
                    descriptor: root.controller.controls["idle." + modelData.key]
                    submit: value => root.controller.edit("idle." + modelData.key, value)
                }
            }
        }
        UI.Label { Layout.fillWidth: true; visible: text.length > 0; text: root.controller.systemd.error; wrapMode: Text.WordWrap }
    }
    UI.Sheet {
        id: colorSheet
        title: "Background color"
        UI.Field { id: colorField; Layout.fillWidth: true; title: "Color"; edited: value => {} }
        UI.Label { Layout.fillWidth: true; text: root.controller.canonicalColor(colorField.text).length > 0 ? "Will use " + root.controller.canonicalColor(colorField.text) : "Enter a valid color, such as #000000."; wrapMode: Text.WordWrap }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            UI.Button { text: "Cancel"; onClicked: colorSheet.close() }
            UI.Button {
                visible: root.controller.canonicalColor(colorField.text).length > 0
                text: "Use " + root.controller.canonicalColor(colorField.text)
                primary: true
                onClicked: { root.controller.setColor(root.controller.canonicalColor(colorField.text)); colorSheet.close(); }
            }
        }
    }
    UI.Sheet {
        id: idleSheet
        title: "Turn on automatic locking"
        UI.Label { Layout.fillWidth: true; text: "Use one idle owner. If swayidle, hypridle or another daemon already handles locking, turn that setup off yourself before enabling aerial-idle. Aerial will not change other daemons or desktop configuration."; wrapMode: Text.WordWrap }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            UI.Button { text: "Cancel"; onClicked: idleSheet.close() }
            UI.Button { text: "Turn on"; primary: true; onClicked: { root.controller.systemd.operate("aerial-idle.service", "enableStart"); idleSheet.close(); } }
        }
    }
}
