import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

Popup {
    id: dlg
    objectName: "replaceTextDialog"
    property var location: null
    property string original: ""
    property string error: ""
    property string result: ""
    property bool running: false
    signal openResult(string path)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    width: Math.min(540, parent.width - 48)
    padding: 24
    closePolicy: running ? Popup.NoAutoClose : Popup.CloseOnEscape
    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    function begin(point, text) {
        location = point
        original = text
        replacement.text = text
        removeText.checked = false
        error = ""
        result = ""
        open()
    }
    function save(path) {
        dlg.error = Doc.startTextReplacement(dlg.location.page, dlg.location.x, dlg.location.y,
                                             dlg.original, replacement.text, path, removeText.checked)
        dlg.running = dlg.error === ""
    }
    onOpened: { replacement.forceActiveFocus(); replacement.selectAll() }
    Connections {
        target: Doc
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
        AppText { text: Tr.s("editExisting"); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold }
        AppText { Layout.fillWidth: true; text: Tr.s("editRunHelp"); wrapMode: Text.Wrap; color: Theme.textSecondary }
        TextField {
            id: replacement
            objectName: "replacementText"
            Layout.fillWidth: true
            enabled: !dlg.running && dlg.result === "" && !removeText.checked
            maximumLength: 512
            selectByMouse: true
        }
        CheckBox {
            id: removeText
            objectName: "removeTextRun"
            text: Tr.s("removeTextRun")
            enabled: !dlg.running && dlg.result === ""
        }
        ProgressBar { Layout.fillWidth: true; visible: dlg.running; indeterminate: true }
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
            PButton { variant: "ghost"; text: Tr.s("close"); enabled: !dlg.running; onClicked: dlg.close() }
            PButton {
                variant: "primary"
                text: dlg.result !== "" ? Tr.s("openFile") : Tr.s("editSave")
                enabled: !Doc.outputBusy
                    && (dlg.result !== "" || removeText.checked
                        || (replacement.text.trim() !== "" && replacement.text.trim() !== dlg.original))
                onClicked: {
                    if (dlg.result !== "") {
                        dlg.close()
                        dlg.openResult(dlg.result)
                    } else {
                        saveDialog.open()
                    }
                }
            }
        }
    }
    FileDialog {
        id: saveDialog
        title: Tr.s("editSave")
        fileMode: FileDialog.SaveFile
        options: FileDialog.DontConfirmOverwrite
        defaultSuffix: "pdf"
        nameFilters: [Tr.s("pdfFiles")]
        onAccepted: dlg.save(selectedFile.toString())
    }
}
