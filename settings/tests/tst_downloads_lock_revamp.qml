import QtQuick
import QtQuick.Templates as T
import QtTest
import InterActive.Foundation as IA
import AerialLock.SettingsTest
import "../qml" as UI

TestCase {
    id: test
    name: "SettingsDownloadsLockRevamp"
    width: 920
    height: 760
    visible: true
    when: windowShown
    property string currentPage: "downloads"
    UI.IdleDialog { id: idleDialog; parent: test }
    UI.Shell {
        id: shell
        anchors.fill: parent
        pages: [
            { key: "overview", title: "Overview", icon: Qt.resolvedUrl("../qml/icons/movie.svg") },
            { key: "sources", title: "Sources", icon: Qt.resolvedUrl("../qml/icons/stack.svg") },
            { key: "downloads", title: "Downloads", icon: Qt.resolvedUrl("../qml/icons/download.svg") },
            { key: "lock", title: "Lock screen", icon: Qt.resolvedUrl("../qml/icons/lock.svg") },
            { key: "about", title: "About", icon: Qt.resolvedUrl("../qml/icons/info.svg") }
        ]
        currentPage: test.currentPage
        UI.DownloadsPage { id: downloads; anchors.fill: parent; visible: test.currentPage === "downloads"; controller: Harness }
        UI.LockScreenPage { id: lock; anchors.fill: parent; visible: test.currentPage === "lock"; controller: Harness }
    }
    function init() {
        verify(Harness.appearance(false));
        currentPage = "downloads";
        downloads.viewport.contentY = 0;
        lock.viewport.contentY = 0;
        verify(Harness.resizeSurface(shell, 920, 760));
        verify(waitForPolish(test.Window.window));
    }
    function cleanupTestCase() { Harness.releaseSurface(shell); }
    function edit(key, value) {
        const descriptor = key === "acquisition.automatic.enabled" ? "automatic" : key;
        // Also wait for an explicit override when the value equals the default.
        // Value equality alone can precede the new service config generation.
        if (JSON.stringify(Harness.controls[descriptor].value) === JSON.stringify(value)
            && JSON.stringify(Harness.saved(key)) === JSON.stringify(value)) return;
        Harness.externalEdit(key, JSON.stringify(value));
        tryVerify(() => JSON.stringify(Harness.controls[descriptor].value) === JSON.stringify(value)
            && !Harness.controls[descriptor].inherited, 3000, key);
    }
    function saved(key, value) {
        tryVerify(() => !Harness.controls[key].pending);
        compare(Harness.controls[key].error, "", key);
        compare(JSON.stringify(Harness.saved(key)), JSON.stringify(value), key);
    }
    function scroll(page, where) {
        page.viewport.contentY = (page.viewport.contentHeight - page.viewport.height) * where;
        verify(waitForPolish(test.Window.window));
    }
    function use(page, control, index) {
        const target = Math.max(0, Math.min(control.mapToItem(page.viewport.contentItem, 0, 0).y - page.viewport.height / 2,
            page.viewport.contentHeight - page.viewport.height));
        page.viewport.contentY = target;
        verify(waitForPolish(test.Window.window));
        mouseClick(control);
        tryCompare(control.popup, "opened", true);
        // The released control preserves current selection on opening.
        for (let i = control.highlightedIndex; i < index; ++i) keyClick(Qt.Key_Down);
        for (let i = control.highlightedIndex; i > index; --i) keyClick(Qt.Key_Up);
        compare(control.highlightedIndex, index);
        keyClick(Qt.Key_Return);
        tryCompare(control.popup, "visible", false);
    }
    function inside(popup, anchor) {
        const p = popup.contentItem.mapToItem(shell, 0, 0);
        const pad = UI.Insets.edge;
        verify(p.x - popup.leftPadding >= pad - 1);
        verify(p.y - popup.topPadding >= pad - 1);
        verify(p.x - popup.leftPadding + popup.width <= shell.width - pad + 1);
        verify(p.y - popup.topPadding + popup.height <= shell.height - pad + 1);
        if (anchor) {
            const icon = findChild(anchor, "iaComboChevron");
            const center = icon.mapToItem(anchor, icon.width / 2, icon.height / 2);
            fuzzyCompare(center.y, anchor.height / 2, 1);
        }
    }
    function clickDialogButton(dialog, role) {
        const button = dialog.standardButton(role);
        // opened can fire before InterActive's footer layout has polished.
        // Send input only once the whole button fits inside the visible lane.
        tryVerify(() => {
            const lane = dialog.footer.contentItem;
            const p = button.mapToItem(lane, 0, 0);
            return button.visible && button.enabled && button.width > 0 && button.height > 0
                && p.x >= -1 && p.x + button.width <= lane.width + 1;
        });
        mouseClick(button);
    }
    function test_customValuesAndGroups() {
        const interval = "acquisition.checkIntervalSeconds", quality = "acquisition.automatic.allowedResolutions";
        const cellular = "acquisition.automatic.connectionTypes.cellular", power = "acquisition.automatic.power.unknown";
        edit(interval, 7200); edit(quality, [2160]); edit(cellular, false); edit(power, false);
        const choice = findChild(downloads, "interval");
        compare(choice.currentValue, "7200");
        verify(choice.displayText.startsWith("Custom:"));
        const qualityChoice = findChild(findChild(downloads, "quality"), "preferenceChoice");
        verify(qualityChoice.visible);
        compare(qualityChoice.currentValue, "[2160]");
        saved(interval, 7200); saved(quality, [2160]);
        use(downloads, choice, 2); saved(interval, 21600);
        use(downloads, qualityChoice, 1); saved(quality, [1080]);
        scroll(downloads, 0.65);
        const mobile = findChild(downloads, cellular), unknown = findChild(downloads, power);
        compare(mobile.text, "Mobile broadband");
        compare(unknown.text, "Unknown");
        mouseClick(mobile); saved(cellular, true);
        mouseClick(unknown); saved(power, true);
        edit(cellular, false); tryCompare(mobile, "checked", false);
        edit(power, false); tryCompare(unknown, "checked", false);
        compare(findChild(downloads, "automatic").label, "Download new videos automatically");
    }
    function test_storageCleanupConfirmation() {
        scroll(downloads, 1);
        const limit = findChild(downloads, "cache.maxBytes");
        edit("cache.maxBytes", 3100000000);
        compare(limit.currentValue, "3100000000"); saved("cache.maxBytes", 3100000000);
        use(downloads, limit, 3); saved("cache.maxBytes", 10000000000);
        const swap = findChild(downloads, "cache.rotationDays");
        edit("cache.rotationDays", 7); use(downloads, swap, 2); saved("cache.rotationDays", 30);
        const dialog = findChild(downloads, "cleanupDialog");
        mouseClick(findChild(downloads, "prepareCleanup"));
        tryCompare(dialog, "opened", true);
        tryCompare(dialog, "activeFocus", true);
        tryVerify(() => Harness.cache.cleanup.state === "confirm");
        verify(Harness.cache.cleanup.message.startsWith("Can remove "));
        keyClick(Qt.Key_Escape); tryCompare(dialog, "visible", false);
        tryVerify(() => !Harness.cache.cleanup.visible);
        compare(Harness.cache.cleanup.state, "confirm");
        mouseClick(findChild(downloads, "prepareCleanup")); tryCompare(dialog, "opened", true);
        tryVerify(() => Harness.cache.cleanup.state === "confirm");
        clickDialogButton(dialog, T.Dialog.Apply);
        tryVerify(() => Harness.cache.cleanup.state === "result");
        verify(Harness.cache.cleanup.message.startsWith("Removed "));
        verify(dialog.visible);
        verify(!dialog.standardButton(T.Dialog.Apply).visible);
        clickDialogButton(dialog, T.Dialog.Cancel);
        tryVerify(() => !Harness.cache.cleanup.visible);
    }
    function test_themeAndCustomColor() {
        currentPage = "lock";
        edit("theme", "Enfield.qml"); edit("backgroundColor", "#000000");
        const mac = findChild(lock, "theme-Macintosh.qml");
        const enfield = findChild(lock, "theme-Enfield.qml");
        const thumb = findChild(enfield, "themeThumbnail");
        tryCompare(thumb, "visible", true);
        fuzzyCompare(thumb.height, thumb.width * 10 / 16, 1);
        verify(enfield.checked && !mac.checked);
        verify(findChild(enfield, "selectedThemeIcon").visible);
        mouseClick(mac); saved("theme", "Macintosh.qml");
        tryCompare(mac, "checked", true); verify(!enfield.checked);
        const combo = findChild(lock, "backgroundColor"), dialog = findChild(lock, "colorDialog");
        use(lock, combo, 1); saved("backgroundColor", "#1f2124");
        compare(combo.displayText, "Graphite");
        use(lock, combo, 3); tryCompare(dialog, "opened", true);
        const field = findChild(dialog, "colorField"), apply = dialog.standardButton(T.Dialog.Apply);
        field.text = "not a color"; verify(!apply.enabled);
        field.text = "#A0112233"; verify(apply.enabled);
        clickDialogButton(dialog, T.Dialog.Cancel); tryCompare(dialog, "visible", false);
        saved("backgroundColor", "#1f2124"); compare(combo.displayText, "Graphite");
        use(lock, combo, 3); tryCompare(dialog, "opened", true);
        field.text = "#A0112233"; clickDialogButton(dialog, T.Dialog.Apply); tryCompare(dialog, "visible", false);
        saved("backgroundColor", "#a0112233");
        compare(combo.displayText, "Custom: #a0112233");
        verify(Qt.colorEqual(combo.leadingColor, "#a0112233"));
        use(lock, combo, 3); tryCompare(dialog, "opened", true);
        compare(field.text, "#a0112233"); keyClick(Qt.Key_Escape); tryCompare(dialog, "visible", false);
    }
    function test_lockControlsAndConfirmation() {
        currentPage = "lock"; scroll(lock, 1);
        const timeout = findChild(lock, "idleTimeout");
        edit("idle.timeoutSeconds", 300); use(lock, timeout, 6); saved("idle.timeoutSeconds", 360);
        for (const key of ["powerActions.enabled", "idle.respectInhibitors", "idle.lockOnSuspend", "idle.logindLockUnlock"]) {
            const row = findChild(lock, key === "powerActions.enabled" ? "powerActions" : key);
            const before = Harness.controls[key].on;
            mouseClick(findChild(row, "preferenceSwitch")); saved(key, !before);
        }
        verify(findChild(lock, "idleNotice").visible);
        // Do not start a real lock; native installed-unit tests exercise the
        // action and explicit confirmation against an owned no-lock unit.
        idleDialog.open();
        const dialog = idleDialog;
        tryCompare(dialog, "opened", true);
        compare(dialog.standardButton(T.Dialog.Apply).text, "Turn on");
        keyClick(Qt.Key_Escape); tryCompare(dialog, "visible", false);
        saved("idle.timeoutSeconds", 360);
    }
    function test_ignoreLeadingSpacesSwitch() {
        currentPage = "lock"; scroll(lock, 0.5);
        const row = findChild(lock, "ignoreLeadingSpaces");
        const toggle = findChild(row, "preferenceSwitch");
        compare(row.label, "Ignore leading spaces");
        compare(toggle.checked, false);
        mouseClick(toggle); saved("ignoreLeadingSpaces", true);
        tryCompare(toggle, "checked", true);
        mouseClick(toggle); saved("ignoreLeadingSpaces", false);
        tryCompare(toggle, "checked", false);
    }
    function frame(name, popup, anchor) {
        if (popup) inside(popup, anchor);
        test.Window.window.update(); verify(waitForRendering(shell));
        verify(Harness.capture(shell, name));
        if (!popup) compare(Harness.edgeViolation(shell, 212, UI.Insets.edge), "");
    }
    function test_visualReferenceFrames() {
        if (!Harness.nativeRendering(shell)) return;
        edit("acquisition.automatic.enabled", true); edit("acquisition.checkIntervalSeconds", 3600);
        edit("acquisition.automatic.allowedResolutions", [1080]); edit("cache.maxBytes", 10000000000); edit("cache.rotationDays", 7);
        edit("acquisition.automatic.connectionTypes.bluetooth", false);
        edit("acquisition.automatic.connectionTypes.other", true);
        edit("acquisition.automatic.power.battery", false); edit("acquisition.automatic.power.unknown", true);
        edit("theme", "Enfield.qml"); edit("backgroundColor", "#000000"); edit("powerActions.enabled", true);
        edit("idle.timeoutSeconds", 300); edit("idle.respectInhibitors", true); edit("idle.lockOnSuspend", true); edit("idle.logindLockUnlock", true);
        for (const dark of [false, true]) {
            verify(Harness.appearance(dark));
            const suffix = "-" + (dark ? "dark" : "light");
            currentPage = "downloads"; scroll(downloads, 0); frame("downloads" + suffix);
            const interval = findChild(downloads, "interval");
            mouseClick(interval); tryCompare(interval.popup, "opened", true);
            frame("downloads-select" + suffix, interval.popup, interval); interval.popup.close();
            scroll(downloads, 0.5); frame("downloads-middle" + suffix);
            scroll(downloads, 1); frame("downloads-end" + suffix);
            const swap = findChild(downloads, "cache.rotationDays");
            mouseClick(swap); tryCompare(swap.popup, "opened", true);
            frame("downloads-select-bottom" + suffix, swap.popup, swap); swap.popup.close();
            mouseClick(findChild(downloads, "prepareCleanup"));
            const cleanup = findChild(downloads, "cleanupDialog");
            tryCompare(cleanup, "opened", true); tryVerify(() => Harness.cache.cleanup.state === "confirm");
            frame("downloads-cleanup" + suffix, cleanup); cleanup.close(); tryVerify(() => !Harness.cache.cleanup.visible);
            currentPage = "lock"; scroll(lock, 0);
            const tile = findChild(lock, "theme-Enfield.qml");
            const icon = findChild(tile, "selectedThemeIcon");
            tryVerify(() => Harness.inkPixels(icon) > 8, 3000, "Selected theme check renders actual ink");
            for (const name of ["Macintosh", "Windows7", "MaterialYou", "Enfield", "Clockwork"]) {
                const thumbnail = findChild(findChild(lock, "theme-" + name + ".qml"), "themeThumbnail");
                tryCompare(thumbnail, "visible", true);
                // Decoded source and a selection outline do not prove artwork was painted.
                tryVerify(() => Harness.pixels(thumbnail, [{ x: 18, y: 18 }, { x: 75, y: 42 }])
                    .some(pixel => !Qt.colorEqual(pixel, IA.Appearance.colors.content)), 3000, name);
            }
            frame("lock-screen" + suffix);
            scroll(lock, 0.5); frame("lock-screen-middle" + suffix);
            scroll(lock, 1); frame("lock-screen-end" + suffix);
            use(lock, findChild(lock, "backgroundColor"), 3);
            const color = findChild(lock, "colorDialog");
            tryCompare(color, "opened", true); frame("lock-screen-color" + suffix, color); color.close();
        }
    }
}
