// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QtTest
import InterActive
import InterActive.Foundation as IA
import InterActive.Tests

Item {
    id: scene
    width: 800
    height: 640
    property bool hostRtl: false
    Binding {
        target: scene.Window.window ? scene.Window.window.LayoutMirroring : null
        property: "enabled"
        value: scene.hostRtl
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding {
        target: scene.Window.window ? scene.Window.window.LayoutMirroring : null
        property: "childrenInherit"
        value: true
        restoreMode: Binding.RestoreBindingOrValue
    }
    Component { id: popupComponent; PopupFixture {} }
    Component { id: buttonBoxComponent; IADialogButtonBox {} }
    Component { id: emptyDialogComponent; IADialog {} }
    Component {
        id: customWidthDialogComponent
        IADialog {
            contentItem: Item { implicitWidth: 700; implicitHeight: 80 }
        }
    }
    Component { id: customButtonComponent; IAButton { text: "Leave unchanged"; T.DialogButtonBox.buttonRole: T.DialogButtonBox.RejectRole } }
    Component {
        id: scrollingDialogComponent
        IADialog {
            id: dialog
            width: 380
            title: "Preferences with a title that must elide at a narrow width"
            property int rows: 1
            standardButtons: T.Dialog.Apply | T.Dialog.Cancel
            focus: true
            modal: true
            Column {
                width: parent.width
                spacing: IA.Appearance.metrics.spacingS
                IATextField { objectName: "firstDraft"; width: parent.width; text: "Retained draft" }
                Repeater {
                    model: dialog.rows
                    Rectangle { width: parent ? parent.width : 0; height: 44; color: IA.Appearance.colors.accent }
                }
                IATextField { objectName: "lastDraft"; width: parent.width; text: "Final field" }
            }
            QtObject { objectName: "nonvisualBodyChild" }
        }
    }
    SignalSpy { id: rejectedSpy; signalName: "rejected" }
    // Like the existing playground host fixtures, a static Window is owned by
    // the fixture, not by QtTest's temporary-object destroy() queue.
    Window {
        id: budgetWindow
        transientParent: scene.Window.window
        property int budgetHeight: 640
        flags: Qt.Dialog
        width: 800
        minimumWidth: 800
        maximumWidth: 800
        height: budgetHeight
        minimumHeight: budgetHeight
        maximumHeight: budgetHeight
        Item { objectName: "budgetParent"; width: 180; height: 140 }
    }
    Component { id: primaryAppearanceComponent; IA.ControlAppearance { source: IA.Appearance; tone: IA.Presentation.Primary } }

    TestCase {
        name: "PopupConsumers"
        when: windowShown
        property PopupFixture fixture

        function modes(): var {
            return [{tag: "item", mode: T.Popup.Item}, {tag: "window", mode: T.Popup.Window}];
        }

        function init(): void {
            IA.Appearance.input = {};
            fixture = createTemporaryObject(popupComponent, scene) as PopupFixture;
            verify(fixture !== null);
            verify(waitForRendering(fixture));
            verify(TestProbe.activate(fixture));
        }

        function cleanup(): void {
            fixture.tooltip.close();
            fixture.submenu.close();
            fixture.menu.close();
            fixture.dialog.close();
            fixture.popup.close();
            scene.hostRtl = false;
            IA.Appearance.input = {};
        }

        function click(item: Item): void {
            verify(TestProbe.click(item, item.width / 2, item.height / 2));
        }

        function test_replacementContentNaturalWidth(): void {
            const dialog = createTemporaryObject(customWidthDialogComponent, scene, {parent: scene}) as IADialog;
            verify(dialog !== null);
            compare(dialog.implicitWidth, 700 + dialog.leftPadding + dialog.rightPadding);
            dialog.contentItem.implicitWidth = 720;
            compare(dialog.implicitWidth, 720 + dialog.leftPadding + dialog.rightPadding);
        }

        function iconIn(item: Item): IA.Icon {
            for (const child of item.children) {
                const icon = child as IA.Icon;
                if (icon !== null && icon.visible)
                    return icon;
                const nested = iconIn(child);
                if (nested !== null)
                    return nested;
            }
            return null;
        }

        function openWithKeyboard(opener: Item, popup: T.Popup, mode: int): void {
            fixture.popupMode = mode;
            opener.forceActiveFocus(Qt.TabFocusReason);
            tryCompare(opener, "activeFocus", true);
            if (mode === T.Popup.Window && TestProbe.isWayland()) {
                tryVerify(function() { return TestProbe.nativeKeyHostReady(opener); });
                verify(TestProbe.nativeKeyClick(opener, Qt.Key_Space));
            } else {
                verify(TestProbe.keyClick(opener, Qt.Key_Space));
            }
            assertHost(popup, mode);
        }

        function assertHost(popup: T.Popup, mode: int): void {
            tryCompare(popup, "opened", true);
            tryVerify(function() { return TestProbe.exposed(popup.contentItem); });
            verify(TestProbe.render(popup.contentItem));
            const owner = TestProbe.hostWindow(fixture);
            const host = TestProbe.hostWindow(popup.contentItem);
            verify(owner !== null);
            verify(host !== null);
            compare(popup.contentItem.Window.window, host);
            if (mode === T.Popup.Window)
                verify(host !== owner, "Window popup has a distinct exposed QQuickWindow, not an Item fallback");
            else {
                compare(host, owner, "Item popup is rendered in its owner's actual window");
                const corner = popup.background.mapToItem(owner.contentItem, 0, 0);
                verify(corner.x >= -0.5 && corner.y >= -0.5);
                verify(corner.x + popup.background.width <= owner.width + 0.5);
                verify(corner.y + popup.background.height <= owner.height + 0.5);
            }
            // Window-mode compositor placement requires the separate marker/
            // output-capture gate; Qt global coordinates are not evidence of it.
        }

        function test_menuThemeWhileOpen_data(): var { return modes(); }

        function test_exactAccentMenu_data(): var {
            const rows = [];
            for (const host of modes()) {
                for (const scheme of ["light", "dark"]) {
                    for (const contrast of ["normal", "high"]) {
                        for (const rtl of [false, true])
                            rows.push({tag: host.tag + "-" + scheme + "-" + contrast + "-" + rtl,
                                mode: host.mode, scheme: scheme, contrast: contrast, rtl: rtl});
                    }
                }
            }
            return rows;
        }

        function menuContrast(first: color, second: color): real {
            const luminance = function(value) {
                const channel = function(c) { return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4); };
                return 0.2126 * channel(value.r) + 0.7152 * channel(value.g) + 0.0722 * channel(value.b);
            };
            const a = luminance(first), b = luminance(second);
            return (Math.max(a, b) + 0.05) / (Math.min(a, b) + 0.05);
        }

        function test_exactAccentMenu(data: var): void {
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast};
            openWithKeyboard(fixture.menuOpener, fixture.menu, data.mode);
            const row = fixture.firstAction;
            row.LayoutMirroring.enabled = data.rtl;
            fixture.menu.currentIndex = 0;
            row.forceActiveFocus(Qt.MouseFocusReason);
            tryCompare(row, "highlighted", true);
            tryCompare(row, "visualFocus", false);
            const surface = row.background as IA.ControlBackground;
            const primary = createTemporaryObject(primaryAppearanceComponent, scene) as IA.ControlAppearance;
            verify(surface !== null && primary !== null);
            const label = findChild(row, "iaLabel") as Text;
            const shortcut = findChild(row, "iaShortcut") as Text;
            verify(label !== null && shortcut !== null);
            verify(TestProbe.render(row));
            const content = row.contentItem;
            const geometry = [row.x, row.y, row.width, row.height, row.implicitWidth, row.implicitHeight,
                content.x, content.y, content.width, content.height, label.x, label.y, label.width, label.height];
            for (const accent of ["", "#F5D90A", data.scheme === "light" ? "#F2F3F5" : "#303237", "#7A1F1F"]) {
                IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast,
                    tokens: {colors: accent.length > 0 ? {accent: accent} : {}}};
                assertHost(fixture.menu, data.mode);
                compare(surface.color, IA.Appearance.colors.accent);
                compare(label.color, IA.Appearance.colors.onAccent);
                compare(shortcut.color, label.color);
                verify(menuContrast(label.color, surface.color) >= 7);
                const outlined = menuContrast(surface.color, IA.Appearance.colors.popup) < (data.contrast === "high" ? 7 : 5);
                compare(surface.border.width, outlined ? IA.Appearance.metrics.borderWidth : 0);
                compare(surface.border.color, primary.stateTable.colors(IA.Presentation.Rest).borderColor);
                verify(TestProbe.render(row));
                compare([row.x, row.y, row.width, row.height, row.implicitWidth, row.implicitHeight,
                    content.x, content.y, content.width, content.height, label.x, label.y, label.width, label.height],
                    geometry, "Live highlight and outline preserve row/content bounds");
                const root = TestProbe.hostWindow(row).contentItem;
                const pixels = grabImage(root);
                for (const sample of [
                    {y: 0.5, color: outlined ? surface.border.color : surface.color},
                    {y: surface.height - 5, color: surface.color}
                ]) {
                    const point = surface.mapToItem(root, surface.width / 2, sample.y);
                    fuzzyCompare(pixels.pixel(Math.floor(point.x * pixels.width / root.width),
                        Math.floor(point.y * pixels.height / root.height)), sample.color, 2,
                        "Real popup exact fill and inward outline pixels");
                }
            }
            fixture.menu.close();
        }

        function test_menuThemeWhileOpen(data: var): void {
            openWithKeyboard(fixture.menuOpener, fixture.menu, data.mode);
            const background = fixture.menu.background as Rectangle;
            verify(background !== null);
            const light = String(background.color);
            const label = findChild(fixture.firstAction, "iaLabel") as Text;
            verify(label !== null);
            const disabledLabel = findChild(fixture.disabledAction, "iaLabel") as Text;
            const shortcut = findChild(fixture.firstAction, "iaShortcut") as Text;
            verify(disabledLabel !== null && shortcut !== null);
            compare(shortcut.text, "Ctrl+K");
            fuzzyCompare(label.mapToItem(fixture.menu.contentItem, 0, 0).x,
                         disabledLabel.mapToItem(fixture.menu.contentItem, 0, 0).x, 0.5,
                         "Menu text shares a leading column despite shortcut/disabled differences");
            const lightText = String(label.color);
            IA.Appearance.input = {scheme: "dark", fontScale: 1.5,
                                   tokens: {metrics: {popupRadius: 11}}};
            verify(waitForRendering(fixture.menu.contentItem));
            assertHost(fixture.menu, data.mode);
            verify(String(background.color) !== light);
            compare(background.color, IA.Appearance.colors.popup);
            compare(background.radius, 11);
            verify(String(label.color) !== lightText);
            compare(disabledLabel.color, fixture.disabledAction.disabledForegroundColor);
            compare(label.font.pixelSize, Math.round(IA.Appearance.typography.controlSize));
            compare(fixture.menuSummary.text, "No action selected");
            click(fixture.firstAction);
            compare(fixture.menuSummary.text, "Compact preview selected");
            tryCompare(fixture.menu, "visible", false);
            tryCompare(fixture.menuOpener, "activeFocus", true);
        }

        function test_nestedMenuEscapeAndOutcome_data(): var { return modes(); }
        function test_nestedMenuEscapeAndOutcome(data: var): void {
            openWithKeyboard(fixture.menuOpener, fixture.menu, data.mode);
            click(fixture.disabledAction);
            compare(fixture.menuSummary.text, "No action selected");
            verify(fixture.menu.visible);
            const entry = fixture.menu.itemAt(2);
            verify(entry !== null);
            click(entry);
            assertHost(fixture.submenu, data.mode);
            if (data.mode === T.Popup.Window)
                verify(TestProbe.hostWindow(fixture.submenu.contentItem)
                       !== TestProbe.hostWindow(fixture.menu.contentItem));
            verify(TestProbe.keyClick(fixture.submenu.contentItem, Qt.Key_Escape));
            tryCompare(fixture.submenu, "visible", false);
            verify(fixture.menu.visible, "Escape closes only the nested level");
            compare(fixture.menuSummary.text, "No action selected");
            verify(TestProbe.keyClick(fixture.menu.contentItem, Qt.Key_Escape));
            tryCompare(fixture.menu, "visible", false);
            tryCompare(fixture.menuOpener, "activeFocus", true);

            openWithKeyboard(fixture.menuOpener, fixture.menu, data.mode);
            click(entry);
            assertHost(fixture.submenu, data.mode);
            const nestedLabel = findChild(fixture.nestedAction, "iaLabel") as Text;
            verify(nestedLabel !== null);
            const light = String((fixture.submenu.background as Rectangle).color);
            IA.Appearance.input = {scheme: "dark", fontScale: 2};
            verify(waitForRendering(fixture.submenu.contentItem));
            verify(String((fixture.submenu.background as Rectangle).color) !== light);
            compare(nestedLabel.font.pixelSize, Math.round(IA.Appearance.typography.controlSize));
            verify(fixture.menu.visible && fixture.submenu.visible);
            fixture.nestedAction.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(fixture.nestedAction, Qt.Key_Space));
            compare(fixture.menuSummary.text, "Summary exported");
            tryCompare(fixture.submenu, "visible", false);
            tryCompare(fixture.menu, "visible", false);
            tryCompare(fixture.menuOpener, "activeFocus", true);
        }

        function test_dialogApplyCancelAndModalBlock_data(): var { return modes(); }

        function test_principalAction_data(): var {
            return [
                {tag: "accept", buttons: T.DialogButtonBox.Ok | T.DialogButtonBox.Cancel, principal: -1, expected: T.DialogButtonBox.Ok},
                {tag: "save", buttons: T.DialogButtonBox.Save | T.DialogButtonBox.Close, principal: -1, expected: T.DialogButtonBox.Save},
                {tag: "yes", buttons: T.DialogButtonBox.Yes | T.DialogButtonBox.No, principal: -1, expected: T.DialogButtonBox.Yes},
                {tag: "apply-only", buttons: T.DialogButtonBox.Apply | T.DialogButtonBox.Cancel, principal: -1, expected: T.DialogButtonBox.Apply},
                {tag: "accept-and-apply", buttons: T.DialogButtonBox.Save | T.DialogButtonBox.Apply, principal: -1, expected: T.DialogButtonBox.Save},
                {tag: "multiple-accept", buttons: T.DialogButtonBox.Ok | T.DialogButtonBox.Save | T.DialogButtonBox.Yes, principal: -1, expected: T.DialogButtonBox.Ok},
                {tag: "override", buttons: T.DialogButtonBox.Save | T.DialogButtonBox.Apply, principal: T.DialogButtonBox.Apply, expected: T.DialogButtonBox.Apply},
                {tag: "none", buttons: T.DialogButtonBox.Save | T.DialogButtonBox.Apply | T.DialogButtonBox.Discard, principal: T.DialogButtonBox.NoButton, expected: T.DialogButtonBox.NoButton},
                {tag: "cancel-only", buttons: T.DialogButtonBox.Cancel | T.DialogButtonBox.Close, principal: -1, expected: T.DialogButtonBox.NoButton},
                {tag: "cancel-override", buttons: T.DialogButtonBox.Save | T.DialogButtonBox.Cancel, principal: T.DialogButtonBox.Cancel, expected: T.DialogButtonBox.NoButton},
                {tag: "missing-override", buttons: T.DialogButtonBox.Save, principal: T.DialogButtonBox.Apply, expected: T.DialogButtonBox.NoButton},
                {tag: "destructive", buttons: T.DialogButtonBox.Save | T.DialogButtonBox.Discard, principal: -1, expected: T.DialogButtonBox.Save}
            ];
        }

        function assertPrincipal(box: IADialogButtonBox, expected: int): void {
            let primaryCount = 0;
            for (let index = 0; index < box.count; ++index) {
                const button = box.itemAt(index) as IAButton;
                verify(button !== null);
                const destructive = button.T.DialogButtonBox.buttonRole === T.DialogButtonBox.DestructiveRole;
                const primary = expected !== T.DialogButtonBox.NoButton && button === box.standardButton(expected);
                compare(button.tone, destructive ? IA.Presentation.Destructive
                    : primary ? IA.Presentation.Primary : IA.Presentation.Neutral);
                if (primary) {
                    ++primaryCount;
                    compare((button.background as Rectangle).color, IA.Appearance.colors.primarySurface,
                            "The principal action consumes the shared solid Primary fill");
                }
            }
            compare(primaryCount, expected === T.DialogButtonBox.NoButton ? 0 : 1);
        }

        function test_principalAction(data: var): void {
            const box = createTemporaryObject(buttonBoxComponent, fixture, {
                standardButtons: data.buttons, principalAction: data.principal
            }) as IADialogButtonBox;
            verify(box !== null);
            verify(TestProbe.render(box));
            assertPrincipal(box, data.expected);
            // Rebuilding Qt's standard-button model must not leave a stale choice.
            box.principalAction = -1;
            box.standardButtons = T.DialogButtonBox.Apply | T.DialogButtonBox.Cancel;
            verify(TestProbe.render(box));
            assertPrincipal(box, T.DialogButtonBox.Apply);
            box.standardButtons |= T.DialogButtonBox.Ok;
            verify(TestProbe.render(box));
            assertPrincipal(box, T.DialogButtonBox.Ok);
            box.principalAction = T.DialogButtonBox.NoButton;
            assertPrincipal(box, T.DialogButtonBox.NoButton);
        }

        function test_dialogHeadingAndEdges_data(): var {
            const rows = [];
            for (const host of modes()) {
                for (const density of ["compact", "touch"]) {
                    for (const scale of [1, 1.5])
                        rows.push({tag: host.tag + "-" + density + "-" + scale,
                            mode: host.mode, density: density, scale: scale});
                }
            }
            return rows;
        }

        function assertDialogEdges(dialog: IADialog): void {
            const footer = dialog.footer as IADialogButtonBox;
            verify(footer !== null);
            verify(TestProbe.render(footer));
            const content = dialog.contentItem;
            const background = dialog.background;
            const contentLeft = content.mapToItem(background, 0, 0).x;
            const contentRight = content.mapToItem(background, content.width, 0).x;
            fuzzyCompare(footer.mapToItem(background, footer.leftPadding, 0).x, contentLeft, 1,
                         "Footer and content share the leading inset");
            fuzzyCompare(footer.mapToItem(background, footer.width - footer.rightPadding, 0).x, contentRight, 1,
                         "Footer and content share the trailing inset");
            fuzzyCompare(footer.contentItem.mapToItem(background, footer.contentItem.width, 0).x, contentRight, 1,
                         "The real button row ends at the content edge");
            let buttonRight = 0;
            for (let index = 0; index < footer.count; ++index) {
                const button = footer.itemAt(index);
                buttonRight = Math.max(buttonRight, button.mapToItem(background, button.width, 0).x);
            }
            fuzzyCompare(buttonRight, contentRight, 1, "The actual trailing button ends at the content edge");
            const title = findChild(dialog, "iaTitle") as IALabel;
            verify(title !== null);
            fuzzyCompare(title.mapToItem(background, title.leftPadding, 0).x, contentLeft, 1,
                         "Title and content share the leading inset");
            compare(footer.topPadding, dialog.topPadding);
            compare(footer.bottomPadding, dialog.bottomPadding);
        }

        function test_dialogHeadingAndEdges(data: var): void {
            IA.Appearance.input = {density: data.density, fontScale: data.scale,
                tokens: {typography: {headingSize: 21, emphasisWeight: 650}, metrics: {popupPadding: 13}}};
            openWithKeyboard(fixture.dialogOpener, fixture.dialog, data.mode);
            const dialog = fixture.dialog;
            const title = findChild(dialog, "iaTitle") as IALabel;
            verify(title !== null);
            compare(title.font.pixelSize, Math.round(21 * data.scale));
            compare(title.font.weight, 650);
            compare(dialog.standardButton(T.Dialog.Apply).font.pixelSize, Math.round(13 * data.scale));
            assertDialogEdges(dialog);
            dialog.leftPadding = 17;
            dialog.rightPadding = 23;
            dialog.topPadding = 11;
            dialog.bottomPadding = 19;
            verify(TestProbe.render(dialog.contentItem));
            assertDialogEdges(dialog);
            // Equal-to-default assignments are genuine local ownership, not a
            // value comparison against the current ordinary typography role.
            dialog.fontPixelSize = IA.Appearance.typography.controlSize;
            dialog.fontWeight = IA.Appearance.typography.controlWeight;
            const localSize = dialog.fontPixelSize;
            const localWeight = dialog.fontWeight;
            IA.Appearance.input = {scheme: "dark", fontScale: 2,
                tokens: {typography: {headingSize: 24, emphasisWeight: 700}}};
            verify(TestProbe.render(dialog.contentItem));
            compare(title.font.pixelSize, Math.round(localSize));
            compare(title.font.weight, localWeight);
            compare(dialog.standardButton(T.Dialog.Apply).font.pixelSize, Math.round(localSize));
            dialog.resetAppearance();
            verify(TestProbe.render(dialog.contentItem));
            compare(title.font.pixelSize, 48);
            compare(title.font.weight, 700);
            compare(dialog.standardButton(T.Dialog.Apply).font.pixelSize, 26);
        }

        function test_dialogWidthRole(): void {
            IA.Appearance.input = {tokens: {metrics: {dialogMinWidth: 260, menuMinWidth: 700}}};
            const dialog = createTemporaryObject(emptyDialogComponent, fixture) as IADialog;
            verify(dialog !== null);
            compare(dialog.implicitWidth, 260);
            IA.Appearance.input = {tokens: {metrics: {dialogMinWidth: 300, menuMinWidth: 20}}};
            compare(dialog.implicitWidth, 300);
        }

        function test_dialogOwnedViewport_data(): var {
            const rows = [];
            for (const host of modes())
                for (const scheme of ["light", "dark"])
                    for (const contrast of ["normal", "high"])
                        for (const rtl of [false, true])
                            for (const touch of [false, true])
                                rows.push({tag: host.tag + "-" + scheme + "-" + contrast + "-" + rtl + (touch ? "-touch" : ""),
                                    mode: host.mode, scheme: scheme, contrast: contrast, rtl: rtl, touch: touch});
            return rows;
        }

        function test_dialogOwnedViewport(data: var): void {
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast,
                density: data.touch ? "touch" : "compact", fontScale: data.touch ? 1.5 : 1};
            const dialog = createTemporaryObject(scrollingDialogComponent, fixture, {parent: fixture, popupType: data.mode}) as IADialog;
            verify(dialog !== null);
            scene.hostRtl = data.rtl;
            dialog.open();
            assertHost(dialog, data.mode);
            const scroll = findChild(dialog, "iaDialogScroll") as IAScrollView;
            const view = findChild(dialog, "iaDialogViewport") as Flickable;
            const body = findChild(dialog, "iaDialogBody") as T.Pane;
            const top = findChild(dialog, "iaDialogTopEdge") as Rectangle;
            const bottom = findChild(dialog, "iaDialogBottomEdge") as Rectangle;
            const topFade = findChild(dialog, "iaDialogTopFade") as Rectangle;
            const bottomFade = findChild(dialog, "iaDialogBottomFade") as Rectangle;
            const close = findChild(dialog, "iaDialogClose") as IAToolButton;
            const draft = findChild(dialog, "firstDraft") as IATextField;
            const last = findChild(dialog, "lastDraft") as IATextField;
            verify(scroll !== null && view !== null && body !== null && top !== null && bottom !== null
                && topFade !== null && bottomFade !== null && close !== null && draft !== null && last !== null);
            verify(findChild(dialog, "nonvisualBodyChild") !== null);
            verify(view.contentHeight <= view.height + 0.5, "A fitting body is fully visible without scrolling");
            fuzzyCompare(dialog.height, dialog.implicitHeight, 0.5);
            compare(top.visible, false);
            compare(bottom.visible, false);
            compare(scroll.T.ScrollBar.vertical.visible, false);
            const width = view.width;
            const lane = scroll.T.ScrollBar.vertical.width + IA.Appearance.metrics.spacingXS;
            fuzzyCompare(scroll.width - view.width, lane, 0.5);
            fuzzyCompare(view.x, data.rtl ? lane : 0, 0.5);
            const gap = view.mapToItem(dialog.contentItem, 0, 0).y - top.y - top.height;
            fuzzyCompare(gap, IA.Appearance.metrics.spacingS, 0.5);
            fuzzyCompare(bottom.y - view.mapToItem(dialog.contentItem, 0, view.height).y, gap, 0.5);
            const frame = dialog.background as Rectangle;
            verify(frame !== null);
            fuzzyCompare(top.width + 2 * frame.border.width, dialog.width, 0.5);
            fuzzyCompare(topFade.mapToItem(view, 0, 0).x, 0, 0.5);
            fuzzyCompare(topFade.width, view.width, 0.5);
            fuzzyCompare(bottomFade.mapToItem(view, 0, 0).x, 0, 0.5);
            fuzzyCompare(bottomFade.width, view.width, 0.5);
            compare(close.Accessible.name, "Close");
            verify(String(close.icon.source).indexOf("close.svg") >= 0);
            fuzzyCompare(close.width, close.height, 0.5);
            const closePoint = close.mapToItem(dialog.header, 0, 0);
            fuzzyCompare(closePoint.x, data.rtl ? 0 : dialog.width - close.width, 0.5,
                "Close occupies the structural trailing header slot");
            const icon = iconIn(close.contentItem);
            verify(icon !== null);
            tryCompare(icon, "available", true, 5000, "The real close asset renders");
            const iconPoint = icon.mapToItem(close, 0, 0);
            fuzzyCompare(iconPoint.x, (close.width - icon.width) / 2, 1);
            fuzzyCompare(iconPoint.y, (close.height - icon.height) / 2, 1);
            fuzzyCompare(iconPoint.x, iconPoint.y, 1);

            dialog.rows = 40;
            tryVerify(function() { return view.contentHeight > view.height; });
            verify(TestProbe.render(view));
            verify(dialog.height <= dialog._heightBudget + 0.5);
            compare(dialog._hostGeometry.availableGeometry, TestProbe.screenAvailableGeometry(fixture));
            console.info("IA_DIALOG_GROWTH " + JSON.stringify({mode: data.mode,
                natural: dialog.implicitHeight, actual: dialog.height, budget: dialog._heightBudget,
                body: view.contentHeight, viewport: view.height}));
            fuzzyCompare(view.width, width, 0.5, "Lane width does not change when overflowing");
            compare(top.visible, false);
            compare(bottom.visible, true);
            compare(bottomFade.visible, true);
            mouseWheel(view, view.width / 2, view.height / 2, 0, -120);
            tryVerify(function() { return view.contentY > view.originY; });
            scroll.forceActiveFocus(Qt.TabFocusReason);
            const beforeKey = view.contentY;
            verify(TestProbe.keyClick(scroll, Qt.Key_Down));
            tryVerify(function() { return view.contentY > beforeKey; });
            compare(top.visible, false, "Owned heading supplies the only top separator");
            compare(bottom.visible, true);
            compare(topFade.visible, true);
            fuzzyCompare(view.mapToItem(dialog.contentItem, 0, 0).y - top.y - top.height, gap, 0.5);
            verify(TestProbe.render(view));
            const root = TestProbe.hostWindow(view).contentItem;
            const pixels = grabImage(root);
            const titleSeparator = findChild(dialog, "iaDialogTitleSeparator") as Rectangle;
            verify(titleSeparator !== null && titleSeparator.visible);
            const point = titleSeparator.mapToItem(root, titleSeparator.width / 2, 0.5);
            fuzzyCompare(pixels.pixel(Math.floor(point.x * pixels.width / root.width),
                Math.floor(point.y * pixels.height / root.height)), IA.Appearance.colors.separator, 2,
                "The owned title boundary paints the real separator-role pixel");
            const fadePoint = view.mapToItem(root, view.width / 2, 0.5);
            fuzzyCompare(pixels.pixel(Math.floor(fadePoint.x * pixels.width / root.width),
                Math.floor(fadePoint.y * pixels.height / root.height)), dialog.backgroundColor, 10,
                "The inward fade paints the resolved surface over real contrasting body pixels");
            view.contentY = view.originY + view.contentHeight - view.height;
            verify(TestProbe.render(view));
            compare(top.visible, false, "No duplicate top separator at the end");
            compare(bottom.visible, false);
            compare(bottomFade.visible, false);
            const lastPoint = last.mapToItem(view, 0, 0);
            verify(lastPoint.y >= -0.5 && lastPoint.y + last.height <= view.height + 0.5, "Final field reachable");
            click(last);
            tryCompare(last, "activeFocus", true);
            verify(TestProbe.keyClick(last, Qt.Key_A, Qt.ControlModifier));
            const selected = last.selectedText;
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast, reducedMotion: true,
                density: data.touch ? "touch" : "compact", fontScale: data.touch ? 1.5 : 1};
            verify(TestProbe.render(view));
            compare(top.visible, false, "Reduced motion retains just the permanent title separator");
            compare(topFade.visible, false);
            compare(last.text, "Final field");
            compare(last.selectedText, selected);
            verify(last.activeFocus);
            compare(draft.text, "Retained draft");
            dialog.width = 340;
            verify(TestProbe.render(view));
            fuzzyCompare(body.width, view.width, 0.5);
            fuzzyCompare(scroll.width - view.width, lane, 0.5);
            const heading = findChild(dialog, "iaTitle") as IALabel;
            verify(heading !== null && heading.truncated, "Title elides rather than wrapping");
            dialog.width = 380;
            dialog.rows = 1;
            tryVerify(function() { return view.contentHeight <= view.height + 0.5; });
            verify(TestProbe.render(view));
            compare(top.visible, false);
            compare(bottom.visible, false);
            fuzzyCompare(view.width, width, 0.5);
            const fittingHeight = dialog.height;
            fixture.height = 210;
            verify(TestProbe.render(view));
            fuzzyCompare(dialog.height, fittingHeight, 0.5, "Local parent size is not the containing window budget");
            fixture.height = 640;
            dialog.height = 220;
            dialog.width = 340;
            verify(TestProbe.render(view));
            fuzzyCompare(dialog.height, 220, 0.5, "Explicit consumer height remains authoritative");
            const naturalWidth = dialog.implicitWidth;
            dialog.title += " with substantially more natural title width";
            dialog.rows = 80;
            tryVerify(function() { return view.contentHeight > view.height && dialog.implicitWidth > naturalWidth; });
            verify(TestProbe.render(view));
            fuzzyCompare(dialog.height, 220, 0.5, "Live content growth preserves explicit height");
            fuzzyCompare(dialog.width, 340, 0.5, "Live implicit width change preserves explicit width");
            rejectedSpy.target = dialog;
            rejectedSpy.clear();
            click(close);
            tryCompare(dialog, "visible", false);
            compare(rejectedSpy.count, 1);
            rejectedSpy.target = null;
        }

        function test_dialogHostWindowBudget_data(): var { return modes(); }
        function test_dialogHostWindowBudget(data: var): void {
            const host = budgetWindow;
            host.budgetHeight = 640;
            host.show();
            const parentItem = findChild(host, "budgetParent") as Item;
            verify(parentItem !== null);
            tryVerify(function() { return TestProbe.exposed(parentItem); });
            tryCompare(host, "height", 640);
            const dialog = createTemporaryObject(scrollingDialogComponent, parentItem,
                {parent: parentItem, popupType: data.mode, rows: 40}) as IADialog;
            verify(dialog !== null);
            dialog.open();
            tryCompare(dialog, "opened", true);
            const view = findChild(dialog, "iaDialogViewport") as Flickable;
            verify(view !== null);
            tryVerify(function() { return TestProbe.exposed(view); });
            verify(TestProbe.render(view));
            const screen = TestProbe.screenAvailableGeometry(parentItem);
            const budget = data.mode === T.Popup.Item ? host.height : screen.height;
            fuzzyCompare(dialog.height, budget - dialog.topMargin - dialog.bottomMargin, 0.5);
            verify(view.contentHeight > view.height);
            host.budgetHeight = 480;
            tryCompare(host, "height", 480);
            const resizedBudget = data.mode === T.Popup.Item ? host.height : screen.height;
            tryVerify(function() { return Math.abs(dialog.height - resizedBudget + dialog.topMargin + dialog.bottomMargin) <= 0.5; });
            verify(TestProbe.render(view));
            verify(view.contentHeight > view.height);
            console.info("IA_DIALOG_HOST_BUDGET " + JSON.stringify({mode: data.mode, hostHeight: host.height,
                localParentHeight: parentItem.height, actual: dialog.height, screen: screen}));
            dialog.close();
            host.close();
        }

        function test_dialogRejectQueryUpdates_data(): var { return test_dialogOwnedViewport_data(); }
        function test_dialogRejectQueryUpdates(data: var): void {
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast};
            const dialog = createTemporaryObject(emptyDialogComponent, fixture, {parent: fixture, title: "Actions", width: 340, popupType: data.mode}) as IADialog;
            verify(dialog !== null);
            scene.hostRtl = data.rtl;
            dialog.open();
            tryCompare(dialog, "opened", true);
            const box = dialog.footer as IADialogButtonBox;
            const close = findChild(dialog, "iaDialogClose") as IAToolButton;
            verify(box !== null && close !== null);
            tryVerify(function() { return TestProbe.exposed(dialog.contentItem); });
            for (const buttons of [T.Dialog.Ok, T.Dialog.Apply, T.Dialog.Cancel, T.Dialog.Close, T.Dialog.Ok]) {
                dialog.standardButtons = buttons;
                verify(TestProbe.render(box));
                compare(box.hasRejectAction, buttons === T.Dialog.Cancel || buttons === T.Dialog.Close);
                compare(close.visible, box.hasRejectAction);
            }
            const custom = createTemporaryObject(customButtonComponent, scene) as IAButton;
            verify(custom !== null);
            box.addItem(custom);
            tryCompare(box, "hasRejectAction", true);
            compare(close.visible, true);
            custom.T.DialogButtonBox.buttonRole = T.DialogButtonBox.ApplyRole;
            compare(box.hasRejectAction, false);
            compare(close.visible, false);
            custom.T.DialogButtonBox.buttonRole = T.DialogButtonBox.RejectRole;
            compare(close.visible, true);
            rejectedSpy.target = dialog;
            rejectedSpy.clear();
            click(close);
            tryCompare(dialog, "visible", false);
            compare(rejectedSpy.count, 1, "A custom RejectRole provides the same native close outcome");
            rejectedSpy.target = null;
            dialog.open();
            tryCompare(dialog, "opened", true);
            box.removeItem(custom);
            compare(box.hasRejectAction, false);
            compare(close.visible, false);
            dialog.close();
        }

        function test_dialogApplyCancelAndModalBlock(data: var): void {
            openWithKeyboard(fixture.dialogOpener, fixture.dialog, data.mode);
            tryCompare(fixture.dialogEditor, "activeFocus", true);
            verify(TestProbe.keyClick(fixture.dialogEditor, Qt.Key_A, Qt.ControlModifier));
            verify(TestProbe.typeAscii(fixture.dialogEditor, "Avery Stone"));
            compare(fixture.dialogEditor.text, "Avery Stone");
            compare(fixture.committedSummary.text, "Saved: Maya Chen");

            // Deliver to the owner window, not the modal host. Its overlay/modal
            // handling must prevent the background consumer from activating.
            click(fixture.backgroundButton);
            compare(fixture.backgroundSummary.text, "Background unchanged");
            verify(fixture.dialog.visible);
            const apply = fixture.dialog.standardButton(T.Dialog.Apply);
            const cancel = fixture.dialog.standardButton(T.Dialog.Cancel);
            verify(apply !== null && cancel !== null);
            verify(findChild(apply, "iaLabel") !== null, "Standard dialog buttons use IA visuals");
            click(apply);
            compare(fixture.committedSummary.text, "Saved: Avery Stone");
            verify(fixture.dialog.visible, "Apply commits without dismissing the dialog");
            click(fixture.dialogEditor);
            verify(TestProbe.keyClick(fixture.dialogEditor, Qt.Key_A, Qt.ControlModifier));
            verify(TestProbe.typeAscii(fixture.dialogEditor, "Cancelled draft"));
            click(cancel);
            tryCompare(fixture.dialog, "visible", false);
            compare(fixture.committedSummary.text, "Saved: Avery Stone");
            tryCompare(fixture.dialogOpener, "activeFocus", true);
            click(fixture.backgroundButton);
            compare(fixture.backgroundSummary.text, "Background activated");
            openWithKeyboard(fixture.dialogOpener, fixture.dialog, data.mode);
            compare(fixture.dialogEditor.text, "Avery Stone");
            verify(TestProbe.keyClick(fixture.dialogEditor, Qt.Key_Escape));
            tryCompare(fixture.dialog, "visible", false);
            compare(fixture.committedSummary.text, "Saved: Avery Stone");
        }

        function test_dialogThemePreservesDraftFocusAndOverrides_data(): var { return modes(); }
        function test_dialogThemePreservesDraftFocusAndOverrides(data: var): void {
            fixture.dialog.controlRadius = 13;
            openWithKeyboard(fixture.dialogOpener, fixture.dialog, data.mode);
            const editor = fixture.dialogEditor;
            editor.backgroundColor = "purple";
            editor.fontPixelSize = 18;
            editor.controlRadius = 9;
            tryCompare(editor, "activeFocus", true);
            verify(TestProbe.keyClick(editor, Qt.Key_A, Qt.ControlModifier));
            verify(TestProbe.typeAscii(editor, "Retained dialog draft"));
            verify(TestProbe.keyClick(editor, Qt.Key_A, Qt.ControlModifier));
            const selected = editor.selectedText;
            const light = String((fixture.dialog.background as Rectangle).color);
            const apply = fixture.dialog.standardButton(T.Dialog.Apply);
            const applyLabel = findChild(apply, "iaLabel") as Text;
            verify(applyLabel !== null);
            IA.Appearance.input = {scheme: "dark", fontScale: 1.5,
                                   tokens: {metrics: {popupRadius: 10}}};
            verify(waitForRendering(fixture.dialog.contentItem));
            assertHost(fixture.dialog, data.mode);
            compare(editor.text, "Retained dialog draft");
            compare(editor.selectedText, selected);
            verify(editor.activeFocus);
            compare(fixture.committedSummary.text, "Saved: Maya Chen");
            verify(String((fixture.dialog.background as Rectangle).color) !== light);
            compare((fixture.dialog.background as Rectangle).color, IA.Appearance.colors.popup);
            compare((fixture.dialog.background as Rectangle).radius, 13);
            compare((editor.background as Rectangle).color, "#800080");
            compare((editor.background as Rectangle).radius, 9);
            compare(editor.font.pixelSize, 18);
            compare(applyLabel.font.pixelSize, Math.round(IA.Appearance.typography.controlSize));
            editor.resetAppearance();
            fixture.dialog.resetAppearance();
            compare(editor.font.pixelSize, applyLabel.font.pixelSize);
            compare((fixture.dialog.background as Rectangle).radius, 10);
            verify(editor.activeFocus);
            click(apply);
            compare(fixture.committedSummary.text, "Saved: Retained dialog draft");
        }

        function test_dialogFooterPreservesOutline_data(): var {
            const rows = [];
            for (const host of modes()) {
                for (const scheme of ["light", "dark"])
                    rows.push({tag: host.tag + "-" + scheme, mode: host.mode, scheme: scheme});
            }
            return rows;
        }

        function test_dialogFooterPreservesOutline(data: var): void {
            IA.Appearance.input = {scheme: data.scheme};
            openWithKeyboard(fixture.dialogOpener, fixture.dialog, data.mode);
            const dialog = fixture.dialog;
            const background = dialog.background as Rectangle;
            const footer = dialog.footer;
            verify(background !== null && footer !== null);
            verify(footer.visible && footer.height > 0);
            const root = TestProbe.hostWindow(dialog.contentItem).contentItem;
            const originalX = dialog.x, originalY = dialog.y;

            for (const customized of [false, true]) {
                dialog.x = originalX; dialog.y = originalY;
                if (customized) {
                    dialog.backgroundColor = "#d6e2ef";
                    dialog.borderColor = "#27445e";
                    IA.Appearance.input = {scheme: data.scheme === "light" ? "dark" : "light"};
                }
                verify(TestProbe.render(background));
                const footerCenter = footer.mapToItem(background, footer.width / 2, footer.height / 2);
                const halfBorder = background.border.width / 2;
                verify(halfBorder > 0);
                const samples = [
                    {x: halfBorder, y: footerCenter.y, color: dialog.borderColor, edge: "left footer edge"},
                    {x: background.width - halfBorder, y: footerCenter.y, color: dialog.borderColor, edge: "right footer edge"},
                    {x: background.width / 2, y: background.height - halfBorder, color: dialog.borderColor, edge: "bottom edge"},
                    {x: dialog.leftPadding + background.radius, y: footerCenter.y,
                     color: dialog.backgroundColor, edge: "surface beneath the footer"}
                ];
                for (const aligned of [false, true]) {
                    if (aligned) {
                        // Full coverage is only expected when native Rectangle
                        // edges coincide with physical pixels. Item centering
                        // legitimately gives an odd-height dialog a half-pixel y.
                        const origin = background.mapToItem(root, 0, 0);
                        const dpr = TestProbe.effectiveDpr(background);
                        dialog.x += Math.round(origin.x * dpr) / dpr - origin.x;
                        dialog.y += Math.round(origin.y * dpr) / dpr - origin.y;
                    }
                    verify(TestProbe.render(background));
                    // Capture the same real native background without footer
                    // paint, retaining allocation and all fractional geometry.
                    footer.opacity = 0;
                    verify(TestProbe.render(background));
                    const uncovered = grabImage(root);
                    footer.opacity = 1;
                    verify(TestProbe.render(background));
                    const pixels = grabImage(root);
                    verify(pixels.width > 0 && pixels.height > 0);
                    const scaleX = pixels.width / root.width;
                    const scaleY = pixels.height / root.height;
                    for (const sample of samples) {
                        const point = background.mapToItem(root, sample.x, sample.y);
                        const x = Math.floor(point.x * scaleX), y = Math.floor(point.y * scaleY);
                        compare(pixels.pixel(x, y), uncovered.pixel(x, y),
                            sample.edge + " is unchanged by footer paint, including fractional coverage");
                        if (aligned)
                            fuzzyCompare(pixels.pixel(x, y), sample.color, 2,
                                sample.edge + (customized ? " with local appearance" : ""));
                    }
                }
            }
        }

        function test_popupAndTooltip_data(): var { return modes(); }
        function test_popupAndTooltip(data: var): void {
            openWithKeyboard(fixture.popupOpener, fixture.popup, data.mode);
            tryCompare(fixture.popupEditor, "activeFocus", true);
            verify(TestProbe.keyClick(fixture.popupEditor, Qt.Key_A, Qt.ControlModifier));
            verify(TestProbe.typeAscii(fixture.popupEditor, "Retained filter"));
            const light = String((fixture.popup.background as Rectangle).color);
            IA.Appearance.input = {scheme: "dark", fontScale: 1.5};
            verify(waitForRendering(fixture.popup.contentItem));
            compare(fixture.popupEditor.text, "Retained filter");
            verify(fixture.popupEditor.activeFocus);
            verify(String((fixture.popup.background as Rectangle).color) !== light);
            compare(fixture.popupEditor.font.pixelSize, Math.round(IA.Appearance.typography.controlSize));
            verify(TestProbe.keyClick(fixture.popupEditor, Qt.Key_Escape));
            tryCompare(fixture.popup, "visible", false);
            tryCompare(fixture.popupOpener, "activeFocus", true);
            fixture.tooltip.open();
            assertHost(fixture.tooltip, data.mode);
            verify(fixture.popupOpener.activeFocus, "Standalone tooltip does not steal focus");
            fixture.tooltip.close();
            tryCompare(fixture.tooltip, "visible", false);
            verify(fixture.popupOpener.activeFocus);
        }
    }
}
