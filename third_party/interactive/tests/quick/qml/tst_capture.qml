// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QtTest
import InterActive
import InterActive.Foundation as IA
import InterActive.Tests

Item {
    id: scene
    width: 1200
    height: 1000

    Component {
        id: stageComponent
        Rectangle {
            x: 24; y: 24
            width: 480; height: 400
            color: IA.Appearance.colors.canvas
            property Item target
            property alias entry: entry
            Item { id: entry; activeFocusOnTab: true; width: 1; height: 1 }
        }
    }
    Component { id: buttonComponent; IAButton { text: "Button" } }
    Component { id: toolComponent; IAToolButton { text: "Tool action" } }
    Component { id: checkComponent; IACheckBox { text: "Enable alerts" } }
    Component { id: radioComponent; IARadioButton { text: "All updates" } }
    Component { id: switchComponent; IASwitch { text: "Synchronize" } }
    Component { id: sliderComponent; IASlider { from: 0; to: 100; value: 50 } }
    Component { id: fieldComponent; IATextField { text: "Maya Chen"; placeholderText: "Display name" } }
    Component {
        id: tabsComponent
        IATabBar {
            width: 360
            property alias target: second
            IATabButton { text: "Overview" }
            IATabButton { id: second; text: "Details" }
        }
    }
    Component {
        id: segmentedComponent
        IASegmentedControl {
            width: 280
            property alias target: second
            Accessible.name: "Appearance"
            IASegmentedButton { text: "Light" }
            IASegmentedButton { id: second; text: "Dark" }
        }
    }
    Component {
        id: listComponent
        ListView {
            id: list
            width: 280; height: 144
            clip: true
            model: 3
            currentIndex: -1
            keyNavigationEnabled: false
            boundsBehavior: Flickable.StopAtBounds
            delegate: IAItemDelegate {
                required property int index
                width: list.width; height: 48
                text: ["First record", "Middle record", "Last record"][index]
                activeFocusOnTab: true
                highlighted: ListView.isCurrentItem
                onClicked: list.currentIndex = index
            }
        }
    }
    Component {
        id: menuComponent
        IAMenu {
            x: 24; y: 24
            width: 280
            popupType: T.Popup.Item
            focus: true
            property alias target: action
            IAMenuItem { id: action; text: "Show details"; checkable: true }
            IAMenuItem { text: "Another action" }
        }
    }
    Component {
        id: dialogComponent
        IADialog {
            id: dialog
            x: 24; y: 24
            width: 360
            title: "Scrollable preferences"
            popupType: T.Popup.Item
            focus: true
            property alias first: first
            property alias target: last
            property int rows: 8
            standardButtons: T.Dialog.Cancel
            Column {
                id: fields
                width: parent.width
                spacing: IA.Appearance.metrics.spacingS
                IATextField { id: first; width: parent.width; text: "First field" }
                Repeater {
                    model: dialog.rows
                    IATextField { width: fields.width; text: "Scrollable field" }
                }
                IATextField { id: last; width: parent.width; text: "Last field - keyboard focus" }
            }
        }
    }

    // The sheet contains frozen consumer pixels, not controls whose interaction
    // would change while the next cell is being driven.
    Component {
        id: sheetComponent
        Rectangle {
            property string heading
            property int columnCount: 2
            property alias grid: grid
            x: 16; y: 16
            width: grid.width + 32
            height: title.height + grid.height + 48
            color: IA.Appearance.colors.canvas
            Text {
                id: title
                x: 16; y: 12
                text: parent.heading
                color: IA.Appearance.colors.text
                font.pixelSize: 18
                font.bold: true
            }
            Grid {
                id: grid
                x: 16; y: title.y + title.height + 12
                columns: parent.columnCount
            }
        }
    }
    Component {
        id: labelComponent
        Text {
            width: 170; height: 72
            color: IA.Appearance.colors.text
            font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }
    Component {
        id: imageComponent
        Item {
            property string imageSource
            property real imageWidth
            property real imageHeight
            property alias status: image.status
            width: 240; height: 72
            Image {
                id: image
                anchors.centerIn: parent
                width: parent.imageWidth; height: parent.imageHeight
                source: parent.imageSource
                fillMode: Image.Stretch
                asynchronous: false
                cache: false
            }
        }
    }

    TestCase {
        name: "UiCapture"
        when: windowShown
        property string destination
        property string manifest: ""
        property int pngCount: 0

        function initTestCase(): void {
            failOnWarning(/.*(TypeError|ReferenceError|Binding loop|polish loop).*/);
            destination = TestProbe.captureDirectory();
            verify(destination.length > 0,
                   "IA_CAPTURE_DIR must name an absolute, existing, writable directory");
        }

        function cleanup(): void {
            TestProbe.move(scene, scene.width - 2, scene.height - 2);
            TestProbe.release(scene, scene.width - 2, scene.height - 2);
            IA.Appearance.input = {};
        }

        function init(): void {
            failOnWarning(/.*(TypeError|ReferenceError|Binding loop|polish loop).*/);
        }

        function cleanupTestCase(): void {
            verify(TestProbe.writeCaptureManifest(manifest), "Cannot write capture manifest");
            compare(pngCount, 60, "All 11 family sheets and four edge fixtures in all four modes");
        }

        function families(): var {
            const choiceStates = ["off", "off+hover", "on", "on+hover", "pressed", "focus", "disabled"];
            return [
                {name: "IAButton", kind: "button", component: buttonComponent,
                    states: ["rest", "hover", "pressed", "keyboard focus", "disabled"],
                    columns: ["Neutral", "Primary", "Caution", "Destructive"],
                    tones: [IA.Presentation.Neutral, IA.Presentation.Primary,
                            IA.Presentation.Caution, IA.Presentation.Destructive]},
                {name: "IAButton-checkable", kind: "checkable", component: buttonComponent,
                    states: ["rest", "hover", "checked", "checked+hover", "checked+pressed"], columns: ["Neutral"]},
                {name: "IAToolButton", kind: "tool", component: toolComponent,
                    states: ["rest", "hover", "pressed", "focus", "checked", "checked+hover"], columns: ["Text", "Icon only"]},
                {name: "IATabButton", kind: "tab", states: ["rest", "hover", "pressed", "selected", "selected+focus", "selected+hover"],
                    columns: ["In IATabBar", "IASegmentedControl"]},
                {name: "IAItemDelegate", kind: "delegate",
                    states: ["rest", "hover", "pressed", "selected", "selected+hover", "focus"], columns: ["In list"]},
                {name: "IAMenuItem", kind: "menu", states: ["rest", "highlighted", "checked", "disabled"],
                    columns: ["Default blue", "Yellow", "Near background", "Dark red"]},
                {name: "IACheckBox", kind: "choice", component: checkComponent, states: choiceStates, columns: ["Check"]},
                {name: "IARadioButton", kind: "choice", component: radioComponent, states: choiceStates, columns: ["Radio"]},
                {name: "IASwitch", kind: "choice", component: switchComponent, states: choiceStates, columns: ["Switch", "Trailing switch row"]},
                {name: "IASlider", kind: "slider", component: sliderComponent,
                    states: ["rest", "hover", "pressed", "focus"], columns: ["50%"]},
                {name: "IATextField", kind: "field", component: fieldComponent,
                    states: ["empty+placeholder", "filled", "hover", "focused", "invalid", "invalid+focused", "readOnly", "disabled"],
                    columns: ["Editor"]}
            ];
        }

        function tabInto(entry: Item, target: Item): void {
            entry.forceActiveFocus(Qt.OtherFocusReason);
            tryCompare(entry, "activeFocus", true);
            for (let i = 0; i < 24; ++i) {
                verify(TestProbe.keyClick(entry, Qt.Key_Tab));
                if (target.activeFocus)
                    break;
            }
            tryCompare(target, "activeFocus", true);
            if (target instanceof T.Control)
                tryCompare(target, "visualFocus", true);
        }

        function parkPointer(): void {
            verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
        }

        function activate(stage: Item): void {
            verify(TestProbe.render(stage));
            verify(TestProbe.activate(stage));
            tryCompare(TestProbe.hostWindow(stage), "active", true);
            parkPointer();
        }

        function snapshot(item: Item, margin: real): var {
            verify(TestProbe.render(item));
            // The probe crops the allocated width/height plus this margin, not
            // virtual text/ink bounds; display that same logical extent below.
            const source = TestProbe.captureDataUrl(item, margin);
            verify(source.length > 0, "Cannot capture rendered region (including focus margin)");
            return {source: source, width: item.width + 2 * margin, height: item.height + 2 * margin};
        }

        function captureCell(spec: var, state: string, column: int): var {
            const savedInput = IA.Appearance.input;
            if (spec.kind === "menu" && column > 0) {
                const accent = column === 1 ? "#F5D90A" : column === 2
                    ? (savedInput.scheme === "dark" ? "#303237" : "#F2F3F5") : "#7A1F1F";
                IA.Appearance.input = {scheme: savedInput.scheme, contrast: savedInput.contrast,
                    reducedMotion: true, tokens: {colors: {accent: accent}}};
            }
            const stage = createTemporaryObject(stageComponent, scene);
            verify(stage !== null);
            let popup = null;
            let host = null;
            let control = null;
            let held = false;
            try {
                if (spec.kind === "tab") {
                    host = createTemporaryObject(column === 1 ? segmentedComponent : tabsComponent, stage, {x: 24, y: 24});
                    control = host.target as T.AbstractButton;
                } else if (spec.kind === "delegate") {
                    host = createTemporaryObject(listComponent, stage, {x: 24, y: 24});
                    host.forceLayout();
                    control = host.itemAtIndex(1);
                } else if (spec.kind === "menu") {
                    popup = createTemporaryObject(menuComponent, stage, {parent: stage, width: 208});
                    control = popup.target;
                } else {
                    control = createTemporaryObject(spec.component, stage, {x: 24, y: 24, width: 208});
                }
                verify(control !== null);
                if (spec.name === "IASwitch" && column === 1)
                    control.indicatorPosition = IA.Presentation.Trailing;
                stage.target = control;
                if (spec.kind === "button")
                    control.tone = spec.tones[column];
                if (spec.kind === "checkable") {
                    control.tone = IA.Presentation.Neutral;
                    control.checkable = true;
                }
                if (spec.kind === "tool") {
                    control.checkable = true;
                    if (column === 1) {
                        control.display = T.AbstractButton.IconOnly;
                        control.icon.source = IA.Appearance.icons.check;
                        control.width = control.implicitWidth;
                    }
                }
                if (spec.kind === "field") {
                    if (state === "empty+placeholder")
                        control.text = "";
                    if (state.indexOf("invalid") >= 0)
                        control.validation = IA.Presentation.Invalid;
                    control.readOnly = state === "readOnly";
                }
                activate(stage);
                stage.entry.forceActiveFocus(Qt.OtherFocusReason);
                if (spec.kind === "delegate")
                    host.currentIndex = -1;
                if (popup !== null) {
                    popup.open();
                    tryCompare(popup, "opened", true);
                    compare(TestProbe.hostWindow(control), TestProbe.hostWindow(stage));
                }
                const selected = state.indexOf("checked") >= 0 || state.indexOf("selected") >= 0
                    || state === "on" || state === "on+hover";
                if (selected) {
                    verify(TestProbe.click(control, control.width / 2, control.height / 2));
                    if (spec.kind === "delegate")
                        compare(host.currentIndex, 1);
                    else
                        compare(control.checked, true);
                    parkPointer();
                    if (popup !== null) {
                        tryCompare(popup, "visible", false);
                        popup.open();
                        tryCompare(popup, "opened", true);
                    } else {
                        stage.entry.forceActiveFocus(Qt.OtherFocusReason);
                    }
                }
                if (state === "disabled")
                    control.enabled = false;
                if (state.indexOf("focus") >= 0)
                    tabInto(stage.entry, control);
                const hover = state.indexOf("hover") >= 0 || state.indexOf("pressed") >= 0 || state === "highlighted";
                if (hover) {
                    verify(TestProbe.move(control, control.width / 2, control.height / 2));
                    tryCompare(control, "hovered", true);
                    if (state === "highlighted")
                        tryCompare(control, "highlighted", true);
                } else if (control.enabled) {
                    tryCompare(control, "hovered", false);
                }
                if (state.indexOf("pressed") >= 0) {
                    verify(TestProbe.press(control, control.width / 2, control.height / 2));
                    held = true;
                    tryCompare(control, spec.kind === "slider" ? "pressed" : "down", true);
                }
                if (spec.kind === "menu")
                    tryCompare(control, "highlighted", state === "highlighted");
                return snapshot(spec.kind === "tab" && column === 1 ? host : control,
                    2 * IA.Appearance.metrics.focusWidth);
            } finally {
                if (held)
                    TestProbe.release(scene, scene.width - 2, scene.height - 2);
                if (popup !== null)
                    popup.close();
                stage.destroy();
                if (spec.kind === "menu")
                    IA.Appearance.input = savedInput;
            }
        }

        function addLabel(grid: Item, text: string, width: real): void {
            verify(createTemporaryObject(labelComponent, grid, {text: text, width: width}) !== null);
        }

        function addImage(grid: Item, capture: var, width: real, height: real): void {
            const image = createTemporaryObject(imageComponent, grid, {
                imageSource: capture.source, imageWidth: capture.width, imageHeight: capture.height,
                width: width, height: height
            });
            verify(image !== null);
            tryCompare(image, "status", Image.Ready);
        }

        function saveSheet(sheet: Item, path: string): void {
            verify(sheet.width <= scene.width - 32 && sheet.height <= scene.height - 32,
                   "Sheet must fit the exposed capture window");
            verify(TestProbe.render(sheet));
            let completed = false;
            let saved = false;
            verify(sheet.grabToImage(function(result) {
                saved = result.saveToFile(path);
                completed = true;
            }), "Cannot start asynchronous sheet grab");
            tryVerify(function() { return completed; }, 10000, "Sheet grab did not complete");
            verify(saved, "Cannot save PNG: " + path);
            ++pngCount;
        }

        function captureFamily(spec: var, scheme: string, contrast: string): void {
            console.info("Capturing", spec.name, scheme, contrast);
            const cellWidth = spec.columns.length === 4 ? 240 : 320;
            const captures = [];
            for (const state of spec.states) {
                for (let column = 0; column < spec.columns.length; ++column)
                    captures.push(captureCell(spec, state, column));
            }
            const sheet = createTemporaryObject(sheetComponent, scene, {
                heading: spec.name + " / " + scheme + " / " + contrast,
                columnCount: spec.columns.length + 1
            });
            verify(sheet !== null);
            try {
                addLabel(sheet.grid, "State", 170);
                for (const column of spec.columns)
                    addLabel(sheet.grid, column, cellWidth);
                let index = 0;
                for (const state of spec.states) {
                    addLabel(sheet.grid, state, 170);
                    for (const column of spec.columns)
                        addImage(sheet.grid, captures[index++], cellWidth, 72);
                }
                const relative = scheme + "-" + contrast + "/" + spec.name + ".png";
                saveSheet(sheet, destination + "/" + relative);
                manifest += relative + " | family=" + spec.name + " | scheme=" + scheme + " | contrast=" + contrast
                    + " | states=" + spec.states.join(",") + " | columns=" + spec.columns.join(",") + "\n";
            } finally {
                sheet.destroy();
            }
        }

        function captureEdge(kind: string, scheme: string, contrast: string): void {
            if (kind === "dialog-last-field") {
                captureDialogEdges(scheme, contrast);
                return;
            }
            console.info("Capturing edge", kind, scheme, contrast);
            const stage = createTemporaryObject(stageComponent, scene);
            verify(stage !== null);
            let capture = null;
            try {
                activate(stage);
                if (kind === "tab-bar") {
                    const bar = createTemporaryObject(tabsComponent, stage, {x: 24, y: 24});
                    verify(bar !== null);
                    verify(TestProbe.render(bar));
                    const first = bar.itemAt(0);
                    tabInto(stage.entry, first);
                    capture = snapshot(bar, 2 * IA.Appearance.metrics.focusWidth);
                } else {
                    const list = createTemporaryObject(listComponent, stage, {x: 24, y: 24});
                    verify(list !== null);
                    list.forceLayout();
                    const target = list.itemAtIndex(kind === "list-first" ? 0 : 2);
                    verify(target !== null);
                    tabInto(stage.entry, target);
                    capture = snapshot(list, 2 * IA.Appearance.metrics.focusWidth);
                }
            } finally {
                stage.destroy();
            }
            const sheet = createTemporaryObject(sheetComponent, scene, {
                heading: "Edge: " + kind + " / " + scheme + " / " + contrast,
                columnCount: 1
            });
            verify(sheet !== null);
            try {
                addImage(sheet.grid, capture, capture.width, capture.height);
                const relative = scheme + "-" + contrast + "/edge-" + kind + ".png";
                saveSheet(sheet, destination + "/" + relative);
                manifest += relative + " | family=edge-" + kind + " | scheme=" + scheme + " | contrast=" + contrast
                    + " | states=keyboard focus | columns=real clipped container\n";
            } finally {
                sheet.destroy();
            }
        }

        function captureDialogEdges(scheme: string, contrast: string): void {
            const captures = [];
            const specs = [
                {name: "Short / Cancel", rows: 0, state: "short", buttons: T.Dialog.Cancel},
                {name: "Long / top", rows: 8, state: "top", buttons: T.Dialog.Cancel},
                {name: "Long / middle", rows: 8, state: "middle", buttons: T.Dialog.Cancel},
                {name: "Long / bottom", rows: 8, state: "bottom", buttons: T.Dialog.Cancel},
                {name: "Reduced / middle", rows: 8, state: "middle", reduced: true, buttons: T.Dialog.Cancel},
                {name: "Window RTL / Close", rows: 8, state: "top", window: true, rtl: true, buttons: T.Dialog.Close},
                {name: "Window / Ok only", rows: 0, state: "short", window: true, buttons: T.Dialog.Ok},
                {name: "RTL / Apply only", rows: 0, state: "short", rtl: true, buttons: T.Dialog.Apply},
                {name: "Window RTL / short", rows: 0, state: "short", window: true, rtl: true, buttons: T.Dialog.Cancel}
            ];
            for (const spec of specs) {
                IA.Appearance.input = {scheme: scheme, contrast: contrast, reducedMotion: spec.reduced === true};
                const stage = createTemporaryObject(stageComponent, scene);
                verify(stage !== null);
                const properties = {parent: stage, width: 340, rows: spec.rows,
                    standardButtons: spec.buttons, popupType: spec.window ? T.Popup.Window : T.Popup.Item};
                if (spec.state !== "short")
                    properties.height = 260;
                const dialog = createTemporaryObject(dialogComponent, stage, properties);
                verify(dialog !== null);
                try {
                    activate(stage);
                    dialog.contentItem.parent.LayoutMirroring.enabled = spec.rtl === true;
                    dialog.contentItem.parent.LayoutMirroring.childrenInherit = true;
                    dialog.open();
                    tryCompare(dialog, "opened", true);
                    const view = findChild(dialog, "iaDialogViewport") as Flickable;
                    verify(view !== null);
                    tryVerify(function() { return TestProbe.exposed(view); });
                    verify(TestProbe.render(view));
                    if (spec.state === "middle")
                        view.contentY = view.originY + (view.contentHeight - view.height) / 2;
                    else if (spec.state === "bottom") {
                        tabInto(dialog.first, dialog.target);
                        view.contentY = view.originY + view.contentHeight - view.height;
                    }
                    verify(TestProbe.render(view));
                    captures.push(snapshot(dialog.contentItem.parent, 0));
                } finally {
                    dialog.close();
                    stage.destroy();
                }
            }
            const sheet = createTemporaryObject(sheetComponent, scene, {
                heading: "Dialog-owned body / " + scheme + " / " + contrast, columnCount: 3
            });
            verify(sheet !== null);
            try {
                for (let row = 0; row < 3; ++row) {
                    for (let column = 0; column < 3; ++column)
                        verify(createTemporaryObject(labelComponent, sheet.grid, {
                            text: specs[row * 3 + column].name, width: 350, height: 24}) !== null);
                    for (let column = 0; column < 3; ++column)
                        addImage(sheet.grid, captures[row * 3 + column], 350, 260);
                }
                saveSheet(sheet, destination + "/" + scheme + "-" + contrast + "/edge-dialog-last-field.png");
                manifest += scheme + "-" + contrast + "/edge-dialog-last-field.png | family=dialog | states=short,top,middle,bottom,reduced,Close,Ok,Apply | hosting=item,window | direction=LTR,RTL\n";
            } finally {
                sheet.destroy();
                IA.Appearance.input = {scheme: scheme, contrast: contrast, reducedMotion: true};
            }
        }

        function test_capture(): void {
            for (const scheme of ["light", "dark"]) {
                for (const contrast of ["normal", "high"]) {
                    IA.Appearance.input = {scheme: scheme, contrast: contrast, reducedMotion: true};
                    verify(TestProbe.createCaptureMode(scheme + "-" + contrast), "Cannot create capture mode directory");
                    for (const spec of families())
                        captureFamily(spec, scheme, contrast);
                    for (const edge of ["dialog-last-field", "list-first", "list-last", "tab-bar"])
                        captureEdge(edge, scheme, contrast);
                }
            }
        }
    }
}
