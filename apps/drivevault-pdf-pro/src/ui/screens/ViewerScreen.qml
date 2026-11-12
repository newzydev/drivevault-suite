import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// The reader: tabs, tool bar, search bar, side panel (thumbnails / bookmarks / comments) and the page view.
// Pages are shown one per row (continuous) or two per row (spreads); the pointer selects text, clicks follow links.
Item {
    id: viewer

    required property Item toolbarHost
    readonly property real toolbarHeight: readerToolbar.implicitHeight
    signal openRequested()
    signal organizeRequested()
    signal splitRequested()
    signal compressRequested()
    signal openPathRequested(string path)
    signal formRequested()
    signal imageExportRequested(int page)
    signal securityRequested(bool unlock)
    signal maintenanceRequested(bool repair)
    signal batchRequested()
    signal ocrRequested(int page)
    property bool replacingText: false
    property string replaceHint: ""

    function beginRedaction() {
        replacingText = false
        if (Doc.rotation !== 0)
            Doc.rotateView(-Doc.rotation)
        Editor.active = false
        closeSearch()
        Doc.clearSelection()
        redactions.begin()
    }

    function runTool(tool) {
        if (redactions.visible && tool !== "redactTitle")
            return
        switch (tool) {
        case "toolOrganize": organizeRequested(); break
        case "toolSplit": splitRequested(); break
        case "toolCompress": compressRequested(); break
        case "toolForm": formRequested(); break
        case "exportImages": imageExportRequested(currentPage); break
        case "thaiOcr": ocrRequested(currentPage); break
        case "protectPdf": securityRequested(false); break
        case "unlockPdf": securityRequested(true); break
        case "maintenance": maintenanceRequested(false); break
        case "redactTitle":
            if (!redactions.visible)
                beginRedaction()
            break
        case "addContent":
        case "addComments":
            replacingText = false
            if (Doc.rotation !== 0)
                Doc.rotateView(-Doc.rotation)
            Editor.active = true
            Editor.tool = tool === "addComments" ? "note" : "select"
            break
        case "editExisting":
            Editor.active = false
            if (Doc.rotation !== 0)
                Doc.rotateView(-Doc.rotation)
            closeSearch()
            Doc.clearSelection()
            replaceHint = ""
            replacingText = true
            break
        }
    }

    // ---- view state ---------------------------------------------------------------------------------------------------
    property real zoom: 1.0                       // 1.0 = 100 % = 96 dpi on screen
    property string zoomMode: "custom"            // "fitWidth" | "fitPage" | "custom"
    property string viewMode: "continuous"        // "continuous" | "twoPage"
    property int currentPage: 0
    property bool showSidebar: false
    property bool searchOpen: false
    property real maxW: 612                       // largest page as shown, in points
    property real maxH: 792
    readonly property int perSpread: viewMode === "twoPage" ? 2 : 1
    readonly property int spreadCount: Math.ceil(Doc.pageCount / perSpread)
    readonly property real pxPerPt: zoom * 96 / 72
    property real renderScale: pxPerPt            // the scale pages are rendered at: follows pxPerPt a moment after zooming stops
    readonly property real dpr: Screen.devicePixelRatio
    readonly property var zoomSteps: [0.1, 0.25, 0.33, 0.5, 0.67, 0.75, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0, 3.0, 4.0, 6.0, 8.0]
    readonly property bool editingText: {
        const a = Window.window ? Window.window.activeFocusItem : null
        return !!a && a.hasOwnProperty("selectedText")
    }
    property int readerCursor: Qt.ArrowCursor
    property var redactStart: null

    function updateRedactDrag(x, y) {
        if (!redactStart)
            return
        const page = redactStart.page
        const origin = pageToView(page, 0, 0)
        const point = pointer.mapFromItem(viewer, origin.x, origin.y)
        const px = Math.max(0, Math.min(Doc.pageWidthPt(page), (x - point.x) / pxPerPt))
        const py = Math.max(0, Math.min(Doc.pageHeightPt(page), (y - point.y) / pxPerPt))
        redactions.preview = { page: page, x: Math.min(redactStart.x, px), y: Math.min(redactStart.y, py),
            w: Math.abs(redactStart.x - px), h: Math.abs(redactStart.y - py) }
    }

    function measure() {
        let w = 0, h = 0
        const n = Doc.pageCount
        for (let i = 0; i < n; ++i) {
            w = Math.max(w, Doc.pageWidthPt(i))
            h = Math.max(h, Doc.pageHeightPt(i))
        }
        maxW = w > 0 ? w : 612
        maxH = h > 0 ? h : 792
    }
    function fitZoom() {
        const availW = Math.max(100, pagesView.width - 48)
        const spanPt = perSpread * maxW + (perSpread - 1) * 9
        const byWidth = availW / (spanPt * 96 / 72)
        if (zoomMode === "fitPage") {
            const availH = Math.max(100, pagesView.height - 36)
            return Math.min(byWidth, availH / (maxH * 96 / 72))
        }
        return byWidth
    }
    function applyFit() {
        if (zoomMode !== "custom")
            zoom = Math.max(0.1, Math.min(8.0, fitZoom()))
    }
    // Keeps the page row that is on top where it is while the zoom (or the layout) changes.
    function keepPosition(change) {
        const idx = pagesView.indexAt(pagesView.contentX + pagesView.width / 2, pagesView.contentY + 4)
        const item = idx >= 0 ? pagesView.itemAtIndex(idx) : null
        const frac = item && item.height > 0 ? (pagesView.contentY - item.y) / item.height : 0
        change()
        Qt.callLater(function () {
            const it = idx >= 0 ? pagesView.itemAtIndex(idx) : null
            if (it)
                pagesView.contentY = it.y + frac * it.height
        })
    }
    function setZoom(z) { keepPosition(function () { zoomMode = "custom"; zoom = Math.max(0.1, Math.min(8.0, z)) }) }
    function zoomIn() {
        for (let i = 0; i < zoomSteps.length; ++i)
            if (zoomSteps[i] > zoom * 1.001) { setZoom(zoomSteps[i]); return }
    }
    function zoomOut() {
        for (let i = zoomSteps.length - 1; i >= 0; --i)
            if (zoomSteps[i] < zoom / 1.001) { setZoom(zoomSteps[i]); return }
    }
    function fit(mode) { keepPosition(function () { zoomMode = mode; applyFit() }) }
    function setViewMode(mode) {
        const page = currentPage
        keepPosition(function () { viewMode = mode; applyFit() })
        Qt.callLater(function () { goTo(page) })
    }
    // Scrolls so that `page` (0-based) is on top; `yPt` (points from the top of the page) is made visible as well.
    function goTo(page, yPt) {
        const n = Doc.pageCount
        if (n <= 0)
            return
        page = Math.max(0, Math.min(n - 1, page))
        pagesView.positionViewAtIndex(Math.floor(page / perSpread), ListView.Beginning)
        if (yPt !== undefined && yPt > 0)
            pagesView.contentY += Math.max(0, yPt * pxPerPt - pagesView.height * 0.3)
        currentPage = page
    }
    function updateCurrent() {
        const x = pagesView.contentX + pagesView.width / 2
        let idx = -1
        for (const dy of [0.35, 0.3, 0.4, 0.2, 0.5]) {
            idx = pagesView.indexAt(x, pagesView.contentY + pagesView.height * dy)
            if (idx >= 0)
                break
        }
        if (idx >= 0 && Math.floor(currentPage / perSpread) !== idx)
            currentPage = idx * perSpread
    }

    // the page, and the place on it (in points), under a point of the viewport; null when the point is not on a page
    function hit(vx, vy) {
        const cx = pagesView.contentX + vx
        for (const dy of [0, -8, 8, -16, 16]) {
            const cy = pagesView.contentY + vy + dy
            const idx = pagesView.indexAt(cx, cy)
            if (idx < 0)
                continue
            const item = pagesView.itemAtIndex(idx)
            const h = item ? item.hitPage(cx - item.x, cy - item.y) : null
            if (h)
                return h
        }
        return null
    }
    function followLink(link) {
        if (link.kind === "page")
            goTo(link.page)
        else if (link.kind === "uri") {
            linkDialog.uri = link.uri
            linkDialog.open()
        }
    }
    function closeTabAt(index) {
        const tab = Doc.tabs[index]
        if (tab && Editor.modifiedFor(tab.id)) {
            closeAsk.tabIndex = index
            closeAsk.open()
            return
        }
        reallyCloseTab(index)
    }
    function reallyCloseTab(index) {
        flushSave()                                // the view of the file is saved while its tab still exists
        Doc.closeTab(index)
    }

    // ---- editing: the pointer puts things on the pages ----------------------------------------------------------------------
    readonly property bool editOn: Editor.active && Doc.rotation === 0
    property string editMode: ""                  // "" | "move" | "resize" | "draw"
    property int editHandle: -1
    property int editPage: 0
    property real editLastX: 0
    property real editLastY: 0

    // where a place on a page is in the pointer's coordinates (reads the scroll position so that a binding follows it)
    function pageToView(page, x, y) {
        const dummy = pagesView.contentX + pagesView.contentY + viewer.pxPerPt
        const sp = pagesView.itemAtIndex(Math.floor(page / perSpread))
        const pv = sp ? sp.pageView(page) : null
        if (!pv)
            return Qt.point(-10000 + dummy * 0, -10000)
        return pv.mapToItem(viewer, x * viewer.pxPerPt, y * viewer.pxPerPt)
    }
    function startEditing(id) {
        Editor.beginEdit(id)
        if (Editor.editingId !== id)
            return
        Editor.beginGesture()
        editField.text = Editor.itemInfo(id).text
        editField.forceActiveFocus()
        editField.selectAll()
    }
    function finishEditing() {
        if (Editor.editingId === 0)
            return
        Editor.endGesture()
        Editor.endEdit()
        pagesView.forceActiveFocus()
    }
    function editPress(vx, vy) {
        finishEditing()
        const h = hit(vx, vy)
        if (Editor.polygonOpen) {
            if (h && h.page === editPage)
                Editor.addPolygonPoint(h.x, h.y)
            return
        }
        if (!h) {
            if (Editor.tool === "select")
                Editor.select(0)
            return
        }
        editPage = h.page
        editLastX = h.x
        editLastY = h.y
        const tool = Editor.tool
        if (tool === "select") {
            const hd = Editor.handleAt(h.page, h.x, h.y, 7 / pxPerPt)
            if (hd >= 0) {
                editMode = "resize"
                editHandle = hd
                Editor.beginGesture()
                return
            }
            const id = Editor.hitTest(h.page, h.x, h.y)
            Editor.select(id)
            if (id > 0) {
                editMode = "move"
                Editor.beginGesture()
            }
        } else if (tool === "image") {
            if (Editor.pendingImage !== "") {
                Editor.addImage(h.page, Editor.pendingImage, h.x, h.y)
                Editor.pendingImage = ""
                Editor.tool = "select"
            }
        } else if (tool === "polygon") {
            Editor.beginPolygon(h.page, h.x, h.y)
        } else if (tool === "stamp") {
            Editor.addStamp(h.page, h.x, h.y, editBar.stampText)
            Editor.tool = "select"
        } else if (Editor.beginDraft(h.page, tool, h.x, h.y) > 0) {
            editMode = "draw"
        }
    }
    function editMove(vx, vy) {
        if (Editor.polygonOpen) {
            const p = hit(vx, vy)
            if (p && p.page === editPage)
                Editor.movePolygon(p.x, p.y)
            return
        }
        if (editMode === "")
            return
        const h = hit(vx, vy)
        if (!h || h.page !== editPage)
            return
        if (editMode === "move")
            Editor.moveSelected(h.x - editLastX, h.y - editLastY)
        else if (editMode === "resize")
            Editor.dragHandle(editHandle, h.x, h.y)
        else if (editMode === "draw")
            Editor.updateDraft(h.x, h.y)
        editLastX = h.x
        editLastY = h.y
    }
    function editRelease() {
        if (editMode === "move" || editMode === "resize") {
            Editor.endGesture()
        } else if (editMode === "draw") {
            const kept = Editor.endDraft()
            if (kept && (Editor.tool === "text" || Editor.tool === "note")) {
                Editor.tool = "select"
                startEditing(Editor.selectedId)
            }
        }
        editMode = ""
    }
    function openSearch() {
        searchOpen = true
        Qt.callLater(function () { searchBar.focusField() })
    }
    function closeSearch() {
        searchOpen = false
        Doc.clearSearch()
        pagesView.forceActiveFocus()
    }

    // ---- remembering the view of each file ---------------------------------------------------------------------------------
    property int pendingId: 0
    property var pendingState: ({})
    function scheduleSave() {
        pendingId = Doc.docId
        pendingState = { page: currentPage, zoom: zoom, zoomMode: zoomMode, viewMode: viewMode }
        saveTimer.restart()
    }
    function flushSave() {
        if (!saveTimer.running)
            return
        saveTimer.stop()
        Doc.saveViewStateFor(pendingId, pendingState)
    }
    Timer { id: saveTimer; interval: 700; onTriggered: Doc.saveViewStateFor(viewer.pendingId, viewer.pendingState) }
    onZoomChanged: scheduleSave()
    onZoomModeChanged: scheduleSave()
    onViewModeChanged: scheduleSave()
    onCurrentPageChanged: scheduleSave()

    function restoreView() {
        const st = Doc.viewState()
        viewMode = st.viewMode === "twoPage" ? "twoPage" : "continuous"
        zoomMode = st.zoomMode === "fitWidth" || st.zoomMode === "fitPage" ? st.zoomMode : "custom"
        measure()
        if (zoomMode === "custom")
            zoom = st.zoomMode === "custom" && st.zoom > 0 ? Math.max(0.1, Math.min(8.0, st.zoom)) : 1.0
        else
            applyFit()
        renderScale = pxPerPt
        const page = st.page > 0 ? Math.min(st.page, Doc.pageCount - 1) : 0
        currentPage = page
        pagesView.contentY = -pagesView.headerItem.height
        const restoringDocId = Doc.docId
        Qt.callLater(function () {
            if (viewer && Doc.docId === restoringDocId)
                viewer.goTo(page)
        })
    }

    Connections {
        target: Doc
        function onDocumentChanged() {
            viewer.replacingText = false
            viewer.flushSave()
            if (Doc.isOpen)
                viewer.restoreView()
        }
        function onRotationChanged() {
            viewer.keepPosition(function () { viewer.measure(); viewer.applyFit() })
        }
        function onSearchTarget(page, y) { viewer.goTo(page, y) }
    }
    Component.onCompleted: { restoreView(); offerRecovery() }
    onPxPerPtChanged: renderTimer.restart()
    Timer { id: renderTimer; interval: 140; onTriggered: viewer.renderScale = viewer.pxPerPt }

    Rectangle {
        id: readerToolbar
        objectName: "readerToolbar"
        parent: viewer.toolbarHost
        visible: viewer.visible
        anchors.fill: parent
        implicitHeight: toolFlow.implicitHeight + 14
        color: Theme.backgroundAlt
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
        Flow {
            id: toolFlow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 7
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 12

            Row {
                spacing: 4
                PButton { objectName: "toggleSidebar"; compact: true; glyph: "sidebar"; tip: Tr.s("thumbnails"); variant: viewer.showSidebar ? "secondary" : "ghost"; onClicked: viewer.showSidebar = !viewer.showSidebar }
                PButton { objectName: "toolbarOpen"; compact: true; glyph: "folder"; text: Tr.s("openFile"); tip: text; variant: "ghost"; onClicked: viewer.openRequested() }
            }

            Row {
                spacing: 4
                PButton { objectName: "prevPage"; compact: true; text: "‹"; tip: Tr.s("prevPage"); enabled: viewer.currentPage > 0; onClicked: viewer.goTo(viewer.currentPage - 1) }
                TextField {
                    id: pageField
                    objectName: "pageField"
                    implicitWidth: 52
                    implicitHeight: 32
                    horizontalAlignment: TextInput.AlignHCenter
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.text
                    font.pixelSize: Theme.fontMd
                    selectByMouse: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 1; top: Math.max(1, Doc.pageCount) }
                    text: activeFocus ? text : String(viewer.currentPage + 1)
                    onAccepted: { viewer.goTo(parseInt(text) - 1); pagesView.forceActiveFocus() }
                    onActiveFocusChanged: if (activeFocus) selectAll()
                    background: Rectangle {
                        radius: 8
                        color: Theme.surface
                        border.width: pageField.activeFocus ? 2 : 1
                        border.color: pageField.activeFocus ? Theme.accent : Theme.border
                    }
                }
                AppText { height: 32; verticalAlignment: Text.AlignVCenter; text: Tr.f("pageOf", Doc.pageCount); color: Theme.textSecondary }
                PButton { objectName: "nextPage"; compact: true; text: "›"; tip: Tr.s("nextPage"); enabled: viewer.currentPage < Doc.pageCount - 1; onClicked: viewer.goTo(viewer.currentPage + 1) }
            }

            Row {
                spacing: 4
                PButton { objectName: "zoomOut"; compact: true; text: "−"; tip: Tr.s("zoomOut"); onClicked: viewer.zoomOut() }
                PButton {
                    objectName: "zoomLabel"
                    compact: true
                    variant: viewer.zoomMode === "custom" && viewer.zoom === 1.0 ? "secondary" : "ghost"
                    implicitWidth: 58
                    text: Math.round(viewer.zoom * 100) + "%"
                    tip: Tr.s("actualSize")
                    onClicked: viewer.setZoom(1.0)
                }
                PButton { objectName: "zoomIn"; compact: true; text: "+"; tip: Tr.s("zoomIn"); onClicked: viewer.zoomIn() }
                PButton { objectName: "fitWidth"; compact: true; text: Tr.s("fitWidth"); variant: viewer.zoomMode === "fitWidth" ? "secondary" : "ghost"; onClicked: viewer.fit("fitWidth") }
                PButton { objectName: "fitPage"; compact: true; text: Tr.s("fitPage"); variant: viewer.zoomMode === "fitPage" ? "secondary" : "ghost"; onClicked: viewer.fit("fitPage") }
            }

            Row {
                spacing: 4
                PButton {
                    objectName: "viewMode"
                    compact: true
                    glyph: viewer.viewMode === "twoPage" ? "pages2" : "page"
                    tip: viewer.viewMode === "twoPage" ? Tr.s("viewContinuous") : Tr.s("viewTwoPage")
                    onClicked: viewer.setViewMode(viewer.viewMode === "twoPage" ? "continuous" : "twoPage")
                }
                PButton { objectName: "rotate"; compact: true; glyph: "rotate"; tip: Tr.s("rotateRight"); enabled: !redactions.visible; onClicked: Doc.rotateView(1) }
                PButton { objectName: "darkPages"; compact: true; glyph: "dark"; tip: Tr.s("darkPages"); variant: Doc.darkPages ? "secondary" : "ghost"; onClicked: Doc.darkPages = !Doc.darkPages }
            }

            Row {
                spacing: 2
                PButton { objectName: "findButton"; compact: true; text: Tr.s("find"); variant: viewer.searchOpen ? "secondary" : "ghost"; onClicked: viewer.searchOpen ? viewer.closeSearch() : viewer.openSearch() }
                PButton { objectName: "printButton"; compact: true; text: Tr.s("print"); variant: "ghost"; onClicked: printDialog.open() }
                PButton { objectName: "organizeButton"; compact: true; text: Tr.s("toolOrganizeShort"); variant: "ghost"; tip: Tr.s("toolOrganize"); onClicked: viewer.organizeRequested() }
                PButton { objectName: "splitButton"; compact: true; text: Tr.s("toolSplitShort"); variant: "ghost"; tip: Tr.s("toolSplit"); onClicked: viewer.splitRequested() }
                PButton { objectName: "compressButton"; compact: true; text: Tr.s("toolCompressShort"); variant: "ghost"; tip: Tr.s("toolCompress"); onClicked: viewer.compressRequested() }
                PButton { objectName: "editButton"; compact: true; text: Tr.s("toolEdit"); variant: Editor.active ? "secondary" : "ghost"; tip: Doc.rotation === 0 ? Tr.s("toolEditTip") : Tr.s("editNeedsNoRotation"); enabled: !redactions.visible && (Doc.rotation === 0 || Editor.active); onClicked: { viewer.replacingText = false; Editor.active = !Editor.active } }
                PButton { objectName: "formButton"; compact: true; text: Tr.s("toolFormShort"); variant: "ghost"; tip: Tr.s("toolForm"); onClicked: viewer.formRequested() }
                PButton {
                    id: outputButton
                    objectName: "outputButton"
                    compact: true
                    variant: "ghost"
                    text: Tr.s("outputTools")
                    enabled: !Doc.outputBusy && !redactions.visible
                    onClicked: outputMenu.popup(outputButton, 0, outputButton.height)
                    PMenu {
                        id: outputMenu
                        PMenuItem { objectName: "exportImagesAction"; text: Tr.s("exportImages"); onTriggered: viewer.imageExportRequested(viewer.currentPage) }
                        PMenuItem { objectName: "protectPdfAction"; text: Tr.s("protectPdf"); onTriggered: viewer.securityRequested(false) }
                        PMenuItem { objectName: "unlockPdfAction"; text: Tr.s("unlockPdf"); onTriggered: viewer.securityRequested(true) }
                        PMenuItem { objectName: "maintenanceAction"; text: Tr.s("maintenance"); onTriggered: viewer.maintenanceRequested(false) }
                        PMenuItem { text: Tr.s("batchPdf"); onTriggered: viewer.batchRequested() }
                        PMenuItem { text: Tr.s("repairPdf"); onTriggered: viewer.maintenanceRequested(true) }
                        PMenuItem {
                            objectName: "redactAction"
                            text: Tr.s("redactTitle")
                            onTriggered: viewer.beginRedaction()
                        }
                    }
                }
            }
        }
    }

    // ---- layout ---------------------------------------------------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        EditBar {
            id: editBar
            Layout.fillWidth: true
            visible: Editor.active
            onOpenResultRequested: path => viewer.openPathRequested(path)
        }
        RedactionBar {
            id: redactions
            Layout.fillWidth: true
            onGoTo: (page, y) => viewer.goTo(page, y)
            onOpenResult: path => viewer.openPathRequested(path)
            onVisibleChanged: if (!visible) viewer.redactStart = null
        }

        SearchBar {
            id: searchBar
            objectName: "searchBar"
            Layout.fillWidth: true
            visible: viewer.searchOpen
            startPage: viewer.currentPage
            onCloseRequested: viewer.closeSearch()
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 42
            visible: viewer.replacingText
            color: Theme.backgroundAlt
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                AppText {
                    Layout.fillWidth: true
                    text: viewer.replaceHint !== "" ? Tr.outputError(viewer.replaceHint) : Tr.s("editRunPick")
                    color: viewer.replaceHint !== "" ? Theme.errorSoft : Theme.text
                    elide: Text.ElideRight
                }
                PButton { compact: true; text: Tr.s("close"); onClicked: viewer.replacingText = false }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            SidePanel {
                id: sidePanel
                objectName: "sidePanel"
                visible: viewer.showSidebar
                Layout.preferredWidth: 208
                Layout.fillHeight: true
                currentPage: viewer.currentPage
                dpr: viewer.dpr
                onPageRequested: (page, y) => viewer.goTo(page, y)
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.viewerBackground
                clip: true

                ListView {
                    id: pagesView
                    onWidthChanged: viewer.applyFit()
                    onHeightChanged: viewer.applyFit()
                    objectName: "pages"
                    anchors.fill: parent
                    model: Doc.isOpen ? viewer.spreadCount : 0
                    spacing: 12
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.AutoFlickDirection
                    contentWidth: Math.max(width, viewer.perSpread * viewer.maxW * viewer.pxPerPt + (viewer.perSpread - 1) * 12 + 48)
                    cacheBuffer: 1500
                    focus: true
                    ScrollBar.vertical: ScrollBar {}
                    ScrollBar.horizontal: ScrollBar {}
                    header: Item { height: 12 }
                    footer: Item { height: 12 }
                    onContentYChanged: viewer.updateCurrent()
                    onContentXChanged: viewer.updateCurrent()

                    delegate: Item {
                        id: spread
                        required property int index
                        readonly property int firstPage: index * viewer.perSpread
                        readonly property int pageTotal: Math.max(0, Math.min(viewer.perSpread, Doc.pageCount - firstPage))
                        width: pagesView.contentWidth
                        height: row.height
                        function pageView(page) {
                            for (let i = 0; i < rep.count; ++i) {
                                const p = rep.itemAt(i)
                                if (p && p.pageIndex === page)
                                    return p
                            }
                            return null
                        }
                        function hitPage(lx, ly) {
                            for (let i = 0; i < rep.count; ++i) {
                                const p = rep.itemAt(i)
                                if (!p)
                                    continue
                                const left = row.x + p.x
                                if (lx >= left - 6 && lx <= left + p.width + 6)
                                    return { page: p.pageIndex, x: (lx - left) / viewer.pxPerPt, y: ly / viewer.pxPerPt }
                            }
                            return null
                        }
                        Row {
                            id: row
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: 12
                            Repeater {
                                id: rep
                                model: spread.pageTotal
                                delegate: PageView {
                                    pageIndex: spread.firstPage + index
                                    pxPerPt: viewer.pxPerPt
                                    renderScale: viewer.renderScale
                                    dpr: viewer.dpr
                                    textSelectionEnabled: !viewer.editOn && !viewer.replacingText && !redactions.visible
                                    showTextCaret: textSelectionEnabled && pagesView.activeFocus && viewer.Window.active
                                    redactMode: redactions.visible
                                    redactMarks: redactions.visible ? redactions.marks.filter(m => m.page === pageIndex) : []
                                    redactPreview: redactions.visible && redactions.preview && redactions.preview.page === pageIndex
                                        ? redactions.preview : null
                                }
                            }
                        }
                    }

                    WheelHandler {
                        acceptedModifiers: Qt.ControlModifier
                        onWheel: event => { if (event.angleDelta.y > 0) viewer.zoomIn(); else if (event.angleDelta.y < 0) viewer.zoomOut() }
                    }
                    Keys.onPressed: event => {
                        if (viewer.editOn && Editor.editingId === 0) {
                            const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
                            if (Editor.polygonOpen && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Escape)) { Editor.finishPolygon(); event.accepted = true; return }
                            if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) { Editor.deleteSelected(); event.accepted = true; return }
                            if (ctrl && event.key === Qt.Key_Z && !(event.modifiers & Qt.ShiftModifier)) { Editor.undo(); event.accepted = true; return }
                            if (ctrl && (event.key === Qt.Key_Y || (event.key === Qt.Key_Z && (event.modifiers & Qt.ShiftModifier)))) { Editor.redo(); event.accepted = true; return }
                        }
                        const page = pagesView.height * 0.9
                        const maxY = pagesView.contentHeight - pagesView.height
                        if (event.key === Qt.Key_PageDown || (event.key === Qt.Key_Space && !(event.modifiers & Qt.ShiftModifier)))
                            pagesView.contentY = Math.min(maxY, pagesView.contentY + page)
                        else if (event.key === Qt.Key_PageUp || (event.key === Qt.Key_Space && (event.modifiers & Qt.ShiftModifier)))
                            pagesView.contentY = Math.max(-pagesView.headerItem.height, pagesView.contentY - page)
                        else if (event.key === Qt.Key_Home)
                            viewer.goTo(0)
                        else if (event.key === Qt.Key_End)
                            viewer.goTo(Doc.pageCount - 1)
                        else if (event.key === Qt.Key_Down)
                            pagesView.contentY = Math.min(maxY, pagesView.contentY + 60)
                        else if (event.key === Qt.Key_Up)
                            pagesView.contentY = Math.max(-pagesView.headerItem.height, pagesView.contentY - 60)
                        else
                            return
                        event.accepted = true
                    }
                }

                // The pointer: drag = select text, click = follow a link, right click = menu. It lies over the list but leaves the scroll bars free.
                MouseArea {
                    id: pointer
                    objectName: "pagePointer"
                    anchors.fill: pagesView
                    anchors.rightMargin: 14
                    anchors.bottomMargin: 14
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    hoverEnabled: true
                    cursorShape: redactions.visible || (viewer.editOn && Editor.tool !== "select") ? Qt.CrossCursor
                        : viewer.editOn || viewer.replacingText ? Qt.ArrowCursor : viewer.readerCursor
                    property real pressX: 0
                    property real pressY: 0
                    property real lastX: 0
                    property real lastY: 0
                    property bool dragged: false
                    property bool extendingSelection: false

                    function extend() {
                        const h = viewer.hit(lastX, lastY)
                        if (h)
                            Doc.selectMove(h.page, h.x, h.y)
                    }
                    onPressed: mouse => {
                        pagesView.forceActiveFocus()
                        if (redactions.visible) {
                            if (mouse.button === Qt.LeftButton && !Doc.outputBusy)
                                viewer.redactStart = viewer.hit(mouse.x, mouse.y)
                            return
                        }
                        if (mouse.button === Qt.RightButton) {
                            pageMenu.popup()
                            return
                        }
                        if (viewer.replacingText) {
                            const point = viewer.hit(mouse.x, mouse.y)
                            const original = point ? Doc.existingTextAt(point.page, point.x, point.y) : ""
                            viewer.replaceHint = original === "" ? "editRunUnsupported" : ""
                            if (original !== "")
                                replaceDialog.begin(point, original)
                            return
                        }
                        if (viewer.editOn) {
                            viewer.editPress(mouse.x, mouse.y)
                            return
                        }
                        pressX = lastX = mouse.x
                        pressY = lastY = mouse.y
                        dragged = false
                        extendingSelection = (mouse.modifiers & Qt.ShiftModifier) !== 0
                        const h = viewer.hit(mouse.x, mouse.y)
                        if (h && extendingSelection)
                            Doc.selectMove(h.page, h.x, h.y)
                        else if (h)
                            Doc.selectStart(h.page, h.x, h.y)
                        else if (!extendingSelection)
                            Doc.clearSelection()
                    }
                    onPositionChanged: mouse => {
                        if (viewer.replacingText)
                            return
                        lastX = mouse.x
                        lastY = mouse.y
                        if (redactions.visible) {
                            viewer.updateRedactDrag(mouse.x, mouse.y)
                            return
                        }
                        if (viewer.editOn) {
                            if (Editor.polygonOpen || (pressed && viewer.editMode !== ""))
                                viewer.editMove(mouse.x, mouse.y)
                            return
                        }
                        if (pressed && (mouse.buttons & Qt.LeftButton)) {
                            if (!dragged && Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) > 4)
                                dragged = true
                            if (dragged) {
                                extend()
                                autoScroll.running = mouse.y < 28 || mouse.y > pointer.height - 28
                            }
                        } else {
                            hoverTimer.start()
                        }
                    }
                    onDoubleClicked: mouse => {
                        if (viewer.editOn && Editor.polygonOpen) {
                            Editor.finishPolygon()
                        } else if (viewer.editOn && Editor.tool === "select") {
                            const h = viewer.hit(mouse.x, mouse.y)
                            const id = h ? Editor.hitTest(h.page, h.x, h.y) : 0
                            if (id > 0) {
                                Editor.select(id)
                                viewer.startEditing(id)
                            }
                        }
                    }
                    onReleased: mouse => {
                        if (viewer.replacingText)
                            return
                        if (redactions.visible) {
                            viewer.updateRedactDrag(mouse.x, mouse.y)
                            const mark = redactions.preview
                            if (mark && mark.w >= 2 && mark.h >= 2)
                                redactions.addMarks([mark])
                            viewer.redactStart = null
                            redactions.preview = null
                            return
                        }
                        if (viewer.editOn) {
                            viewer.editRelease()
                            return
                        }
                        autoScroll.running = false
                        if (mouse.button !== Qt.LeftButton)
                            return
                        if (dragged) {
                            lastX = mouse.x
                            lastY = mouse.y
                            extend()
                            return
                        }
                        if (extendingSelection)
                            return
                        const h = viewer.hit(mouse.x, mouse.y)
                        if (h) {
                            const link = Doc.linkAt(h.page, h.x, h.y)
                            if (link.kind !== "none") {
                                Doc.clearSelection()
                                viewer.followLink(link)
                            }
                        }
                    }
                    onCanceled: {
                        autoScroll.stop()
                        viewer.redactStart = null
                        redactions.preview = null
                    }
                    onExited: {
                        hoverTimer.stop()
                        viewer.readerCursor = Qt.ArrowCursor
                    }
                    Timer {
                        id: hoverTimer
                        interval: 45
                        onTriggered: {
                            const h = viewer.hit(pointer.lastX, pointer.lastY)
                            viewer.readerCursor = !h ? Qt.ArrowCursor
                                : Doc.linkAt(h.page, h.x, h.y).kind !== "none" ? Qt.PointingHandCursor
                                : Doc.isTextAt(h.page, h.x, h.y) ? Qt.IBeamCursor : Qt.ArrowCursor
                        }
                    }
                    // dragging above / below the view keeps scrolling and selecting
                    Timer {
                        id: autoScroll
                        interval: 30
                        repeat: true
                        onTriggered: {
                            const up = pointer.lastY < 28
                            const maxY = pagesView.contentHeight - pagesView.height
                            pagesView.contentY = up ? Math.max(-pagesView.headerItem.height, pagesView.contentY - 22) : Math.min(maxY, pagesView.contentY + 22)
                            pointer.extend()
                        }
                    }
                }
            }
        }
    }

    // the field in which the text of an item is typed (the item itself is not drawn while it is edited)
    Item {
        id: editHost
        z: 50
        visible: Editor.editingId > 0 && viewer.editOn
        readonly property var info: { Editor.rev; Editor.editingId; return Editor.editingId > 0 ? Editor.itemInfo(Editor.editingId) : ({}) }
        readonly property point pos: viewer.pageToView(info.page || 0, info.x || 0, info.y || 0)
        x: pos.x
        y: pos.y
        width: (info.w || 0) * viewer.pxPerPt
        height: (info.h || 0) * viewer.pxPerPt
        rotation: info.angle || 0
        TextArea {
            id: editField
            objectName: "editField"
            anchors.fill: parent
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            padding: 2 * viewer.pxPerPt
            color: Editor.color
            font.family: Editor.fontFamily
            font.pixelSize: Math.max(6, Editor.fontSize * viewer.pxPerPt)
            font.bold: Editor.bold
            font.italic: Editor.italic
            horizontalAlignment: Editor.align === 1 ? Text.AlignHCenter : Editor.align === 2 ? Text.AlignRight : Text.AlignLeft
            background: Rectangle { color: "#E6FFFFFF"; border.width: 1; border.color: Theme.accent }
            onTextChanged: { if (Editor.editingId > 0) Editor.setText(Editor.editingId, text) }
            onActiveFocusChanged: { if (!activeFocus) viewer.finishEditing() }
            Keys.onEscapePressed: viewer.finishEditing()
        }
    }
    // unsaved edits of an earlier run (autosave) are offered once per file
    property string recoverAsked: ""
    function offerRecovery() {
        if (Editor.recoverable && Doc.filePath !== recoverAsked && !recoverAsk.opened) {
            recoverAsked = Doc.filePath
            recoverAsk.open()
        }
    }
    Connections { target: Editor; function onChanged() { viewer.offerRecovery() } }
    ConfirmDialog {
        id: recoverAsk
        objectName: "recoverAsk"
        title: Tr.s("recoverTitle")
        text: Tr.s("recoverText")
        confirmText: Tr.s("recoverRestore")
        cancelText: Tr.s("recoverDiscard")
        onConfirmed: { Editor.restoreRecovery(); Editor.active = true }
        onDeclined: Editor.declineRecovery()
    }
    ConfirmDialog {
        id: closeAsk
        property int tabIndex: -1
        title: Tr.s("closeUnsavedTitle")
        text: Tr.s("closeUnsavedText")
        confirmText: Tr.s("closeAnyway")
        cancelText: Tr.s("keepEditing")
        onConfirmed: viewer.reallyCloseTab(tabIndex)
    }

    PMenu {
        id: pageMenu
        PMenuItem { objectName: "menuCopy"; text: Tr.s("copy"); enabled: Doc.hasSelection; onTriggered: Doc.copySelection() }
        PMenuItem { objectName: "menuSelectAll"; text: Tr.s("selectAll"); onTriggered: Doc.selectAll() }
    }

    ReplaceTextDialog {
        id: replaceDialog
        onOpenResult: path => viewer.openPathRequested(path)
    }
    LinkDialog { id: linkDialog; objectName: "linkDialog" }
    PrintDialog { id: printDialog; objectName: "printDialog"; currentPage: viewer.currentPage }

    // ---- keyboard ----------------------------------------------------------------------------------------------------------
    Shortcut { sequences: [StandardKey.ZoomIn, "Ctrl+="]; onActivated: viewer.zoomIn() }
    Shortcut { sequences: [StandardKey.ZoomOut]; onActivated: viewer.zoomOut() }
    Shortcut { sequences: ["Ctrl+0"]; onActivated: viewer.fit("fitPage") }
    Shortcut { sequences: ["Ctrl+1"]; onActivated: viewer.setZoom(1.0) }
    Shortcut { sequences: ["Ctrl+2"]; onActivated: viewer.fit("fitWidth") }
    Shortcut { sequences: ["Ctrl+W"]; onActivated: viewer.closeTabAt(Doc.currentTab) }
    Shortcut { sequences: [StandardKey.Find]; onActivated: viewer.openSearch() }
    Shortcut { sequences: [StandardKey.FindNext, "F3"]; onActivated: Doc.searchStep(1) }
    Shortcut { sequences: [StandardKey.FindPrevious, "Shift+F3"]; onActivated: Doc.searchStep(-1) }
    Shortcut { sequences: [StandardKey.Print]; onActivated: printDialog.open() }
    Shortcut { sequences: [StandardKey.Copy]; enabled: !viewer.editingText; onActivated: Doc.copySelection() }
    Shortcut { sequences: [StandardKey.SelectAll]; enabled: !viewer.editingText; onActivated: Doc.selectAll() }
    Shortcut { sequences: ["Ctrl+Tab"]; onActivated: Doc.nextTab(1) }
    Shortcut { sequences: ["Ctrl+Shift+Tab"]; onActivated: Doc.nextTab(-1) }
    Shortcut { sequences: ["Ctrl+Shift+="]; onActivated: Doc.rotateView(1) }
    Shortcut { sequences: ["Ctrl+Shift+-"]; onActivated: Doc.rotateView(-1) }
    Shortcut {
        sequences: ["Escape"]
        enabled: !printDialog.opened && !linkDialog.opened && !(Window.window && Window.window.visibility === Window.FullScreen)
        onActivated: { if (Editor.polygonOpen) Editor.finishPolygon(); else if (Editor.editingId > 0) viewer.finishEditing(); else if (viewer.searchOpen) viewer.closeSearch(); else Doc.clearSelection() }
    }
}
