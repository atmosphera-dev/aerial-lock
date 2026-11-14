import QtQuick
import Quickshell
import Quickshell.Services.Pam
import "../Modules/Lock" as LockModule
import "../Services" as Services

// Real Lock controller, synthetic PAM only. No compositor lock is requested.
ShellRoot {
    id: fixture
    settings.watchFiles: false
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
        property int result: PamResult.Failed
        signal pamMessage()
        signal completed(int result)
        signal error(int error)
        function start() {
            active = true
            responseRequired = true
            pamMessage()
            return true
        }
        function respond(answer) {
            active = false
            responseRequired = false
            completed(result)
        }
        function abort() { active = false; responseRequired = false }
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
            var lock = loader.item as LockModule.Lock
            var input = lock.context.auth.input
            var writer = input.registerWriter()
            input.claimWriter(writer)
            // Empty initial submission exposes a PAM prompt first.
            lock.tryUnlock()
            input.applyEdit(writer, input.revision, "wrong-secret-7e83", 17, 17)
            lock.tryUnlock()
            endpoint.result = PamResult.Success
            input.applyEdit(writer, input.revision, "right-secret-91ab", 17, 17)
            lock.tryUnlock()
            input.unregisterWriter(writer)
            console.log("LOCK_STATE_EXERCISED")
            Qt.quit()
        }
    }
    Timer { interval: 10000; running: true; onTriggered: Qt.exit(1) }
}
