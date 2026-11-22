// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQml.Models
import InterActive
import InterActive.Foundation as IA

IAMenu {
    id: root
    objectName: "accentPaletteMenu"
    required property var presets
    required property var savedKeys
    required property string currentDisplayKey
    required property real windowWidthBudget
    readonly property color savedTextColor: IA.Appearance.scheme === "dark"
        ? (IA.Appearance.contrast === "high" ? "#E8E8E8" : "#C8C8C8")
        : (IA.Appearance.contrast === "high" ? "#404040" : "#595959")
    property bool pendingCustom: false
    signal accentSelected(color value)
    signal customRequested()
    signal openerFocusRequested()
    width: Math.min(360, Math.max(0, windowWidthBudget - 2 * margins))
    y: parent ? parent.height : 0
    popupType: T.Popup.Item
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent

    function openFor(anchor: Item): void {
        if (visible) {
            close();
            return;
        }
        parent = anchor;
        pendingCustom = false;
        open();
    }

    onOpened: {
        let match = presets.findIndex(function(preset) { return preset.hex.toLowerCase() === root.currentDisplayKey; });
        currentIndex = match >= 0 ? match : presets.length;
        const item = itemAt(currentIndex);
        if (item)
            item.forceActiveFocus(Qt.PopupFocusReason);
    }
    onClosed: {
        const custom = pendingCustom;
        pendingCustom = false;
        if (custom)
            customRequested();
        else
            openerFocusRequested();
    }

    Instantiator {
        model: root.presets
        delegate: IAMenuItem {
            id: row
            required property var modelData
            readonly property string displayKey: modelData.hex.toLowerCase()
            readonly property bool saved: root.savedKeys.indexOf(displayKey) >= 0
            readonly property color rowTextColor: saved ? root.savedTextColor : IA.Appearance.colors.text
            objectName: "accentPreset_" + displayKey.slice(1)
            text: modelData.name + " " + modelData.hex
            Accessible.name: text
            Accessible.description: saved ? "Saved; still selectable" : "Select this calibration accent"
            foregroundColor: rowTextColor
            selectedForegroundColor: rowTextColor
            inactiveForegroundColor: rowTextColor
            disabledForegroundColor: rowTextColor
            fontFamily: root.fontFamily
            fontPixelSize: root.fontPixelSize
            fontWeight: root.fontWeight
            onTriggered: root.accentSelected(modelData.hex)
            contentItem: RowLayout {
                spacing: row.spacing
                layoutDirection: row.mirrored ? Qt.RightToLeft : Qt.LeftToRight
                Rectangle {
                    objectName: "accentPresetSwatch"
                    implicitWidth: IA.Appearance.metrics.iconSize
                    implicitHeight: IA.Appearance.metrics.iconSize
                    Layout.alignment: Qt.AlignVCenter
                    color: row.modelData.hex
                    radius: row.controlRadius
                    border.width: IA.Appearance.metrics.borderWidth
                    border.color: row.borderColor
                    Accessible.ignored: true
                }
                IALabel {
                    objectName: "accentPresetName"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignVCenter
                    text: row.modelData.name
                    foregroundColor: row.rowTextColor
                    fontFamily: row.fontFamily
                    fontPixelSize: row.fontPixelSize
                    fontWeight: row.fontWeight
                    Accessible.ignored: true
                }
                IALabel {
                    objectName: "accentPresetHex"
                    Layout.alignment: Qt.AlignVCenter
                    text: row.modelData.hex
                    foregroundColor: row.rowTextColor
                    fontFamily: row.fontFamily
                    fontPixelSize: row.fontPixelSize
                    fontWeight: row.fontWeight
                    Accessible.ignored: true
                }
            }
        }
        onObjectAdded: (index, object) => root.insertItem(index, object)
        onObjectRemoved: (index, object) => root.removeItem(object)
    }
    IAMenuItem {
        id: custom
        objectName: "accentPaletteCustom"
        text: "Custom…"
        Accessible.name: text
        Accessible.description: "Open the live Accent color dialog"
        onTriggered: root.pendingCustom = true
        contentItem: IALabel {
            text: custom.text
            foregroundColor: IA.Appearance.colors.text
            fontFamily: custom.fontFamily
            fontPixelSize: custom.fontPixelSize
            fontWeight: custom.fontWeight
            Accessible.ignored: true
        }
    }
}
