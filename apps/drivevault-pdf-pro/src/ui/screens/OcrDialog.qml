import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

Popup {
    id: dlg
    objectName: "ocrDialog"
    property int currentPage: 0
    property bool ready: false
    property bool running: false
    property string error: ""
    property string result: ""
    property string summary: ""
    readonly property string pages: pageMode.currentIndex === 0 ? "all" : pageMode.currentIndex === 1 ? "current" : range.text
    signal openResult(string path)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    width: Math.min(520, parent.width - 48)
    padding: 24
    closePolicy: running ? Popup.NoAutoClose : Popup.CloseOnEscape
    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }
    onAboutToShow: {
        ready = Doc.ocrAvailable()
        error = ready ? "" : "ocrModelRequired"
        result = ""
        summary = ""
        pageMode.currentIndex = 0
        range.clear()
    }
    Connections {
        target: Doc
        function onThaiOcrReport(recognized, skipped, unrecognized) {
            if (dlg.running)
                dlg.summary = Tr.f2("ocrPageSummary", recognized, skipped).replace("%3", unrecognized)
        }
        function onOutputFinished(code, path) {
            if (dlg.running) {
                dlg.running = false
                dlg.error = code
                dlg.result = path
            }
        }
    }
    contentItem: ColumnLayout {
        spacing: 12
        AppText { text: Tr.s("thaiOcr"); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold }
        AppText { Layout.fillWidth: true; text: Tr.s("ocrDescription"); wrapMode: Text.Wrap; color: Theme.textSecondary }
        ComboBox {
            id: pageMode
            Layout.fillWidth: true
            enabled: dlg.ready && !dlg.running
            model: [Tr.s("allPages"), Tr.f("currentPageN", dlg.currentPage + 1), Tr.s("pageRange")]
        }
        TextField {
            id: range
            Layout.fillWidth: true
            visible: pageMode.currentIndex === 2
            enabled: !dlg.running
            placeholderText: Tr.s("rangeHint")
        }
        ProgressBar { Layout.fillWidth: true; visible: dlg.running; value: Doc.outputProgress }
        AppText {
            objectName: "ocrSummary"
            Layout.fillWidth: true
            visible: !dlg.running && dlg.summary !== ""
            text: dlg.summary
            wrapMode: Text.Wrap
            color: Theme.textSecondary
        }
        AppText {
            Layout.fillWidth: true
            visible: dlg.error !== "" || dlg.result !== ""
            text: dlg.error !== "" ? Tr.outputError(dlg.error) : Tr.f("outputSaved", dlg.result)
            color: dlg.error !== "" ? Theme.errorSoft : Theme.success
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PButton {
                variant: "ghost"
                text: dlg.running ? Tr.s("cancel") : Tr.s("close")
                onClicked: dlg.running ? Doc.cancelOutput() : dlg.close()
            }
            PButton {
                objectName: "ocrSave"
                variant: "primary"
                text: dlg.result !== "" ? Tr.s("openFile") : Tr.s("ocrSave")
                enabled: dlg.ready && !Doc.outputBusy
                onClicked: {
                    if (dlg.result !== "") {
                        dlg.close()
                        dlg.openResult(dlg.result)
                    } else if (Doc.imagePageCount(dlg.pages, dlg.currentPage) === 0) {
                        dlg.error = "range"
                    } else {
                        saveDialog.open()
                    }
                }
            }
        }
    }
    FileDialog {
        id: saveDialog
        title: Tr.s("ocrSave")
        fileMode: FileDialog.SaveFile
        options: FileDialog.DontConfirmOverwrite
        defaultSuffix: "pdf"
        nameFilters: [Tr.s("pdfFiles")]
        onAccepted: {
            dlg.summary = ""
            dlg.error = Doc.startThaiOcr(dlg.pages, dlg.currentPage, selectedFile.toString())
            dlg.running = dlg.error === ""
        }
    }
}
