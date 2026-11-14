import QtQuick
import QtTest as Test
import Quickshell
import "../Modules/Lock" as LockModule
import "../Services" as Services

// Real controller + editor, using public Qt key events. No session lock or
// authentication attempt: this fixture tests the complete status projection.
ShellRoot {
    id: fixture
    settings.watchFiles: false
    property bool failed: false
    property int assertions: 0
    property int turns: 0
    readonly property LockModule.Lock lock: lockLoader.item as LockModule.Lock
    readonly property LockModule.AuthField editor: editorLoader.item as LockModule.AuthField
    function check(condition, name) {
        assertions++
        if (!condition) {
            failed = true
            console.error("PASSWORD_LIMIT_ASSERTION_FAILED " + name)
        }
    }
    Services.ConfigStore { id: fixtureConfig }
    Test.TestEvent { id: keys }
    Loader {
        id: lockLoader
        active: fixtureConfig.ready
        sourceComponent: Component {
            LockModule.Lock {
                config: fixtureConfig
                logger: Services.Logger
                locked: false
            }
        }
    }
    Window {
        id: testWindow
        width: 320
        height: 100
        visible: fixture.lock !== null
        Loader {
            id: editorLoader
            active: fixture.lock !== null
            sourceComponent: Component {
                LockModule.AuthField {
                    context: fixture.lock.context
                    width: 280
                    height: 40
                }
            }
        }
    }
    Timer {
        interval: 20
        repeat: true
        running: true
        onTriggered: {
            if (!fixture.lock || !testWindow.active || !fixture.editor) {
                if (++fixture.turns > 200) {
                    console.error("PASSWORD_LIMIT_NOT_EXERCISED")
                    Qt.exit(1)
                }
                if (testWindow.visible) testWindow.requestActivate()
                return
            }
            stop()
            fixtureConfig.pamLimits.maxResponseSize = 5 // synthetic small cap
            fixture.editor.focusField()
            var input = fixture.lock.context.auth.input
            var prompt = fixture.lock.context.auth.status
            for (var i = 0; i < 4; i++)
                fixture.check(keys.keyClick(Qt.Key_A, Qt.NoModifier, 0), "native key delivered")
            fixture.check(input.text === "aaaa", "exact fit retained")
            fixture.check(fixture.lock.context.auth.status === prompt, "exact fit has no hint")
            fixture.check(keys.keyClick(Qt.Key_B, Qt.NoModifier, 0), "overflow key delivered")
            fixture.check(input.text === "aaaa", "overflow rejected whole")
            fixture.check(fixture.lock.context.auth.status === "Password too long (maximum 4 characters)", "overflow reaches status")
            fixture.check(keys.keyClick(Qt.Key_Left, Qt.NoModifier, 0), "navigation key delivered")
            fixture.check(fixture.lock.context.auth.status === "Password too long (maximum 4 characters)", "navigation does not clear hint")
            fixture.check(keys.keyClick(Qt.Key_Backspace, Qt.NoModifier, 0), "Backspace delivered")
            fixture.check(input.text === "aaa", "Backspace edits draft")
            fixture.check(fixture.lock.context.auth.status === prompt, "Backspace clears hint")
            console.log("PASSWORD_LIMIT_OUTCOME assertions=" + assertions + " failed=" + failed)
            Qt.exit(failed ? 1 : 0)
        }
    }
}
