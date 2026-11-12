import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// Fill in the fields of a PDF form (AcroForm). The values are written into a NEW copy; the open file is never changed.
Popup {
    id: dlg
    objectName: "formDialog"

    property var edits: ({})
    property string message: ""
    property bool messageIsError: false
    property string result: ""
    property bool askReplace: false
    property string pendingPath: ""
    signal openResultRequested(string path)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    width: Math.min(680, (parent ? parent.width : 680) - 48)
    height: Forms.fields.length > 0 ? Math.min(700, (parent ? parent.height : 700) - 48) : Math.min(contentItem.implicitHeight + topPadding + bottomPadding, (parent ? parent.height : 700) - 48)
    padding: 24
    closePolicy: Forms.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#B3000000" }
    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    function setValue(name, value) {
        const next = Object.assign({}, edits)
        next[name] = value
        edits = next
    }
    function reasonText(item) {
        const i = item.lastIndexOf(": ")
        const name = item.substring(0, i)
        const reason = item.substring(i + 2)
        return name + " (" + Tr.s("formSkip_" + reason) + ")"
    }
    function save(path, overwrite) {
        message = ""
        result = ""
        const code = Forms.save(edits, path, overwrite)
        if (code === "exists") {
            pendingPath = path
            askReplace = true
        } else if (code !== "") {
            fail(Tr.s(code === "sameFile" ? "errSameFile" : code === "noValues" ? "formNoChanges" : "busyNow"))
        }
    }
    function fail(text) { message = text; messageIsError = true }
    function exportTo(url) {
        message = ""
        result = ""
        const code = Forms.exportData(edits, url)
        if (code === "")
            { messageIsError = false; message = Tr.s("formDataExported") }
        else
            fail(Tr.s(code === "write" ? "errWrite" : "noFormFields"))
    }
    function importFrom(url) {
        message = ""
        result = ""
        const r = Forms.importData(url)
        if (!r.ok) {
            fail(Tr.s(r.error === "format" ? "formDataBad" : r.error === "noFields" ? "noFormFields" : "errFile"))
            return
        }
        const next = Object.assign({}, edits)
        for (const k in r.values)
            next[k] = r.values[k]
        edits = next
        messageIsError = false
        let t = Tr.f("formDataImported", r.matched)
        if (r.unknown.length > 0)
            t += "  " + Tr.f("formDataUnknown", r.unknown.length)
        if (r.skipped.length > 0) {
            const parts = []
            for (const s of r.skipped)
                parts.push(reasonText(s))
            t += "  " + Tr.s("formSkipped") + " " + parts.join(", ")
        }
        message = t
    }

    onAboutToShow: {
        edits = ({})
        message = ""
        result = ""
        askReplace = false
        Forms.load()
    }
    onClosed: edits = ({})

    Connections {
        target: Forms
        function onSaveFinished(ok, err, detail, filled, skipped, warnings, path) {
            if (ok) {
                dlg.result = path
                dlg.messageIsError = false
                let t = Tr.f("formFilled", filled)
                if (skipped.length > 0) {
                    const parts = []
                    for (const s of skipped)
                        parts.push(dlg.reasonText(s))
                    t += "  " + Tr.s("formSkipped") + " " + parts.join(", ")
                }
                if (warnings.indexOf("encrypted") >= 0)
                    t += "  " + Tr.s("stillProtected")
                dlg.message = t
            } else {
                dlg.fail(Tr.s(err === "password" ? "errPasswordFile" : err === "noForm" ? "noFormFields" : err === "write" ? "errWrite" : "errSaveUnknown"))
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 10
        AppText { Layout.fillWidth: true; text: Tr.s("formTitle") + (Doc.isOpen ? " - " + Doc.fileName : ""); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold; elide: Text.ElideMiddle }
        AppText { Layout.fillWidth: true; text: Tr.s("formHint"); color: Theme.textDisabled; font.pixelSize: Theme.fontSm; wrapMode: Text.Wrap }

        AppText { objectName: "formLoading"; visible: Forms.busy && !Forms.loaded; text: Tr.s("formLoading"); color: Theme.textSecondary }
        AppText {
            objectName: "formEmpty"
            Layout.fillWidth: true
            visible: Forms.loaded && Forms.fields.length === 0
            text: Forms.error === "" ? Tr.s("noFormFields") : Tr.s("errFile")
            color: Theme.warning
            wrapMode: Text.Wrap
        }
        AppText { Layout.fillWidth: true; visible: Forms.xfa; text: Tr.s("formXfa"); color: Theme.warning; font.pixelSize: Theme.fontSm; wrapMode: Text.Wrap }

        ListView {
            id: list
            objectName: "formList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            visible: Forms.fields.length > 0
            model: Forms.fields
            ScrollBar.vertical: ScrollBar {}
            delegate: ColumnLayout {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width - 14
                spacing: 3
                AppText {
                    Layout.fillWidth: true
                    text: row.modelData.name + (row.modelData.page >= 0 ? "   ·   " + Tr.f("pageN", row.modelData.page + 1) : "")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSm
                    elide: Text.ElideRight
                }
                Loader {
                    Layout.fillWidth: true
                    sourceComponent: row.modelData.type === "text" ? textEditor : row.modelData.type === "checkbox" ? checkEditor
                                   : row.modelData.type === "choice" ? choiceEditor : row.modelData.type === "radio" ? radioEditor : noEditor
                    property var field: row.modelData
                }
            }
        }
        Component {
            id: textEditor
            PTextField {
                objectName: "formField_" + field.name
                implicitHeight: field.multiline ? 74 : 36
                text: dlg.edits[field.name] !== undefined ? dlg.edits[field.name] : field.value
                enabled: !field.readOnly && !Forms.busy
                maximumLength: field.maxLength > 0 ? field.maxLength : 32767
                color: Theme.text
                font.pixelSize: Theme.fontMd
                leftPadding: 10
                selectByMouse: true
                onTextEdited: dlg.setValue(field.name, text)
                background: Rectangle { radius: 8; color: Theme.surface; border.color: parent.activeFocus ? Theme.accent : Theme.border; opacity: parent.enabled ? 1 : 0.5 }
            }
        }
        Component {
            id: checkEditor
            PCheckBox {
                objectName: "formField_" + field.name
                checked: dlg.edits[field.name] !== undefined ? dlg.edits[field.name] : field.checked
                enabled: !field.readOnly && !Forms.busy
                text: Tr.s("formTick")

                onToggled: dlg.setValue(field.name, checked)
            }
        }
        Component {
            id: choiceEditor
            ComboBox {
                id: cb
                objectName: "formField_" + field.name
                implicitHeight: 36
                model: field.options
                enabled: !field.readOnly && !Forms.busy
                currentIndex: field.options.indexOf(dlg.edits[field.name] !== undefined ? dlg.edits[field.name] : field.value)
                onActivated: dlg.setValue(field.name, currentText)
                font.pixelSize: Theme.fontMd
                contentItem: AppText { leftPadding: 10; text: cb.displayText; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: 8; color: Theme.surface; border.color: cb.activeFocus ? Theme.accent : Theme.border }
            }
        }
        Component {
            id: radioEditor
            ComboBox {
                id: rb
                objectName: "formField_" + field.name
                implicitHeight: 36
                model: [Tr.s("formNoChoice")].concat(field.options)
                enabled: !field.readOnly && !Forms.busy
                currentIndex: field.options.indexOf(dlg.edits[field.name] !== undefined ? dlg.edits[field.name] : field.value) + 1
                onActivated: dlg.setValue(field.name, currentIndex === 0 ? "" : currentText)
                font.pixelSize: Theme.fontMd
                contentItem: AppText { leftPadding: 10; text: rb.displayText; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: 8; color: Theme.surface; border.color: rb.activeFocus ? Theme.accent : Theme.border }
            }
        }
        Component { id: noEditor; AppText { text: Tr.s("formUnsupported"); color: Theme.textDisabled } }

        AppText { objectName: "formMessage"; Layout.fillWidth: true; visible: dlg.message !== ""; text: (dlg.result !== "" ? Tr.s("saved") + " " + dlg.result + "  " : "") + dlg.message; color: dlg.messageIsError ? Theme.errorSoft : Theme.success; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            visible: dlg.askReplace
            AppText { Layout.fillWidth: true; text: Tr.f("replaceAsk", dlg.pendingPath.split(/[\\/]/).pop()); color: Theme.warning; wrapMode: Text.Wrap }
            PButton { objectName: "formReplaceNo"; compact: true; variant: "ghost"; text: Tr.s("cancel"); onClicked: dlg.askReplace = false }
            PButton { objectName: "formReplaceYes"; compact: true; text: Tr.s("replace"); onClicked: { dlg.askReplace = false; dlg.save(dlg.pendingPath, true) } }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PButton { objectName: "formImportData"; variant: "ghost"; text: Tr.s("formImportData"); tip: Tr.s("formDataTip"); visible: Forms.fields.length > 0; enabled: !Forms.busy; onClicked: importDialog.open() }
            PButton { objectName: "formExportData"; variant: "ghost"; text: Tr.s("formExportData"); tip: Tr.s("formDataTip"); visible: Forms.fields.length > 0; enabled: !Forms.busy; onClicked: exportDialog.open() }
            Item { Layout.fillWidth: true }
            PButton { objectName: "formOpenResult"; visible: dlg.result !== ""; text: Tr.s("openResult"); onClicked: { const p = dlg.result; dlg.close(); dlg.openResultRequested(p) } }
            PButton { objectName: "formClose"; variant: "ghost"; text: Tr.s("close"); enabled: !Forms.busy; onClicked: dlg.close() }
            PButton { objectName: "formSave"; variant: "primary"; text: Forms.busy ? Tr.s("saving") : Tr.s("formSaveAs"); enabled: !Forms.busy && Object.keys(dlg.edits).length > 0; onClicked: saveDialog.open() }
        }
    }

    FileDialog {
        id: importDialog
        title: Tr.s("formImportData")
        nameFilters: [Tr.s("formDataFiles")]
        onAccepted: dlg.importFrom(selectedFile.toString())
    }
    FileDialog {
        id: exportDialog
        title: Tr.s("formExportData")
        nameFilters: [Tr.s("formDataFiles")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "xfdf"
        currentFolder: Doc.isOpen ? "file:///" + Doc.filePath.replace(/\\/g, "/").replace(/\/[^\/]*$/, "") : ""
        onAccepted: dlg.exportTo(selectedFile.toString())
    }
    FileDialog {
        id: saveDialog
        title: Tr.s("formSaveAs")
        nameFilters: [Tr.s("pdfFiles")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        currentFolder: Doc.isOpen ? "file:///" + Doc.filePath.replace(/\\/g, "/").replace(/\/[^\/]*$/, "") : ""
        onAccepted: dlg.save(Doc.localPath(selectedFile.toString()), false)
    }
}
