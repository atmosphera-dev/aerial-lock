// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import InterActive.Foundation as IA
import InterActive.Tests

Item {
    id: root
    width: 640
    height: 500
    property alias editor: editor
    property alias scroller: scroller
    property alias probe: counter
    property alias facade: appearance
    DocumentProbe { id: counter; document: editor.textDocument }
    IA.ControlAppearance { id: appearance; source: IA.Appearance; family: "field" }
    Flickable {
        id: scroller
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: editor.contentHeight
        TextEdit {
            id: editor
            objectName: "longDocumentEditor"
            width: scroller.width
            height: contentHeight
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.WordWrap
            selectByMouse: true
            persistentSelection: true
            cursorVisible: false
            font: counter.countValue(appearance.resolvedFont, "documentFont")
            color: counter.countValue(appearance.foregroundColor, "documentForeground")
            selectionColor: appearance.selectedBackgroundColor
            selectedTextColor: appearance.selectedForegroundColor
        }
    }
    function prepare(): void {
        editor.text = counter.longText();
        editor.insert(100, " Undo history marker.");
        editor.forceActiveFocus(Qt.OtherFocusReason);
        editor.select(12, 42);
        scroller.contentY = 1000;
    }
    function editingState(): var {
        return {cursor: editor.cursorPosition, start: editor.selectionStart,
            end: editor.selectionEnd, focus: editor.activeFocus, scroll: scroller.contentY};
    }
}
