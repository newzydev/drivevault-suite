import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

Popup {
    id: dlg
    objectName: "batchDialog"
    property string input: ""
    property string output: ""
    property string error: ""
    property var rows: []
    readonly property bool protect: mode.currentIndex === 2

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: Math.min(650, parent.width - 48)
    padding: 22
    closePolicy: Doc.outputBusy ? Popup.NoAutoClose : Popup.CloseOnEscape
    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }
    onAboutToShow: { error = ""; rows = [] }
    onClosed: { user.clear(); owner.clear(); confirm.clear() }

    function start() {
        error = ""
        rows = []
        if (protect && (owner.text !== confirm.text || owner.text === user.text)) {
            error = owner.text !== confirm.text ? "passwordMismatch" : "invalidPasswords"
            return
        }
        error = Doc.startBatch(input, output, {
            mode: ["compress", "watermark", "protect"][mode.currentIndex],
            watermark: watermark.text, user: protect ? user.text : "", owner: protect ? owner.text : ""
        })
        user.clear()
        owner.clear()
        confirm.clear()
    }
    Connections {
        target: Doc
        function onBatchFinished(error, rows) {
            if (dlg.visible) {
                dlg.error = error
                dlg.rows = rows
            }
        }
    }
    contentItem: ColumnLayout {
        spacing: 7
        AppText { text: Tr.s("batchPdf"); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold }
        AppText { Layout.fillWidth: true; text: Tr.s("batchHint"); wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            enabled: !Doc.outputBusy
            PButton { text: Tr.s("batchInput"); onClicked: sourcePicker.open() }
            AppText { Layout.fillWidth: true; text: dlg.input; textFormat: Text.PlainText; elide: Text.ElideMiddle }
        }
        RowLayout {
            Layout.fillWidth: true
            enabled: !Doc.outputBusy
            PButton { text: Tr.s("batchOutput"); onClicked: outputPicker.open() }
            AppText { Layout.fillWidth: true; text: dlg.output; textFormat: Text.PlainText; elide: Text.ElideMiddle }
        }
        ComboBox {
            id: mode
            objectName: "batchMode"
            enabled: !Doc.outputBusy
            model: [Tr.s("toolCompress"), Tr.s("batchWatermark"), Tr.s("protectPdf")]
        }
        TextField {
            id: watermark
            visible: mode.currentIndex === 1
            enabled: !Doc.outputBusy
            Layout.fillWidth: true
            maximumLength: 120
            placeholderText: Tr.s("batchWatermark")
        }
        TextField { id: user; visible: dlg.protect; enabled: !Doc.outputBusy; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: Tr.s("userPasswordOptional") }
        TextField { id: owner; visible: dlg.protect; enabled: !Doc.outputBusy; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: Tr.s("ownerPassword") }
        TextField { id: confirm; visible: dlg.protect; enabled: !Doc.outputBusy; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: Tr.s("repeatOwnerPassword") }
        ProgressBar { Layout.fillWidth: true; visible: Doc.outputBusy; value: Doc.outputProgress }
        AppText { Layout.fillWidth: true; visible: dlg.error !== ""; text: Tr.outputError(dlg.error); color: Theme.errorSoft; wrapMode: Text.Wrap }
        ListView {
            objectName: "batchReport"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(126, count * 25)
            visible: count > 0
            clip: true
            model: dlg.rows
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width - 14
                height: 25
                AppText { Layout.fillWidth: true; text: modelData.name; textFormat: Text.PlainText; elide: Text.ElideMiddle }
                AppText { text: modelData.status === "" ? Tr.s("batchSaved") : Tr.outputError(modelData.status); elide: Text.ElideRight; Layout.maximumWidth: 270 }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PButton {
                objectName: "batchClose"
                text: Tr.s(Doc.outputBusy ? "cancel" : "close")
                onClicked: { if (Doc.outputBusy) Doc.cancelImageExport(); else dlg.close() }
            }
            PButton {
                objectName: "batchStart"
                text: Tr.s("batchStart")
                variant: "primary"
                enabled: !Doc.outputBusy && dlg.input !== "" && dlg.output !== ""
                    && (mode.currentIndex !== 1 || watermark.text.trim() !== "") && (!dlg.protect || owner.text !== "")
                onClicked: dlg.start()
            }
        }
    }
    FolderDialog { id: sourcePicker; title: Tr.s("batchInput"); onAccepted: dlg.input = Doc.localPath(selectedFolder.toString()) }
    FolderDialog { id: outputPicker; title: Tr.s("batchOutput"); onAccepted: dlg.output = Doc.localPath(selectedFolder.toString()) }
}
