// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QtTest
import InterActive
import InterActive.Foundation as IA
import InterActive.Tests

Item {
    id: scene
    width: 640; height: 480
    property bool rtl: false
    Binding {
        target: scene.Window.window ? scene.Window.window.LayoutMirroring : null
        property: "enabled"
        value: scene.rtl
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding {
        target: scene.Window.window ? scene.Window.window.LayoutMirroring : null
        property: "childrenInherit"
        value: true
        restoreMode: Binding.RestoreBindingOrValue
    }
    Component {
        id: comboComponent
        IAComboBox {
            x: 40; y: 40; width: 180
            model: ["Daily", "Weekly", "Custom"]
            Accessible.name: "Check interval"
        }
    }
    Component {
        id: modelComponent
        ListModel {
            ListElement { caption: "Same as Downloads"; token: "default" }
            ListElement { caption: "Custom: 137 minutes"; token: "137" }
            ListElement { caption: "Every day"; token: "1440" }
        }
    }
    // Native semantic reference only; not an IA rendering/interaction fallback.
    Component { id: nativeComboComponent; T.ComboBox { editable: true } }
    SignalSpy { id: activated; signalName: "activated" }
    SignalSpy { id: accepted; signalName: "accepted" }

    TestCase {
        id: tests
        name: "ComboBox"
        when: windowShown
        property IAComboBox combo

        function init(): void {
            IA.Appearance.input = {reducedMotion:true};
            scene.rtl = false;
            combo = createTemporaryObject(comboComponent, scene) as IAComboBox;
            verify(combo !== null);
            activated.target = combo; activated.clear();
            accepted.target = combo; accepted.clear();
            verify(TestProbe.activateForInput(combo));
            verify(TestProbe.move(scene, 600, 450));
            verify(TestProbe.render(combo));
            failOnWarning(/.*(TypeError|ReferenceError|Binding loop|polish loop|Unable to assign).*/);
        }
        function cleanup(): void {
            combo.popup.close();
            activated.target = null; accepted.target = null;
            scene.rtl = false;
        }
        function part(name: string): var {
            const item = findChild(combo, name) as Item;
            verify(item !== null, name);
            return item;
        }
        function open(mode: int): void {
            combo.popup.popupType = mode;
            verify(TestProbe.activateForInput(combo));
            combo.forceActiveFocus(Qt.TabFocusReason);
            if (TestProbe.isWayland()) {
                verify(TestProbe.nativeKeyHostReady(combo));
                verify(TestProbe.nativeKeyClick(combo, Qt.Key_Space));
            } else {
                verify(TestProbe.keyClick(combo, Qt.Key_Space));
            }
            tryCompare(combo.popup, "opened", true);
            tryVerify(() => TestProbe.exposed(combo.popup.contentItem));
            verify(TestProbe.render(combo.popup.contentItem));
            const owner = TestProbe.hostWindow(combo);
            const host = TestProbe.hostWindow(combo.popup.contentItem);
            verify(host !== null && TestProbe.exposed(combo.popup.contentItem));
            compare(host === owner, mode === T.Popup.Item, "Actual popup hosting, not fallback");
            verify(TestProbe.inputHostReady(combo), "Qt ComboBox retains its keyboard owner");
        }
        function test_stringModelAndActivation(): void {
            compare(combo.count, 3); compare(combo.currentText, "Daily");
            compare(part("iaComboLabel").text, "Daily");
            compare(combo.currentValue, "Daily");
            combo.currentIndex = 1;
            compare(combo.currentText, "Weekly"); compare(activated.count, 0);
            combo.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(combo, Qt.Key_Down));
            compare(combo.currentText, "Custom"); compare(activated.count, 1);
            compare(activated.signalArguments[0][0], 2);
            verify(TestProbe.keyClick(combo, Qt.Key_Up));
            compare(combo.currentValue, "Weekly"); compare(activated.count, 2);
            compare(TestProbe.accessibleState(combo).role, 46); // QAccessible::ComboBox
            compare(TestProbe.accessibleState(combo).name, "Check interval");
            compare(TestProbe.accessibleState(combo).focusable, true);
        }
        function test_roleModelExactValues(): void {
            const model = createTemporaryObject(modelComponent, scene) as ListModel;
            combo.textRole = "caption"; combo.valueRole = "token"; combo.model = model;
            combo.currentValue = "137";
            compare(combo.currentIndex, 1); compare(combo.currentValue, "137");
            compare(part("iaComboLabel").text, "Custom: 137 minutes");
            compare(activated.count, 0);
            model.insert(0, {caption:"Every hour",token:"60"});
            compare(combo.currentValue, "137"); compare(combo.currentIndex, 2);
            model.setProperty(2, "caption", "Custom: exactly 137 minutes");
            compare(part("iaComboLabel").text, "Custom: exactly 137 minutes");
            compare(activated.count, 0);
            combo.displayText = "Application-owned caption";
            compare(part("iaComboLabel").text, "Application-owned caption");
            combo.displayText = undefined;
            compare(part("iaComboLabel").text, "Custom: exactly 137 minutes");
            combo.currentValue = "not present";
            compare(combo.currentIndex, -1); compare(combo.currentValue, "not present");
            compare(activated.count, 0);
        }
        function test_popup_data(): var {
            const rows = [];
            for (const mode of [T.Popup.Item, T.Popup.Window])
                for (const roles of [false, true])
                    rows.push({tag:mode+"-"+roles,mode:mode,roles:roles});
            return rows;
        }
        function test_popup(data): void {
            if (data.roles) {
                combo.textRole = "caption"; combo.valueRole = "token";
                combo.model = createTemporaryObject(modelComponent, scene);
            }
            open(data.mode);
            const list = combo.popup.contentItem as ListView;
            verify(list !== null);
            tryVerify(function() { return list.itemAtIndex(1) !== null; });
            compare(list.itemAtIndex(1).text, data.roles ? "Custom: 137 minutes" : "Weekly");
            // Synchronization while open does not emit a user activation or replace the view.
            combo.currentIndex = 1;
            compare(activated.count, 0); compare(combo.popup.contentItem, list);
            combo.popup.close(); tryCompare(combo.popup, "visible", false);
            open(data.mode);
            verify(TestProbe.keyClick(combo, Qt.Key_Down));
            tryCompare(combo, "highlightedIndex", 2);
            verify(TestProbe.keyClick(combo, Qt.Key_Return));
            tryCompare(combo.popup, "visible", false);
            compare(combo.currentValue, data.roles ? "1440" : "Custom");
            compare(activated.count, 1);
            open(data.mode);
            verify(TestProbe.keyClick(combo, Qt.Key_Escape));
            tryCompare(combo.popup, "visible", false); compare(activated.count, 1);
        }
        function test_appearanceAndGeometry_data(): var {
            const rows = [];
            for (const scheme of ["light", "dark"])
                for (const density of ["compact", "touch"])
                    for (const rtl of [false, true])
                        rows.push({tag:scheme+"-"+density+"-"+rtl,scheme:scheme,density:density,rtl:rtl});
            return rows;
        }
        function test_appearanceAndGeometry(data): void {
            IA.Appearance.input = {scheme:data.scheme,density:data.density,fontScale:1.5,reducedMotion:true};
            scene.rtl = data.rtl;
            verify(TestProbe.render(combo)); compare(combo.mirrored, data.rtl);
            const slot = combo.indicator, glyph = part("iaComboChevron");
            compare(slot.width, combo.height); compare(slot.height, combo.height);
            compare(slot.x, data.rtl ? 0 : combo.width - combo.height);
            const p = glyph.mapToItem(slot, 0, 0);
            fuzzyCompare(p.x, p.y, 0.01);
            fuzzyCompare(p.x, slot.width - p.x - glyph.width, 0.01);
            fuzzyCompare(p.y, slot.height - p.y - glyph.height, 0.01);
            if (data.density === "touch") verify(combo.height >= 44);
            const before = [combo.width,combo.height,slot.x,slot.width];
            combo.leadingColor = "#234567";
            verify(TestProbe.render(combo));
            compare([combo.width,combo.height,slot.x,slot.width], before);
            const swatch = part("iaComboSwatch"), label = part("iaComboLabel");
            compare(swatch.visible, true);
            compare(swatch.x, data.rtl ? combo.contentItem.width - swatch.width : 0);
            if (data.rtl)
                verify(label.x + label.width + combo.spacing <= swatch.x + 0.5);
            else
                verify(swatch.x + swatch.width + combo.spacing <= label.x + 0.5);
            compare(TestProbe.renderedPixel(swatch, swatch.width/2, swatch.height/2), combo.leadingColor);
            combo.inherited = true;
            compare(label.color, IA.Appearance.colors.secondaryText);
            compare(combo.enabled, true);
            combo.inherited = false; verify(label.color !== IA.Appearance.colors.secondaryText);
            combo.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.render(combo));
            compare(combo.visualFocus, true); compare((combo.background as IA.ControlBackground).focused, true);
            compare([combo.width,combo.height,slot.x,slot.width], before);
            combo.enabled = false;
            verify(TestProbe.render(combo));
            compare(TestProbe.accessibleState(combo).disabled, true);
            compare(label.color, combo.disabledForegroundColor);
            compare([combo.width,combo.height,slot.x,slot.width], before);
        }
        function test_openAppearanceUpdateAndElision(): void {
            combo.model = ["A very long custom value which must stay on one line", "Weekly"];
            combo.leadingColor = "#345678"; combo.inherited = true;
            open(T.Popup.Item);
            const list = combo.popup.contentItem, editor = combo.contentItem;
            const label = part("iaComboLabel") as Text;
            compare(label.elide, Text.ElideRight); compare(label.wrapMode, Text.NoWrap);
            verify(label.truncated);
            const before = combo.currentValue;
            IA.Appearance.input = {scheme:"dark",density:"touch",tokens:{colors:{accent:"#987654"}},reducedMotion:true};
            verify(TestProbe.render(list));
            compare(combo.popup.opened, true); compare(combo.popup.contentItem, list);
            compare(combo.contentItem, editor); compare(combo.currentValue, before);
            compare(label.color, IA.Appearance.colors.secondaryText); compare(activated.count, 0);
            combo.foregroundColor = "#abcdef";
            combo.inherited = false; compare(label.color, "#abcdef");
            combo.resetAppearance(); verify(label.color !== "#abcdef");
        }
        function test_bottomAndLargeModel(): void {
            const values = [];
            for (let i=0; i<60; ++i) values.push("Option " + i);
            combo.model = values;
            combo.popupMargins = 24;
            const host = TestProbe.hostWindow(combo);
            combo.y = host.height - combo.height - 24;
            open(T.Popup.Item);
            const p = combo.popup.background.mapToItem(host.contentItem, 0, 0);
            verify(p.y >= 23.5);
            verify(p.y + combo.popup.height <= host.height - 23.5);
            verify(p.y < combo.y, "Bottom popup flips upward");
            compare(combo.popup.width, combo.width);
            const list = combo.popup.contentItem as ListView;
            verify(list.contentHeight > list.height);
            combo.popup.close(); tryCompare(combo.popup, "visible", false);
            combo.currentIndex = 59;
            open(T.Popup.Item);
            tryVerify(function() { return list.itemAtIndex(59) !== null; });
            const last = list.itemAtIndex(59);
            const pos = last.mapToItem(list, 0, 0);
            verify(pos.y >= -0.5 && pos.y + last.height <= list.height + 0.5);
            verify(TestProbe.click(last, last.width/2, last.height/2));
            tryCompare(combo.popup, "visible", false);
            compare(combo.currentValue, "Option 59"); compare(activated.count, 1);
        }
        function test_editableNativeInput(): void {
            combo.editable = true;
            verify(TestProbe.render(combo));
            combo.forceActiveFocus(Qt.TabFocusReason);
            combo.selectAll();
            verify(TestProbe.typeAscii(combo.contentItem, "Weekly"));
            compare(combo.editText, "Weekly");
            verify(TestProbe.keyClick(combo.contentItem, Qt.Key_Return));
            compare(accepted.count, 1); compare(combo.currentValue, "Weekly");
            const reference = createTemporaryObject(nativeComboComponent, scene) as T.ComboBox;
            verify(reference !== null);
            compare(combo.editable, true);
            compare(TestProbe.accessibleState(combo).editable, TestProbe.accessibleState(reference).editable);
            compare(TestProbe.accessibleState(combo).role, TestProbe.accessibleState(reference).role);
            const editor = combo.contentItem as T.TextField;
            verify(editor !== null); compare(editor.enabled, true); compare(editor.readOnly, false);
            const accessibleEditor = TestProbe.accessibleState(editor);
            compare(accessibleEditor.role, 42); // QAccessible::EditableText
            compare(accessibleEditor.textInterface, true); compare(accessibleEditor.readOnly, false);
        }
        function test_disabledAndEmptyModel(): void {
            const before = combo.currentValue;
            combo.enabled = false;
            verify(TestProbe.click(combo, combo.width/2, combo.height/2));
            compare(combo.popup.visible, false); compare(combo.currentValue, before);
            compare(activated.count, 0);
            combo.enabled = true;
            combo.model = [];
            compare(combo.count, 0); compare(combo.currentIndex, -1);
            compare(combo.currentText, ""); compare(part("iaComboLabel").text, "");
            combo.model = ["Restored"];
            compare(combo.currentValue, "Restored"); compare(activated.count, 0);
        }
    }
}
