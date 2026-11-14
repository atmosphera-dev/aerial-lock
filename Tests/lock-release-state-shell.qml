import QtQuick
import Quickshell

// Loads the unmodified production shell/controller; only native state is fake.
ShellRoot {
    id: fixture
    settings.watchFiles: false
    property int unlocks: 0
    readonly property var productionShell: production.item
    readonly property string mode: Quickshell.env("RELEASE_FIXTURE_MODE")
    QtObject {
        id: endpoint
        property bool locked: true
        property bool secure: true
        property bool deferredRelease: false
        signal lockStateChanged()
        signal secureStateChanged()
        onLockedChanged: {
            if (locked) return
            if (fixture.mode === "held") { locked = true; return }
            if (fixture.mode === "secure-held") return
            if (fixture.mode === "late" && !deferredRelease) {
                deferredRelease = true
                locked = true
                lateRelease.start()
                return
            }
            secure = false
            var notifications = fixture.mode === "none" || fixture.mode === "late" ? 0 : fixture.mode === "twice" ? 2 : 1
            for (var i = 0; i < notifications; i++) {
                secureStateChanged()
                lockStateChanged()
            }
        }
    }
    Timer {
        id: lateRelease
        interval: 20
        onTriggered: endpoint.locked = false
    }
    Loader {
        id: production
        Component.onCompleted: setSource("root:/ProductionShell.qml", { sessionLockOverride: endpoint })
    }
    Connections {
        target: fixture.productionShell ? fixture.productionShell.lock : null
        function onUnlocked() {
            fixture.unlocks++
            console.info("RELEASE_FIXTURE unlocked=" + fixture.unlocks)
        }
    }
    Timer {
        interval: 1
        running: fixture.productionShell !== null && fixture.productionShell.lock !== null
        onTriggered: fixture.productionShell.lock.release()
    }
    // No actual native lock was acquired in this fixture.
    Timer { interval: 5000; running: true; onTriggered: Qt.exit(99) }
}
