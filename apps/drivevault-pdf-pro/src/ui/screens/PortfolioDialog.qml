import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// Create a PDF portfolio: files of any type embedded in one new PDF. The files are only read.
Popup {
    id: dlg
    objectName: "portfolioDialog"
    property bool creating: false
    property string error: ""
    signal openResult(string path)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: Math.min(630, parent.width - 48)
    padding: 20
    closePolicy: creating ? Popup.NoAutoClose : Popup.CloseOnEscape
    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    function begin() {
        files.clear()
        nameField.text = ""
        error = ""
        open()
    }
    function addFiles(urls) {
        for (const u of urls) {
            const path = Doc.localPath(u.toString())
            let known = false
            for (let i = 0; i < files.count; ++i)
                known = known || files.get(i).path === path
            if (!known)
                files.append({ path: path })
        }
    }
    function save(url) {
        const list = []
        for (let i = 0; i < files.count; ++i)
            list.push(files.get(i).path)
        error = Doc.startPortfolio(list, nameField.text, url)
        creating = error === ""
    }
    function errorText(code) {
        return code.indexOf("fileRead:") === 0 ? Tr.f("portfolioFileRead", code.substring(9)) : Tr.outputError(code)
    }

    ListModel { id: files }
    Connections {
        target: Doc
        function onOutputFinished(error, path) {
            if (!dlg.creating)
                return
            dlg.creating = false
            dlg.error = error
            if (error === "") {
                dlg.close()
                dlg.openResult(path)
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 8
        AppText { text: Tr.s("portfolioTitle"); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold }
        AppText { Layout.fillWidth: true; text: Tr.s("portfolioHint"); color: Theme.textDisabled; font.pixelSize: Theme.fontSm; wrapMode: Text.Wrap }
        PTextField { id: nameField; objectName: "portfolioName"; Layout.fillWidth: true; enabled: !Doc.outputBusy; placeholderText: Tr.s("portfolioName") }
        PButton { objectName: "portfolioAdd"; enabled: !Doc.outputBusy; text: Tr.s("portfolioAddFiles"); onClicked: chooseFiles.open() }
        AppText { visible: files.count === 0; text: Tr.s("portfolioEmpty"); color: Theme.textSecondary }
        ListView {
            objectName: "portfolioList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(200, count * 34)
            visible: files.count > 0
            enabled: !Doc.outputBusy
            clip: true
            model: files
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                required property string path
                required property int index
                width: ListView.view.width - 12
                height: 34
                AppText { Layout.fillWidth: true; text: path.split(/[\\/]/).pop(); elide: Text.ElideMiddle; textFormat: Text.PlainText }
                PButton { compact: true; variant: "ghost"; text: "↑"; tip: Tr.s("createMoveUp"); enabled: index > 0; onClicked: files.move(index, index - 1, 1) }
                PButton { compact: true; variant: "ghost"; text: "↓"; tip: Tr.s("createMoveDown"); enabled: index + 1 < files.count; onClicked: files.move(index, index + 1, 1) }
                PButton { compact: true; variant: "ghost"; glyph: "close"; tip: Tr.s("removeRecent"); onClicked: files.remove(index) }
            }
        }
        ProgressBar { Layout.fillWidth: true; visible: dlg.creating; value: Doc.outputProgress }
        AppText { objectName: "portfolioError"; Layout.fillWidth: true; visible: dlg.error !== ""; text: dlg.errorText(dlg.error); wrapMode: Text.Wrap; color: Theme.errorSoft }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PButton { objectName: "portfolioClose"; text: Tr.s(dlg.creating ? "cancel" : "close"); onClicked: dlg.creating ? Doc.cancelOutput() : dlg.close() }
            PButton {
                objectName: "portfolioSave"
                variant: "primary"
                text: Tr.s("saveAs")
                enabled: !Doc.outputBusy && files.count > 0
                onClicked: chooseOutput.open()
            }
        }
    }
    FileDialog {
        id: chooseFiles
        title: Tr.s("portfolioAddFiles")
        fileMode: FileDialog.OpenFiles
        onAccepted: dlg.addFiles(selectedFiles)
    }
    FileDialog {
        id: chooseOutput
        title: Tr.s("saveAs")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: [Tr.s("pdfFiles")]
        onAccepted: dlg.save(selectedFile.toString())
    }
}
