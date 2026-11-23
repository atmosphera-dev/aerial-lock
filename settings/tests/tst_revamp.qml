import QtQuick
import QtTest
import AerialLock.SettingsTest
import InterActive
import InterActive.Components
import InterActive.Foundation as IA
import "../qml" as UI

TestCase {
    id: test
    name: "RevampOverview"
    width: 920
    height: 760
    visible: true
    when: windowShown
    readonly property var pages: [
        { key: "overview", title: "Overview", icon: Qt.resolvedUrl("../qml/icons/sky.svg") },
        { key: "sources", title: "Sources", icon: Qt.resolvedUrl("../qml/icons/stack.svg") },
        { key: "downloads", title: "Downloads", icon: Qt.resolvedUrl("../qml/icons/download.svg") },
        { key: "lock", title: "Lock screen", icon: Qt.resolvedUrl("../qml/icons/lock.svg") },
        { key: "about", title: "About", icon: Qt.resolvedUrl("../qml/icons/info.svg") }
    ]
    UI.Shell {
        id: shell
        anchors.fill: parent
        pages: Harness.panel === "settings" ? test.pages : []
        UI.Overview {
            id: overview
            anchors.fill: parent
            visible: Harness.panel === "settings" && !reference.visible
            facts: Harness.statusModel.facts
            notices: Harness.notices.entries
            activity: Harness.cache.activity
            onActionRequested: (actionId, sourceId) => Harness.dispatchAction(actionId, sourceId)
        }
        UI.Overview {
            id: reference
            visible: false
            anchors.fill: parent
            facts: ({ headline: "Downloading Grand Canyon", detail: "1.2 GB of 2.0 GB, about 3 minutes left",
                downloading: true, progressKnown: true, progress: 0.59, primaryActionId: "stop-download", primaryActionLabel: "Stop download",
                secondaryActionId: "pause-downloads", secondaryActionLabel: "Pause automatic downloads",
                readyCountText: "14 videos", nextCheckText: "Today at 10:31 AM", storageFraction: 0.68, usageKnown: true,
                storageDescription: "6.8 GB of 10 GB used, 1.2 GB of it by the current download" })
            notices: [{ id: "idle", title: "The screen doesn't lock by itself",
                message: "Automatic locking needs the aerial-idle service, and it isn't running.",
                actionId: "turn-on-idle", actionLabel: "Turn on", severity: "warning" }]
            activity: [
                { path: "/one", label: "Downloaded 3 videos from Apple TV", time: "Today at 9:31 AM, 2.4 GB", state: "Done", error: false },
                { path: "/two", label: "Checked 4 sources for new videos", time: "Today at 8:31 AM, 3 new videos found", state: "Done", error: false },
                { path: "/three", label: "Couldn't reach Drone flights", time: "Yesterday at 6:02 PM, example.org didn't answer",
                    state: "Needs attention", error: true, actionId: "open-sources", actionLabel: "Open Sources" }
            ]
        }
        UI.EmptyPage {
            id: unavailable
            anchors.fill: parent
            visible: Harness.panel !== "settings"
            title: "The aerial service isn't running"
            description: Harness.diagnostic
            actionText: "Try again"
            onActionRequested: Harness.dispatchAction("retry-service")
        }
    }
    SignalSpy { id: navigation; target: shell; signalName: "selected" }
    SignalSpy { id: pageRequest; target: Harness; signalName: "pageRequested" }
    Component { id: statusComponent; UI.StatusHeader { x: 237; y: 24; width: 379 } }
    Component { id: meterComponent; UI.StorageMeter { x: 237; y: 300; width: 180 } }
    Component { id: resourceIcon; IA.Icon { x: 260; y: 50; width: 22; height: 22; source: "qrc:/settings/qml/icons/sky.svg" } }
    Component {
        id: bannerComponent
        IAStatusBanner {
            x: 237; y: 82; width: 379
            title: "The screen doesn't lock by itself"
            message: "Automatic locking needs the aerial-idle service, and it isn't running."
            iconSource: Qt.resolvedUrl("../qml/icons/alert.svg")
            actionText: "Open About"
        }
    }
    function test_edgeSamplerDetectsPhysicalPixels_data() {
        return [{ tag: "1x", scale: 1 }, { tag: "1.5x", scale: 1.5 }, { tag: "2x", scale: 2 }];
    }
    function test_edgeSamplerDetectsPhysicalPixels(data) {
        verify(Harness.edgePixelFixture(data.scale).startsWith("unexpected edge pixel at 21,20:"));
    }
    function test_bannerReservesCompleteAction_data() {
        return [{ tag: "reference", w: 620, action: "Turn on" }, { tag: "minimum", w: 379, action: "Open About" }];
    }
    function test_bannerReservesCompleteAction(data) {
        const banner = createTemporaryObject(bannerComponent, test, { width: data.w, actionText: data.action });
        verify(banner !== null);
        verify(waitForPolish(test.Window.window));
        const action = findChild(banner, "iaBannerAction");
        verify(action.width >= action.implicitWidth, "Banner must not shrink its action text");
        compare(findChild(action, "iaLabel").truncated, false);
        const message = findChild(banner, "iaBannerMessage");
        compare(message.fontPixelSize, IA.Appearance.typography.supportingSize);
        verify(Qt.colorEqual(message.foregroundColor, IA.Appearance.colors.secondaryText));
        if (data.w === 379) verify(message.contentHeight > message.font.pixelSize * 1.5, "Description wraps instead");
        const icon = findChild(banner, "iaBannerIcon"), title = findChild(banner, "iaBannerTitle");
        fuzzyCompare(icon.mapToItem(banner, 0, icon.height / 2).y, title.mapToItem(banner, 0, title.height / 2).y, 0.1);
    }
    function test_nativeIconArtwork() {
        if (!Harness.nativeRendering(shell)) return; // Software scene graph is not visual acceptance.
        test.Window.window.update();
        verify(waitForRendering(shell));
        for (const key of ["overview", "sources", "downloads", "lock", "about"]) {
            const delegate = findChild(shell, "navigation-" + key);
            const icon = Harness.iconIn(delegate);
            verify(icon !== null, "Shared icon owner is observable");
            verify(Harness.inkPixels(icon) > 8, key + " icon has rendered artwork");
        }
        const icon = Harness.iconIn(findChild(overview, "overviewStatus"));
        verify(Harness.inkPixels(icon) > 8, "Status glyph is not an empty grey tile");
        for (const key of ["overviewPrimary", "overviewSecondary"]) {
            const icon = Harness.iconIn(findChild(overview, key));
            verify(icon !== null && Harness.inkPixels(icon) > 8, key + " icon renders");
        }
        const banner = createTemporaryObject(bannerComponent, test);
        verify(waitForPolish(test.Window.window));
        test.Window.window.update(); verify(waitForRendering(banner));
        verify(Harness.inkPixels(findChild(banner, "iaBannerIcon")) > 8, "Banner icon renders");
        const resource = createTemporaryObject(resourceIcon, test);
        tryCompare(resource, "available", true);
        test.Window.window.update();
        verify(waitForRendering(resource));
        verify(Harness.inkPixels(resource) > 8, "The compiled qrc path renders too");
    }
    IAComboBox {
        id: select
        visible: false
        x: test.width - width - UI.Insets.edge
        y: test.height - height - UI.Insets.edge
        width: UI.Insets.controlLane
        popupMargins: UI.Insets.edge
        model: Harness.controls["acquisition.checkIntervalSeconds"].choices
        textRole: "label"
        valueRole: "token"
        currentIndex: Harness.controls["acquisition.checkIntervalSeconds"].selected
        inherited: Harness.controls["acquisition.checkIntervalSeconds"].inherited
        onActivated: Harness.choose("acquisition.checkIntervalSeconds", currentValue)
    }
    function resize(w, h) {
        test.width = w; test.height = h;
        verify(Harness.resizeSurface(shell, w, h));
    }
    function init() {
        resize(920, 760);
        overview.viewport.contentY = 0;
        verify(Harness.appearance(false));
        verify(waitForPolish(test.Window.window));
    }
    function cleanup() { select.popup.close(); select.visible = false; reference.visible = false; }
    function cleanupTestCase() { Harness.releaseSurface(shell); }
    function test_navigationIntent() {
        navigation.clear();
        mouseClick(findChild(shell, "navigation-sources"));
        compare(navigation.count, 1); compare(navigation.signalArguments[0][0], "sources");
        compare(shell.currentPage, "overview"); // only the consumer changes pages
    }
    function test_unknownAndKnownProjectionConsumers() {
        const properties = { UsageKnown: false, ReadyClips: 0, DeferReason: "None" };
        const job = { State: "downloading", Cancellable: true, TotalKnown: false, TotalBytes: 100,
            ReceivedBytes: 50, Details: { assetTitle: "Grand Canyon" } };
        let facts = Harness.statusProjection(properties, [job]);
        const status = createTemporaryObject(statusComponent, test, { facts: facts });
        const meter = createTemporaryObject(meterComponent, test, { facts: facts });
        verify(status !== null && meter !== null);
        verify(waitForPolish(test.Window.window));
        const progress = findChild(status, "overviewProgress");
        compare(progress.visible, true); compare(progress.indeterminate, true);
        verify(findChild(status, "overviewDetail").text.indexOf("unknown size") >= 0);
        verify(findChild(status, "overviewDetail").text.indexOf("left") < 0);
        compare(meter.visible, false);
        compare(findChild(status, "overviewPrimary").visible, true);
        compare(findChild(status, "overviewSecondary").visible, true);
        job.TotalKnown = true;
        properties.UsageKnown = true; properties.ManagedBytes = 100; properties.MaxBytes = 80;
        facts = Harness.statusProjection(properties, [job]);
        status.facts = facts; meter.facts = facts;
        compare(progress.indeterminate, false); compare(progress.value, 0.5);
        compare(meter.visible, true); compare(meter.value, 1);
        verify(facts.storageDescription.indexOf("over limit") >= 0);
    }
    function test_consumerActionsAndCli() {
        const secondary = findChild(overview, "overviewSecondary");
        for (let i = 0; i < 2; ++i) {
            const next = !Harness.controls.automatic.on;
            mouseClick(secondary);
            tryVerify(() => !Harness.controls.automatic.pending && Harness.controls.automatic.on === next);
            compare(Harness.saved("acquisition.automatic.enabled"), next);
            compare(secondary.text, Harness.statusModel.facts.secondaryActionLabel);
        }
        Harness.externalEdit("acquisition.automatic.enabled", "true");
        tryCompare(secondary, "text", "Pause automatic downloads");
        Harness.externalEdit("acquisition.automatic.enabled", "false");
        tryCompare(secondary, "text", "Resume automatic downloads");
        mouseClick(findChild(overview, "overviewPrimary"));
        tryVerify(() => Harness.cache.activity.length > 0 && Harness.cache.activity[0].error);
        tryVerify(() => findChild(overview, "activity-" + Harness.cache.activity[0].path) !== null);
        verify(waitForPolish(test.Window.window));
        const activity = findChild(overview, "activity-" + Harness.cache.activity[0].path);
        const action = findChild(activity, "activityAction");
        const viewport = overview.viewport;
        const center = () => action.mapToItem(viewport, action.width / 2, action.height / 2);
        // The failing job is first in Recent activity, not necessarily at the end of the page.
        viewport.contentY = Math.max(0, Math.min(viewport.contentHeight - viewport.height,
            viewport.contentY + center().y - viewport.height / 2));
        tryVerify(() => center().y >= 0 && center().y < viewport.height);
        pageRequest.clear();
        mouseClick(action);
        compare(pageRequest.count, 1); compare(pageRequest.signalArguments[0][0], "sources");
        compare(findChild(overview, "overviewReady").text, Harness.statusModel.facts.readyCountText);
        compare(findChild(overview, "overviewNextCheck").text, Harness.statusModel.facts.nextCheckText);
        compare(findChild(overview, "overviewStorage").value, Harness.statusModel.facts.storageFraction);
    }
    function test_insets_data() {
        return [{ tag: "reference", w: 920, h: 760 }, { tag: "minimum", w: 640, h: 480 }];
    }
    function test_insets(data) {
        resize(data.w, data.h);
        verify(waitForPolish(test.Window.window));
        compare(UI.Insets.edge, 24);
        const sidebar = findChild(shell, "sidebar");
        const column = findChild(shell, "sidebarColumn");
        compare(sidebar.width, 212);
        fuzzyCompare(column.x, 24, 0.01); fuzzyCompare(column.y, 24, 0.01);
        fuzzyCompare(sidebar.width - column.x - column.width, 24, 0.01);
        fuzzyCompare(sidebar.height - column.y - column.height, 24, 0.01);
        const viewport = overview.viewport, content = overview.contentColumn;
        compare(viewport.clip, true);
        compare(viewport.contentWidth, viewport.width);
        fuzzyCompare(viewport.mapToItem(shell, 0, 0).y, 24, 0.01);
        fuzzyCompare(shell.height - viewport.mapToItem(shell, 0, viewport.height).y, 24, 0.01);
        fuzzyCompare(content.x, 24, 0.01);
        compare(content.width, Math.min(620, viewport.width - 48));
        const band = findChild(overview, "pageBand"), app = findChild(shell, "appBand");
        fuzzyCompare(band.mapToItem(shell, 0, band.height / 2).y, app.mapToItem(shell, 0, app.height / 2).y, 0.01);
        const bar = overview.scrollBar;
        fuzzyCompare(shell.width - bar.mapToItem(shell, bar.width, 0).x, 8, 0.01);
        verify(bar.mapToItem(shell, 0, 0).x - content.mapToItem(shell, content.width, 0).x >= 8);
        for (const fraction of [0, 0.5, 1]) {
            viewport.contentY = fraction * Math.max(0, viewport.contentHeight - viewport.height);
            verify(waitForPolish(test.Window.window));
            fuzzyCompare(viewport.mapToItem(shell, 0, 0).y, 24, 0.01);
            fuzzyCompare(shell.height - viewport.mapToItem(shell, 0, viewport.height).y, 24, 0.01);
            waitForRendering(shell);
            verify(Harness.capture(shell, "insets-" + data.tag + "-" + fraction));
            compare(Harness.edgeViolation(shell, 212, UI.Insets.edge), "");
        }
    }
    function test_openSelectCliPreservesStateAndMargins() {
        resize(640, 480);
        Harness.externalEdit("acquisition.checkIntervalSeconds", "3600");
        tryCompare(select, "currentValue", "3600");
        select.visible = true;
        verify(waitForPolish(test.Window.window));
        select.forceActiveFocus(Qt.TabFocusReason);
        keyClick(Qt.Key_Space);
        tryCompare(select.popup, "opened", true);
        const content = select.popup.contentItem as ListView;
        verify(content !== null);
        const before = content.contentY;
        Harness.externalEdit("acquisition.checkIntervalSeconds", "21600");
        tryCompare(select, "currentValue", "21600");
        compare(select.popup.opened, true);
        compare(select.activeFocus, true);
        fuzzyCompare(content.contentY, before, 0.01);
        const p = content.mapToItem(test, -select.popup.leftPadding, -select.popup.topPadding);
        verify(p.x >= 24 && p.y >= 24);
        verify(p.x + select.popup.width <= test.width - 24);
        verify(p.y + select.popup.height <= test.height - 24);
        keyClick(Qt.Key_Down); keyClick(Qt.Key_Return);
        tryVerify(() => !Harness.controls["acquisition.checkIntervalSeconds"].pending);
        compare(String(Harness.saved("acquisition.checkIntervalSeconds")), select.currentValue);
    }
    function test_serviceOutageEmptyInsets() {
        Harness.stopService();
        tryCompare(unavailable, "visible", true);
        tryVerify(() => shell.pages.length === 0);
        try {
            for (const data of [{ w: 920, h: 760, dark: false }, { w: 640, h: 480, dark: false },
                { w: 920, h: 760, dark: true }, { w: 640, h: 480, dark: true }]) {
                const size = data;
                verify(Harness.appearance(data.dark));
                resize(size.w, size.h);
                verify(waitForPolish(test.Window.window));
                const state = findChild(unavailable, "emptyState");
                const point = state.mapToItem(unavailable, 0, 0);
                const retry = findChild(unavailable, "connectionRetry");
                verify(retry.visible);
                compare(state.contentItem.mapToItem(unavailable, 0, 0).x,
                    retry.mapToItem(unavailable, 0, 0).x);
                verify(state.width <= 420);
                verify(point.x >= 24 && point.y >= 24);
                verify(point.x + state.width <= unavailable.width - 24);
                verify(point.y + state.height <= unavailable.height - 24);
                waitForRendering(shell);
                compare(Harness.edgeViolation(shell, 212, UI.Insets.edge), "");
                verify(Harness.capture(shell, "unavailable-" + (data.dark ? "dark" : "light") + "-" + size.w));
            }
        } finally {
            Harness.startService(); tryCompare(overview, "visible", true);
        }
    }
    function test_visualFrames() {
        for (const dark of [false, true]) {
            verify(Harness.appearance(dark));
            for (const size of [{ w: 920, h: 760, name: "reference" }, { w: 640, h: 480, name: "minimum" }]) {
                resize(size.w, size.h);
                verify(waitForPolish(test.Window.window));
                for (const fraction of [0, 0.5, 1]) {
                    overview.viewport.contentY = fraction * Math.max(0, overview.viewport.contentHeight - overview.viewport.height);
                    verify(waitForPolish(test.Window.window));
                    waitForRendering(shell);
                    verify(Harness.capture(shell, size.name + "-" + (dark ? "dark" : "light") + "-" + fraction));
                    compare(Harness.edgeViolation(shell, 212, UI.Insets.edge), "");
                }
            }
        }
    }
    function test_visualMockEquivalentFrames() {
        reference.visible = true;
        for (const dark of [false, true]) {
            verify(Harness.appearance(dark));
            for (const size of [{ w: 920, h: 760, name: "reference" }, { w: 640, h: 480, name: "minimum" }]) {
                resize(size.w, size.h);
                verify(waitForPolish(test.Window.window));
                for (const fraction of [0, 1]) {
                    reference.viewport.contentY = fraction * Math.max(0, reference.viewport.contentHeight - reference.viewport.height);
                    verify(waitForPolish(test.Window.window));
                    test.Window.window.update();
                    verify(waitForRendering(shell));
                    verify(Harness.capture(shell, "mock-" + size.name + "-" + (dark ? "dark" : "light") + "-" + fraction));
                    compare(Harness.edgeViolation(shell, 212, UI.Insets.edge), "");
                    const action = findChild(reference, "iaBannerAction");
                    verify(action.width >= action.implicitWidth);
                    if (Harness.nativeRendering(shell) && fraction === 0) {
                        verify(Harness.inkPixels(findChild(reference, "iaBannerIcon")) > 8);
                        verify(Harness.inkPixels(Harness.iconIn(findChild(reference, "overviewStatus"))) > 8);
                    }
                }
            }
        }
    }
}
