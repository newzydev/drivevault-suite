import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

ColumnLayout {
    id: bar
    objectName: "redactionBar"
    property int sourceId: 0
    property var marks: []
    property var preview: null
    property string error: ""
    property string result: ""
    property bool saving: false
    readonly property bool selective: saveMode.currentIndex === 0
    signal goTo(int page, real y)
    signal openResult(string path)

    readonly property var patterns: [
        "", "",
        "(?<!\\d)\\d(?:[ -]?\\d){12}(?!\\d)",
        "(?<![\\d+])(?:\\+66[ -]?|0)[2-9](?:[ -]?\\d){7,8}(?!\\d)",
        "[\\p{L}\\d.!#$%&'*+/=?^_{|}~-]+@[\\p{L}\\d-]+(?:\\.[\\p{L}\\d-]+)+",
        "(?<!\\d)\\d{3}[ -]?\\d[ -]?\\d{5}[ -]?\\d(?!\\d)"
    ]

    visible: sourceId !== 0
    spacing: 5

    function begin() {
        if (sourceId === Doc.docId)
            return
        sourceId = Doc.docId
        marks = []
        preview = null
        query.clear()
        label.clear()
        mode.currentIndex = 0
        saveMode.currentIndex = 0
        boxColor.currentIndex = 0
        error = ""
        result = ""
    }
    function close() {
        sourceId = 0
        marks = []
        preview = null
        query.clear()
        label.clear()
        result = ""
    }
    function addMarks(incoming) {
        const next = marks.slice()
        const seen = new Set(next.map(m => [m.page, m.x, m.y, m.w, m.h].join(",")))
        for (const mark of incoming) {
            const key = [mark.page, mark.x, mark.y, mark.w, mark.h].join(",")
            if (!seen.has(key)) {
                next.push(mark)
                seen.add(key)
            }
        }
        if (next.length > 2000) {
            error = "redactTooMany"
            return
        }
        marks = next
        error = ""
        result = ""
        if (marks.length > 0)
            review.currentIndex = marks.length - 1
    }
    function scan() {
        result = ""
        const preset = mode.currentIndex >= 2
        error = Doc.startRedactionScan(preset ? patterns[mode.currentIndex] : query.text, mode.currentIndex !== 0)
    }
    function save(path) {
        if (sourceId === 0 || sourceId !== Doc.docId)
            return
        result = ""
        error = Doc.startRedaction(marks, ["#000000", "#ffffff", "#b91c1c"][boxColor.currentIndex],
                                   selective ? "" : label.text, path, selective)
        saving = error === ""
    }

    Connections {
        target: Doc
        function onDocumentChanged() {
            if (bar.sourceId !== Doc.docId)
                bar.close()
        }
        function onRotationChanged() {
            if (Doc.rotation !== 0)
                bar.close()
        }
        function onRedactionScanFinished(sourceId, error, marks) {
            if (bar.sourceId !== sourceId)
                return
            if (error !== "") {
                bar.error = error
                return
            }
            bar.addMarks(marks)
            bar.goTo(marks[0].page, marks[0].y)
        }
        function onOutputFinished(error, path) {
            if (!bar.saving)
                return
            bar.saving = false
            if (bar.sourceId === Doc.docId) {
                bar.error = error
                bar.result = path
            }
        }
    }

    AppText {
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        text: Tr.s("redactHint")
        wrapMode: Text.Wrap
        color: Theme.textSecondary
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        enabled: !Doc.outputBusy
        ComboBox {
            id: mode
            objectName: "redactSearchMode"
            model: [Tr.s("redactText"), "Regex", Tr.s("redactId"), Tr.s("redactPhone"), Tr.s("redactEmail"), Tr.s("redactBank")]
        }
        PTextField {
            id: query
            objectName: "redactQuery"
            Layout.fillWidth: true
            visible: mode.currentIndex < 2
            maximumLength: 512
            inputMethodHints: Qt.ImhSensitiveData
            placeholderText: mode.currentIndex === 0 ? Tr.s("redactText") : Tr.s("redactPattern")
            onAccepted: bar.scan()
        }
        PButton { objectName: "redactScan"; compact: true; text: Tr.s("redactScan"); onClicked: bar.scan() }
        ComboBox {
            id: review
            objectName: "redactReview"
            Layout.fillWidth: mode.currentIndex >= 2
            implicitWidth: 155
            model: bar.marks.map((m, i) => Tr.f2("redactMark", i + 1, m.page + 1))
            displayText: count > 0 ? currentText : Tr.s("redactEmpty")
            enabled: count > 0
            onActivated: {
                const m = bar.marks[currentIndex]
                bar.goTo(m.page, m.y)
            }
        }
        PButton {
            objectName: "redactRemove"
            compact: true
            text: Tr.s("redactRemove")
            enabled: review.currentIndex >= 0
            onClicked: {
                const next = bar.marks.slice()
                const index = review.currentIndex
                next.splice(index, 1)
                bar.marks = next
                review.currentIndex = Math.min(index, next.length - 1)
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        ComboBox {
            id: saveMode
            objectName: "redactSaveMode"
            enabled: !Doc.outputBusy
            model: [Tr.s("redactSelective"), Tr.s("redactRaster")]
        }
        ComboBox {
            id: boxColor
            enabled: !Doc.outputBusy
            model: [Tr.s("redactBlack"), Tr.s("redactWhite"), Tr.s("redactRed")]
        }
        PTextField {
            id: label
            Layout.fillWidth: true
            enabled: !Doc.outputBusy && !bar.selective
            maximumLength: 80
            placeholderText: Tr.s("redactLabel")
        }
        PButton {
            compact: true
            text: Tr.s("openResult")
            visible: bar.result !== ""
            enabled: !Doc.outputBusy
            onClicked: bar.openResult(bar.result)
        }
        PButton {
            objectName: "redactApply"
            compact: true
            text: Tr.s("redactApply")
            enabled: !Doc.outputBusy && bar.marks.length > 0
            onClicked: confirm.open()
        }
        PButton {
            objectName: "redactClose"
            compact: true
            text: Tr.s(Doc.outputBusy ? "cancel" : "close")
            onClicked: { if (Doc.outputBusy) Doc.cancelImageExport(); else bar.close() }
        }
    }
    ProgressBar { Layout.fillWidth: true; visible: bar.visible && Doc.outputBusy; value: Doc.outputProgress; indeterminate: !bar.saving }
    AppText {
        Layout.fillWidth: true
        Layout.leftMargin: 10
        Layout.rightMargin: 10
        visible: bar.error !== "" || bar.result !== ""
        text: bar.error !== "" ? Tr.outputError(bar.error) : Tr.f("outputSaved", bar.result)
        color: bar.error !== "" ? Theme.errorSoft : Theme.success
        wrapMode: Text.WrapAnywhere
    }
    ConfirmDialog {
        id: confirm
        title: Tr.s("redactApply")
        text: Tr.s(bar.selective ? "redactSelectiveConfirm" : "redactConfirm")
        confirmText: Tr.s("redactApply")
        cancelText: Tr.s("cancel")
        onConfirmed: if (bar.visible) destination.open()
    }
    FileDialog {
        id: destination
        title: Tr.s("redactApply")
        fileMode: FileDialog.SaveFile
        options: FileDialog.DontConfirmOverwrite
        nameFilters: [Tr.s("pdfFiles")]
        defaultSuffix: "pdf"
        onAccepted: bar.save(selectedFile.toString())
    }
}
