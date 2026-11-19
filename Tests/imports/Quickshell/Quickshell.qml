pragma Singleton
import QtQml

QtObject {
    readonly property string shellDir: ""
    property var testEnvironment: null
    function env(name) {
        if (testEnvironment !== null)
            return testEnvironment[name] || ""
        throw new Error("Quickshell environment access is outside the handoff test")
    }
}
