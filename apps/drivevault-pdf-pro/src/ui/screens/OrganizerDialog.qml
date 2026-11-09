import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// Merge / reorder / delete / duplicate / rotate / crop pages and add blank pages or pages of other files; then save the result as a new file.
// The list lives in the backend object `Organizer`; nothing is written until "Save as". Source files are never modified.
Popup {
    id: dlg
    objectName: "organizerDialog"

    property string mode: "organize"                // "organize" | "merge" (only the title and the first step differ)
    property var sel: ({})                          // selected rows: {row: true}; replaced (not changed) so bindings update
    property int anchorRow: -1
    property int dropRow: -1                        // where a dragged page would land
    property string result: ""                      // path of the file that was written
    property string resultNote: ""
    property string error: ""
    property bool askDiscard: false
    property bool askReplace: false
    property string pendingPath: ""
    property var pendingRows: []
    property var addQueue: []                       // file urls still to add
    property int addAt: -1
    property string addPassword: ""
    readonly property int selCount: selRows().length
    readonly property real thumbW: 140
    readonly property real thumbH: 180
    signal openResultRequested(string path)

    parent: Overlay.overlay
    x: 24
    y: 24
    width: (parent ? parent.width : 1000) - 48
    height: (parent ? parent.height : 700) - 48
    modal: true
    dim: true
    padding: 0
    closePolicy: Popup.NoAutoClose
    Overlay.modal: Rectangle { color: "#B3000000" }

    // ---- entry points ---------------------------------------------------------------------------------------------------------
    function begin(newMode) {
        mode = newMode
        result = ""; resultNote = ""; error = ""; askDiscard = false; askReplace = false
        clearSel()
        if (Doc.isOpen)
            Organizer.loadCurrent()
        else
            Organizer.clear()
        open()
        if (newMode === "merge" || !Doc.isOpen)
            addDialog.open()
    }
    function finish() {
        Organizer.clear()
        askDiscard = false
        close()
    }
    function tryClose() {
        if (Organizer.busy)
            return
        if (Organizer.modified && result === "") {
            askDiscard = true
            return
        }
        finish()
    }

    // ---- selection --------------------------------------------------------------------------------------------------------------
    function selRows() {
        const out = []
        for (const k in sel)
            out.push(parseInt(k))
        out.sort((a, b) => a - b)
        return out
    }
    function clearSel() { sel = ({}); anchorRow = -1 }
    function select(row, ctrl, shift) {
        const next = ctrl ? Object.assign({}, sel) : ({})
        if (shift && anchorRow >= 0) {
            for (let i = Math.min(anchorRow, row); i <= Math.max(anchorRow, row); ++i)
                next[i] = true
        } else if (ctrl && next[row]) {
            delete next[row]
            anchorRow = row
        } else {
            next[row] = true
            anchorRow = row
        }
        sel = next
    }
    function selectAll() {
        const next = ({})
        for (let i = 0; i < Organizer.count; ++i)
            next[i] = true
        sel = next
    }
    // the rows an action works on: the selection (nothing selected = no action)
    function rowsOrNone() { return selRows() }

    // ---- adding files ---------------------------------------------------------------------------------------------------------
    function startAdding(urls) {
        const rows = selRows()
        addAt = rows.length > 0 ? rows[rows.length - 1] + 1 : -1
        addQueue = urls.map(u => u.toString())
        addNext("")
    }
    function addNext(password) {
        error = ""
        while (addQueue.length > 0) {
            const url = addQueue[0]
            const before = Organizer.count
            const code = Organizer.addFile(url, password, addAt)
            if (code === "password") {
                pwDialog.path = Doc.localPath(url)
                pwDialog.wrong = password !== ""
                if (!pwDialog.opened)
                    pwDialog.open()
                return
            }
            password = ""
            if (code !== "")
                error = Tr.s("addFailed") + " " + Doc.localPath(url).split(/[\\/]/).pop() + ": " + Tr.error(code)
            else if (addAt >= 0)
                addAt += Organizer.count - before
            addQueue = addQueue.slice(1)
        }
        pwDialog.close()
    }

    // ---- saving -----------------------------------------------------------------------------------------------------------------
    function saveTo(path, rows, overwrite) {
        error = ""
        const code = Organizer.save(path, rows, overwrite)
        if (code === "exists") {
            pendingPath = path
            pendingRows = rows
            askReplace = true
        } else if (code === "") {
            result = ""
        } else if (code === "empty") {
            error = Tr.s("nothingToSave")
        } else if (code !== "") {
            error = Tr.s("busyNow")
        }
    }
    function saveErrorText(code, file) {
        const names = { password: "errPasswordFile", format: "errFormatFile", file: "errFileFile", range: "errRange", exists: "errExists", write: "errWrite" }
        return Tr.s(names[code] !== undefined ? names[code] : "errSaveUnknown") + (file !== "" ? " (" + file.split(/[\\/]/).pop() + ")" : "")
    }

    Connections {
        target: Organizer
        function onCountChanged() { dlg.clearSel() }
        function onSaveFinished(ok, err, errFile, warnings, pages, bytes, path) {
            if (ok) {
                dlg.result = path
                dlg.resultNote = Tr.f2("savedNote", pages, Math.max(1, Math.round(bytes / 1024)))
                const w = []
                for (const code of warnings)
                    w.push(Tr.warning(code))
                if (w.length > 0)
                    dlg.resultNote += "  " + w.join("  ")
                dlg.error = ""
            } else if (err === "cancelled") {
                dlg.error = Tr.s("saveCancelled")
            } else {
                dlg.error = dlg.saveErrorText(err, errFile)
            }
        }
    }

    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 0

        // title
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.bottomMargin: 8
            spacing: 12
            AppText {
                Layout.fillWidth: true
                text: dlg.mode === "merge" ? Tr.s("mergeTitle") : Tr.s("organizeTitle")
                font.pixelSize: Theme.fontXl
                font.weight: Font.DemiBold
            }
            AppText { text: Tr.f("pagesCount", Organizer.count) + (dlg.selCount > 0 ? "  ·  " + Tr.f("selectedCount", dlg.selCount) : ""); color: Theme.textSecondary; objectName: "orgCount" }
            PButton { objectName: "orgClose"; compact: true; variant: "ghost"; text: "✕"; tip: Tr.s("close"); enabled: !Organizer.busy; onClicked: dlg.tryClose() }
        }

        // tools
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 6
            PButton { objectName: "orgAdd"; compact: true; text: Tr.s("addFiles"); onClicked: addDialog.open() }
            PButton { objectName: "orgBlank"; compact: true; text: Tr.s("addBlank"); onClicked: { const r = dlg.selRows(); Organizer.addBlank(r.length > 0 ? r[r.length - 1] + 1 : -1) } }
            Rectangle { width: 1; height: 28; color: Theme.border }
            PButton { objectName: "orgRotateLeft"; compact: true; text: "↺ 90°"; tip: Tr.s("pgRotateLeft"); enabled: dlg.selCount > 0; onClicked: Organizer.rotate(dlg.selRows(), -1) }
            PButton { objectName: "orgRotateRight"; compact: true; text: "↻ 90°"; tip: Tr.s("pgRotateRight"); enabled: dlg.selCount > 0; onClicked: Organizer.rotate(dlg.selRows(), 1) }
            PButton { objectName: "orgDuplicate"; compact: true; text: Tr.s("duplicate"); enabled: dlg.selCount > 0; onClicked: Organizer.duplicate(dlg.selRows()) }
            PButton { objectName: "orgDelete"; compact: true; text: Tr.s("deletePages"); enabled: dlg.selCount > 0; onClicked: Organizer.removeRows(dlg.selRows()) }
            PButton { objectName: "orgCrop"; compact: true; text: Tr.s("crop"); enabled: dlg.selCount > 0; onClicked: cropDialog.open() }
            Rectangle { width: 1; height: 28; color: Theme.border }
            PButton { objectName: "orgSelectAll"; compact: true; variant: "ghost"; text: Tr.s("selectAll"); enabled: Organizer.count > 0; onClicked: dlg.selectAll() }
            PButton { objectName: "orgUndo"; compact: true; variant: "ghost"; text: Tr.s("undo"); enabled: Organizer.canUndo && !Organizer.busy; onClicked: Organizer.undo() }
            PButton { objectName: "orgRedo"; compact: true; variant: "ghost"; text: Tr.s("redo"); enabled: Organizer.canRedo && !Organizer.busy; onClicked: Organizer.redo() }
            PButton { objectName: "orgReset"; compact: true; variant: "ghost"; text: Tr.s("resetList"); enabled: Organizer.modified && !Organizer.busy; onClicked: Organizer.reset() }
        }
        AppText {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 6
            text: Tr.s("organizeHint")
            color: Theme.textDisabled
            font.pixelSize: Theme.fontSm
            wrapMode: Text.Wrap
        }

        // the pages
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            Layout.topMargin: 8
            radius: 8
            color: Theme.viewerBackground
            clip: true

            AppText {
                anchors.centerIn: parent
                visible: Organizer.count === 0
                text: Tr.s("noPagesYet")
                color: Theme.textSecondary
            }

            GridView {
                id: grid
                objectName: "organizerGrid"
                anchors.fill: parent
                anchors.margins: 8
                cellWidth: 170
                cellHeight: 240
                model: Organizer
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                // the grid has as many columns as fit
                function rowAt(sceneX, sceneY) {
                    const p = grid.mapFromItem(null, sceneX, sceneY)
                    const i = grid.indexAt(p.x + grid.contentX, p.y + grid.contentY)
                    if (i >= 0)
                        return i
                    return p.y + grid.contentY > grid.contentHeight ? Organizer.count - 1 : -1
                }

                delegate: Item {
                    id: cell
                    required property int index
                    required property int sourceId
                    required property int page
                    required property int rotation
                    required property real widthPt
                    required property real heightPt
                    required property string label
                    required property bool blank
                    required property var crop
                    required property string file
                    objectName: "orgPage_" + index
                    width: grid.cellWidth
                    height: grid.cellHeight
                    readonly property bool selected: dlg.sel[index] === true
                    readonly property real pxPerPt: Math.min(dlg.thumbW / Math.max(1, widthPt), dlg.thumbH / Math.max(1, heightPt))
                    readonly property real imgW: widthPt * pxPerPt
                    readonly property real imgH: heightPt * pxPerPt

                    Rectangle {
                        id: card
                        width: parent.width - 12
                        height: parent.height - 12
                        x: 6
                        y: 6
                        radius: 8
                        color: cell.selected ? Theme.accentSubtle : (hover.hovered ? Theme.surfaceHover : Theme.surface)
                        border.width: cell.selected ? 2 : 1
                        border.color: cell.selected ? Theme.accent : Theme.border
                        z: drag.active ? 10 : 0
                        opacity: drag.active ? 0.8 : 1
                        Behavior on x { enabled: !drag.active; NumberAnimation { duration: 120 } }
                        Behavior on y { enabled: !drag.active; NumberAnimation { duration: 120 } }

                        HoverHandler { id: hover }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            acceptedModifiers: Qt.NoModifier
                            onTapped: dlg.select(cell.index, false, false)
                            onDoubleTapped: Organizer.rotate([cell.index], 1)
                        }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            acceptedModifiers: Qt.ControlModifier
                            onTapped: dlg.select(cell.index, true, false)
                        }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            acceptedModifiers: Qt.ShiftModifier
                            onTapped: dlg.select(cell.index, false, true)
                        }
                        DragHandler {
                            id: drag
                            target: card
                            acceptedButtons: Qt.LeftButton
                            xAxis.enabled: true
                            yAxis.enabled: true
                            onActiveChanged: {
                                if (active) {
                                    if (!dlg.sel[cell.index])
                                        dlg.select(cell.index, false, false)
                                } else {
                                    const to = dlg.dropRow
                                    dlg.dropRow = -1
                                    card.x = 6
                                    card.y = 6
                                    if (to >= 0 && to !== cell.index) {
                                        const rows = dlg.selRows()
                                        if (rows.length <= 1) {
                                            Organizer.beginGesture()
                                            Organizer.moveOne(cell.index, to)
                                            Organizer.endGesture()
                                        } else {
                                            let before = 0
                                            for (const r of rows)
                                                if (r < to)
                                                    before++
                                            Organizer.moveRows(rows, Math.max(0, to - before + (to > cell.index ? 1 : 0)))
                                        }
                                    }
                                }
                            }
                            onCentroidChanged: {
                                if (active)
                                    dlg.dropRow = grid.rowAt(centroid.scenePosition.x, centroid.scenePosition.y)
                            }
                        }

                        // the page
                        Item {
                            x: (parent.width - cell.imgW) / 2
                            y: 8 + (dlg.thumbH - cell.imgH) / 2
                            width: cell.imgW
                            height: cell.imgH
                            Rectangle { anchors.fill: parent; color: "white"; visible: cell.blank || thumb.status !== Image.Ready }
                            Image {
                                id: thumb
                                anchors.fill: parent
                                visible: !cell.blank
                                asynchronous: true
                                cache: true
                                fillMode: Image.Stretch
                                source: cell.blank ? "" : "image://pdf/" + cell.sourceId + "/" + cell.page + "/" + Math.round(cell.pxPerPt * 1000) + "/" + cell.rotation + "/0"
                            }
                            // the cropped away margins
                            Rectangle { x: 0; y: 0; width: parent.width; height: cell.crop.top * cell.pxPerPt; color: "#B3101114" }
                            Rectangle { x: 0; y: parent.height - cell.crop.bottom * cell.pxPerPt; width: parent.width; height: cell.crop.bottom * cell.pxPerPt; color: "#B3101114" }
                            Rectangle { x: 0; y: 0; width: cell.crop.left * cell.pxPerPt; height: parent.height; color: "#B3101114" }
                            Rectangle { x: parent.width - cell.crop.right * cell.pxPerPt; y: 0; width: cell.crop.right * cell.pxPerPt; height: parent.height; color: "#B3101114" }
                            Rectangle { anchors.fill: parent; color: "transparent"; border.color: Theme.border }
                        }
                        AppText {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 6
                            horizontalAlignment: Text.AlignHCenter
                            text: (cell.index + 1) + (cell.blank ? "  ·  " + Tr.s("blankPage") : "  ·  " + cell.label)
                            elide: Text.ElideMiddle
                            font.pixelSize: Theme.fontSm
                            color: cell.selected ? Theme.text : Theme.textSecondary
                        }
                        // where a dragged page lands
                        Rectangle {
                            visible: dlg.dropRow === cell.index && !drag.active
                            x: -6
                            y: 0
                            width: 4
                            height: parent.height
                            radius: 2
                            color: Theme.accent
                        }
                    }
                }
            }
        }

        // messages
        AppText {
            objectName: "orgError"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: dlg.error !== ""
            text: dlg.error
            color: Theme.errorSoft
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: dlg.result !== ""
            spacing: 10
            AppText { objectName: "orgResult"; Layout.fillWidth: true; text: Tr.s("saved") + " " + dlg.result + "  " + dlg.resultNote; color: Theme.success; wrapMode: Text.Wrap }
            PButton { objectName: "orgOpenResult"; compact: true; text: Tr.s("openResult"); onClicked: { const p = dlg.result; dlg.finish(); dlg.openResultRequested(p) } }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: dlg.askReplace
            spacing: 10
            AppText { Layout.fillWidth: true; text: Tr.f("replaceAsk", dlg.pendingPath.split(/[\\/]/).pop()); color: Theme.warning; wrapMode: Text.Wrap }
            PButton { objectName: "orgReplaceNo"; compact: true; variant: "ghost"; text: Tr.s("cancel"); onClicked: dlg.askReplace = false }
            PButton { objectName: "orgReplaceYes"; compact: true; text: Tr.s("replace"); onClicked: { dlg.askReplace = false; dlg.saveTo(dlg.pendingPath, dlg.pendingRows, true) } }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: dlg.askDiscard
            spacing: 10
            AppText { Layout.fillWidth: true; text: Tr.s("discardAsk"); color: Theme.warning; wrapMode: Text.Wrap }
            PButton { objectName: "orgDiscardNo"; compact: true; variant: "ghost"; text: Tr.s("keepEditing"); onClicked: dlg.askDiscard = false }
            PButton { objectName: "orgDiscardYes"; compact: true; text: Tr.s("discard"); onClicked: dlg.finish() }
        }
        ProgressBar {
            objectName: "orgProgress"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 6
            visible: Organizer.busy
            from: 0
            to: 1
            value: Organizer.progress
        }

        // bottom bar
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            spacing: 8
            Item { Layout.fillWidth: true }
            PButton { objectName: "orgCancelSave"; visible: Organizer.busy; variant: "ghost"; text: Tr.s("cancel"); onClicked: Organizer.cancelSave() }
            PButton { objectName: "orgCloseBottom"; visible: !Organizer.busy; variant: "ghost"; text: Tr.s("close"); onClicked: dlg.tryClose() }
            PButton { objectName: "orgSaveSelected"; text: Tr.s("saveSelected"); enabled: dlg.selCount > 0 && !Organizer.busy; onClicked: { dlg.pendingRows = dlg.selRows(); saveDialog.open() } }
            PButton { objectName: "orgSave"; variant: "primary"; text: Organizer.busy ? Tr.s("saving") : Tr.s("saveAs"); enabled: Organizer.count > 0 && !Organizer.busy; onClicked: { dlg.pendingRows = []; saveDialog.open() } }
        }
    }

    Shortcut { sequences: [StandardKey.Undo]; enabled: dlg.opened && Organizer.canUndo; onActivated: Organizer.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Y"]; enabled: dlg.opened && Organizer.canRedo; onActivated: Organizer.redo() }
    Shortcut { sequences: ["Delete"]; enabled: dlg.opened && dlg.selCount > 0 && !cropDialog.opened; onActivated: Organizer.removeRows(dlg.selRows()) }
    Shortcut { sequences: [StandardKey.SelectAll]; enabled: dlg.opened && !cropDialog.opened; onActivated: dlg.selectAll() }
    Shortcut { sequences: ["Escape"]; enabled: dlg.opened && !cropDialog.opened && !pwDialog.opened && !addDialog.visible && !saveDialog.visible; onActivated: dlg.tryClose() }

    FileDialog {
        id: addDialog
        title: Tr.s("addFiles")
        nameFilters: [Tr.s("pdfFiles")]
        fileMode: FileDialog.OpenFiles
        onAccepted: dlg.startAdding(selectedFiles)
    }
    FileDialog {
        id: saveDialog
        title: Tr.s("saveAs")
        nameFilters: [Tr.s("pdfFiles")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        currentFolder: Doc.isOpen ? "file:///" + Doc.filePath.replace(/\\/g, "/").replace(/\/[^\/]*$/, "") : ""
        onAccepted: dlg.saveTo(Doc.localPath(selectedFile.toString()), dlg.pendingRows, false)
    }
    PasswordDialog {
        id: pwDialog
        onSubmitted: (path, password) => dlg.addNext(password)
        onClosed: {
            // cancelled: skip this file and go on with the others
            if (dlg.addQueue.length > 0) {
                dlg.addQueue = dlg.addQueue.slice(1)
                dlg.addNext("")
            }
        }
    }

    // crop: the margins to cut away, in millimetres, for the selected pages
    Popup {
        id: cropDialog
        objectName: "cropDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        dim: true
        width: Math.min(420, (parent ? parent.width : 420) - 48)
        padding: 24
        closePolicy: Popup.CloseOnEscape
        Overlay.modal: Rectangle { color: "#B3000000" }
        background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }
        function apply() {
            const k = 72 / 25.4
            Organizer.crop(dlg.selRows(), cl.value * k, ct.value * k, cr.value * k, cb.value * k)
            close()
        }
        contentItem: ColumnLayout {
            spacing: 12
            AppText { Layout.fillWidth: true; text: Tr.s("cropTitle"); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold }
            AppText { Layout.fillWidth: true; text: Tr.s("cropText"); color: Theme.textSecondary; wrapMode: Text.Wrap }
            GridLayout {
                columns: 2
                columnSpacing: 16
                rowSpacing: 8
                AppText { text: Tr.s("marginTop"); color: Theme.textSecondary }
                SpinBox { id: ct; objectName: "cropTop"; from: 0; to: 400; value: 0; editable: true }
                AppText { text: Tr.s("marginBottom"); color: Theme.textSecondary }
                SpinBox { id: cb; objectName: "cropBottom"; from: 0; to: 400; value: 0; editable: true }
                AppText { text: Tr.s("marginLeft"); color: Theme.textSecondary }
                SpinBox { id: cl; objectName: "cropLeft"; from: 0; to: 400; value: 0; editable: true }
                AppText { text: Tr.s("marginRight"); color: Theme.textSecondary }
                SpinBox { id: cr; objectName: "cropRight"; from: 0; to: 400; value: 0; editable: true }
            }
            AppText { Layout.fillWidth: true; text: Tr.s("cropNote"); color: Theme.textDisabled; font.pixelSize: Theme.fontSm; wrapMode: Text.Wrap }
            RowLayout {
                Layout.fillWidth: true
                PButton { objectName: "cropClear"; variant: "ghost"; text: Tr.s("removeCrop"); onClicked: { ct.value = 0; cb.value = 0; cl.value = 0; cr.value = 0; cropDialog.apply() } }
                Item { Layout.fillWidth: true }
                PButton { objectName: "cropCancel"; variant: "ghost"; text: Tr.s("cancel"); onClicked: cropDialog.close() }
                PButton { objectName: "cropApply"; variant: "primary"; text: Tr.s("apply"); onClicked: cropDialog.apply() }
            }
        }
    }
}
