// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import InterActive

Item {
    id: fixture
    width: 600
    height: 100
    property string requestedName
    property string requestedSource: "icons/consumer-mark.svg"
    property color tint: "transparent"
    readonly property list<Item> controls: [button, tool, tab, delegate, menuItem]
    Row {
        spacing: 12
        IAButton {
            id: button
            width: 100
            height: 60
            icon.name: fixture.requestedName
            icon.source: fixture.requestedSource
            icon.color: fixture.tint
        }
        IAToolButton {
            id: tool
            width: 100
            height: 60
            icon.name: fixture.requestedName
            icon.source: fixture.requestedSource
            icon.color: fixture.tint
        }
        IATabButton {
            id: tab
            width: 100
            height: 60
            icon.name: fixture.requestedName
            icon.source: fixture.requestedSource
            icon.color: fixture.tint
        }
        IAItemDelegate {
            id: delegate
            width: 100
            height: 60
            icon.name: fixture.requestedName
            icon.source: fixture.requestedSource
            icon.color: fixture.tint
        }
        IAMenuItem {
            id: menuItem
            width: 100
            height: 60
            icon.name: fixture.requestedName
            icon.source: fixture.requestedSource
            icon.color: fixture.tint
        }
    }
}
