import QtQuick
import QtTest
import AerialLock.SettingsTest
import "../qml"
TestCase {
    id: test
    name: "SettingsPages"
    width: 672
    height: 1500
    visible: true
    when: windowShown
    property int currentPage: 0
    Cache { id: cache; anchors.fill: parent; visible: test.currentPage === 0; controller: Harness }
    Lock { id: lock; anchors.fill: parent; visible: test.currentPage === 1; controller: Harness }
    Sources { id: sources; anchors.fill: parent; visible: test.currentPage === 2; controller: Harness }
    About { id: about; anchors.fill: parent; visible: test.currentPage === 3; controller: Harness }
    SourceSheet { id: sheet; sources: Harness.sources; parent: test }
    CleanupSheet { id: cleanupSheet; cache: Harness.cache; parent: test }
    SignalSpy { id: trayIntent; target: Harness; signalName: "trayChangeRequested" }
    function test_aboutTrayIntentAndNoHost() {
        currentPage = 3;
        const toggle = findChild(about, "about-tray");
        verify(!toggle.visible);
        Harness.configureTray(true, false);
        verify(waitForPolish(test.Window.window));
        verify(toggle.visible);
        trayIntent.clear(); mouseClick(toggle);
        compare(trayIntent.count, 1); compare(trayIntent.signalArguments[0][0], true);
        Harness.configureTray(true, true);
        verify(waitForPolish(test.Window.window));
        trayIntent.clear(); mouseClick(toggle);
        compare(trayIntent.count, 1); compare(trayIntent.signalArguments[0][0], false);
        Harness.configureTray(false, false);
        verify(waitForPolish(test.Window.window)); verify(!toggle.visible);
        Harness.configureTray(false, true);
        verify(waitForPolish(test.Window.window)); verify(toggle.visible);
        trayIntent.clear(); mouseClick(toggle); compare(trayIntent.count, 1); compare(trayIntent.signalArguments[0][0], false);
        Harness.configureTray(false, false, true);
        verify(waitForPolish(test.Window.window)); verify(toggle.visible);
        trayIntent.clear(); mouseClick(toggle); compare(trayIntent.count, 1); compare(trayIntent.signalArguments[0][0], false);
        Harness.configureTray(false, false);
    }
    function test_allPagesConstruct() {
        for (let i = 0; i < 4; ++i) {
            currentPage = i;
            verify(waitForPolish(test.Window.window));
            verify([cache, lock, sources, about][i].contentHeight > 0);
            const title = findChild([cache, lock, sources, about][i], "pageTitle");
            const heading = findChild([cache, lock, sources, about][i], "pageHeading");
            verify(title.y >= 0, "page title must be inside its heading band");
            fuzzyCompare(title.y + title.height / 2, heading.height / 2, 1);
        }
        compare(Harness.themes.rows.length, 5);
    }
    function test_cacheChoices() {
        currentPage = 0;
        for (const key of ["cache.maxBytes", "cache.rotationDays"]) {
            const select = findChild(cache, key);
            verify(select !== null);
            if (Harness.controls[key].selected >= Harness.controls[key].choices.length - 1) {
                Harness.externalEdit(key, JSON.stringify(Harness.controls[key].choices[0].value));
            }
            tryVerify(() => Harness.controls[key].selected < Harness.controls[key].choices.length - 1);
            const before = Harness.controls[key].selected;
            const next = Harness.controls[key].choices[before + 1];
            select.forceActiveFocus(Qt.TabFocusReason);
            keyClick(Qt.Key_Space);
            tryCompare(select.popup, "visible", true);
            keyClick(Qt.Key_Down);
            keyClick(Qt.Key_Return);
            tryVerify(() => !Harness.controls[key].pending);
            compare(Harness.controls[key].error, "");
            compare(JSON.stringify(Harness.saved(key)), JSON.stringify(next.value));
            compare(select.currentValue, next.token);
        }
    }
    function test_lockSwitches() {
        currentPage = 1;
        for (const data of [{ row: "powerActions", key: "powerActions.enabled" }, { row: "idle-respectInhibitors", key: "idle.respectInhibitors" }, { row: "idle-lockOnSuspend", key: "idle.lockOnSuspend" }, { row: "idle-logindLockUnlock", key: "idle.logindLockUnlock" }]) {
            verify(waitForPolish(test.Window.window));
            const row = findChild(lock, data.row);
            const toggle = findChild(row, "toggle");
            const desired = !Harness.controls[data.key].on;
            mouseClick(toggle);
            tryVerify(() => !Harness.controls[data.key].pending && Harness.controls[data.key].on === desired);
            compare(Harness.saved(data.key), desired);
        }
    }
    function test_pageGeometry_data() { return [{ tag: "reference", width: 672 }, { tag: "compact", width: 432 }, { tag: "wide", width: 992 }]; }
    function test_pageGeometry(data) {
        test.width = data.width;
        for (const pair of [{ page: cache, index: 0, name: "cache.maxBytes" }, { page: lock, index: 1, name: "idleTimeout" }]) {
            currentPage = pair.index;
            verify(waitForPolish(test.Window.window));
            const select = findChild(pair.page, pair.name);
            const icon = findChild(select, "chevron");
            const point = icon.mapToItem(select, 0, 0);
            fuzzyCompare(point.y, select.height - point.y - icon.height, 1);
            fuzzyCompare(point.y, select.width - point.x - icon.width, 1);
        }
        test.width = 672;
    }
    function test_sheetDraftSurvivesAndCloses() {
        currentPage = 2;
        Harness.sources.begin("");
        tryCompare(sheet, "visible", true);
        const name = findChild(findChild(sheet, "sourceName"), "input");
        name.forceActiveFocus();
        keyClick(Qt.Key_A); keyClick(Qt.Key_B); keyClick(Qt.Key_C);
        compare(Harness.sources.draft.name, "abc");
        Harness.sources.field("source", "not a URL"); Harness.sources.save();
        verify(Harness.sources.sheetOpen);
        verify(Harness.sources.draft.error.length > 0);
        compare(name.text, "abc");
        keyClick(Qt.Key_Escape); tryVerify(() => !Harness.sources.sheetOpen);
        compare(Harness.sources.rows.length, 0);
    }
    function test_pendingSourceSaveDisablesEditors() {
        currentPage = 2;
        const previous = Harness.controls["cache.rotationDays"].value;
        Harness.sources.begin(""); Harness.sources.field("name", "Pending source"); Harness.sources.field("source", "https://example.org/list.json");
        try {
            Harness.externalEdit("cache.rotationDays", JSON.stringify(previous === 1 ? 7 : 1));
            Harness.sources.save(); verify(Harness.sources.draft.pending);
            for (const name of ["sourceName", "sourceAddress", "sourceFormat", "sourceQuality"]) verify(!findChild(sheet, name).enabled, name);
            tryVerify(() => !Harness.sources.draft.pending);
            verify(Harness.sources.sheetOpen); verify(Harness.sources.draft.error.length > 0);
            for (const name of ["sourceName", "sourceAddress", "sourceFormat", "sourceQuality"]) verify(findChild(sheet, name).enabled, name);
        } finally {
            tryVerify(() => !Harness.sources.draft.pending); Harness.sources.dismiss();
            Harness.externalEdit("cache.rotationDays", JSON.stringify(previous));
        }
    }
    function test_sourceSelectorsHaveAccessibleNames() {
        Harness.sources.begin("");
        try {
            compare(findChild(sheet, "sourceFormat").Accessible.name, "Format");
            compare(findChild(sheet, "sourceQuality").Accessible.name, "Highest quality");
        } finally { Harness.sources.dismiss(); }
    }
    function test_cleanupSheetTakesKeyboardFocus() {
        currentPage = 0;
        Harness.cache.prepareCleanup();
        try {
        tryCompare(cleanupSheet, "visible", true);
        tryCompare(cleanupSheet, "activeFocus", true);
        tryVerify(() => Harness.cache.cleanup.state === "confirm");
        } finally {
        cleanupSheet.close();
        }
        tryCompare(cleanupSheet, "visible", false);
    }
    function test_sheetOwnerLossAndMalformedFile() {
        currentPage = 2;
        Harness.sources.begin(""); Harness.sources.field("name", "Keep my draft");
        Harness.sources.field("source", "https://example.org/a.json");
        let stopped = false;
        let broken = false;
        try {
            stopped = true; Harness.stopService(); tryCompare(Harness, "panel", "unavailable");
            compare(Harness.sources.draft.name, "Keep my draft"); verify(sheet.visible);
            Harness.startService(); stopped = false; tryCompare(Harness, "panel", "settings");
            Harness.sources.save(); verify(Harness.sources.draft.error.length > 0); compare(Harness.sources.draft.name, "Keep my draft");
            broken = true; Harness.breakConfig(); tryCompare(Harness, "panel", "diagnostic");
            compare(Harness.sources.draft.name, "Keep my draft"); verify(sheet.visible); verify(Harness.brokenBytesUnchanged());
        } finally {
            try { if (broken) Harness.repairConfig(); } finally {
                try { if (stopped) Harness.startService(); } finally { Harness.sources.dismiss(); }
            }
        }
        tryCompare(Harness, "panel", "settings");
        tryVerify(() => !Harness.sources.sheetOpen);
    }
    function test_sourceDragAndSwitch() {
        currentPage = 2;
        Harness.externalEdit("sources", JSON.stringify([{ id: "one", type: "folder", name: "One", source: "/home/tester/Videos/one" }, { id: "two", type: "folder", name: "Two", source: "/home/tester/Videos/two" }]));
        try {
            tryVerify(() => Harness.sources.rows.length === 2);
            verify(waitForPolish(test.Window.window));
            const first = findChild(sources, "source-one");
            const second = findChild(sources, "source-two");
            const grip = findChild(second, "sourceGrip");
            const drop = first.mapToItem(test, 24, first.height / 2);
            mousePress(grip, grip.width / 2, grip.height / 2);
            mouseMove(test, drop.x, drop.y, 100);
            mouseRelease(test, drop.x, drop.y);
            tryVerify(() => Harness.sources.rows[0].id === "two");
            compare(Harness.saved("sources")[0].id, "two");
            tryVerify(() => !Harness.sources.busy && findChild(sources, "source-two").enabled);
            verify(waitForPolish(test.Window.window)); // reordered rows are new delegates
            const toggle = findChild(findChild(sources, "source-two"), "sourceToggle");
            mouseClick(toggle); tryVerify(() => !Harness.sources.rows[0].enabled);
            compare(Harness.saved("sources")[0].enabled, false);
        } finally {
            Harness.externalEdit("sources", "[]"); tryVerify(() => Harness.sources.rows.length === 0);
        }
    }
}
