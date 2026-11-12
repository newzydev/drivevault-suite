import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// Accessibility check: the things a screen reader starts from, and a copy with the title and the language set. It is a quick check, not a full PDF/UA test.
Popup {
    id: dlg
    objectName: "accessibilityDialog"

    property string message: ""
    property bool messageIsError: false
    property string result: ""
    property bool askReplace: false
    property string pendingPath: ""
    readonly property var languages: [["", "-"], ["en-US", "English"], ["th-TH", "ไทย (Thai)"], ["ja-JP", "日本語"], ["zh-CN", "中文"], ["de-DE", "Deutsch"], ["fr-FR", "Français"]]
    signal openResultRequested(string path)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    width: Math.min(700, (parent ? parent.width : 700) - 48)
    height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, (parent ? parent.height : 700) - 48)
    padding: 24
    closePolicy: Access.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#B3000000" }
    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    function fail(text) { message = text; messageIsError = true }
    function begin() {
        message = ""
        result = ""
        askReplace = false
        titleField.text = ""
        langBox.currentIndex = 0
        Access.check()
        open()
    }
    function saveTo(path, overwrite) {
        message = ""
        result = ""
        const code = Access.saveInfo(titleField.text, languages[langBox.currentIndex][0], path, overwrite)
        if (code === "exists") {
            pendingPath = path
            askReplace = true
        } else if (code !== "") {
            fail(Tr.s(code === "sameFile" ? "errSameFile" : code === "nothing" ? "accNothing" : "busyNow"))
        }
    }
    function statusColor(s) { return s === "pass" ? Theme.success : s === "fail" ? Theme.errorSoft : s === "warn" ? Theme.warning : Theme.textSecondary }
    function statusMark(s) { return s === "pass" ? "✓" : s === "fail" ? "✕" : s === "warn" ? "!" : s === "info" ? "i" : "–" }
    function checkText(c) {
        let t = Tr.s("accCheck_" + c.id + "_" + c.status)
        if (c.count > 0)
            t = t.replace("%1", c.count)
        if (c.detail !== "" && (c.id === "title" || c.id === "language"))
            t += "  (" + c.detail + ")"
        return t
    }

    Connections {
        target: Access
        function onSaveFinished(ok, err, detail, path) {
            if (ok) {
                dlg.result = path
                dlg.messageIsError = false
                dlg.message = Tr.s("accSaved")
            } else {
                const names = { file: "errFile", format: "errFormat", password: "errPasswordFile", write: "errWrite" }
                dlg.fail(Tr.s(names[err] !== undefined ? names[err] : "errSaveUnknown"))
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 10
        AppText { Layout.fillWidth: true; text: Tr.s("accTitle") + (Doc.isOpen ? " - " + Doc.fileName : ""); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold; elide: Text.ElideMiddle }
        AppText { Layout.fillWidth: true; text: Tr.s("accHint"); color: Theme.textDisabled; font.pixelSize: Theme.fontSm; wrapMode: Text.Wrap }
        AppText { objectName: "accChecking"; visible: Access.busy && !Access.loaded; text: Tr.s("accChecking"); color: Theme.textSecondary }
        AppText { visible: Access.error !== ""; text: Access.error === "password" ? Tr.s("errPasswordFile") : Tr.s("errFile"); color: Theme.warning; wrapMode: Text.Wrap; Layout.fillWidth: true }

        Repeater {
            model: Access.checks
            delegate: RowLayout {
                id: row
                required property var modelData
                required property int index
                objectName: "accRow_" + modelData.id
                Layout.fillWidth: true
                spacing: 10
                AppText { objectName: "accMark_" + row.modelData.id; Layout.preferredWidth: 18; horizontalAlignment: Text.AlignHCenter; text: dlg.statusMark(row.modelData.status); color: dlg.statusColor(row.modelData.status); font.weight: Font.Bold }
                AppText { Layout.fillWidth: true; text: dlg.checkText(row.modelData); wrapMode: Text.Wrap; color: row.modelData.status === "na" ? Theme.textDisabled : Theme.text }
            }
        }
        AppText { Layout.fillWidth: true; visible: Access.loaded; text: Tr.s("accNotFull"); color: Theme.textDisabled; font.pixelSize: Theme.fontSm; wrapMode: Text.Wrap }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle; visible: Access.loaded }

        // ---- fix: title and language into a copy
        AppText { visible: Access.loaded; text: Tr.s("accFixTitle"); color: Theme.textSecondary; font.pixelSize: Theme.fontSm }
        PTextField { id: titleField; objectName: "accTitleField"; visible: Access.loaded; Layout.fillWidth: true; placeholderText: Access.title !== "" ? Access.title : Tr.s("accTitlePlaceholder") }
        RowLayout {
            visible: Access.loaded
            Layout.fillWidth: true
            spacing: 8
            AppText { text: Tr.s("accLanguage"); color: Theme.textSecondary }
            ComboBox {
                id: langBox
                objectName: "accLanguageBox"
                Layout.preferredWidth: 220
                implicitHeight: 36
                model: dlg.languages.map(l => l[1])
                font.pixelSize: Theme.fontMd
                contentItem: AppText { leftPadding: 10; text: langBox.displayText; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: 8; color: Theme.surface; border.color: langBox.activeFocus ? Theme.accent : Theme.border }
            }
            Item { Layout.fillWidth: true }
        }
        AppText { objectName: "accMessage"; Layout.fillWidth: true; visible: dlg.message !== ""; text: (dlg.result !== "" ? Tr.s("saved") + " " + dlg.result + "  " : "") + dlg.message; color: dlg.messageIsError ? Theme.errorSoft : Theme.success; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            visible: dlg.askReplace
            AppText { Layout.fillWidth: true; text: Tr.f("replaceAsk", dlg.pendingPath.split(/[\\/]/).pop()); color: Theme.warning; wrapMode: Text.Wrap }
            PButton { compact: true; variant: "ghost"; text: Tr.s("cancel"); onClicked: dlg.askReplace = false }
            PButton { objectName: "accReplaceYes"; compact: true; text: Tr.s("replace"); onClicked: { dlg.askReplace = false; dlg.saveTo(dlg.pendingPath, true) } }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item { Layout.fillWidth: true }
            PButton { objectName: "accOpenResult"; visible: dlg.result !== ""; text: Tr.s("openResult"); onClicked: { const p = dlg.result; dlg.close(); dlg.openResultRequested(p) } }
            PButton { objectName: "accClose"; variant: "ghost"; text: Tr.s("close"); enabled: !Access.busy; onClicked: dlg.close() }
            PButton {
                objectName: "accSave"
                variant: "primary"
                visible: Access.loaded
                text: Access.busy ? Tr.s("saving") : Tr.s("accSaveAs")
                enabled: !Access.busy && (titleField.text.trim() !== "" || langBox.currentIndex > 0)
                onClicked: saveDialog.open()
            }
        }
    }

    FileDialog {
        id: saveDialog
        title: Tr.s("accSaveAs")
        nameFilters: [Tr.s("pdfFiles")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        currentFolder: Doc.isOpen ? "file:///" + Doc.filePath.replace(/\\/g, "/").replace(/\/[^\/]*$/, "") : ""
        onAccepted: dlg.saveTo(Doc.localPath(selectedFile.toString()), false)
    }
}
