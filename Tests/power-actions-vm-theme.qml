import QtQuick
import Quickshell
import Quickshell.Io
import "../Services"

// Trusted VM-only observer/intent consumer. The installed native service and
// lock remain real. No transport selection or test switch enters the product.
FocusScope {
    id: root
    required property LockContext context
    property Item backdrop: null
    readonly property var preferredTopics: []
    property int sequence: 0
    property bool observing: true
    readonly property var loadedTheme: theme.item
    function focusInput() { if (loadedTheme) loadedTheme.focusInput() }
    Component.onCompleted: console.log("POWER_VM_THEME_CREATED")
    Component.onDestruction: console.log("POWER_VM_THEME_DESTROYED")
    Loader {
        id: theme
        anchors.fill: parent
        Component.onCompleted: setSource("root:/Themes/" + Quickshell.env("POWER_VM_THEME") + ".qml",
                                        {context: root.context, backdrop: root.backdrop})
    }
    Binding { target: theme.item; property: "backdrop"; value: root.backdrop; when: theme.item !== null }
    FileView {
        id: command
        path: Quickshell.env("POWER_VM_COMMAND")
        preload: true
        onLoaded: {
            var message = JSON.parse(text())
            if (message.sequence <= root.sequence) return
            root.sequence = message.sequence
            switch (message.action) {
            case "suspend": root.context.power.requestSuspend(); break
            case "hibernate": root.context.power.requestHibernate(); break
            case "reboot": root.context.power.requestReboot(); break
            case "powerOff": root.context.power.requestPowerOff(); break
            case "confirm": root.context.power.confirm(); break
            case "cancel": root.context.power.cancel(); break
            case "dismiss": root.context.power.clearError(); break
            case "stop": root.observing = false; console.log("POWER_VM_OBSERVATION_STOPPED"); break
            }
        }
    }
    // Read-only named-object traversal is test observation, never product wiring.
    function inventory(item, result) {
        if (!item) return
        if (item.objectName && item.visible) {
            var p = item.mapToItem(root, 0, 0)
            var q = item.mapToItem(root, item.width, item.height)
            var entry = {name: item.objectName, x: p.x, y: p.y,
                         width: q.x - p.x, height: q.y - p.y, focused: item.activeFocus}
            if (item.objectName === "powerMessage") {
                entry.text = item.text
                entry.format = item.textFormat
            }
            result.push(entry)
        }
        for (var child of item.children) inventory(child, result)
    }
    Timer {
        interval: 100
        repeat: true
        running: root.observing
        onTriggered: {
            command.reload()
            var power = root.context.power
            var input = root.context.auth.input
            var items = []
            root.inventory(theme.item, items)
            var snapshot = {sequence: root.sequence, theme: Quickshell.env("POWER_VM_THEME"),
                output: root.context.session.outputName, attached: root.Window.window !== null,
                windowActive: root.Window.active, width: root.width, height: root.height,
                state: power.state, pending: power.pendingAction, active: power.activeAction,
                confirming: power.confirmationRequired, busy: power.busy, error: power.error,
                status: power.status, who: power.blockerWho, why: power.blockerWhy,
                enabled: power.enabled, checking: root.context.labels.powerChecking,
                canSuspend: power.canSuspend, canHibernate: power.canHibernate,
                canReboot: power.canReboot, canPowerOff: power.canPowerOff,
                suspendReason: power.suspendReason, hibernateReason: power.hibernateReason,
                rebootReason: power.rebootReason, powerOffReason: power.powerOffReason,
                syntheticDraft: input.text === "power-vm-draft", items: items}
            // Never emit credential-derived metadata; only the exact synthetic draft.
            if (snapshot.syntheticDraft) {
                snapshot.cursor = input.cursorPosition
                snapshot.anchor = input.anchorPosition
                snapshot.revision = input.revision
            }
            console.log("POWER_VM_STATE " + JSON.stringify(snapshot))
        }
    }
}
