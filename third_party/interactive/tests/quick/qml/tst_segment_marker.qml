// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import InterActive
import InterActive.Foundation as IA
import InterActive.Tests

Rectangle {
    id: scene
    width: 360
    height: 220
    color: IA.Appearance.colors.canvas
    property bool captureMarkers: false
    Component {
        id: tabs
        IATabBar {
            property bool rtl: false
            LayoutMirroring.enabled: rtl
            LayoutMirroring.childrenInherit: true
            width: 320
            IATabButton { text: "Current" }
            IATabButton { text: "Other" }
        }
    }
    Component {
        id: segments
        IASegmentedControl {
            property bool rtl: false
            LayoutMirroring.enabled: rtl
            LayoutMirroring.childrenInherit: true
            width: 320
            IASegmentedButton { text: "Current" }
            IASegmentedButton { text: "Other" }
        }
    }
    Component {
        id: reference
        IA.ControlBackground {
            outlined: false
            // Match the product's native unmerged batch class, without clipping.
            transform: Scale { xScale: 1 }
        }
    }
    Component {
        id: artwork
        IA.SegmentBackground {
            selected: true
            resolvedAppearance: IA.ControlAppearance { source: IA.Appearance; family: "tab"; controlRadius: 9 }
        }
    }
    TestCase {
        name: "SegmentMarker"
        when: windowShown
        function init(): void {
            failOnWarning(/.*(TypeError|ReferenceError|Binding loop|polish loop|Cannot anchor).*/);
        }
        function cleanup(): void {
            TestProbe.release(scene, 350, 210);
            TestProbe.move(scene, 350, 210);
            IA.Appearance.input = {};
        }
        function modes(): var {
            const rows = [];
            for (const kind of ["tabs", "segments"])
                for (const scheme of ["light", "dark"])
                    for (const contrast of ["normal", "high"])
                        for (const rtl of [false, true])
                            rows.push({tag: kind+"-"+scheme+"-"+contrast+"-"+rtl,
                                kind: kind, scheme: scheme, contrast: contrast, rtl: rtl});
            return rows;
        }
        function exactArtwork(bg): void {
            const frame = findChild(bg, "iaSegmentFrame") as IA.ControlBackground;
            verify(frame !== null);
            const nativeRoot = TestProbe.hostWindow(bg).contentItem;
            verify(TestProbe.render(bg));
            const actual = grabImage(nativeRoot);
            const plain = createTemporaryObject(reference, bg.parent, {
                x:bg.x, y:bg.y, z:bg.z, width:bg.width, height:bg.height,
                resolvedAppearance:bg.resolvedAppearance, controlEnabled:bg.controlEnabled,
                down:bg.down, hovered:bg.hovered, selected:bg.selected, active:bg.active,
                focused:bg.focused, outlined:bg.outlined, visible:false});
            verify(plain !== null);
            bg.visible = false; plain.visible = true;
            verify(TestProbe.render(plain));
            const normal = grabImage(nativeRoot);
            plain.color = IA.Appearance.colors.accent;
            verify(TestProbe.render(plain));
            const accent = grabImage(nativeRoot);
            plain.visible = false; bg.visible = true;
            const origin = bg.mapToItem(nativeRoot, 0, 0);
            const scale = actual.width / nativeRoot.width;
            const deviceSplit = (origin.x + bg.split) * scale;
            const candidates = [Math.floor(deviceSplit + 0.5), Math.ceil(deviceSplit - 0.5)];
            const x0 = Math.max(0, Math.floor((origin.x - 1) * scale));
            const x1 = Math.min(actual.width, Math.ceil((origin.x + bg.width + 1) * scale));
            const y0 = Math.max(0, Math.floor((origin.y - 1) * scale));
            const y1 = Math.min(actual.height, Math.ceil((origin.y + bg.height + 1) * scale));
            let matches = false;
            let mismatch = "";
            // A tie may assign the entire boundary column to either side.
            // One candidate must match the whole frame, never per-pixel choices.
            for (const split of candidates) {
                mismatch = "";
                for (let y = y0; y < y1 && !mismatch; ++y)
                    for (let x = x0; x < x1; ++x) {
                        const inside = bg.marked && (bg.mirrored ? x >= split : x < split);
                        const expected = (inside ? accent : normal).pixel(x,y);
                        if (!Qt.colorEqual(actual.pixel(x,y), expected)) {
                            mismatch = "Exact native partition pixel " + x + "," + y
                                + " split=" + split + " marked=" + bg.marked + " rtl=" + bg.mirrored
                                + " actual=" + actual.pixel(x,y) + " expected=" + expected;
                            break;
                        }
                    }
                if (!mismatch) { matches = true; break; }
            }
            plain.destroy();
            verify(matches, "No gap, overlap, altered fill or native frame pixel: " + mismatch);
        }
        function cap(button, marked: bool): void {
            compare(button.background.marked, marked);
            exactArtwork(button.background);
        }
        function test_currentMarker_data(): var { return modes(); }
        function test_currentMarker(data): void {
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast, reducedMotion: true};
            const group = createTemporaryObject(data.kind === "tabs" ? tabs : segments,
                scene, {x: 12, y: 12, rtl: data.rtl});
            verify(group !== null);
            verify(TestProbe.render(group));
            verify(TestProbe.activateForInput(group));
            const first = group.itemAt(0), other = group.itemAt(1), bg = first.background;
            TestProbe.move(scene, 350, 210);
            cap(first, true); cap(other, false);
            const originalSize = Qt.size(bg.width, bg.height);
            verify(TestProbe.move(other, other.width / 2, other.height / 2));
            tryCompare(other, "hovered", true);
            cap(first, true); cap(other, false);
            other.forceActiveFocus(Qt.TabFocusReason);
            compare(other.visualFocus, true);
            cap(first, true); cap(other, false);
            if (scene.captureMarkers) verify(TestProbe.captureWindow(first, data.tag+"-other-focus.png"));
            first.forceActiveFocus(Qt.TabFocusReason);
            compare(first.visualFocus, true);
            cap(first, true);
            if (scene.captureMarkers) verify(TestProbe.captureWindow(first, data.tag+"-current-focus.png"));
            compare(Qt.size(bg.width, bg.height), originalSize);
            verify(TestProbe.press(first, first.width/2, first.height/2));
            compare(first.down, true); cap(first, true);
            verify(TestProbe.release(first, first.width/2, first.height/2));
            first.enabled = false; cap(first, false); first.enabled = true;
            verify(TestProbe.click(other, other.width/2, other.height/2));
            compare(group.currentIndex, 1); cap(first, false); cap(other, true);
            other.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(group, data.rtl ? Qt.Key_Right : Qt.Key_Left));
            compare(group.currentIndex, 0); cap(first, true); cap(other, false);
            const identity = first.background;
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast === "high" ? "normal" : "high",
                density: "touch", reducedMotion: true, tokens: {colors: {accent: "#b351ba"}}};
            group.rtl = !data.rtl;
            group.width = 336;
            first.controlRadius = 9;
            cap(first, true); cap(other, false);
            compare(first.background, identity); compare(group.currentIndex, 0);
            compare(first.activeFocus, true);
            console.info("IA_MARKER_GEOMETRY " + JSON.stringify({tag:data.tag,
                dpr:TestProbe.effectiveDpr(first), width:bg.width, height:bg.height,
                radius:first.controlRadius, border:findChild(bg,"iaSegmentFrame").border.width, mirrored:first.mirrored}));
        }
        function test_fractionalTinyInactive_data(): var { return modes(); }
        function test_fractionalTinyInactive(data): void {
            IA.Appearance.input = {scheme:data.scheme, contrast:data.contrast};
            const bg = createTemporaryObject(artwork, scene, {x:12.5, y:80.25,
                width:155.5, height:48, mirrored:data.rtl});
            verify(bg !== null);
            for (const focused of [false, true]) {
                bg.focused = focused;
                for (const width of [0, 1, 3, 4, 7, 33.5, 155.5]) {
                    bg.width = width;
                    exactArtwork(bg);
                }
                bg.active = false; exactArtwork(bg);
                bg.hovered = true; exactArtwork(bg);
                bg.selected = false; exactArtwork(bg);
                bg.selected = true; bg.controlEnabled = false; exactArtwork(bg);
                bg.controlEnabled = true; bg.active = true; bg.hovered = false;
            }
        }
    }
}
