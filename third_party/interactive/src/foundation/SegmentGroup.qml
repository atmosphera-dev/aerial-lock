// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick

ListView {
    id: root
    required property ControlAppearance resolvedAppearance
    property bool mirrored: false
    LayoutMirroring.enabled: mirrored
    orientation: ListView.Horizontal
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.AutoFlickIfNeeded
    snapMode: ListView.SnapToItem
    highlightRangeMode: ListView.ApplyRange
    highlightMoveDuration: 0
    preferredHighlightBegin: 0
    preferredHighlightEnd: width
    clip: true

    // The native template owns the returned Item in its background slot.
    // Only construction/replacement calls this; live appearance stays bound.
    function createBackground(parentItem: Item): Item {
        const background = trackComponent.createObject(parentItem) as Item;
        if (!background)
            throw new Error(trackComponent.errorString());
        return background;
    }

    Component {
        id: trackComponent
        ControlBackground {
            resolvedAppearance: root.resolvedAppearance
            outlined: false
        }
    }
}
