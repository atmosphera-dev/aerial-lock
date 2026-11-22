// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QtTest
import InterActive
import InterActive.Foundation as IA
import InterActive.Playground
import InterActive.Tests

Item {
    id: scene
    width: 1200
    height: 820
    Main { id: workbench; visible: false }

    TestCase {
        name: "AppearanceCapture"
        when: windowShown
        property string destination
        property string manifest: ""
        property int count: 0

        function initTestCase(): void {
            failOnWarning(/.*(TypeError|ReferenceError|Binding loop|polish loop).*/);
            destination = TestProbe.captureDirectory();
            verify(destination.length > 0, "IA_CAPTURE_DIR must be an existing absolute writable directory");
            verify(TestProbe.freshPaletteSamples());
            PlaygroundState.resetFixtures();
            workbench.show();
        }

        function cleanupTestCase(): void {
            verify(TestProbe.writeCaptureManifest(manifest));
            compare(count, 10);
            workbench.close();
        }

        function init(): void {
            failOnWarning(/.*(TypeError|ReferenceError|Binding loop|polish loop).*/);
        }

        function cleanup(): void {
            const dialog = findChild(workbench, "appearanceWindow") as IADialog;
            dialog.close();
            tryCompare(dialog, "visible", false);
        }

        function test_nativeAppearance_data(): var {
            const rows = [];
            for (const scheme of ["light", "dark"])
                for (const contrast of ["normal", "high"])
                    for (const expanded of [false, true])
                        rows.push({tag: scheme + "-" + contrast + "-" + (expanded ? "expanded" : "closed"),
                            scheme: scheme, contrast: contrast, expanded: expanded, rtl: false});
            for (const expanded of [false, true])
                rows.push({tag: "light-normal-rtl-" + (expanded ? "expanded" : "closed"),
                    scheme: "light", contrast: "normal", expanded: expanded, rtl: true});
            return rows;
        }

        function test_nativeAppearance(data: var): void {
            workbench.rightToLeft = data.rtl;
            verify(PlaygroundState.setAppearance({scheme: data.scheme, contrast: data.contrast}));
            // Match the real default-scene launcher's existing temporary
            // calibration suggestions, without adding a launcher capture scene.
            verify(PlaygroundState.applyInitialPaletteSuggestions());
            const opener = findChild(workbench, "appearanceOpen") as IAToolButton;
            const dialog = findChild(workbench, "appearanceWindow") as IADialog;
            const editor = findChild(dialog, "appearanceEditor") as AppearanceEditor;
            verify(opener !== null && dialog !== null && editor !== null);
            tryVerify(function() { return TestProbe.exposed(opener); });
            verify(TestProbe.activateForInput(opener));
            verify(TestProbe.click(opener, opener.width / 2, opener.height / 2));
            tryCompare(dialog, "opened", true);
            compare(dialog.popupType, T.Popup.Window);
            tryVerify(function() { return TestProbe.exposed(editor); });
            const host = TestProbe.hostWindow(editor);
            verify(host !== null && host !== TestProbe.hostWindow(opener), "Actual separate native Appearance host");
            const toggle = findChild(editor, "appearanceEditorToggle") as IASwitch;
            compare(dialog.mirrored, data.rtl);
            compare(toggle.mirrored, data.rtl);
            if (toggle.checked !== data.expanded) {
                verify(TestProbe.activateForInput(toggle));
                verify(TestProbe.click(toggle, toggle.width / 2, toggle.height / 2));
            }
            compare(toggle.checked, data.expanded);
            compare((findChild(editor, "appearanceInputScroll") as IAScrollView).visible, data.expanded);
            compare(IA.Appearance.scheme, data.scheme);
            compare(IA.Appearance.contrast, data.contrast);
            verify(TestProbe.render(editor));
            const screen = TestProbe.screenAvailableGeometry(editor);
            verify(host.height > 0 && host.height <= screen.height, "Native dialog height is screen bounded");
            const close = findChild(dialog, "iaDialogClose") as IAToolButton;
            verify(close !== null && close.visible);
            const footer = dialog.standardButton(T.Dialog.Close);
            verify(footer !== null && footer.visible);
            const regions = [];
            for (const item of [dialog.header, findChild(editor, "appearanceSchemeRow"),
                findChild(editor, "appearancePaletteFlow"), findChild(editor, "appearanceSwitchRows"),
                findChild(editor, "paletteSamplePath"), findChild(editor, "appearanceInputScroll"), dialog.footer]) {
                const point = item.mapToItem(host.contentItem, 0, 0);
                regions.push({name: item.objectName, x: point.x, y: point.y,
                    width: item.width, height: item.height, visible: item.visible});
            }
            for (const name of ["iaTitle", "iaDialogClose", "appearanceLight", "appearanceDark", "appearanceReset",
                "accentPickerButton", "amberColorWell", "dangerColorWell", "savePaletteButton", "appearanceTouch",
                "appearanceContrast", "appearanceEditorToggle", "appearanceReducedMotion", "accentHexField", "appearanceJson"]) {
                const item = findChild(dialog, name) as Item;
                verify(item !== null);
                const point = item.mapToItem(host.contentItem, 0, 0);
                regions.push({name: name, x: point.x, y: point.y, width: item.width, height: item.height, visible: item.visible});
            }
            for (const item of [close, footer]) {
                const point = item.mapToItem(host.contentItem, 0, 0);
                verify(point.x >= 0 && point.y >= 0 && point.x + item.width <= host.width + 0.5
                    && point.y + item.height <= host.height + 0.5, "Outer chrome remains in the captured extent");
            }
            const nativeRoot = host.contentItem;
            compare(nativeRoot.x, 0);
            compare(nativeRoot.y, 0);
            compare(nativeRoot.width, host.width);
            compare(nativeRoot.height, host.height);
            const dpr = editor.Screen.devicePixelRatio;
            verify(dpr > 0);
            const pixels = grabImage(nativeRoot);
            compare(pixels.width, Math.round(host.width * dpr), "Whole native frame physical width");
            compare(pixels.height, Math.round(host.height * dpr), "Whole native frame physical height");
            const path = destination + "/appearance-" + data.tag + ".png";
            // QtTest saves throw on failure; the C++-created native root does
            // not need its own QML engine for this public capture method.
            pixels.save(path);
            ++count;
            const receipt = {file: "appearance-" + data.tag + ".png", width: host.width, height: host.height,
                physicalWidth: pixels.width, physicalHeight: pixels.height,
                dpr: dpr, scheme: data.scheme, contrast: data.contrast, expanded: data.expanded, rtl: data.rtl,
                input: PlaygroundState.appearanceInput,
                samplePath: (findChild(editor, "paletteSamplePath") as IALabel).text, regions: regions};
            manifest += JSON.stringify(receipt) + "\n";
            console.info("IA_APPEARANCE_CAPTURE " + JSON.stringify(receipt));
            dialog.close();
            tryCompare(dialog, "visible", false);
        }
    }
}
