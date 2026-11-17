import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// The AI assistant of the open document: ask questions, summarise. It runs a language model on this computer (nothing is uploaded) and only reads the file.
// A card that floats at the bottom of the page area: only the question bar while the conversation is empty; it grows (up to half of the
// page area) as the conversation fills and then scrolls with the mouse wheel.
Rectangle {
    id: root

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 20
    property bool collapsed: false           // "−" makes the card a small pill so it does not cover the document; "+" brings it back as it was
    width: collapsed ? miniRow.implicitWidth + 20 : Math.min(760, parent.width - 48)
    height: collapsed ? miniRow.implicitHeight + 16 : column.implicitHeight + 20
    radius: 16
    color: Theme.backgroundAlt
    border.width: 1
    border.color: Theme.border
    visible: Doc.isOpen
    readonly property bool hasChat: Assistant.messages.length > 0
    readonly property real chatMax: Math.max(140, parent.height * 0.5)
    property real chatTarget: hasChat ? Math.min(chatMax, chat.contentHeight + 4) : 0
    property real chatH: chatTarget
    Behavior on chatH { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

    signal pageRequested(int page, real y)
    signal ocrRequested()

    readonly property var dl: Assistant.downloader
    readonly property bool ready: Assistant.state === "idle" || Assistant.state === "starting" || Assistant.state === "busy"
    readonly property bool canAsk: Doc.isOpen && !Assistant.busy && Assistant.engineInstalled && Assistant.modelInstalled

    function statusText() {
        const st = Assistant.status
        if (st === "starting") return Tr.s("aiStarting")
        if (st === "reading") return Tr.s("aiReading")
        if (st === "waiting") return Tr.s("aiWaiting")
        if (st === "writing") return Tr.s("aiWriting")
        if (st === "part") return Tr.s("aiPart").replace("%1", Assistant.statusPart).replace("%2", Assistant.statusParts)
        if (st === "noDocument") return Tr.s("aiNoDocument")
        if (st === "noText") return Tr.s("aiNoText")
        if (st === "noEngine") return Tr.s("aiNoEngine")
        if (st === "noModel") return Tr.s("aiNoModel")
        if (st === "startFailed") return Tr.s("aiStartFailed") + (Assistant.detail !== "" ? "\n" + Assistant.detail : "")
        if (st === "failed") return Tr.s("aiFailed") + (Assistant.detail !== "" ? "\n" + Assistant.detail : "")
        return ""
    }
    readonly property bool statusIsError: ["noDocument", "noText", "noEngine", "noModel", "startFailed", "failed"].indexOf(Assistant.status) >= 0

    function send() {
        const text = input.text.trim()
        if (text === "" || !root.canAsk) return
        input.text = ""
        Assistant.ask(text)
    }

    TextEdit { id: clipboardHelper; visible: false }
    function copyText(text) { clipboardHelper.text = text; clipboardHelper.selectAll(); clipboardHelper.copy() }

    // ---- made small: the AI mark and "+" ---------------------------------------------------------------------------------------
    RowLayout {
        id: miniRow
        objectName: "aiMini"
        anchors.centerIn: parent
        visible: root.collapsed
        spacing: 6
        LineIcon { name: "ai"; size: 18; color: Theme.textSecondary }
        PButton {
            objectName: "aiExpand"
            compact: true
            variant: "ghost"
            text: "+"
            tip: Tr.s("aiExpand")
            onClicked: root.collapsed = false
        }
    }

    ColumnLayout {
        id: column
        visible: !root.collapsed
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        spacing: 8

        // ---- the model is not there yet --------------------------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            visible: !Assistant.engineInstalled || !Assistant.modelInstalled
            implicitHeight: setupColumn.implicitHeight + 20
            radius: 10
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
            ColumnLayout {
                id: setupColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 10
                spacing: 8
                AppText {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSm
                    text: !Assistant.engineInstalled ? Tr.s("aiNoEngine") : Tr.s("aiNeedModel").replace("%1", root.dl.sizeText)
                }
                Rectangle {
                    Layout.fillWidth: true
                    visible: root.dl.busy
                    height: 6
                    radius: 3
                    color: Theme.surfaceHover
                    Rectangle { width: parent.width * root.dl.progress; height: parent.height; radius: 3; color: Theme.accent }
                }
                AppText {
                    Layout.fillWidth: true
                    visible: root.dl.code !== "idle" && root.dl.code !== "installed"
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSm
                    color: root.dl.code === "failed" ? Theme.error : Theme.textSecondary
                    text: (root.dl.code === "starting" ? Tr.s("aiDlStarting") : root.dl.code === "downloading" ? Tr.s("aiDlDownloading") + "  " + Math.round(root.dl.progress * 100) + "%"
                           : root.dl.code === "checking" ? Tr.s("aiDlChecking") : root.dl.code === "cancelled" ? Tr.s("aiDlCancelled") : Tr.s("aiDlFailed"))
                          + (root.dl.code === "failed" && root.dl.message !== "" ? "\n" + root.dl.message : "")
                }
                RowLayout {
                    visible: Assistant.engineInstalled
                    spacing: 8
                    PButton {
                        objectName: "aiDownload"
                        visible: !root.dl.busy
                        variant: "primary"
                        text: Tr.s("aiDownload")
                        onClicked: root.dl.download()
                    }
                    PButton { visible: root.dl.busy; variant: "ghost"; text: Tr.s("aiCancel"); onClicked: root.dl.cancel() }
                    Item { Layout.fillWidth: true }
                }
                AppText {
                    Layout.fillWidth: true
                    visible: Assistant.engineInstalled && !root.dl.busy
                    wrapMode: Text.WrapAnywhere
                    font.pixelSize: Theme.fontSm
                    color: Theme.textDisabled
                    text: Tr.s("aiModelFolder").replace("%1", Assistant.modelFolder)
                }
            }
        }

        // ---- the conversation (grows with its content, then scrolls) -----------------------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.chatH
            visible: root.chatH > 0.5
            ListView {
                id: chat
                objectName: "aiMessages"
                anchors.fill: parent
                clip: true
                spacing: 10
                model: Assistant.messages
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                onCountChanged: Qt.callLater(function () { chat.positionViewAtEnd() })
                Connections { target: Assistant; function onMessagesChanged() { if (chat.atYEnd || chat.contentHeight - chat.contentY < chat.height + 80) Qt.callLater(function () { chat.positionViewAtEnd() }) } }
                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index
                    readonly property bool mine: modelData.role === "user"
                    width: chat.width
                    height: bubble.height
                    Rectangle {
                        id: bubble
                        x: row.mine ? row.width - width - 4 : 0
                        width: Math.min(row.width - 12, Math.max(80, content.implicitWidth + 24))
                        height: content.implicitHeight + 20
                        radius: 10
                        color: row.mine ? Qt.alpha(Theme.accent, 0.25) : Theme.surface
                        border.width: row.mine ? 0 : 1
                        border.color: row.modelData.error ? Theme.error : Theme.border
                        ColumnLayout {
                            id: content
                            x: 12
                            y: 10
                            width: row.width - 12 - 24
                            spacing: 6
                            AppText {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                font.pixelSize: Theme.fontSm
                                text: row.modelData.text === "@summary" ? Tr.s("aiSummaryAsk") : (row.modelData.text === "" && row.modelData.streaming ? "…" : row.modelData.text)
                                color: Theme.text
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 4
                                visible: !row.mine && row.modelData.pages.length > 0
                                Repeater {
                                    model: row.modelData.pages
                                    delegate: PButton {
                                        required property int modelData
                                        compact: true
                                        implicitHeight: 24
                                        labelPixelSize: Theme.fontSm
                                        variant: "ghost"
                                        text: Tr.f("pageN", modelData)
                                        onClicked: root.pageRequested(modelData - 1, 0)
                                    }
                                }
                            }
                            RowLayout {
                                visible: !row.mine && !row.modelData.streaming && row.modelData.text !== ""
                                PButton {
                                    compact: true
                                    implicitHeight: 24
                                    labelPixelSize: Theme.fontSm
                                    variant: "ghost"
                                    text: Tr.s("aiCopy")
                                    onClicked: root.copyText(row.modelData.text)
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                }
            }
        }

        // ---- status ----------------------------------------------------------------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            visible: statusLine.text !== ""
            spacing: 8
            AppText {
                id: statusLine
                objectName: "aiStatus"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSm
                color: root.statusIsError ? Theme.error : Theme.textSecondary
                text: root.statusText()
            }
            PButton {
                visible: Assistant.status === "noText"
                compact: true
                implicitHeight: 28
                labelPixelSize: Theme.fontSm
                variant: "secondary"
                text: Tr.s("aiOpenOcr")
                onClicked: root.ocrRequested()
            }
        }
        AppText {
            Layout.fillWidth: true
            visible: Assistant.status === "" && Assistant.state === "idle" && Assistant.modelInstalled && Assistant.engineInstalled && Assistant.messages.length > 0
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSm
            color: Theme.textDisabled
            text: Tr.s("aiCheckAnswers")
        }

        // ---- the question ----------------------------------------------------------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            PButton {
                objectName: "aiSummarize"
                compact: true
                variant: "secondary"
                text: Tr.s("aiSummarize")
                enabled: root.canAsk
                onClicked: Assistant.summarize()
            }
            PTextField {
                id: input
                objectName: "aiInput"
                Layout.fillWidth: true
                placeholderText: Tr.s("aiAskPlaceholder")
                enabled: Doc.isOpen && Assistant.engineInstalled && Assistant.modelInstalled
                onAccepted: root.send()
                onActiveFocusChanged: if (activeFocus && Assistant.state === "idle") Assistant.prepare()      // warm the model up when the user starts to type
            }
            PButton {
                objectName: "aiSend"
                visible: !Assistant.busy
                variant: "primary"
                text: Tr.s("aiSend")
                enabled: root.canAsk && input.text.trim() !== ""
                onClicked: root.send()
            }
            PButton {
                objectName: "aiStop"
                visible: Assistant.busy
                variant: "secondary"
                text: Tr.s("aiStop")
                onClicked: Assistant.stop()
            }
            PButton {
                objectName: "aiClear"
                compact: true
                variant: "ghost"
                glyph: "close"
                tip: Tr.s("aiClear")
                visible: Assistant.messages.length > 0 || Assistant.busy
                onClicked: Assistant.clear()
            }
            PButton {
                objectName: "aiCollapse"
                compact: true
                variant: "ghost"
                text: "−"
                tip: Tr.s("aiCollapse")
                onClicked: root.collapsed = true
            }
        }
    }
}
