import QtQuick
import "../../Services"

// Shared intent/focus adapter, deliberately without pixels or platform work.
// Each theme supplies its existing controls/layout and a public focus callback.
FocusScope {
    id: root
    required property LockContext context
    property var actions: [] // [{action, label, name, available, icon}]
    property var focusControl: function(index) {}
    property int focusIndex: 0
    readonly property bool confirming: context.power.confirmationRequired
    readonly property bool waiting: context.power.busy
    readonly property bool failed: context.power.error.length > 0
    readonly property bool active: context.power.enabled && (confirming || waiting || failed)
    readonly property var availableActions: context.power.enabled
            ? actions.filter(function(action) { return action.available }) : []
    readonly property var selected: {
        for (var action of actions) {
            if (action.action === context.power.activeAction)
                return action
        }
        return {action: "", label: "", name: "", icon: ""}
    }
    readonly property var controls: {
        if (!context.power.enabled) return []
        if (confirming) return [{action: "cancel", label: context.labels.powerCancel, name: "powerCancel"},
                                {action: "confirm", label: selected.label, name: "powerConfirm"}]
        if (waiting) return []
        if (failed) return [{action: "dismiss", label: context.labels.powerDismiss, name: "powerDismiss"}]
        return availableActions
    }
    readonly property string message: confirming ? context.labels.powerConfirmQuestion
            : failed ? context.power.error : waiting ? context.power.status : ""

    function activate(action) {
        if (action === "cancel") context.power.cancel()
        else if (action === "confirm") context.power.confirm()
        else if (action === "dismiss") context.power.clearError()
        else if (!active) {
            switch (action) {
            case "suspend": context.power.requestSuspend(); break
            case "hibernate": context.power.requestHibernate(); break
            case "reboot": context.power.requestReboot(); break
            case "powerOff": context.power.requestPowerOff(); break
            }
        }
    }
    function focusFirst() {
        if (visible && enabled && confirming && Window.active) {
            focusIndex = 0
            focusControl(0)
        }
    }
    function navigate(direction) {
        if (!confirming || controls.length === 0) return
        focusIndex = (focusIndex + direction + controls.length) % controls.length
        focusControl(focusIndex)
    }
    onConfirmingChanged: { if (confirming) Qt.callLater(root.focusFirst) }
    onVisibleChanged: { if (visible && confirming) Qt.callLater(root.focusFirst) }
    Keys.onEscapePressed: function(event) {
        if (confirming) { context.power.cancel(); event.accepted = true }
    }
}
