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

    Text {
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
                    Text { Layout.fillWidth: true; text: row.modelData.name; font.pixelSize: Theme.fontSm; font.weight: Font.DemiBold; color: Theme.text; elide: Text.ElideRight }
                    Text { Layout.fillWidth: true; text: row.modelData.description + "  ·  " + row.modelData.sizeMb + " MB"; font.pixelSize: Theme.fontXs; color: Theme.textSecondary; wrapMode: Text.Wrap }
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
        Text { text: Math.round(root.mm.downloadProgress * 100) + "%"; font.pixelSize: Theme.fontXs; color: Theme.textSecondary }
        AppButton { text: qsTr("Cancel"); variant: "ghost"; implicitHeight: 26; onClicked: root.mm.cancelDownload() }
    }

    // ---- the language model of "Edit by prompt" ---------------------------------------------------------------------------------------
    readonly property var llm: Ai.llm
    Connections {
        target: root.llm
        function onDownloadFinished(ok, message) {
            App.toast(ok ? qsTr("Language model downloaded.") : qsTr("Download failed: %1").arg(message), ok ? "success" : "warning")
        }
    }
    ConfirmDialog {
        id: deleteLlmDialog
        title: qsTr("Delete the language model?")
        message: qsTr("The model file is removed from the model folder. You can download it again later.")
        primaryText: qsTr("Delete")
        dangerPrimary: true
        onPrimary: { const e = root.llm.remove(); if (e !== "") App.toast(e, "warning") }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: 8
        implicitHeight: llmColumn.implicitHeight + 20
        radius: Theme.radiusSmall + 2
        color: Theme.surface
        border.width: 1
        border.color: root.llm.available ? Theme.accent : Theme.borderSubtle
        ColumnLayout {
            id: llmColumn
            anchors.fill: parent
            anchors.margins: 10
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { Layout.fillWidth: true; text: qsTr("Language model for prompts (%1)").arg(root.llm.modelName); font.pixelSize: Theme.fontSm; font.weight: Font.DemiBold; color: Theme.text; elide: Text.ElideRight }
                StatusBadge {
                    objectName: "llmStatus"
                    kind: !root.llm.engineAvailable ? "unsupported" : root.llm.installed ? "available" : "modelRequired"
                    label: !root.llm.engineAvailable ? qsTr("Engine missing") : root.llm.installed ? qsTr("Ready") : qsTr("Not installed")
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Understands free Thai and English sentences for \"Edit by prompt\". It runs on this computer only (about 3 GB of memory while it works) and the plan is always shown before anything is changed. Qwen3.5 by Alibaba (Apache 2.0), run by llama.cpp (MIT).")
                font.pixelSize: Theme.fontXs
                color: Theme.textSecondary
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                AppButton {
                    objectName: "llmDownload"
                    visible: !root.llm.installed && root.llm.engineAvailable && !root.llm.downloading
                    text: qsTr("Download (%1 MB)").arg(root.llm.modelSizeMb)
                    iconName: "import"
                    implicitHeight: 28
                    onClicked: { const e = root.llm.download(); if (e !== "") App.toast(e, "warning") }
                }
                AppButton { visible: root.llm.downloading; text: qsTr("Cancel"); variant: "ghost"; implicitHeight: 26; onClicked: root.llm.cancelDownload() }
                Rectangle {
                    visible: root.llm.downloading
                    Layout.fillWidth: true
                    height: 6
                    radius: 3
                    color: Theme.surfaceHover
                    Rectangle { width: parent.width * root.llm.downloadProgress; height: parent.height; radius: 3; color: Theme.accent }
                }
                Text { visible: root.llm.downloading; text: Math.round(root.llm.downloadProgress * 100) + "%"; font.pixelSize: Theme.fontXs; color: Theme.textSecondary }
                Item { visible: !root.llm.downloading; Layout.fillWidth: true }
                AppSwitch {
                    objectName: "llmEnable"
                    visible: root.llm.installed
                    checked: root.llm.enabled
                    onToggled: root.llm.enabled = checked
                    Accessible.name: qsTr("Use the language model")
                }
                IconButton {
                    visible: root.llm.installed
                    iconName: "trash"
                    tip: qsTr("Delete the model file")
                    onClicked: deleteLlmDialog.open()
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        AppButton { objectName: "modelImport"; iconName: "folder"; text: qsTr("Import model…"); onClicked: importDialog.open() }
        Item { Layout.fillWidth: true }
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("Downloads only start when you press Download. Speech models come from the whisper.cpp project and the language model from Unsloth, both on Hugging Face; audio, video and your prompts never leave this computer.")
        font.pixelSize: Theme.fontXs
        color: Theme.textDisabled
        wrapMode: Text.Wrap
    }
}
