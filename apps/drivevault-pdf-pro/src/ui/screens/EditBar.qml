import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// The edit tool bar: tools, the style of the next item (or of the selected one), undo / redo, marks on every page, and "Save as".
// What is put on the pages is burned into a NEW file; the open file is never changed.
Rectangle {
    id: bar
    objectName: "editBar"

    readonly property var sel: { Editor.rev; Editor.selectedId; return Editor.selectedId > 0 ? Editor.selectedInfo() : ({}) }
    property string stampText: "APPROVED"
    property string message: ""
    property bool messageIsError: false
    property string result: ""
    property bool askReplace: false
    property string pendingPath: ""
    signal openResultRequested(string path)
    readonly property var swatches: ["#000000", "#EF4444", "#F59E0B", "#22C55E", "#3B82F6", "#8B5CF6", "#FFFFFF", "#6B7280"]
    readonly property var toolNames: ["select", "text", "image", "rect", "ellipse", "line", "arrow", "pen", "highlight", "underline", "strike", "stamp", "polygon", "note", "formText", "formCheck", "formChoice"]

    implicitHeight: flow.implicitHeight + 16 + msgRow.height
    color: Theme.editBar
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }

    function save(path, overwrite) {
        message = ""
        result = ""
        const code = Editor.save(path, overwrite)
        if (code === "exists") {
            pendingPath = path
            askReplace = true
        } else if (code === "") {
            message = ""
        } else if (code === "fontRestricted") {
            fail(Tr.f("fontRestrictedText", Editor.restrictedFontNames().join(", ")))
        } else {
            fail(Tr.s(code === "noEdits" ? "nothingToSave" : code === "sameFile" ? "errSameFile" : code === "busy" ? "busyNow" : "errSaveUnknown"))
        }
    }
    function fail(text) { message = text; messageIsError = true }
    function pickColor(c) { Editor.setStyle("color", c) }

    Connections {
        target: Editor
        function onSaveFinished(ok, err, detail, warnings, pages, bytes, path) {
            if (ok) {
                bar.result = path
                bar.message = Tr.f2("editSaved", pages, Math.max(1, Math.round(bytes / 1024)))
                bar.messageIsError = false
            } else if (err === "cancelled") {
                bar.fail(Tr.s("saveCancelled"))
            } else if (err === "imageMissing") {
                bar.fail(Tr.f("errImageMissing", detail))
            } else if (err === "password") {
                bar.fail(Tr.s("errPasswordFile"))
            } else {
                bar.fail(Tr.s(err === "write" ? "errWrite" : "errSaveUnknown"))
            }
        }
    }

    Flow {
        id: flow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 6

        // tools
        Repeater {
            model: bar.toolNames
            delegate: PButton {
                id: tb
                required property string modelData
                objectName: "tool_" + modelData
                compact: true
                text: Tr.s("tool_" + modelData)
                variant: Editor.tool === modelData ? "secondary" : "ghost"
                onClicked: {
                    if (modelData === "image")
                        imageDialog.open()
                    else
                        Editor.tool = modelData
                }
            }
        }
        Item { width: 5; height: 32; Rectangle { anchors.centerIn: parent; width: 1; height: 22; color: Theme.border } }

        // colour of lines and text
        Row {
            spacing: 4
            height: 32
            Repeater {
                model: bar.swatches
                delegate: Rectangle {
                    required property string modelData
                    objectName: "color_" + modelData.substring(1)
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20; height: 20; radius: 10
                    color: modelData
                    border.width: Qt.colorEqual(Editor.color, modelData) ? 3 : 1
                    border.color: Qt.colorEqual(Editor.color, modelData) ? Theme.accent : Theme.border
                    TapHandler { onTapped: bar.pickColor(parent.color) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }
        }
        AppText { height: 32; verticalAlignment: Text.AlignVCenter; text: Tr.s("fillLabel"); color: Theme.textSecondary }
        Row {
            spacing: 4
            height: 32
            Rectangle {
                objectName: "fill_none"
                anchors.verticalCenter: parent.verticalCenter
                width: 20; height: 20; radius: 10
                color: "transparent"
                border.width: !Editor.hasFill ? 3 : 1
                border.color: !Editor.hasFill ? Theme.accent : Theme.border
                Rectangle { anchors.centerIn: parent; width: 16; height: 2; rotation: -45; color: Theme.errorSoft }
                TapHandler { onTapped: Editor.setStyle("noFill", true) }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }
            Repeater {
                model: bar.swatches
                delegate: Rectangle {
                    required property string modelData
                    objectName: "fill_" + modelData.substring(1)
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20; height: 20; radius: 10
                    color: modelData
                    border.width: Editor.hasFill && Qt.colorEqual(Editor.fillColor, modelData) ? 3 : 1
                    border.color: Editor.hasFill && Qt.colorEqual(Editor.fillColor, modelData) ? Theme.accent : Theme.border
                    TapHandler { onTapped: Editor.setStyle("fill", parent.color) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }
        }
        Item { width: 5; height: 32; Rectangle { anchors.centerIn: parent; width: 1; height: 22; color: Theme.border } }

        AppText { height: 32; verticalAlignment: Text.AlignVCenter; text: Tr.s("lineWidth"); color: Theme.textSecondary }
        SpinBox { objectName: "editWidth"; from: 0; to: 40; value: Math.round(Editor.strokeWidth); editable: true; implicitHeight: 32; onValueModified: Editor.setStyle("width", value) }
        AppText { height: 32; verticalAlignment: Text.AlignVCenter; text: Tr.s("opacityLabel"); color: Theme.textSecondary }
        PSlider {
            objectName: "editOpacity"
            width: 90; height: 32
            from: 5; to: 100; stepSize: 5
            value: Math.round(Editor.opacity * 100)
            onMoved: Editor.setStyle("opacity", value / 100)
        }
        Item { width: 5; height: 32; Rectangle { anchors.centerIn: parent; width: 1; height: 22; color: Theme.border } }

        // text
        ComboBox {
            id: fontBox
            objectName: "editFont"
            width: 170; height: 32
            model: Editor.fonts
            currentIndex: Math.max(0, Editor.fonts.indexOf(Editor.fontFamily))
            onActivated: Editor.setStyle("font", currentText)
            font.pixelSize: Theme.fontMd
            contentItem: AppText { leftPadding: 10; text: fontBox.displayText; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { radius: 8; color: Theme.surface; border.color: fontBox.activeFocus ? Theme.accent : Theme.border }
        }
        SpinBox { objectName: "editSize"; from: 4; to: 400; value: Math.round(Editor.fontSize); editable: true; implicitHeight: 32; onValueModified: Editor.setStyle("size", value) }
        PButton { objectName: "editBold"; compact: true; text: "B"; tip: Tr.s("bold"); variant: Editor.bold ? "secondary" : "ghost"; onClicked: Editor.setStyle("bold", !Editor.bold) }
        PButton { objectName: "editItalic"; compact: true; text: "I"; tip: Tr.s("italic"); variant: Editor.italic ? "secondary" : "ghost"; onClicked: Editor.setStyle("italic", !Editor.italic) }
        PButton { objectName: "editAlignLeft"; compact: true; text: "⇤"; tip: Tr.s("alignLeft"); variant: Editor.align === 0 ? "secondary" : "ghost"; onClicked: Editor.setStyle("align", 0) }
        PButton { objectName: "editAlignCenter"; compact: true; text: "↔"; tip: Tr.s("alignCenter"); variant: Editor.align === 1 ? "secondary" : "ghost"; onClicked: Editor.setStyle("align", 1) }
        PButton { objectName: "editAlignRight"; compact: true; text: "⇥"; tip: Tr.s("alignRight"); variant: Editor.align === 2 ? "secondary" : "ghost"; onClicked: Editor.setStyle("align", 2) }
        ComboBox {
            id: stampBox
            objectName: "editStamp"
            width: 150; height: 32
            editable: true
            model: ["APPROVED", "DRAFT", "CONFIDENTIAL", "REJECTED", "FINAL", "ผ่านการอนุมัติ", "ลับ"]
            onEditTextChanged: bar.stampText = editText
            onActivated: bar.stampText = currentText
            font.pixelSize: Theme.fontMd
            contentItem: PTextField {
                text: stampBox.editText
                leftPadding: 10
                color: Theme.text
                font: stampBox.font
                verticalAlignment: TextInput.AlignVCenter
                onTextEdited: stampBox.editText = text
                background: null
            }
            background: Rectangle { radius: 8; color: Theme.surface; border.color: stampBox.activeFocus ? Theme.accent : Theme.border }
        }
        Item { width: 5; height: 32; Rectangle { anchors.centerIn: parent; width: 1; height: 22; color: Theme.border } }

        // the selected item
        PButton { objectName: "editReplaceImage"; compact: true; visible: bar.sel.kind === "image"; text: Tr.s("replaceImage"); tip: Tr.s("replaceImageTip"); onClicked: replaceImageDialog.open() }
        PButton { objectName: "editTurn"; compact: true; text: "↻ 90°"; tip: Tr.s("turnItem"); enabled: Editor.selectedId > 0; onClicked: Editor.rotateSelected(90) }
        PButton { objectName: "editFront"; compact: true; text: Tr.s("toFront"); enabled: Editor.selectedId > 0; onClicked: Editor.toFront() }
        PButton { objectName: "editBack"; compact: true; text: Tr.s("toBack"); enabled: Editor.selectedId > 0; onClicked: Editor.toBack() }
        PButton { objectName: "editDelete"; compact: true; text: Tr.s("deletePages"); enabled: Editor.selectedId > 0; onClicked: Editor.deleteSelected() }
        PButton { objectName: "editSnap"; compact: true; variant: Editor.snapEnabled ? "secondary" : "ghost"; text: Tr.s("snapGuides"); tip: Tr.s("snapGuidesTip"); onClicked: Editor.snapEnabled = !Editor.snapEnabled }
        PButton { objectName: "editUndo"; compact: true; variant: "ghost"; text: Tr.s("undo"); enabled: Editor.canUndo; onClicked: Editor.undo() }
        PButton { objectName: "editRedo"; compact: true; variant: "ghost"; text: Tr.s("redo"); enabled: Editor.canRedo; onClicked: Editor.redo() }
        Item { width: 5; height: 32; Rectangle { anchors.centerIn: parent; width: 1; height: 22; color: Theme.border } }

        PTextField { objectName: "editFormOptions"; visible: bar.sel.kind === "formChoice"; width: 230; implicitHeight: 32; height: 32; placeholderText: Tr.s("formOptions"); text: bar.sel.optionsText || ""; onEditingFinished: Editor.setOptions(Editor.selectedId, text) }
        PButton { objectName: "editMarks"; compact: true; text: Tr.s("marksButton"); tip: Tr.s("marksTip"); onClicked: marks.open() }
        PButton { objectName: "editDiscard"; compact: true; variant: "ghost"; text: Tr.s("discardAll"); enabled: Editor.count > 0; onClicked: discardAsk.open() }
        PButton { objectName: "editSave"; text: Editor.busy ? Tr.s("saving") : Tr.s("saveAs"); variant: "primary"; implicitHeight: 32; enabled: Editor.count > 0 && !Editor.busy; onClicked: saveDialog.open() }
        PButton { objectName: "editCancelSave"; compact: true; variant: "ghost"; visible: Editor.busy; text: Tr.s("cancel"); onClicked: Editor.cancelSave() }
        PButton { objectName: "editClose"; compact: true; variant: "ghost"; text: "✕"; tip: Tr.s("closeEditor"); onClicked: Editor.active = false }
    }

    // a message under the tools
    Row {
        id: msgRow
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.top: flow.bottom
        anchors.topMargin: 2
        spacing: 10
        visible: bar.message !== "" || bar.askReplace
        height: visible ? 30 : 0
        AppText { objectName: "editMessage"; visible: bar.message !== ""; height: 30; verticalAlignment: Text.AlignVCenter; text: (bar.result !== "" ? Tr.s("saved") + " " + bar.result + "  " : "") + bar.message; color: bar.messageIsError ? Theme.errorSoft : Theme.success }
        PButton { objectName: "editOpenResult"; compact: true; visible: bar.result !== ""; text: Tr.s("openResult"); onClicked: { const p = bar.result; bar.result = ""; bar.message = ""; bar.openResultRequested(p) } }
        AppText { visible: bar.askReplace; height: 30; verticalAlignment: Text.AlignVCenter; text: Tr.f("replaceAsk", bar.pendingPath.split(/[\\/]/).pop()); color: Theme.warning }
        PButton { objectName: "editReplaceNo"; compact: true; visible: bar.askReplace; variant: "ghost"; text: Tr.s("cancel"); onClicked: bar.askReplace = false }
        PButton { objectName: "editReplaceYes"; compact: true; visible: bar.askReplace; text: Tr.s("replace"); onClicked: { bar.askReplace = false; bar.save(bar.pendingPath, true) } }
    }

    FileDialog {
        id: imageDialog
        title: Tr.s("tool_image")
        nameFilters: [Tr.s("imageFiles")]
        onAccepted: {
            Editor.pendingImage = selectedFile.toString()
            Editor.tool = "image"
        }
    }
    FileDialog {
        id: replaceImageDialog
        title: Tr.s("replaceImage")
        nameFilters: [Tr.s("imageFiles")]
        onAccepted: {
            const code = Editor.replaceImage(Editor.selectedId, selectedFile.toString())
            if (code !== "")
                bar.fail(Tr.s(code === "format" ? "errFormat" : "errFile"))
        }
    }
    FileDialog {
        id: saveDialog
        title: Tr.s("saveAs")
        nameFilters: [Tr.s("pdfFiles")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        currentFolder: Doc.isOpen ? "file:///" + Doc.filePath.replace(/\\/g, "/").replace(/\/[^\/]*$/, "") : ""
        onAccepted: bar.save(Doc.localPath(selectedFile.toString()), false)
    }
    ConfirmDialog {
        id: discardAsk
        title: Tr.s("discardAll")
        text: Tr.s("discardEditsAsk")
        confirmText: Tr.s("discard")
        cancelText: Tr.s("keepEditing")
        onConfirmed: Editor.discardEdits()
    }
    MarkDialog { id: marks }
}
