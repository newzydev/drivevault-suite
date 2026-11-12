import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// Digital signatures: the signatures of the open file with their checks, and signing a COPY with a certificate file (.pfx / .p12).
// The open file is never changed. The signature is invisible; signing a file that already has a signature is refused (it would break it).
Popup {
    id: dlg
    objectName: "signatureDialog"

    property string pfx: ""
    property string message: ""
    property bool messageIsError: false
    property string result: ""
    property bool askReplace: false
    property string pendingPath: ""
    signal openResultRequested(string path)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    width: Math.min(700, (parent ? parent.width : 700) - 48)
    height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, (parent ? parent.height : 700) - 48)
    padding: 24
    closePolicy: Signatures.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#B3000000" }
    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    function fail(text) { message = text; messageIsError = true }
    function begin() {
        pfx = ""
        message = ""
        result = ""
        askReplace = false
        passwordField.text = ""
        Signatures.check()
        open()
    }
    function signTo(path, overwrite) {
        message = ""
        result = ""
        const code = Signatures.sign({ pfx: pfx, pfxPassword: passwordField.text, reason: reasonField.text, location: locationField.text }, path, overwrite)
        if (code === "exists") {
            pendingPath = path
            askReplace = true
        } else if (code !== "") {
            fail(Tr.s(code === "noCertificate" ? "sigNoCertificate" : code === "sameFile" ? "errSameFile" : "busyNow"))
        }
    }
    function trustText(t) { return Tr.s("sigTrust_" + (t === "" ? "unknown" : t)) }

    Connections {
        target: Signatures
        function onSignFinished(ok, err, detail, signer, path) {
            if (ok) {
                dlg.result = path
                dlg.messageIsError = false
                dlg.message = Tr.f("sigSigned", signer)
            } else {
                const names = { file: "errFile", format: "errFormat", password: "sigErrPassword", encrypted: "sigErrEncrypted", alreadySigned: "sigErrAlready",
                                badPfx: "sigErrBadPfx", wrongPfxPassword: "sigErrWrongPfxPassword", noKey: "sigErrNoKey", tooLarge: "sigErrTooLarge", write: "errWrite" }
                dlg.fail(Tr.s(names[err] !== undefined ? names[err] : "errSaveUnknown"))
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 10
        AppText { Layout.fillWidth: true; text: Tr.s("sigTitle") + (Doc.isOpen ? " - " + Doc.fileName : ""); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold; elide: Text.ElideMiddle }

        // ---- the signatures of this file
        AppText { text: Tr.s("sigThisFile"); color: Theme.textSecondary; font.pixelSize: Theme.fontSm }
        AppText { objectName: "sigChecking"; visible: Signatures.busy && !Signatures.loaded; text: Tr.s("sigChecking"); color: Theme.textSecondary }
        AppText { objectName: "sigNone"; visible: Signatures.loaded && Signatures.signatures.length === 0; text: Tr.s("sigNone"); color: Theme.textSecondary }
        AppText { visible: Signatures.error === "password"; text: Tr.s("sigErrPassword"); color: Theme.warning; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Repeater {
            model: Signatures.signatures
            delegate: Rectangle {
                id: card
                required property var modelData
                required property int index
                objectName: "sigCard_" + index
                Layout.fillWidth: true
                implicitHeight: col.implicitHeight + 16
                radius: 8
                color: Theme.surface
                border.color: modelData.intact && modelData.wholeFile ? Qt.alpha(Theme.success, 0.6) : Qt.alpha(Theme.warning, 0.7)
                ColumnLayout {
                    id: col
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 2
                    AppText { Layout.fillWidth: true; text: (card.modelData.signer !== "" ? card.modelData.signer : Tr.s("sigUnknownSigner")) + (card.modelData.field !== "" ? "   ·   " + card.modelData.field : ""); font.weight: Font.DemiBold; elide: Text.ElideRight }
                    AppText {
                        objectName: "sigStatus_" + card.index
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: card.modelData.intact && card.modelData.wholeFile ? Theme.success : Theme.warning
                        text: card.modelData.error !== "" ? Tr.s("sigError_" + card.modelData.error)
                              : card.modelData.wholeFile ? Tr.s("sigIntact") : Tr.s("sigChangedAfter")
                    }
                    AppText { Layout.fillWidth: true; visible: card.modelData.trust !== ""; text: dlg.trustText(card.modelData.trust); color: card.modelData.trust === "trusted" ? Theme.success : Theme.textSecondary; font.pixelSize: Theme.fontSm; wrapMode: Text.Wrap }
                    AppText { Layout.fillWidth: true; visible: card.modelData.reason !== "" || card.modelData.location !== ""; text: [card.modelData.reason, card.modelData.location].filter(s => s !== "").join("  ·  "); color: Theme.textSecondary; font.pixelSize: Theme.fontSm; elide: Text.ElideRight }
                }
            }
        }
        AppText { Layout.fillWidth: true; text: Tr.s("sigTrustNote"); color: Theme.textDisabled; font.pixelSize: Theme.fontSm; wrapMode: Text.Wrap }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }

        // ---- sign a copy
        AppText { text: Tr.s("sigSignCopy"); color: Theme.textSecondary; font.pixelSize: Theme.fontSm }
        AppText { Layout.fillWidth: true; text: Tr.s("sigSignHint"); color: Theme.textDisabled; font.pixelSize: Theme.fontSm; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PButton { objectName: "sigPickPfx"; text: Tr.s("sigPickPfx"); enabled: !Signatures.busy; onClicked: pfxDialog.open() }
            AppText { objectName: "sigPfxName"; Layout.fillWidth: true; text: dlg.pfx === "" ? Tr.s("sigNoPfx") : Doc.localPath(dlg.pfx).split(/[\\/]/).pop(); color: dlg.pfx === "" ? Theme.textDisabled : Theme.text; elide: Text.ElideMiddle }
        }
        PTextField { id: passwordField; objectName: "sigPfxPassword"; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: Tr.s("sigPfxPassword") }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PTextField { id: reasonField; objectName: "sigReason"; Layout.fillWidth: true; placeholderText: Tr.s("sigReason") }
            PTextField { id: locationField; objectName: "sigLocation"; Layout.fillWidth: true; placeholderText: Tr.s("sigLocation") }
        }
        AppText { objectName: "sigMessage"; Layout.fillWidth: true; visible: dlg.message !== ""; text: (dlg.result !== "" ? Tr.s("saved") + " " + dlg.result + "  " : "") + dlg.message; color: dlg.messageIsError ? Theme.errorSoft : Theme.success; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            visible: dlg.askReplace
            AppText { Layout.fillWidth: true; text: Tr.f("replaceAsk", dlg.pendingPath.split(/[\\/]/).pop()); color: Theme.warning; wrapMode: Text.Wrap }
            PButton { compact: true; variant: "ghost"; text: Tr.s("cancel"); onClicked: dlg.askReplace = false }
            PButton { objectName: "sigReplaceYes"; compact: true; text: Tr.s("replace"); onClicked: { dlg.askReplace = false; dlg.signTo(dlg.pendingPath, true) } }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item { Layout.fillWidth: true }
            PButton { objectName: "sigOpenResult"; visible: dlg.result !== ""; text: Tr.s("openResult"); onClicked: { const p = dlg.result; dlg.close(); dlg.openResultRequested(p) } }
            PButton { objectName: "sigClose"; variant: "ghost"; text: Tr.s("close"); enabled: !Signatures.busy; onClicked: dlg.close() }
            PButton { objectName: "sigSign"; variant: "primary"; text: Signatures.busy ? Tr.s("saving") : Tr.s("sigSignAs"); enabled: !Signatures.busy && dlg.pfx !== ""; onClicked: saveDialog.open() }
        }
    }

    FileDialog {
        id: pfxDialog
        title: Tr.s("sigPickPfx")
        nameFilters: [Tr.s("sigPfxFiles")]
        onAccepted: dlg.pfx = selectedFile.toString()
    }
    FileDialog {
        id: saveDialog
        title: Tr.s("sigSignAs")
        nameFilters: [Tr.s("pdfFiles")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        currentFolder: Doc.isOpen ? "file:///" + Doc.filePath.replace(/\\/g, "/").replace(/\/[^\/]*$/, "") : ""
        onAccepted: dlg.signTo(Doc.localPath(selectedFile.toString()), false)
    }
}
