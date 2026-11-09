import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

Popup {
    id: dlg
    objectName: "imageExportDialog"
    property int currentPage: 0
    property string error: ""
    property string result: ""
    property bool cancelling: false
    readonly property string pages: pageMode.currentIndex === 0 ? "all" : pageMode.currentIndex === 1 ? "current" : range.text
    readonly property int count: Doc.pageCount > 0 ? Doc.imagePageCount(pages, currentPage) : 0
    readonly property bool jpeg: format.currentIndex === 1
    readonly property bool folder: count > 1 && target.currentIndex === 1
    readonly property string extension: count > 1 ? "zip" : jpeg ? "jpg" : "png"

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: Math.min(520, parent.width - 48)
    padding: 24
    closePolicy: Doc.outputBusy ? Popup.NoAutoClose : Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#B3000000" }
    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    onAboutToShow: {
        error = ""
        result = ""
        cancelling = false
        pageMode.currentIndex = 0
        range.clear()
        target.currentIndex = 0
    }

    function save(path) {
        result = ""
        cancelling = false
        error = Doc.startImageExport({
            pages: pages, currentPage: currentPage, jpeg: jpeg, transparent: alpha.checked,
            dpi: resolution.value, quality: quality.value, folder: folder
        }, path)
    }

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
        spacing: 10
        AppText { text: Tr.s("exportImages"); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold }
        AppText { Layout.fillWidth: true; text: Tr.s("outputSavedSource"); color: Theme.textSecondary; wrapMode: Text.Wrap }
        GridLayout {
            columns: 2
            Layout.fillWidth: true
            enabled: !Doc.outputBusy
            AppText { text: Tr.s("outputPages") }
            ComboBox {
                id: pageMode
                objectName: "imagePageMode"
                Layout.fillWidth: true
                model: [Tr.s("allPages"), Tr.f("currentPageN", dlg.currentPage + 1), Tr.s("pageRange")]
            }
            TextField {
                id: range
                objectName: "imageRange"
                visible: pageMode.currentIndex === 2
                Layout.column: 1
                Layout.fillWidth: true
                placeholderText: Tr.s("rangeHint")
            }
            AppText { text: Tr.s("outputFormat") }
            ComboBox { id: format; Layout.fillWidth: true; model: ["PNG", "JPEG"] }
            AppText { text: "DPI" }
            SpinBox { id: resolution; from: 72; to: 600; value: 144; editable: true }
            AppText { visible: dlg.jpeg; text: Tr.s("outputQuality") }
            SpinBox { id: quality; visible: dlg.jpeg; from: 1; to: 100; value: 90; editable: true }
            CheckBox {
                id: alpha
                visible: !dlg.jpeg
                Layout.columnSpan: 2
                text: Tr.s("transparentPng")
                contentItem: AppText { leftPadding: 28; text: alpha.text; verticalAlignment: Text.AlignVCenter }
            }
            AppText { visible: dlg.count > 1; text: Tr.s("outputTarget") }
            ComboBox { id: target; visible: dlg.count > 1; Layout.fillWidth: true; model: ["ZIP", Tr.s("imageFolder")] }
        }
        AppText {
            Layout.fillWidth: true
            visible: dlg.error !== "" || dlg.result !== ""
            text: dlg.error !== "" ? Tr.outputError(dlg.error) : Tr.f("outputSaved", dlg.result)
            color: dlg.error !== "" ? Theme.errorSoft : Theme.success
            wrapMode: Text.WrapAnywhere
        }
        ProgressBar { Layout.fillWidth: true; visible: Doc.outputBusy; value: Doc.outputProgress }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PButton {
                objectName: "imageClose"
                variant: "ghost"
                text: Doc.outputBusy ? Tr.s("cancel") : Tr.s("close")
                enabled: !Doc.outputBusy || !dlg.cancelling
                onClicked: {
                    if (Doc.outputBusy) {
                        dlg.cancelling = true
                        Doc.cancelImageExport()
                    } else {
                        dlg.close()
                    }
                }
            }
            PButton {
                objectName: "imageSave"
                variant: "primary"
                text: Tr.s("exportImages")
                enabled: !Doc.outputBusy
                onClicked: {
                    dlg.error = ""
                    dlg.result = ""
                    if (dlg.count === 0)
                        dlg.error = "range"
                    else if (dlg.folder)
                        chooseFolder.open()
                    else
                        chooseFile.open()
                }
            }
        }
    }
    FileDialog {
        id: chooseFile
        title: Tr.s("exportImages")
        fileMode: FileDialog.SaveFile
        options: FileDialog.DontConfirmOverwrite
        defaultSuffix: dlg.extension
        nameFilters: [dlg.extension === "jpg" ? "JPEG (*.jpg *.jpeg)" : dlg.extension.toUpperCase() + " (*." + dlg.extension + ")"]
        onAccepted: dlg.save(selectedFile.toString())
    }
    FolderDialog {
        id: chooseFolder
        title: Tr.s("chooseImageFolder")
        onAccepted: dlg.save(selectedFolder.toString())
    }
}
