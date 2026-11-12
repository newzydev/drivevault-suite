import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// Compress the open file: a level (or own values), then a real before / after (sizes, images, one page side by side), then save as a new file.
// The open file is never changed.
Popup {
    id: dlg
    objectName: "compressDialog"

    property string level: "medium"
    property string error: ""
    property string saved: ""
    property bool askReplace: false
    property string pendingPath: ""
    property bool ran: false                  // a run finished (with or without a smaller file)
    property int previewPage: 0
    signal openResultRequested(string path)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    width: Math.min(760, (parent ? parent.width : 760) - 48)
    height: dlg.ran || Compressor.hasResult ? Math.min(760, (parent ? parent.height : 760) - 48) : Math.min(contentItem.implicitHeight + topPadding + bottomPadding, (parent ? parent.height : 760) - 48)
    padding: 24
    closePolicy: Compressor.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#B3000000" }

    function fmt(bytes) {
        if (bytes >= 1048576)
            return (bytes / 1048576).toFixed(2) + " MB"
        return Math.max(1, Math.round(bytes / 1024)) + " KB"
    }
    function applyPreset(name) {
        level = name
        if (name === "custom")
            return
        const p = Compressor.preset(name)
        dpiBox.value = p.dpi
        qualityBox.value = p.quality
        stripBox.checked = p.strip
        linearBox.checked = p.linearize
    }
    function custom() { level = "custom" }
    function start() {
        error = ""
        saved = ""
        ran = false
        const code = Compressor.start({ dpi: dpiBox.value, quality: qualityBox.value, strip: stripBox.checked, linearize: linearBox.checked })
        if (code === "noDocument")
            error = Tr.s("split_noDocument")
        else if (code !== "")
            error = Tr.s("busyNow")
    }
    function save(path, overwrite) {
        const code = Compressor.saveAs(path, overwrite)
        if (code === "") {
            saved = path
            error = ""
        } else if (code === "exists") {
            pendingPath = path
            askReplace = true
        } else {
            error = Tr.s(code === "sameFile" ? "errSameFile" : "errWrite")
        }
    }
    function close2() {
        Compressor.discard()
        saved = ""
        ran = false
        error = ""
        askReplace = false
        close()
    }

    onAboutToShow: {
        applyPreset("medium")
        error = ""; saved = ""; ran = false; askReplace = false
        previewPage = Math.max(0, Math.min(Doc.pageCount - 1, 0))
    }

    Connections {
        target: Compressor
        function onFinished(ok, err) {
            dlg.ran = true
            if (ok)
                dlg.error = ""
            else if (err === "cancelled")
                dlg.error = Tr.s("compressCancelled")
            else if (err === "noJpeg")
                dlg.error = Tr.s("errNoJpeg")
            else
                dlg.error = Tr.s(err === "password" ? "errPasswordFile" : err === "write" ? "errWrite" : "errCompress")
        }
    }

    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 10
        AppText { Layout.fillWidth: true; text: Tr.s("compressTitle") + (Doc.isOpen ? " - " + Doc.fileName : ""); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold; elide: Text.ElideMiddle }

        // level
        ButtonGroup { id: levels }
        Flow {
            Layout.fillWidth: true
            spacing: 14
            Repeater {
                model: ["low", "medium", "high", "custom"]
                delegate: PRadioButton {
                    id: lv
                    required property string modelData
                    objectName: "level_" + modelData
                    ButtonGroup.group: levels
                    text: Tr.s("level_" + modelData)
                    checked: dlg.level === modelData
                    enabled: !Compressor.busy

                    onClicked: dlg.applyPreset(modelData)
                }
            }
        }
        AppText { Layout.fillWidth: true; text: Tr.s("levelText_" + dlg.level); color: Theme.textSecondary; font.pixelSize: Theme.fontSm; wrapMode: Text.Wrap }

        GridLayout {
            columns: 4
            columnSpacing: 12
            rowSpacing: 8
            AppText { text: Tr.s("imageDpi"); color: Theme.textSecondary }
            SpinBox { id: dpiBox; objectName: "compressDpi"; from: 0; to: 600; stepSize: 10; editable: true; enabled: !Compressor.busy; onValueModified: dlg.custom() }
            AppText { text: Tr.s("jpegQuality"); color: Theme.textSecondary }
            SpinBox { id: qualityBox; objectName: "compressQuality"; from: 0; to: 100; stepSize: 5; editable: true; enabled: !Compressor.busy; onValueModified: dlg.custom() }
            PCheckBox { id: stripBox; objectName: "compressStrip"; Layout.columnSpan: 2; text: Tr.s("stripExtras"); enabled: !Compressor.busy; onToggled: dlg.custom()
 }
            PCheckBox { id: linearBox; objectName: "compressLinear"; Layout.columnSpan: 2; text: Tr.s("fastWeb"); enabled: !Compressor.busy; onToggled: dlg.custom()
 }
        }
        AppText { Layout.fillWidth: true; text: Tr.s("compressZeroNote"); color: Theme.textDisabled; font.pixelSize: Theme.fontSm; wrapMode: Text.Wrap }

        ProgressBar { objectName: "compressProgress"; Layout.fillWidth: true; visible: Compressor.busy; from: 0; to: 1; value: Compressor.progress }

        // result
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            radius: 8
            color: Theme.surface
            border.color: Theme.borderSubtle
            visible: dlg.ran || Compressor.hasResult
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                AppText {
                    objectName: "compressSummary"
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.weight: Font.DemiBold
                    color: Compressor.result.written ? Theme.success : Theme.warning
                    visible: dlg.ran && Compressor.result.inBytes !== undefined
                    text: Compressor.result.written
                          ? dlg.fmt(Compressor.result.inBytes) + "  →  " + dlg.fmt(Compressor.result.outBytes) + "   (−" + Math.round((1 - Compressor.result.outBytes / Compressor.result.inBytes) * 100) + " %)"
                          : Tr.s("noGain")
                }
                AppText {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSm
                    visible: Compressor.hasResult
                    text: Tr.f2("imagesReport", Compressor.result.images, Compressor.result.changed) + "  " + Tr.f2("imagesReport2", Compressor.result.downsampled, Compressor.result.skipped)
                          + (Compressor.result.warnings && Compressor.result.warnings.indexOf("encrypted") >= 0 ? "  " + Tr.s("stillProtected") : "")
                }
                RowLayout {
                    visible: Compressor.hasResult
                    spacing: 8
                    AppText { text: Tr.s("comparePage"); color: Theme.textSecondary }
                    SpinBox { id: pageBox; objectName: "comparePage"; from: 1; to: Math.max(1, Doc.pageCount); value: dlg.previewPage + 1; editable: true; onValueModified: dlg.previewPage = value - 1 }
                }
                RowLayout {
                    id: compare
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: Compressor.hasResult
                    spacing: 12
                    readonly property real pw: Doc.pageWidthPt(dlg.previewPage)
                    readonly property real ph: Doc.pageHeightPt(dlg.previewPage)
                    readonly property real boxW: (width - spacing) / 2
                    readonly property real boxH: Math.max(60, height - 22)
                    readonly property real scale: pw > 0 && ph > 0 ? Math.min(boxW / pw, boxH / ph) : 1
                    Repeater {
                        model: [{ title: "before", id: Compressor.beforeId }, { title: "after", id: Compressor.afterId }]
                        delegate: ColumnLayout {
                            id: col
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 2
                            AppText { Layout.alignment: Qt.AlignHCenter; text: Tr.s(col.modelData.title === "before" ? "before" : "after"); color: Theme.textSecondary; font.pixelSize: Theme.fontSm }
                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Image {
                                    objectName: "compare_" + col.modelData.title
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: compare.pw * compare.scale
                                    height: compare.ph * compare.scale
                                    asynchronous: true
                                    cache: false
                                    fillMode: Image.Stretch
                                    source: col.modelData.id > 0 ? "image://pdf/" + col.modelData.id + "/" + dlg.previewPage + "/" + Math.round(compare.scale * 1000) + "/" + Doc.rotation + "/0" : ""
                                }
                            }
                        }
                    }
                }
            }
        }
        Item { Layout.fillHeight: true; visible: !(dlg.ran || Compressor.hasResult) }

        AppText { objectName: "compressError"; Layout.fillWidth: true; visible: dlg.error !== ""; text: dlg.error; color: Theme.errorSoft; wrapMode: Text.Wrap }
        AppText { objectName: "compressSaved"; Layout.fillWidth: true; visible: dlg.saved !== ""; text: Tr.s("saved") + " " + dlg.saved; color: Theme.success; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            visible: dlg.askReplace
            AppText { Layout.fillWidth: true; text: Tr.f("replaceAsk", dlg.pendingPath.split(/[\\/]/).pop()); color: Theme.warning; wrapMode: Text.Wrap }
            PButton { objectName: "compressReplaceNo"; compact: true; variant: "ghost"; text: Tr.s("cancel"); onClicked: dlg.askReplace = false }
            PButton { objectName: "compressReplaceYes"; compact: true; text: Tr.s("replace"); onClicked: { dlg.askReplace = false; dlg.save(dlg.pendingPath, true) } }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item { Layout.fillWidth: true }
            PButton { objectName: "compressOpenResult"; visible: dlg.saved !== ""; text: Tr.s("openResult"); onClicked: { const p = dlg.saved; dlg.close2(); dlg.openResultRequested(p) } }
            PButton { objectName: "compressClose"; variant: "ghost"; text: Compressor.busy ? Tr.s("cancel") : Tr.s("close"); onClicked: { if (Compressor.busy) Compressor.cancel(); else dlg.close2() } }
            PButton { objectName: "compressSave"; visible: Compressor.hasResult; text: Tr.s("saveAs"); onClicked: saveDialog.open() }
            PButton { objectName: "compressStart"; variant: Compressor.hasResult ? "secondary" : "primary"; text: Compressor.busy ? Tr.s("compressing") : (Compressor.hasResult || dlg.ran ? Tr.s("compressAgain") : Tr.s("compressNow")); enabled: !Compressor.busy; onClicked: dlg.start() }
        }
    }

    FileDialog {
        id: saveDialog
        title: Tr.s("saveAs")
        nameFilters: [Tr.s("pdfFiles")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        currentFolder: Doc.isOpen ? "file:///" + Doc.filePath.replace(/\\/g, "/").replace(/\/[^\/]*$/, "") : ""
        onAccepted: dlg.save(Doc.localPath(selectedFile.toString()), false)
    }
}
