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

    component DirectionHost: Window {
        id: host
        property bool rtl: false
        property bool inheritDirection: true
        property bool overlayOverride: false
        property bool overlayDirection: false
        property bool overlayInheritance: false
        property T.Popup currentPopup: null
        property var activation: null
        property int activations: 0
        property alias anchor: anchor
        property alias opener: opener
        visible: false
        width: 800
        height: 640
        flags: Qt.Window
        transientParent: null
        LayoutMirroring.enabled: host.rtl
        LayoutMirroring.childrenInherit: host.inheritDirection
        Item {
            id: anchor
            anchors.fill: parent
            IAButton {
                id: opener
                x: 24
                y: 24
                text: "Open preferences"
                onClicked: {
                    ++host.activations;
                    if (host.activation)
                        host.activation();
                    else if (host.currentPopup)
                        host.currentPopup.open();
                }
            }
        }
        Binding {
            target: anchor.T.Overlay.overlay.LayoutMirroring
            property: "enabled"
            when: host.overlayOverride
            value: host.overlayDirection
            restoreMode: Binding.RestoreBindingOrValue
        }
        Binding {
            target: anchor.T.Overlay.overlay.LayoutMirroring
            property: "childrenInherit"
            when: host.overlayOverride
            value: host.overlayInheritance
            restoreMode: Binding.RestoreBindingOrValue
        }
    }
    DirectionHost { id: a }
    DirectionHost { id: b }
    component AliasHost: Window {
        id: aliasHost
        visible: false
        width: 800; height: 640; flags: Qt.Window; transientParent: null
        property T.Popup currentPopup: null
        property alias anchor: aliasAnchor
        property alias opener: aliasOpener
        Item {
            id: aliasAnchor
            anchors.fill: parent
            IAButton {
                id: aliasOpener
                x: 24; y: 24; text: "Open alias consumer"
                onClicked: aliasHost.currentPopup.open()
            }
        }
    }
    // TestProbe deliberately gives observed Windows C++ ownership. Declarative
    // fixtures live with the scene, not QtTest's temporary-object destroy list.
    readonly property list<Window> aliasHosts: [
        AliasHost {}, AliasHost {}, AliasHost {}, AliasHost {},
        AliasHost {}, AliasHost {}, AliasHost {}, AliasHost {}
    ]
    Component { id: aliasSpyComponent; SignalSpy {} }
    Component {
        id: popupComponent
        IAPopup {
            property bool openOnCompletion: false
            property var lifetimeObserver: null
            Component.onCompleted: if (openOnCompletion) open()
            Component.onDestruction: if (lifetimeObserver) ++lifetimeObserver.destructions
            width: 280; height: 100; x: 24; y: 80; focus: true
            contentItem: IASwitch { objectName: "retainedChoice"; text: "Keep draft"; checked: true }
        }
    }
    Component {
        id: dialogComponent
        IADialog {
            property bool openOnCompletion: false
            property var lifetimeObserver: null
            Component.onCompleted: if (openOnCompletion) open()
            Component.onDestruction: if (lifetimeObserver) ++lifetimeObserver.destructions
            width: 320; x: 24; y: 80; title: "Preferences"; focus: true
            standardButtons: T.Dialog.Close
            IATextField { objectName: "retainedDraft"; width: parent.width; text: "Retained text" }
        }
    }
    Component {
        id: menuComponent
        IAMenu {
            property bool openOnCompletion: false
            property var lifetimeObserver: null
            Component.onCompleted: if (openOnCompletion) open()
            Component.onDestruction: if (lifetimeObserver) ++lifetimeObserver.destructions
            width: 280; x: 24; y: 80; focus: true
            IAMenuItem { objectName: "retainedAction"; text: "Keep checked"; checkable: true; checked: true }
            IAMenu {
                objectName: "nestedMenu"; title: "Nested"; popupType: T.Popup.Window
                IAMenuItem { objectName: "nestedAction"; text: "Complete" }
            }
        }
    }
    Component {
        id: tooltipComponent
        IAToolTip {
            property bool openOnCompletion: false
            property var lifetimeObserver: null
            Component.onCompleted: if (openOnCompletion) open()
            Component.onDestruction: if (lifetimeObserver) ++lifetimeObserver.destructions
            width: 280; x: 24; y: 80; text: "Retained tooltip"; delay: 0; timeout: -1
        }
    }
    Component {
        id: customDialogComponent
        IADialog {
            property bool openOnCompletion: false
            Component.onCompleted: if (openOnCompletion) open()
            width: 280; height: 160; x: 24; y: 80
            contentItem: IASwitch { text: "Custom dialog content"; checked: true }
        }
    }
    Component {
        id: customMenuComponent
        IAMenu {
            property bool openOnCompletion: false
            Component.onCompleted: if (openOnCompletion) open()
            width: 280; height: 100; x: 24; y: 80
            contentItem: IASwitch { text: "Custom menu content"; checked: true }
        }
    }
    Component {
        id: customTooltipComponent
        IAToolTip {
            property bool openOnCompletion: false
            Component.onCompleted: if (openOnCompletion) open()
            width: 280; height: 100; x: 24; y: 80; delay: 0; timeout: -1
            contentItem: IASwitch { text: "Custom tooltip content"; checked: true }
        }
    }
    Component {
        id: defaultPopupComponent
        IAPopup {
            property bool openOnCompletion: false
            Component.onCompleted: if (openOnCompletion) open()
            width: 280; height: 100; x: 24; y: 80
        }
    }
    Component {
        id: replacementComponent
        IASwitch { objectName: "replacementChoice"; implicitWidth: 240; text: "Replacement"; checked: true }
    }
    Component {
        id: transitioningComponent
        IAPopup {
            width: 280; height: 100; x: 24; y: 80
            enter: Transition {
                NumberAnimation { id: animation; property: "opacity"; from: 0; to: 1; duration: 500 }
            }
            contentItem: IASwitch { text: "Entering" }
        }
    }

    SignalSpy { id: ae; target: a.LayoutMirroring; signalName: "enabledChanged" }
    SignalSpy { id: ai; target: a.LayoutMirroring; signalName: "childrenInheritChanged" }
    SignalSpy { id: be; target: b.LayoutMirroring; signalName: "enabledChanged" }
    SignalSpy { id: bi; target: b.LayoutMirroring; signalName: "childrenInheritChanged" }
    // Window and contentItem attachments share Qt state. Qt sends enabledChanged
    // through the latest attachment (qquickitem.cpp1491-1499,1580-1593), so watch
    // both public aliases rather than missing writes after source-chain reads.
    SignalSpy { id: ace; target: a.contentItem.LayoutMirroring; signalName: "enabledChanged" }
    SignalSpy { id: bce; target: b.contentItem.LayoutMirroring; signalName: "enabledChanged" }

    TestCase {
        id: test
        name: "PopupDirection"
        when: windowShown
        property list<T.Popup> popups: []
        property int aEnabled: 0
        property int aInherit: 0
        property int bEnabled: 0
        property int bInherit: 0
        property T.Popup observedPopup: null
        property Item observedContent: null
        property Window observedWindow: null
        property var firstNativeFrame: null
        property int destructions: 0
        property Window retainedNativeWindow: null

        Connections {
            target: test.observedPopup
            function onContentItemChanged(): void {
                test.observedContent = test.observedPopup ? test.observedPopup.contentItem : null;
                test.observeWindow();
            }
        }
        Connections {
            target: test.observedContent ? test.observedContent.Window : null
            function onWindowChanged(): void { test.observeWindow(); }
        }
        Connections {
            target: test.observedWindow
            function onFrameSwapped(): void {
                if (test.firstNativeFrame === null && test.observedPopup && test.observedPopup.visible
                    && test.observedWindow !== a && test.observedWindow !== b) {
                    test.firstNativeFrame = {mirrored: test.observedPopup.mirrored,
                        enabled: test.observedWindow.LayoutMirroring.enabled,
                        inherit: test.observedWindow.LayoutMirroring.childrenInherit};
                    console.info("IA_POPUP_DIRECTION_FIRST_FRAME " + JSON.stringify(test.firstNativeFrame));
                }
            }
        }

        function initTestCase(): void {
            failOnWarning(/.*(TypeError|ReferenceError|Binding loop|polish loop|Cannot assign|Unable to assign).*/);
        }
        function init(): void {
            failOnWarning(/.*(TypeError|ReferenceError|Binding loop|polish loop|Cannot assign|Unable to assign).*/);
            IA.Appearance.input = {};
            a.overlayOverride = false;
            b.overlayOverride = false;
            a.rtl = true;
            b.rtl = false;
            a.inheritDirection = true;
            b.inheritDirection = true;
            a.currentPopup = null;
            b.currentPopup = null;
            a.activation = null;
            b.activation = null;
            a.show();
            tryVerify(function() { return TestProbe.exposed(a.anchor); });
            verify(TestProbe.render(a.anchor));
            verify(TestProbe.activateForInput(a.opener));
            ae.clear(); ai.clear(); be.clear(); bi.clear(); ace.clear(); bce.clear();
            aEnabled = 0; aInherit = 0; bEnabled = 0; bInherit = 0;
            popups = [];
            observedPopup = null;
            observedContent = null;
            observedWindow = null;
            firstNativeFrame = null;
            checkIdle();
        }
        function cleanup(): void {
            try {
                for (const popup of popups) {
                    if (popup) {
                        popup.close();
                        tryCompare(popup, "visible", false);
                    }
                }
                observedPopup = null;
                observedContent = null;
                observedWindow = null;
                checkHosts();
                // Retire the migration host before idle input on A. requestActivate
                // is not a compositor keyboard-ownership override on Wayland.
                b.close();
                checkIdle();
            } finally {
                // Assertion failure must not leak mapped hosts into the next case.
                a.close();
                b.close();
            }
        }
        function checkHosts(): void {
            compare(a.LayoutMirroring.enabled, a.rtl);
            compare(a.LayoutMirroring.childrenInherit, a.inheritDirection);
            compare(b.LayoutMirroring.enabled, b.rtl);
            compare(b.LayoutMirroring.childrenInherit, b.inheritDirection);
            compare(ae.count + ace.count, aEnabled, "Host A has no unexpected enabled write, including transient writes");
            compare(ai.count, aInherit, "Host A has no unexpected childrenInherit write");
            compare(be.count + bce.count, bEnabled, "Host B has no unexpected enabled write, including transient writes");
            compare(bi.count, bInherit, "Host B has no unexpected childrenInherit write");
        }
        function setDirection(host: DirectionHost, rtl: bool): void {
            if (host.rtl !== rtl) {
                if (host === a) ++aEnabled; else ++bEnabled;
            }
            host.rtl = rtl;
            checkHosts();
        }
        function setInheritance(host: DirectionHost, value: bool): void {
            if (host.inheritDirection !== value) {
                if (host === a) ++aInherit; else ++bInherit;
            }
            host.inheritDirection = value;
            checkHosts();
        }
        function checkIdle(): void {
            tryCompare(a.anchor.T.Overlay.overlay, "visible", false);
            tryCompare(b.anchor.T.Overlay.overlay, "visible", false);
            const count = a.activations;
            a.currentPopup = null;
            a.activation = null;
            console.info("IA_DIRECTION_HOST_INPUT " + JSON.stringify({aActive: a.active, bActive: b.active,
                aExposed: TestProbe.exposed(a.opener), bExposed: TestProbe.exposed(b.opener),
                aOwnsOpener: TestProbe.hostWindow(a.opener) === a, aReady: TestProbe.inputHostReady(a.opener),
                bReady: TestProbe.inputHostReady(b.opener), aTransientB: a.transientParent === b,
                bTransientA: b.transientParent === a, aTransientScene: a.transientParent === scene.Window.window,
                bTransientScene: b.transientParent === scene.Window.window}));
            verify(TestProbe.activateForInput(a.opener));
            verify(TestProbe.click(a.opener, a.opener.width / 2, a.opener.height / 2));
            compare(a.activations, count + 1, "Idle overlay does not intercept the actual background button");
            checkHosts();
        }
        function make(kind: string, mode: int, owner: DirectionHost): T.Popup {
            const component = kind === "dialog" ? dialogComponent : kind === "menu" ? menuComponent
                : kind === "tooltip" ? tooltipComponent : popupComponent;
            const popup = createTemporaryObject(component, scene, {parent: owner.anchor, popupType: mode}) as T.Popup;
            verify(popup !== null);
            popups.push(popup);
            return popup;
        }
        function observeWindow(): void {
            observedWindow = observedContent ? observedContent.Window.window : null;
        }
        function watch(popup: T.Popup): void {
            observedPopup = popup;
            observedContent = popup.contentItem;
            firstNativeFrame = null;
            observeWindow();
        }
        function open(popup: T.Popup, owner: DirectionHost): void {
            owner.currentPopup = popup;
            watch(popup);
            activateAndDispatch(owner.opener, popup.popupType === T.Popup.Window);
        }
        function activateAndDispatch(opener: IAButton, nativeWindow: bool): void {
            verify(TestProbe.activateForInput(opener));
            opener.forceActiveFocus(Qt.TabFocusReason);
            tryCompare(opener, "activeFocus", true);
            if (nativeWindow && TestProbe.isWayland()) {
                tryVerify(function() { return TestProbe.nativeKeyHostReady(opener); });
                verify(TestProbe.nativeKeyClick(opener, Qt.Key_Space));
            } else {
                verify(TestProbe.keyClick(opener, Qt.Key_Space));
            }
        }
        function ready(popup: T.Popup, owner: Window, rtl: bool): void {
            tryCompare(popup, "opened", true);
            tryVerify(function() { return TestProbe.exposed(popup.contentItem); });
            verify(TestProbe.render(popup.contentItem));
            const host = TestProbe.hostWindow(popup.contentItem);
            compare(host !== owner, popup.popupType === T.Popup.Window, "Actual Item/native host, no fallback");
            compare(popup.mirrored, rtl);
            compare(popup.contentItem.LayoutMirroring.enabled, rtl);
            if (popup.popupType === T.Popup.Window) {
                tryVerify(function() { return firstNativeFrame !== null; });
                compare(firstNativeFrame.mirrored, rtl, "First observed native frame already has the inherited direction");
                compare(firstNativeFrame.enabled, rtl);
                compare(firstNativeFrame.inherit, true);
            }
            checkHosts();
        }
        function close(popup: T.Popup): void {
            popup.close();
            tryCompare(popup, "visible", false);
            checkHosts();
        }
        function test_parityLifecycle_data(): var {
            const rows = [];
            for (const kind of ["dialog", "popup", "menu", "tooltip"])
                for (const mode of [T.Popup.Item, T.Popup.Window])
                    rows.push({tag: kind + "-" + mode, kind: kind, mode: mode});
            return rows;
        }
        function test_parityLifecycle(data: var): void {
            const popup = make(data.kind, data.mode, a);
            const content = popup.contentItem;
            open(popup, a);
            ready(popup, a, true);
            setDirection(a, false);
            verify(TestProbe.render(content));
            compare(popup.mirrored, false);
            compare(popup.contentItem, content);
            setDirection(a, true);
            verify(TestProbe.render(content));
            compare(popup.mirrored, true);
            close(popup);
            checkIdle();
            open(popup, a);
            ready(popup, a, true);
            close(popup);
            // The original host binding must still evaluate after popup cycles.
            setDirection(a, false);
            setDirection(a, true);
        }
        function test_publicOverlayFlags_data(): var {
            const rows = [];
            for (const mode of [T.Popup.Item, T.Popup.Window])
                for (const edge of [
                    {name: "enabled-not-inherited", root: false, inherit: true, enabled: true, children: false, expected: false},
                    {name: "ancestor-inherited", root: true, inherit: true, enabled: false, children: false, expected: true},
                    {name: "explicit-ltr-branch", root: true, inherit: true, enabled: false, children: true, expected: false},
                    {name: "explicit-rtl-branch", root: false, inherit: false, enabled: true, children: true, expected: true},
                    {name: "root-does-not-inherit", root: true, inherit: false, enabled: true, children: false, expected: false}
                ]) rows.push({tag: mode + "-" + edge.name, mode: mode, edge: edge});
            return rows;
        }
        function test_publicOverlayFlags(data: var): void {
            setDirection(a, data.edge.root);
            setInheritance(a, data.edge.inherit);
            a.overlayDirection = data.edge.enabled;
            a.overlayInheritance = data.edge.children;
            a.overlayOverride = true;
            const popup = make("popup", data.mode, a);
            open(popup, a);
            ready(popup, a, data.edge.expected);
            close(popup);
        }
        function test_contentTypeModalAndDestroy_data(): var { return test_parityLifecycle_data(); }
        function test_contentTypeModalAndDestroy(data: var): void {
            const popup = make(data.kind, data.mode, a);
            open(popup, a);
            ready(popup, a, true);
            const replacementParent = popup.contentItem.parent;
            const replacement = createTemporaryObject(replacementComponent, replacementParent,
                {parent: replacementParent}) as IASwitch;
            verify(replacement !== null);
            popup.contentItem = replacement;
            tryVerify(function() { return TestProbe.exposed(replacement); });
            verify(TestProbe.render(replacement));
            compare(replacement.mirrored, true);
            compare(replacement.checked, true);
            checkHosts();
            popup.modal = true;
            verify(TestProbe.render(replacement));
            compare(popup.mirrored, true);
            checkHosts();
            popup.modal = false;
            verify(TestProbe.render(replacement));
            compare(popup.mirrored, true);
            checkHosts();
            close(popup);
            popup.popupType = data.mode === T.Popup.Item ? T.Popup.Window : T.Popup.Item;
            open(popup, a);
            ready(popup, a, true);
            close(popup);
            popup.lifetimeObserver = test;
            const before = destructions;
            retainedNativeWindow = popup.popupType === T.Popup.Window
                ? TestProbe.hostWindow(popup.contentItem) : null;
            const hadNativeWindow = retainedNativeWindow !== null;
            popup.destroy();
            tryCompare(test, "destructions", before + 1, 5000, "Actual Component destruction reached the surviving observer");
            if (hadNativeWindow)
                tryCompare(test, "retainedNativeWindow", null, 5000, "The actual native Window was destroyed with its popup");
            checkIdle();
        }
        function test_construction_data(): var {
            const rows = [];
            for (const kind of ["dialog", "popup", "menu", "tooltip"])
                for (const mode of [T.Popup.Item, T.Popup.Window])
                    for (const custom of [false, true])
                        for (const creation of ["ordinary", "initial", "completed"])
                            rows.push({tag: kind + mode + custom + creation,
                                kind: kind, mode: mode, custom: custom, creation: creation});
            return rows;
        }
        function test_construction(data: var): void {
            const component = data.kind === "dialog" ? (data.custom ? customDialogComponent : dialogComponent)
                : data.kind === "menu" ? (data.custom ? customMenuComponent : menuComponent)
                : data.kind === "tooltip" ? (data.custom ? customTooltipComponent : tooltipComponent)
                : (data.custom ? popupComponent : defaultPopupComponent);
            let popup = null;
            // Construct in the real opener's input handler. Initial visibility and
            // completed-handler open retain the same real native input provenance;
            // neither path delays or intercepts the popup's own opening.
            a.activation = function() {
                popup = createTemporaryObject(component, scene, {parent: a.anchor,
                    popupType: data.mode, visible: data.creation === "initial",
                    openOnCompletion: data.creation === "completed"});
                verify(popup !== null);
                popups.push(popup);
                watch(popup);
                if (data.creation === "ordinary") popup.open();
            };
            activateAndDispatch(a.opener, data.mode === T.Popup.Window);
            a.activation = null;
            verify(popup !== null);
            ready(popup, a, true);
            setDirection(a, false);
            verify(TestProbe.render(popup.contentItem));
            compare(popup.mirrored, false);
            close(popup);
        }
        function test_hostMigration_data(): var {
            return [{tag: "item", mode: T.Popup.Item}, {tag: "native", mode: T.Popup.Window}];
        }
        function test_hostMigration(data: var): void {
            prepareMigrationHosts();
            const popup = make("popup", data.mode, a);
            open(popup, a);
            ready(popup, a, true);
            const oldNative = TestProbe.hostWindow(popup.contentItem);
            popup.parent = b.anchor;
            compare(popup.visible, true, "Native migration remains open, not close-and-reopen");
            tryVerify(function() { return TestProbe.exposed(popup.contentItem); });
            verify(TestProbe.render(popup.contentItem));
            compare(popup.mirrored, false);
            compare(TestProbe.hostWindow(popup.contentItem), data.mode === T.Popup.Item ? b : oldNative);
            checkHosts();
            popup.parent = a.anchor;
            compare(popup.visible, true);
            tryVerify(function() { return TestProbe.exposed(popup.contentItem); });
            verify(TestProbe.render(popup.contentItem));
            compare(popup.mirrored, true);
            checkHosts();
            close(popup);
            setDirection(a, false);
            setDirection(b, true);
        }
        function test_itemMigrationDuringEnter(): void {
            prepareMigrationHosts();
            const popup = createTemporaryObject(transitioningComponent, scene, {parent: a.anchor}) as IAPopup;
            verify(popup !== null);
            popups.push(popup);
            open(popup, a);
            verify(popup.visible && !popup.opened, "The actual enter transition is still running");
            verify(!popup.opened, "Migration still exercises the enter transition");
            compare(TestProbe.hostWindow(popup.contentItem), a);
            popup.parent = b.anchor;
            compare(TestProbe.hostWindow(popup.contentItem), a, "Qt retains the old Item target during enter");
            const replacement = createTemporaryObject(replacementComponent, null) as IASwitch;
            verify(replacement !== null);
            popup.contentItem = replacement;
            compare(TestProbe.hostWindow(replacement), a, "Replacement still uses Qt's retained Item host");
            checkHosts();
            tryCompare(popup, "opened", true);
            checkHosts();
            close(popup);
            // The old top-level still owns the compositor keyboard focus.
            // Retire it before dispatching the reopen through the new host.
            a.close();
            tryVerify(function() { return TestProbe.exposed(b.anchor); });
            open(popup, b);
            ready(popup, b, false);
            close(popup);
            a.show();
            tryVerify(function() { return TestProbe.exposed(a.anchor); });
        }
        function test_nestedNativeMenu(): void {
            const popup = make("menu", T.Popup.Window, a) as IAMenu;
            open(popup, a);
            ready(popup, a, true);
            const nested = findChild(popup, "nestedMenu") as IAMenu;
            const entry = popup.itemAt(1);
            verify(nested !== null && entry !== null);
            verify(TestProbe.click(entry, entry.width / 2, entry.height / 2));
            tryCompare(nested, "opened", true);
            tryVerify(function() { return TestProbe.exposed(nested.contentItem); });
            verify(TestProbe.render(nested.contentItem));
            verify(TestProbe.hostWindow(nested.contentItem) !== TestProbe.hostWindow(popup.contentItem));
            compare(nested.mirrored, true);
            setDirection(a, false);
            verify(TestProbe.render(nested.contentItem));
            compare(popup.mirrored, false);
            compare(nested.mirrored, false);
            checkHosts();
            nested.close();
            tryCompare(nested, "visible", false);
            close(popup);
        }
        function prepareOtherHost(): void {
            b.show();
            tryVerify(function() { return TestProbe.exposed(b.anchor); });
            verify(TestProbe.render(b.anchor));
            verify(TestProbe.activateForInput(b.opener));
            checkHosts();
        }
        function prepareMigrationHosts(): void {
            // Map both hosts before opening a native popup. Mapping a competing
            // toplevel during its grab causes the compositor to dismiss it.
            a.close();
            b.show();
            tryVerify(function() { return TestProbe.exposed(b.anchor); });
            verify(TestProbe.render(b.anchor));
            a.show();
            tryVerify(function() { return TestProbe.exposed(a.anchor); });
            verify(TestProbe.render(a.anchor));
            verify(TestProbe.activateForInput(a.opener));
            checkHosts();
        }
        function test_aliasNotificationOwners_data(): var {
            const rows = [];
            for (const order of ["item-first", "window-first"])
                for (const first of ["item", "window"])
                    for (const kind of ["popup", "menu"])
                        rows.push({tag: order + first + kind, order: order, first: first, kind: kind, index: rows.length});
            return rows;
        }
        function test_aliasNotificationOwners(data: var): void {
            const host = scene.aliasHosts[data.index] as AliasHost;
            verify(host !== null);
            let itemFlags = null;
            let windowFlags = null;
            if (data.order === "item-first") {
                itemFlags = host.contentItem.LayoutMirroring;
                compare(itemFlags.enabled, false);
                compare(itemFlags.childrenInherit, false);
                windowFlags = host.LayoutMirroring;
                compare(windowFlags.enabled, false);
                compare(windowFlags.childrenInherit, false);
            } else {
                windowFlags = host.LayoutMirroring;
                compare(windowFlags.enabled, false);
                compare(windowFlags.childrenInherit, false);
                itemFlags = host.contentItem.LayoutMirroring;
                compare(itemFlags.enabled, false);
                compare(itemFlags.childrenInherit, false);
            }
            const spies = [];
            for (const flags of [itemFlags, windowFlags])
                for (const signal of ["enabledChanged", "childrenInheritChanged"])
                    spies.push(createTemporaryObject(aliasSpyComponent, test, {target: flags, signalName: signal}));
            windowFlags.enabled = true;
            windowFlags.childrenInherit = true;
            for (const spy of spies) spy.clear();
            let enabledChanges = 0;
            let inheritanceChanges = 0;
            let itemInheritanceChanges = 0;
            let windowInheritanceChanges = 0;
            const component = data.kind === "menu" ? menuComponent : popupComponent;
            const popup = createTemporaryObject(component, scene, {parent: host.anchor, popupType: T.Popup.Window}) as T.Popup;
            verify(popup !== null);
            popups.push(popup);
            let nested = null;
            try {
                host.show();
                tryVerify(function() { return TestProbe.exposed(host.anchor); });
                verify(TestProbe.render(host.anchor));
                host.currentPopup = popup;
                watch(popup);
                activateAndDispatch(host.opener, true);
                ready(popup, host, true);
                if (data.kind === "menu") {
                    nested = findChild(popup, "nestedMenu") as IAMenu;
                    verify(nested !== null);
                    watch(nested);
                    const entry = (popup as IAMenu).itemAt(1);
                    verify(TestProbe.click(entry, entry.width / 2, entry.height / 2));
                    ready(nested, host, true);
                }
                for (const setter of data.first === "item" ? [itemFlags, windowFlags] : [windowFlags, itemFlags]) {
                    for (const step of [
                        {property: "enabled", value: false, expected: false},
                        {property: "enabled", value: true, expected: true},
                        {property: "childrenInherit", value: false, expected: false},
                        {property: "childrenInherit", value: true, expected: true}
                    ]) {
                        setter[step.property] = step.value;
                        if (step.property === "enabled") {
                            ++enabledChanges;
                        } else {
                            ++inheritanceChanges;
                            if (setter === itemFlags) ++itemInheritanceChanges; else ++windowInheritanceChanges;
                        }
                        verify(TestProbe.render(popup.contentItem));
                        compare(popup.mirrored, step.expected);
                        compare(popup.contentItem.LayoutMirroring.enabled, step.expected);
                        if (nested !== null) {
                            verify(TestProbe.render(nested.contentItem));
                            compare(nested.mirrored, step.expected);
                            compare(nested.contentItem.LayoutMirroring.enabled, step.expected);
                        }
                        compare(spies[0].count + spies[2].count, enabledChanges, "No extra enabled writes through either alias");
                        compare(spies[1].count + spies[3].count, inheritanceChanges, "No extra inheritance writes through either alias");
                        compare(spies[data.order === "item-first" ? 2 : 0].count, enabledChanges,
                            "The last-created attachment owns enabled notifications");
                        compare(spies[1].count, itemInheritanceChanges, "Item setter owns its inheritance notifications");
                        compare(spies[3].count, windowInheritanceChanges, "Window setter owns its inheritance notifications");
                        compare(itemFlags.enabled, windowFlags.enabled);
                        compare(itemFlags.childrenInherit, windowFlags.childrenInherit);
                        checkHosts();
                    }
                }
            } finally {
                if (nested !== null) nested.close();
                popup.close();
                host.close();
                observedPopup = null;
                observedContent = null;
                observedWindow = null;
            }
        }
    }
}
