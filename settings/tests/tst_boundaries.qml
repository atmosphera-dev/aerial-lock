import QtQuick
import QtTest
import AerialLock.SettingsTest
import "../qml" as UI

TestCase {
    id: test
    name: "SettingsComponentBoundaries"
    width: 920
    height: 760
    visible: true
    when: windowShown

    UI.ThemeTile {
        id: tile
        x: 24; y: 24; width: 200; height: implicitHeight
        text: "Preview"
        preview: Harness.themes.rows.length ? Harness.themes.rows[0].preview : ""
    }
    UI.SettingsDialog {
        id: dialog
        parent: test
        title: "Boundary probe"
        Item { implicitHeight: 100; implicitWidth: 300 }
    }
    UI.AboutPage {
        id: about
        anchors.fill: parent
        visible: false
        controller: Harness
    }
    SignalSpy { id: trayIntent; target: Harness; signalName: "trayChangeRequested" }

    function test_previewReplacementAndDecodeSize() {
        const thumb = findChild(tile, "themeThumbnail");
        const image = findChild(thumb, "themePreviewImage");
        try {
            verify(waitForPolish(test.Window.window));
            tryCompare(image, "status", Image.Ready);
            verify(image.sourceSize.width > 0 && image.sourceSize.height > 0);
            verify(image.implicitWidth <= image.sourceSize.width);
            verify(image.implicitHeight <= image.sourceSize.height);
            tryVerify(() => thumb.width > 0 && image.sourceSize.width <= Math.ceil(thumb.width * tile.Screen.devicePixelRatio));
            const red = Harness.replacePreview("#ff0000"); Harness.retry();
            tryVerify(() => Harness.themes.rows[0].user);
            function checkPreview(expected, color) {
                tryVerify(() => String(image.source) === expected, 5000, "expected the replacement preview URL");
                tryCompare(image, "status", Image.Ready);
                // Offscreen loads the Image but does not render it through Shape.fillItem.
                if (Qt.platform.pluginName !== "offscreen")
                    tryVerify(() => Qt.colorEqual(Harness.pixels(thumb, [{ x: 50, y: 50 }])[0], color));
            }
            checkPreview(red, "#ff0000");
            const blue = Harness.replacePreview("#0000ff");
            verify(blue !== red);
            checkPreview(blue, "#0000ff");
        } finally { Harness.replacePreview(""); Harness.retry(); }
    }
    function test_aboutTrayIntentAndNoHost() {
        about.visible = true;
        const toggle = findChild(about, "about-tray");
        try {
            for (const state of [[false, false, false], [true, false, false], [true, true, false], [false, true, false], [false, false, true]]) {
                Harness.configureTray(...state);
                compare(toggle.enabled, state.some(on => on));
                if (!toggle.enabled) continue;
                trayIntent.clear(); mouseClick(toggle);
                compare(trayIntent.count, 1);
                compare(trayIntent.signalArguments[0][0], !(state[1] || state[2]));
            }
        } finally { Harness.configureTray(false, false); about.visible = false; }
    }
    function test_dialogSilhouette() {
        dialog.open();
        try {
            tryCompare(dialog, "opened", true);
            tryCompare(dialog, "activeFocus", true);
            verify(waitForPolish(test.Window.window));
            const item = dialog.background;
            const colors = Harness.pixels(item, [{ x: 1, y: 1 }, { x: -2, y: 1 }, { x: item.width - 2, y: 1 },
                { x: item.width + 1, y: 1 }, { x: item.width / 2, y: 20 }]);
            compare(colors[0], colors[1], "heading must not paint over the rounded top-left corner");
            compare(colors[2], colors[3], "heading must not paint over the rounded top-right corner");
            verify(!Qt.colorEqual(colors[0], colors[4]), "dialog header must be painted inside its corners");
        } finally { dialog.close(); }
    }
}
