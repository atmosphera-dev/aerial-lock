import QtQuick
import Quickshell
import AerialLock.Native
import "../Modules/Lock" as LockModule
import "../Services" as Services

// Real Lock controller, synthetic PAM only. No compositor lock is requested.
ShellRoot {
    id: fixture
    settings.watchFiles: false
    readonly property LockModule.Lock lock: loader.item as LockModule.Lock
    Binding {
        target: Services.Logger
        property: "verbose"
        value: Quickshell.env("AERIAL_LOCK_VERBOSE") === "1"
    }
    Services.ConfigStore { id: cfg }
    QtObject {
        id: endpoint
        property bool active: false
        property string message: ""
        property bool messageIsError: false
        property bool responseRequired: false
        property bool responseVisible: false
        property string user: "synthetic-fixture"
        property string config: "synthetic"
        property int result: AuthContext.Failed
        property int starts: 0
        property int responses: 0
        property var feedback: []
        signal pamMessage()
        signal completed(int result)
        signal error(int error)
        function start() {
            starts++
            active = true
            message = "Password:"
            responseRequired = true
            pamMessage()
            return true
        }
        function respond(answer) {
            responses++
            active = false
            responseRequired = false
            completed(result)
        }
        function abort() { active = false; responseRequired = false }
    }
    Connections {
        target: fixture.lock
        function onStatusMessageChanged() { endpoint.feedback.push(fixture.lock.statusMessage) }
    }
    Loader {
        id: loader
        active: cfg.ready
        sourceComponent: Component {
            LockModule.Lock {
                config: cfg
                logger: Services.Logger
                locked: false
                pamEndpoint: endpoint
            }
        }
        onLoaded: sequence.start()
    }
    Timer {
        id: sequence
        interval: 0
        onTriggered: {
            const lock = fixture.lock
            var input = lock.context.auth.input
            var writer = input.registerWriter()
            input.claimWriter(writer)
            var initialStatus = lock.context.auth.status
            lock.context.auth.submit()
            if (endpoint.starts !== 0 || endpoint.responses !== 0 || lock.context.auth.status !== initialStatus) {
                console.error("Empty initial Return changed status or started PAM")
                Qt.exit(1)
                return
            }
            input.applyEdit(writer, input.revision, "wrong-secret-7e83", 17, 17)
            lock.context.auth.submit()
            var rejection = lock.context.auth.status
            lock.context.auth.submit()
            if (endpoint.starts !== 1 || endpoint.responses !== 1 || rejection !== lock.context.labels.authFailed
                    || lock.context.auth.status !== rejection) {
                console.error("Typed rejection or empty retry did not preserve the expected state")
                Qt.exit(1)
                return
            }
            endpoint.result = AuthContext.Success
            input.applyEdit(writer, input.revision, "right-secret-91ab", 17, 17)
            lock.context.auth.submit()
            if (endpoint.starts !== 2 || endpoint.responses !== 2 || endpoint.feedback.includes("Password:")) {
                console.error("First PAM prompt was displayed or answered more than once")
                Qt.exit(1)
                return
            }
            input.unregisterWriter(writer)
            console.log("LOCK_STATE_EXERCISED")
            Qt.quit()
        }
    }
    Timer { interval: 10000; running: true; onTriggered: Qt.exit(1) }
}
