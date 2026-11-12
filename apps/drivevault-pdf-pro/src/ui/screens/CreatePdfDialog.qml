import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

Popup {
    id: dlg
    objectName: "createPdfDialog"
    property bool fromImages: false
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

    function begin(images) {
        fromImages = images
        pages.clear()
        error = ""
        open()
    }

    function addImages(paths) {
        for (const path of paths)
            pages.append({ path: Doc.localPath(path.toString()), turns: 0 })
    }

    function save(path) {
        const images = []
        if (fromImages) {
            if (pages.count === 0)
                return
            for (let i = 0; i < pages.count; ++i) {
                const row = pages.get(i)
                images.push({ path: row.path, turns: row.turns })
            }
        }
        error = Doc.startCreatePdf({
            paper: ["a4", "letter", "legal", "custom"][paper.currentIndex],
            width: paperWidth.value, height: paperHeight.value,
            landscape: orientation.currentIndex === 1, margin: margin.value
        }, images, path)
        creating = error === ""
    }

    ListModel { id: pages }
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
        AppText {
            text: Tr.s(dlg.fromImages ? "createFromImages" : "createBlankPdf")
            font.pixelSize: Theme.fontXl
            font.weight: Font.DemiBold
        }
        GridLayout {
            columns: 2
            enabled: !Doc.outputBusy
            AppText { text: Tr.s("createPaper") }
            ComboBox { id: paper; objectName: "createPaper"; model: ["A4", "Letter", "Legal", Tr.s("createCustom")] }
            AppText { visible: paper.currentIndex === 3; text: Tr.s("createPaperSize") }
            RowLayout {
                visible: paper.currentIndex === 3
                SpinBox { id: paperWidth; objectName: "createWidth"; from: 20; to: 2000; value: 210; editable: true; Accessible.name: Tr.s("createWidth") }
                AppText { text: "×" }
                SpinBox { id: paperHeight; objectName: "createHeight"; from: 20; to: 2000; value: 297; editable: true; Accessible.name: Tr.s("createHeight") }
            }
            AppText { text: Tr.s("createOrientation") }
            ComboBox { id: orientation; objectName: "createOrientation"; model: [Tr.s("createPortrait"), Tr.s("createLandscape")] }
            AppText { text: Tr.s("createMargin") }
            SpinBox { id: margin; from: 0; to: 200; value: 12; editable: true; Accessible.name: Tr.s("createMargin") }
        }
        PButton {
            visible: dlg.fromImages
            enabled: !Doc.outputBusy
            text: Tr.s("createAddImages")
            onClicked: chooseImages.open()
        }
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(140, count * 34)
            visible: dlg.fromImages && count > 0
            enabled: !Doc.outputBusy
            clip: true
            model: pages
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                required property string path
                required property int turns
                required property int index
                width: ListView.view.width - 12
                height: 34
                AppText { Layout.fillWidth: true; text: path.split(/[\\/]/).pop(); elide: Text.ElideMiddle; textFormat: Text.PlainText }
                AppText { text: (turns * 90) + "°" }
                PButton { objectName: "createMoveUp_" + index; compact: true; variant: "ghost"; text: "↑"; tip: Tr.s("createMoveUp"); enabled: index > 0; onClicked: pages.move(index, index - 1, 1) }
                PButton { compact: true; variant: "ghost"; text: "↓"; tip: Tr.s("createMoveDown"); enabled: index + 1 < pages.count; onClicked: pages.move(index, index + 1, 1) }
                PButton { objectName: "createRotate_" + index; compact: true; variant: "ghost"; glyph: "rotate"; tip: Tr.s("rotateRight"); onClicked: pages.setProperty(index, "turns", (turns + 1) % 4) }
                PButton { compact: true; variant: "ghost"; glyph: "close"; tip: Tr.s("removeRecent"); onClicked: pages.remove(index) }
            }
        }
        ProgressBar { Layout.fillWidth: true; visible: dlg.creating; value: Doc.outputProgress }
        AppText { Layout.fillWidth: true; visible: dlg.error !== ""; text: Tr.outputError(dlg.error); wrapMode: Text.Wrap; color: Theme.errorSoft }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PButton { text: Tr.s(dlg.creating ? "cancel" : "close"); onClicked: dlg.creating ? Doc.cancelOutput() : dlg.close() }
            PButton {
                variant: "primary"
                text: Tr.s("saveAs")
                enabled: !Doc.outputBusy && (!dlg.fromImages || pages.count > 0)
                onClicked: chooseOutput.open()
            }
        }
    }
    FileDialog {
        id: chooseImages
        title: Tr.s("createAddImages")
        fileMode: FileDialog.OpenFiles
        nameFilters: [Tr.s("createImageFilter")]
        onAccepted: dlg.addImages(selectedFiles)
    }
    FileDialog {
        id: chooseOutput
        title: Tr.s("saveAs")
        fileMode: FileDialog.SaveFile
        options: FileDialog.DontConfirmOverwrite
        defaultSuffix: "pdf"
        nameFilters: [Tr.s("pdfFiles")]
        onAccepted: dlg.save(selectedFile.toString())
    }
}
