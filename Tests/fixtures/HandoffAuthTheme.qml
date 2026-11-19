import QtQuick
import "../../Services" as Services
import "../../Modules/Lock" as LockModule

FocusScope {
    id: root
    required property Services.LockContext context
    readonly property int authWriterToken: field.writerToken
    function focusInput() { field.focusField() }
    LockModule.AuthField {
        id: field
        objectName: "candidateAuthField"
        context: root.context
        width: 240
        height: 40
    }
}
