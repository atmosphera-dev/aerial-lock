import QtQuick
import QtTest
import "../Services" as Services
import "../Modules/Lock" as LockModule

Item {
    id: root
    width: 640
    height: 480
    Component { id: contextComponent; Services.LockContext {} }
    Component {
        id: hostComponent
        LockModule.ThemeHost {
            width: 640
            height: 480
            config: null
            logger: Services.Logger
        }
    }
    TestCase {
        id: tests
        name: "AuthRecovery"
        when: windowShown
        property var context: null
        property var host: null
        property var accepted: []
        Connections {
            target: tests.context
            function onSubmitRequested(writer, generation) {
                const submission = tests.context.auth.input.consumeSubmission(writer, generation)
                if (submission !== null)
                    tests.accepted.push(submission.text)
            }
        }
        function init() {
            failOnWarning(/.*/)
            context = contextComponent.createObject(root)
            host = hostComponent.createObject(root, {context: context})
            accepted = []
        }
        function cleanup() {
            host.destroy()
            wait(0)
            context.destroy()
            wait(0)
        }
        function load(filename) {
            const lifecycle = findChild(host, "themeLifecycle")
            const loader = findChild(host, "themeCandidateLoader")
            lifecycle.phase = "candidate"
            host.allowLoading = true
            loader.setSource(Qt.resolvedUrl("fixtures/" + filename), {context: context})
            return loader
        }
        function test_failed_candidate_always_leaves_a_submittable_fallback_data() {
            return [{tag: "rejected-claim"}, {tag: "promoted-teardown"},
                    {tag: "pending-pin"}, {tag: "claim-timeout"}]
        }
        function test_failed_candidate_always_leaves_a_submittable_fallback(data) {
            const fallback = findChild(host, "authField")
            fallback.focusField()
            tryCompare(fallback, "hasActiveFocus", true)
            keyClick(Qt.Key_A)
            keyClick(Qt.Key_B)
            compare(context.auth.input.text, "ab")
            if (data.tag === "rejected-claim") {
                ignoreWarning(/ThemeHost: theme .* failed: theme vanished during handoff - using built-in panel/)
                load("RejectingHandoffTheme.qml")
            } else if (data.tag === "promoted-teardown") {
                const loader = load("HandoffAuthTheme.qml")
                tryCompare(host, "usingFallback", false)
                ignoreWarning(/ThemeHost: theme .* failed: theme vanished after promotion - using built-in panel/)
                loader.active = false
            } else {
                if (data.tag === "claim-timeout")
                    ignoreWarning(/ThemeHost: theme .* failed: theme editor did not claim the shared draft - using built-in panel/)
                load("HandoffTheme.qml")
                tryCompare(context.auth.input, "handoffState", "awaitingClaim")
                if (data.tag === "pending-pin")
                    host.fallbackPinned = true
            }
            tryCompare(host, "settled", true)
            tryCompare(host, "usingFallback", true)
            tryCompare(context.auth.input, "themeWriter", fallback.writerToken)
            fallback.focusField()
            tryCompare(fallback, "hasActiveFocus", true)
            compare(context.auth.input.text, "ab", "failure never discards the committed draft")
            keyClick(Qt.Key_C)
            compare(context.auth.input.text, "abc")
            keyClick(Qt.Key_Return)
            compare(accepted.length, 1, "visible fallback can submit after any candidate failure")
            compare(accepted[0], "abc")
            compare(context.auth.input.text, "")
        }
        function test_elective_handoff_cannot_block_active_writer_submission_data() {
            return [{tag: "outgoing", sink: false}, {tag: "surface-sink", sink: true}]
        }
        function test_elective_handoff_cannot_block_active_writer_submission(data) {
            const model = context.auth.input
            const candidate = model.registerWriter()
            const writer = data.sink ? model.registerWriter(false) : model.activeWriter
            if (data.sink)
                verify(model.transferFocus(writer))
            verify(model.applyEdit(writer, model.revision, "synthetic", 9, 9))
            // Represents a conforming or third-party editor still deferring
            // elective release. It does not compose in the active native focus.
            const fallback = findChild(host, "authField")
            fallback._gestureActive = true
            verify(model.requestHandoff(candidate))
            compare(model.handoffState, "releasing")
            context.auth.submit()
            compare(accepted.length, 1)
            compare(accepted[0], "synthetic")
        }
    }
}
