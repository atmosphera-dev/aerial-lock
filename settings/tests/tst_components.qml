import QtQuick
import QtTest
import AerialLock.SettingsTest
import "../qml/components" as UI
TestCase {
    id: test
    name: "SettingsComponentBoundaries"
    width: 960
    height: 900
    visible: true
    when: windowShown
    UI.SourceRow {
        id: source
        width: 672
        height: implicitHeight
        sourceId: "boundary"
        title: "Source"
        toggle: () => {}
        edit: () => {}
        remove: () => {}
        retry: () => {}
        beginDrag: () => {}
    }
    UI.Sheet {
        id: sheet
        parent: test
        title: "Boundary probe"
        Item { implicitHeight: 100; implicitWidth: 300 }
    }
    UI.ThemeCard {
        id: thumbnail
        y: 200
        width: 200
        text: "Preview"
        preview: Harness.themes.rows.length ? Harness.themes.rows[0].preview : ""
    }
    function test_sourceToggleUsesObservedState_data() {
        return [{ tag: "rejected", accepted: false }, { tag: "accepted", accepted: true }];
    }
    function test_sourceToggleUsesObservedState(data) {
        source.on = false;
        source.on = true;
        const control = findChild(source, "sourceToggle");
        let request = null;
        source.toggle = (id, value) => { request = value; if (data.accepted) source.on = value; };
        try {
            mouseClick(control);
            compare(request, false);
            compare(control.checked, data.accepted ? false : true);
            source.on = false; compare(control.checked, false);
            source.on = true; compare(control.checked, true);
        } finally { source.toggle = () => {}; source.on = true; }
    }
    function test_previewDecodeIsThumbnailSized() {
        const image = thumbnail.contentItem.children[0].children[0];
        verify(image !== null);
        tryCompare(image, "status", Image.Ready);
        verify(image.sourceSize.width > 0 && image.sourceSize.height > 0);
        verify(image.implicitWidth <= image.sourceSize.width);
        verify(image.implicitHeight <= image.sourceSize.height);
        verify(image.sourceSize.width <= Math.ceil(thumbnail.availableWidth * thumbnail.Screen.devicePixelRatio));
    }
    function cleanup() { sheet.close(); }
    function test_sourceInset_data() {
        return [{ tag: "compact", width: 432 }, { tag: "reference", width: 672 }, { tag: "wide", width: 992 }];
    }
    function test_sourceInset(data) {
        source.width = data.width;
        verify(waitForPolish(test.Window.window));
        const grip = findChild(source, "sourceGrip");
        fuzzyCompare(grip.mapToItem(source, 0, 0).x, 6, 0.01);
        fuzzyCompare(grip.mapToItem(source, 0, grip.height / 2).y, source.height / 2, 1);
    }
    function test_sheetSilhouette() {
        sheet.open(); tryCompare(sheet, "visible", true);
        verify(waitForPolish(test.Window.window));
        const item = sheet.contentItem.parent;
        const colors = Harness.pixels(item, [{ x: 1, y: 1 }, { x: -2, y: 1 }, { x: item.width - 2, y: 1 }, { x: item.width + 1, y: 1 }, { x: item.width / 2, y: 20 }, { x: item.width / 2, y: 80 }]);
        // Window grabs are composited: a clear corner must match the adjacent
        // scrim, not an alpha value in the final opaque window image.
        compare(colors[0], colors[1], "heading must not paint over the rounded top-left corner");
        compare(colors[2], colors[3], "heading must not paint over the rounded top-right corner");
        compare(colors[4], UI.Style.card);
        compare(colors[5], UI.Style.window);
    }
}
