// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick

// Internal adapter for Qt's separate-window popup inheritance boundary.
// No visual child is added to the overlay or to either window.
QtObject {
    id: root
    required property Item source
    required property Item contentItem
    property Window _nativeWindow: null
    // A source may be QQuickView; QtQuick's Window type is narrower.
    readonly property var _sourceWindow: source ? source.Window.window : null
    property list<QtObject> _hostWindows: []

    function _rememberHost(): void {
        if (_sourceWindow && _hostWindows.indexOf(_sourceWindow) < 0)
            _hostWindows.push(_sourceWindow);
    }
    // Remember ownership independently of target notifications: during enter,
    // Qt can keep replacement content in an earlier application's Item host.
    on_SourceWindowChanged: _rememberHost()

    function _metadata(item: Item): var {
        const itemEnabled = item.LayoutMirroring.enabled;
        const itemInherit = item.LayoutMirroring.childrenInherit;
        const window = item.Window.window;
        if (window && item === window.contentItem) {
            // These aliases share values, but Qt notifies either attachment.
            // Read both before OR so neither dependency can short-circuit away.
            const windowEnabled = window.LayoutMirroring.enabled;
            const windowInherit = window.LayoutMirroring.childrenInherit;
            return {enabled: itemEnabled || windowEnabled, childrenInherit: itemInherit || windowInherit};
        }
        return {enabled: itemEnabled, childrenInherit: itemInherit};
    }

    readonly property bool _inheritedDirection: {
        let item = root.source;
        while (item) {
            const metadata = root._metadata(item);
            if (metadata.childrenInherit)
                return metadata.enabled;
            item = item.parent;
        }
        return false;
    }

    function _classifyWindow(): void {
        root._rememberHost();
        const target = root.contentItem ? root.contentItem.Window.window : null;
        root._nativeWindow = target && root._sourceWindow && root._hostWindows.indexOf(target) < 0 ? target : null;
    }

    // The source changes before the target during Item host migration. Only
    // classify on the target's window event, never on the source change alone.
    onContentItemChanged: _classifyWindow()
    Component.onCompleted: _classifyWindow()
    readonly property Connections _windowChanges: Connections {
        target: root.contentItem ? root.contentItem.Window : null
        function onWindowChanged(): void { root._classifyWindow(); }
    }
    readonly property Binding _enabled: Binding {
        target: root._nativeWindow ? root._nativeWindow.LayoutMirroring : null
        property: "enabled"
        value: root._inheritedDirection
        restoreMode: Binding.RestoreBindingOrValue
    }
    readonly property Binding _childrenInherit: Binding {
        target: root._nativeWindow ? root._nativeWindow.LayoutMirroring : null
        property: "childrenInherit"
        value: true
        restoreMode: Binding.RestoreBindingOrValue
    }
}
