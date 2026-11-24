pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import AerialLock.Settings
import InterActive
import InterActive.Components
import InterActive.Foundation as IA

PageScaffold {
    id: root
    required property SettingsController controller
    readonly property var controls: controller.controls
    readonly property var idle: controller.notices.idle
    readonly property string appliedColor: controller.canonicalColor(controls.backgroundColor.value || "")
    readonly property var presets: [
        { label: "Black", token: "#000000" }, { label: "Graphite", token: "#1f2124" }, { label: "Midnight", token: "#101a33" }
    ]
    readonly property int presetIndex: presets.findIndex(entry => entry.token === appliedColor)
    readonly property var backgroundChoice: ({
        choices: presets.concat([{ label: presetIndex < 0 && appliedColor.length ? "Custom: " + appliedColor : "Custom...", token: "" }]),
        selected: presetIndex < 0 ? presets.length : presetIndex,
        inherited: controls.backgroundColor.inherited
    })
    title: "Lock screen"

    ColumnLayout {
        Layout.fillWidth: true
        spacing: IA.Appearance.metrics.spacingXS
        IALabel { text: "Theme"; fontWeight: IA.Appearance.typography.emphasisWeight }
        IALabel {
            Layout.fillWidth: true
            text: "Used from the next time the screen locks."
            fontPixelSize: IA.Appearance.typography.supportingSize
            foregroundColor: IA.Appearance.colors.secondaryText
        }
        GridLayout {
            Layout.fillWidth: true
            Layout.topMargin: IA.Appearance.metrics.spacingS
            columns: 3
            columnSpacing: IA.Appearance.metrics.spacingL
            rowSpacing: IA.Appearance.metrics.spacingL
            Repeater {
                model: root.controller.themes.rows
                ThemeTile {
                    required property var modelData
                    objectName: "theme-" + modelData.name
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    text: modelData.title
                    preview: modelData.preview
                    checked: root.controls.theme.value === modelData.name
                    onClicked: root.controller.edit("theme", modelData.name)
                }
            }
        }
        IALabel {
            Layout.fillWidth: true
            text: root.controls.theme.error
            visible: text.length > 0
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            foregroundColor: IA.Appearance.colors.errorText
        }
    }
    SettingsSection {
        Layout.fillWidth: true
        title: "Appearance"
        SettingRow {
            id: colorRow
            label: "Background color"
            description: "Shown while no video is ready."
            error: root.controls.backgroundColor.error
            SettingsChoice {
                objectName: "backgroundColor"
                Accessible.name: colorRow.label
                descriptor: root.backgroundChoice
                leadingColor: root.controls.backgroundColor.color
                onRequested: token => {
                    if (token) root.controller.setColor(token);
                    else {
                        colorField.text = root.controls.backgroundColor.value || "";
                        colorDialog.open();
                    }
                }
            }
        }
        PreferenceRow {
            objectName: "powerActions"
            label: "Power buttons"
            description: "Suspend, restart and shut down on the lock screen."
            descriptor: root.controls["powerActions.enabled"]
            onToggled: value => root.controller.edit("powerActions.enabled", value)
        }
    }
    SettingsSection {
        Layout.fillWidth: true
        title: "Password"
        PreferenceRow {
            objectName: "ignoreLeadingSpaces"
            label: "Ignore leading spaces"
            description: "Spaces typed before the password are dropped. Turn off if your password starts with a space."
            descriptor: root.controls.ignoreLeadingSpaces
            onToggled: value => root.controller.edit("ignoreLeadingSpaces", value)
        }
    }
    SettingsSection {
        Layout.fillWidth: true
        title: "Locking"
        description: "Changes apply when aerial-idle restarts. " + (root.controller.systemd.idle.observation || "")
        IAStatusBanner {
            objectName: "idleNotice"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            visible: !!root.idle.id
            title: root.idle.title || ""
            message: root.idle.message || ""
            validation: root.idle.severity === "error" ? IA.Presentation.Invalid
                : root.idle.severity === "warning" ? IA.Presentation.Warning : IA.Presentation.None
            iconSource: Qt.resolvedUrl("icons/alert.svg")
            actionText: root.idle.actionLabel || ""
            controlRadius: 0
            borderColor: IA.Appearance.colors.transparent
            onActionRequested: root.controller.dispatchAction(root.idle.actionId)
        }
        SettingRow {
            id: timeout
            label: "Lock after inactivity"
            error: root.controls["idle.timeoutSeconds"].error
            SettingsChoice {
                objectName: "idleTimeout"
                Accessible.name: timeout.label
                descriptor: root.controls["idle.timeoutSeconds"]
                onRequested: token => root.controller.choose("idle.timeoutSeconds", token)
            }
        }
        Repeater {
            model: [
                { key: "respectInhibitors", label: "Wait while video plays", description: "Video players and other apps can keep the screen on." },
                { key: "lockOnSuspend", label: "Lock before sleep", description: "" },
                { key: "logindLockUnlock", label: "Follow system lock requests", description: "Respond to lock and unlock requests from your session." }
            ]
            PreferenceRow {
                required property var modelData
                readonly property string key: "idle." + modelData.key
                objectName: key
                label: modelData.label
                description: modelData.description
                descriptor: root.controls[key]
                onToggled: value => root.controller.edit(key, value)
            }
        }
        SettingRow {
            visible: error.length > 0
            error: root.controller.systemd.error
        }
    }

    SettingsDialog {
        id: colorDialog
        objectName: "colorDialog"
        title: "Background color"
        readonly property string canonical: root.controller.canonicalColor(colorField.text)
        standardButtons: T.Dialog.Apply | T.Dialog.Cancel
        onApplied: if (canonical.length) { root.controller.setColor(canonical); close(); }
        onOpened: {
            const apply = standardButton(T.Dialog.Apply);
            apply.text = "Use color";
            apply.enabled = Qt.binding(() => colorDialog.canonical.length > 0);
            colorField.forceActiveFocus(Qt.TabFocusReason);
        }
        ColumnLayout {
            width: parent.width
            spacing: IA.Appearance.metrics.spacingS
            IAFieldRow {
                Layout.fillWidth: true
                label: "Color"
                IATextField { id: colorField; objectName: "colorField"; Layout.fillWidth: true; Accessible.name: "Color" }
                Rectangle {
                    Layout.preferredWidth: IA.Appearance.metrics.iconSize
                    Layout.preferredHeight: width
                    visible: colorDialog.canonical.length > 0
                    color: visible ? colorDialog.canonical : IA.Appearance.colors.transparent
                    radius: IA.Appearance.metrics.spacingXS
                    border.width: IA.Appearance.metrics.hairlineWidth
                    border.color: IA.Appearance.colors.outline
                    Accessible.ignored: true
                }
            }
            IALabel {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                foregroundColor: IA.Appearance.colors.secondaryText
                text: colorDialog.canonical.length ? "Will use " + colorDialog.canonical : "Enter a valid color, such as #000000."
            }
        }
    }
}
