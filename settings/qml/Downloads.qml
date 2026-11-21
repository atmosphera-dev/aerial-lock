import QtQuick
import QtQuick.Layouts
import AerialLock.Settings
import "components" as UI
UI.Page {
    id: root
    required property SettingsController controller
    title: "Downloads"
    readonly property string intervalText: interval.displayText
    UI.Section {
        Layout.fillWidth: true
        title: "Automatic downloads"
        UI.Group {
            Layout.fillWidth: true
            Preference {
                Layout.fillWidth: true
                objectName: "automatic"
                title: "Download videos automatically"
                description: "Checks for new videos, fills the cache, then swaps in new ones on schedule. Fetch now works even when this is off."
                descriptor: root.controller.controls.automatic
                submit: value => root.controller.edit("automatic", value)
            }
            UI.SettingRow {
                Layout.fillWidth: true
                title: "Check for new videos"
                separator: true
                error: root.controller.controls["acquisition.checkIntervalSeconds"].error
                UI.Select {
                    id: interval
                    objectName: "interval"
                    model: root.controller.controls["acquisition.checkIntervalSeconds"].choices
                    currentIndex: root.controller.controls["acquisition.checkIntervalSeconds"].selected
                    inherited: root.controller.controls["acquisition.checkIntervalSeconds"].inherited
                    Accessible.name: "Check for new videos"
                    onActivated: root.controller.choose("acquisition.checkIntervalSeconds", currentValue)
                }
            }
        }
    }
    UI.Section {
        Layout.fillWidth: true
        title: "Video quality"
        UI.Group {
            Layout.fillWidth: true
            Preference {
                Layout.fillWidth: true
                title: "Allow 4K videos"
                objectName: "quality"
                description: "When off, automatic downloads use up to 1080p. Fetch now may use higher quality according to the source's settings. 4K videos take about four times the space and data."
                descriptor: root.controller.controls["acquisition.automatic.allowedResolutions"]
                submit: value => root.controller.edit("acquisition.automatic.allowedResolutions", value)
                choose: token => root.controller.choose("acquisition.automatic.allowedResolutions", token)
            }
        }
    }
    UI.Section {
        Layout.fillWidth: true
        title: "Connections"
        description: "Automatic downloads use only the connections turned on here."
        observation: root.controller.connectionObservation
        UI.Group {
            Layout.fillWidth: true
            Preference {
                Layout.fillWidth: true
                title: "Download on metered connections"
                objectName: "metered"
                description: "Phone hotspots and other connections marked as metered."
                descriptor: root.controller.controls["acquisition.automatic.allowMetered"]
                submit: value => root.controller.edit("acquisition.automatic.allowMetered", value)
            }
            GridLayout {
                Layout.fillWidth: true
                columns: root.width < 560 ? 1 : 2
                columnSpacing: 0
                rowSpacing: 0
                Repeater {
                    model: [
                        { key: "ethernet", label: "Ethernet", icon: "ethernet" },
                        { key: "wifi", label: "Wi-Fi", icon: "wifi" },
                        { key: "cellular", label: "Mobile broadband", icon: "cell" },
                        { key: "bluetooth", label: "Bluetooth", icon: "bluetooth" },
                        { key: "vpn", label: "VPN", icon: "vpn" },
                        { key: "other", label: "Other", icon: "network" },
                        { key: "unknown", label: "Unknown", icon: "help" }
                    ]
                    Preference {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        compact: true
                        separator: true
                        verticalSeparator: root.width >= 560 && index % 2 === 0 && index < 6
                        title: modelData.label
                        objectName: "connection-" + modelData.key
                        glyph: modelData.icon
                        descriptor: root.controller.controls["acquisition.automatic.connectionTypes." + modelData.key]
                        submit: value => root.controller.edit("acquisition.automatic.connectionTypes." + modelData.key, value)
                    }
                }
            }
        }
    }
    UI.Section {
        Layout.fillWidth: true
        title: "Power"
        description: "Automatic downloads run only on the power sources turned on here."
        observation: root.controller.powerObservation
        UI.Group {
            Layout.fillWidth: true
            GridLayout {
                Layout.fillWidth: true
                columns: root.width < 560 ? 1 : 2
                columnSpacing: 0
                rowSpacing: 0
                Repeater {
                    model: [{ key: "ac", label: "AC power", icon: "plug" }, { key: "battery", label: "Battery", icon: "battery" }, { key: "unknown", label: "Unknown", icon: "help" }]
                    Preference {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        compact: true
                        separator: index > 1
                        verticalSeparator: root.width >= 560 && index === 0
                        title: modelData.label
                        objectName: "power-" + modelData.key
                        glyph: modelData.icon
                        descriptor: root.controller.controls["acquisition.automatic.power." + modelData.key]
                        submit: value => root.controller.edit("acquisition.automatic.power." + modelData.key, value)
                    }
                }
            }
        }
    }
}
