import QtQuick
import "../Services"
import "../Themes" as Themes

// Test-only consumer barrier around the actual default theme. No response
// contents, lengths or key traffic are observed or persisted. A niri IPC
// output-on reply does not prove the new lock window has keyboard focus.
FocusScope {
    id: root
    required property LockContext context
    property Item backdrop: null
    readonly property int authWriterToken: theme.authWriterToken
    property bool nominated: false
    property int readyGeneration: -1
    readonly property var attachedWindow: Window.window
    readonly property bool windowActive: Window.active
    onAttachedWindowChanged: {
        readyGeneration = -1
        if (attachedWindow === null)
            console.log("HANDOFF_INPUT_PARKED")
    }
    onWindowActiveChanged: readyGeneration = -1
    onActiveFocusChanged: readyGeneration = -1
    Component.onCompleted: {
        if (attachedWindow === null)
            console.log("HANDOFF_INPUT_PARKED")
    }
    focus: true
    function focusInput() { theme.focusInput() }
    Themes.Macintosh {
        id: theme
        anchors.fill: parent
        context: root.context
        backdrop: root.backdrop
    }
    Timer {
        interval: 10
        repeat: true
        running: true
        onTriggered: {
            const input = root.context.auth.input
            if (!root.nominated && input.themeWriter === root.authWriterToken) {
                root.nominated = true
                console.log("HANDOFF_THEME_NOMINATED")
            }
            if (!root.Window.active || !root.activeFocus || input.activeWriter !== root.authWriterToken) {
                root.readyGeneration = -1
                return
            }
            if (root.readyGeneration !== input.generation) {
                root.readyGeneration = input.generation
                console.log("HANDOFF_INPUT_READY")
            }
        }
    }
}
