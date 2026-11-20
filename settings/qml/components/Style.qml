pragma Singleton
import QtQuick
QtObject {
    readonly property SystemPalette colors: SystemPalette { colorGroup: SystemPalette.Active }
    readonly property color window: colors.window
    readonly property color card: colors.base
    readonly property color text: colors.windowText
    readonly property color muted: colors.placeholderText
    readonly property color line: colors.mid
    readonly property color accent: colors.accent
    readonly property color accentText: colors.highlightedText
    readonly property color selection: colors.highlight
    readonly property color selectionText: colors.highlightedText
}
