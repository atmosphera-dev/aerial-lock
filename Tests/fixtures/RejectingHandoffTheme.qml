import QtQuick
import "../../Services" as Services
import "../../Modules/Lock" as LockModule

FocusScope {
    id: root
    required property Services.LockContext context
    readonly property var preferredTopics: []
    property int authWriterToken: field.writerToken
    function focusInput() { field.focusField() }
    LockModule.AuthField {
        id: field
        context: root.context
        width: 240
        height: 40
    }
    Connections {
        target: root.context.auth.input
        function onThemeWriterChanged() {
            if (root.context.auth.input.themeWriter === field.writerToken)
                root.authWriterToken = -1
        }
    }
}
