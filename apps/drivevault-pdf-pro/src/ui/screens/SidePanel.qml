import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// The left panel: page thumbnails, the outline (bookmarks) and the comments / markup list.
Rectangle {
    id: panel

    property string mode: "thumbs"        // "thumbs" | "outline" | "comments"
    property int currentPage: 0
    property real dpr: 1
    signal pageRequested(int page, real y)

    color: Theme.backgroundAlt
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.borderSubtle }

    property var outlineItems: []
    function reload() { outlineItems = Doc.outline() }
    Component.onCompleted: reload()
    Connections { target: Doc; function onDocumentChanged() { panel.reload() } }
    onModeChanged: if (mode === "comments") Doc.requestAnnotations()
    Connections {
        target: Doc
        function onDocumentChanged() { if (panel.mode === "comments") Doc.requestAnnotations() }
    }
    onCurrentPageChanged: if (mode === "thumbs") thumbs.positionViewAtIndex(currentPage, ListView.Contain)

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: 1
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 44
            color: "transparent"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 6
                PButton { objectName: "sideThumbs"; compact: true; glyph: "thumbs"; tip: Tr.s("thumbnails"); variant: panel.mode === "thumbs" ? "secondary" : "ghost"; onClicked: panel.mode = "thumbs" }
                PButton { objectName: "sideOutline"; compact: true; glyph: "outline"; tip: Tr.s("outline"); variant: panel.mode === "outline" ? "secondary" : "ghost"; onClicked: panel.mode = "outline" }
                PButton { objectName: "sideComments"; compact: true; glyph: "comment"; tip: Tr.s("comments"); variant: panel.mode === "comments" ? "secondary" : "ghost"; onClicked: panel.mode = "comments" }
                Item { Layout.fillWidth: true }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: panel.mode === "thumbs" ? 0 : panel.mode === "outline" ? 1 : 2

            // ---- thumbnails -----------------------------------------------------------------------------------------------
            ListView {
                id: thumbs
                objectName: "thumbnails"
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
                    readonly property real pw: { Doc.docId; Doc.rotation; return Doc.pageWidthPt(index) }
                    readonly property real ph: { Doc.docId; Doc.rotation; return Doc.pageHeightPt(index) }
                    readonly property real tw: 118
                    width: thumbs.width
                    height: (pw > 0 ? tw * ph / pw : tw * 1.4) + 24
                    Rectangle {
                        x: (parent.width - width) / 2
                        width: thumb.tw
                        height: parent.height - 24
                        color: Doc.darkPages ? "#141414" : "white"
                        border.width: panel.currentPage === thumb.index ? 3 : 0
                        border.color: Theme.accent
                        Image {
                            anchors.fill: parent
                            asynchronous: true
                            smooth: true
                            source: thumb.pw > 0 ? "image://pdf/" + Doc.docId + "/" + thumb.index + "/" + Math.round(thumb.tw / thumb.pw * panel.dpr * 1000)
                                                   + "/" + Doc.rotation + "/" + (Doc.darkPages ? 1 : 0) : ""
                        }
                    }
                    AppText {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: thumb.index + 1
                        font.pixelSize: Theme.fontSm
                        color: panel.currentPage === thumb.index ? Theme.text : Theme.textSecondary
                    }
                    TapHandler { onTapped: panel.pageRequested(thumb.index, 0) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }

            // ---- outline ---------------------------------------------------------------------------------------------------
            Item {
                ListView {
                    id: outlineList
                    objectName: "outlineList"
                    anchors.fill: parent
                    clip: true
                    model: panel.outlineItems
                    topMargin: 6
                    bottomMargin: 6
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        required property int index
                        objectName: "outline_" + index
                        width: outlineList.width
                        implicitHeight: Math.max(34, titleText.implicitHeight + 14)
                        height: implicitHeight
                        color: rowHover.hovered && modelData.page >= 0 ? Theme.surfaceHover : "transparent"
                        HoverHandler { id: rowHover; cursorShape: row.modelData.page >= 0 ? Qt.PointingHandCursor : Qt.ArrowCursor }
                        TapHandler { enabled: row.modelData.page >= 0; onTapped: panel.pageRequested(row.modelData.page, 0) }
                        AppText {
                            id: titleText
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 12 + row.modelData.level * 16
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData.title
                            wrapMode: Text.Wrap
                            font.pixelSize: Theme.fontSm
                            font.weight: row.modelData.level === 0 ? Font.DemiBold : Font.Normal
                            color: row.modelData.page >= 0 ? Theme.text : Theme.textDisabled
                        }
                    }
                }
                AppText {
                    objectName: "noOutline"
                    visible: panel.outlineItems.length === 0
                    anchors.centerIn: parent
                    width: parent.width - 32
                    text: Tr.s("noOutline")
                    color: Theme.textDisabled
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSm
                }
            }

            // ---- comments / markup --------------------------------------------------------------------------------------------
            Item {
                ListView {
                    id: commentList
                    objectName: "commentList"
                    anchors.fill: parent
                    clip: true
                    model: Doc.annotationItems
                    topMargin: 6
                    bottomMargin: 6
                    spacing: 2
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: crow
                        required property var modelData
                        required property int index
                        objectName: "comment_" + index
                        width: commentList.width
                        implicitHeight: ctext.implicitHeight + 28
                        height: implicitHeight
                        color: chover.hovered ? Theme.surfaceHover : "transparent"
                        HoverHandler { id: chover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: panel.pageRequested(crow.modelData.page, crow.modelData.y) }
                        ColumnLayout {
                            id: ctext
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 2
                            AppText {
                                Layout.fillWidth: true
                                text: crow.modelData.type + "  ·  " + Tr.f("pageN", crow.modelData.page + 1)
                                font.pixelSize: Theme.fontSm
                                color: Theme.accentText
                            }
                            AppText {
                                Layout.fillWidth: true
                                visible: crow.modelData.text !== ""
                                text: crow.modelData.text
                                wrapMode: Text.Wrap
                                font.pixelSize: Theme.fontSm
                            }
                        }
                    }
                }
                AppText {
                    objectName: "noComments"
                    visible: Doc.annotationItems.length === 0
                    anchors.centerIn: parent
                    width: parent.width - 32
                    text: Doc.annotationsLoading ? Tr.s("loading") : Tr.s("noComments")
                    color: Theme.textDisabled
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSm
                }
            }
        }
    }
}
