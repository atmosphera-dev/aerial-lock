// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.MenuItem {
    id: control
    property int tone: IA.Presentation.Auto
    // The action owns shortcut activation; this is its optional human-readable label.
    property string shortcutText
    property alias backgroundColor: appearance.backgroundColor
    property alias foregroundColor: appearance.foregroundColor
    property alias hoverBackgroundColor: appearance.hoverBackgroundColor
    property alias pressedBackgroundColor: appearance.pressedBackgroundColor
    property alias disabledBackgroundColor: appearance.disabledBackgroundColor
    property alias disabledForegroundColor: appearance.disabledForegroundColor
    property alias inactiveBackgroundColor: appearance.inactiveBackgroundColor
    property alias inactiveForegroundColor: appearance.inactiveForegroundColor
    property alias selectedBackgroundColor: appearance.selectedBackgroundColor
    property alias selectedForegroundColor: appearance.selectedForegroundColor
    property alias borderColor: appearance.borderColor
    property alias disabledBorderColor: appearance.disabledBorderColor
    property alias focusColor: appearance.focusColor
    property alias controlRadius: appearance.controlRadius
    property alias fontFamily: appearance.fontFamily
    property alias fontPixelSize: appearance.fontPixelSize
    property alias fontWeight: appearance.fontWeight
    property alias minimumHeight: appearance.minimumHeight
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    readonly property bool hasIcon: icon.name.length > 0 || icon.source.toString().length > 0
    readonly property bool showsIcon: hasIcon && display !== T.AbstractButton.TextOnly
    readonly property real leadingPadding: Math.max(implicitTextPadding, textPadding)
    function resetAppearance(): void { appearance.resetOverrides(); }
    IA.ControlAppearance { id: appearance; source: IA.Appearance; family: "menuItem"; tone: control.tone }
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: Math.max(implicitContentHeight + topPadding + bottomPadding, minimumHeight)
    implicitTextPadding: (checkable ? IA.Appearance.metrics.indicatorSize + spacing : 0)
                         + (showsIcon ? icon.width + spacing : 0)
    horizontalPadding: appearance.horizontalPadding
    verticalPadding: appearance.verticalPadding
    leftPadding: mirrored && subMenu !== null ? 0 : appearance.horizontalPadding
    rightPadding: !mirrored && subMenu !== null ? 0 : appearance.horizontalPadding
    spacing: appearance.spacing
    font: appearance.resolvedFont
    hoverEnabled: true
    icon.width: IA.Appearance.metrics.iconSize
    icon.height: IA.Appearance.metrics.iconSize
    icon.color: surface.foregroundColor

    indicator: checkMark
    arrow: submenuArrow
    contentItem: RowLayout {
        id: content
        LayoutMirroring.enabled: false
        LayoutMirroring.childrenInherit: true
        spacing: 0
        layoutDirection: control.mirrored ? Qt.RightToLeft : Qt.LeftToRight
        // Qt aggregates implicitTextPadding across the menu. One leading lane
        // reserves that width even on plain rows; unused local slots collapse.
        RowLayout {
            Layout.preferredWidth: control.leadingPadding
            Layout.minimumWidth: control.leadingPadding
            Layout.maximumWidth: control.leadingPadding
            visible: control.leadingPadding > 0
            spacing: 0
            layoutDirection: control.mirrored ? Qt.RightToLeft : Qt.LeftToRight
            Item {
                Layout.preferredWidth: IA.Appearance.metrics.indicatorSize
                Layout.leftMargin: control.mirrored ? control.spacing : 0
                Layout.rightMargin: control.mirrored ? 0 : control.spacing
                implicitHeight: IA.Appearance.metrics.indicatorSize
                visible: control.checkable
                IA.Icon {
                    id: checkMark
                    objectName: "iaIndicator"
                    anchors.centerIn: parent
                    visible: control.checked
                    width: IA.Appearance.metrics.indicatorSize
                    height: IA.Appearance.metrics.indicatorSize
                    source: IA.Appearance.icons.check
                    color: surface.foregroundColor
                }
            }
            Item { Layout.fillWidth: true }
            IA.Icon {
                objectName: "iaMenuIcon"
                Layout.preferredWidth: control.icon.width
                Layout.preferredHeight: control.icon.height
                Layout.leftMargin: control.mirrored ? control.spacing : 0
                Layout.rightMargin: control.mirrored ? 0 : control.spacing
                visible: control.showsIcon
                name: control.icon.name
                source: IA.IconSource.resolve(control, control.icon.source)
                color: control.icon.color
                cache: control.icon.cache
            }
        }
        Text {
            id: label
            objectName: "iaLabel"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            visible: control.display !== T.AbstractButton.IconOnly
            text: control.text
            font: control.font
            color: surface.foregroundColor
            textFormat: Text.PlainText
            elide: Text.ElideRight
            horizontalAlignment: control.mirrored ? Text.AlignRight : Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            id: shortcut
            objectName: "iaShortcut"
            Layout.minimumWidth: 0
            visible: control.shortcutText.length > 0
            leftPadding: control.mirrored ? 0 : control.spacing
            rightPadding: control.mirrored ? control.spacing : 0
            text: control.shortcutText
            font: control.font
            color: surface.foregroundColor
            textFormat: Text.PlainText
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        Item {
            Layout.preferredWidth: control.height
            implicitHeight: IA.Appearance.metrics.iconSize
            visible: control.subMenu !== null
            Item {
                objectName: "iaArrowSlot"
                anchors.centerIn: parent
                width: control.height
                height: control.height
                IA.Icon {
                    id: submenuArrow
                    objectName: "iaSubmenuArrow"
                    anchors.centerIn: parent
                    visible: control.subMenu !== null
                    width: IA.Appearance.metrics.iconSize
                    height: IA.Appearance.metrics.iconSize
                    source: IA.Appearance.icons.chevronRight
                    color: surface.foregroundColor
                    mirrored: control.mirrored
                }
            }
        }
    }
    background: IA.ControlBackground {
        id: surface
        resolvedAppearance: appearance
        controlEnabled: control.enabled
        down: control.down
        hovered: control.hovered
        selected: control.highlighted
        active: control.Window.active
        focused: control.visualFocus
        outlined: false
        border.width: focused ? colors.focusedBorderWidth : colors.borderWidth
    }
}
