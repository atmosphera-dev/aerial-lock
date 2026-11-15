import QtQuick
import Quickshell
import Quickshell.Services.Pam
import AerialLock.Native

// Run through the production runner inside a disposable Wayland VM session.
// The operator prepares an auth-only pam_unix fixture service with no lockout.
ShellRoot {
    id: root
    settings.watchFiles: false
    readonly property bool oldBackend: Quickshell.env("AERIAL_TEST_AUTH_BACKEND") === "old"
    readonly property int iterations: Number(Quickshell.env("AERIAL_TEST_AUTH_ITERATIONS") || "100")
    property int attempts: 0
    property double started: 0
    readonly property var endpoint: backend.item
    Loader {
        id: backend
        sourceComponent: root.oldBackend ? oldContext : execContext
        onLoaded: next.start()
    }
    Component {
        id: oldContext
        PamContext { config: "aerial-auth-timezone"; user: Quickshell.env("USER") }
    }
    Component {
        id: execContext
        AuthContext { config: "aerial-auth-timezone"; user: Quickshell.env("USER") }
    }
    Connections {
        target: root.endpoint
        function onPamMessage() {
            if (root.endpoint.responseRequired)
                root.endpoint.respond("deliberately-invalid-fixture-response")
        }
        function onCompleted(result) {
            deadline.stop()
            if (result !== 1) { console.error("AUTH_TIMEZONE_UNEXPECTED_RESULT " + result); Qt.exit(2); return }
            root.attempts++
            console.log("AUTH_TIMEZONE_RESULT " + root.attempts + " latencyMs=" + (Date.now() - root.started))
            if (root.attempts >= root.iterations) {
                console.log("AUTH_TIMEZONE_PASS backend=" + (root.oldBackend ? "old" : "exec") + " attempts=" + root.attempts)
                Qt.quit()
            } else next.start()
        }
        function onError(error) { console.error("AUTH_TIMEZONE_ERROR " + error); Qt.exit(2) }
    }
    Timer {
        id: next
        interval: 1
        onTriggered: {
            root.started = Date.now()
            deadline.start()
            if (!root.endpoint.start()) Qt.exit(2)
        }
    }
    Timer {
        id: deadline
        interval: 10000
        onTriggered: {
            console.log("AUTH_TIMEZONE_STALL backend=" + (root.oldBackend ? "old" : "exec") + " after=" + root.attempts)
            root.endpoint.abort()
            Qt.exit(3)
        }
    }
    Component.onDestruction: { if (root.endpoint) root.endpoint.abort() }
}
