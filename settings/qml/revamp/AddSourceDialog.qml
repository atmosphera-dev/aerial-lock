import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import InterActive
import InterActive.Components
import InterActive.Foundation as IA

IADialog {
    id: root
    required property var draft
    property bool appleAvailable: false
    readonly property bool online: draft.type === "community" || draft.type === "apple"
    signal fieldEdited(string key, var value)
    signal choosePathRequested()
    signal saveRequested()
    signal dismissed()
    title: draft.editing ? "Edit source" : "Add source"
    modal: true
    focus: true
    margins: Insets.edge
    width: Math.min(440, parent ? parent.width - 2 * Insets.edge : 440)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    enabled: !draft.pending
    closePolicy: draft.pending ? T.Popup.NoAutoClose : T.Popup.CloseOnEscape
    standardButtons: T.Dialog.Apply | T.Dialog.Cancel
    onApplied: root.saveRequested()
    onRejected: root.dismissed()
    onOpened: {
        // Qt creates standard buttons lazily. Bind the actual, completed button,
        // not a pre-construction standardButton() lookup with no notify signal.
        const button = root.standardButton(T.Dialog.Apply);
        button.text = Qt.binding(() => root.draft.pending ? "Saving..." : root.draft.editing ? "Save" : "Add source");
        nameField.forceActiveFocus(Qt.TabFocusReason);
    }
    ColumnLayout {
        width: parent.width
        spacing: IA.Appearance.metrics.spacingL
        IAFieldRow {
            Layout.fillWidth: true
            label: "Type"
            IARadioButton { objectName: "sourceTypeOnline"; text: "Online pack"; checked: root.online; onClicked: root.fieldEdited("type", "community") }
            IARadioButton { objectName: "sourceTypeFolder"; text: "Folder"; checked: root.draft.type === "folder"; onClicked: root.fieldEdited("type", "folder") }
            IARadioButton { objectName: "sourceTypeFile"; text: "Video file"; checked: root.draft.type === "file"; onClicked: root.fieldEdited("type", "file") }
        }
        IAButton {
            objectName: "sourceApple"
            visible: root.appleAvailable && !root.draft.editing
            text: "Use bundled Apple TV source"
            onClicked: root.fieldEdited("type", "defaultApple")
        }
        IAFieldRow {
            Layout.fillWidth: true
            label: "Name"
            IATextField {
                id: nameField
                objectName: "sourceName"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Accessible.name: "Name"
                text: root.draft.name || ""
                onTextEdited: root.fieldEdited("name", text)
            }
        }
        IAFieldRow {
            Layout.fillWidth: true
            label: root.online ? "Address" : root.draft.type === "folder" ? "Folder" : "Video file"
            description: root.online ? "A pack manifest, usually ending in .json." : ""
            IATextField {
                objectName: "sourceAddress"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Accessible.name: root.online ? "Address" : root.draft.type === "folder" ? "Folder" : "Video file"
                text: root.draft.source || ""
                onTextEdited: root.fieldEdited("source", text)
            }
            IAButton { objectName: "sourceChoosePath"; visible: !root.online; text: "Choose..."; onClicked: root.choosePathRequested() }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.online
            spacing: IA.Appearance.metrics.spacingL
            IAFieldRow {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                label: "Format"
                IAComboBox {
                    objectName: "sourceFormat"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Accessible.name: "Format"
                    popupMargins: Insets.edge
                    textRole: "label"; valueRole: "value"
                    model: [{ label: "Aerial community pack", value: "community" }, { label: "Apple aerials", value: "apple" }]
                    currentIndex: root.draft.type === "apple" ? 1 : 0
                    onActivated: root.fieldEdited("type", currentValue)
                }
            }
            IAFieldRow {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                label: "Highest quality"
                IAComboBox {
                    objectName: "sourceQuality"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Accessible.name: "Highest quality"
                    popupMargins: Insets.edge
                    textRole: "label"; valueRole: "value"
                    model: root.draft.qualityChoices || []
                    currentIndex: root.draft.qualitySelected || 0
                    inherited: root.draft.qualityInherited ?? true
                    onActivated: root.fieldEdited("maxResolution", currentValue)
                }
            }
        }
        IALabel {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            visible: text.length > 0
            text: root.draft.error || ""
            textFormat: Text.PlainText
            foregroundColor: IA.Appearance.colors.errorText
            wrapMode: Text.Wrap
        }
    }
}
