import QtQuick
import DriveVault.PdfPro.Backend

// One page: white sheet, the rendered image, the search hits and the selected text on top. Sizes are in screen pixels:
// pxPerPt = pixels per point of the page (zoom), renderScale = the scale the image is rendered at (follows pxPerPt a moment after zooming).
Item {
    id: pv

    property int pageIndex: 0
    property real pxPerPt: 96 / 72
    property real renderScale: 96 / 72
    property real dpr: 1

    // the page as shown (view rotation applied); reading Doc.docId / Doc.rotation makes the values follow the document and the rotation
    readonly property real pw: { Doc.docId; Doc.rotation; return Doc.pageWidthPt(pageIndex) * pxPerPt }
    readonly property real ph: { Doc.docId; Doc.rotation; return Doc.pageHeightPt(pageIndex) * pxPerPt }
    property var searchRects: []
    property var selectionRects: []
    property var redactMarks: []
    property var redactPreview: null
    property bool redactMode: false
    property bool textSelectionEnabled: false
    property bool showTextCaret: false
    readonly property var textCaret: Doc.textCaret

    width: pw
    height: ph

    function refreshSearch() { searchRects = Doc.searchRects(pageIndex) }
    function refreshSelection() { selectionRects = Doc.selectionRects(pageIndex) }
    Component.onCompleted: { refreshSearch(); refreshSelection() }
    onPageIndexChanged: { refreshSearch(); refreshSelection() }
    Connections {
        target: Doc
        function onSearchChanged() { pv.refreshSearch() }
        function onSelectionChanged() { pv.refreshSelection() }
        function onRotationChanged() { pv.refreshSearch(); pv.refreshSelection() }
    }

    Rectangle { anchors.fill: parent; color: Doc.darkPages ? "#141414" : "white" }
    Image {
        objectName: "pageImage_" + pv.pageIndex
        anchors.fill: parent
        asynchronous: true
        smooth: true
        source: "image://pdf/" + Doc.docId + "/" + pv.pageIndex + "/" + Math.round(pv.renderScale * pv.dpr * 1000)
                + "/" + Doc.rotation + "/" + (Doc.darkPages ? 1 : 0)
    }
    Repeater {
        model: pv.searchRects
        delegate: Rectangle {
            required property var modelData
            x: modelData.x * pv.pxPerPt - 1
            y: modelData.y * pv.pxPerPt - 1
            width: modelData.w * pv.pxPerPt + 2
            height: modelData.h * pv.pxPerPt + 2
            radius: 2
            color: modelData.current ? "#B3FF9800" : "#66FFEB3B"
            border.width: modelData.current ? 1 : 0
            border.color: "#E65100"
        }
    }
    // what the user has put on the page (drawn by the backend; the same painter writes it into the new file)
    Image {
        objectName: "overlay_" + pv.pageIndex
        anchors.fill: parent
        visible: !pv.redactMode && Editor.count > 0
        asynchronous: true
        cache: false
        smooth: true
        source: Editor.count > 0 ? "image://ovl/" + Doc.docId + "/" + pv.pageIndex + "/" + Math.round(pv.renderScale * pv.dpr * 1000) + "/" + Doc.rotation + "/" + Editor.rev : ""
    }
    Repeater {
        model: pv.redactPreview ? pv.redactMarks.concat([pv.redactPreview]) : pv.redactMarks
        delegate: Rectangle {
            required property var modelData
            objectName: "redactionMark_" + pv.pageIndex
            x: modelData.x * pv.pxPerPt
            y: modelData.y * pv.pxPerPt
            width: modelData.w * pv.pxPerPt
            height: modelData.h * pv.pxPerPt
            color: "#66EF4444"
            border.color: "#DC2626"
            border.width: 2
        }
    }
    // the frame and handles of the selected item
    readonly property var sel: { Editor.rev; Editor.selectedId; return Editor.active && Doc.rotation === 0 && Editor.selectedId > 0 ? Editor.selectedInfo() : ({}) }
    Item {
        visible: pv.sel.page === pv.pageIndex
        x: (pv.sel.x || 0) * pv.pxPerPt
        y: (pv.sel.y || 0) * pv.pxPerPt
        width: (pv.sel.w || 0) * pv.pxPerPt
        height: (pv.sel.h || 0) * pv.pxPerPt
        rotation: pv.sel.angle || 0
        Rectangle { objectName: "selectionFrame"; anchors.fill: parent; anchors.margins: -2; color: "transparent"; border.width: 1; border.color: "#3B82F6" }
        Repeater {
            model: pv.sel.page === pv.pageIndex && pv.sel.handles ? pv.sel.handles : []
            delegate: Rectangle {
                required property var modelData
                x: modelData.x * pv.pxPerPt - (pv.sel.x || 0) * pv.pxPerPt - 4
                y: modelData.y * pv.pxPerPt - (pv.sel.y || 0) * pv.pxPerPt - 4
                width: 8; height: 8
                color: "white"; border.width: 1; border.color: "#3B82F6"
            }
        }
    }
    Repeater {
        model: pv.textSelectionEnabled ? pv.selectionRects : []
        delegate: Rectangle {
            required property var modelData
            x: modelData.x * pv.pxPerPt
            y: modelData.y * pv.pxPerPt
            width: modelData.w * pv.pxPerPt
            height: modelData.h * pv.pxPerPt
            color: Doc.darkPages ? "#805E9FFF" : "#553B82F6"
        }
    }
    Rectangle {
        id: caret
        objectName: "textCaret_" + pv.pageIndex
        visible: pv.showTextCaret && pv.textCaret.page === pv.pageIndex
        x: Math.round((pv.textCaret.x || 0) * pv.pxPerPt)
        y: Math.round((pv.textCaret.y || 0) * pv.pxPerPt)
        width: Math.max(1, (pv.textCaret.w || 0) * pv.pxPerPt)
        height: Math.max(1, (pv.textCaret.h || 0) * pv.pxPerPt)
        color: Doc.darkPages ? "#A9CCFF" : "#17488C"
        onVisibleChanged: opacity = 1
        Connections {
            target: Doc
            function onSelectionChanged() {
                if (caret.visible) {
                    caret.opacity = 1
                    blink.restart()
                }
            }
        }
        Timer {
            id: blink
            interval: 500
            repeat: true
            running: caret.visible
            onTriggered: caret.opacity = 1 - caret.opacity
        }
    }
}
