import QtQuick
import QtTest
import AerialLock.SettingsTest
import "../qml"
import "../qml/components" as UI
TestCase {
    id: test
    name: "SettingsDownloads"
    width: 960
    height: 1100
    visible: true
    when: windowShown
    Downloads { id: page; anchors.fill: parent; controller: Harness }
    UI.Select { id: probe; visible: false; model: [{ label: "Every hour", value: 3600 }]; currentIndex: 0 }
    Component {
        id: sourceComponent
        UI.SourceRow { sourceId: "fixture"; title: "Source"; toggle: () => {}; edit: () => {}; remove: () => {}; retry: () => {}; beginDrag: () => {} }
    }
    Component { id: themeComponent; UI.ThemeCard { text: "Theme without a preview" } }
    Component { id: cacheComponent; UI.CacheMeter { usage: "2 GB of 10 GB used"; free: "8 GB free"; segments: [{ kind: "videos", fraction: 0.2 }]; legend: [{ kind: "videos", label: "Videos 2 GB" }] } }
    function test_sharedComponents() {
        for (const component of [sourceComponent, themeComponent, cacheComponent]) {
            const item = createTemporaryObject(component, test, { width: 600 });
            verify(item !== null);
            verify(item.implicitHeight > 0);
        }
    }
    function test_paletteProjection() {
        compare(UI.Style.selectionText, UI.Style.colors.highlightedText);
        compare(UI.Style.accentText, UI.Style.colors.highlightedText);
    }
    function test_geometry_data() {
        return [{ tag: "reference", width: 960 }, { tag: "compact", width: 720 }, { tag: "wide", width: 1280 }];
    }
    function test_geometry(data) {
        test.width = data.width;
        verify(waitForPolish(test.Window.window));
        const interval = findChild(page, "interval");
        verify(interval !== null);
        const chevron = findChild(interval, "chevron");
        const point = chevron.mapToItem(interval, 0, 0);
        fuzzyCompare(point.y, interval.height - point.y - chevron.height, 1);
        fuzzyCompare(point.y, interval.width - point.x - chevron.width, 1);
        compare(interval.height, 34);
    }
    function test_keyboard() {
        const interval = findChild(page, "interval");
        interval.forceActiveFocus(Qt.TabFocusReason);
        verify(interval.activeFocus);
        keyClick(Qt.Key_Space);
        tryCompare(interval.popup, "visible", true);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        tryVerify(() => !Harness.controls["acquisition.checkIntervalSeconds"].pending);
        tryVerify(() => Harness.controls["acquisition.checkIntervalSeconds"].value === 21600);
    }
    function test_switches_data() {
        const cases = [{ tag: "automatic", row: "automatic", key: "automatic" }, { tag: "quality", row: "quality", key: "acquisition.automatic.allowedResolutions" }, { tag: "metered", row: "metered", key: "acquisition.automatic.allowMetered" }];
        for (const type of ["ethernet", "wifi", "cellular", "bluetooth", "vpn", "other", "unknown"])
            cases.push({ tag: "connection-" + type, row: "connection-" + type, key: "acquisition.automatic.connectionTypes." + type });
        for (const type of ["ac", "battery", "unknown"])
            cases.push({ tag: "power-" + type, row: "power-" + type, key: "acquisition.automatic.power." + type });
        return cases;
    }
    function test_switches(data) {
        test.width = 960;
        verify(waitForPolish(test.Window.window));
        const row = findChild(page, data.row);
        verify(row !== null);
        const toggle = findChild(row, "toggle");
        verify(toggle !== null && toggle.visible);
        const track = toggle.indicator;
        const trackPoint = track.mapToItem(row, 0, 0);
        fuzzyCompare(trackPoint.y + track.height / 2, row.height / 2, 1);
        const expected = !Harness.controls[data.key].on;
        mouseClick(toggle);
        tryVerify(() => !Harness.controls[data.key].pending && Harness.controls[data.key].on === expected);
        compare(Harness.controls[data.key].error, "");
    }
}
