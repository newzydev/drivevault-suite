import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

Popup {
    id: dlg
    objectName: "securityDialog"
    property bool unlock: false
    property string error: ""
    property string result: ""
    readonly property string title: Tr.s(unlock ? "unlockPdf" : "protectPdf")
    signal openResultRequested(string path)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: Math.min(560, parent.width - 48)
    padding: 24
    closePolicy: Doc.outputBusy ? Popup.NoAutoClose : Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#B3000000" }
    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    function clearPasswords() {
        owner.clear()
        user.clear()
        confirmOwner.clear()
        confirmUser.clear()
    }
    function save(path) {
        result = ""
        error = Doc.startSecuritySave({
            unlock: unlock, authorized: consent.checked, owner: owner.text, user: unlock ? "" : user.text,
            print: allowPrint.checked, copy: allowCopy.checked, modify: allowModify.checked,
            annotate: allowAnnotate.checked
        }, path)
        clearPasswords()
    }
    onAboutToShow: {
        error = ""
        result = ""
        consent.checked = false
        allowPrint.checked = true
        allowCopy.checked = true
        allowModify.checked = true
        allowAnnotate.checked = true
    }
    onClosed: clearPasswords()

    Connections {
        target: Doc
        function onOutputFinished(code, path) {
            if (dlg.opened) {
                dlg.error = code
                dlg.result = path
            }
        }
    }
    contentItem: ColumnLayout {
        spacing: 8
        AppText { text: dlg.title; font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold }
        AppText { Layout.fillWidth: true; text: Tr.s("outputSavedSource"); color: Theme.textSecondary; wrapMode: Text.Wrap }
        AppText {
            Layout.fillWidth: true
            text: Tr.s(dlg.unlock ? "unlockHint" : "protectHint")
            wrapMode: Text.Wrap
            color: Theme.textSecondary
        }
        ColumnLayout {
            Layout.fillWidth: true
            enabled: !Doc.outputBusy
            PTextField {
                id: user
                objectName: "securityUser"
                visible: !dlg.unlock
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: Tr.s("userPasswordOptional")
                Accessible.name: placeholderText
            }
            PTextField {
                id: confirmUser
                visible: !dlg.unlock && user.text !== ""
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: Tr.s("repeatUserPassword")
                Accessible.name: placeholderText
            }
            PTextField {
                id: owner
                objectName: "securityPassword"
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: Tr.s(dlg.unlock ? "unlockPassword" : "ownerPassword")
                Accessible.name: placeholderText
            }
            PTextField {
                id: confirmOwner
                visible: !dlg.unlock
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: Tr.s("repeatOwnerPassword")
                Accessible.name: placeholderText
            }
            GridLayout {
                columns: 2
                visible: !dlg.unlock
                PCheckBox { id: allowPrint; text: Tr.s("allowPrint") }
                PCheckBox { id: allowCopy; text: Tr.s("allowCopy") }
                PCheckBox { id: allowModify; text: Tr.s("allowModify") }
                PCheckBox { id: allowAnnotate; text: Tr.s("allowAnnotate") }
            }
            PCheckBox {
                id: consent
                objectName: "securityConsent"
                visible: dlg.unlock
                Layout.fillWidth: true
                text: Tr.s("authorization")
                wrapLabel: true

            }
        }
        AppText {
            Layout.fillWidth: true
            visible: dlg.error !== "" || dlg.result !== ""
            text: dlg.error !== "" ? Tr.outputError(dlg.error) : Tr.f("outputSaved", dlg.result)
            color: dlg.error !== "" ? Theme.errorSoft : Theme.success
            wrapMode: Text.WrapAnywhere
        }
        ProgressBar { Layout.fillWidth: true; visible: Doc.outputBusy; indeterminate: true }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PButton { objectName: "securityClose"; variant: "ghost"; text: Tr.s("close"); enabled: !Doc.outputBusy; onClicked: dlg.close() }
            PButton {
                visible: dlg.result !== ""
                text: Tr.s("openResult")
                onClicked: { dlg.close(); dlg.openResultRequested(dlg.result) }
            }
            PButton {
                objectName: "securitySave"
                variant: "primary"
                text: dlg.title
                enabled: !Doc.outputBusy && owner.text !== "" && (!dlg.unlock || consent.checked)
                onClicked: {
                    dlg.error = ""
                    dlg.result = ""
                    if (!dlg.unlock && (confirmOwner.text !== owner.text || (user.text !== "" && confirmUser.text !== user.text)))
                        dlg.error = "passwordMismatch"
                    else
                        chooseFile.open()
                }
            }
        }
    }
    FileDialog {
        id: chooseFile
        title: dlg.title
        fileMode: FileDialog.SaveFile
        options: FileDialog.DontConfirmOverwrite
        nameFilters: [Tr.s("pdfFiles")]
        defaultSuffix: "pdf"
        onAccepted: dlg.save(selectedFile.toString())
    }
}
