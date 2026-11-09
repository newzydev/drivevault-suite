import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

Popup {
    id: dlg
    objectName: "maintenanceDialog"
    property bool repair: false
    property bool loaded: false
    property int sourceId: 0
    property string error: ""
    property string result: ""
    property string input: ""
    property var attachments: []
    property var removed: []
    property var initial: ({})
    signal openResult(string path)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: Math.min(600, parent.width - 48)
    padding: 22
    closePolicy: Doc.outputBusy ? Popup.NoAutoClose : Popup.CloseOnEscape
    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    function begin(repairMode) {
        repair = repairMode
        sourceId = Doc.docId
        loaded = repair
        error = ""
        result = ""
        input = repair && Doc.isOpen ? Doc.filePath : ""
        attachments = []
        removed = []
        initial = ({})
        titleField.clear()
        author.clear()
        keywords.clear()
        password.clear()
        clean.checked = false
        open()
        if (!repair)
            error = Doc.inspectMaintenance()
    }
    function save(path) {
        result = ""
        const fields = {}
        if (titleField.text !== initial.Title) fields.Title = titleField.text
        if (author.text !== initial.Author) fields.Author = author.text
        if (keywords.text !== initial.Keywords) fields.Keywords = keywords.text
        error = Doc.startMaintenance({mode: repair ? "repair" : clean.checked ? "clean" : "metadata",
            source: input, sourceId: sourceId, password: password.text, fields: fields, attachments: removed}, path)
        password.clear()
    }
    onClosed: password.clear()

    Connections {
        target: Doc
        function onMaintenanceInspected(value) {
            if (!dlg.visible)
                return
            if (dlg.sourceId !== Doc.docId) {
                dlg.error = "maintenanceSourceChanged"
                return
            }
            dlg.error = value.error || ""
            dlg.loaded = dlg.error === ""
            if (dlg.loaded) {
                dlg.initial = value
                titleField.text = value.Title
                author.text = value.Author
                keywords.text = value.Keywords
                dlg.attachments = value.attachments
            }
        }
        function onOutputFinished(error, path) {
            if (dlg.visible) {
                dlg.error = error
                dlg.result = path
            }
        }
    }
    contentItem: ColumnLayout {
        spacing: 7
        AppText { text: Tr.s(dlg.repair ? "repairPdf" : "maintenance"); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold }
        AppText {
            Layout.fillWidth: true
            text: Tr.s(dlg.repair ? "repairHint" : clean.checked ? "cleanupHint" : "maintenanceHint")
            wrapMode: Text.Wrap
        }
        RowLayout {
            visible: dlg.repair
            Layout.fillWidth: true
            AppText { Layout.fillWidth: true; text: dlg.input || Tr.s("repairSource"); textFormat: Text.PlainText; elide: Text.ElideMiddle }
            PButton { text: Tr.s("openFile"); enabled: !Doc.outputBusy; onClicked: sourceFile.open() }
        }
        TextField {
            id: password
            visible: dlg.repair
            Layout.fillWidth: true
            enabled: !Doc.outputBusy
            echoMode: TextInput.Password
            placeholderText: Tr.s("ownerPassword")
        }
        CheckBox {
            id: clean
            objectName: "maintenanceClean"
            visible: !dlg.repair
            enabled: dlg.loaded && !Doc.outputBusy
            text: Tr.s("cleanupPdf")
            contentItem: AppText { leftPadding: 28; text: clean.text; verticalAlignment: Text.AlignVCenter }
        }
        GridLayout {
            columns: 2
            Layout.fillWidth: true
            visible: !dlg.repair && !clean.checked
            enabled: dlg.loaded && !Doc.outputBusy
            AppText { text: Tr.s("metadataTitle") }
            TextField { id: titleField; objectName: "metadataTitle"; Layout.fillWidth: true; maximumLength: 4096 }
            AppText { text: Tr.s("metadataAuthor") }
            TextField { id: author; Layout.fillWidth: true; maximumLength: 4096 }
            AppText { text: Tr.s("metadataKeywords") }
            TextField { id: keywords; Layout.fillWidth: true; maximumLength: 4096 }
        }
        AppText {
            visible: !dlg.repair && !clean.checked && dlg.loaded
            text: Tr.s(dlg.attachments.length ? "attachments" : "noAttachments")
        }
        ListView {
            visible: !dlg.repair && !clean.checked && count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(105, count * 34)
            enabled: !Doc.outputBusy
            clip: true
            model: dlg.attachments
            ScrollBar.vertical: ScrollBar {}
            delegate: CheckBox {
                required property var modelData
                width: ListView.view.width
                height: 34
                text: Tr.f("removeAttachment", modelData.name)
                checked: dlg.removed.includes(modelData.id)
                contentItem: AppText { leftPadding: 28; text: parent.text; textFormat: Text.PlainText; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                onToggled: {
                    const id = modelData.id
                    dlg.removed = checked ? dlg.removed.concat([id]) : dlg.removed.filter(value => value !== id)
                }
            }
        }
        AppText {
            Layout.fillWidth: true
            visible: dlg.error !== "" || dlg.result !== ""
            text: dlg.error !== "" ? Tr.outputError(dlg.error) : Tr.f("outputSaved", dlg.result)
            textFormat: Text.PlainText
            color: dlg.error !== "" ? Theme.errorSoft : Theme.success
            wrapMode: Text.WrapAnywhere
        }
        ProgressBar { Layout.fillWidth: true; visible: Doc.outputBusy; indeterminate: true }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PButton { objectName: "maintenanceClose"; text: Tr.s("close"); enabled: !Doc.outputBusy; onClicked: dlg.close() }
            PButton { text: Tr.s("openResult"); visible: dlg.result !== ""; onClicked: { dlg.close(); dlg.openResult(dlg.result) } }
            PButton {
                objectName: "maintenanceSave"
                text: Tr.s("maintenanceSave")
                variant: "primary"
                enabled: !Doc.outputBusy && dlg.loaded && (dlg.repair ? dlg.input !== "" : clean.checked
                    || dlg.removed.length > 0 || titleField.text !== dlg.initial.Title
                    || author.text !== dlg.initial.Author || keywords.text !== dlg.initial.Keywords)
                onClicked: destination.open()
            }
        }
    }
    FileDialog {
        id: sourceFile
        title: Tr.s("repairSource")
        nameFilters: [Tr.s("pdfFiles")]
        onAccepted: dlg.input = Doc.localPath(selectedFile.toString())
    }
    FileDialog {
        id: destination
        title: Tr.s("maintenanceSave")
        fileMode: FileDialog.SaveFile
        options: FileDialog.DontConfirmOverwrite
        nameFilters: [Tr.s("pdfFiles")]
        defaultSuffix: "pdf"
        onAccepted: dlg.save(selectedFile.toString())
    }
}
