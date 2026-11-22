// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import InterActive
import InterActive.Components
import InterActive.Foundation as IA

Item {
    id: root
    property PlaygroundState state: PlaygroundState
    // Accept every compatible QQuickWindow host, including QQuickView.
    property var pickerParentWindow: root.Window.window
    property Item accentMenuOpener: null
    property string jsonDraft: "{}"
    property var accentDraft: undefined
    readonly property var accentPresets: [
        {name: "Default blue", hex: "#5C9CF5"},
        {name: "Saturated blue", hex: "#245BDB"},
        {name: "Dark blue", hex: "#233B67"},
        {name: "Muted blue", hex: "#7C91AD"},
        {name: "Pale blue", hex: "#CBDCF5"},
        {name: "Dark purple", hex: "#49335F"},
        {name: "Sage green", hex: "#73947B"},
        {name: "Pale green", hex: "#D4EBCF"},
        {name: "Orange", hex: "#F07828"},
        {name: "Dark red", hex: "#8F263B"},
        {name: "Middle gray", hex: "#808080"},
        {name: "Light gray", hex: "#D0D0D0"}
    ]
    readonly property string accentDisplayKey: state.opaqueAccentKey(IA.Appearance.colors.accent)
    readonly property string accentDisplayName: {
        const preset = accentPresets.find(function(value) { return value.hex.toLowerCase() === root.accentDisplayKey; });
        return preset ? preset.name : "Custom";
    }
    readonly property bool currentAccentSaved: accentDisplayKey.length > 0
        && accentDisplayName !== "Custom" && state.savedAccentKeys.indexOf(accentDisplayKey) >= 0
    implicitHeight: layout.implicitHeight
    implicitWidth: 600

    function openAccentPicker(): void { accentPicker.open(); }
    function openAmberPicker(): void { amberPicker.open(); }
    function openDangerPicker(): void { dangerPicker.open(); }
    function openAccentMenu(anchor: Item): void {
        accentMenuOpener = anchor;
        root.state.refreshSavedAccents();
        accentMenu.openFor(anchor);
    }

    AccentPaletteMenu {
        id: accentMenu
        presets: root.accentPresets
        savedKeys: root.state.savedAccentKeys
        currentDisplayKey: root.accentDisplayKey
        windowWidthBudget: root.accentMenuOpener && root.accentMenuOpener.Window.window
            ? root.accentMenuOpener.Window.window.width : root.width
        onAccentSelected: value => root.state.setAppearanceToken("colors", "accent", value)
        onCustomRequested: accentPicker.open()
        onOpenerFocusRequested: {
            if (root.accentMenuOpener)
                root.accentMenuOpener.forceActiveFocus(Qt.PopupFocusReason);
        }
    }

    function pickerTrace(kind: string, role: int): int {
        return root.state.traceBridge.pickerHandlerBegin(kind === "sync"
            ? AppearanceTraceBridge.Synchronization : AppearanceTraceBridge.ColorHandler, role, 0);
    }

    function endPickerTrace(token: int): void { root.state.traceBridge.pickerHandlerEnd(token, 0); }

    readonly property bool hasActionSurfaceOverrides: {
        const tokens = root.state.appearanceInput.tokens;
        const colors = tokens ? tokens.colors : undefined;
        return colors !== undefined && (colors.primarySurface !== undefined
            || colors.cautionSurface !== undefined || colors.destructiveSurface !== undefined);
    }

    function applyAccentText(): void {
        root.state.setAppearanceToken("colors", "accent", accentField.text);
        root.accentDraft = undefined;
    }

    ColumnLayout {
        id: layout
        width: root.width
        spacing: IA.Appearance.metrics.spacingM
        RowLayout {
            objectName: "appearanceSchemeRow"
            Layout.fillWidth: true
            spacing: IA.Appearance.metrics.spacingS
            IASegmentedControl {
                objectName: "appearanceSchemeChoice"
                Layout.fillWidth: true
                Layout.preferredWidth: 240
                Layout.maximumWidth: 240
                Layout.minimumWidth: 0
                Accessible.name: "Appearance"
                currentIndex: IA.Appearance.scheme === "dark" ? 1 : 0
                onActivated: index => root.state.setAppearanceOption("scheme", index === 1 ? "dark" : "light")
                IASegmentedButton {
                    objectName: "appearanceLight"
                    text: "Light"
                }
                IASegmentedButton {
                    objectName: "appearanceDark"
                    text: "Dark"
                }
            }
            IAButton {
                objectName: "appearanceReset"
                text: "Defaults"
                onClicked: root.state.setAppearance({})
            }
            Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
        }
        Flow {
            objectName: "appearancePaletteFlow"
            Layout.fillWidth: true
            spacing: IA.Appearance.metrics.spacingS
            PaletteColorPicker {
                id: accentPicker
                namePrefix: "accent"
                label: "Accent"
                dialogTitle: "Accent color (live)"
                pickerParentWindow: root.pickerParentWindow
                resolvedColor: IA.Appearance.colors.accent
                displayName: root.accentDisplayName
                readoutForeground: root.currentAccentSaved ? accentMenu.savedTextColor : IA.Appearance.colors.text
                dropdownIndicator: true
                openerEnterEnabled: true
                activation: root.openAccentMenu
                maximumWidth: layout.width
                itemSpacing: IA.Appearance.metrics.spacingS
                swatchSize: IA.Appearance.metrics.iconSize
                swatchBorderWidth: IA.Appearance.metrics.borderWidth
                resetEnabled: root.state.hasAccentOverride || root.state.hasAmberCorrection
                    || root.state.hasDangerCorrection || root.accentDraft !== undefined
                accessibleDescription: root.accentDisplayName + " " + String(IA.Appearance.colors.accent).toUpperCase()
                    + (root.currentAccentSaved ? ". Saved; still selectable." : ".")
                    + " Choose a preset or Custom for the live dialog. Reset also clears temporary Amber and Danger corrections."
                traceBegin: function(kind) { return root.pickerTrace(kind, AppearanceTraceBridge.Accent); }
                traceEnd: root.endPickerTrace
                onColorSelected: value => root.state.setAppearanceToken("colors", "accent", value)
                onResetRequested: {
                    root.accentDraft = undefined;
                    root.state.resetAccentForCalibration();
                }
            }
            PaletteColorPicker {
                id: amberPicker
                openerVisible: false
                namePrefix: "amber"
                label: "Amber"
                dialogTitle: "Amber warning/caution color (live)"
                pickerParentWindow: root.pickerParentWindow
                resolvedColor: IA.Appearance.colors.warning
                maximumWidth: layout.width
                itemSpacing: IA.Appearance.metrics.spacingS
                swatchSize: IA.Appearance.metrics.iconSize
                swatchBorderWidth: IA.Appearance.metrics.borderWidth
                resetEnabled: root.state.hasAmberCorrection || root.state.hasAmberOverride
                accessibleDescription: "Live warning/caution correction for this accent. Reset resumes underlying input or generation."
                traceBegin: function(kind) { return root.pickerTrace(kind, AppearanceTraceBridge.Amber); }
                traceEnd: root.endPickerTrace
                onColorSelected: value => root.state.setSemanticCorrection("warning", value)
                onResetRequested: root.state.resetSemanticCorrection("warning")
            }
            PaletteColorPicker {
                id: dangerPicker
                openerVisible: false
                namePrefix: "danger"
                label: "Danger"
                dialogTitle: "Danger error color (live)"
                pickerParentWindow: root.pickerParentWindow
                resolvedColor: IA.Appearance.colors.error
                maximumWidth: layout.width
                itemSpacing: IA.Appearance.metrics.spacingS
                swatchSize: IA.Appearance.metrics.iconSize
                swatchBorderWidth: IA.Appearance.metrics.borderWidth
                resetEnabled: root.state.hasDangerCorrection || root.state.hasDangerOverride
                traceBegin: function(kind) { return root.pickerTrace(kind, AppearanceTraceBridge.Danger); }
                traceEnd: root.endPickerTrace
                onColorSelected: value => root.state.setSemanticCorrection("error", value)
                onResetRequested: root.state.resetSemanticCorrection("error")
            }
            RowLayout {
                objectName: "amberColorWellRow"
                spacing: IA.Appearance.metrics.spacingS
                IALabel { text: "Amber"; Layout.alignment: Qt.AlignVCenter }
                IAColorWell {
                    objectName: "amberColorWell"
                    label: "Amber"
                    Layout.alignment: Qt.AlignVCenter
                    colorValue: IA.Appearance.colors.warning
                    presets: root.accentPresets
                    onColorSelected: value => root.state.setSemanticCorrection("warning", value)
                    onDefaultRequested: root.state.resetSemanticCorrection("warning")
                    onCustomColorRequested: amberPicker.open()
                }
            }
            RowLayout {
                objectName: "dangerColorWellRow"
                spacing: IA.Appearance.metrics.spacingS
                IALabel { text: "Danger"; Layout.alignment: Qt.AlignVCenter }
                IAColorWell {
                    objectName: "dangerColorWell"
                    label: "Danger"
                    Layout.alignment: Qt.AlignVCenter
                    colorValue: IA.Appearance.colors.error
                    presets: root.accentPresets
                    onColorSelected: value => root.state.setSemanticCorrection("error", value)
                    onDefaultRequested: root.state.resetSemanticCorrection("error")
                    onCustomColorRequested: dangerPicker.open()
                }
            }
            IAButton {
                objectName: "savePaletteButton"
                text: "Save"
                Accessible.description: "Append the three applied root colors to the calibration text file."
                onClicked: root.state.savePaletteSample()
            }
        }
        ColumnLayout {
            objectName: "appearanceSwitchRows"
            Layout.fillWidth: true
            Layout.preferredWidth: 360
            Layout.maximumWidth: 360
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignLeading
            spacing: IA.Appearance.metrics.spacingS
            IASwitch {
                objectName: "appearanceTouch"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: "Touch targets"
                indicatorPosition: IA.Presentation.Trailing
                checked: IA.Appearance.density === "touch"
                onToggled: root.state.setAppearanceOption("density", checked ? "touch" : "compact")
            }
            IASwitch {
                objectName: "appearanceContrast"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: "7:1 preview"
                indicatorPosition: IA.Presentation.Trailing
                checked: IA.Appearance.contrast === "high"
                onToggled: root.state.setAppearanceOption("contrast", checked ? "high" : "normal")
            }
            IASwitch {
                id: inputButton
                objectName: "appearanceEditorToggle"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: "Input editor"
                indicatorPosition: IA.Presentation.Trailing
            }
            IASwitch {
                objectName: "appearanceReducedMotion"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: "Reduced motion"
                indicatorPosition: IA.Presentation.Trailing
                checked: IA.Appearance.reducedMotion
                onToggled: root.state.setAppearanceOption("reducedMotion", checked)
            }
        }
        IALabel {
            objectName: "paletteSamplePath"
            Layout.fillWidth: true
            text: "Samples: " + (root.state.paletteSamplePath || "No usable Documents destination")
            foregroundColor: IA.Appearance.colors.secondaryText
            fontPixelSize: IA.Appearance.typography.supportingSize
            wrapMode: Text.WrapAnywhere
        }
        IALabel {
            objectName: "paletteHistoryStatus"
            Layout.fillWidth: true
            visible: root.state.paletteHistoryStatus.length > 0
            text: root.state.paletteHistoryStatus
            foregroundColor: root.state.paletteHistoryAvailable ? IA.Appearance.colors.secondaryText : IA.Appearance.colors.errorText
            fontPixelSize: IA.Appearance.typography.supportingSize
            wrapMode: Text.WrapAnywhere
        }
        IALabel {
            objectName: "paletteRootExplanation"
            Layout.fillWidth: true
            visible: root.hasActionSurfaceOverrides
            text: "Pickers and Save use root Accent/Warning/Error. Explicit action-surface overrides and the local purple specimen remain independent; a sample is not every visual role."
            wrapMode: Text.Wrap
            fontPixelSize: IA.Appearance.typography.supportingSize
        }
        IALabel {
            objectName: "paletteSaveStatus"
            Layout.fillWidth: true
            visible: root.state.paletteSaveStatus.length > 0
            text: root.state.paletteSaveStatus
            foregroundColor: root.state.paletteSaveFailed ? IA.Appearance.colors.errorText : IA.Appearance.colors.secondaryText
            fontPixelSize: IA.Appearance.typography.supportingSize
            wrapMode: Text.WrapAnywhere
        }
        IAScrollView {
            id: editorScroll
            objectName: "appearanceInputScroll"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(editorBody.implicitHeight, 320)
            visible: inputButton.checked
            clip: true
            leftPadding: mirrored ? effectiveScrollBarWidth : 0
            rightPadding: mirrored ? 0 : effectiveScrollBarWidth
            contentWidth: availableWidth
            ColumnLayout {
                id: editorBody
                width: editorScroll.availableWidth
                spacing: IA.Appearance.metrics.spacingM
                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width >= 800 ? 4 : root.width >= 500 ? 2 : 1
                    rowSpacing: IA.Appearance.metrics.spacingM
                    columnSpacing: IA.Appearance.metrics.spacingM
                    IAFieldRow {
                        Layout.fillWidth: true
                        label: "Text scale"
                        IATextField {
                            id: scaleField
                            Layout.fillWidth: true
                            text: String(IA.Appearance.fontScale)
                            Accessible.name: "Text scale"
                            onAccepted: root.state.setAppearanceOption("fontScale", Number(text))
                        }
                        IAButton {
                            text: "Set"
                            Accessible.name: "Set text scale"
                            onClicked: root.state.setAppearanceOption("fontScale", Number(scaleField.text))
                        }
                    }
                    IAFieldRow {
                        Layout.fillWidth: true
                        label: "Accent"
                        description: root.accentDraft !== undefined ? "Unapplied hex edit; Set applies it." : ""
                        IATextField {
                            id: accentField
                            objectName: "accentHexField"
                            Layout.fillWidth: true
                            text: root.accentDraft !== undefined ? root.accentDraft : IA.Appearance.colors.accent.toString()
                            Accessible.name: "Accent color"
                            onTextEdited: root.accentDraft = text
                            onAccepted: root.applyAccentText()
                        }
                        IAButton {
                            objectName: "applyAccentHex"
                            text: "Set"
                            Accessible.name: "Set accent color"
                            onClicked: root.applyAccentText()
                        }
                    }
                    IAFieldRow {
                        Layout.fillWidth: true
                        label: "Control radius"
                        IATextField {
                            id: radiusField
                            Layout.fillWidth: true
                            text: String(IA.Appearance.metrics.controlRadius)
                            Accessible.name: "Control radius"
                            onAccepted: root.state.setAppearanceToken("metrics", "controlRadius", Number(text))
                        }
                        IAButton {
                            text: "Set"
                            Accessible.name: "Set control radius"
                            onClicked: root.state.setAppearanceToken("metrics", "controlRadius", Number(radiusField.text))
                        }
                    }
                    IAFieldRow {
                        Layout.fillWidth: true
                        label: "Control spacing"
                        IATextField {
                            id: spacingField
                            Layout.fillWidth: true
                            text: String(IA.Appearance.metrics.spacing)
                            Accessible.name: "Control spacing"
                            onAccepted: root.state.setAppearanceToken("metrics", "spacing", Number(text))
                        }
                        IAButton {
                            text: "Set"
                            Accessible.name: "Set control spacing"
                            onClicked: root.state.setAppearanceToken("metrics", "spacing", Number(spacingField.text))
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: IA.Appearance.metrics.spacingS
                    IAButton {
                        text: "Sans serif"
                        onClicked: root.state.setAppearanceToken("typography", "family", "Sans Serif")
                    }
                    IAButton {
                        text: "Serif"
                        onClicked: root.state.setAppearanceToken("typography", "family", "Serif")
                    }
                    IAButton {
                        text: "Partial profile"
                        onClicked: root.state.setAppearance({tokens: {colors: {accent: "#8657b8"}}})
                    }
                }
                IAFieldRow {
                    Layout.fillWidth: true
                    label: "Replacement input (JSON)"
                    description: "Load snapshot copies the current input. Apply JSON replaces it; malformed input keeps the last valid snapshot."
                    IATextField {
                        Layout.fillWidth: true
                        objectName: "appearanceJson"
                        text: root.jsonDraft
                        Accessible.name: "Appearance JSON input"
                        onTextEdited: root.jsonDraft = text
                        onAccepted: root.state.setAppearanceJson(text)
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: IA.Appearance.metrics.spacingS
                    IAButton {
                        text: "Load snapshot"
                        onClicked: root.jsonDraft = JSON.stringify(root.state.appearanceInput)
                    }
                    IAButton {
                        text: "Apply JSON"
                        onClicked: root.state.setAppearanceJson(root.jsonDraft)
                    }
                }
                IALabel {
                    Layout.fillWidth: true
                    objectName: "appearanceInspector"
                    text: "Resolved: " + IA.Appearance.scheme + ", " + IA.Appearance.density
                        + ", text " + IA.Appearance.fontScale + "x\nInput: "
                        + JSON.stringify(root.state.appearanceInput)
                    fontPixelSize: IA.Appearance.typography.supportingSize
                    foregroundColor: IA.Appearance.colors.secondaryText
                    wrapMode: Text.WrapAnywhere
                }
                IALabel {
                    Layout.fillWidth: true
                    visible: IA.Appearance.diagnostics.length > 0
                    text: JSON.stringify(IA.Appearance.diagnostics)
                    fontPixelSize: IA.Appearance.typography.supportingSize
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
        IAStatusBanner {
            Layout.fillWidth: true
            visible: root.state.appearanceError.length > 0
            title: "Input rejected"
            message: root.state.appearanceError
            validation: IA.Presentation.Invalid
        }
    }
}
