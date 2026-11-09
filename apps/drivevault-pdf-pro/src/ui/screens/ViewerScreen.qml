import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// The reader: tabs, tool bar, search bar, side panel (thumbnails / bookmarks / comments) and the page view.
// Pages are shown one per row (continuous) or two per row (spreads); the pointer selects text, clicks follow links.
Item {
    id: viewer

    signal openRequested()
    signal organizeRequested()
    signal splitRequested()
    signal compressRequested()

    // ---- view state ---------------------------------------------------------------------------------------------------
    property real zoom: 1.0                       // 1.0 = 100 % = 96 dpi on screen
    property string zoomMode: "fitWidth"          // "fitWidth" | "fitPage" | "custom"
    property string viewMode: "continuous"        // "continuous" | "twoPage"
    property int currentPage: 0
    property bool showSidebar: true
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
    property string linkCursor: ""

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
        flushSave()                                // the view of the file is saved while its tab still exists
        Doc.closeTab(index)
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
        zoomMode = (st.zoomMode === "custom" || st.zoomMode === "fitPage") ? st.zoomMode : "fitWidth"
        measure()
        if (zoomMode === "custom" && st.zoom > 0)
            zoom = Math.max(0.1, Math.min(8.0, st.zoom))
        else
            applyFit()
        renderScale = pxPerPt
        const page = st.page > 0 ? Math.min(st.page, Doc.pageCount - 1) : 0
        currentPage = page
        pagesView.contentY = -pagesView.headerItem.height
        Qt.callLater(function () { goTo(page) })
    }

    Connections {
        target: Doc
        function onDocumentChanged() {
            viewer.flushSave()
            if (Doc.isOpen)
                viewer.restoreView()
        }
        function onRotationChanged() {
            viewer.keepPosition(function () { viewer.measure(); viewer.applyFit() })
        }
        function onSearchTarget(page, y) { viewer.goTo(page, y) }
    }
    Component.onCompleted: restoreView()
    onWidthChanged: applyFit()
    onHeightChanged: applyFit()
    onPxPerPtChanged: renderTimer.restart()
    Timer { id: renderTimer; interval: 140; onTriggered: viewer.renderScale = viewer.pxPerPt }

    // ---- layout ---------------------------------------------------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        FileTabs { Layout.fillWidth: true; onOpenRequested: viewer.openRequested(); onCloseTabRequested: index => viewer.closeTabAt(index) }      // not "TabBar": that name is a Qt Quick Controls type

        // tool bar
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Math.max(52, toolFlow.implicitHeight + 16)
            color: "#0D0E11"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
            Flow {
                id: toolFlow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 8
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 6

                PButton { objectName: "toggleSidebar"; compact: true; glyph: "sidebar"; tip: Tr.s("thumbnails"); variant: viewer.showSidebar ? "secondary" : "ghost"; onClicked: viewer.showSidebar = !viewer.showSidebar }
                PButton { objectName: "toolbarOpen"; compact: true; text: Tr.s("openFile"); onClicked: viewer.openRequested() }
                Item { width: 5; height: 32; Rectangle { anchors.centerIn: parent; width: 1; height: 22; color: Theme.border } }

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
                AppText { text: Tr.f("pageOf", Doc.pageCount); color: Theme.textSecondary }
                PButton { objectName: "nextPage"; compact: true; text: "›"; tip: Tr.s("nextPage"); enabled: viewer.currentPage < Doc.pageCount - 1; onClicked: viewer.goTo(viewer.currentPage + 1) }
                Item { width: 5; height: 32; Rectangle { anchors.centerIn: parent; width: 1; height: 22; color: Theme.border } }

                PButton { objectName: "zoomOut"; compact: true; text: "−"; tip: Tr.s("zoomOut"); onClicked: viewer.zoomOut() }
                PButton {
                    objectName: "zoomLabel"
                    compact: true
                    variant: "ghost"
                    implicitWidth: 58
                    text: Math.round(viewer.zoom * 100) + "%"
                    tip: Tr.s("actualSize")
                    onClicked: viewer.setZoom(1.0)
                }
                PButton { objectName: "zoomIn"; compact: true; text: "+"; tip: Tr.s("zoomIn"); onClicked: viewer.zoomIn() }
                PButton { objectName: "fitWidth"; compact: true; text: Tr.s("fitWidth"); variant: viewer.zoomMode === "fitWidth" ? "secondary" : "ghost"; onClicked: viewer.fit("fitWidth") }
                PButton { objectName: "fitPage"; compact: true; text: Tr.s("fitPage"); variant: viewer.zoomMode === "fitPage" ? "secondary" : "ghost"; onClicked: viewer.fit("fitPage") }
                Item { width: 5; height: 32; Rectangle { anchors.centerIn: parent; width: 1; height: 22; color: Theme.border } }

                PButton {
                    objectName: "viewMode"
                    compact: true
                    glyph: viewer.viewMode === "twoPage" ? "pages2" : "page"
                    tip: viewer.viewMode === "twoPage" ? Tr.s("viewContinuous") : Tr.s("viewTwoPage")
                    onClicked: viewer.setViewMode(viewer.viewMode === "twoPage" ? "continuous" : "twoPage")
                }
                PButton { objectName: "rotate"; compact: true; glyph: "rotate"; tip: Tr.s("rotateRight"); onClicked: Doc.rotateView(1) }
                PButton { objectName: "darkPages"; compact: true; glyph: "dark"; tip: Tr.s("darkPages"); variant: Doc.darkPages ? "secondary" : "ghost"; onClicked: Doc.darkPages = !Doc.darkPages }
                Item { width: 5; height: 32; Rectangle { anchors.centerIn: parent; width: 1; height: 22; color: Theme.border } }

                PButton { objectName: "findButton"; compact: true; text: Tr.s("find"); variant: viewer.searchOpen ? "secondary" : "ghost"; onClicked: viewer.searchOpen ? viewer.closeSearch() : viewer.openSearch() }
                PButton { objectName: "printButton"; compact: true; text: Tr.s("print"); variant: "ghost"; onClicked: printDialog.open() }
                PButton { objectName: "organizeButton"; compact: true; text: Tr.s("toolOrganizeShort"); variant: "ghost"; tip: Tr.s("toolOrganize"); onClicked: viewer.organizeRequested() }
                PButton { objectName: "splitButton"; compact: true; text: Tr.s("toolSplitShort"); variant: "ghost"; tip: Tr.s("toolSplit"); onClicked: viewer.splitRequested() }
                PButton { objectName: "compressButton"; compact: true; text: Tr.s("toolCompressShort"); variant: "ghost"; tip: Tr.s("toolCompress"); onClicked: viewer.compressRequested() }

                LanguageSwitch {}
            }
        }

        SearchBar {
            id: searchBar
            objectName: "searchBar"
            Layout.fillWidth: true
            visible: viewer.searchOpen
            startPage: viewer.currentPage
            onCloseRequested: viewer.closeSearch()
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
                                }
                            }
                        }
                    }

                    WheelHandler {
                        acceptedModifiers: Qt.ControlModifier
                        onWheel: event => { if (event.angleDelta.y > 0) viewer.zoomIn(); else if (event.angleDelta.y < 0) viewer.zoomOut() }
                    }
                    Keys.onPressed: event => {
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
                    cursorShape: viewer.linkCursor !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor
                    property real pressX: 0
                    property real pressY: 0
                    property real lastX: 0
                    property real lastY: 0
                    property bool dragged: false

                    function extend() {
                        const h = viewer.hit(lastX, lastY)
                        if (h)
                            Doc.selectMove(h.page, h.x, h.y)
                    }
                    onPressed: mouse => {
                        pagesView.forceActiveFocus()
                        if (viewer.searchOpen && !searchBar.activeFocus)
                            pagesView.forceActiveFocus()
                        if (mouse.button === Qt.RightButton) {
                            pageMenu.popup()
                            return
                        }
                        pressX = lastX = mouse.x
                        pressY = lastY = mouse.y
                        dragged = false
                        const h = viewer.hit(mouse.x, mouse.y)
                        if (h)
                            Doc.selectStart(h.page, h.x, h.y)
                        else
                            Doc.clearSelection()
                    }
                    onPositionChanged: mouse => {
                        lastX = mouse.x
                        lastY = mouse.y
                        if (pressed && (mouse.buttons & Qt.LeftButton)) {
                            if (!dragged && Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) > 4)
                                dragged = true
                            if (dragged) {
                                extend()
                                autoScroll.running = mouse.y < 28 || mouse.y > pointer.height - 28
                            }
                        } else {
                            hoverTimer.restart()
                        }
                    }
                    onReleased: mouse => {
                        autoScroll.running = false
                        if (mouse.button !== Qt.LeftButton || dragged)
                            return
                        const h = viewer.hit(mouse.x, mouse.y)
                        if (h) {
                            const link = Doc.linkAt(h.page, h.x, h.y)
                            if (link.kind !== "none")
                                viewer.followLink(link)
                        }
                    }
                    onExited: viewer.linkCursor = ""
                    // the hand cursor over a link (looked up a moment after the pointer stops, not on every move)
                    Timer {
                        id: hoverTimer
                        interval: 70
                        onTriggered: {
                            const h = viewer.hit(pointer.lastX, pointer.lastY)
                            viewer.linkCursor = h && Doc.linkAt(h.page, h.x, h.y).kind !== "none" ? "link" : ""
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

    PMenu {
        id: pageMenu
        PMenuItem { objectName: "menuCopy"; text: Tr.s("copy"); enabled: Doc.hasSelection; onTriggered: Doc.copySelection() }
        PMenuItem { objectName: "menuSelectAll"; text: Tr.s("selectAll"); onTriggered: Doc.selectAll() }
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
        onActivated: { if (viewer.searchOpen) viewer.closeSearch(); else Doc.clearSelection() }
    }
}
