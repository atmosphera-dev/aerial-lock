// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import InterActive
import InterActive.Components
import InterActive.Foundation as IA

Item {
    id: root
    required property color resolvedColor
    required property string label
    required property string dialogTitle
    required property string namePrefix
    property string accessibleDescription: "Changes apply live; closing keeps edits."
    property var pickerParentWindow: root.Window.window
    property string displayName: label
    property color readoutForeground: pickerButton.foregroundColor
    property bool dropdownIndicator: false
    property bool openerVisible: true
    property bool openerEnterEnabled: false
    property var activation: function(anchor) { root.open(); }
    property bool resetEnabled: false
    property real maximumWidth: implicitWidth
    property real itemSpacing: 8
    property real swatchSize: 16
    property real swatchBorderWidth: 1
    property var traceBegin: function(kind) { return 0; }
    property var traceEnd: function(token) {}
    property bool synchronizing: false
    signal colorSelected(color value)
    signal resetRequested()
    implicitWidth: openerVisible ? pickerButton.implicitWidth + (resetButton.visible ? resetButton.implicitWidth + itemSpacing : 0) : 0
    implicitHeight: openerVisible ? pair.implicitHeight : 0
    width: Math.min(implicitWidth, maximumWidth)
    height: implicitHeight

    function synchronize(): void {
        const token = traceBegin("sync");
        try {
            synchronizing = true;
            // The generic persistent editor synchronizes without reopening its host.
            dialog.colorValue = resolvedColor;
        } finally {
            synchronizing = false;
            traceEnd(token);
        }
    }

    function open(): void {
        synchronizing = true;
        try {
            synchronize();
            synchronizing = true;
            dialog.open();
        } finally {
            synchronizing = false;
        }
    }

    function close(): void { dialog.close(); }
    function focusOpener(): void { pickerButton.forceActiveFocus(Qt.PopupFocusReason); }
    onResolvedColorChanged: synchronize()

    readonly property IAColorDialog _dialog: IAColorDialog {
        id: dialog
        objectName: root.namePrefix + "ColorDialog"
        title: root.dialogTitle
        parentWindow: root.pickerParentWindow
        onColorEdited: value => {
            const token = root.traceBegin("color");
            try {
                if (visible && !root.synchronizing)
                    root.colorSelected(value);
            } finally {
                root.traceEnd(token);
            }
        }
    }

    // The pair stays one outer Flow item; its own Flow wraps if enlarged text
    // makes a single row wider than the available toolbar lane.
    Flow {
        id: pair
        visible: root.openerVisible
        width: root.width
        spacing: root.itemSpacing
        IAButton {
            id: pickerButton
            objectName: root.namePrefix + "PickerButton"
            text: root.label
            width: Math.min(implicitWidth, pair.width)
            Accessible.name: "Choose " + root.label.toLowerCase() + " color"
            Accessible.description: root.accessibleDescription
            property bool enterHeld: false
            onClicked: root.activation(pickerButton)
            Keys.onPressed: event => {
                if (root.openerEnterEnabled && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                    event.accepted = true;
                    if (!event.isAutoRepeat)
                        enterHeld = true;
                }
            }
            Keys.onReleased: event => {
                if (root.openerEnterEnabled && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                    event.accepted = true;
                    if (!event.isAutoRepeat) {
                        const activate = enterHeld;
                        enterHeld = false;
                        if (activate)
                            root.activation(pickerButton);
                    }
                }
            }
            contentItem: RowLayout {
                spacing: root.itemSpacing
                Rectangle {
                    objectName: root.namePrefix + "Swatch"
                    implicitWidth: root.swatchSize
                    implicitHeight: root.swatchSize
                    Layout.alignment: Qt.AlignVCenter
                    color: root.resolvedColor
                    radius: pickerButton.controlRadius
                    border.width: root.swatchBorderWidth
                    border.color: pickerButton.borderColor
                    Accessible.ignored: true
                }
                IALabel {
                    objectName: root.namePrefix + "Readout"
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    text: root.displayName + " " + String(root.resolvedColor).toUpperCase()
                    foregroundColor: root.readoutForeground
                    fontFamily: pickerButton.fontFamily
                    fontPixelSize: pickerButton.fontPixelSize
                    fontWeight: pickerButton.fontWeight
                    Accessible.ignored: true
                }
                IA.Icon {
                    visible: root.dropdownIndicator
                    implicitWidth: IA.Appearance.metrics.iconSize
                    implicitHeight: IA.Appearance.metrics.iconSize
                    Layout.alignment: Qt.AlignVCenter
                    source: IA.Appearance.icons.chevronDown
                    color: pickerButton.foregroundColor
                    Accessible.ignored: true
                }
            }
        }
        IAToolButton {
            id: resetButton
            objectName: "reset" + root.namePrefix.charAt(0).toUpperCase() + root.namePrefix.slice(1) + "Button"
            text: "Reset " + root.label.toLowerCase()
            width: Math.min(implicitWidth, pair.width)
            visible: root.resetEnabled
            onClicked: root.resetRequested()
        }
    }
}
