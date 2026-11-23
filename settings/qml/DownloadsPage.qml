import QtQuick
import QtQuick.Layouts
import AerialLock.Settings
import InterActive
import InterActive.Foundation as IA

PageScaffold {
    id: root
    required property SettingsController controller
    readonly property var controls: controller.controls
    title: "Downloads"

    SettingsSection {
        Layout.fillWidth: true
        title: "Automatic downloads"
        PreferenceRow {
            objectName: "automatic"
            label: "Download new videos automatically"
            description: "Fetch now on Overview works either way."
            descriptor: root.controls.automatic
            onToggled: value => root.controller.edit("automatic", value)
        }
        SettingRow {
            id: interval
            readonly property string key: "acquisition.checkIntervalSeconds"
            label: "Check for new videos"
            error: root.controls[key].error
            SettingsChoice {
                objectName: "interval"
                descriptor: root.controls[interval.key]
                Accessible.name: interval.label
                onRequested: token => root.controller.choose(interval.key, token)
            }
        }
        PreferenceRow {
            id: quality
            readonly property string key: "acquisition.automatic.allowedResolutions"
            objectName: "quality"
            label: "Allow 4K videos"
            description: "About four times the space and data of 1080p."
            descriptor: root.controls[key]
            onToggled: value => root.controller.edit(key, value)
            onChosen: token => root.controller.choose(key, token)
        }
    }
    SettingsSection {
        Layout.fillWidth: true
        title: "When to download"
        description: "Automatic downloads wait until these allow it. " + root.controller.downloadObservation
        PreferenceRow {
            readonly property string key: "acquisition.automatic.allowMetered"
            objectName: "metered"
            label: "Metered connections"
            description: "Phone hotspots and connections you marked as metered."
            descriptor: root.controls[key]
            onToggled: value => root.controller.edit(key, value)
        }
        CheckGroup {
            label: "Connections"
            prefix: "acquisition.automatic.connectionTypes."
            controls: root.controls
            choices: [
                { key: "ethernet", label: "Ethernet" }, { key: "wifi", label: "Wi-Fi" },
                { key: "cellular", label: "Mobile broadband" }, { key: "bluetooth", label: "Bluetooth" },
                { key: "vpn", label: "VPN" }, { key: "other", label: "Other" }, { key: "unknown", label: "Unknown" }
            ]
            onRequested: (key, value) => root.controller.edit(key, value)
        }
        CheckGroup {
            label: "Power"
            prefix: "acquisition.automatic.power."
            controls: root.controls
            choices: [{ key: "ac", label: "AC power" }, { key: "battery", label: "Battery" }, { key: "unknown", label: "Unknown" }]
            onRequested: (key, value) => root.controller.edit(key, value)
        }
    }
    SettingsSection {
        Layout.fillWidth: true
        title: "Storage"
        description: "Your own folders and files don't count toward the limit."
        Repeater {
            model: [
                { key: "cache.maxBytes", label: "Cache limit", description: "" },
                { key: "cache.rotationDays", label: "Swap in new videos", description: "When the cache is full, the least recently played go first." }
            ]
            SettingRow {
                id: storage
                required property var modelData
                label: modelData.label
                description: modelData.description
                error: root.controls[modelData.key].error
                SettingsChoice {
                    objectName: storage.modelData.key
                    descriptor: root.controls[storage.modelData.key]
                    Accessible.name: storage.label
                    onRequested: token => root.controller.choose(storage.modelData.key, token)
                }
            }
        }
        SettingRow {
            label: "Free up space"
            description: "Shows what can go before anything is deleted."
            IAButton {
                objectName: "prepareCleanup"
                text: "Clean up"
                onClicked: root.controller.cache.prepareCleanup()
            }
        }
    }
    CleanupDialog {
        objectName: "cleanupDialog"
        cache: root.controller.cache
    }
}
