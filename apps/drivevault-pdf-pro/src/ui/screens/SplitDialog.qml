import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// Split the open file into several new files: by page ranges, every N pages, by (top level) bookmarks or by a maximum file size.
// The open file is not changed; existing files are never overwritten (a name gets " (2)").
Popup {
    id: dlg
    objectName: "splitDialog"

    property string mode: "ranges"
    property var preview: []
    property string error: ""
    property var resultFiles: []
    property var resultWarnings: []
    property bool hasBookmarks: false

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    width: Math.min(620, (parent ? parent.width : 620) - 48)
    height: Math.min(640, (parent ? parent.height : 640) - 48)
    padding: 24
    closePolicy: Doc.splitting ? Popup.NoAutoClose : Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#B3000000" }

    function folderOfOpenFile() {
        return Doc.isOpen ? Doc.filePath.replace(/[\\/][^\\/]*$/, "") : ""
    }
    function param() {
        return mode === "every" ? everyBox.value : mode === "ranges" ? rangeField.text : ""
    }
    function refresh() {
        error = ""
        preview = mode === "size" ? [] : Doc.splitPreview(mode, param())
    }
    function start() {
        error = ""
        resultFiles = []
        const code = Doc.startSplit({
            mode: mode,
            text: rangeField.text,
            n: everyBox.value,
            sizeMb: parseFloat(sizeField.text.replace(",", ".")),
            outDir: folderField.text
        })
        if (code !== "")
            error = Tr.s("split_" + code)
    }
    onAboutToShow: {
        hasBookmarks = Doc.outline().length > 0
        if (mode === "bookmarks" && !hasBookmarks)
            mode = "ranges"
        folderField.text = folderOfOpenFile()
        resultFiles = []
        resultWarnings = []
        everyBox.to = Math.max(1, Doc.pageCount - 1)
        refresh()
    }

    Connections {
        target: Doc
        function onSplitFinished(ok, err, files, warnings, errorFile) {
            if (ok) {
                dlg.resultFiles = files
                dlg.resultWarnings = warnings
                dlg.error = ""
            } else if (err === "cancelled") {
                dlg.error = Tr.s("splitCancelled")
            } else {
                const names = { password: "errPasswordFile", format: "errFormatFile", file: "errFileFile", range: "errRange", exists: "errExists", write: "errWrite" }
                dlg.error = Tr.s(names[err] !== undefined ? names[err] : "errSaveUnknown") + (errorFile !== "" ? " (" + errorFile.split(/[\\/]/).pop() + ")" : "")
            }
        }
    }

    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 10
        AppText { Layout.fillWidth: true; text: Tr.s("splitTitle") + (Doc.isOpen ? " - " + Doc.fileName : ""); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold; elide: Text.ElideMiddle }

        ButtonGroup { id: modes }
        RadioButton {
            id: rRanges; objectName: "splitRanges"; ButtonGroup.group: modes; text: Tr.s("splitRanges"); checked: dlg.mode === "ranges"; enabled: !Doc.splitting
            contentItem: AppText { leftPadding: 28; text: parent.text; verticalAlignment: Text.AlignVCenter }
            onClicked: { dlg.mode = "ranges"; dlg.refresh() }
        }
        TextField {
            id: rangeField
            objectName: "splitRangeField"
            Layout.fillWidth: true
            Layout.leftMargin: 28
            implicitHeight: 34
            enabled: dlg.mode === "ranges" && !Doc.splitting
            placeholderText: Tr.s("splitRangesHint")
            placeholderTextColor: Theme.textDisabled
            color: Theme.text
            font.pixelSize: Theme.fontMd
            leftPadding: 10
            onTextChanged: dlg.refresh()
            background: Rectangle { radius: 8; color: Theme.surface; border.color: rangeField.activeFocus ? Theme.accent : Theme.border }
        }
        RowLayout {
            Layout.fillWidth: true
            RadioButton {
                id: rEvery; objectName: "splitEvery"; ButtonGroup.group: modes; text: Tr.s("splitEvery"); checked: dlg.mode === "every"; enabled: !Doc.splitting
                contentItem: AppText { leftPadding: 28; text: parent.text; verticalAlignment: Text.AlignVCenter }
                onClicked: { dlg.mode = "every"; dlg.refresh() }
            }
            SpinBox { id: everyBox; objectName: "splitEveryN"; from: 1; to: 9999; value: 1; editable: true; enabled: dlg.mode === "every" && !Doc.splitting; onValueChanged: dlg.refresh() }
            AppText { text: Tr.s("splitPagesUnit"); color: Theme.textSecondary }
        }
        RadioButton {
            id: rBook; objectName: "splitBookmarks"; ButtonGroup.group: modes; text: Tr.s("splitBookmarks") + (dlg.hasBookmarks ? "" : " (" + Tr.s("noBookmarks") + ")"); checked: dlg.mode === "bookmarks"; enabled: dlg.hasBookmarks && !Doc.splitting
            contentItem: AppText { leftPadding: 28; text: parent.text; color: parent.enabled ? Theme.text : Theme.textDisabled; verticalAlignment: Text.AlignVCenter }
            onClicked: { dlg.mode = "bookmarks"; dlg.refresh() }
        }
        RowLayout {
            Layout.fillWidth: true
            RadioButton {
                id: rSize; objectName: "splitSize"; ButtonGroup.group: modes; text: Tr.s("splitSize"); checked: dlg.mode === "size"; enabled: !Doc.splitting
                contentItem: AppText { leftPadding: 28; text: parent.text; verticalAlignment: Text.AlignVCenter }
                onClicked: { dlg.mode = "size"; dlg.refresh() }
            }
            TextField {
                id: sizeField
                objectName: "splitSizeField"
                implicitWidth: 80
                implicitHeight: 34
                text: "5"
                enabled: dlg.mode === "size" && !Doc.splitting
                color: Theme.text
                font.pixelSize: Theme.fontMd
                leftPadding: 10
                validator: DoubleValidator { bottom: 0.01; top: 100000; decimals: 2; notation: DoubleValidator.StandardNotation }
                background: Rectangle { radius: 8; color: Theme.surface; border.color: sizeField.activeFocus ? Theme.accent : Theme.border }
            }
            AppText { text: "MB"; color: Theme.textSecondary }
        }

        // what will be written
        AppText { text: Tr.s("splitPreview"); color: Theme.textSecondary; font.pixelSize: Theme.fontSm }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 90
            radius: 8
            color: Theme.surface
            border.color: Theme.borderSubtle
            clip: true
            AppText {
                anchors.centerIn: parent
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: dlg.resultFiles.length === 0 && (dlg.mode === "size" || dlg.preview.length === 0)
                color: Theme.textSecondary
                text: dlg.mode === "size" ? Tr.s("splitSizeNote") : Tr.s("splitNothing")
            }
            ListView {
                id: previewList
                objectName: "splitPreviewList"
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                visible: dlg.resultFiles.length === 0
                model: dlg.preview
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    id: prow
                    required property var modelData
                    required property int index
                    width: ListView.view.width - 12
                    spacing: 10
                    AppText { Layout.fillWidth: true; text: prow.modelData.name; elide: Text.ElideMiddle }
                    AppText { text: Tr.f2("splitPartInfo", prow.modelData.pages, prow.modelData.count); color: Theme.textSecondary }
                }
            }
            ListView {
                id: resultList
                objectName: "splitResultList"
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                visible: dlg.resultFiles.length > 0
                model: dlg.resultFiles
                ScrollBar.vertical: ScrollBar {}
                delegate: AppText {
                    required property string modelData
                    width: ListView.view.width - 12
                    text: "✓ " + modelData.split(/[\\/]/).pop()
                    color: Theme.success
                    elide: Text.ElideMiddle
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            AppText { text: Tr.s("splitFolder"); color: Theme.textSecondary }
            TextField {
                id: folderField
                objectName: "splitFolderField"
                Layout.fillWidth: true
                implicitHeight: 34
                enabled: !Doc.splitting
                color: Theme.text
                font.pixelSize: Theme.fontMd
                leftPadding: 10
                selectByMouse: true
                background: Rectangle { radius: 8; color: Theme.surface; border.color: folderField.activeFocus ? Theme.accent : Theme.border }
            }
            PButton { objectName: "splitBrowse"; compact: true; text: Tr.s("browse"); enabled: !Doc.splitting; onClicked: folderDialog.open() }
        }

        AppText { objectName: "splitError"; Layout.fillWidth: true; visible: dlg.error !== ""; text: dlg.error; color: Theme.errorSoft; wrapMode: Text.Wrap }
        AppText {
            objectName: "splitDone"
            Layout.fillWidth: true
            visible: dlg.resultFiles.length > 0
            wrapMode: Text.Wrap
            color: Theme.success
            text: {
                let t = Tr.f2("splitDone", dlg.resultFiles.length, folderField.text)
                for (const w of dlg.resultWarnings)
                    t += "  " + Tr.warning(w)
                return t
            }
        }
        ProgressBar { objectName: "splitProgress"; Layout.fillWidth: true; visible: Doc.splitting; from: 0; to: 1; value: Doc.splitProgress }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PButton { objectName: "splitCancel"; variant: "ghost"; text: Doc.splitting ? Tr.s("cancel") : Tr.s("close"); onClicked: { if (Doc.splitting) Doc.cancelSplit(); else dlg.close() } }
            PButton {
                objectName: "splitStart"
                variant: "primary"
                text: Doc.splitting ? Tr.s("splitting") : Tr.s("splitNow")
                enabled: !Doc.splitting && folderField.text !== "" && (dlg.mode === "size" ? parseFloat(sizeField.text.replace(",", ".")) > 0 : dlg.preview.length > 0)
                onClicked: dlg.start()
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: Tr.s("splitFolder")
        onAccepted: folderField.text = Doc.localPath(selectedFolder.toString())
    }
}
