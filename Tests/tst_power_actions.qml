import QtQuick
import QtTest
import "../Services" as Services

Item {
    id: root
    width: 1280
    height: 800
    property var context: null
    property var theme: null
    property var requests: []
    property int confirmations: 0
    property int cancellations: 0
    property int dismissals: 0
    Component { id: contextComponent; Services.LockContext {} }
    Component {
        id: hibernateConsumer
        Item {
            required property var context
            function activate() { if (context.power.enabled && context.power.canHibernate) context.power.requestHibernate() }
        }
    }
    Connections {
        target: root.context
        function onPowerRequested(action) {
            root.requests = root.requests.concat([action])
            root.context.power.activeAction = action
            root.context.power.pendingAction = action
            root.context.power.confirmationRequired = action === "reboot" || action === "powerOff"
            root.context.power.busy = !root.context.power.confirmationRequired
            if (root.context.power.busy) root.context.power.status = root.context.labels.powerSuspending
        }
        function onPowerConfirmRequested() {
            root.confirmations++
            root.context.power.confirmationRequired = false
            root.context.power.busy = true
            root.context.power.status = root.context.labels.powerRestarting
        }
        function onPowerCancelRequested() {
            root.cancellations++
            root.context.power.confirmationRequired = false
            root.context.power.busy = false
            root.context.power.pendingAction = ""
            root.context.power.activeAction = ""
            root.theme.focusInput()
        }
        function onPowerErrorDismissRequested() {
            root.dismissals++
            root.context.power.error = ""
            root.context.power.activeAction = ""
        }
    }
    TestCase {
        id: tc
        name: "ThemePowerActions"
        when: windowShown
        function init() {
            root.context = createTemporaryObject(contextComponent, root)
            root.context.session.loginName = "tester"
            root.context.auth.status = root.context.labels.prompt
            root.context.power.enabled = true
            root.context.power.canReboot = true
            root.context.power.canPowerOff = true
            root.context.power.canSuspend = true
            root.requests = []
            root.confirmations = root.cancellations = root.dismissals = 0
            root.Window.window.requestActivate()
        }
        function cleanup() {
            if (root.theme) { root.theme.destroy(); root.theme = null; wait(0) }
            root.context = null
        }
        function rows() {
            var result = []
            for (var name of ["Windows7", "MaterialYou", "Enfield", "Clockwork"]) {
                for (var width of [1280, 492]) result.push({tag: name + "-" + width, name: name, width: width})
            }
            return result
        }
        function load(data) {
            root.width = data.width
            root.height = data.width === 492 ? 768 : 800
            var component = Qt.createComponent("../Themes/" + data.name + ".qml")
            compare(component.status, Component.Ready, component.errorString())
            root.theme = component.createObject(root, {context: root.context, width: root.width, height: root.height})
            verify(root.theme !== null)
            root.theme.focusInput()
            wait(150)
        }
        function item(name) {
            var result = findChild(root.theme, name)
            verify(result !== null, "missing " + name)
            return result
        }
        function powerNames(name) {
            return name === "MaterialYou" ? ["powerTile", "rebootTile", "sleepTile"]
                    : name === "Clockwork" ? ["rebootAction", "shutdownAction"] : ["restartAction", "shutdownAction"]
        }
        function rebootName(name) { return name === "MaterialYou" ? "rebootTile" : name === "Clockwork" ? "rebootAction" : "restartAction" }
        function noNormalControls(name) {
            for (var control of powerNames(name)) {
                var object = findChild(root.theme, control)
                verify(object === null || !object.visible, control + " must not remain during active state")
            }
        }
        function inside(object) {
            tryVerify(function() {
                var start = object.mapToItem(root.theme, 0, 0)
                var end = object.mapToItem(root.theme, object.width, object.height)
                return object.width > 0 && object.height > 0 && start.x >= -0.75 && start.y >= -0.75
                        && end.x <= root.width + 0.75 && end.y <= root.height + 0.75
            }, 1000, object.objectName + " has a settled nonempty hit area inside the lock surface")
        }
        function test_confirm_wait_failure_data() { return rows() }
        function test_confirm_wait_failure(data) {
            load(data)
            keyClick(Qt.Key_A)
            tryCompare(root.context.auth.input, "text", "a")
            var control = item(rebootName(data.name))
            mouseClick(control, control.width / 2, control.height / 2)
            compare(root.requests.join(), "reboot")
            tryCompare(item("powerCancel"), "activeFocus", true)
            noNormalControls(data.name)
            compare(root.context.auth.input.text, "a", "confirmation never consumes the response")
            inside(item("powerCancel")); inside(item("powerConfirm")); inside(item("powerMessage"))
            if (data.name === "Windows7" || data.name === "Enfield") {
                var area = item("powerRow")
                if (!area.stacked)
                    tryCompare(item("powerMessage"), "lineCount", 1, 1000, "a fitting question stays on one line")
            }
            compare(item("authField").hasActiveFocus, false)
            keyClick(Qt.Key_Escape)
            compare(root.cancellations, 1)
            compare(root.confirmations, 0)
            control = item(rebootName(data.name))
            control.forceActiveFocus()
            keyPress(Qt.Key_Return); keyRelease(Qt.Key_Return)
            tryCompare(item("powerCancel"), "activeFocus", true)
            compare(root.confirmations, 0, "opening key release cannot confirm")
            keyClick(Qt.Key_Tab)
            tryCompare(item("powerConfirm"), "activeFocus", true)
            keyClick(Qt.Key_Return)
            compare(root.confirmations, 1)
            noNormalControls(data.name)
            inside(item("powerMessage"))
            if (data.name === "Windows7" || data.name === "Enfield") {
                verify(item("powerProgress").visible)
                inside(item("powerProgress"))
            }
            verify(findChild(root.theme, "powerConfirm") === null || !findChild(root.theme, "powerConfirm").visible)
            root.context.power.busy = false
            root.context.power.error = "Restart is blocked by <b>Firefox</b> %3: <img src='invalid'>"
            root.theme.focusInput()
            wait(0) // settle the error state's RowLayouts before probing/clicking
            tryCompare(item("authField"), "hasActiveFocus", true)
            noNormalControls(data.name)
            var message = item("powerMessage")
            compare(message.text, root.context.power.error)
            compare(message.textFormat, Text.PlainText)
            inside(message); inside(item("powerDismiss"))
            var dismiss = item("powerDismiss")
            mouseClick(dismiss, dismiss.width / 2, dismiss.height / 2)
            compare(root.dismissals, 1)
            for (var name of powerNames(data.name)) verify(item(name).visible)
        }
        function test_hide_unavailable_data() { return rows() }
        function test_hide_unavailable(data) {
            root.context.power.canReboot = false
            load(data)
            var reboot = findChild(root.theme, rebootName(data.name))
            verify(reboot === null || !reboot.visible)
            root.context.power.enabled = false
            wait(0)
            for (var name of powerNames(data.name)) {
                var control = findChild(root.theme, name)
                verify(control === null || !control.visible)
            }
            compare(root.requests.length, 0)
        }
        function test_material_grid_closes_up() {
            load({name: "MaterialYou", width: 1280})
            var grid = item("powerArea")
            var fullHeight = grid.height
            verify(Math.abs(item("sleepTile").width - grid.width) < 0.75, "lone tile fills the grid within Qt's pixel rounding")
            root.context.power.canReboot = false
            wait(0)
            verify(grid.height < fullHeight)
            verify(item("powerTile").width < grid.width)
            compare(item("powerTile").height, item("sleepTile").height)
            var power = item("powerTile")
            var sleep = item("sleepTile")
            tryVerify(function() { return Math.abs(power.mapToItem(grid, 0, 0).y - sleep.mapToItem(grid, 0, 0).y) < 0.75 },
                      1000, "two available tiles occupy the same settled row")
        }
        function test_hibernate_context_consumer() {
            var consumer = createTemporaryObject(hibernateConsumer, root, {context: root.context})
            consumer.activate()
            compare(root.requests.length, 0)
            root.context.power.canHibernate = true
            consumer.activate()
            compare(root.requests.join(), "hibernate")
            compare(root.context.power.confirmationRequired, false)
        }
        function test_translated_literal_question_data() { return rows() }
        function test_translated_literal_question(data) {
            root.context.labels.powerConfirmQuestion = "Sure <b>%1</b>?"
            root.context.labels.powerCancel = "Cancel <i>"
            load(data)
            var control = item(rebootName(data.name))
            mouseClick(control, control.width / 2, control.height / 2)
            var message = item("powerMessage")
            compare(message.text, data.name === "MaterialYou" ? "SURE <B>%1</B>?" : "Sure <b>%1</b>?")
            compare(message.textFormat, Text.PlainText)
            tryCompare(item("powerCancel"), "activeFocus", true)
            inside(message); inside(item("powerCancel")); inside(item("powerConfirm"))
        }
    }
}
