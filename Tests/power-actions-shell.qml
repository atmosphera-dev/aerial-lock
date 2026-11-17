import QtQuick
import Quickshell
import AerialLock.Native
import "../Modules/Lock" as LockModule
import "../Services" as Services

// Real controller/context and native service on a private peer bus. Only the
// PAM and compositor boundaries are synthetic; no real lock is acquired.
ShellRoot {
    id: fixture
    settings.watchFiles: false
    required property var power
    property var checks: []
    property int unlocks: 0
    property int releases: 0
    property int stage: 0
    property int writer: -1
    property var steps: []
    function require(value, name) {
        if (!value) { console.error("POWER_CONTROLLER_FAIL " + name); Qt.exit(1); throw Error(name) }
        checks.push(name)
    }
    function configure(value) { power.control("Configure", JSON.stringify(value || {})) }
    function count(method) {
        return JSON.parse(power.control("Events", "{}")).filter(function(event) { return event.method === method }).length
    }
    function signalLifecycle(sleep, value) {
        power.control("Signal", JSON.stringify({name: sleep ? "PrepareForSleep" : "PrepareForShutdown", value: value}))
    }
    Services.ConfigStore { id: cfg }
    QtObject {
        id: lockBoundary
        property bool locked: true
        property bool secure: locked
        signal secureStateChanged()
        signal lockStateChanged()
    }
    QtObject {
        id: pam
        property bool active: false
        property string message: ""
        property bool messageIsError: false
        property bool responseRequired: false
        property bool responseVisible: false
        property string user: "synthetic-fixture"
        property int starts: 0
        property int responses: 0
        property int aborts: 0
        signal pamMessage()
        signal completed(int result)
        signal error(int error)
        function start() { starts++; active = true; return true }
        function respond(answer) { responses++ }
        function abort() {
            aborts++
            if (active) { active = false; responseRequired = false; completed(AuthContext.Success) }
        }
    }
    Loader {
        id: loader
        active: cfg.ready
        sourceComponent: Component {
            LockModule.Lock {
                config: cfg
                logger: Services.Logger
                sessionLockOverride: lockBoundary
                pamEndpoint: pam
                powerEndpoint: power
                onUnlocked: fixture.unlocks++
                onUnlockRequested: fixture.releases++
            }
        }
        onLoaded: {
            configure()
            steps = [
                function() {
                    if (!power.canReboot || !power.canHibernate) return false
                    var lock = loader.item as LockModule.Lock
                    writer = lock.context.auth.input.registerWriter()
                    lock.context.auth.input.claimWriter(writer)
                    lock.context.auth.input.applyEdit(writer, lock.context.auth.input.revision, "queued-synthetic", 16, 16)
                    lock.tryUnlock()
                    require(pam.starts === 1 && lock.unlockInProgress, "in-flight-auth")
                    lock.context.auth.input.applyEdit(writer, lock.context.auth.input.revision, "retained-draft", 4, 9)
                    lock.context.power.requestReboot()
                    require(power.confirmationRequired && !lock.unlockInProgress, "power-cancels-pam-before-sync-success")
                    require(unlocks === 0 && releases === 0 && lock.locked, "late-auth-success-cannot-unlock")
                    require(lock.acceptedResponse === null && pam.responses === 0, "queued-response-retired")
                    require(lock.context.auth.input.text === "retained-draft", "editable-draft-retained")
                    require(lock.context.auth.input.selectionStart === 4 && lock.context.auth.input.selectionEnd === 9, "selection-retained")
                    lock.tryUnlock(); lock.context.auth.submit()
                    require(pam.starts === 1 && pam.responses === 0, "confirmation-blocks-submit")
                    lock.context.power.cancel()
                    require(count("RebootWithFlags") === 0, "cancel-no-dispatch")
                    configure({inhibitors: [{what: "shutdown", mode: "block", who: "<b>Firefox</b> %3", why: "<img> %2"}]})
                    lock.context.power.requestReboot(); lock.context.power.confirm()
                    return true
                },
                function() {
                    if (power.errorCode !== "inhibited" || !power.blockerWho) return false
                    var lock = loader.item as LockModule.Lock
                    require(lock.context.power.error === "Restart is blocked by <b>Firefox</b> %3: <img> %2", "literal-blocker-projection")
                    require(!lock.context.power.interactionActive && lock.locked, "failure-restores-auth-with-lock")
                    lock.context.power.clearError()
                    configure()
                    lock.context.power.requestHibernate()
                    require(!power.confirmationRequired, "hibernate-immediate")
                    return true
                },
                function() {
                    if (power.state !== "waiting") return false
                    var lock = loader.item as LockModule.Lock
                    require(count("HibernateWithFlags") === 1 && lock.locked && unlocks === 0 && releases === 0, "accepted-action-never-releases")
                    require(lock.context.power.status === "Hibernating...", "action-specific-status")
                    require(pam.starts === 1 && pam.responses === 0, "power-never-authenticates")
                    signalLifecycle(true, true); signalLifecycle(true, false)
                    return true
                },
                function() {
                    if (power.state !== "idle") return false
                    var lock = loader.item as LockModule.Lock
                    require(lock.context.auth.input.text === "retained-draft" && lock.locked, "resume-retains-lock-and-draft")
                    configure({hold: "SuspendWithFlags"})
                    lock.context.power.requestSuspend()
                    return true
                },
                function() {
                    if (count("SuspendWithFlags") !== 1) return false
                    configure({hold: "ListInhibitors",
                               faults: {SuspendWithFlags: "org.freedesktop.login1.BlockedByInhibitorLock"},
                               inhibitors: [{what: "sleep", mode: "block", who: "Firefox", why: "Downloading file"}]})
                    power.control("Release", "{}")
                    return true
                },
                function() {
                    if (count("ListInhibitors") !== 1) return false
                    var lock = loader.item as LockModule.Lock
                    require(power.errorCode === "inhibited" && !lock.context.power.interactionActive,
                            "dispatch-rejection-restores-input-before-detail")
                    require(lock.locked && unlocks === 0 && releases === 0 && pam.starts === 1,
                            "dispatch-rejection-does-not-authenticate-or-release")
                    lock.tryUnlock()
                    require(pam.starts === 2 && lock.unlockInProgress, "independent-auth-allowed-while-detail-held")
                    lock.context.power.requestReboot()
                    require(power.confirmationRequired && !lock.unlockInProgress && unlocks === 0 && releases === 0,
                            "new-intent-cancels-independent-pam-safely")
                    power.control("Release", "{}")
                    return true
                },
                function() {
                    var lock = loader.item as LockModule.Lock
                    require(power.confirmationRequired && !power.errorCode && !power.blockerWho,
                            "late-detail-cannot-replace-new-controller-intent")
                    lock.context.power.cancel()
                    configure({hold: "ListInhibitors"})
                    lock.context.power.requestSuspend()
                    return true
                },
                function() {
                    if (count("ListInhibitors") !== 1) return false
                    var lock = loader.item as LockModule.Lock
                    lock.release()
                    power.control("Release", "{}")
                    return true
                },
                function() {
                    require(count("SuspendWithFlags") === 0, "external-release-retires-preflight")
                    require(unlocks === 1 && releases === 1, "external-release-semantics-unchanged")
                    console.log("POWER_CONTROLLER_PASS " + JSON.stringify(checks))
                    Qt.quit()
                    return true
                }
            ]
            drive.start()
        }
    }
    Timer {
        id: drive
        interval: 10
        repeat: true
        onTriggered: { if (stage < steps.length && steps[stage]()) stage++ }
    }
    Timer { interval: 15000; running: true; onTriggered: { console.error("POWER_CONTROLLER_TIMEOUT " + stage); Qt.exit(1) } }
}
