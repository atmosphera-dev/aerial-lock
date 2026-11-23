import QtQuick
import QtQuick.Templates as T
import QtTest
import AerialLock.SettingsTest
import InterActive
import InterActive.Foundation as IA
import "../qml/revamp" as UI

TestCase {
    id: test
    name: "RevampSources"
    width: 920
    height: 760
    visible: true
    when: windowShown
    property bool reference: false
    property var referenceDraft: ({ type: "community", name: "Drone flights", source: "https://example.org/drones/manifest.json",
        qualityChoices: [{ label: "Same as Downloads", value: 0 }, { label: "1080p", value: 1080 }, { label: "4K", value: 2160 }],
        qualitySelected: 0, qualityInherited: true })
    readonly property var originals: [
        { id: "one", name: "One <plain>", type: "folder", source: "/home/tester/Videos/one", enabled: false, future: { kept: 17 } },
        { id: "two", name: "Two", type: "file", source: "/home/tester/Videos/two.mp4" },
        { id: "three", name: "Three", type: "community", source: "http://127.0.0.1:1/pack.json", maxResolution: 1080 }
    ]
    UI.Shell {
        id: shell
        anchors.fill: parent
        currentPage: "sources"
        pages: [
            { key: "overview", title: "Overview", icon: Qt.resolvedUrl("../qml/revamp/icons/sky.svg") },
            { key: "sources", title: "Sources", icon: Qt.resolvedUrl("../qml/revamp/icons/stack.svg") },
            { key: "downloads", title: "Downloads", icon: Qt.resolvedUrl("../qml/revamp/icons/download.svg") },
            { key: "lock", title: "Lock screen", icon: Qt.resolvedUrl("../qml/revamp/icons/lock.svg") },
            { key: "about", title: "About", icon: Qt.resolvedUrl("../qml/revamp/icons/info.svg") }
        ]
        UI.SourcesPage {
            id: page
            anchors.fill: parent
            sourceModel: test.reference ? Harness.referenceSources : Harness.sources.rowModel
            busy: !test.reference && Harness.sources.busy
            error: test.reference ? "" : Harness.sources.error
            onAddRequested: Harness.sources.begin("")
            onToggled: (id, on) => Harness.sources.toggle(id, on)
            onEditRequested: id => Harness.sources.begin(id)
            onRemoveRequested: id => Harness.sources.remove(id)
            onCheckRequested: id => Harness.sources.retry(id)
            onOrderStarted: id => { if (!test.reference) Harness.sources.beginReorder(id); }
            onMoveRequested: (id, delta) => Harness.sources.move(id, delta)
            onDroppedBefore: id => Harness.sources.dropBefore(id)
        }
    }
    UI.AddSourceDialog {
        id: dialog
        parent: test
        visible: Harness.sources.sheetOpen
        draft: Harness.sources.draft
        appleAvailable: Harness.sources.appleAvailable
        onFieldEdited: (key, value) => Harness.sources.field(key, value)
        onChoosePathRequested: Harness.sources.choosePath()
        onSaveRequested: Harness.sources.save()
        onDismissed: Harness.sources.dismiss()
    }
    UI.AddSourceDialog { id: mockDialog; parent: test; draft: test.referenceDraft }
    SignalSpy { id: chooseIntent; target: dialog; signalName: "choosePathRequested" }
    Component {
        id: isolatedRow
        UI.SourceRow {
            x: 237; y: 350; width: 620
            source: ({ id: "isolated", name: "Source", description: "Folder", glyph: "folder", enabled: false,
                problem: false, pathDetail: false, canRetry: false, canMoveUp: false, canMoveDown: false })
        }
    }
    SignalSpy { id: toggleIntent; signalName: "toggled" }
    function resize(w, h) {
        test.width = w; test.height = h;
        verify(Harness.resizeSurface(shell, w, h));
    }
    function settled() { tryVerify(() => !Harness.sources.busy); verify(waitForPolish(test.Window.window)); }
    function row(id) { return findChild(page, "source-" + id); }
    function action(id, name) { return findChild(row(id), name); }
    function menu(id) { return action(id, "sourceMenu"); }
    function openMenu(id) {
        const more = action(id, "sourceMore");
        more.forceActiveFocus(Qt.TabFocusReason); keyClick(Qt.Key_Space);
        tryCompare(menu(id), "opened", true);
    }
    function init() {
        reference = false;
        resize(920, 760); verify(Harness.appearance(false));
        Harness.externalEdit("sources", JSON.stringify(originals));
        tryVerify(() => Harness.sources.rows.length === 3 && Harness.sources.rows[0].id === "one");
        settled(); page.viewport.contentY = 0;
    }
    function cleanup() {
        mockDialog.close();
        for (const id of ["one", "two", "three", "apple", "community", "folder", "drone"]) {
            const current = row(id); if (current) findChild(current, "sourceMenu").close();
        }
        settled(); Harness.sources.dismiss();
        Harness.externalEdit("sources", "[]");
        tryVerify(() => Harness.sources.rows.length === 0);
    }
    function cleanupTestCase() { Harness.releaseSurface(shell); }
    function test_keyboardMovesKeepDelegateAndFocus() {
        const first = row("one"), more = action("one", "sourceMore");
        openMenu("one");
        compare(action("one", "sourceMoveUp").visible, false);
        const down = action("one", "sourceMoveDown");
        down.forceActiveFocus(Qt.TabFocusReason); keyClick(Qt.Key_Return);
        settled(); tryVerify(() => Harness.sources.rows[1].id === "one");
        compare(Harness.saved("sources")[1].id, "one");
        compare(row("one"), first); tryCompare(more, "activeFocus", true);
        compare(Harness.saved("sources")[1].future.kept, 17);
        compare(Harness.saved("sources")[1].enabled, false);
        openMenu("one");
        action("one", "sourceMoveUp").forceActiveFocus(Qt.TabFocusReason); keyClick(Qt.Key_Return);
        settled(); compare(Harness.saved("sources")[0].id, "one");
        compare(row("one"), first); tryCompare(more, "activeFocus", true);
        openMenu("three"); compare(action("three", "sourceMoveDown").visible, false);
    }
    function test_cliUpdatesKeepOpenMenuIdentityAndRejectStaleMove() {
        const second = row("two"); openMenu("two");
        Harness.externalEdit("sources", JSON.stringify([originals[2], originals[0], originals[1]]));
        tryVerify(() => Harness.sources.rows[0].id === "three");
        compare(row("two"), second); compare(menu("two").opened, true);
        verify(action("two", "sourceMoveUp").visible);
        action("two", "sourceMoveUp").forceActiveFocus(Qt.TabFocusReason); keyClick(Qt.Key_Return);
        settled(); tryVerify(() => Harness.sources.error.length > 0);
        compare(Harness.saved("sources")[0].id, "three");
        compare(Harness.saved("sources")[2].id, "two");
    }
    function test_dragAndToggleRoundTrip() {
        const second = row("two"), first = row("one"), grip = findChild(second, "sourceGrip");
        const drop = first.mapToItem(test, 24, first.height / 2);
        mousePress(grip, grip.width / 2, grip.height / 2);
        mouseMove(test, drop.x, drop.y, 100); mouseRelease(test, drop.x, drop.y);
        tryVerify(() => Harness.sources.rows[0].id === "two"); settled();
        compare(Harness.saved("sources")[0].id, "two"); compare(row("two"), second);
        mouseClick(action("two", "sourceToggle"));
        tryVerify(() => !Harness.sources.rows[0].enabled); settled();
        compare(Harness.saved("sources")[0].enabled, false);
    }
    function test_rejectedToggleStillShowsAppliedValue() {
        const item = createTemporaryObject(isolatedRow, test);
        verify(item !== null); toggleIntent.target = item; toggleIntent.clear();
        verify(waitForPolish(test.Window.window));
        const toggle = findChild(item, "sourceToggle");
        mouseClick(toggle);
        compare(toggleIntent.count, 1); compare(toggleIntent.signalArguments[0][0], true);
        compare(toggle.checked, item.source.enabled, "A rejected or pending intent is not an applied value");
        toggleIntent.target = null;
    }
    function test_dragCancellationDoesNotWrite() {
        const second = row("two"), first = row("one"), grip = findChild(second, "sourceGrip");
        const before = JSON.stringify(Harness.saved("sources"));
        Harness.observeTouch(second);
        const sequence = touchEvent(test);
        sequence.press(0, grip, grip.width / 2, grip.height / 2).commit();
        sequence.move(0, grip, grip.width / 2, grip.height + 20).commit();
        sequence.move(0, first, 18, first.height / 2).commit();
        Harness.cancelTouch(second);
        sequence.release(0, first, 18, first.height / 2).commit();
        settled(); compare(JSON.stringify(Harness.saved("sources")), before);
    }
    function test_nativeSourceArtwork() {
        if (!Harness.nativeRendering(shell)) return;
        reference = true; verify(waitForPolish(test.Window.window));
        test.Window.window.update(); verify(waitForRendering(shell));
        for (const id of ["apple", "community", "folder", "drone"]) {
            tryVerify(() => Harness.inkPixels(Harness.iconIn(action(id, "sourceKind"))) > 8, 2000, id + " kind icon renders");
            tryVerify(() => Harness.inkPixels(Harness.iconIn(action(id, "sourceGrip"))) > 3, 2000, id + " grip renders");
            tryVerify(() => Harness.inkPixels(Harness.iconIn(action(id, "sourceMore"))) > 3, 2000, id + " menu icon renders");
        }
        tryVerify(() => Harness.inkPixels(Harness.iconIn(findChild(page, "addSource"))) > 4, 2000, "Add icon renders");
    }
    function test_sourceCopyIsNotElidedExceptPaths() {
        reference = true;
        for (const size of [{ w: 920, h: 760 }, { w: 640, h: 480 }]) {
            resize(size.w, size.h); verify(waitForPolish(test.Window.window));
            for (const id of ["apple", "community", "folder", "drone"]) {
                compare(action(id, "sourceTitle").truncated, false, id + " name is complete");
                if (id !== "folder") compare(action(id, "sourceDetail").truncated, false, id + " detail is complete");
            }
        }
    }
    function test_dialogCustomQualityValidationAndPending() {
        openMenu("three"); mouseClick(action("three", "sourceEdit"));
        tryCompare(dialog, "opened", true);
        Harness.sources.field("maxResolution", 1440);
        const quality = findChild(dialog, "sourceQuality");
        compare(quality.currentValue, 1440); compare(quality.displayText, "1440p"); compare(quality.inherited, false);
        Harness.sources.field("name", "");
        mouseClick(dialog.standardButton(T.Dialog.Apply));
        verify(Harness.sources.sheetOpen); verify(dialog.visible); verify(Harness.sources.draft.error.length > 0);
        Harness.sources.field("name", "Edited <plain>");
        Harness.sources.field("maxResolution", 1080);
        const apply = dialog.standardButton(T.Dialog.Apply);
        mouseClick(apply); verify(Harness.sources.busy);
        verify(!dialog.enabled); keyClick(Qt.Key_Escape); verify(dialog.visible);
        settled(); tryCompare(dialog, "visible", false);
        compare(Harness.saved("sources")[2].name, "Edited <plain>");
        compare(Harness.saved("sources")[2].maxResolution, 1080);
        mouseClick(findChild(page, "addSource")); tryCompare(dialog, "opened", true);
        compare(findChild(dialog, "sourceQuality").inherited, true);
        compare(findChild(dialog, "sourceQuality").enabled, true);
        compare(dialog.standardButton(T.Dialog.Apply).text, "Add source");
        compare((dialog.standardButton(T.Dialog.Apply) as IAButton).tone, IA.Presentation.Primary);
        compare((dialog.standardButton(T.Dialog.Cancel) as IAButton).tone, IA.Presentation.Neutral);
        verify(findChild(dialog, "sourceApple").visible);
        keyClick(Qt.Key_Escape); tryCompare(dialog, "visible", false);
    }
    function test_dialogChooserAndOwnerLossKeepDraft() {
        Harness.sources.begin(""); tryCompare(dialog, "opened", true);
        mouseClick(findChild(dialog, "sourceTypeFolder"));
        chooseIntent.clear(); mouseClick(findChild(dialog, "sourceChoosePath"));
        compare(chooseIntent.count, 1);
        Harness.sources.field("name", "Keep my draft");
        Harness.sources.field("source", "/home/tester/Videos/kept");
        Harness.stopService();
        tryCompare(Harness, "panel", "unavailable");
        compare(Harness.sources.draft.name, "Keep my draft"); compare(dialog.visible, true);
        Harness.startService(); tryCompare(Harness, "panel", "settings");
        Harness.sources.save();
        verify(Harness.sources.sheetOpen); verify(Harness.sources.draft.error.length > 0);
        compare(findChild(dialog, "sourceName").text, "Keep my draft");
    }
    function geometry(popup) {
        verify(waitForPolish(test.Window.window));
        const origin = popup.contentItem.mapToItem(test, -popup.leftPadding, -popup.topPadding);
        // Dialog content begins below the native header, whereas a menu has none.
        const y = origin.y - (popup === mockDialog || popup === dialog ? popup.header.height + popup.spacing : 0);
        verify(origin.x >= 24 && y >= 24, "popup leading inset");
        verify(origin.x + popup.width <= test.width - 24 && y + popup.height <= test.height - 24, "popup trailing inset");
    }
    function frame(name) {
        test.Window.window.update(); verify(waitForRendering(shell));
        verify(Harness.capture(shell, name));
        if (!mockDialog.visible) compare(Harness.edgeViolation(shell, 212, UI.Insets.edge), "");
    }
    function test_visualMockFrames() {
        reference = true; verify(waitForPolish(test.Window.window));
        for (const dark of [false, true]) {
            verify(Harness.appearance(dark));
            for (const size of [{ w: 920, h: 760, name: "reference" }, { w: 640, h: 480, name: "minimum" }]) {
                resize(size.w, size.h); verify(waitForPolish(test.Window.window));
                const prefix = "sources-" + size.name + "-" + (dark ? "dark" : "light");
                frame(prefix);
                mouseClick(action("community", "sourceMore")); tryCompare(menu("community"), "opened", true);
                geometry(menu("community")); frame(prefix + "-menu"); menu("community").close();
                mockDialog.open(); tryCompare(mockDialog, "opened", true); geometry(mockDialog);
                const button = mockDialog.standardButton(T.Dialog.Apply);
                verify(button.width >= button.implicitWidth);
                const title = findChild(mockDialog, "iaTitle"), name = findChild(mockDialog, "sourceName");
                fuzzyCompare(title.mapToItem(test, 0, 0).x, name.mapToItem(test, 0, 0).x, 0.1);
                frame(prefix + "-add");
                const viewport = findChild(mockDialog, "iaDialogViewport");
                if (viewport.contentHeight > viewport.height) {
                    viewport.contentY = viewport.contentHeight - viewport.height;
                    frame(prefix + "-add-end");
                }
                mockDialog.close(); tryCompare(mockDialog, "visible", false);
            }
        }
    }
}
