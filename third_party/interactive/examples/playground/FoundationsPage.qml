// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import InterActive
import InterActive.Components
import InterActive.Foundation as IA

Item {
    id: root
    objectName: "foundationsPage"
    property PlaygroundState state: PlaygroundState
    property bool windowPopups: false
    readonly property bool menuOpen: mixedMenu.opened
    readonly property bool dialogOpen: editDialog.opened
    readonly property Item activePopupContent: editDialog.opened ? editDialog.contentItem
        : deliveryMenu.opened ? deliveryMenu.contentItem : mixedMenu.opened ? mixedMenu.contentItem : null
    readonly property list<Item> openPopupContents: {
        const contents = [];
        if (mixedMenu.opened)
            contents.push(mixedMenu.contentItem);
        if (deliveryMenu.opened)
            contents.push(deliveryMenu.contentItem);
        if (editDialog.opened)
            contents.push(editDialog.contentItem);
        if (deliveryCombo.popup.opened)
            contents.push(deliveryCombo.popup.contentItem);
        return contents;
    }

    function openMenu(): void { mixedMenu.open(); }
    function openDialog(): void {
        if (!editDialog.visible) {
            state.beginDialog();
            editDialog.open();
        }
    }

    IAScrollView {
        id: scrollView
        anchors.fill: parent
        clip: true
        Flickable {
            id: scroller
            objectName: "overviewScroll"
            contentWidth: width
            contentHeight: overview.implicitHeight + 2 * IA.Appearance.metrics.spacingXL
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: overview
                x: IA.Appearance.metrics.spacingXL
                y: IA.Appearance.metrics.spacingXL
                width: Math.max(0, scroller.width - 2 * IA.Appearance.metrics.spacingXL)
                spacing: IA.Appearance.metrics.spacingXL
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: IA.Appearance.metrics.spacingXS
                    IALabel {
                        Layout.fillWidth: true
                        text: "Foundations"
                        fontPixelSize: IA.Appearance.typography.headingSize
                        fontWeight: IA.Appearance.typography.emphasisWeight
                    }
                    IALabel {
                        Layout.fillWidth: true
                        text: "A mixed surface for the first visual review. Edit, select, open a menu, then change the appearance."
                        foregroundColor: IA.Appearance.colors.secondaryText
                        wrapMode: Text.Wrap
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: overview.width >= 900 ? 3 : overview.width >= 640 ? 2 : 1
                    columnSpacing: IA.Appearance.metrics.spacingXL
                    rowSpacing: IA.Appearance.metrics.spacingXL
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 360
                        Layout.minimumWidth: 0
                        Layout.alignment: Qt.AlignTop
                        spacing: IA.Appearance.metrics.spacingL
                        Specimen {
                            Layout.fillWidth: true
                            title: "Profile preferences"
                            Flow {
                                Layout.fillWidth: true
                                spacing: IA.Appearance.metrics.spacingS
                                IAButton {
                                    id: menuButton
                                    objectName: "menuButton"
                                    text: "Collection actions"
                                    onClicked: root.openMenu()
                                }
                                IAToolButton {
                                    objectName: "dialogButton"
                                    text: "Edit in dialog"
                                    onClicked: root.openDialog()
                                }
                            }
                            IAFieldRow {
                                Layout.fillWidth: true
                                label: "Display name"
                                IATextField {
                                    objectName: "nameField"
                                    Layout.fillWidth: true
                                    text: root.state.draftName
                                    selectByMouse: true
                                    Accessible.name: "Display name"
                                    onTextEdited: root.state.draftName = text
                                }
                            }
                            IAFieldRow {
                                Layout.fillWidth: true
                                label: "Address"
                                description: "Use an address at example.test. Nothing leaves this workbench."
                                validation: emailField.validation
                                message: emailField.validation === IA.Presentation.Invalid ? "Use a name@example.test address." : ""
                                IATextField {
                                    id: emailField
                                    objectName: "emailField"
                                    Layout.fillWidth: true
                                    text: root.state.draftEmail
                                    selectByMouse: true
                                    Accessible.name: "Example address"
                                    validation: text.endsWith("@example.test") && text.indexOf("@") > 0
                                        ? IA.Presentation.None : IA.Presentation.Invalid
                                    onTextEdited: root.state.draftEmail = text
                                }
                            }
                            IACheckBox {
                                objectName: "choiceCheck"
                                Layout.fillWidth: true
                                text: "Include research notes"
                                checked: root.state.optIn
                                onClicked: root.state.optIn = checked
                            }
                            IASettingsRow {
                                Layout.fillWidth: true
                                label: "Notifications"
                                description: root.state.notifications ? "Enabled in the draft" : "Paused in the draft"
                                IASwitch {
                                    objectName: "choiceSwitch"
                                    Accessible.name: "Notifications"
                                    checked: root.state.notifications
                                    onClicked: root.state.notifications = checked
                                }
                            }
                            IAFieldRow {
                                Layout.fillWidth: true
                                label: "Delivery schedule"
                                IAComboBox {
                                    id: deliveryCombo
                                    objectName: "deliveryCombo"
                                    Layout.fillWidth: true
                                    Accessible.name: "Delivery schedule"
                                    textRole: "text"
                                    valueRole: "value"
                                    model: [{text:"Daily",value:"daily"},{text:"Weekly",value:"weekly"}]
                                    currentValue: root.state.delivery
                                    popup.popupType: root.windowPopups ? T.Popup.Window : T.Popup.Item
                                    onActivated: root.state.delivery = currentValue
                                }
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: IA.Appearance.metrics.spacingL
                                IARadioButton {
                                    objectName: "dailyRadio"
                                    text: "Daily"
                                    checked: root.state.delivery === "daily"
                                    onClicked: root.state.delivery = "daily"
                                }
                                IARadioButton {
                                    objectName: "weeklyRadio"
                                    text: "Weekly"
                                    checked: root.state.delivery === "weekly"
                                    onClicked: root.state.delivery = "weekly"
                                }
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: IA.Appearance.metrics.spacingS
                                IAButton {
                                    objectName: "applyButton"
                                    text: "Apply"
                                    highlighted: true
                                    enabled: root.state.validDraft && root.state.dirty
                                    onClicked: root.state.applyDraft()
                                }
                                IAButton {
                                    objectName: "resetButton"
                                    text: "Reset"
                                    enabled: root.state.dirty
                                    onClicked: root.state.resetDraft()
                                }
                                IALabel {
                                    text: root.state.dirty ? "Unapplied changes" : "Up to date"
                                    height: IA.Appearance.metrics.controlHeight
                                    verticalAlignment: Text.AlignVCenter
                                    foregroundColor: IA.Appearance.colors.secondaryText
                                }
                            }
                            IAStatusBanner {
                                Layout.fillWidth: true
                                title: "Committed profile"
                                message: root.state.committedSummary
                                objectName: "committedSummary"
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 300
                        Layout.minimumWidth: 0
                        Layout.alignment: Qt.AlignTop
                        spacing: IA.Appearance.metrics.spacingXL
                        Specimen {
                            Layout.fillWidth: true
                            title: "Value and selection"
                            RowLayout {
                                Layout.fillWidth: true
                                IALabel { text: "Preview level" }
                                Item { Layout.fillWidth: true }
                                IALabel {
                                    objectName: "valueReadout"
                                    text: Math.round(root.state.previewValue) + "%"
                                }
                            }
                            IASlider {
                                objectName: "valueSlider"
                                Layout.fillWidth: true
                                from: 0
                                to: 100
                                stepSize: 1
                                value: root.state.previewValue
                                Accessible.name: "Preview level"
                                onMoved: root.state.previewValue = value
                            }
                            IAProgressBar {
                                objectName: "progressPreview"
                                Layout.fillWidth: true
                                from: 0
                                to: 100
                                value: root.state.previewValue
                                Accessible.name: "Preview progress"
                            }
                            IATabBar {
                                objectName: "previewTabs"
                                property bool initialized: false
                                Layout.fillWidth: true
                                currentIndex: root.state.selectedTab
                                Component.onCompleted: initialized = true
                                onCurrentIndexChanged: {
                                    if (initialized && currentIndex >= 0)
                                        root.state.selectedTab = currentIndex;
                                }
                                IATabButton {
                                    text: "Collection"
                                }
                                IATabButton {
                                    text: "Details"
                                }
                            }
                            IAItemDelegate {
                                objectName: "listRow"
                                Layout.fillWidth: true
                                visible: root.state.collectionVisible && root.state.selectedTab === 0
                                text: root.state.pinned ? "Design resources (pinned)" : "Design resources"
                                highlighted: root.state.rowSelected
                                onClicked: root.state.rowSelected = true
                            }
                            IALabel {
                                Layout.fillWidth: true
                                objectName: "selectionInspector"
                                visible: root.state.collectionVisible
                                text: root.state.selectedTab === 1
                                    ? "Design resources\n128 reference items, reviewed locally. Delivery: " + root.state.delivery + "."
                                    : root.state.rowSelected ? "Selected: Design resources (128 items)" : "Select a collection to inspect it."
                                foregroundColor: IA.Appearance.colors.secondaryText
                                wrapMode: Text.Wrap
                            }
                            IAEmptyState {
                                Layout.fillWidth: true
                                visible: !root.state.collectionVisible
                                title: "No collection in the preview"
                                description: "The collection was removed from this local fixture."
                                actionText: "Restore collection"
                                onActionRequested: root.state.collectionVisible = true
                            }
                        }
                        Specimen {
                            Layout.fillWidth: true
                            title: "Type and surfaces"
                            IAPane {
                                Layout.fillWidth: true
                                backgroundColor: IA.Appearance.colors.content
                                contentItem: ColumnLayout {
                                    spacing: IA.Appearance.metrics.spacingM
                                    IALabel {
                                        Layout.fillWidth: true
                                        text: "A place for the details"
                                        fontPixelSize: IA.Appearance.typography.headingSize
                                        wrapMode: Text.Wrap
                                    }
                                    IALabel {
                                        Layout.fillWidth: true
                                        text: "Quiet type, fine separators and flat surfaces. The controls do the talking."
                                        wrapMode: Text.Wrap
                                    }
                                    IALabel {
                                        Layout.fillWidth: true
                                        text: "Supporting text / 0, 1, 128, 12860\nCaf\u0065\u0301 / \u65e5\u672c\u8a9e / \u0627\u0644\u0639\u0631\u0628\u064a\u0629 / ID-128"
                                        fontPixelSize: IA.Appearance.typography.supportingSize
                                        foregroundColor: IA.Appearance.colors.secondaryText
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                            IAFrame {
                                Layout.fillWidth: true
                                contentItem: IALabel {
                                    text: "Frame\nA boundary when the content needs one."
                                    wrapMode: Text.Wrap
                                }
                            }
                            IAPane {
                                Layout.fillWidth: true
                                backgroundColor: IA.Appearance.colors.inverseSurface
                                contentItem: IALabel {
                                    text: "Inverse surface\nA local surface, not a second theme."
                                    foregroundColor: IA.Appearance.colors.inverseText
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 240
                        Layout.minimumWidth: 0
                        Layout.alignment: Qt.AlignTop
                        spacing: IA.Appearance.metrics.spacingXL
                        Specimen {
                            Layout.fillWidth: true
                            title: "Solid semantic buttons"
                            description: "Solid fills from the shared palette. Click a sample to compare."
                            Flow {
                                objectName: "opencodeReferenceRow"
                                Layout.fillWidth: true
                                spacing: IA.Appearance.metrics.spacingS
                                Repeater {
                                    model: [
                                        {key: "blue", name: "Primary", tone: IA.Presentation.Primary},
                                        {key: "amber", name: "Caution", tone: IA.Presentation.Caution},
                                        {key: "rose", name: "Danger", tone: IA.Presentation.Destructive}
                                    ]
                                    ColumnLayout {
                                        id: reference
                                        required property var modelData
                                        readonly property color effectiveColor: sample.backgroundColor
                                        width: implicitWidth
                                        height: implicitHeight
                                        spacing: IA.Appearance.metrics.spacingXS
                                        IAButton {
                                            id: sample
                                            objectName: "opencode-" + reference.modelData.key
                                            Layout.alignment: Qt.AlignHCenter
                                            text: "Sample"
                                            tone: reference.modelData.tone
                                            Accessible.name: "Select " + reference.modelData.name + " semantic sample"
                                            onClicked: root.state.colorReference = reference.modelData.name + " " + String(reference.effectiveColor).toUpperCase()
                                        }
                                        IALabel {
                                            Layout.alignment: Qt.AlignHCenter
                                            text: reference.modelData.name
                                            fontPixelSize: IA.Appearance.typography.supportingSize
                                        }
                                        IALabel {
                                            objectName: "opencode-color-" + reference.modelData.key
                                            Layout.alignment: Qt.AlignHCenter
                                            text: String(reference.effectiveColor).toUpperCase()
                                            fontPixelSize: IA.Appearance.typography.supportingSize
                                            foregroundColor: IA.Appearance.colors.secondaryText
                                        }
                                    }
                                }
                            }
                            IAPane {
                                objectName: "semanticSuccessPreview"
                                Layout.fillWidth: true
                                backgroundColor: IA.Appearance.colors.successSurface
                                contentItem: ColumnLayout {
                                    spacing: IA.Appearance.metrics.spacingXS
                                    IALabel {
                                        objectName: "semanticSuccessText"
                                        Layout.fillWidth: true
                                        text: "Saved successfully"
                                        foregroundColor: IA.Appearance.colors.successText
                                        wrapMode: Text.Wrap
                                    }
                                    RowLayout {
                                        objectName: "semanticSuccessSwatchRow"
                                        Layout.fillWidth: true
                                        spacing: IA.Appearance.metrics.spacingS
                                        Rectangle {
                                            objectName: "semanticSuccessSwatch"
                                            Layout.fillHeight: true
                                            Layout.preferredWidth: IA.Appearance.metrics.iconSize
                                            Layout.preferredHeight: IA.Appearance.metrics.iconSize
                                            color: IA.Appearance.colors.success
                                        }
                                        IALabel {
                                            objectName: "semanticSuccessReadout"
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            verticalAlignment: Text.AlignVCenter
                                            text: "Success " + String(IA.Appearance.colors.success).toUpperCase()
                                            foregroundColor: IA.Appearance.colors.successText
                                            fontPixelSize: IA.Appearance.typography.supportingSize
                                            wrapMode: Text.Wrap
                                        }
                                    }
                                }
                            }
                            IALabel {
                                objectName: "opencodeSelection"
                                Layout.fillWidth: true
                                text: root.state.colorReference.length > 0
                                    ? "Selected: " + root.state.colorReference : "No color selected"
                                foregroundColor: IA.Appearance.colors.secondaryText
                                wrapMode: Text.Wrap
                            }
                        }
                        Specimen {
                            Layout.fillWidth: true
                            title: "State shelf"
                            Flow {
                                Layout.fillWidth: true
                                spacing: IA.Appearance.metrics.spacingS
                                IAButton { text: "Disabled"; enabled: false }
                                IAButton {
                                    text: "Primary"
                                    tone: IA.Presentation.Primary
                                    enabled: root.state.collectionVisible
                                    onClicked: {
                                        root.state.rowSelected = true;
                                        root.state.selectedTab = 0;
                                    }
                                }
                                IAButton {
                                    text: "Remove"
                                    tone: IA.Presentation.Destructive
                                    enabled: root.state.collectionVisible
                                    onClicked: root.state.collectionVisible = false
                                }
                            }
                            IATextField {
                                Layout.fillWidth: true
                                text: "Read-only reference"
                                readOnly: true
                                selectByMouse: true
                                Accessible.name: "Read-only reference"
                            }
                            IAFieldRow {
                                Layout.fillWidth: true
                                label: "Invalid specimen"
                                message: "An example of application-supplied validation."
                                validation: IA.Presentation.Invalid
                                IATextField {
                                    Layout.fillWidth: true
                                    text: "not-an-address"
                                    validation: IA.Presentation.Invalid
                                    Accessible.name: "Invalid specimen"
                                }
                            }
                            IAButton {
                                id: customButton
                                objectName: "customPurpleButton"
                                text: "Local purple"
                                backgroundColor: IA.Appearance.contrast === "high" ? "#6A3D99" : "#8657B8"
                                controlRadius: 9
                                fontPixelSize: 15
                                onClicked: root.state.pinned = !root.state.pinned
                            }
                            IALabel {
                                Layout.fillWidth: true
                                text: "A local color, radius and type size. Reset removes those overrides and resumes application defaults."
                                foregroundColor: IA.Appearance.colors.secondaryText
                                wrapMode: Text.Wrap
                            }
                            IAButton {
                                objectName: "resetLocalAppearance"
                                text: "Reset local appearance"
                                onClicked: customButton.resetAppearance()
                            }
                            IAToolButton {
                                id: tipButton
                                text: "About this shelf"
                                Accessible.description: "Hover or focus for a tooltip."
                                IAToolTip {
                                    parent: tipButton
                                    visible: tipButton.hovered || tipButton.visualFocus
                                    text: "These are real IA controls, not painted state replicas."
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Shortcut {
        sequence: "Ctrl+E"
        context: Qt.ApplicationShortcut
        enabled: !root.dialogOpen
        onActivated: root.openDialog()
    }

    IAMenu {
        id: mixedMenu
        objectName: "mixedMenu"
        parent: menuButton
        y: menuButton.height
        width: Math.min(280, root.width - 2 * IA.Appearance.metrics.spacingS)
        margins: IA.Appearance.metrics.spacingS
        popupType: root.windowPopups ? T.Popup.Window : T.Popup.Item
        IAMenuItem {
            objectName: "menuInspectCollection"
            text: "Inspect collection"
            icon.source: IA.Appearance.icons.search
            enabled: root.state.collectionVisible
            onTriggered: {
                root.state.rowSelected = true;
                root.state.selectedTab = 1;
            }
        }
        IAMenuItem {
            objectName: "menuEditProfile"
            text: "Edit profile…"
            onTriggered: root.openDialog()
        }
        IAMenuItem {
            objectName: "menuPinCollection"
            text: "Pin collection"
            checkable: true
            checked: root.state.pinned
            onTriggered: root.state.pinned = checked
        }
        IAMenu {
            id: deliveryMenu
            objectName: "deliverySubmenu"
            title: "Delivery"
            popupType: root.windowPopups ? T.Popup.Window : T.Popup.Item
            IAMenuItem {
                text: "Daily"
                checkable: true
                autoExclusive: true
                checked: root.state.delivery === "daily"
                onTriggered: root.state.delivery = "daily"
            }
            IAMenuItem {
                text: "Weekly"
                checkable: true
                autoExclusive: true
                checked: root.state.delivery === "weekly"
                onTriggered: root.state.delivery = "weekly"
            }
        }
        IAMenuSeparator {}
        IAMenuItem { text: "Export (unavailable)"; enabled: false }
        IAMenuItem {
            objectName: "menuRemoveCollection"
            text: root.state.collectionVisible ? "Remove collection" : "Restore collection"
            tone: root.state.collectionVisible ? IA.Presentation.Destructive : IA.Presentation.Neutral
            onTriggered: root.state.collectionVisible = !root.state.collectionVisible
        }
    }

    IADialog {
        id: editDialog
        objectName: "editDialog"
        parent: root
        anchors.centerIn: parent
        width: Math.min(440, root.width - 2 * IA.Appearance.metrics.spacingS)
        margins: IA.Appearance.metrics.spacingS
        title: "Edit profile"
        modal: true
        focus: true
        popupType: root.windowPopups ? T.Popup.Window : T.Popup.Item
        standardButtons: T.Dialog.Apply | T.Dialog.Cancel
        onApplied: {
            if (root.state.applyDialog())
                accept();
        }
        onRejected: root.state.cancelDialog()
        ColumnLayout {
            id: dialogForm
            width: parent.width
            spacing: IA.Appearance.metrics.spacingL
            IALabel {
                Layout.fillWidth: true
                text: "Apply commits these details and the current preferences. Cancel leaves your form draft and committed profile unchanged."
                foregroundColor: IA.Appearance.colors.secondaryText
                wrapMode: Text.Wrap
            }
            IAFieldRow {
                Layout.fillWidth: true
                label: "Display name"
                IATextField {
                    objectName: "dialogNameField"
                    Layout.fillWidth: true
                    text: root.state.dialogName
                    selectByMouse: true
                    Accessible.name: "Dialog display name"
                    onTextEdited: root.state.dialogName = text
                }
            }
            IAFieldRow {
                Layout.fillWidth: true
                label: "Address"
                IATextField {
                    objectName: "dialogEmailField"
                    Layout.fillWidth: true
                    text: root.state.dialogEmail
                    selectByMouse: true
                    Accessible.name: "Dialog example address"
                    onTextEdited: root.state.dialogEmail = text
                }
            }
            IALabel {
                Layout.fillWidth: true
                visible: !root.state.validDialog
                text: "Enter a display name and a name@example.test address to apply."
                foregroundColor: IA.Appearance.colors.errorText
                wrapMode: Text.Wrap
            }
        }
    }
}
