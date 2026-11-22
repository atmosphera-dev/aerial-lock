// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import QtTest
import InterActive
import InterActive.Components
import InterActive.Foundation as IA
import InterActive.Playground
import InterActive.Tests

Item {
    id: scene
    property bool editorRtl: false
    width: Window.window ? Window.window.width : 1200
    height: Window.window ? Window.window.height : 820
    ColumnLayout {
        id: layout
        anchors.fill: parent
        spacing: 0
        IAScrollView {
            id: editorScroll
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(editor.implicitHeight, 450)
            visible: false
            padding: 0
            contentWidth: availableWidth
            AppearanceEditor {
                id: editor
                width: parent.width
                LayoutMirroring.enabled: scene.editorRtl
                LayoutMirroring.childrenInherit: true
            }
        }
    }
    Component {
        id: pageComponent
        FoundationsPage { Layout.fillWidth: true; Layout.fillHeight: true }
    }
    component NarrowMenuWindow: Window {
        id: narrowHost
        required property int viewportWidth
        property bool rightToLeft: false
        LayoutMirroring.enabled: narrowHost.rightToLeft
        LayoutMirroring.childrenInherit: true
        flags: Qt.Dialog
        visible: false
        width: viewportWidth
        height: 820
        minimumWidth: viewportWidth
        maximumWidth: viewportWidth
        minimumHeight: 820
        maximumHeight: 820
        IAScrollView {
            objectName: "narrowAppearanceScroll"
            anchors.fill: parent
            padding: 0
            contentWidth: availableWidth
            AppearanceEditor { objectName: "narrowEditor"; width: parent.width }
        }
    }
    NarrowMenuWindow { id: narrow320Window; viewportWidth: 320 }
    NarrowMenuWindow { id: narrow360Window; viewportWidth: 360 }
    Main { id: consumerWindow; visible: false }
    Main { id: directionWindow; visible: false }

    Window {
        id: inputHostWindow
        visible: false
        width: 320
        height: 180
        flags: Qt.Dialog
        IATextField { objectName: "inputHostField"; x: 20; y: 40; width: 260 }
    }

    SignalSpy { id: inputSpy; target: PlaygroundState; signalName: "appearanceInputChanged" }
    SignalSpy { id: menuOpenedSpy; signalName: "opened" }
    SignalSpy { id: choiceActivatedSpy; signalName: "activated" }
    SignalSpy { id: switchToggledSpy; signalName: "toggled" }

    TestCase {
        name: "PlaygroundConsumers"
        when: windowShown
        property FoundationsPage page
        readonly property var references: [
            {key: "blue", name: "Primary", tone: IA.Presentation.Primary, role: "primary", seed: "accent"},
            {key: "amber", name: "Caution", tone: IA.Presentation.Caution, role: "caution", seed: "warning"},
            {key: "rose", name: "Danger", tone: IA.Presentation.Destructive, role: "destructive", seed: "error"}
        ]

        function init(): void {
            verify(TestProbe.freshPaletteSamples());
            PlaygroundState.resetFixtures();
            verify(PlaygroundState.setAppearance({}));
            page = createTemporaryObject(pageComponent, layout) as FoundationsPage;
            verify(page !== null);
            compare(page.state, PlaygroundState);
            verify(TestProbe.render(page));
            verify(TestProbe.activate(page));
        }

        function cleanup(): void {
            const accentMenu = findChild(editor, "accentPaletteMenu") as AccentPaletteMenu;
            verify(accentMenu !== null);
            accentMenu.close();
            tryCompare(accentMenu, "visible", false);
            menuOpenedSpy.target = null;
            choiceActivatedSpy.target = null;
            switchToggledSpy.target = null;
            scene.editorRtl = false;
            for (const role of ["accent", "amber", "danger"]) {
                const colorDialog = findChild(editor, role + "ColorDialog") as IAColorDialog;
                if (colorDialog !== null && colorDialog.visible) {
                    colorDialog.close();
                    tryCompare(colorDialog, "visible", false);
                }
            }
            verify(TestProbe.paletteSampleLock(false));
            editor.accentDraft = undefined;
            editor.jsonDraft = "{}";
            const inputToggle = findChild(editor, "appearanceEditorToggle") as IASwitch;
            if (inputToggle !== null)
                inputToggle.checked = false;
            editorScroll.visible = false;
            for (const name of ["deliverySubmenu", "mixedMenu", "editDialog"]) {
                const popup = findChild(page, name) as T.Popup;
                verify(popup !== null);
                popup.close();
                tryCompare(popup, "visible", false);
            }
        }

        function click(item: Item): void {
            verify(item !== null);
            if (item.mapToItem(editor, 0, 0).x >= 0 && item.mapToItem(editor, 0, 0).y >= 0
                && isEditorItem(item))
                revealEditor(item);
            verify(TestProbe.click(item, item.width / 2, item.height / 2));
        }

        function isEditorItem(item: Item): bool {
            for (let parent = item.parent; parent !== null; parent = parent.parent)
                if (parent === editor)
                    return true;
            return false;
        }

        function accentMenuFor(owner: AppearanceEditor): AccentPaletteMenu {
            const menu = findChild(owner, "accentPaletteMenu") as AccentPaletteMenu;
            verify(menu !== null);
            return menu;
        }

        function revealMenuItem(menu: AccentPaletteMenu, index: int): IAMenuItem {
            const view = menu.contentItem as ListView;
            verify(view !== null);
            view.positionViewAtIndex(index, ListView.Contain);
            verify(TestProbe.render(view));
            // Item popups reparent rows into the overlay: use Menu's public itemAt.
            const row = menu.itemAt(index) as IAMenuItem;
            verify(row !== null);
            return row;
        }

        function openCustomAccent(): void {
            const opener = findChild(editor, "accentPickerButton") as IAButton;
            click(opener);
            const menu = accentMenuFor(editor);
            tryCompare(menu, "opened", true);
            compare(menu.count, 13);
            const custom = revealMenuItem(menu, 12);
            compare(custom.objectName, "accentPaletteCustom");
            verify(TestProbe.click(custom, custom.width / 2, custom.height / 2));
            tryCompare(menu, "visible", false);
            tryCompare(findChild(editor, "accentColorDialog") as IAColorDialog, "visible", true);
        }

        function test_deliveryComboDraftAndCommit(): void {
            const combo = findChild(page, "deliveryCombo") as IAComboBox;
            verify(combo !== null);
            choiceActivatedSpy.target = combo;
            choiceActivatedSpy.clear();
            PlaygroundState.delivery = "weekly";
            compare(combo.currentValue, "weekly");
            compare(choiceActivatedSpy.count, 0);
            verify(TestProbe.activateForInput(combo));
            combo.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(combo, Qt.Key_Up));
            compare(combo.currentValue, "daily");
            compare(PlaygroundState.delivery, "daily");
            compare(choiceActivatedSpy.count, 1);
            verify(TestProbe.keyClick(combo, Qt.Key_Down));
            compare(PlaygroundState.delivery, "weekly");
            PlaygroundState.applyDraft();
            verify(PlaygroundState.committedSummary.toLowerCase().indexOf("weekly") >= 0);
            compare(PlaygroundState.dirty, false);
        }

        function test_toolbarGeometry_data(): var {
            const rows = [];
            for (const width of [320, 360, 1200])
                for (const touch of [false, true])
                    rows.push({tag: width + "-" + touch, width: width, touch: touch});
            return rows;
        }

        function test_appearanceSchemeChoice_data(): var {
            return [{tag: "ltr", rtl: false}, {tag: "rtl", rtl: true}];
        }

        function test_appearanceSchemeChoice(data: var): void {
            editorScroll.visible = true;
            scene.editorRtl = data.rtl;
            const choice = findChild(editor, "appearanceSchemeChoice") as IASegmentedControl;
            const light = findChild(editor, "appearanceLight") as IASegmentedButton;
            const dark = findChild(editor, "appearanceDark") as IASegmentedButton;
            const field = findChild(page, "nameField") as IATextField;
            verify(choice !== null && light !== null && dark !== null && field !== null);
            choiceActivatedSpy.target = choice;
            choiceActivatedSpy.clear();
            inputSpy.clear();
            click(dark);
            compare(choice.currentIndex, 1);
            verify(dark.checked && !light.checked);
            compare(IA.Appearance.scheme, "dark");
            compare(PlaygroundState.appearanceInput.scheme, "dark");
            compare(choiceActivatedSpy.count, 1);
            compare(inputSpy.count, 1);
            verify(TestProbe.render(page));
            compare((field.background as Rectangle).color, IA.Appearance.colors.content);
            const darkSurface = String((field.background as Rectangle).color);
            verify(TestProbe.activateForInput(dark));
            verify(TestProbe.keyClick(dark, data.rtl ? Qt.Key_Right : Qt.Key_Left));
            compare(choice.currentIndex, 0);
            compare(IA.Appearance.scheme, "light");
            verify(light.checked && !dark.checked);
            compare(choiceActivatedSpy.count, 2);
            verify(TestProbe.render(page));
            verify(String((field.background as Rectangle).color) !== darkSurface);
            verify(TestProbe.keyClick(light, data.rtl ? Qt.Key_Left : Qt.Key_Right));
            compare(IA.Appearance.scheme, "dark");
            compare(choiceActivatedSpy.count, 3);
            // Space on the selected native radio button cannot clear selection.
            verify(TestProbe.keyClick(dark, Qt.Key_Space));
            verify(dark.checked && !light.checked);
            compare(choiceActivatedSpy.count, 3);
            verify(TestProbe.keyClick(dark, Qt.Key_Backtab, Qt.ShiftModifier));
            tryCompare(light, "activeFocus", true);
            verify(TestProbe.keyClick(light, Qt.Key_Space));
            compare(IA.Appearance.scheme, "light");
            compare(choice.currentIndex, 0);
            compare(choiceActivatedSpy.count, 4, "Space selects the keyboard-reached native segment");
            for (const scheme of ["dark", "light", "dark"]) {
                inputSpy.clear();
                verify(PlaygroundState.setAppearanceJson(JSON.stringify({scheme: scheme})));
                verify(TestProbe.render(editor));
                compare(choice.currentIndex, scheme === "dark" ? 1 : 0);
                compare(light.checked, scheme === "light");
                compare(dark.checked, scheme === "dark");
                compare(inputSpy.count, 1, "External replacement is not echoed by selection synchronization");
                compare(choiceActivatedSpy.count, 4);
                compare(findChild(editor, "appearanceLight"), light);
                compare(findChild(editor, "appearanceDark"), dark);
            }
        }

        function test_appearanceIndependentSwitches(): void {
            editorScroll.visible = true;
            const names = ["appearanceTouch", "appearanceContrast", "appearanceEditorToggle", "appearanceReducedMotion"];
            const rows = names.map(function(name) { return findChild(editor, name) as IASwitch; });
            for (const row of rows) {
                verify(row !== null);
                compare(row.indicatorPosition, IA.Presentation.Trailing);
                compare(row.checked, false);
            }
            const json = findChild(editor, "appearanceJson") as IATextField;
            const hex = findChild(editor, "accentHexField") as IATextField;
            editor.jsonDraft = '{"tokens":';
            editor.accentDraft = "#123abc";
            for (let index = 0; index < rows.length; ++index) {
                click(rows[index]);
                for (let peer = 0; peer < rows.length; ++peer)
                    compare(rows[peer].checked, peer <= index, "Only the activated independent row changes");
            }
            compare(IA.Appearance.density, "touch");
            compare(IA.Appearance.contrast, "high");
            compare(IA.Appearance.reducedMotion, true);
            compare((findChild(editor, "appearanceInputScroll") as IAScrollView).visible, true);
            // The existing resolved setting is the consumer contract; native
            // switch visuals must also show its on endpoint.
            compare(rows[3].position, 1);
            for (const index of [0, 1, 2, 3]) {
                const row = rows[index];
                const peers = rows.map(function(peer) { return peer.checked; });
                revealEditor(row);
                switchToggledSpy.target = row;
                switchToggledSpy.clear();
                const indicator = row.indicator;
                verify(TestProbe.press(indicator, indicator.width * 0.8, indicator.height / 2));
                verify(TestProbe.move(indicator, indicator.width * 0.05, indicator.height / 2));
                verify(TestProbe.release(indicator, indicator.width * 0.05, indicator.height / 2));
                compare(row.checked, false, "Native switch drag reaches the off endpoint");
                compare(switchToggledSpy.count, 1);
                for (let peer = 0; peer < rows.length; ++peer)
                    if (peer !== index) compare(rows[peer].checked, peers[peer], "Dragging never changes peers");
            }
            compare(IA.Appearance.density, "compact");
            compare(IA.Appearance.contrast, "normal");
            compare(IA.Appearance.reducedMotion, false);
            click(rows[2]);
            compare(rows[2].checked, true);
            for (const row of [rows[0], rows[1], rows[3]]) {
                switchToggledSpy.target = row;
                switchToggledSpy.clear();
                inputSpy.clear();
                verify(PlaygroundState.setAppearance({density: "touch", contrast: "high", reducedMotion: true}));
                verify(TestProbe.render(editor));
                compare(row.checked, true);
                compare(switchToggledSpy.count, 0, "Resolved checked changes are not user toggles");
                compare(inputSpy.count, 1);
                verify(PlaygroundState.setAppearance({}));
            }
            click(rows[2]);
            compare(rows[2].checked, false);
            compare((findChild(editor, "appearanceInputScroll") as IAScrollView).visible, false);
            click(rows[2]);
            compare(findChild(editor, "appearanceJson"), json);
            compare(findChild(editor, "accentHexField"), hex);
            compare(json.text, '{"tokens":');
            compare(hex.text, "#123abc");
            click(findChild(editor, "appearanceDark") as IASegmentedButton);
            click(findChild(editor, "appearanceReset") as IAButton);
            compare(PlaygroundState.appearanceInput, {});
            compare(rows[2].checked, true);
            compare(json.text, '{"tokens":');
            compare(hex.text, "#123abc");
        }

        function test_actualMainDirectionAndLive_data(): var {
            return [{tag: "initial-ltr", rtl: false}, {tag: "initial-rtl", rtl: true}];
        }

        function assertMainDirection(owner: AppearanceEditor, dialog: IADialog, rtl: bool): void {
            // opened is a Qt logical-state signal; a recreated native surface
            // must be exposed before the existing frame-observation assertion.
            tryVerify(function() { return TestProbe.exposed(owner); });
            verify(TestProbe.render(owner));
            const host = TestProbe.hostWindow(owner);
            verify(host !== null && host !== TestProbe.hostWindow(findChild(directionWindow, "appearanceOpen") as Item));
            compare(dialog.popupType, T.Popup.Window);
            compare(dialog.mirrored, rtl);
            const choice = findChild(owner, "appearanceSchemeChoice") as IASegmentedControl;
            const lane = findChild(owner, "appearanceSwitchRows") as ColumnLayout;
            compare(choice.mirrored, rtl);
            fuzzyCompare(lane.mapToItem(owner, 0, 0).x, rtl ? owner.width - lane.width : 0, 0.5);
            for (const name of ["appearanceTouch", "appearanceContrast", "appearanceEditorToggle", "appearanceReducedMotion"]) {
                const row = findChild(owner, name) as IASwitch;
                compare(row.mirrored, rtl);
                const point = row.indicator.mapToItem(row, 0, 0);
                fuzzyCompare(row.indicator.width, row.indicator.implicitWidth, 0.5,
                    "Live direction preserves the native indicator's natural width");
                fuzzyCompare(row.indicator.height, row.indicator.implicitHeight, 0.5);
                fuzzyCompare(point.x, rtl ? row.leftPadding : row.width - row.rightPadding - row.indicator.width, 0.5);
                const nativePoint = row.mapToItem(host.contentItem, 0, 0);
                verify(nativePoint.x >= 0 && nativePoint.x + row.width <= host.width + 0.5);
            }
            const close = findChild(dialog, "iaDialogClose") as IAToolButton;
            const title = findChild(dialog, "iaTitle") as IALabel;
            const footer = dialog.footer as IADialogButtonBox;
            const dismiss = dialog.standardButton(T.Dialog.Close);
            compare(close.mirrored, rtl);
            compare(footer.mirrored, rtl);
            const closePoint = close.mapToItem(dialog.header, 0, 0);
            fuzzyCompare(closePoint.x, rtl ? 0 : dialog.header.width - close.width, 0.5);
            const titlePoint = title.mapToItem(dialog.header, 0, 0);
            if (rtl)
                fuzzyCompare(titlePoint.x + title.width, dialog.header.width - dialog.rightPadding, 0.5);
            else
                fuzzyCompare(titlePoint.x, dialog.leftPadding, 0.5);
            const footerPoint = dismiss.mapToItem(footer, 0, 0);
            fuzzyCompare(footerPoint.x, rtl ? footer.leftPadding : footer.width - footer.rightPadding - dismiss.width, 0.5);
            const choicePoint = choice.mapToItem(owner, 0, 0);
            fuzzyCompare(choicePoint.x, rtl ? owner.width - choice.width : 0, 0.5);
            console.info("IA_MAIN_DIRECTION " + JSON.stringify({rtl: rtl, dialog: dialog.mirrored,
                choice: choice.mirrored, laneX: lane.mapToItem(owner, 0, 0).x,
                editorWidth: owner.width, laneWidth: lane.width, headerCloseX: closePoint.x,
                footerCloseX: footerPoint.x, hostWidth: host.width, hostHeight: host.height}));
        }

        function test_actualMainDirectionAndLive(data: var): void {
            directionWindow.rightToLeft = data.rtl;
            directionWindow.show();
            const opener = findChild(directionWindow, "appearanceOpen") as IAToolButton;
            const dialog = findChild(directionWindow, "appearanceWindow") as IADialog;
            const owner = findChild(dialog, "appearanceEditor") as AppearanceEditor;
            try {
                tryVerify(function() { return TestProbe.exposed(opener); });
                verify(TestProbe.activateForInput(opener));
                verify(TestProbe.click(opener, opener.width / 2, opener.height / 2));
                tryCompare(dialog, "opened", true);
                tryVerify(function() { return TestProbe.exposed(owner); });
                assertMainDirection(owner, dialog, data.rtl);
                const toggle = findChild(owner, "appearanceEditorToggle") as IASwitch;
                if (!toggle.checked)
                    verify(TestProbe.click(toggle, toggle.width / 2, toggle.height / 2));
                compare(toggle.checked, true);
                verify(TestProbe.render(owner));
                const dark = findChild(owner, "appearanceDark") as IASegmentedButton;
                verify(TestProbe.click(dark, dark.width / 2, dark.height / 2));
                compare(IA.Appearance.scheme, "dark");
                const hex = findChild(owner, "accentHexField") as IATextField;
                const json = findChild(owner, "appearanceJson") as IATextField;
                edit(hex, "#123abc");
                edit(json, '{"tokens":');
                verify(TestProbe.keyClick(json, Qt.Key_A, Qt.ControlModifier));
                const selection = json.selectedText;
                const input = JSON.stringify(PlaygroundState.appearanceInput);
                const nativeHost = TestProbe.hostWindow(owner);
                const names = ["appearanceSchemeChoice", "appearanceLight", "appearanceDark", "appearanceSwitchRows",
                    "appearanceTouch", "appearanceContrast", "appearanceEditorToggle", "appearanceReducedMotion", "accentHexField", "appearanceJson"];
                const identities = names.map(function(name) { return findChild(owner, name); });
                for (const rtl of [data.rtl, !data.rtl, data.rtl]) {
                    directionWindow.rightToLeft = rtl;
                    assertMainDirection(owner, dialog, rtl);
                    compare(TestProbe.hostWindow(owner), nativeHost, "Live direction does not replace the native host");
                    for (let index = 0; index < names.length; ++index)
                        compare(findChild(owner, names[index]), identities[index]);
                    compare(owner.jsonDraft, '{"tokens":');
                    compare(owner.accentDraft, "#123abc");
                    compare(json.text, '{"tokens":');
                    compare(hex.text, "#123abc");
                    compare(json.selectedText, selection);
                    compare(json.activeFocus, true, "Live mirroring retains the actually edited field's focus");
                    compare(toggle.checked, true);
                    compare((findChild(owner, "appearanceSchemeChoice") as IASegmentedControl).currentIndex, 1);
                    compare(JSON.stringify(PlaygroundState.appearanceInput), input, "Direction is not an appearance write");
                }
                const dismiss = dialog.standardButton(T.Dialog.Close);
                verify(TestProbe.click(dismiss, dismiss.width / 2, dismiss.height / 2));
                tryCompare(dialog, "visible", false);
                verify(TestProbe.activateForInput(opener));
                verify(TestProbe.click(opener, opener.width / 2, opener.height / 2));
                tryCompare(dialog, "opened", true);
                assertMainDirection(owner, dialog, data.rtl);
                compare(owner.jsonDraft, '{"tokens":');
                compare(owner.accentDraft, "#123abc");
                compare(toggle.checked, true);
                for (let index = 0; index < names.length; ++index)
                    compare(findChild(owner, names[index]), identities[index]);
            } finally {
                dialog.close();
                tryCompare(dialog, "visible", false);
                directionWindow.close();
                verify(TestProbe.activate(page));
            }
        }

        function test_appearanceRowsGeometry_data(): var {
            const rows = [];
            for (const width of [320, 360, 1200])
                for (const touch of [false, true])
                    for (const rtl of [false, true])
                        rows.push({tag: width + "-" + touch + "-" + rtl, width: width, touch: touch, rtl: rtl});
            return rows;
        }

        function test_appearanceRowsGeometry(data: var): void {
            verify(PlaygroundState.setAppearance({density: data.touch ? "touch" : "compact", fontScale: data.touch ? 1.5 : 1}));
            const host = data.width === 320 ? narrow320Window : data.width === 360 ? narrow360Window : consumerWindow;
            host.rightToLeft = data.rtl;
            host.show();
            const dialog = data.width === 1200 ? findChild(host, "appearanceWindow") as IADialog : null;
            try {
                let owner = findChild(host, data.width === 1200 ? "appearanceEditor" : "narrowEditor") as AppearanceEditor;
                if (dialog !== null) {
                    const opener = findChild(host, "appearanceOpen") as IAToolButton;
                    tryVerify(function() { return TestProbe.exposed(opener); });
                    verify(TestProbe.activateForInput(opener));
                    verify(TestProbe.click(opener, opener.width / 2, opener.height / 2));
                    tryCompare(dialog, "opened", true);
                }
                verify(owner !== null);
                tryVerify(function() { return TestProbe.exposed(owner); });
                verify(TestProbe.render(owner));
                const lane = findChild(owner, "appearanceSwitchRows") as ColumnLayout;
                verify(lane !== null);
                fuzzyCompare(lane.width, Math.min(360, owner.width), 0.5);
                let previousBottom = -1;
                for (const name of ["appearanceTouch", "appearanceContrast", "appearanceEditorToggle", "appearanceReducedMotion"]) {
                    const row = findChild(owner, name) as IASwitch;
                    verify(row !== null);
                    const label = row.contentItem as Text;
                    const indicator = row.indicator;
                    compare(label.wrapMode, Text.NoWrap);
                    compare(label.elide, Text.ElideRight);
                    compare(row.mirrored, data.rtl);
                    fuzzyCompare(row.width, lane.width, 0.5);
                    const edge = indicator.mapToItem(row, 0, 0).x;
                    fuzzyCompare(edge, data.rtl ? row.leftPadding : row.width - row.rightPadding - indicator.width, 0.5);
                    fuzzyCompare(indicator.mapToItem(row, 0, indicator.height / 2).y,
                        label.mapToItem(row, 0, label.height / 2).y, 0.5);
                    const position = row.mapToItem(owner, 0, 0);
                    verify(position.x >= -0.5 && position.x + row.width <= owner.width + 0.5);
                    verify(position.y >= previousBottom);
                    previousBottom = position.y + row.height;
                    if (data.touch) verify(row.height >= 44);
                    // Scroll the actual owning viewport, then require native
                    // input ownership and click/Tab reachability without focus repair.
                    const scroll = findChild(dialog !== null ? dialog : host,
                        dialog !== null ? "iaDialogScroll" : "narrowAppearanceScroll") as IAScrollView;
                    verify(scroll !== null);
                    const flick = scroll.contentItem as Flickable;
                    verify(flick !== null);
                    const center = row.mapToItem(flick.contentItem, 0, row.height / 2).y;
                    flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, center - flick.height / 2));
                    verify(TestProbe.render(owner));
                    const point = row.mapToItem(TestProbe.hostWindow(owner).contentItem, 0, 0);
                    verify(point.x >= -0.5 && point.x + row.width <= host.width + 0.5);
                    verify(point.y >= -0.5 && point.y + row.height <= TestProbe.hostWindow(owner).height + 0.5);
                    verify(TestProbe.activateForInput(row));
                    verify(TestProbe.click(row, row.width / 2, row.height / 2));
                    tryCompare(row, "activeFocus", true);
                    verify(TestProbe.keyClick(row, Qt.Key_Tab));
                    verify(!row.activeFocus, "Tab leaves the current visible row");
                    // Restore the requested density without changing the next row's peers.
                    verify(PlaygroundState.setAppearance({density: data.touch ? "touch" : "compact", fontScale: data.touch ? 1.5 : 1}));
                    verify(TestProbe.render(owner));
                }
                const choice = findChild(owner, "appearanceSchemeChoice") as IASegmentedControl;
                const reset = findChild(owner, "appearanceReset") as IAButton;
                const choicePoint = choice.mapToItem(owner, 0, 0);
                const resetPoint = reset.mapToItem(owner, 0, 0);
                verify(choicePoint.x >= -0.5 && choicePoint.x + choice.width <= owner.width + 0.5);
                verify(resetPoint.x >= -0.5 && resetPoint.x + reset.width <= owner.width + 0.5);
                verify(choicePoint.x + choice.width <= resetPoint.x + 0.5 || resetPoint.x + reset.width <= choicePoint.x + 0.5);
                fuzzyCompare(choicePoint.y + choice.height / 2, resetPoint.y + reset.height / 2, 0.5);
                const outer = findChild(dialog !== null ? dialog : host,
                    dialog !== null ? "iaDialogScroll" : "narrowAppearanceScroll") as IAScrollView;
                const viewport = outer.contentItem as Flickable;
                viewport.contentY = 0;
                verify(TestProbe.render(owner));
                const light = findChild(owner, "appearanceLight") as IASegmentedButton;
                verify(TestProbe.activateForInput(light));
                verify(TestProbe.click(light, light.width / 2, light.height / 2));
                tryCompare(light, "activeFocus", true);
                verify(TestProbe.keyClick(light, data.rtl ? Qt.Key_Left : Qt.Key_Right));
                compare(choice.currentIndex, 1);
                compare(IA.Appearance.scheme, "dark");
                verify(TestProbe.render(owner));
                const selected = choice.currentItem;
                const selectedPoint = selected.mapToItem(choice.contentItem, 0, 0);
                verify(selectedPoint.x >= -0.5 && selectedPoint.x + selected.width <= choice.contentItem.width + 0.5,
                    "Native segmented viewport reveals its keyboard-selected choice at narrow widths");
                console.info("IA_APPEARANCE_ROWS " + JSON.stringify({width: data.width, touch: data.touch,
                    rtl: data.rtl, editorWidth: owner.width, laneWidth: lane.width, choiceWidth: choice.width}));
            } finally {
                if (dialog !== null) dialog.close();
                host.close();
                host.rightToLeft = false;
                verify(TestProbe.activate(page));
            }
        }

        function test_toolbarGeometry(data: var): void {
            verify(PlaygroundState.setAppearance({density: data.touch ? "touch" : "compact", fontScale: data.touch ? 1.5 : 1}));
            const component = Qt.createComponent("qrc:/qt/qml/InterActive/Playground/AppearanceToolbar.qml");
            tryCompare(component, "status", Component.Ready);
            const toolbar = createTemporaryObject(component, scene, {width: data.width}) as AppearanceToolbar;
            verify(toolbar !== null);
            verify(TestProbe.render(toolbar));
            const items = toolbar.contentItem.children;
            compare(items.length, 4, "Exactly three tools and one status, with no additional toolbar content");
            const center = items[0].mapToItem(toolbar, 0, items[0].height / 2).y;
            for (const item of items) {
                const point = item.mapToItem(toolbar, 0, 0);
                verify(point.x >= 0 && point.x + item.width <= data.width + 0.5);
                fuzzyCompare(point.y + item.height / 2, center, 0.5);
                const button = item as IAToolButton;
                if (button !== null) {
                    fuzzyCompare(button.width, button.height, 0.5, "Square tool slot");
                    if (data.touch)
                        verify(button.width >= 44 && button.height >= 44);
                }
            }
            const button = findChild(toolbar, "toolbarAccentButton") as IAToolButton;
            const swatch = findChild(toolbar, "toolbarAccentSwatch") as Rectangle;
            const point = swatch.mapToItem(button, 0, 0);
            fuzzyCompare(point.x, button.width - point.x - swatch.width, 0.5);
            fuzzyCompare(point.y, button.height - point.y - swatch.height, 0.5);
            fuzzyCompare(point.x, point.y, 0.5, "Uniform swatch facing-edge clearance");
            compare((findChild(toolbar, "toolbarStatus") as IALabel).wrapMode, Text.NoWrap);
        }

        function test_toolbarAndAppearanceWindow(): void {
            consumerWindow.show();
            const toolbar = findChild(consumerWindow, "appearanceToolbar") as AppearanceToolbar;
            const dialog = findChild(consumerWindow, "appearanceWindow") as IADialog;
            const owner = findChild(dialog, "appearanceEditor") as AppearanceEditor;
            verify(toolbar !== null && dialog !== null && owner !== null);
            tryVerify(function() { return TestProbe.exposed(toolbar); });
            verify(TestProbe.activate(toolbar));
            try {
                const buttons = toolbar.contentItem.children.filter(function(item) { return (item as IAToolButton) !== null; });
                compare(buttons.length, 3);
                compare(buttons.map(function(item) { return item.objectName; }), ["appearanceScheme", "toolbarAccentButton", "appearanceOpen"]);
                const status = findChild(toolbar, "toolbarStatus") as IALabel;
                verify(status !== null);
                compare(status.text, "0 saved accents");
                compare(status.wrapMode, Text.NoWrap);
                compare(status.foregroundColor, IA.Appearance.colors.secondaryText);
                verify(TestProbe.render(toolbar));
                const centers = buttons.concat([status]).map(function(item) { return item.mapToItem(toolbar, 0, item.height / 2).y; });
                for (const center of centers)
                    fuzzyCompare(center, centers[0], 0.5, "One toolbar centerline");
                const rail = findChild(consumerWindow, "navigationRail") as IAPane;
                const current = findChild(consumerWindow, "navigationCurrentPage") as IAItemDelegate;
                const heading = findChild(consumerWindow, "navigationHeading") as IALabel;
                const subtitle = findChild(consumerWindow, "navigationSubtitle") as IALabel;
                verify(rail !== null && current !== null && heading !== null && subtitle !== null);
                compare(rail.backgroundColor, IA.Appearance.colors.control);
                compare(current.highlighted, true);
                const delegateComponent = Qt.createComponent("qrc:/qt/qml/InterActive/IAItemDelegate.qml");
                tryCompare(delegateComponent, "status", Component.Ready);
                const ordinaryRow = createTemporaryObject(delegateComponent, scene) as IAItemDelegate;
                verify(ordinaryRow !== null);
                compare(current.selectedBackgroundColor, ordinaryRow.selectedBackgroundColor, "Unmodified library selection role");
                verify(TestProbe.move(toolbar, toolbar.width - 2, toolbar.height / 2));
                tryCompare(current, "hovered", false);
                tryVerify(function() { return consumerWindow.active; });
                compare((current.background as Rectangle).color, current.selectedBackgroundColor);
                const label = findChild(current, "iaLabel") as Text;
                verify(label !== null);
                fuzzyCompare(heading.mapToItem(rail, heading.leftPadding, 0).x, label.mapToItem(rail, 0, 0).x, 0.5);
                fuzzyCompare(subtitle.mapToItem(rail, subtitle.leftPadding, 0).x, label.mapToItem(rail, 0, 0).x, 0.5);
                verify(TestProbe.click(buttons[0], buttons[0].width / 2, buttons[0].height / 2));
                compare(IA.Appearance.scheme, "dark");
                verify(TestProbe.click(buttons[0], buttons[0].width / 2, buttons[0].height / 2));
                compare(IA.Appearance.scheme, "light");
                verify(TestProbe.click(buttons[1], buttons[1].width / 2, buttons[1].height / 2));
                const menu = accentMenuFor(owner);
                tryCompare(menu, "opened", true);
                const preset = revealMenuItem(menu, 1);
                verify(TestProbe.click(preset, preset.width / 2, preset.height / 2));
                tryCompare(menu, "visible", false);
                compare((findChild(toolbar, "toolbarAccentSwatch") as Rectangle).color, "#245bdb");
                verify(TestProbe.click(buttons[1], buttons[1].width / 2, buttons[1].height / 2));
                tryCompare(menu, "opened", true);
                const custom = revealMenuItem(menu, 12);
                verify(TestProbe.click(custom, custom.width / 2, custom.height / 2));
                tryCompare(menu, "visible", false);
                const picker = findChild(owner, "accentColorDialog") as IAColorDialog;
                assertPickerColor(picker, IA.Appearance.colors.accent);
                compare(dialog.visible, false, "Toolbar Custom does not require opening Appearance");
                picker.close();
                tryCompare(picker, "visible", false);
                verify(TestProbe.activate(toolbar));
                verify(TestProbe.click(buttons[2], buttons[2].width / 2, buttons[2].height / 2));
                tryCompare(dialog, "opened", true);
                compare(dialog.popupType, T.Popup.Window);
                tryVerify(function() { return TestProbe.exposed(owner); });
                verify(TestProbe.hostWindow(owner) !== TestProbe.hostWindow(toolbar), "Appearance is a real separate IA dialog window");
                verify(TestProbe.render(owner));
                for (const name of ["appearanceLight", "appearanceDark", "appearanceReset", "accentPickerButton", "resetAccentButton",
                    "amberColorWell", "dangerColorWell", "savePaletteButton",
                    "appearanceTouch", "appearanceContrast", "appearanceEditorToggle", "appearanceReducedMotion", "paletteSamplePath", "paletteHistoryStatus",
                    "paletteRootExplanation", "paletteSaveStatus", "accentHexField", "applyAccentHex", "appearanceJson", "appearanceInspector"])
                    verify(findChild(owner, name) !== null, name + " retained in Appearance window");
                const touch = findChild(owner, "appearanceTouch") as IASwitch;
                const preview = findChild(owner, "appearanceContrast") as IASwitch;
                const toggle = findChild(owner, "appearanceEditorToggle") as IASwitch;
                const save = findChild(owner, "savePaletteButton") as IAButton;
                verify(TestProbe.click(save, save.width / 2, save.height / 2));
                compare(TestProbe.paletteSamples().length, 1);
                compare(status.text, "1 saved accents");
                verify(TestProbe.click(touch, touch.width / 2, touch.height / 2));
                compare(IA.Appearance.density, "touch");
                verify(TestProbe.render(owner));
                verify(TestProbe.click(preview, preview.width / 2, preview.height / 2));
                compare(IA.Appearance.contrast, "high");
                verify(TestProbe.render(owner));
                verify(TestProbe.click(toggle, toggle.width / 2, toggle.height / 2));
                compare(toggle.checked, true);
                owner.accentDraft = "#123abc";
                const json = findChild(owner, "appearanceJson") as IATextField;
                const identity = json;
                verify(TestProbe.render(owner));
                const inputScroll = findChild(owner, "appearanceInputScroll") as IAScrollView;
                const outerScroll = findChild(dialog, "iaDialogScroll") as IAScrollView;
                verify(inputScroll !== null && outerScroll !== null);
                for (const entry of [{scroll: inputScroll, item: json}, {scroll: outerScroll, item: inputScroll}]) {
                    const flick = entry.scroll.contentItem as Flickable;
                    verify(flick !== null);
                    const center = entry.item.mapToItem(flick.contentItem, 0, entry.item.height / 2).y;
                    flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, center - flick.height / 2));
                    verify(TestProbe.render(owner));
                }
                const host = TestProbe.hostWindow(owner);
                const point = json.mapToItem(host.contentItem, 0, 0);
                verify(point.x >= 0 && point.x + json.width <= host.width + 0.5);
                verify(point.y >= 0 && point.y + json.height <= host.height + 0.5, "Moved JSON editor is reachable in the actual native viewport");
                const queue = [inputScroll];
                while (queue.length > 0) {
                    const item = queue.pop() as Item;
                    const field = item as IATextField;
                    const button = item as IAButton;
                    if (field !== null || (button !== null && button.text === "Set")) {
                        for (const scroll of [inputScroll, outerScroll]) {
                            const position = item.mapToItem(scroll, 0, 0);
                            const leading = scroll.mirrored ? scroll.effectiveScrollBarWidth : 0;
                            const trailing = scroll.mirrored ? scroll.width : scroll.width - scroll.effectiveScrollBarWidth;
                            verify(position.x >= leading - 0.5 && position.x + item.width <= trailing + 0.5,
                                "Every input field/Set action stays outside the actual scrollbar lane");
                        }
                    }
                    for (const child of item.children)
                        queue.push(child);
                }
                verify(TestProbe.activateForInput(json));
                verify(TestProbe.click(json, json.width / 2, json.height / 2));
                tryCompare(json, "activeFocus", true);
                verify(TestProbe.keyClick(json, Qt.Key_A, Qt.ControlModifier));
                verify(TestProbe.typeAscii(json, '{"tokens":'));
                compare(owner.jsonDraft, '{"tokens":');
                verify(TestProbe.render(owner));
                dialog.close();
                tryCompare(dialog, "visible", false);
                verify(TestProbe.activate(toolbar));
                tryVerify(function() { return consumerWindow.active; });
                verify(TestProbe.render(toolbar));
                verify(TestProbe.click(buttons[2], buttons[2].width / 2, buttons[2].height / 2));
                tryCompare(dialog, "opened", true);
                compare(findChild(owner, "appearanceJson"), identity);
                compare(json.text, '{"tokens":');
                compare(owner.accentDraft, "#123abc");
                compare(toggle.checked, true);
                compare(touch.checked, true);
                compare(preview.checked, true);
            } finally {
                dialog.close();
                tryCompare(dialog, "visible", false);
                consumerWindow.close();
                verify(TestProbe.activate(page));
            }
        }

        function test_accentPresetSelectionAndPreservation(): void {
            editorScroll.visible = true;
            verify(TestProbe.writePaletteHistory(["#49335f", "#5c9cf5", "#49335f"]));
            const bytes = TestProbe.paletteSampleBytes();
            const expected = [
                ["Default blue", "#5C9CF5"], ["Saturated blue", "#245BDB"], ["Dark blue", "#233B67"],
                ["Muted blue", "#7C91AD"], ["Pale blue", "#CBDCF5"], ["Dark purple", "#49335F"],
                ["Sage green", "#73947B"], ["Pale green", "#D4EBCF"], ["Orange", "#F07828"],
                ["Dark red", "#8F263B"], ["Middle gray", "#808080"], ["Light gray", "#D0D0D0"]
            ];
            compare(editor.accentPresets.length, 12);
            editor.accentDraft = "#123abc";
            editor.jsonDraft = '{"tokens":';
            PlaygroundState.draftName = "Dropdown form draft";
            PlaygroundState.dialogName = "Dropdown dialog draft";
            const identities = [findChild(page, "nameField"), findChild(page, "valueSlider"),
                findChild(page, "opencode-blue"), findChild(page, "customPurpleButton")];
            const menu = accentMenuFor(editor);
            const opener = findChild(editor, "accentPickerButton") as IAButton;
            verify(TestProbe.render(editor));
            verify(PlaygroundState.applyInitialPaletteSuggestions());
            for (let index = 0; index < expected.length; ++index) {
                verify(PlaygroundState.setSemanticCorrection("warning", "#db9a01"));
                verify(PlaygroundState.setSemanticCorrection("error", "#db1234"));
                const previous = String(IA.Appearance.colors.accent);
                const before = JSON.stringify(PlaygroundState.appearanceInput);
                inputSpy.clear();
                click(opener);
                tryCompare(menu, "opened", true);
                compare(menu.count, 13);
                compare(JSON.stringify(PlaygroundState.appearanceInput), before);
                compare(inputSpy.count, 0, "Opening and refresh do not submit appearance");
                const row = revealMenuItem(menu, index);
                compare(row.text, expected[index][0] + " " + expected[index][1]);
                compare(row.objectName, "accentPreset_" + expected[index][1].slice(1).toLowerCase());
                verify(row.enabled && row.opacity === 1);
                click(row);
                tryCompare(menu, "visible", false);
                compare(inputSpy.count, 1, "Selection goes through one appearance transaction");
                assertAccentConsumers(expected[index][1]);
                compare(editor.accentDisplayName, expected[index][0]);
                compare(PlaygroundState.hasAmberCorrection, previous === expected[index][1].toLowerCase());
                compare(PlaygroundState.hasDangerCorrection, previous === expected[index][1].toLowerCase());
                if (previous !== expected[index][1].toLowerCase()) {
                    verify(!Qt.colorEqual(IA.Appearance.colors.warning, "#aa5500"), "No saved companions replayed");
                    verify(!Qt.colorEqual(IA.Appearance.colors.error, "#440011"));
                }
                compare(editor.accentDraft, "#123abc");
                compare(editor.jsonDraft, '{"tokens":');
                compare(PlaygroundState.draftName, "Dropdown form draft");
                compare(PlaygroundState.dialogName, "Dropdown dialog draft");
                for (const item of identities)
                    verify(findChild(page, item.objectName) === item);
                compare(TestProbe.paletteSampleBytes(), bytes);
            }
            verify(PlaygroundState.setAppearanceToken("colors", "accent", "#fe73947b"));
            compare(editor.accentDisplayName, "Custom", "Translucent values are not named opaque presets");
            click(opener);
            tryCompare(menu, "opened", true);
            compare(menu.currentIndex, 12);
            menu.currentIndex = 2;
            verify(TestProbe.keyClick(menu.contentItem, Qt.Key_Escape));
            tryCompare(menu, "visible", false);
            compare(editor.accentDisplayName, "Custom");
            compare(TestProbe.paletteSampleBytes(), bytes);
        }

        function test_accentMenuKeyboard_data(): var {
            return [{tag: "space", key: Qt.Key_Space}, {tag: "return", key: Qt.Key_Return}, {tag: "enter", key: Qt.Key_Enter}];
        }

        function test_accentMenuKeyboard(data: var): void {
            editorScroll.visible = true;
            verify(TestProbe.writePaletteHistory(["#245bdb"]));
            const before = TestProbe.paletteSampleBytes();
            const opener = findChild(editor, "accentPickerButton") as IAButton;
            const menu = accentMenuFor(editor);
            menuOpenedSpy.target = menu;
            menuOpenedSpy.clear();
            inputSpy.clear();
            verify(TestProbe.render(editor));
            // Keyboard navigation starts without a stale pointer over a menu
            // row from the preceding data case or an old toolbar position.
            verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
            opener.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(opener, data.key));
            tryCompare(menu, "opened", true);
            compare(menuOpenedSpy.count, 1);
            compare(menu.currentIndex, 0);
            verify(TestProbe.keyClick(menu.contentItem, Qt.Key_Down));
            tryCompare(menu, "currentIndex", 1);
            const row = menu.itemAt(1) as IAMenuItem;
            verify(row !== null && row.enabled);
            verify(row.Accessible.description.indexOf("Saved; still selectable") >= 0);
            compare(inputSpy.count, 0, "Highlighting a saved row is not a write");
            verify(TestProbe.keyClick(menu.contentItem, Qt.Key_Return));
            tryCompare(menu, "visible", false);
            assertAccentConsumers("#245bdb");
            tryCompare(opener, "activeFocus", true);
            click(opener);
            tryCompare(menu, "opened", true);
            click(opener);
            tryCompare(menu, "visible", false, 5000, "Second opener click toggles closed");
            click(opener);
            tryCompare(menu, "opened", true);
            verify(TestProbe.keyClick(menu.contentItem, Qt.Key_Up));
            compare(inputSpy.count, 1);
            verify(TestProbe.keyClick(menu.contentItem, Qt.Key_Escape));
            tryCompare(menu, "visible", false);
            tryCompare(opener, "activeFocus", true);
            click(opener);
            tryCompare(menu, "opened", true);
            verify(TestProbe.click(scene, scene.width - 2, scene.height - 2));
            tryCompare(menu, "visible", false);
            openCustomAccent();
            const dialog = findChild(editor, "accentColorDialog") as IAColorDialog;
            assertPickerColor(dialog, IA.Appearance.colors.accent);
            const hue = findChild(dialog, "hueSlider");
            verify(hue !== null);
            tryVerify(function() { return TestProbe.hostWindow(hue).active; });
            compare(TestProbe.paletteSampleBytes(), before);
        }

        function test_accentHistoryRefreshAndSave(): void {
            editorScroll.visible = true;
            verify(TestProbe.render(editor));
            const menu = accentMenuFor(editor);
            const opener = findChild(editor, "accentPickerButton") as IAButton;
            const row = menu.itemAt(0) as IAMenuItem;
            verify(row !== null);
            compare(PlaygroundState.savedAccentKeys.length, 0);
            verify(TestProbe.writePaletteHistory(["#5c9cf5"], true));
            click(opener);
            tryCompare(menu, "opened", true);
            compare(PlaygroundState.savedAccentKeys, ["#5c9cf5"]);
            const name = findChild(row, "accentPresetName") as IALabel;
            compare(name.color, menu.savedTextColor);
            verify(PlaygroundState.paletteHistoryStatus.indexOf("skipped 1") >= 0);
            menu.close();
            tryCompare(menu, "visible", false);
            verify(PlaygroundState.setAppearanceToken("colors", "accent", "#245bdb"));
            const unsavedRow = menu.itemAt(1) as IAMenuItem;
            const unsavedName = findChild(unsavedRow, "accentPresetName") as IALabel;
            compare(unsavedName.color, IA.Appearance.colors.text);
            const before = TestProbe.paletteSampleBytes();
            click(findChild(editor, "savePaletteButton") as IAButton);
            verify(TestProbe.paletteSampleBytes().startsWith(before));
            compare(unsavedName.color, menu.savedTextColor, "Already-created row updates immediately after explicit Save");
            compare((findChild(editor, "accentReadout") as IALabel).color, menu.savedTextColor);
            const saved = PlaygroundState.paletteSaveStatus;
            verify(TestProbe.paletteHistoryReadable(false));
            try {
                click(opener);
                tryCompare(menu, "opened", true);
                verify(!PlaygroundState.paletteHistoryAvailable);
                compare(PlaygroundState.savedAccentKeys.length, 0);
                compare(unsavedName.color, IA.Appearance.colors.text);
                const historyStatus = findChild(editor, "paletteHistoryStatus") as IALabel;
                verify(historyStatus.visible && historyStatus.text.indexOf("unavailable") >= 0);
                compare(PlaygroundState.paletteSaveStatus, saved);
                compare(PlaygroundState.paletteSaveFailed, false);
                compare(PlaygroundState.appearanceError, "");
            } finally {
                verify(TestProbe.paletteHistoryReadable(true));
            }
            menu.close();
            tryCompare(menu, "visible", false);
            click(opener);
            tryCompare(menu, "opened", true);
            compare(unsavedName.color, menu.savedTextColor);
        }

        function assertRenderedLabel(label: IALabel): void {
            // Qt 6.8's grabImage crops by local x/y, not scene coordinates.
            // Follow the existing icon/popup tests: capture the host root and map.
            const root = TestProbe.hostWindow(label).contentItem;
            const pixels = grabImage(root);
            verify(pixels.width > 0 && pixels.height > 0);
            const point = label.mapToItem(root, 0, 0);
            const left = Math.max(0, Math.ceil(point.x * pixels.width / root.width));
            const top = Math.max(0, Math.ceil(point.y * pixels.height / root.height));
            const right = Math.min(pixels.width, Math.floor((point.x + label.width) * pixels.width / root.width));
            const bottom = Math.min(pixels.height, Math.floor((point.y + label.height) * pixels.height / root.height));
            verify(right > left && bottom > top, "Label has a visible rendered region");
            let matching = 0;
            for (let y = top; y < bottom; ++y)
                for (let x = left; x < right; ++x) {
                    const pixel = pixels.pixel(x, y);
                    // Fractionally positioned 13px LCD glyphs have no fully
                    // covered pixel. Measured cores differ by up to five RGB8
                    // levels; this only bounds rasterization, not text contrast.
                    if (pixel.a === 1 && Math.abs(pixel.r - label.color.r) * 255 <= 6
                        && Math.abs(pixel.g - label.color.g) * 255 <= 6
                        && Math.abs(pixel.b - label.color.b) * 255 <= 6)
                        ++matching;
                }
            verify(matching > 0, "Rendered glyph cores match the label foreground within LCD rasterization tolerance");
        }

        function test_accentSavedGreyStates_data(): var {
            return [{tag: "light-normal", scheme: "light", contrast: "normal"},
                {tag: "dark-normal", scheme: "dark", contrast: "normal"},
                {tag: "light-high", scheme: "light", contrast: "high"},
                {tag: "dark-high", scheme: "dark", contrast: "high"}];
        }

        function test_accentSavedGreyStates(data: var): void {
            verify(PlaygroundState.setAppearance({scheme: data.scheme, contrast: data.contrast}));
            editorScroll.visible = true;
            verify(TestProbe.writePaletteHistory(["#5c9cf5", "#49335f"]));
            const menu = accentMenuFor(editor);
            const opener = findChild(editor, "accentPickerButton") as IAButton;
            click(opener);
            tryCompare(menu, "opened", true);
            const row = menu.itemAt(0) as IAMenuItem;
            const unsaved = menu.itemAt(1) as IAMenuItem;
            const name = findChild(row, "accentPresetName") as IALabel;
            const hex = findChild(row, "accentPresetHex") as IALabel;
            const swatch = findChild(row, "accentPresetSwatch") as Rectangle;
            verify(name !== null && hex !== null && swatch !== null);
            compare((findChild(unsaved, "accentPresetName") as IALabel).color, IA.Appearance.colors.text);
            const surface = row.background as IA.ControlBackground;
            verify(surface !== null);
            const target = data.contrast === "high" ? 7 : 5;
            for (const state of ["normal", "hover", "pressed", "highlight", "inactive"]) {
                menu.currentIndex = -1;
                verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
                tryCompare(row, "hovered", false);
                try {
                    if (state === "hover") {
                        verify(TestProbe.move(row, row.width / 2, row.height / 2));
                        tryCompare(row, "hovered", true);
                    } else if (state === "pressed") {
                        verify(TestProbe.press(row, row.width / 2, row.height / 2));
                        tryCompare(row, "down", true);
                    } else if (state === "highlight" || state === "inactive") {
                        menu.currentIndex = 0;
                        tryCompare(row, "highlighted", true);
                        if (state === "inactive")
                            surface.active = false;
                    }
                    verify(TestProbe.render(menu.contentItem));
                    compare(name.color, menu.savedTextColor);
                    compare(hex.color, menu.savedTextColor);
                    compare(row.foregroundColor, menu.savedTextColor);
                    compare(row.selectedForegroundColor, menu.savedTextColor);
                    compare(row.inactiveForegroundColor, menu.savedTextColor);
                    compare(row.disabledForegroundColor, menu.savedTextColor);
                    compare(swatch.color, "#5c9cf5");
                    verify(row.enabled && row.opacity === 1);
                    const background = surface.color.a === 0 ? (menu.background as Rectangle).color : surface.color;
                    if (row.highlighted && contrastRatio(name.color, background) < target) {
                        // P2's accent highlight cannot silently rewrite this
                        // caller-owned gray pair. Its real collision is reported.
                        verify(row.appearanceDiagnostics.some(function(diagnostic) {
                            return diagnostic.code === "low-contrast"
                                && diagnostic.path === "selectedForegroundColor/selectedBackgroundColor";
                        }), "An exact local label below the new highlight floor is diagnosed");
                    } else {
                        verify(contrastRatio(name.color, background) >= target, data.tag + "/" + state + " saved-name contrast");
                        verify(contrastRatio(hex.color, background) >= target, data.tag + "/" + state + " saved-hex contrast");
                    }
                    assertRenderedLabel(name);
                    assertRenderedLabel(hex);
                    const centers = [swatch, name, hex].map(function(item) { return item.mapToItem(row, 0, item.height / 2).y; });
                    fuzzyCompare(centers[0], centers[1], 0.5);
                    fuzzyCompare(centers[1], centers[2], 0.5);
                    console.info("IA_DROPDOWN_STATE " + JSON.stringify({scheme: data.scheme, contrast: data.contrast,
                        state: state, foreground: String(name.color), background: String(background),
                        ratio: contrastRatio(name.color, background), centers: centers}));
                } finally {
                    if (state === "pressed") {
                        verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
                        verify(TestProbe.release(scene, scene.width - 2, scene.height - 2));
                        tryCompare(row, "down", false);
                    }
                    if (state === "inactive")
                        surface.active = true;
                }
            }
            menu.close();
            tryCompare(menu, "visible", false);
            const readout = findChild(editor, "accentReadout") as IALabel;
            compare(readout.color, menu.savedTextColor);
            verify(contrastRatio(readout.color, opener.backgroundColor) >= target);
            assertRenderedLabel(readout);
        }

        function test_accentMenuRealNarrowWindows_data(): var {
            const rows = [];
            for (const width of [320, 360])
                for (const touch of [false, true])
                    rows.push({tag: width + "-" + touch, width: width, touch: touch});
            return rows;
        }

        function test_accentMenuRealNarrowWindows(data: var): void {
            verify(PlaygroundState.setAppearance({density: data.touch ? "touch" : "compact", fontScale: data.touch ? 1.5 : 1}));
            // Engine-owned fixture: the existing probe deliberately gives returned
            // native windows C++ ownership. Its lifecycle here is show/close.
            // Keep each native host at its fixed test width. Qt 6.8's hidden
            // Wayland resize can leave the content root at the previous width.
            const host = data.width === 320 ? narrow320Window : narrow360Window;
            host.show();
            try {
                tryCompare(host, "width", data.width);
                const owner = findChild(host, "narrowEditor") as AppearanceEditor;
                verify(owner !== null);
                tryVerify(function() { return TestProbe.exposed(owner) && owner.width === data.width; });
                verify(TestProbe.render(owner));
                verify(TestProbe.activate(owner));
                const opener = findChild(owner, "accentPickerButton") as IAButton;
                const menu = accentMenuFor(owner);
                verify(TestProbe.click(opener, opener.width / 2, opener.height / 2));
                tryCompare(menu, "opened", true);
                const top = menu.contentItem.mapToItem(host.contentItem, -menu.leftPadding, -menu.topPadding);
                verify(top.x >= -0.5 && top.x + menu.width <= host.width + 0.5);
                verify(top.y >= -0.5 && top.y + menu.height <= host.height + 0.5);
                const input = JSON.stringify(PlaygroundState.appearanceInput);
                const view = menu.contentItem as ListView;
                for (let index = 0; index < 13; ++index) {
                    const row = revealMenuItem(menu, index);
                    const point = row.mapToItem(view, 0, 0);
                    verify(point.y >= -0.5 && point.y + row.height <= view.height + 0.5);
                    verify(row.width > 0 && row.width <= view.width + 0.5);
                    if (data.touch)
                        verify(row.width >= 44 && row.height >= 44);
                    for (const item of row.contentItem.children) {
                        const bounds = item.mapToItem(row.contentItem, 0, 0);
                        verify(bounds.x >= -0.5 && bounds.x + item.width <= row.availableWidth + 0.5);
                    }
                    row.forceActiveFocus(Qt.TabFocusReason);
                    menu.currentIndex = index;
                    if (index < 12) {
                        verify(TestProbe.keyClick(row, Qt.Key_Down));
                        tryCompare(menu, "currentIndex", index + 1);
                    }
                }
                compare(JSON.stringify(PlaygroundState.appearanceInput), input);
                console.info("IA_DROPDOWN_WINDOW_GEOMETRY " + JSON.stringify({width: host.width, height: host.height,
                    touch: data.touch, popup: {x: top.x, y: top.y, width: menu.width, height: menu.height},
                    scrollable: view.interactive, count: menu.count}));
                verify(TestProbe.keyClick(menu.contentItem, Qt.Key_Escape));
                tryCompare(menu, "visible", false);
            } finally {
                host.close();
                verify(TestProbe.activate(page));
            }
        }

        function revealEditor(item: Item): void {
            const input = findChild(editor, "appearanceInputScroll") as IAScrollView;
            for (const scroll of [input, editorScroll]) {
                const flick = scroll.contentItem as Flickable;
                verify(flick !== null);
                const center = item.mapToItem(flick.contentItem, item.width / 2, item.height / 2);
                flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, center.y - flick.height / 2));
            }
            verify(TestProbe.render(editor));
        }

        function reveal(item: Item): void {
            verify(item !== null);
            const scroller = findChild(page, "overviewScroll") as Flickable;
            verify(scroller !== null);
            verify(scroller.width > 0 && scroller.height > 0,
                   "The gallery's consumer Flickable must have a nonempty viewport");
            for (let attempt = 0; attempt < 60; ++attempt) {
                const point = item.mapToItem(scroller, item.width / 2, item.height / 2);
                if (point.y >= item.height / 2 && point.y <= scroller.height - item.height / 2) {
                    verify(point.x >= 0 && point.x < scroller.width);
                    return;
                }
                mouseWheel(scroller, scroller.width - 12, scroller.height / 2,
                           0, point.y < 0 ? 360 : -360);
                tryCompare(scroller, "moving", false);
                verify(TestProbe.render(scroller));
            }
            fail("Control was not reachable by scrolling: " + item.objectName);
        }

        function clickPage(item: Item): void {
            reveal(item);
            click(item);
        }

        function edit(field: IATextField, text: string): void {
            const started = Date.now();
            const ready = TestProbe.activateForInput(field);
            const elapsedMs = Date.now() - started;
            const host = TestProbe.hostWindow(field);
            const mainHost = TestProbe.hostWindow(page);
            const diagnostic = {text: text,
                phase: field.objectName === "dialogNameField" ? text.split(" ")[0] : field.objectName,
                elapsedMs: elapsedMs, ready: ready, exposed: TestProbe.exposed(field),
                active: host !== null && host.active,
                exactOwner: TestProbe.inputHostReady(field),
                mainReady: TestProbe.inputHostReady(page),
                mainActive: mainHost !== null && mainHost.active,
                sameHost: host !== null && host === mainHost};
            if (field.objectName === "dialogNameField") {
                console.info("IA_DIALOG_INPUT_PHASE " + JSON.stringify(diagnostic));
            }
            verify(ready, "Native input owner ready for " + field.objectName + " " + JSON.stringify(diagnostic));
            click(field);
            tryCompare(field, "activeFocus", true);
            verify(TestProbe.keyClick(field, Qt.Key_A, Qt.ControlModifier));
            verify(TestProbe.typeAscii(field, text));
            compare(field.text, text);
        }

        function test_inputHostActivationCompletesAcrossReopen(): void {
            const mainField = findChild(page, "nameField") as IATextField;
            const window = inputHostWindow;
            window.transientParent = TestProbe.hostWindow(page);
            verify(mainField !== null && window !== null);
            const field = findChild(window, "inputHostField") as IATextField;
            verify(field !== null);
            try {
                for (let opening = 0; opening < 2; ++opening) {
                    window.show();
                    tryVerify(function() { return TestProbe.exposed(field); });
                    verify(TestProbe.activateForInput(field));
                    verify(TestProbe.inputHostReady(field), "activation completes at the intended native owner");
                    verify(!TestProbe.inputHostReady(mainField), "transient ancestor is not the global input owner");
                    verify(TestProbe.hostWindow(mainField).active, "active transient ancestor is insufficient");
                    verify(TestProbe.click(field, field.width / 2, field.height / 2));
                    tryCompare(field, "activeFocus", true);
                    // Match consumer lifecycle: return to the owner after the
                    // dialog closes, not by stealing focus from an open dialog.
                    window.close();
                    tryCompare(window, "visible", false);
                    verify(TestProbe.activateForInput(mainField));
                    verify(TestProbe.inputHostReady(mainField), "return to ancestor waits for global ownership");
                    verify(!TestProbe.inputHostReady(field));
                    verify(TestProbe.click(mainField, mainField.width / 2, mainField.height / 2));
                    tryCompare(mainField, "activeFocus", true);
                }
            } finally { window.close(); }
        }

        function renderedSummary(): string {
            const banner = findChild(page, "committedSummary") as IAStatusBanner;
            verify(banner !== null);
            const label = findChild(banner, "iaBannerMessage") as IALabel;
            verify(label !== null);
            compare(label.text, banner.message);
            return label.text;
        }

        function test_editApplyAndThemePreservesDraft(): void {
            const field = findChild(page, "nameField") as IATextField;
            const apply = findChild(page, "applyButton") as IAButton;
            verify(field !== null && apply !== null);
            reveal(field);
            edit(field, "Avery Stone");
            verify(renderedSummary().startsWith("Maya Chen\n"));
            verify(PlaygroundState.dirty);
            clickPage(apply);
            verify(renderedSummary().startsWith("Avery Stone\n"));
            verify(!PlaygroundState.dirty);

            reveal(field);
            edit(field, "Uncommitted gallery draft");
            verify(TestProbe.keyClick(field, Qt.Key_A, Qt.ControlModifier));
            const selected = field.selectedText;
            const light = String((field.background as Rectangle).color);
            verify(PlaygroundState.setAppearance({scheme: "dark", fontScale: 1.5}));
            verify(TestProbe.render(page));
            compare(field.text, "Uncommitted gallery draft");
            compare(field.selectedText, selected);
            verify(field.activeFocus);
            verify(String((field.background as Rectangle).color) !== light);
            compare((field.background as Rectangle).color, IA.Appearance.colors.content);
            compare(field.font.pixelSize, Math.round(IA.Appearance.typography.controlSize));
            verify(renderedSummary().startsWith("Avery Stone\n"));
            clickPage(apply);
            verify(renderedSummary().startsWith("Uncommitted gallery draft\n"));
        }

        function test_choicesValuesAndTabsReachConsumers(): void {
            const check = findChild(page, "choiceCheck") as IACheckBox;
            const toggle = findChild(page, "choiceSwitch") as IASwitch;
            const weekly = findChild(page, "weeklyRadio") as IARadioButton;
            const daily = findChild(page, "dailyRadio") as IARadioButton;
            const apply = findChild(page, "applyButton") as IAButton;
            verify(check !== null && toggle !== null && weekly !== null && daily !== null && apply !== null);
            clickPage(check);
            clickPage(toggle);
            clickPage(weekly);
            verify(weekly.checked && !daily.checked);
            clickPage(apply);
            verify(renderedSummary().endsWith("weekly updates; notifications off; research notes excluded."));

            const slider = findChild(page, "valueSlider") as IASlider;
            const readout = findChild(page, "valueReadout") as IALabel;
            const progress = findChild(page, "progressPreview") as IAProgressBar;
            verify(slider !== null && readout !== null && progress !== null);
            reveal(slider);
            slider.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(slider, Qt.Key_Right));
            compare(readout.text, "38%");
            compare(progress.value, 38);
            const fill = findChild(progress, "iaFill") as Rectangle;
            verify(fill !== null);
            fuzzyCompare(fill.width, progress.contentItem.width * 0.38, 0.5);
            const tabs = findChild(page, "previewTabs") as IATabBar;
            const inspector = findChild(page, "selectionInspector") as IALabel;
            const row = findChild(page, "listRow") as IAItemDelegate;
            verify(tabs !== null && inspector !== null && row !== null);
            clickPage(row);
            compare(inspector.text, "Selected: Design resources (128 items)");
            clickPage(tabs.itemAt(1));
            verify(inspector.visible);
            verify(inspector.text.startsWith("Design resources\n128 reference items"));
            verify(inspector.text.endsWith("Delivery: weekly."));
            verify(!row.visible);
        }

        function test_menuAndDialogTransactions_data(): var {
            return [{tag: "item", windowPopups: false}, {tag: "window", windowPopups: true}];
        }

        function test_successPreviewLayout_data(): var {
            const rows = [];
            for (const width of [1200, 360, 320])
                for (const scheme of ["light", "dark"])
                    for (const contrast of ["normal", "high"])
                        rows.push({tag: width + "-" + scheme + "-" + contrast, width: width, scheme: scheme, contrast: contrast});
            return rows;
        }

        function test_successPreviewLayout(data: var): void {
            const host = TestProbe.hostWindow(scene);
            verify(host !== null);
            const minimumWidth = host.minimumWidth;
            const minimumHeight = host.minimumHeight;
            host.minimumWidth = Math.max(minimumWidth, data.width);
            host.minimumHeight = Math.max(minimumHeight, 820);
            try {
                tryVerify(function() { return host.width >= data.width && host.height >= 820; });
                page.visible = false;
                const specimen = createTemporaryObject(pageComponent, scene, {width: data.width, height: 820}) as FoundationsPage;
                verify(specimen !== null);
                const pane = findChild(specimen, "semanticSuccessPreview") as IAPane;
                const label = findChild(specimen, "semanticSuccessText") as IALabel;
                const swatch = findChild(specimen, "semanticSuccessSwatch") as Rectangle;
                const readout = findChild(specimen, "semanticSuccessReadout") as IALabel;
                const row = findChild(specimen, "semanticSuccessSwatchRow") as Item;
                const scroll = findChild(specimen, "overviewScroll") as Flickable;
                verify(pane !== null && label !== null && swatch !== null && readout !== null && row !== null && scroll !== null);
                const identities = [pane, label, swatch, readout];
                for (const accent of ["#5C9CF5", "#FFDF00", "#49335F", "#E53935"]) {
                    verify(PlaygroundState.setAppearance({scheme: data.scheme, contrast: data.contrast, tokens: {colors: {accent: accent}}}));
                    verify(TestProbe.render(specimen));
                    const center = pane.mapToItem(scroll.contentItem, 0, pane.height / 2).y;
                    scroll.contentY = Math.max(0, Math.min(scroll.contentHeight - scroll.height, center - scroll.height / 2));
                    verify(TestProbe.render(specimen));
                    for (const item of identities)
                        verify(findChild(specimen, item.objectName) === item);
                    compare((pane.background as Rectangle).color, IA.Appearance.colors.successSurface);
                    compare(label.color, IA.Appearance.colors.successText);
                    compare(swatch.color, IA.Appearance.colors.success);
                    compare(readout.text.toLowerCase(), "success " + String(swatch.color).toLowerCase());
                    const target = data.contrast === "high" ? 7 : 5;
                    verify(contrastRatio(label.color, (pane.background as Rectangle).color) >= target);
                    const centers = [swatch.mapToItem(row, 0, 0).y + swatch.height / 2,
                        readout.mapToItem(row, 0, 0).y + readout.height / 2];
                    console.info("IA_SUCCESS_GEOMETRY " + JSON.stringify({width: data.width, scheme: data.scheme,
                        contrast: data.contrast, accent: accent, centers: centers, host: [host.width, host.height],
                        specimen: [specimen.width, specimen.height]}));
                    fuzzyCompare(centers[0], centers[1], 0.5, "Shared layout centerline");
                    for (const item of [label, row]) {
                        const point = item.mapToItem(pane.contentItem, 0, 0);
                        verify(point.x >= -0.5 && point.x + item.width <= pane.availableWidth + 0.5);
                        verify(point.y >= -0.5 && point.y + item.height <= pane.availableHeight + 0.5);
                    }
                    const left = pane.mapToItem(specimen, 0, 0).x;
                    verify(left >= 0 && left + pane.width <= data.width + 0.5, "Success specimen fits the current width");
                    const pixels = grabImage(specimen);
                    verify(pixels.width > 0 && pixels.height > 0);
                    const sample = swatch.mapToItem(specimen, swatch.width / 2, swatch.height / 2);
                    fuzzyCompare(pixels.pixel(Math.floor(sample.x * pixels.width / specimen.width),
                        Math.floor(sample.y * pixels.height / specimen.height)), swatch.color, 0.03, "Success fill is rendered");
                    const surface = pane.mapToItem(specimen, pane.width / 2, pane.topPadding / 2);
                    fuzzyCompare(pixels.pixel(Math.floor(surface.x * pixels.width / specimen.width),
                        Math.floor(surface.y * pixels.height / specimen.height)), IA.Appearance.colors.successSurface,
                        0.03, "Success message surface is rendered");
                }
            } finally {
                host.minimumWidth = minimumWidth;
                host.minimumHeight = minimumHeight;
            }
        }

        function test_referenceColors(): void {
            const caption = findChild(page, "opencodeSelection") as IALabel;
            const row = findChild(page, "opencodeReferenceRow") as Flow;
            verify(caption !== null && row !== null);
            let buttonSize = null;
            for (const scheme of ["light", "dark"]) {
                verify(PlaygroundState.setAppearance({scheme: scheme}));
                const input = JSON.stringify(PlaygroundState.appearanceInput);
                let rowTop = -1;
                for (const reference of references) {
                    const button = findChild(page, "opencode-" + reference.key) as IAButton;
                    verify(button !== null);
                    reveal(button);
                    verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
                    tryCompare(button, "hovered", false);
                    verify(TestProbe.render(button));
                    compare(button.tone, reference.tone);
                    compare((button.background as Rectangle).color, IA.Appearance.colors[reference.role + "Surface"]);
                    if (reference.role === "primary")
                        compare((button.background as Rectangle).color, "#5c9cf5");
                    compare((findChild(button, "iaLabel") as Text).color, IA.Appearance.colors[reference.role + "Text"]);
                    compare((button.background as Rectangle).border.color, IA.Appearance.colors[reference.role + "Outline"]);
                    const swatch = findChild(page, "opencode-color-" + reference.key) as IALabel;
                    verify(swatch !== null);
                    const effective = String(button.backgroundColor).toLowerCase();
                    compare(swatch.text.toLowerCase(), effective);
                    const size = Qt.size(button.width, button.height);
                    if (buttonSize === null)
                        buttonSize = size;
                    else
                        compare(size, buttonSize);
                    const top = button.mapToItem(row, 0, 0).y;
                    if (rowTop < 0)
                        rowTop = top;
                    else
                        fuzzyCompare(top, rowTop, 0.5);
                    click(button);
                    compare(caption.text.toLowerCase(), "selected: " + reference.name.toLowerCase() + " " + effective);
                    compare(JSON.stringify(PlaygroundState.appearanceInput), input);
                }
            }
            PlaygroundState.resetFixtures();
            compare(caption.text, "No color selected");
        }

        function sameRgba(actual: color, expected: color): bool {
            // QColor HSL/HSV retains editing coordinates; RGB normalization can
            // change derived coordinates without changing any effective channel.
            return actual.r === expected.r && actual.g === expected.g
                && actual.b === expected.b && actual.a === expected.a;
        }
        function assertRgba(actual: color, expected: color): void {
            verify(sameRgba(actual,expected), "Exact RGBA mismatch: " + JSON.stringify({
                actual:[actual.r,actual.g,actual.b,actual.a],expected:[expected.r,expected.g,expected.b,expected.a]}));
        }
        function assertPickerColor(dialog: IAColorDialog, expected: color): void {
            tryCompare(dialog, "visible", true);
            assertRgba(dialog.currentColor, expected);
            const picker = findChild(dialog, "iaColorEditor") as IAColorEditor;
            verify(picker !== null);
            tryCompare(picker, "visible", true);
            tryVerify(function() { return TestProbe.exposed(picker); });
            verify(TestProbe.render(picker));
            assertRgba(picker.currentColor, expected);
            assertRgba((findChild(picker, "iaColorPreviewSample") as Rectangle).color, expected);
        }

        function semanticWell(owner: Item, role: string): IAColorWell {
            const well = findChild(owner, role + "ColorWell") as IAColorWell;
            verify(well !== null && well.visible);
            return well;
        }

        function semanticAction(owner: Item, role: string, custom: bool): T.AbstractButton {
            return findChild(semanticWell(owner, role), custom ? "iaColorWellCustomButton" : "iaColorWellSampleButton") as T.AbstractButton;
        }

        function semanticDefault(owner: Item, role: string): void {
            const well = semanticWell(owner, role);
            const opener = semanticAction(owner, role, false);
            revealEditor(opener);
            verify(TestProbe.click(opener, 8, opener.height / 2));
            const menu = findChild(well, "iaColorWellMenu") as IAMenu;
            tryCompare(menu, "opened", true);
            const action = menu.itemAt(menu.count - 1);
            (menu.contentItem as ListView).positionViewAtIndex(menu.count - 1, ListView.Contain);
            verify(TestProbe.render(action));
            click(action);
            tryCompare(menu, "visible", false);
        }

        function test_semanticWellPresetsAndDefaults_data(): var {
            return [{tag:"amber-generated",role:"amber",prior:false},{tag:"danger-generated",role:"danger",prior:false},
                {tag:"amber-input",role:"amber",prior:true},{tag:"danger-input",role:"danger",prior:true}];
        }

        function test_semanticWellPresetsAndDefaults(data): void {
            editorScroll.visible = true;
            const key = data.role === "amber" ? "warning" : "error";
            const peer = data.role === "amber" ? "error" : "warning";
            // Hold the peer's raw input explicitly, as the live-dialog isolation
            // fixture does. A generated peer may legitimately be re-derived.
            const input = {tokens:{colors:{[peer]:"#cf3456"}}};
            if (data.prior) input.tokens.colors[key] = "#cd951a";
            verify(PlaygroundState.setAppearance(input));
            const underlying = String(IA.Appearance.colors[key]), peerColor = String(IA.Appearance.colors[peer]);
            const well = semanticWell(editor, data.role);
            compare(well.presets, editor.accentPresets);
            const legacy = findChild(editor, data.role + "PickerButton") as IAButton;
            compare(legacy.visible, false);
            compare(legacy.parent.parent.implicitWidth, 0);
            compare(legacy.parent.parent.implicitHeight, 0);
            const opener = semanticAction(editor, data.role, false);
            revealEditor(opener);
            verify(TestProbe.click(opener, 8, opener.height / 2));
            const menu = findChild(well, "iaColorWellMenu") as IAMenu;
            tryCompare(menu, "opened", true);
            click(menu.itemAt(1));
            tryCompare(menu, "visible", false);
            compare(String(IA.Appearance.colors[key]), editor.accentPresets[0].hex.toLowerCase());
            compare(well.colorValue, IA.Appearance.colors[key]);
            compare((findChild(well,"iaColorWellSample") as Rectangle).color, IA.Appearance.colors[key]);
            compare(String(IA.Appearance.colors[peer]), peerColor);
            semanticDefault(editor, data.role);
            compare(String(IA.Appearance.colors[key]), underlying);
            compare(well.colorValue, IA.Appearance.colors[key]);
            compare(String(IA.Appearance.colors[peer]), peerColor);
            compare(data.role === "amber" ? PlaygroundState.hasAmberCorrection : PlaygroundState.hasDangerCorrection, false);
            const rawColors = PlaygroundState.appearanceInput.tokens.colors;
            if (data.prior) compare(String(rawColors[key]), "#cd951a");
            else verify(rawColors[key] === undefined, "Default resumes generation, not a captured color override");
            compare(String(rawColors[peer]), "#cf3456");
            verify(!TestProbe.paletteSampleExists());
        }

        function assertAccentConsumers(expected: color): void {
            verify(TestProbe.render(page));
            assertRgba(IA.Appearance.colors.accent, expected);
            const swatch = findChild(editor, "accentSwatch") as Rectangle;
            const readout = findChild(editor, "accentReadout") as IALabel;
            const sample = findChild(page, "opencode-blue") as IAButton;
            const slider = findChild(page, "valueSlider") as IASlider;
            const progress = findChild(page, "progressPreview") as IAProgressBar;
            verify(swatch !== null && readout !== null && sample !== null
                   && slider !== null && progress !== null);
            assertRgba(swatch.color, expected);
            compare(readout.text.toLowerCase(), editor.accentDisplayName.toLowerCase() + " " + String(expected).toLowerCase());
            compare(readout.color, readout.foregroundColor);
            const background = sample.background as Rectangle;
            const label = findChild(sample, "iaLabel") as Text;
            const sliderFill = findChild(slider, "iaFill") as Rectangle;
            const progressFill = findChild(progress, "iaFill") as Rectangle;
            verify(background !== null && label !== null && sliderFill !== null && progressFill !== null);
            tryCompare(sample, "hovered", false);
            compare(background.color, IA.Appearance.colors.primarySurface);
            compare(label.color, sample.foregroundColor);
            compare(background.border.color, sample.borderColor);
            assertRgba(sliderFill.color, expected);
            assertRgba(progressFill.color, expected);
            assertSemanticConsumers();
        }

        function assertSemanticConsumers(): void {
            for (const reference of references) {
                const button = findChild(page, "opencode-" + reference.key) as IAButton;
                const label = findChild(button, "iaLabel") as Text;
                const background = button.background as Rectangle;
                const readout = findChild(page, "opencode-color-" + reference.key) as IALabel;
                verify(button !== null && label !== null && background !== null && readout !== null);
                compare(background.color, button.hovered ? button.hoverBackgroundColor : button.backgroundColor);
                compare(label.color, button.foregroundColor);
                compare(background.border.color, label.color);
                compare(readout.text.toLowerCase(), String(button.backgroundColor).toLowerCase());
            }
            const pane = findChild(page, "semanticSuccessPreview") as IAPane;
            const label = findChild(page, "semanticSuccessText") as IALabel;
            const swatch = findChild(page, "semanticSuccessSwatch") as Rectangle;
            const readout = findChild(page, "semanticSuccessReadout") as IALabel;
            verify(pane !== null && label !== null && swatch !== null && readout !== null);
            compare((pane.background as Rectangle).color, IA.Appearance.colors.successSurface);
            compare(label.color, IA.Appearance.colors.successText);
            compare(swatch.color, IA.Appearance.colors.success);
            compare(readout.text.toLowerCase(), "success " + String(IA.Appearance.colors.success).toLowerCase());
        }

        function test_accentOpeningAndExplicitDefaultReset(): void {
            editorScroll.visible = true;
            verify(TestProbe.render(editor));
            const opener = findChild(editor, "accentPickerButton") as IAButton;
            const reset = findChild(editor, "resetAccentButton") as IAToolButton;
            const dialog = findChild(editor, "accentColorDialog") as IAColorDialog;
            verify(opener !== null && reset !== null && dialog !== null);
            const defaultAccent = IA.Appearance.colors.accent;
            compare(PlaygroundState.hasAccentOverride, false);
            compare(reset.visible, false);
            openCustomAccent();
            compare(dialog.modal, false);
            compare(dialog.popupType, T.Popup.Window);
            compare(dialog.footer.standardButtons, T.DialogButtonBox.Close);
            assertPickerColor(dialog, defaultAccent);
            compare(JSON.stringify(PlaygroundState.appearanceInput), "{}");
            compare(PlaygroundState.hasAccentOverride, false);
            dialog.close();
            tryCompare(dialog, "visible", false);
            compare(JSON.stringify(PlaygroundState.appearanceInput), "{}");

            verify(PlaygroundState.setAppearanceToken("colors", "accent", String(defaultAccent)));
            compare(PlaygroundState.hasAccentOverride, true, "Presence, not inequality, enables Reset accent");
            compare(reset.visible, true);
            click(reset);
            compare(JSON.stringify(PlaygroundState.appearanceInput), "{}",
                    "Removing the sole token prunes both empty containers");
            compare(PlaygroundState.hasAccentOverride, false);
            compare(reset.visible, false);
            assertAccentConsumers(defaultAccent);
            verify(PlaygroundState.setAppearance({tokens: {
                metrics: {controlRadius: 9}, colors: {accent: String(defaultAccent)}}}));
            click(reset);
            const remaining = {tokens: {metrics: {controlRadius: 9}}};
            compare(PlaygroundState.appearanceInput, remaining,
                    "An empty colors group is pruned without removing another token group");
            editor.openAccentPicker();
            assertPickerColor(dialog, defaultAccent);
            compare(PlaygroundState.appearanceInput, remaining);
        }

        function assertSample(record: var): void {
            compare(record.version, 1);
            compare(record.scheme, IA.Appearance.scheme);
            compare(record.contrast, IA.Appearance.contrast);
            verify(record.utc.endsWith("Z"));
            for (const role of [{name: "accent", key: "accent"}, {name: "amber", key: "warning"}, {name: "danger", key: "error"}]) {
                const applied = IA.Appearance.colors[role.key];
                const value = record[role.name];
                compare(value.rgba16, [Math.round(applied.r * 65535), Math.round(applied.g * 65535),
                    Math.round(applied.b * 65535), Math.round(applied.a * 65535)]);
            }
            compare(record.corrections.amber, PlaygroundState.hasAmberCorrection);
            compare(record.corrections.danger, PlaygroundState.hasDangerCorrection);
        }

        function test_liveCalibrationPickers_data(): var {
            const rows = [];
            for (const role of ["amber", "danger"])
                for (const scheme of ["light", "dark"])
                    for (const contrast of ["normal", "high"])
                        rows.push({tag: role + "-" + scheme + "-" + contrast,
                            role: role, scheme: scheme, contrast: contrast});
            return rows;
        }

        function test_liveCalibrationPickers(data: var): void {
            verify(PlaygroundState.setAppearance({scheme: data.scheme, contrast: data.contrast}));
            verify(PlaygroundState.applyInitialPaletteSuggestions());
            editorScroll.visible = true;
            editor.accentDraft = "#234567";
            editor.jsonDraft = '{"tokens":';
            PlaygroundState.draftName = "Calibration form draft";
            PlaygroundState.dialogName = "Calibration dialog draft";
            PlaygroundState.draftEmail = "invalid-calibration-draft";
            verify(TestProbe.render(editor));
            const role = data.role;
            const key = role === "amber" ? "warning" : "error";
            const dialog = findChild(editor, role + "ColorDialog") as IAColorDialog;
            const otherRole = role === "amber" ? "danger" : "amber";
            const other = findChild(editor, otherRole + "ColorDialog") as IAColorDialog;
            const opener = semanticAction(editor, role, true);
            const otherOpener = semanticAction(editor, otherRole, true);
            verify(dialog !== null && other !== null && opener !== null && otherOpener !== null);
            const identities = [findChild(page, "opencode-amber"), findChild(page, "opencode-rose"),
                findChild(page, "customPurpleButton"), findChild(page, "nameField")];
            const purple = identities[2] as IAButton;
            const purpleColor = purple.backgroundColor;
            const input = JSON.stringify(PlaygroundState.appearanceInput);
            click(otherOpener);
            assertPickerColor(other, IA.Appearance.colors[otherRole === "amber" ? "warning" : "error"]);
            const otherPicker = findChild(other, "iaColorEditor");
            verify(otherPicker !== null);
            click(opener);
            assertPickerColor(dialog, IA.Appearance.colors[key]);
            compare(JSON.stringify(PlaygroundState.appearanceInput), input, "Opening two dialogs is not an edit");
            const otherColor = other.currentColor;
            const otherSelected = Qt.rgba(otherColor.r,otherColor.g,otherColor.b,otherColor.a);
            for (const name of ["hueSlider", "alphaSlider"]) {
                const control = findChild(dialog, name) as T.Slider;
                verify(control !== null && control.handle !== null);
                verify(control !== findChild(other, name), "Input targets this role's host");
                verify(TestProbe.activate(control));
                verify(TestProbe.render(control));
                const start = control.handle.mapToItem(control, control.handle.width / 2, control.handle.height / 2);
                const x = control.leftPadding + control.handle.width / 2
                    + (name === "hueSlider" ? 0.72 : 0.42) * (control.availableWidth - control.handle.width);
                try {
                    verify(TestProbe.press(control, start.x, start.y));
                    tryCompare(control, "pressed", true);
                    // QML QColor wrappers reference the source property. Capture
                    // immutable channels before the drag instead of a live alias.
                    const appliedBefore = IA.Appearance.colors[key];
                    const before = Qt.rgba(appliedBefore.r, appliedBefore.g, appliedBefore.b, appliedBefore.a);
                    verify(TestProbe.move(control, x, start.y));
                    tryVerify(function() { return !Qt.colorEqual(before, IA.Appearance.colors[key]); });
                    compare(control.pressed, true);
                    assertRgba(IA.Appearance.colors[key], dialog.currentColor);
                    assertRgba((findChild(semanticWell(editor, role), "iaColorWellSample") as Rectangle).color, dialog.currentColor);
                    assertSemanticConsumers();
                    const invalidEmail = findChild(page, "emailField") as IATextField;
                    verify(invalidEmail !== null);
                    compare(invalidEmail.validation, IA.Presentation.Invalid);
                    compare(invalidEmail.borderColor, IA.Appearance.colors.errorText,
                        "The real validation consumer follows semantic corrections before release");
                    for (const item of identities)
                        verify(findChild(page, item.objectName) === item);
                    compare(purple.backgroundColor, purpleColor);
                    compare(other.visible, true);
                    assertRgba(other.currentColor, otherSelected);
                    verify(findChild(other, "iaColorEditor") === otherPicker,
                        "An unaffected dialog was not closed/reopened");
                    verify(!TestProbe.paletteSampleExists());
                } finally {
                    verify(TestProbe.release(control, x, start.y));
                }
            }
            const plane = findChild(dialog, "iaColorPlane");
            verify(plane !== null);
            const planeColor = dialog.currentColor;
            const beforePlane = Qt.rgba(planeColor.r, planeColor.g, planeColor.b, planeColor.a);
            verify(TestProbe.press(plane, plane.width * 0.8, plane.height * 0.2));
            try {
                verify(TestProbe.move(plane, plane.width * 0.6, plane.height * 0.35));
                tryVerify(function() { return !sameRgba(dialog.currentColor, beforePlane); });
                tryVerify(function(){return sameRgba(IA.Appearance.colors[key],dialog.currentColor);});
                assertSemanticConsumers();
            } finally {
                verify(TestProbe.release(plane, plane.width * 0.6, plane.height * 0.35));
            }
            const applied = JSON.stringify(PlaygroundState.appearanceInput);
            dialog.close();
            other.close();
            compare(JSON.stringify(PlaygroundState.appearanceInput), applied);
            verify(!TestProbe.paletteSampleExists());
            verify(TestProbe.activate(editor));
            click(findChild(editor, "savePaletteButton") as IAButton);
            compare(TestProbe.paletteSamples().length, 1);
            assertSample(TestProbe.paletteSamples()[0]);
            const first = TestProbe.paletteSampleBytes();
            compare(editor.accentDraft, "#234567");
            compare(editor.jsonDraft, '{"tokens":');
            compare(PlaygroundState.draftName, "Calibration form draft");
            compare(PlaygroundState.dialogName, "Calibration dialog draft");
            verify(PlaygroundState.setAppearanceToken("colors", "accent", "#234567"));
            verify(!PlaygroundState.hasAmberCorrection && !PlaygroundState.hasDangerCorrection);
            compare(TestProbe.paletteSampleBytes(), first);
            verify(PlaygroundState.setSemanticCorrection(key, Qt.rgba(0.7, 0.2, 0.4, 0.6)));
            click(findChild(editor, "savePaletteButton") as IAButton);
            compare(TestProbe.paletteSamples().length, 2);
            assertSample(TestProbe.paletteSamples()[1]);
            verify(TestProbe.paletteSampleBytes().startsWith(first));
            const bytes = TestProbe.paletteSampleBytes();
            semanticDefault(editor, role);
            compare(TestProbe.paletteSampleBytes(), bytes);
            click(findChild(editor, "appearanceReset") as IAButton);
            compare(JSON.stringify(PlaygroundState.appearanceInput), "{}");
            compare(TestProbe.paletteSampleBytes(), bytes);
        }

        function test_visibleSemanticDialogsSurviveAccentReset(): void {
            editorScroll.visible = true;
            verify(PlaygroundState.applyInitialPaletteSuggestions());
            verify(TestProbe.render(editor));
            const amber = findChild(editor, "amberColorDialog") as IAColorDialog;
            const danger = findChild(editor, "dangerColorDialog") as IAColorDialog;
            const accent = findChild(editor, "accentColorDialog") as IAColorDialog;
            editor.openAmberPicker();
            editor.openDangerPicker();
            editor.openAccentPicker();
            assertPickerColor(accent, IA.Appearance.colors.accent);
            assertPickerColor(amber, IA.Appearance.colors.warning);
            assertPickerColor(danger, IA.Appearance.colors.error);
            verify(PlaygroundState.setAppearanceToken("colors", "accent", "#234567"));
            assertPickerColor(accent, IA.Appearance.colors.accent);
            assertPickerColor(amber, IA.Appearance.colors.warning);
            assertPickerColor(danger, IA.Appearance.colors.error);
            verify(!PlaygroundState.hasAmberCorrection && !PlaygroundState.hasDangerCorrection);
            compare(Object.keys(PlaygroundState.appearanceInput.tokens.colors), ["accent"]);
            verify(PlaygroundState.setSemanticCorrection("warning", Qt.rgba(1, 0.5, 0.1, 1)));
            verify(PlaygroundState.setSemanticCorrection("error", Qt.rgba(1, 0.2, 0.2, 1)));
            const reset = findChild(editor, "resetAccentButton") as IAToolButton;
            verify(reset.visible);
            verify(TestProbe.activate(editor));
            click(reset);
            assertPickerColor(accent, IA.Appearance.colors.accent);
            assertPickerColor(amber, IA.Appearance.colors.warning);
            assertPickerColor(danger, IA.Appearance.colors.error);
            verify(!PlaygroundState.hasAmberCorrection && !PlaygroundState.hasDangerCorrection);
            compare(JSON.stringify(PlaygroundState.appearanceInput), "{}");
            verify(!TestProbe.paletteSampleExists());
        }

        function test_calibrationToolbarGeometry_data(): var {
            const rows = [];
            for (const width of [1200, 360, 320])
                for (const scheme of ["light", "dark"])
                    for (const contrast of ["normal", "high"])
                        for (const touch of [false, true])
                            rows.push({tag: width + scheme + contrast + touch, width: width,
                                scheme: scheme, contrast: contrast, touch: touch});
            return rows;
        }

        function test_pendingEditorLaneFitsDesktopContent(): void {
            const component = Qt.createComponent("qrc:/qt/qml/InterActive/Playground/AppearanceEditor.qml");
            tryCompare(component, "status", Component.Ready);
            const specimen = createTemporaryObject(component, scene, {width: 1000, height: 450,
                accentDraft: "#123abc", jsonDraft: '{"tokens":'}) as AppearanceEditor;
            verify(specimen !== null);
            const toggle = findChild(specimen, "appearanceEditorToggle") as IASwitch;
            verify(toggle !== null);
            toggle.checked = true;
            verify(TestProbe.render(specimen));
            const queue = [specimen];
            let fields = 0, setters = 0;
            while (queue.length > 0) {
                const item = queue.pop() as Item;
                const field = item as IATextField;
                const button = item as IAButton;
                if (field !== null || (button !== null && button.text === "Set")) {
                    const point = item.mapToItem(specimen, 0, 0);
                    verify(point.x >= 0 && point.x + item.width <= specimen.width + 0.5,
                        item.objectName + " stays inside the real desktop toolbar lane with pending drafts");
                    if (field !== null) ++fields;
                    else ++setters;
                }
                for (const child of item.children)
                    queue.push(child);
            }
            compare(fields, 5);
            compare(setters, 4);
        }

        function test_calibrationToolbarGeometry(data: var): void {
            verify(PlaygroundState.setAppearance({scheme: data.scheme, contrast: data.contrast,
                density: data.touch ? "touch" : "compact", fontScale: data.touch ? 1.5 : 1,
                tokens: {colors: {primarySurface: "#234567", warning: "#ff7a18", error: "#ff3434"}}}));
            editorScroll.visible = true;
            const host = TestProbe.hostWindow(scene);
            verify(host !== null);
            const minimumWidth = host.minimumWidth;
            host.minimumWidth = Math.max(minimumWidth, data.width);
            try {
            tryVerify(function() { return host.width >= data.width; });
            // Use a same-component specimen with the requested lane width instead
            // of pretending a compositor-ignored window resize is a narrow test.
            const scrollComponent = Qt.createComponent("qrc:/qt/qml/InterActive/IAScrollView.qml");
            tryCompare(scrollComponent, "status", Component.Ready);
            const scroll = createTemporaryObject(scrollComponent, scene, {width: data.width, height: 420, padding: 0}) as IAScrollView;
            verify(scroll !== null);
            scroll.contentWidth = Qt.binding(function() { return scroll.availableWidth; });
            const component = Qt.createComponent("qrc:/qt/qml/InterActive/Playground/AppearanceEditor.qml");
            tryCompare(component, "status", Component.Ready);
            const viewport = scroll.contentItem as Flickable;
            verify(viewport !== null);
            const specimen = createTemporaryObject(component, viewport.contentItem) as AppearanceEditor;
            specimen.width = Qt.binding(function() { return scroll.availableWidth; });
            scroll.contentHeight = Qt.binding(function() { return specimen.implicitHeight; });
            verify(specimen !== null);
            editorScroll.visible = false;
            verify(TestProbe.render(specimen));
            const flick = scroll.contentItem as Flickable;
            verify(flick !== null);
            const controls = [];
            for (const name of ["accentPickerButton", "resetAccentButton", "amberColorWellRow",
                                "dangerColorWellRow", "savePaletteButton"]) {
                const item = findChild(specimen, name) as Item;
                verify(item !== null);
                if (!item.visible) continue;
                const point = item.mapToItem(flick.contentItem, 0, 0);
                verify(point.x >= -0.5 && point.x + item.width <= data.width + 0.5, name + " fits width");
                verify(item.width > 0 && item.height > 0);
                if (data.touch)
                    verify(item.width >= 44 && item.height >= 44, name + " touch target");
                controls.push({x: point.x, y: point.y, w: item.width, h: item.height, name: name});
                flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, point.y));
                verify(TestProbe.render(specimen));
                const visiblePoint = item.mapToItem(flick, 0, 0);
                verify(visiblePoint.y >= -0.5 && visiblePoint.y + item.height <= flick.height + 0.5,
                    name + " remains reachable through scroll");
                if (name === "savePaletteButton") {
                    verify(TestProbe.click(item, item.width / 2, item.height / 2));
                    compare(TestProbe.paletteSamples().length, 1);
                    assertSample(TestProbe.paletteSamples()[0]);
                }
            }
            for (let i = 0; i < controls.length; ++i)
                for (let j = i + 1; j < controls.length; ++j) {
                    const a = controls[i], b = controls[j];
                    verify(a.x + a.w <= b.x + 0.5 || b.x + b.w <= a.x + 0.5
                        || a.y + a.h <= b.y + 0.5 || b.y + b.h <= a.y + 0.5,
                        a.name + " and " + b.name + " do not overlap");
                }
            for (const role of ["accent"]) {
                const button = findChild(specimen, role + "PickerButton") as IAButton;
                const swatch = findChild(specimen, role + "Swatch") as Rectangle;
                const readout = findChild(specimen, role + "Readout") as IALabel;
                const centers = [swatch.mapToItem(button, 0, swatch.height / 2).y,
                    readout.mapToItem(button, 0, readout.height / 2).y];
                fuzzyCompare(centers[0], centers[1], 0.5, "Swatch/readout shared centerline");
            }
            for (const role of ["amber", "danger"]) {
                const well = semanticWell(specimen, role);
                const label = well.parent.children[0] as IALabel;
                fuzzyCompare(label.mapToItem(well, 0, label.height / 2).y, well.height / 2, 0.5, "Label/well centerline");
                if (data.touch) verify(well.height >= 44);
                const circle = semanticAction(specimen, role, true);
                compare(circle.height, well.height - 2 * IA.Appearance.metrics.spacingXS);
                const point = circle.mapToItem(flick.contentItem, 0, 0);
                flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, point.y));
                verify(TestProbe.render(specimen));
                const visiblePoint = circle.mapToItem(flick, 0, 0);
                verify(visiblePoint.y >= -0.5 && visiblePoint.y + circle.height <= flick.height + 0.5);
            }
            console.info("IA_CALIBRATION_GEOMETRY " + JSON.stringify({width: data.width,
                scheme: data.scheme, contrast: data.contrast, touch: data.touch, controls: controls}));
            verify((findChild(specimen, "paletteRootExplanation") as IALabel).visible);
            const input = JSON.stringify(PlaygroundState.appearanceInput);
            verify(TestProbe.paletteSampleLock(true));
            const save = findChild(specimen, "savePaletteButton") as IAButton;
            const point = save.mapToItem(flick.contentItem, 0, 0);
            flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, point.y));
            verify(TestProbe.render(specimen));
            verify(TestProbe.click(save, save.width / 2, save.height / 2));
            compare(PlaygroundState.paletteSaveFailed, true);
            compare(TestProbe.paletteSamples().length, 1);
            compare(JSON.stringify(PlaygroundState.appearanceInput), input);
            compare(PlaygroundState.appearanceError, "");
            compare((findChild(specimen, "paletteSaveStatus") as IALabel).text, PlaygroundState.paletteSaveStatus);
            } finally {
                host.minimumWidth = minimumWidth;
            }
        }

        function test_liveAccentPicker_data(): var {
            const rows = [];
            for (const contrast of ["normal", "high"])
                for (const scheme of ["light", "dark"])
                    rows.push({tag: contrast + "-" + scheme, contrast: contrast, scheme: scheme});
            return rows;
        }

        function test_liveAccentPicker(data: var): void {
            const input = {scheme: data.scheme, contrast: data.contrast};
            verify(PlaygroundState.setAppearance(input));
            const defaultAccent = IA.Appearance.colors.accent;
            editorScroll.visible = true;
            editor.accentDraft = "#234567";
            editor.jsonDraft = '{"fontScale":1.2}';
            verify(TestProbe.render(editor));
            const opener = findChild(editor, "accentPickerButton") as IAButton;
            const dialog = findChild(editor, "accentColorDialog") as IAColorDialog;
            const reset = findChild(editor, "resetAccentButton") as IAToolButton;
            verify(opener !== null && dialog !== null && reset !== null);
            const sample = findChild(page, "opencode-blue") as IAButton;
            const slider = findChild(page, "valueSlider") as IASlider;
            const progress = findChild(page, "progressPreview") as IAProgressBar;
            verify(sample !== null && slider !== null && progress !== null);
            reveal(sample);
            reveal(slider);
            verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
            verify(TestProbe.render(page));
            const consumers = [sample, slider, progress];
            const originalSurface = String((sample.background as Rectangle).color);
            const fillWidths = [(findChild(slider, "iaFill") as Rectangle).width,
                (findChild(progress, "iaFill") as Rectangle).width];
            const independent = [];
            for (const name of ["customPurpleButton"]) {
                const button = findChild(page, name) as IAButton;
                verify(button !== null);
                const background = button.background as Rectangle;
                const label = findChild(button, "iaLabel") as Text;
                verify(background !== null && label !== null);
                independent.push({button: button, background: background, label: label,
                    colors: [String(background.color), String(label.color), String(background.border.color)]});
            }
            const semanticItems = [findChild(page, "opencode-amber"), findChild(page, "opencode-rose"),
                findChild(page, "semanticSuccessPreview"), findChild(page, "semanticSuccessText"), findChild(page, "semanticSuccessSwatch")];
            openCustomAccent();
            assertPickerColor(dialog, defaultAccent);
            compare(PlaygroundState.hasAccentOverride, false);
            compare(PlaygroundState.appearanceInput, input);

            const controls = data.tag === "normal-light" ? ["hueSlider", "alphaSlider"] : ["hueSlider"];
            for (const name of controls) {
                const control = findChild(dialog, name) as T.Slider;
                verify(control !== null && control.handle !== null);
                tryCompare(control, "visible", true);
                verify(TestProbe.activate(control));
                verify(TestProbe.render(control));
                const start = control.handle.mapToItem(control, control.handle.width / 2, control.handle.height / 2);
                let x = start.x;
                const y = start.y;
                try {
                    verify(TestProbe.move(control, x, y));
                    verify(TestProbe.press(control, x, y));
                    tryCompare(control, "pressed", true);
                    for (const position of name === "hueSlider" ? [0.14, 0.83] : [0.4]) {
                        const previous = String(IA.Appearance.colors.accent);
                        x = control.leftPadding + control.handle.width / 2
                            + position * (control.availableWidth - control.handle.width);
                        verify(TestProbe.move(control, x, y));
                        tryVerify(function() { return String(IA.Appearance.colors.accent) !== previous; });
                        compare(control.pressed, true, "Live input must arrive before mouse release");
                        compare(dialog.visible, true);
                        compare(PlaygroundState.hasAccentOverride, true);
                        assertRgba(IA.Appearance.colors.accent, dialog.currentColor);
                        compare(String(PlaygroundState.appearanceInput.tokens.colors.accent).toLowerCase(),
                                String(IA.Appearance.colors.accent).toLowerCase());
                        assertPickerColor(dialog, IA.Appearance.colors.accent);
                        assertAccentConsumers(IA.Appearance.colors.accent);
                        for (const item of semanticItems)
                            verify(findChild(page, item.objectName) === item);
                        compare(Object.keys(PlaygroundState.appearanceInput.tokens.colors), ["accent"],
                            "Generated companions remain outputs, not hidden input overrides");
                        verify(String((sample.background as Rectangle).color) !== originalSurface);
                        for (const consumer of consumers)
                            verify(findChild(page, consumer.objectName) === consumer);
                        for (const other of independent) {
                            verify(findChild(page, other.button.objectName) === other.button);
                            compare([String(other.background.color), String(other.label.color),
                                String(other.background.border.color)], other.colors);
                        }
                        compare([(findChild(slider, "iaFill") as Rectangle).width,
                            (findChild(progress, "iaFill") as Rectangle).width], fillWidths);
                        compare(slider.value, 37);
                        compare(progress.value, 37);
                        compare(editor.accentDraft, "#234567");
                        compare(editor.jsonDraft, '{"fontScale":1.2}');
                        if (name === "alphaSlider")
                            verify(IA.Appearance.colors.accent.a > 0 && IA.Appearance.colors.accent.a < 1);
                        compare(control.pressed, true);
                        compare(dialog.visible, true, "Consumer observations precede release and dialog closure");
                    }
                } finally {
                    verify(TestProbe.release(control, x, y));
                    tryCompare(control, "pressed", false);
                }
            }
            // Exercise actual saturation/lightness input as well as hue and alpha.
            const picker = findChild(dialog, "iaColorPlane");
            verify(picker !== null && picker.width > 0 && picker.height > 0);
            verify(TestProbe.activate(picker));
            verify(TestProbe.render(picker));
            let px = picker.width * 0.8, py = picker.height * 0.2;
            const semanticColors = [];
            try {
                verify(TestProbe.move(picker, px, py));
                verify(TestProbe.press(picker, px, py));
                for (const position of [Qt.point(0.8, 0.2), Qt.point(0.2, 0.2), Qt.point(0.8, 0.8)]) {
                    px = picker.width * position.x;
                    py = picker.height * position.y;
                    verify(TestProbe.move(picker, px, py));
                    tryVerify(function(){return sameRgba(IA.Appearance.colors.accent,dialog.currentColor);});
                    assertAccentConsumers(dialog.currentColor);
                    compare(dialog.visible, true, "Intensity observations precede pointer release");
                    semanticColors.push([String(IA.Appearance.colors.error), String(IA.Appearance.colors.warning),
                        String(IA.Appearance.colors.success)]);
                    for (const other of independent)
                        compare([String(other.background.color), String(other.label.color), String(other.background.border.color)], other.colors);
                }
            } finally {
                verify(TestProbe.release(picker, px, py));
            }
            verify(JSON.stringify(semanticColors[0]) !== JSON.stringify(semanticColors[2]),
                "A real intensity edit changes the final companion palette");
            const applied = JSON.stringify(PlaygroundState.appearanceInput);
            const selected = IA.Appearance.colors.accent;
            dialog.close();
            tryCompare(dialog, "visible", false);
            compare(JSON.stringify(PlaygroundState.appearanceInput), applied, "Closing does not roll back live edits");
            assertAccentConsumers(selected);
            verify(TestProbe.activate(editor));
            click(reset);
            compare(PlaygroundState.appearanceInput, input);
            compare(PlaygroundState.hasAccentOverride, false);
            compare(editor.accentDraft, undefined);
            compare(editor.jsonDraft, '{"fontScale":1.2}');
            assertAccentConsumers(defaultAccent);
        }

        function test_accentExternalInputAndDrafts_data(): var {
            return [{tag: "light", scheme: "light"}, {tag: "dark", scheme: "dark"}];
        }

        function test_accentExternalInputAndDrafts(data: var): void {
            const input = {scheme: data.scheme, contrast: "high", fontScale: 1.1,
                tokens: {metrics: {controlRadius: 11}, typography: {family: "Serif"},
                    colors: {warning: "#d8a5ed", error: "#ed9b70"}}};
            verify(PlaygroundState.setAppearance(input));
            const defaultAccent = IA.Appearance.colors.accent;
            const field = findChild(page, "nameField") as IATextField;
            reveal(field);
            edit(field, "Uncommitted accent draft");
            const committed = renderedSummary();
            editorScroll.visible = true;
            verify(TestProbe.render(editor));
            const toggle = findChild(editor, "appearanceEditorToggle") as IASwitch;
            click(toggle);
            verify(TestProbe.render(editor));
            const hex = findChild(editor, "accentHexField") as IATextField;
            const apply = findChild(editor, "applyAccentHex") as IAButton;
            const json = findChild(editor, "appearanceJson") as IATextField;
            const reset = findChild(editor, "resetAccentButton") as IAToolButton;
            const dialog = findChild(editor, "accentColorDialog") as IAColorDialog;
            verify(hex !== null && apply !== null && json !== null && reset !== null && dialog !== null);
            const pendingHex = "#234567";
            const pendingJson = '{"tokens":';
            edit(hex, pendingHex);
            editor.jsonDraft = pendingJson;
            compare(editor.accentDraft, pendingHex);
            editor.openAccentPicker();
            assertPickerColor(dialog, defaultAccent);
            compare(PlaygroundState.appearanceInput, input);

            for (const step of [{json: true, accent: "#1b4f9a"}, {json: false, accent: "#8070c8aa"}]) {
                const replacement = JSON.parse(JSON.stringify(input));
                replacement.tokens.colors.accent = step.accent;
                if (step.json)
                    verify(PlaygroundState.setAppearanceJson(JSON.stringify(replacement)));
                else
                    verify(PlaygroundState.setAppearance(replacement));
                assertPickerColor(dialog, IA.Appearance.colors.accent);
                assertAccentConsumers(IA.Appearance.colors.accent);
                compare(PlaygroundState.appearanceInput, replacement, "Reopening must not rewrite the snapshot");
                compare(IA.Appearance.colors.warning, input.tokens.colors.warning);
                compare(IA.Appearance.colors.error, input.tokens.colors.error);
                compare(PlaygroundState.hasAccentOverride, true);
                compare(hex.text, pendingHex);
                compare(editor.accentDraft, pendingHex);
                compare(editor.jsonDraft, pendingJson);
                compare(json.text, pendingJson);
                compare(field.text, "Uncommitted accent draft");
                compare(renderedSummary(), committed);
            }

            // External removal synchronizes the open picker but is not the user's
            // explicit Reset accent action, so the pending hex must survive.
            verify(PlaygroundState.resetAppearanceToken("colors", "accent"));
            assertPickerColor(dialog, defaultAccent);
            assertAccentConsumers(defaultAccent);
            compare(PlaygroundState.appearanceInput, input);
            compare(PlaygroundState.hasAccentOverride, false);
            compare(hex.text, pendingHex);
            compare(editor.accentDraft, pendingHex);
            dialog.close();
            tryCompare(dialog, "visible", false);
            verify(TestProbe.activate(editor));
            click(apply);
            compare(IA.Appearance.colors.accent, pendingHex, "The preserved manual entry can still be applied");
            compare(PlaygroundState.hasAccentOverride, true);
            editor.openAccentPicker();
            assertPickerColor(dialog, IA.Appearance.colors.accent);
            verify(TestProbe.activate(editor));
            click(reset);
            assertPickerColor(dialog, defaultAccent);
            assertAccentConsumers(defaultAccent);
            compare(PlaygroundState.appearanceInput, input, "Reset accent retains all siblings and envelope fields");
            compare(PlaygroundState.hasAccentOverride, false);
            compare(reset.visible, false);
            compare(editor.accentDraft, undefined);
            compare(hex.text.toLowerCase(), String(defaultAccent).toLowerCase());
            compare(editor.jsonDraft, pendingJson);
            compare(json.text, pendingJson);
            verify(findChild(page, "nameField") === field);
            compare(field.text, "Uncommitted accent draft");
            compare(PlaygroundState.draftName, field.text);
            verify(PlaygroundState.dirty);
            compare(renderedSummary(), committed);
            compare(IA.Appearance.scheme, data.scheme);
            compare(IA.Appearance.contrast, "high");
            compare(IA.Appearance.fontScale, 1.1);
            compare(IA.Appearance.metrics.controlRadius, 11);
            compare(IA.Appearance.typography.family, "Serif");
        }

        function test_liveSemanticPalette_data(): var {
            return [{tag: "light", scheme: "light"}, {tag: "dark", scheme: "dark"}];
        }

        function contrastRatio(foreground: color, background: color): real {
            // Independent WCAG sRGB calculation on the actual opaque consumer colors.
            const luminances = [];
            for (const value of [foreground, background]) {
                compare(value.a, 1);
                const channels = [value.r, value.g, value.b];
                for (let index = 0; index < channels.length; ++index) {
                    const channel = channels[index];
                    channels[index] = channel <= 0.04045 ? channel / 12.92
                        : Math.pow((channel + 0.055) / 1.055, 2.4);
                }
                luminances.push(0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2]);
            }
            return (Math.max(luminances[0], luminances[1]) + 0.05)
                / (Math.min(luminances[0], luminances[1]) + 0.05);
        }

        function test_highContrastStates_data(): var {
            const rows = [];
            for (const contrast of ["normal", "high"]) {
                const target = contrast === "high" ? 7 : 5;
                for (const scheme of ["light", "dark"]) {
                    for (const reference of references)
                        rows.push({tag: contrast + "-" + scheme + "-" + reference.key,
                            contrast: contrast, target: target, scheme: scheme,
                            name: "opencode-" + reference.key, role: reference.role});
                    for (const name of ["customPurpleButton", "menuButton"])
                        rows.push({tag: contrast + "-" + scheme + "-" + name,
                            contrast: contrast, target: target, scheme: scheme, name: name, role: ""});
                }
            }
            return rows;
        }

        function test_highContrastStates(data: var): void {
            verify(PlaygroundState.setAppearance({scheme: data.scheme, contrast: data.contrast}));
            const button = findChild(page, data.name) as IAButton;
            verify(button !== null && button.enabled);
            reveal(button);
            verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
            tryCompare(button, "hovered", false);
            tryCompare(button, "down", false);
            const background = button.background as Rectangle;
            const label = findChild(button, "iaLabel") as Text;
            verify(background !== null && label !== null);
            if (data.role.length > 0) {
                compare(button.backgroundColor, IA.Appearance.colors[data.role + "Surface"]);
                compare(button.foregroundColor, IA.Appearance.colors[data.role + "Text"]);
                if (data.role === "primary")
                    compare(button.backgroundColor, "#5c9cf5", "Default primary stays exact");
            } else if (data.name === "customPurpleButton") {
                compare(button.backgroundColor, data.contrast === "high" ? "#6a3d99" : "#8657b8");
            }
            for (const state of ["normal", "hover", "pressed"]) {
                try {
                    if (state === "hover") {
                        verify(TestProbe.move(button, button.width / 2, button.height / 2));
                        tryCompare(button, "hovered", true);
                    } else if (state === "pressed") {
                        verify(TestProbe.press(button, button.width / 2, button.height / 2));
                        tryCompare(button, "down", true);
                    }
                    verify(TestProbe.render(button));
                    verify(background.visible && label.visible);
                    compare(background.color, button.hovered ? button.hoverBackgroundColor : button.backgroundColor);
                    const inset = findChild(background, "iaPressInset") as Rectangle;
                    const content = button.contentItem as IA.IconLabel;
                    verify(inset !== null && content !== null);
                    compare(inset.visible, state === "pressed");
                    compare(content.contentShift, state === "pressed" ? IA.Appearance.metrics.pressContentShift : 0);
                    compare(label.color, button.foregroundColor);
                    const ratio = contrastRatio(label.color, background.color);
                    verify(ratio >= data.target, data.tag + "/" + state + " rendered text contrast: " + ratio);
                    const fg = label.color, bg = background.color;
                    const roundedRatio = contrastRatio(
                        Qt.rgba(Math.round(fg.r * 255) / 255, Math.round(fg.g * 255) / 255, Math.round(fg.b * 255) / 255, 1),
                        Qt.rgba(Math.round(bg.r * 255) / 255, Math.round(bg.g * 255) / 255, Math.round(bg.b * 255) / 255, 1));
                    verify(roundedRatio >= data.target, data.tag + "/" + state + " 8-bit text contrast: " + roundedRatio);
                    if (state !== "normal" && data.name !== "menuButton") {
                        const base = button.backgroundColor;
                        verify(String(bg) !== String(base), data.tag + "/" + state + " feedback changes the resting fill");
                        // Independent CAM16-UCS distance/floor bounds are covered
                        // by the Foundation matrix; this checks its real consumer.
                    }
                    if (data.role.length > 0)
                        compare(background.border.color, label.color, "Semantic border matches rendered text");
                } finally {
                    if (state === "pressed") {
                        // Release away from the control so the neutral menu does not open.
                        verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
                        verify(TestProbe.release(scene, scene.width - 2, scene.height - 2));
                        tryCompare(button, "down", false);
                    }
                }
            }
        }

        function test_previewTogglePreservesConsumersAndDefaults(): void {
            editorScroll.visible = true;
            verify(TestProbe.render(page));
            const preview = findChild(editor, "appearanceContrast") as IASwitch;
            const light = findChild(editor, "appearanceLight") as IASegmentedButton;
            const dark = findChild(editor, "appearanceDark") as IASegmentedButton;
            const defaults = findChild(editor, "appearanceReset") as IAButton;
            verify(preview !== null && light !== null && dark !== null && defaults !== null);
            compare(IA.Appearance.contrast, "normal");
            compare(JSON.stringify(PlaygroundState.appearanceInput), "{}");
            compare(preview.checked, false);

            const field = findChild(page, "nameField") as IATextField;
            reveal(field);
            edit(field, "Uncommitted preview draft");
            const committed = renderedSummary();
            editor.jsonDraft = '{"scheme":"dark"}';
            verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
            verify(TestProbe.render(page));
            const consumers = [];
            for (const name of ["opencode-blue", "opencode-amber", "opencode-rose", "customPurpleButton", "menuButton"]) {
                const button = findChild(page, name) as IAButton;
                verify(button !== null);
                const background = button.background as Rectangle;
                const label = findChild(button, "iaLabel") as Text;
                verify(background !== null && label !== null);
                consumers.push({button: button, background: background, label: label,
                    normal: [String(background.color), String(label.color), String(background.border.color)]});
            }
            const originalPage = page;
            const steps = [
                {button: preview, contrast: "high", scheme: "light"},
                {button: dark, contrast: "high", scheme: "dark"},
                {button: light, contrast: "high", scheme: "light"},
                {button: preview, contrast: "normal", scheme: "light"},
                {button: preview, contrast: "high", scheme: "light"},
                {button: dark, contrast: "high", scheme: "dark"},
                {button: defaults, contrast: "normal", scheme: "light"}
            ];
            for (const step of steps) {
                click(step.button);
                verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
                verify(TestProbe.render(page));
                compare(IA.Appearance.contrast, step.contrast);
                compare(IA.Appearance.scheme, step.scheme);
                compare(preview.checked, step.contrast === "high");
                if (step.button === defaults) {
                    compare(JSON.stringify(PlaygroundState.appearanceInput), "{}");
                } else {
                    compare(PlaygroundState.appearanceInput.contrast, step.contrast);
                    if (step.button === light || step.button === dark)
                        compare(PlaygroundState.appearanceInput.scheme, step.scheme);
                }
                verify(page === originalPage);
                verify(findChild(page, "nameField") === field);
                compare(field.text, "Uncommitted preview draft");
                compare(PlaygroundState.draftName, field.text);
                verify(PlaygroundState.dirty);
                compare(renderedSummary(), committed);
                compare(editor.jsonDraft, '{"scheme":"dark"}');
                for (const consumer of consumers) {
                    const button = consumer.button;
                    verify(findChild(page, button.objectName) === button);
                    verify(button.background === consumer.background);
                    verify(findChild(button, "iaLabel") === consumer.label);
                    tryCompare(button, "hovered", false);
                    if (step.contrast === "normal") {
                        compare([String(consumer.background.color), String(consumer.label.color),
                            String(consumer.background.border.color)], consumer.normal,
                            "Returning to normal restores the prior consumer palette");
                    } else {
                        if (button.objectName === "opencode-blue") {
                            compare(String(consumer.background.color), consumer.normal[0],
                                      "Default blue stays exact in high mode");
                            compare(String(consumer.label.color), consumer.normal[1],
                                      "The unchanged primary surfaces use the same 7:1 label target in both modes");
                            compare(consumer.label.color, IA.Appearance.colors.primaryText);
                        } else if (button.objectName === "opencode-amber" || button.objectName === "opencode-rose") {
                            const reference = references.find(function(value) { return "opencode-" + value.key === button.objectName; });
                            compare(consumer.background.color, IA.Appearance.colors[reference.role + "Surface"]);
                            compare(consumer.label.color, IA.Appearance.colors[reference.role + "Text"]);
                            verify(contrastRatio(consumer.label.color, consumer.background.color) >= 7);
                        } else if (button.objectName === "customPurpleButton") {
                            verify(String(consumer.background.color) !== consumer.normal[0],
                                     "The caller-owned high purple fill changes");
                            verify(String(consumer.label.color) !== consumer.normal[1],
                                     "The changed purple fill reaches the existing consumer foreground");
                        } else {
                            compare(consumer.background.color, IA.Appearance.colors.control,
                                     "The neutral control retains its selected scheme's fill");
                            compare(consumer.label.color, IA.Appearance.colors.text);
                            verify(String(consumer.label.color) !== consumer.normal[1],
                                     "The high profile still strengthens neutral text");
                        }
                    }
                }
            }
        }

        function test_liveSemanticPalette(data: var): void {
            verify(PlaygroundState.setAppearance({scheme: data.scheme}));
            const samples = [];
            for (const reference of references) {
                const button = findChild(page, "opencode-" + reference.key) as IAButton;
                const swatch = findChild(page, "opencode-color-" + reference.key) as IALabel;
                verify(button !== null && swatch !== null);
                const background = button.background as Rectangle;
                const label = findChild(button, "iaLabel") as Text;
                verify(background !== null && label !== null);
                samples.push({reference: reference, button: button, background: background,
                    label: label, swatch: swatch, size: Qt.size(button.width, button.height)});
            }
            clickPage(samples[0].button);
            const caption = findChild(page, "opencodeSelection") as IALabel;
            verify(caption !== null);
            const selectedColor = caption.text;

            const check = findChild(page, "choiceCheck") as IACheckBox;
            const toggle = findChild(page, "choiceSwitch") as IASwitch;
            clickPage(check);
            clickPage(toggle);
            const slider = findChild(page, "valueSlider") as IASlider;
            const progress = findChild(page, "progressPreview") as IAProgressBar;
            verify(slider !== null && progress !== null);
            reveal(slider);
            slider.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(slider, Qt.Key_Right));
            compare(slider.value, 38);
            const sliderFill = findChild(slider, "iaFill") as Rectangle;
            const progressFill = findChild(progress, "iaFill") as Rectangle;
            verify(sliderFill !== null && progressFill !== null);
            const fillWidths = [sliderFill.width, progressFill.width];

            const row = findChild(page, "listRow") as IAItemDelegate;
            const tabs = findChild(page, "previewTabs") as IATabBar;
            verify(tabs !== null);
            clickPage(row);
            clickPage(tabs.itemAt(1));
            const field = findChild(page, "nameField") as IATextField;
            reveal(field);
            edit(field, "Palette update draft");
            verify(TestProbe.keyClick(field, Qt.Key_A, Qt.ControlModifier));
            const selection = field.selectedText;
            compare(selection, "Palette update draft");
            const committed = renderedSummary();
            const scroller = findChild(page, "overviewScroll") as Flickable;
            verify(scroller !== null);
            const scrollY = scroller.contentY;
            const consumers = [check, toggle, slider, progress, row, tabs, field, caption, scroller];

            // A light then dark seed set exercises changing foregrounds as well as fills.
            const palettes = [
                {accent: "#ffdf00"},
                {accent: "#49335f"},
                {accent: "#70c8aa", warning: "#d8a5ed", error: "#ed9b70"},
                {accent: "#214f91", warning: "#654114", error: "#802d38"},
                {accent: "#214f91", warning: "#654114", error: "#802d38",
                    primarySurface: "#c8d6f0", cautionSurface: "#e7c58c", destructiveSurface: "#dfb1bf"}
            ];
            for (let index = 0; index < palettes.length; ++index) {
                const palette = palettes[index];
                const previousText = samples.map(function(sample) { return String(sample.label.color); });
                verify(PlaygroundState.setAppearance({scheme: data.scheme, tokens: {colors: palette}}));
                verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
                verify(TestProbe.render(page));
                for (const consumer of consumers) {
                    verify(consumer !== null);
                    verify(findChild(page, consumer.objectName) === consumer, "Palette edits retain consumer identity");
                }
                for (let sampleIndex = 0; sampleIndex < samples.length; ++sampleIndex) {
                    const sample = samples[sampleIndex];
                    const reference = sample.reference;
                    const surface = palette[reference.role + "Surface"] || palette[reference.seed]
                        || IA.Appearance.colors[reference.role + "Surface"];
                    verify(findChild(page, "opencode-" + reference.key) === sample.button,
                           "The tone model must not recreate Repeater delegates");
                    verify(sample.button.background === sample.background);
                    verify(findChild(sample.button, "iaLabel") === sample.label);
                    verify(findChild(page, "opencode-color-" + reference.key) === sample.swatch);
                    tryCompare(sample.button, "hovered", false);
                    compare(sample.background.color, surface);
                    compare(sample.background.color, IA.Appearance.colors[reference.role + "Surface"]);
                    compare(sample.label.color, sample.button.foregroundColor);
                    compare(sample.background.border.color, sample.button.borderColor);
                    if (index === 1)
                        verify(String(sample.label.color) !== previousText[sampleIndex], "Actual text follows the new fill's readable foreground");
                    compare(sample.swatch.text.toLowerCase(), String(surface).toLowerCase());
                    compare(Qt.size(sample.button.width, sample.button.height), sample.size);
                }
                verify(findChild(slider, "iaFill") === sliderFill);
                verify(findChild(progress, "iaFill") === progressFill);
                compare(sliderFill.color, palette.accent);
                compare(progressFill.color, palette.accent);
                compare([sliderFill.width, progressFill.width], fillWidths);
                compare(slider.value, 38);
                compare(progress.value, 38);
                compare((findChild(page, "valueReadout") as IALabel).text, "38%");
                compare(PlaygroundState.previewValue, 38);
                compare(field.text, "Palette update draft");
                compare(field.selectedText, selection);
                verify(field.activeFocus);
                verify(PlaygroundState.dirty);
                compare(renderedSummary(), committed);
                compare(check.checked, false);
                compare(toggle.checked, false);
                compare(PlaygroundState.optIn, false);
                compare(PlaygroundState.notifications, false);
                compare(tabs.currentIndex, 1);
                compare(PlaygroundState.selectedTab, 1);
                verify(PlaygroundState.rowSelected);
                verify(!row.visible);
                compare(scroller.contentY, scrollY);
                compare(caption.text, selectedColor);
            }
            const input = JSON.stringify(PlaygroundState.appearanceInput);
            clickPage(samples[1].button);
            compare(caption.text.toLowerCase(), "selected: caution " + palettes[4].cautionSurface);
            compare(JSON.stringify(PlaygroundState.appearanceInput), input);
            PlaygroundState.resetFixtures();
            compare(caption.text, "No color selected");
        }

        function assertHost(popup: T.Popup, distinct: bool): void {
            verify(popup !== null);
            tryCompare(popup, "opened", true);
            tryVerify(function() { return TestProbe.exposed(popup.contentItem); });
            verify(TestProbe.render(popup.contentItem));
            const owner = TestProbe.hostWindow(page);
            const host = TestProbe.hostWindow(popup.contentItem);
            verify(owner !== null && host !== null);
            compare(host !== owner, distinct);
        }

        function test_menuAndDialogTransactions(data: var): void {
            page.windowPopups = data.windowPopups;
            const opener = findChild(page, "menuButton") as IAButton;
            const menu = findChild(page, "mixedMenu") as IAMenu;
            const inspect = findChild(page, "menuInspectCollection") as IAMenuItem;
            const inspector = findChild(page, "selectionInspector") as IALabel;
            verify(opener !== null && inspect !== null && inspector !== null);
            if (data.windowPopups && TestProbe.isWayland()) {
                reveal(opener);
                opener.forceActiveFocus(Qt.TabFocusReason);
                tryCompare(opener, "activeFocus", true);
                tryVerify(function() { return TestProbe.nativeKeyHostReady(opener); });
                verify(TestProbe.nativeKeyClick(opener, Qt.Key_Space));
            } else {
                clickPage(opener);
            }
            assertHost(menu, data.windowPopups);
            click(inspect);
            tryCompare(menu, "visible", false);
            verify(inspector.text.startsWith("Design resources\n128 reference items"));
            compare((findChild(page, "previewTabs") as IATabBar).currentIndex, 1);

            const field = findChild(page, "nameField") as IATextField;
            reveal(field);
            edit(field, "Uncommitted form draft");
            const committed = renderedSummary();
            const dialogButton = findChild(page, "dialogButton") as IAToolButton;
            const dialog = findChild(page, "editDialog") as IADialog;
            const dialogField = findChild(page, "dialogNameField") as IATextField;
            verify(dialogButton !== null && dialog !== null && dialogField !== null);
            clickPage(dialogButton);
            assertHost(dialog, data.windowPopups);
            compare(dialogField.text, "Uncommitted form draft");
            edit(dialogField, "Cancelled gallery edit");
            click(dialog.standardButton(T.Dialog.Cancel));
            tryCompare(dialog, "visible", false);
            compare(field.text, "Uncommitted form draft");
            compare(renderedSummary(), committed);

            clickPage(dialogButton);
            assertHost(dialog, data.windowPopups);
            edit(dialogField, "Committed dialog edit");
            const light = String((dialog.background as Rectangle).color);
            verify(PlaygroundState.setAppearance({scheme: "dark"}));
            verify(TestProbe.render(dialog.contentItem));
            verify(String((dialog.background as Rectangle).color) !== light);
            verify(dialogField.activeFocus);
            compare(dialogField.text, "Committed dialog edit");
            click(dialog.standardButton(T.Dialog.Apply));
            tryCompare(dialog, "visible", false);
            compare(field.text, "Committed dialog edit");
            verify(renderedSummary().startsWith("Committed dialog edit\n"));
            verify(!PlaygroundState.dirty);
        }
    }
}
