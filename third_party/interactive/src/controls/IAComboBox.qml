// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.ComboBox {
    id: control
    property bool inherited: false
    property color leadingColor: "transparent"
    property real popupMargins: IA.Appearance.metrics.spacingS
    readonly property real _sourceWindowHeight: Window.height
    property alias backgroundColor: appearance.backgroundColor
    property alias foregroundColor: appearance.foregroundColor
    property alias hoverBackgroundColor: appearance.hoverBackgroundColor
    property alias pressedBackgroundColor: appearance.pressedBackgroundColor
    property alias disabledBackgroundColor: appearance.disabledBackgroundColor
    property alias disabledForegroundColor: appearance.disabledForegroundColor
    property alias borderColor: appearance.borderColor
    property alias disabledBorderColor: appearance.disabledBorderColor
    property alias focusColor: appearance.focusColor
    property alias controlRadius: appearance.controlRadius
    property alias fontFamily: appearance.fontFamily
    property alias fontPixelSize: appearance.fontPixelSize
    property alias fontWeight: appearance.fontWeight
    property alias minimumHeight: appearance.minimumHeight
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    function resetAppearance(): void { appearance.resetOverrides(); }

    IA.ControlAppearance { id: appearance; source: IA.Appearance; family: "button" }
    implicitWidth: Math.max(IA.Appearance.metrics.fieldWidth,
        implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitContentHeight + topPadding + bottomPadding, minimumHeight)
    leftPadding: mirrored ? (indicator && indicator.visible ? indicator.width : 0) : appearance.horizontalPadding
    rightPadding: mirrored ? appearance.horizontalPadding : (indicator && indicator.visible ? indicator.width : 0)
    topPadding: appearance.verticalPadding
    bottomPadding: appearance.verticalPadding
    spacing: appearance.spacing
    font: appearance.resolvedFont
    hoverEnabled: true

    indicator: Item {
        objectName: "iaComboIndicatorSlot"
        width: control.height
        height: control.height
        LayoutMirroring.enabled: control.mirrored
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        IA.Icon {
            objectName: "iaComboChevron"
            anchors.centerIn: parent
            width: IA.Appearance.metrics.iconSize
            height: IA.Appearance.metrics.iconSize
            source: IA.Appearance.icons.chevronDown
            color: surface.foregroundColor
        }
    }
    contentItem: T.TextField {
        id: editor
        objectName: "iaComboEditor"
        LayoutMirroring.enabled: false
        LayoutMirroring.childrenInherit: true
        readonly property real swatchLane: swatch.visible ? swatch.width + control.spacing : 0
        implicitWidth: Math.max(contentWidth, label.implicitWidth) + leftPadding + rightPadding
        implicitHeight: Math.max(contentHeight, swatch.visible ? swatch.implicitHeight : 0)
        leftPadding: control.mirrored ? 0 : swatchLane
        rightPadding: control.mirrored ? swatchLane : 0
        topPadding: 0
        bottomPadding: 0
        text: control.editable ? control.editText : ""
        font: control.font
        enabled: control.editable
        autoScroll: control.editable
        readOnly: control.down
        inputMethodHints: control.inputMethodHints
        validator: control.validator
        selectByMouse: control.selectTextByMouse
        color: control.enabled && control.inherited ? IA.Appearance.colors.secondaryText : surface.foregroundColor
        selectionColor: appearance.stateTable.selectionColor
        selectedTextColor: appearance.stateTable.selectedTextColor
        verticalAlignment: TextInput.AlignVCenter
        horizontalAlignment: control.mirrored ? TextInput.AlignRight : TextInput.AlignLeft
        clip: true
        background: null
        Text {
            id: label
            objectName: "iaComboLabel"
            visible: !control.editable
            anchors.fill: parent
            anchors.leftMargin: editor.leftPadding
            anchors.rightMargin: editor.rightPadding
            text: control.displayText
            textFormat: Text.PlainText
            font: control.font
            color: editor.color
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: editor.horizontalAlignment
        }
        Rectangle {
            id: swatch
            objectName: "iaComboSwatch"
            visible: control.leadingColor.a > 0
            implicitWidth: IA.Appearance.metrics.indicatorSize
            implicitHeight: implicitWidth
            width: implicitWidth
            height: implicitHeight
            LayoutMirroring.enabled: control.mirrored
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            color: control.leadingColor
            radius: IA.Appearance.metrics.controlRadius
            border.width: IA.Appearance.metrics.hairlineWidth / Screen.devicePixelRatio
            border.color: IA.Appearance.colors.outline
        }
    }
    background: IA.ControlBackground {
        id: surface
        resolvedAppearance: appearance
        controlEnabled: control.enabled
        down: control.down
        hovered: control.hovered
        active: control.Window.active
        focused: control.visualFocus || (control.editable && control.activeFocus)
        visible: !control.flat || control.down || control.visualFocus
    }
    delegate: IAItemDelegate {
        required property var model
        required property int index
        width: ListView.view ? ListView.view.width : 0
        text: model[control.textRole || "modelData"]
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
        fontFamily: appearance.fontFamily
        fontPixelSize: appearance.fontPixelSize
        fontWeight: appearance.fontWeight
    }
    popup: IAPopup {
        y: control.height + IA.Appearance.metrics.spacingXS
        width: control.width
        height: Math.min(implicitHeight,
            Math.max(0, control._sourceWindowHeight - topMargin - bottomMargin))
        margins: control.popupMargins
        padding: IA.Appearance.metrics.spacingXS
        fontFamily: appearance.fontFamily
        fontPixelSize: appearance.fontPixelSize
        fontWeight: appearance.fontWeight
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: IAScrollBar { }
        }
    }
}
