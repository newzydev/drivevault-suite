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
    Repeater {
        model: pv.selectionRects
        delegate: Rectangle {
            required property var modelData
            x: modelData.x * pv.pxPerPt
            y: modelData.y * pv.pxPerPt
            width: modelData.w * pv.pxPerPt
            height: modelData.h * pv.pxPerPt
            color: "#664A90E2"
        }
    }
}
