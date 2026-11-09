import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// Print: printer, pages, copies, colour and fit to paper. Printing runs in the background; this dialog shows the progress.
Popup {
    id: dlg

    property int currentPage: 0
    property string error: ""
    property var printers: []

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    width: Math.min(460, (parent ? parent.width : 460) - 48)
    padding: 24
    closePolicy: Doc.printing ? Popup.NoAutoClose : Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#B3000000" }

    onAboutToShow: {
        error = ""
        printers = Doc.printerNames()
        const def = Doc.defaultPrinter()
        printerBox.currentIndex = Math.max(0, printers.indexOf(def))
        allPages.checked = true
        rangeField.text = ""
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
            color: colorBox.checked,
            fit: fitBox.checked
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

    contentItem: ColumnLayout {
        spacing: 12
        AppText { Layout.fillWidth: true; text: Tr.s("printTitle"); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold }

        AppText { text: Tr.s("printer"); color: Theme.textSecondary; font.pixelSize: Theme.fontSm }
        ComboBox {
            id: printerBox
            objectName: "printerBox"
            Layout.fillWidth: true
            implicitHeight: 38
            model: dlg.printers
            enabled: dlg.printers.length > 0 && !Doc.printing
            displayText: dlg.printers.length > 0 ? currentText : Tr.s("noPrinters")
            font.pixelSize: Theme.fontMd
            contentItem: AppText { leftPadding: 10; text: printerBox.displayText; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; color: printerBox.enabled ? Theme.text : Theme.textDisabled }
            background: Rectangle { radius: 8; color: Theme.surface; border.color: printerBox.activeFocus ? Theme.accent : Theme.border }
        }

        AppText { text: Tr.s("pagesToPrint"); color: Theme.textSecondary; font.pixelSize: Theme.fontSm }
        ColumnLayout {
            spacing: 2
            ButtonGroup { id: pageGroup }
            RadioButton { id: allPages; objectName: "printAll"; ButtonGroup.group: pageGroup; text: Tr.s("allPages"); checked: true; enabled: !Doc.printing; contentItem: AppText { leftPadding: 28; text: parent.text; verticalAlignment: Text.AlignVCenter } }
            RadioButton { id: currentOnly; objectName: "printCurrent"; ButtonGroup.group: pageGroup; text: Tr.f("currentPageN", dlg.currentPage + 1); enabled: !Doc.printing; contentItem: AppText { leftPadding: 28; text: parent.text; verticalAlignment: Text.AlignVCenter } }
            RowLayout {
                RadioButton { id: rangeOption; objectName: "printRange"; ButtonGroup.group: pageGroup; text: Tr.s("pageRange"); enabled: !Doc.printing; contentItem: AppText { leftPadding: 28; text: parent.text; verticalAlignment: Text.AlignVCenter } }
                TextField {
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
                    background: Rectangle { radius: 8; color: Theme.surface; border.color: rangeField.activeFocus ? Theme.accent : Theme.border }
                }
            }
        }
        RowLayout {
            spacing: 16
            AppText { text: Tr.s("copies"); color: Theme.textSecondary }
            SpinBox { id: copies; objectName: "printCopies"; from: 1; to: 99; value: 1; editable: true; enabled: !Doc.printing }
            CheckBox { id: colorBox; objectName: "printColor"; checked: true; text: Tr.s("colour"); enabled: !Doc.printing; contentItem: AppText { leftPadding: 28; text: parent.text; verticalAlignment: Text.AlignVCenter } }
            CheckBox { id: fitBox; objectName: "printFit"; checked: true; text: Tr.s("fitPaper"); enabled: !Doc.printing; contentItem: AppText { leftPadding: 28; text: parent.text; verticalAlignment: Text.AlignVCenter } }
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
