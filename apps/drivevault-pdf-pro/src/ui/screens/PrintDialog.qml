import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// Print, laid out like the print window of the big PDF programs: printer, copies and colour on top, the pages, the page sizing (fit, actual size,
// shrink oversized pages, custom scale), both sides and the orientation on the left, a preview of the page on the paper on the right.
// Printing runs in the background; this dialog shows the progress.
Popup {
    id: dlg

    property int currentPage: 0
    property string error: ""
    property var printers: []
    property int previewPage: 0
    property var paper: ({ name: "A4", widthPt: 595, heightPt: 842, widthMm: 210, heightMm: 297 })

    // ---- what the preview shows (the same rules as PrintTask) ----------------------------------------------------------------
    readonly property string orient: orientPortrait.checked ? "portrait" : orientLandscape.checked ? "landscape" : "auto"
    readonly property string scaleMode: scaleActual.checked ? "actual" : scaleShrink.checked ? "shrink" : scaleCustom.checked ? "custom" : "fit"
    readonly property int customPercent: { const n = parseInt(customField.text); return isNaN(n) ? 100 : Math.max(10, Math.min(400, n)) }
    readonly property real pageW: { Doc.docId; Doc.rotation; return Doc.isOpen ? Doc.pageWidthPt(previewPage) : 0 }
    readonly property real pageH: { Doc.docId; Doc.rotation; return Doc.isOpen ? Doc.pageHeightPt(previewPage) : 0 }
    readonly property bool landscapePaper: orient === "landscape" || (orient === "auto" && pageW > pageH)
    readonly property real paperW: landscapePaper ? paper.heightPt : paper.widthPt
    readonly property real paperH: landscapePaper ? paper.widthPt : paper.heightPt
    readonly property real toPaper: pageW > 0 && pageH > 0 ? Math.min(paperW / pageW, paperH / pageH) : 1
    readonly property real pageScale: scaleMode === "fit" ? toPaper : scaleMode === "shrink" ? Math.min(1, toPaper) : scaleMode === "actual" ? 1 : customPercent / 100
    readonly property real boxScale: Math.min(250 / paperW, 300 / paperH)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    width: Math.min(900, (parent ? parent.width : 900) - 48)
    padding: 22
    closePolicy: Doc.printing ? Popup.NoAutoClose : Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#B3000000" }

    function updatePaper() {
        paper = Doc.paperInfo(printers.length > 0 ? printers[Math.max(0, printerBox.currentIndex)] : "")
    }

    onAboutToShow: {
        error = ""
        printers = Doc.printerNames()
        const def = Doc.defaultPrinter()
        printerBox.currentIndex = Math.max(0, printers.indexOf(def))
        allPages.checked = true
        rangeField.text = ""
        previewPage = Math.max(0, Math.min(currentPage, Doc.pageCount - 1))
        updatePaper()
    }

    function start() {
        error = ""
        let pages = "all"
        if (currentOnly.checked)
            pages = "current"
        else if (rangeOption.checked)
            pages = rangeField.text
        const code = Doc.startPrint({
            printer: printers.length > 0 ? printers[printerBox.currentIndex] : "",
            pages: pages,
            currentPage: dlg.currentPage,
            copies: copies.value,
            color: !grayBox.checked,
            scaleMode: dlg.scaleMode,
            scalePercent: dlg.customPercent,
            orientation: dlg.orient,
            duplex: duplexBox.checked
        })
        if (code === "badRange")
            error = Tr.f("badRange", Doc.pageCount)
        else if (code !== "")
            error = Tr.s("printBusy")
    }
    Connections {
        target: Doc
        function onPrintFinished(ok, message) { if (ok) dlg.close() }
    }

    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    // a titled box around a group of options
    component Section: Rectangle {
        id: sec
        property string title: ""
        default property alias body: bodyColumn.data
        Layout.fillWidth: true
        implicitHeight: bodyColumn.implicitHeight + titleText.implicitHeight + 26
        radius: 10
        color: Theme.surface
        border.width: 1
        border.color: Theme.borderSubtle
        AppText { id: titleText; x: 14; y: 10; text: sec.title; font.weight: Font.DemiBold }
        ColumnLayout {
            id: bodyColumn
            x: 14
            y: titleText.implicitHeight + 16
            width: sec.width - 28
            spacing: 4
        }
    }

    contentItem: ColumnLayout {
        spacing: 14
        AppText { Layout.fillWidth: true; text: Tr.s("printTitle"); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold }

        RowLayout {
            Layout.fillWidth: true
            spacing: 18

            // ---- the options ---------------------------------------------------------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    AppText { text: Tr.s("printer"); color: Theme.textSecondary }
                    ComboBox {
                        id: printerBox
                        objectName: "printerBox"
                        Layout.fillWidth: true
                        implicitHeight: 38
                        model: dlg.printers
                        enabled: dlg.printers.length > 0 && !Doc.printing
                        displayText: dlg.printers.length > 0 ? currentText : Tr.s("noPrinters")
                        font.pixelSize: Theme.fontMd
                        onCurrentIndexChanged: dlg.updatePaper()
                        contentItem: AppText { leftPadding: 10; text: printerBox.displayText; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; color: printerBox.enabled ? Theme.text : Theme.textDisabled }
                        background: Rectangle { radius: 8; color: Theme.surface; border.color: printerBox.activeFocus ? Theme.accent : Theme.border }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    AppText { text: Tr.s("copies"); color: Theme.textSecondary }
                    SpinBox { id: copies; objectName: "printCopies"; from: 1; to: 99; value: 1; editable: true; enabled: !Doc.printing }
                    Item { Layout.fillWidth: true }
                    PCheckBox { id: grayBox; objectName: "printGray"; checked: false; text: Tr.s("printGray"); enabled: !Doc.printing }
                }

                Section {
                    title: Tr.s("pagesToPrintTitle")
                    ButtonGroup { id: pageGroup }
                    PRadioButton { id: allPages; objectName: "printAll"; ButtonGroup.group: pageGroup; text: Tr.s("allPages"); checked: true; enabled: !Doc.printing }
                    PRadioButton { id: currentOnly; objectName: "printCurrent"; ButtonGroup.group: pageGroup; text: Tr.f("currentPageN", dlg.currentPage + 1); enabled: !Doc.printing }
                    RowLayout {
                        Layout.fillWidth: true
                        PRadioButton { id: rangeOption; objectName: "printRange"; ButtonGroup.group: pageGroup; text: Tr.s("pageRange"); enabled: !Doc.printing }
                        PTextField {
                            id: rangeField
                            objectName: "printRangeField"
                            Layout.fillWidth: true
                            implicitHeight: 34
                            enabled: rangeOption.checked && !Doc.printing
                            placeholderText: Tr.s("rangeHint")
                            placeholderTextColor: Theme.textDisabled
                            color: Theme.text
                            font.pixelSize: Theme.fontMd
                            leftPadding: 10
                            onTextChanged: { if (text !== "") rangeOption.checked = true }
                            background: Rectangle { radius: 8; color: Theme.backgroundAlt; border.color: rangeField.activeFocus ? Theme.accent : Theme.border }
                        }
                    }
                }

                Section {
                    title: Tr.s("pageSizing")
                    ButtonGroup { id: scaleGroup }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 2
                        PRadioButton { id: scaleFit; objectName: "printFit"; ButtonGroup.group: scaleGroup; text: Tr.s("scaleFit"); enabled: !Doc.printing }
                        PRadioButton { id: scaleActual; objectName: "printActual"; ButtonGroup.group: scaleGroup; text: Tr.s("scaleActual"); enabled: !Doc.printing }
                        PRadioButton { id: scaleShrink; objectName: "printShrink"; ButtonGroup.group: scaleGroup; text: Tr.s("scaleShrink"); checked: true; enabled: !Doc.printing }
                        RowLayout {
                            spacing: 6
                            PRadioButton { id: scaleCustom; objectName: "printCustom"; ButtonGroup.group: scaleGroup; text: Tr.s("scaleCustom"); enabled: !Doc.printing }
                            PTextField {
                                id: customField
                                objectName: "printCustomField"
                                Layout.preferredWidth: 64
                                implicitHeight: 32
                                text: "100"
                                enabled: scaleCustom.checked && !Doc.printing
                                validator: IntValidator { bottom: 10; top: 400 }
                                inputMethodHints: Qt.ImhDigitsOnly
                                horizontalAlignment: TextInput.AlignRight
                                onTextChanged: { if (activeFocus) scaleCustom.checked = true }
                                background: Rectangle { radius: 8; color: Theme.backgroundAlt; border.color: customField.activeFocus ? Theme.accent : Theme.border }
                            }
                            AppText { text: "%"; color: Theme.textSecondary }
                        }
                    }
                }

                PCheckBox { id: duplexBox; objectName: "printDuplex"; text: Tr.s("duplex"); enabled: !Doc.printing }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    AppText { text: Tr.s("orientation"); color: Theme.textSecondary }
                    ButtonGroup { id: orientGroup }
                    PRadioButton { id: orientAuto; objectName: "printOrientAuto"; ButtonGroup.group: orientGroup; text: Tr.s("orientAuto"); checked: true; enabled: !Doc.printing }
                    PRadioButton { id: orientPortrait; objectName: "printOrientPortrait"; ButtonGroup.group: orientGroup; text: Tr.s("orientPortrait"); enabled: !Doc.printing }
                    PRadioButton { id: orientLandscape; objectName: "printOrientLandscape"; ButtonGroup.group: orientGroup; text: Tr.s("orientLandscape"); enabled: !Doc.printing }
                    Item { Layout.fillWidth: true }
                }
            }

            // ---- the preview: the page on the paper -------------------------------------------------------------------------------
            ColumnLayout {
                Layout.preferredWidth: 290
                Layout.maximumWidth: 290
                Layout.alignment: Qt.AlignTop
                spacing: 6
                AppText { objectName: "printScale"; Layout.alignment: Qt.AlignHCenter; text: Tr.f("previewScale", Math.round(dlg.pageScale * 100)); color: Theme.textSecondary; font.pixelSize: Theme.fontSm }
                AppText {
                    objectName: "printPaper"
                    Layout.alignment: Qt.AlignHCenter
                    text: dlg.paper.name + "  (" + dlg.paper.widthMm + " x " + dlg.paper.heightMm + " mm)"
                    font.pixelSize: Theme.fontSm
                }
                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 270
                    Layout.preferredHeight: 316
                    Rectangle {
                        id: sheet
                        objectName: "printSheet"
                        anchors.centerIn: parent
                        width: dlg.paperW * dlg.boxScale
                        height: dlg.paperH * dlg.boxScale
                        color: "white"
                        border.width: 1
                        border.color: Theme.border
                        clip: true
                        Image {
                            anchors.centerIn: parent
                            width: dlg.pageW * dlg.pageScale * dlg.boxScale
                            height: dlg.pageH * dlg.pageScale * dlg.boxScale
                            asynchronous: true
                            smooth: true
                            visible: Doc.isOpen && dlg.pageW > 0
                            source: Doc.isOpen && dlg.pageW > 0
                                ? "image://pdf/" + Doc.docId + "/" + dlg.previewPage + "/" + Math.max(1, Math.round(dlg.pageScale * dlg.boxScale * 2 * 1000)) + "/" + Doc.rotation + "/0" : ""
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    PButton { objectName: "printPrevPage"; compact: true; variant: "ghost"; text: "‹"; enabled: dlg.previewPage > 0; onClicked: dlg.previewPage-- }
                    PSlider {
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, Doc.pageCount - 1)
                        stepSize: 1
                        snapMode: Slider.SnapAlways
                        enabled: Doc.pageCount > 1
                        value: dlg.previewPage
                        onMoved: dlg.previewPage = Math.round(value)
                    }
                    PButton { objectName: "printNextPage"; compact: true; variant: "ghost"; text: "›"; enabled: dlg.previewPage < Doc.pageCount - 1; onClicked: dlg.previewPage++ }
                }
                AppText { Layout.alignment: Qt.AlignHCenter; text: Tr.s("previewPageOf").replace("%1", dlg.previewPage + 1).replace("%2", Doc.pageCount); font.pixelSize: Theme.fontSm }
                AppText { Layout.fillWidth: true; text: Tr.s("previewNote"); color: Theme.textDisabled; font.pixelSize: Theme.fontSm; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
            }
        }

        AppText { objectName: "printError"; Layout.fillWidth: true; visible: dlg.error !== ""; text: dlg.error; color: Theme.errorSoft; wrapMode: Text.Wrap; font.pixelSize: Theme.fontSm }
        AppText { Layout.fillWidth: true; visible: dlg.printers.length === 0; text: Tr.s("noPrintersText"); color: Theme.warning; wrapMode: Text.Wrap; font.pixelSize: Theme.fontSm }
        ProgressBar { objectName: "printProgress"; Layout.fillWidth: true; visible: Doc.printing; from: 0; to: 1; value: Doc.printProgress }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PButton { objectName: "printCancel"; variant: "ghost"; text: Tr.s("cancel"); onClicked: { if (Doc.printing) Doc.cancelPrint(); else dlg.close() } }
            PButton { objectName: "printStart"; variant: "primary"; text: Doc.printing ? Tr.s("printing") : Tr.s("print"); enabled: !Doc.printing && dlg.printers.length > 0; onClicked: dlg.start() }
        }
    }
}
