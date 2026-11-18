import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Master.Backend

// The answer of the AI, drawn like Claude's: its Markdown (C++: markdownBlocks, MarkdownModel) is a list of blocks and every block is its own piece - paragraphs,
// headings, rows of lists with their bullets, code boxes with rounded corners and a copy button, tables with thin lines, quotes with a bar, rules. A block is a
// selectable text. While an answer is written only the last block changes (the model reports just the rows that changed), so the finished blocks stay as they are.
Column {
    id: view

    property string text: ""
    signal linkActivated(string link)

    spacing: 0
    readonly property var answerStyle: ({ codeBg: Theme.surface.toString(), text: Theme.text.toString(), link: Theme.accent.toString(), fontPx: 15 })

    MarkdownModel { id: blocks; markdown: view.text; style: view.answerStyle }

    Repeater {
        model: blocks
        delegate: Item {
            id: blk
            required property var block
            required property int index
            readonly property string kind: block.type
            readonly property int quote: block.quote
            readonly property int topGap: index === 0 ? 0 : kind === "h" ? (block.level <= 2 ? 16 : 8) : (kind === "table" || kind === "code") ? 8 : 0
            readonly property int gapAfter: kind === "h" ? 8 : kind === "li" ? 5 : kind === "p" ? 12 : 16
            width: view.width
            height: ld.height + topGap + gapAfter

            Rectangle {                                        // a quoted block: a bar at the left
                visible: blk.quote > 0
                x: 0
                y: blk.topGap
                width: 3
                height: ld.height
                radius: 1.5
                color: Theme.border
            }
            Loader {
                id: ld
                x: blk.quote * 18
                y: blk.topGap
                width: blk.width - x
                sourceComponent: blk.kind === "p" ? pComp : blk.kind === "h" ? hComp : blk.kind === "li" ? liComp : blk.kind === "code" ? codeComp : blk.kind === "table" ? tableComp : hrComp
            }

            // ---- a paragraph ----
            Component {
                id: pComp
                TextEdit {
                    width: parent.width
                    text: "<p style=\"margin:0px; line-height:112%\">" + blk.block.html + "</p>"
                    textFormat: TextEdit.RichText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    font.pixelSize: 15
                    color: blk.quote > 0 ? Theme.textSecondary : Theme.text
                    onLinkActivated: link => view.linkActivated(link)
                }
            }
            // ---- a heading ----
            Component {
                id: hComp
                TextEdit {
                    width: parent.width
                    text: "<p style=\"margin:0px; line-height:105%\">" + blk.block.html + "</p>"
                    textFormat: TextEdit.RichText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    font.pixelSize: blk.block.level === 1 ? 22 : blk.block.level === 2 ? 19 : blk.block.level === 3 ? 17 : 15
                    font.weight: Font.DemiBold
                    color: Theme.text
                    onLinkActivated: link => view.linkActivated(link)
                }
            }
            // ---- one row of a list: the bullet (or the number) and the text, indented by the level ----
            Component {
                id: liComp
                Item {
                    width: parent.width
                    implicitHeight: liText.implicitHeight
                    readonly property real indent: blk.block.depth * 24
                    Text {
                        x: parent.indent
                        width: 22
                        text: blk.block.marker
                        horizontalAlignment: Text.AlignRight
                        font.pixelSize: 15
                        color: Theme.textSecondary
                    }
                    TextEdit {
                        id: liText
                        x: parent.indent + 30
                        width: parent.width - x
                        text: "<p style=\"margin:0px; line-height:112%\">" + blk.block.html + "</p>"
                        textFormat: TextEdit.RichText
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        font.pixelSize: 15
                        color: blk.quote > 0 ? Theme.textSecondary : Theme.text
                        onLinkActivated: link => view.linkActivated(link)
                    }
                }
            }
            // ---- a code box: rounded, the language and a copy button on top ----
            Component {
                id: codeComp
                Rectangle {
                    id: box
                    property bool copied: false
                    width: parent.width
                    implicitHeight: boxColumn.implicitHeight
                    radius: 10
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.borderSubtle
                    Timer { id: copiedTimer; interval: 1600; onTriggered: box.copied = false }
                    Column {
                        id: boxColumn
                        width: parent.width
                        Item {
                            width: parent.width
                            height: 34
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 14
                                anchors.verticalCenter: parent.verticalCenter
                                text: blk.block.lang !== "" ? blk.block.lang : ""
                                font.pixelSize: 12
                                color: Theme.textSecondary
                            }
                            Rectangle {
                                id: copyButton
                                objectName: "answerCopyCode"
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                width: copyLabel.implicitWidth + 20
                                height: 24
                                radius: 6
                                color: copyArea.containsMouse ? Theme.surfaceHover : "transparent"
                                Text {
                                    id: copyLabel
                                    anchors.centerIn: parent
                                    text: box.copied ? Tr.s("copy_done") : Tr.s("copy_code")
                                    font.pixelSize: 12
                                    color: box.copied ? Theme.success : Theme.textSecondary
                                }
                                MouseArea {
                                    id: copyArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { Ai.copyText(blk.block.text); box.copied = true; copiedTimer.restart() }
                                }
                            }
                        }
                        Rectangle { width: parent.width; height: 1; color: Theme.borderSubtle }
                        Item {
                            width: parent.width
                            height: codeText.implicitHeight + 24
                            TextEdit {
                                id: codeText
                                x: 14
                                y: 12
                                width: parent.width - 28
                                text: blk.block.text
                                textFormat: TextEdit.PlainText
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextEdit.WrapAnywhere
                                font.family: "Consolas"
                                font.pixelSize: 13
                                color: Theme.text
                            }
                        }
                    }
                }
            }
            // ---- a table: thin lines, a header row, columns of the same width ----
            Component {
                id: tableComp
                Rectangle {
                    width: parent.width
                    implicitHeight: tableColumn.implicitHeight + 2
                    radius: 10
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.border
                    clip: true
                    Column {
                        id: tableColumn
                        x: 1
                        y: 1
                        width: parent.width - 2
                        RowLayout {
                            width: parent.width
                            spacing: 0
                            Repeater {
                                model: blk.block.head
                                delegate: Rectangle {
                                    id: headCell
                                    required property string modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    Layout.fillHeight: true
                                    implicitHeight: headText.implicitHeight + 20
                                    color: Theme.surface
                                    TextEdit {
                                        id: headText
                                        x: 12
                                        y: 10
                                        width: parent.width - 24
                                        text: "<b>" + headCell.modelData + "</b>"
                                        textFormat: TextEdit.RichText
                                        readOnly: true
                                        selectByMouse: true
                                        wrapMode: TextEdit.Wrap
                                        horizontalAlignment: blk.block.aligns[headCell.index] === "right" ? Text.AlignRight : blk.block.aligns[headCell.index] === "center" ? Text.AlignHCenter : Text.AlignLeft
                                        font.pixelSize: 14
                                        color: Theme.text
                                    }
                                }
                            }
                        }
                        Repeater {
                            model: blk.block.rows
                            delegate: Column {
                                id: bodyRow
                                required property var modelData
                                width: tableColumn.width
                                Rectangle { width: parent.width; height: 1; color: Theme.border }
                                RowLayout {
                                    width: parent.width
                                    spacing: 0
                                    Repeater {
                                        model: bodyRow.modelData
                                        delegate: Item {
                                            id: bodyCell
                                            required property string modelData
                                            required property int index
                                            Layout.fillWidth: true
                                            Layout.preferredWidth: 1
                                            Layout.alignment: Qt.AlignTop
                                            implicitHeight: cellText.implicitHeight + 20
                                            TextEdit {
                                                id: cellText
                                                x: 12
                                                y: 10
                                                width: parent.width - 24
                                                text: bodyCell.modelData
                                                textFormat: TextEdit.RichText
                                                readOnly: true
                                                selectByMouse: true
                                                wrapMode: TextEdit.Wrap
                                                horizontalAlignment: blk.block.aligns[bodyCell.index] === "right" ? Text.AlignRight : blk.block.aligns[bodyCell.index] === "center" ? Text.AlignHCenter : Text.AlignLeft
                                                font.pixelSize: 14
                                                color: Theme.text
                                                onLinkActivated: link => view.linkActivated(link)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // ---- a rule ----
            Component {
                id: hrComp
                Rectangle { width: parent.width; height: 1; color: Theme.border }
            }
        }
    }
}
