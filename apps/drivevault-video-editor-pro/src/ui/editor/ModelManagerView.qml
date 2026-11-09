import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.Backend

// Speech model manager: pick, import, download and delete the ggml models used for captions.
ColumnLayout {
    id: root

    readonly property var mm: Ai.models
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    spacing: 8

    FileDialog {
        id: importDialog
        title: qsTr("Import a speech model (ggml-*.bin)")
        nameFilters: [qsTr("Speech model (*.bin)"), qsTr("All files (*)")]
        onAccepted: {
            const err = root.mm.importModel(selectedFile.toString())
            App.toast(err !== "" ? err : qsTr("Model imported."), err !== "" ? "warning" : "success")
        }
    }
    ConfirmDialog {
        id: deleteDialog
        property string modelId
        title: qsTr("Delete this model?")
        message: qsTr("The model file is removed from the model folder. You can download or import it again later.")
        primaryText: qsTr("Delete")
        dangerPrimary: true
        onPrimary: { const e = root.mm.remove(modelId); if (e !== "") App.toast(e, "warning") }
    }
    Connections {
        target: root.mm
        function onDownloadFinished(ok, message) {
            App.toast(ok ? qsTr("Model downloaded.") : qsTr("Download failed: %1").arg(message), ok ? "success" : "warning")
        }
    }

    AppText {
        Layout.fillWidth: true
        text: qsTr("Folder: %1").arg(root.mm.directory)
        font.pixelSize: Theme.fontXs
        color: Theme.textDisabled
        elide: Text.ElideMiddle
    }

    Repeater {
        model: root.mm.models
        delegate: Rectangle {
            id: row
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: rowLayout.implicitHeight + 16
            radius: Theme.radiusSmall + 2
            color: modelData.selected ? Theme.accentSubtle : Theme.surface
            border.width: 1
            border.color: modelData.selected ? Theme.accent : Theme.borderSubtle

            RowLayout {
                id: rowLayout
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    AppText { Layout.fillWidth: true; text: row.modelData.name; font.pixelSize: Theme.fontSm; font.weight: Font.DemiBold; color: Theme.text; elide: Text.ElideRight }
                    AppText { Layout.fillWidth: true; text: row.modelData.description + "  ·  " + row.modelData.sizeMb + " MB"; font.pixelSize: Theme.fontXs; color: Theme.textSecondary; wrapMode: Text.Wrap }
                }
                AppButton {
                    objectName: "modelUse_" + row.modelData.id
                    visible: row.modelData.installed && !row.modelData.selected
                    text: qsTr("Use")
                    implicitHeight: 28
                    onClicked: root.mm.select(row.modelData.id)
                }
                StatusBadge { visible: row.modelData.selected; kind: "available"; label: qsTr("In use") }
                AppButton {
                    objectName: "modelGet_" + row.modelData.id
                    visible: !row.modelData.installed && row.modelData.downloadable
                    enabled: !root.mm.downloading
                    text: qsTr("Download")
                    iconName: "import"
                    implicitHeight: 28
                    onClicked: { const e = root.mm.download(row.modelData.id); if (e !== "") App.toast(e, "warning") }
                }
                IconButton {
                    visible: row.modelData.removable
                    iconName: "trash"
                    tip: qsTr("Delete the model file")
                    onClicked: { deleteDialog.modelId = row.modelData.id; deleteDialog.open() }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: root.mm.downloading
        spacing: 8
        Rectangle {
            Layout.fillWidth: true
            height: 6
            radius: 3
            color: Theme.surfaceHover
            Rectangle { width: parent.width * root.mm.downloadProgress; height: parent.height; radius: 3; color: Theme.accent }
        }
        AppText { text: Math.round(root.mm.downloadProgress * 100) + "%"; font.pixelSize: Theme.fontXs; color: Theme.textSecondary }
        AppButton { text: qsTr("Cancel"); variant: "ghost"; implicitHeight: 26; onClicked: root.mm.cancelDownload() }
    }

    RowLayout {
        Layout.fillWidth: true
        AppButton { objectName: "modelImport"; iconName: "folder"; text: qsTr("Import model…"); onClicked: importDialog.open() }
        Item { Layout.fillWidth: true }
    }
    AppText {
        Layout.fillWidth: true
        text: qsTr("Downloads only start when you press Download. Speech models come from the whisper.cpp project on Hugging Face; your audio and video never leave this computer.")
        font.pixelSize: Theme.fontXs
        color: Theme.textDisabled
        wrapMode: Text.Wrap
    }
}
