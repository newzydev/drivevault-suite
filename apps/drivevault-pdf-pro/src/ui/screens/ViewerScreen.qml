import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// The reader: tool bar, page thumbnails and the continuous page view. Pages are rendered by the engine on demand (only the visible ones).
Item {
    id: viewer

    signal openRequested()

    // ---- view state ---------------------------------------------------------------------------------------------------
    property real zoom: 1.0                       // 1.0 = 100 % = 96 dpi on screen
    property string zoomMode: "fitWidth"          // "fitWidth" | "fitPage" | "custom"
    property int currentPage: 0
    property bool showThumbs: true
    property real maxW: 612                       // largest page, in points
    property real maxH: 792
    readonly property real pxPerPt: zoom * 96 / 72
    property real renderScale: pxPerPt            // the scale pages are rendered at: follows pxPerPt a moment after zooming stops
    readonly property real dpr: Screen.devicePixelRatio
    readonly property var zoomSteps: [0.1, 0.25, 0.33, 0.5, 0.67, 0.75, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0, 3.0, 4.0, 6.0, 8.0]

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
        const byWidth = availW / (maxW * 96 / 72)
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
    // Keeps the page that is on top where it is while the zoom changes.
    function changeZoom(change) {
        const idx = currentPage
        const item = pagesView.itemAtIndex(idx)
        const frac = item && item.height > 0 ? (pagesView.contentY - item.y) / item.height : 0
        change()
        Qt.callLater(function () {
            const it = pagesView.itemAtIndex(idx)
            if (it)
                pagesView.contentY = it.y + frac * it.height
        })
    }
    function setZoom(z) { changeZoom(function () { zoomMode = "custom"; zoom = Math.max(0.1, Math.min(8.0, z)) }) }
    function zoomIn() {
        for (let i = 0; i < zoomSteps.length; ++i)
            if (zoomSteps[i] > zoom * 1.001) { setZoom(zoomSteps[i]); return }
    }
    function zoomOut() {
        for (let i = zoomSteps.length - 1; i >= 0; --i)
            if (zoomSteps[i] < zoom / 1.001) { setZoom(zoomSteps[i]); return }
    }
    function fit(mode) { changeZoom(function () { zoomMode = mode; applyFit() }) }
    function goTo(i) {
        const n = Doc.pageCount
        if (n <= 0)
            return
        i = Math.max(0, Math.min(n - 1, i))
        pagesView.positionViewAtIndex(i, ListView.Beginning)
        currentPage = i
    }
    function updateCurrent() {
        const x = pagesView.contentX + pagesView.width / 2
        let idx = -1
        for (const dy of [0.35, 0.3, 0.4, 0.2, 0.5]) {
            idx = pagesView.indexAt(x, pagesView.contentY + pagesView.height * dy)
            if (idx >= 0)
                break
        }
        if (idx >= 0 && idx !== currentPage)
            currentPage = idx
    }

    Connections {
        target: Doc
        function onDocumentChanged() {
            if (!Doc.isOpen)
                return
            viewer.measure()
            viewer.currentPage = 0
            viewer.zoomMode = "fitWidth"
            viewer.applyFit()
            viewer.renderScale = viewer.pxPerPt
            pagesView.contentY = -pagesView.headerItem.height
        }
    }
    Component.onCompleted: { measure(); applyFit(); renderScale = pxPerPt }
    onWidthChanged: applyFit()
    onHeightChanged: applyFit()
    onPxPerPtChanged: renderTimer.restart()
    onCurrentPageChanged: thumbsView.positionViewAtIndex(currentPage, ListView.Contain)
    Timer { id: renderTimer; interval: 140; onTriggered: viewer.renderScale = viewer.pxPerPt }

    // ---- layout ---------------------------------------------------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // tool bar
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 56
            color: "#0D0E11"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8

                PButton { objectName: "toggleThumbs"; compact: true; text: "☰"; tip: Tr.s("thumbnails"); variant: viewer.showThumbs ? "secondary" : "ghost"; onClicked: viewer.showThumbs = !viewer.showThumbs }
                PButton { objectName: "toolbarOpen"; compact: true; text: Tr.s("openFile"); onClicked: viewer.openRequested() }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.border; Layout.leftMargin: 4; Layout.rightMargin: 4 }

                PButton { objectName: "prevPage"; compact: true; text: "‹"; tip: Tr.s("prevPage"); enabled: viewer.currentPage > 0; onClicked: viewer.goTo(viewer.currentPage - 1) }
                TextField {
                    id: pageField
                    objectName: "pageField"
                    implicitWidth: 54
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
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.border; Layout.leftMargin: 4; Layout.rightMargin: 4 }

                PButton { objectName: "zoomOut"; compact: true; text: "−"; tip: Tr.s("zoomOut"); onClicked: viewer.zoomOut() }
                AppText {
                    objectName: "zoomLabel"
                    Layout.preferredWidth: 52
                    text: Math.round(viewer.zoom * 100) + "%"
                    horizontalAlignment: Text.AlignHCenter
                    font.weight: Font.Medium
                }
                PButton { objectName: "zoomIn"; compact: true; text: "+"; tip: Tr.s("zoomIn"); onClicked: viewer.zoomIn() }
                PButton { objectName: "fitWidth"; compact: true; text: Tr.s("fitWidth"); variant: viewer.zoomMode === "fitWidth" ? "secondary" : "ghost"; onClicked: viewer.fit("fitWidth") }
                PButton { objectName: "fitPage"; compact: true; text: Tr.s("fitPage"); variant: viewer.zoomMode === "fitPage" ? "secondary" : "ghost"; onClicked: viewer.fit("fitPage") }
                PButton { objectName: "actualSize"; compact: true; text: "100%"; tip: Tr.s("actualSize"); variant: "ghost"; onClicked: viewer.setZoom(1.0) }

                Item { Layout.fillWidth: true }
                LanguageSwitch {}
                PButton { objectName: "closeFile"; compact: true; text: "✕"; tip: Tr.s("closeFile"); variant: "ghost"; onClicked: Doc.close() }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // thumbnails
            Rectangle {
                visible: viewer.showThumbs
                Layout.preferredWidth: 168
                Layout.fillHeight: true
                color: Theme.backgroundAlt
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.borderSubtle }
                ListView {
                    id: thumbsView
                    objectName: "thumbnails"
                    anchors.fill: parent
                    anchors.rightMargin: 1
                    clip: true
                    model: Doc.isOpen ? Doc.pageCount : 0
                    spacing: 10
                    topMargin: 12
                    bottomMargin: 12
                    boundsBehavior: Flickable.StopAtBounds
                    cacheBuffer: 600
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Item {
                        id: thumb
                        required property int index
                        readonly property real pw: { Doc.docId; return Doc.pageWidthPt(index) }
                        readonly property real ph: { Doc.docId; return Doc.pageHeightPt(index) }
                        readonly property real tw: 118
                        width: thumbsView.width
                        height: (pw > 0 ? tw * ph / pw : tw * 1.4) + 24
                        Rectangle {
                            x: (parent.width - width) / 2
                            width: thumb.tw
                            height: parent.height - 24
                            color: "white"
                            border.width: viewer.currentPage === thumb.index ? 3 : 0
                            border.color: Theme.accent
                            Image {
                                anchors.fill: parent
                                asynchronous: true
                                smooth: true
                                source: thumb.pw > 0 ? "image://pdf/" + Doc.docId + "/" + thumb.index + "/" + Math.round(thumb.tw / thumb.pw * viewer.dpr * 1000) : ""
                            }
                        }
                        AppText {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: thumb.index + 1
                            font.pixelSize: Theme.fontSm
                            color: viewer.currentPage === thumb.index ? Theme.text : Theme.textSecondary
                        }
                        TapHandler { onTapped: viewer.goTo(thumb.index) }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                }
            }

            // pages
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.viewerBackground
                clip: true

                ListView {
                    id: pagesView
                    objectName: "pages"
                    anchors.fill: parent
                    model: Doc.isOpen ? Doc.pageCount : 0
                    spacing: 12
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.AutoFlickDirection
                    contentWidth: Math.max(width, viewer.maxW * viewer.pxPerPt + 48)
                    cacheBuffer: 1500
                    focus: true
                    ScrollBar.vertical: ScrollBar {}
                    ScrollBar.horizontal: ScrollBar {}
                    header: Item { height: 12 }
                    footer: Item { height: 12 }
                    onContentYChanged: viewer.updateCurrent()
                    onContentXChanged: viewer.updateCurrent()

                    delegate: Item {
                        id: pageItem
                        required property int index
                        readonly property real pw: { Doc.docId; return Doc.pageWidthPt(index) * viewer.pxPerPt }
                        width: pagesView.contentWidth
                        height: { Doc.docId; return Doc.pageHeightPt(index) * viewer.pxPerPt }
                        Rectangle {
                            x: (parent.width - width) / 2
                            width: pageItem.pw
                            height: pageItem.height
                            color: "white"
                            Image {
                                objectName: "pageImage_" + pageItem.index
                                anchors.fill: parent
                                asynchronous: true
                                smooth: true
                                source: "image://pdf/" + Doc.docId + "/" + pageItem.index + "/" + Math.round(viewer.renderScale * viewer.dpr * 1000)
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
            }
        }
    }

    // keyboard shortcuts of the viewer
    Shortcut { sequences: [StandardKey.ZoomIn, "Ctrl+="]; onActivated: viewer.zoomIn() }
    Shortcut { sequences: [StandardKey.ZoomOut]; onActivated: viewer.zoomOut() }
    Shortcut { sequences: ["Ctrl+0"]; onActivated: viewer.fit("fitPage") }
    Shortcut { sequences: ["Ctrl+1"]; onActivated: viewer.setZoom(1.0) }
    Shortcut { sequences: ["Ctrl+2"]; onActivated: viewer.fit("fitWidth") }
    Shortcut { sequences: ["Ctrl+W"]; onActivated: Doc.close() }
}
