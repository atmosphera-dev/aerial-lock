// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import InterActive
import InterActive.Foundation as IA

Item {
    id: root
    property color colorValue: "white"
    readonly property color currentColor: editState.currentColor
    signal colorEdited(color value)
    readonly property real _planeExtent: 256
    readonly property real _railExtent: Math.max(216 * IA.Appearance.fontScale,
        4 * (modeTextMetrics.width + 2 * IA.Appearance.metrics.spacingS))
    property bool _layoutReady: false
    Component.onCompleted: _layoutReady = true
    // Allocated-width reflow starts after native deferred content is complete.
    readonly property bool _stacked: _layoutReady && width < implicitWidth
    implicitWidth: _planeExtent + IA.Appearance.metrics.spacingL + _railExtent
    implicitHeight: layout.implicitHeight
    readonly property ColorEditState _state: ColorEditState {
        id: editState
        colorValue: root.colorValue
        onColorEdited: value => root.colorEdited(value)
    }
    component Checker: Item {
        id: checker
        clip: true
        Repeater {
            model: Math.ceil(checker.width / 8) * Math.ceil(checker.height / 8)
            Rectangle {
                required property int index
                readonly property int columns: Math.max(1, Math.ceil(checker.width / 8))
                x: (index % columns) * 8
                y: Math.floor(index / columns) * 8
                width: 8; height: 8
                color: (index % columns + Math.floor(index / columns)) % 2 ? "#aaaaaa" : "white"
                Accessible.ignored: true
            }
        }
    }
    component ColorRail: IASlider {
        id: rail
        property bool spectrum: false
        property color sampleColor: "white"
        readonly property IA.ControlAppearance _visual: IA.ControlAppearance {
            id: railAppearance
            source: IA.Appearance
            family: "slider"
        }
        background: Item {
            x: rail.leftPadding; y: rail.topPadding
            width: rail.availableWidth; height: rail.availableHeight
            Item {
                anchors.centerIn: parent
                width: Math.max(0, parent.width - rail.handle.width)
                height: rail.spectrum ? IA.Appearance.metrics.trackHeight : 2 * IA.Appearance.metrics.trackHeight
                Checker { anchors.fill: parent; visible: !rail.spectrum }
                Rectangle {
                    objectName: "iaColorRailTrack"
                    anchors.fill: parent
                    LayoutMirroring.enabled: false
                    rotation: rail.mirrored ? 180 : 0
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: rail.spectrum ? "red" : Qt.rgba(rail.sampleColor.r, rail.sampleColor.g, rail.sampleColor.b, 0) }
                        GradientStop { position: 1/6; color: rail.spectrum ? "yellow" : Qt.rgba(rail.sampleColor.r, rail.sampleColor.g, rail.sampleColor.b, 1/6) }
                        GradientStop { position: 2/6; color: rail.spectrum ? "lime" : Qt.rgba(rail.sampleColor.r, rail.sampleColor.g, rail.sampleColor.b, 2/6) }
                        GradientStop { position: 3/6; color: rail.spectrum ? "cyan" : Qt.rgba(rail.sampleColor.r, rail.sampleColor.g, rail.sampleColor.b, 3/6) }
                        GradientStop { position: 4/6; color: rail.spectrum ? "blue" : Qt.rgba(rail.sampleColor.r, rail.sampleColor.g, rail.sampleColor.b, 4/6) }
                        GradientStop { position: 5/6; color: rail.spectrum ? "magenta" : Qt.rgba(rail.sampleColor.r, rail.sampleColor.g, rail.sampleColor.b, 5/6) }
                        GradientStop { position: 1; color: rail.spectrum ? "red" : Qt.rgba(rail.sampleColor.r, rail.sampleColor.g, rail.sampleColor.b, 1) }
                    }
                    // Intrinsic data artwork must retain its complete height,
                    // including when high-contrast control outlines grow.
                }
            }
        }
        handle: IA.ControlBackground {
            objectName: "iaThumb"
            resolvedAppearance: railAppearance
            implicitWidth: IA.Appearance.metrics.sliderThumbSize
            implicitHeight: implicitWidth
            radius: width / 2
            controlEnabled: rail.enabled
            down: rail.pressed
            hovered: rail.hovered
            focused: rail.visualFocus
            LayoutMirroring.enabled: false
            x: rail.leftPadding + (rail.availableWidth - width) * rail.visualPosition
            y: rail.topPadding + (rail.availableHeight - height) / 2
        }
    }
    ColumnLayout {
        id: layout
        width: root.width
        spacing: IA.Appearance.metrics.spacingM
        GridLayout {
            id: columnLayout
            Layout.fillWidth: true
            columns: root._stacked ? 1 : 2
            columnSpacing: IA.Appearance.metrics.spacingL
            rowSpacing: IA.Appearance.metrics.spacingL
            IAColorPlane {
                id: plane
                objectName: "iaColorPlane"
                Layout.alignment: Qt.AlignTop | Qt.AlignHCenter
                Layout.preferredWidth: Math.min(root._planeExtent, root.width)
                Layout.preferredHeight: Layout.preferredWidth
                hue: editState.hue / 360
                saturation: editState.saturation
                lightness: editState.lightness
                onPositionEdited: (saturation, lightness) => editState.editPlane(saturation, lightness)
            }
            ColumnLayout {
                id: rail
                Layout.fillWidth: true
                Layout.preferredWidth: root._railExtent
                Layout.minimumWidth: 0
                Layout.minimumHeight: root._stacked ? 0 : plane.height
                spacing: IA.Appearance.metrics.spacingS
                IASegmentedControl {
                    id: modes
                    objectName: "iaColorModes"
                    Layout.fillWidth: true
                    padding: 0
                    spacing: 0
                    currentIndex: editState.mode
                    onActivated: index => { editState.mode = index; }
                    IASegmentedButton { text: "Hex"; horizontalPadding: IA.Appearance.metrics.spacingS }
                    IASegmentedButton { text: "RGB"; horizontalPadding: IA.Appearance.metrics.spacingS }
                    IASegmentedButton { text: "HSV"; horizontalPadding: IA.Appearance.metrics.spacingS }
                    IASegmentedButton { text: "HSL"; horizontalPadding: IA.Appearance.metrics.spacingS }
                }
                // All modes and fields persist: visibility is not object lifetime.
                Repeater {
                    model: [
                        [{key:"hex",label:"Hex"},{key:"hexAlpha",label:"Alpha (%)"}],
                        [{key:"rgbRed",label:"Red"},{key:"rgbGreen",label:"Green"},{key:"rgbBlue",label:"Blue"},{key:"rgbAlpha",label:"Alpha (%)"}],
                        [{key:"hsvHue",label:"Hue (deg)"},{key:"hsvSaturation",label:"Saturation (%)"},{key:"hsvValue",label:"Value (%)"},{key:"hsvAlpha",label:"Alpha (%)"}],
                        [{key:"hslHue",label:"Hue (deg)"},{key:"hslSaturation",label:"Saturation (%)"},{key:"hslLightness",label:"Lightness (%)"},{key:"hslAlpha",label:"Alpha (%)"}]
                    ]
                    GridLayout {
                        id: fields
                        required property int index
                        required property var modelData
                        Layout.fillWidth: true
                        visible: editState.mode === index
                        columns: width >= 210 * IA.Appearance.fontScale && (index !== 0 || IA.Appearance.density === "touch") ? 2 : 1
                        columnSpacing: IA.Appearance.metrics.spacingS
                        rowSpacing: IA.Appearance.metrics.spacingS
                        Repeater {
                            model: fields.modelData
                            ColumnLayout {
                                id: field
                                required property var modelData
                                readonly property string key: modelData.key
                                readonly property string error: editState.fields[key].error
                                readonly property bool pending: editState.fields[key].pending
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: IA.Appearance.metrics.spacingXS
                                IALabel {
                                    Layout.fillWidth: true
                                    text: field.modelData.label
                                    fontPixelSize: IA.Appearance.typography.supportingSize
                                    elide: Text.ElideRight
                                }
                                IATextField {
                                    id: input
                                    objectName: "iaColor_" + field.key
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: editState.fields[field.key].text
                                    validation: field.error.length ? IA.Presentation.Invalid : IA.Presentation.None
                                    Accessible.name: field.key === "hex" ? "Hex RGB or ARGB" : ["Hex", "RGB", "HSV", "HSL"][fields.index] + " " + field.modelData.label
                                    Accessible.description: field.error.length ? field.error : field.pending ? "Unapplied draft" : "Applied color channel"
                                    onTextEdited: editState.setDraft(field.key, text)
                                    onEditingFinished: editState.commit(field.key)
                                }
                                IALabel {
                                    Layout.fillWidth: true
                                    visible: field.pending
                                    text: field.error.length ? field.error : "Unapplied draft"
                                    fontPixelSize: IA.Appearance.typography.supportingSize
                                    foregroundColor: field.error.length ? IA.Appearance.colors.errorText : IA.Appearance.colors.text
                                    elide: Text.ElideRight
                                    Accessible.name: text
                                }
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true; visible: !root._stacked }
                Rectangle {
                    objectName: "iaColorPreview"
                    Layout.fillWidth: true
                    implicitHeight: preview.implicitHeight + 2 * IA.Appearance.metrics.spacingS
                    color: "transparent"
                    border.color: IA.Appearance.colors.separator
                    border.width: IA.Appearance.metrics.borderWidth
                    radius: IA.Appearance.metrics.controlRadius
                    RowLayout {
                        id: preview
                        anchors.fill: parent
                        anchors.margins: IA.Appearance.metrics.spacingS
                        spacing: IA.Appearance.metrics.spacingM
                        Checker {
                            Layout.preferredWidth: IA.Appearance.metrics.controlHeight + IA.Appearance.metrics.spacingS
                            Layout.preferredHeight: Layout.preferredWidth
                            Rectangle { objectName: "iaColorPreviewSample"; anchors.fill: parent; color: editState.currentColor }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: IA.Appearance.metrics.spacingXS
                            IALabel { Layout.fillWidth: true; text: "Selected color"; fontPixelSize: IA.Appearance.typography.supportingSize; elide: Text.ElideRight }
                            IALabel { objectName: "iaColorArgb"; Layout.fillWidth: true; text: editState.argb; fontPixelSize: IA.Appearance.typography.supportingSize; elide: Text.ElideRight; Accessible.name: "Applied ARGB " + text }
                        }
                    }
                }
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: IA.Appearance.metrics.spacingS
            rowSpacing: IA.Appearance.metrics.spacingS
            IALabel { id: hueLabel; objectName: "iaHueLabel"; text: "Hue"; Layout.preferredWidth: Math.max(hueLabel.implicitWidth, opacityLabel.implicitWidth) }
            ColorRail {
                id: hue
                objectName: "hueSlider"
                Layout.fillWidth: true
                spectrum: true
                from: 0; to: 360
                stepSize: 1
                value: editState.hue
                Accessible.name: "Hue in degrees"
                onMoved: editState.editHue(value)
            }
            IALabel { id: hueValue; objectName: "iaHueValue"; text: Number(editState.hue.toFixed(1)) + " deg"; Layout.preferredWidth: Math.max(hueValueMetrics.advanceWidth, alphaValueMetrics.advanceWidth); horizontalAlignment: Text.AlignRight }
            IALabel { id: opacityLabel; objectName: "iaOpacityLabel"; text: "Opacity"; Layout.preferredWidth: Math.max(hueLabel.implicitWidth, opacityLabel.implicitWidth) }
            ColorRail {
                id: opacity
                objectName: "alphaSlider"
                Layout.fillWidth: true
                sampleColor: editState.currentColor
                from: 0; to: 1
                stepSize: 0.01
                value: editState.opacity
                Accessible.name: "Opacity"
                onMoved: editState.editOpacity(value)
            }
            IALabel { objectName: "iaOpacityValue"; text: Number((editState.opacity * 100).toFixed(1)) + "%"; Layout.preferredWidth: Math.max(hueValueMetrics.advanceWidth, alphaValueMetrics.advanceWidth); horizontalAlignment: Text.AlignRight }
        }
    }
    readonly property TextMetrics _hueValueMetrics: TextMetrics { id: hueValueMetrics; font: hueValue.font; text: "360.0 deg" }
    readonly property TextMetrics _alphaValueMetrics: TextMetrics { id: alphaValueMetrics; font: hueValueMetrics.font; text: "100.0%" }
    readonly property TextMetrics _modeTextMetrics: TextMetrics { id: modeTextMetrics; font: modes.font; text: "RGB" }
}
