import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.Master.Backend

// The page of the AI Q&A tab and of the Agentic AI tab: the conversations on the left, the conversation with the message box in the middle and a side panel on
// the right. The AI behind it is chosen in the settings of the page (a local model, an API, or Claude Code / Codex); until one is ready the page says why.
Item {
    id: root

    property string mode: "ask"                       // ask | agent
    readonly property bool agent: mode === "agent"
    readonly property var ch: agent ? Ai.agent : Ai.ask          // the AiChannel of this page
    readonly property bool ready: ch.notReady === ""
    signal tabPicked(string key)

    function openSettings() { settings.open() }
    // diagnostics only: show the settings or a menu (for a screenshot)
    function showDiagnostic(part) {
        if (part === "settings")
            settings.open()
        else if (part === "model")
            modelMenu.popup(modelChip, 0, -modelMenu.implicitHeight - 6)
        else if (part === "mode")
            modeMenu.popup(modeChip, 0, -modeMenu.implicitHeight - 6)
        else if (part === "effort")
            effortMenu.popup(effortChip, 0, -effortMenu.implicitHeight - 6)
    }
    function statusText(status) { return Tr.s(status === "thinking" ? "ai_thinking" : status === "starting" ? "ai_starting" : status === "waiting" ? "ai_waiting" : status === "working" ? "ai_working" : "ai_writing") }
    readonly property string modelLabel: Ai.localModelName === "" ? Tr.s("ai_model_none") : Ai.localModelName.replace(/\.gguf$/i, "")
    function why(code) { return Tr.s("why_" + code) }
    // the text of an error of a reply: what happened, in words, plus what the service said
    function errorText(code, detail) {
        let text
        if (Tr.s("err_" + code) !== "err_" + code)
            text = Tr.s("err_" + code)
        else if (code.indexOf("http") === 0)
            text = Tr.s("err_http").replace("%1", code.substring(4))
        else
            text = Tr.s("err_failed")
        if (detail && detail !== "")
            text += "\n" + detail
        return text
    }
    function send() {
        const e = ch.send(input.text)
        if (e === "")
            input.clear()
    }

    FolderDialog {
        id: folderDialog
        title: Tr.s("ai_folder_choose")
        onAccepted: root.ch.setFolder(selectedFolder.toString())
    }
    AiSettingsDialog { id: settings; channel: root.ch; agent: root.agent }

    // the model of this PC to use
    MMenu {
        id: modelMenu
        implicitWidth: 340
        Repeater {
            model: Ai.installedModels
            delegate: MMenuItem {
                required property var modelData
                objectName: "modelItem"
                text: modelData.name.replace(/\.gguf$/i, "") + "   (" + modelData.sizeText + ")"
                checkable: true
                checked: Ai.localModel === modelData.path
                onTriggered: Ai.selectLocalModel(modelData.path)
            }
        }
        MenuSeparator { padding: 4; contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
        MMenuItem { objectName: "modelManage"; text: Tr.s("ai_model_manage"); onTriggered: settings.open() }
    }
    // what the agent may do with the files (the mode)
    MMenu {
        id: modeMenu
        implicitWidth: 520
        Repeater {
            model: ["read", "plan", "ask", "edit"]
            delegate: MMenuItem {
                required property string modelData
                objectName: "mode_" + modelData
                text: Tr.s("mode_" + modelData) + "  -  " + Tr.s("mode_" + modelData + "_hint")
                checkable: true
                checked: root.ch.permission === modelData
                onTriggered: root.ch.setPermission(modelData)
            }
        }
    }
    // how hard it tries
    MMenu {
        id: effortMenu
        implicitWidth: 520
        Repeater {
            model: ["low", "medium", "high"]
            delegate: MMenuItem {
                required property string modelData
                objectName: "effort_" + modelData
                text: Tr.s("effort_" + modelData) + "  -  " + Tr.s("effort_" + modelData + "_hint")
                checkable: true
                checked: root.ch.effort === modelData
                onTriggered: root.ch.setEffort(modelData)
            }
        }
    }

    // small parts
    component Chip: Rectangle {
        id: chip
        property alias text: chipText.text
        property bool clickable: false
        signal clicked()
        implicitHeight: 28
        implicitWidth: chipText.implicitWidth + 22
        radius: 14
        color: clickable && chipMouse.containsMouse ? Theme.surfaceHover : "transparent"
        border.width: 1
        border.color: Theme.border
        Text { id: chipText; anchors.centerIn: parent; font.pixelSize: 12; font.weight: Font.Medium; color: Theme.textSecondary }
        MouseArea { id: chipMouse; anchors.fill: parent; enabled: chip.clickable; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: chip.clicked() }
    }
    component SideTitle: Text {
        font.pixelSize: 12
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.6
        color: Theme.textSecondary
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- slim header: the tabs --------------------------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 64
            color: "#17130E"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
            TabSwitcher {
                anchors.centerIn: parent
                current: root.mode
                prefix: "aiTab_"
                onPicked: key => root.tabPicked(key)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- left: conversations / tasks -------------------------------------------------------------------------------------
            Rectangle {
                id: side
                objectName: "aiSidebar"
                Layout.preferredWidth: root.width < 900 ? 210 : 270
                Layout.fillHeight: true
                color: "#0D0E11"
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.borderSubtle }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    MButton {
                        objectName: "aiNew"
                        Layout.fillWidth: true
                        variant: "primary"
                        text: "+  " + Tr.s(root.agent ? "ai_new_agent" : "ai_new_ask")
                        onClicked: { root.ch.newConversation(); input.forceActiveFocus() }
                    }
                    SideTitle { text: Tr.s(root.agent ? "ai_list_agent" : "ai_list_ask") }
                    Text {
                        visible: root.ch.conversations.length === 0
                        Layout.fillWidth: true
                        text: Tr.s("ai_list_empty")
                        font.pixelSize: 13
                        color: Theme.textDisabled
                        wrapMode: Text.Wrap
                    }
                    ListView {
                        id: convList
                        objectName: "aiConversations"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        model: root.ch.conversations
                        delegate: Rectangle {
                            id: row
                            required property var modelData
                            readonly property bool current: modelData.id === root.ch.currentId
                            width: convList.width
                            height: 38
                            radius: 9
                            color: current ? Qt.alpha(Theme.accent, 0.16) : rowMouse.containsMouse ? Theme.surfaceHover : "transparent"
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 12
                                anchors.right: removeText.left
                                anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.modelData.title
                                font.pixelSize: 13
                                color: row.current ? Theme.text : Theme.textSecondary
                                elide: Text.ElideRight
                            }
                            Text {
                                id: removeText
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                visible: rowMouse.containsMouse
                                text: "✕"
                                font.pixelSize: 12
                                color: removeMouse.containsMouse ? Theme.error : Theme.textDisabled
                                MouseArea { id: removeMouse; anchors.fill: parent; anchors.margins: -8; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.ch.remove(row.modelData.id) }
                            }
                            MouseArea { id: rowMouse; anchors.fill: parent; z: -1; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.ch.select(row.modelData.id) }
                        }
                    }
                    RowLayout {
                        spacing: 8
                        Rectangle { width: 8; height: 8; radius: 4; color: root.ready ? Theme.success : Theme.textDisabled }
                        Text {
                            objectName: "aiStatus"
                            Layout.fillWidth: true
                            text: root.ready ? root.modelLabel : Tr.s("ai_not_ready")
                            font.pixelSize: 13
                            color: Theme.textSecondary
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            // ---- middle: the conversation --------------------------------------------------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // title row
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 28
                    Layout.rightMargin: 28
                    Layout.topMargin: 14
                    spacing: 10
                    Text { text: Tr.s(root.agent ? "ai_new_agent" : "ai_new_ask"); font.pixelSize: 15; font.weight: Font.DemiBold; color: Theme.text }
                    Chip { text: root.modelLabel }
                    Item { Layout.fillWidth: true }
                    MButton { objectName: "aiSettings"; compact: true; text: Tr.s("ai_settings"); onClicked: settings.open() }
                }

                // the conversation (or the welcome when it is empty)
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ListView {
                        id: list
                        objectName: "aiMessages"
                        anchors.fill: parent
                        anchors.leftMargin: 28
                        anchors.rightMargin: 28
                        anchors.topMargin: 12
                        visible: root.ch.messages.length > 0
                        clip: true
                        spacing: 20
                        model: root.ch.messages
                        property bool follow: true
                        onMovementEnded: follow = atYEnd
                        onModelChanged: if (follow) Qt.callLater(positionViewAtEnd)
                        onCountChanged: { follow = true; Qt.callLater(positionViewAtEnd) }
                        ScrollBar.vertical: ScrollBar {}
                        delegate: ColumnLayout {
                            id: msg
                            required property var modelData
                            readonly property bool isUser: modelData.role === "user"
                            width: list.width - 14
                            spacing: 6

                            // the question
                            Rectangle {
                                visible: msg.isUser
                                Layout.alignment: Qt.AlignRight
                                Layout.maximumWidth: msg.width * 0.82
                                implicitWidth: Math.min(userText.implicitWidth + 28, msg.width * 0.82)
                                implicitHeight: userText.implicitHeight + 20
                                radius: 14
                                color: Qt.alpha(Theme.accent, 0.16)
                                TextEdit {
                                    id: userText
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    text: msg.isUser ? msg.modelData.text : ""
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: TextEdit.Wrap
                                    font.pixelSize: 14
                                    color: Theme.text
                                }
                            }

                            // the answer: what the agent did, the text, how it ended
                            Repeater {
                                model: msg.isUser ? [] : msg.modelData.activity
                                delegate: Text {
                                    required property string modelData
                                    Layout.fillWidth: true
                                    text: "›  " + modelData
                                    font.pixelSize: 12
                                    color: Theme.textSecondary
                                    elide: Text.ElideRight
                                }
                            }
                            TextEdit {
                                visible: !msg.isUser && msg.modelData.text !== ""
                                Layout.fillWidth: true
                                text: msg.isUser ? "" : msg.modelData.text
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextEdit.Wrap
                                textFormat: TextEdit.MarkdownText
                                font.pixelSize: 14
                                color: Theme.text
                                onLinkActivated: link => { if (link.indexOf("https://") === 0) Qt.openUrlExternally(link) }
                            }
                            Text {
                                visible: !msg.isUser && msg.modelData.streaming && msg.modelData.text === ""
                                text: root.statusText(root.ch.status)
                                font.pixelSize: 13
                                color: Theme.textSecondary
                            }
                            Text {
                                visible: !msg.isUser && msg.modelData.stopped
                                text: Tr.s("ai_stopped")
                                font.pixelSize: 12
                                font.italic: true
                                color: Theme.textDisabled
                            }
                            Rectangle {
                                objectName: "aiApproval"
                                visible: !msg.isUser && msg.modelData.approval && msg.modelData.approval.path !== undefined
                                Layout.fillWidth: true
                                implicitHeight: approvalColumn.implicitHeight + 24
                                radius: 12
                                color: Qt.alpha(Theme.warning, 0.1)
                                border.width: 1
                                border.color: Qt.alpha(Theme.warning, 0.5)
                                ColumnLayout {
                                    id: approvalColumn
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 8
                                    Text {
                                        Layout.fillWidth: true
                                        text: msg.isUser || !msg.modelData.approval || msg.modelData.approval.path === undefined ? ""
                                              : Tr.s(msg.modelData.approval.replaces ? "appr_replace" : "appr_new").replace("%1", msg.modelData.approval.path).replace("%2", Math.round(msg.modelData.approval.bytes))
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        color: Theme.text
                                        wrapMode: Text.Wrap
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: Math.min(previewText.implicitHeight + 16, 180)
                                        radius: 8
                                        color: Theme.background
                                        clip: true
                                        Text {
                                            id: previewText
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            text: msg.isUser || !msg.modelData.approval || msg.modelData.approval.preview === undefined ? "" : msg.modelData.approval.preview
                                            font.family: "Consolas"
                                            font.pixelSize: 12
                                            color: Theme.textSecondary
                                            wrapMode: Text.WrapAnywhere
                                        }
                                    }
                                    Text { visible: msg.modelData.approval && msg.modelData.approval.replaces === true; Layout.fillWidth: true; text: Tr.s("appr_backup"); font.pixelSize: 12; color: Theme.textSecondary; wrapMode: Text.Wrap }
                                    RowLayout {
                                        spacing: 8
                                        MButton { objectName: "approvalAllow"; variant: "primary"; text: Tr.s("appr_allow"); onClicked: root.ch.answerApproval(true) }
                                        MButton { objectName: "approvalDeny"; text: Tr.s("appr_deny"); onClicked: root.ch.answerApproval(false) }
                                    }
                                }
                            }
                            Rectangle {
                                visible: !msg.isUser && msg.modelData.errorCode !== ""
                                Layout.fillWidth: true
                                implicitHeight: errText.implicitHeight + 20
                                radius: 10
                                color: Qt.alpha(Theme.error, 0.1)
                                border.width: 1
                                border.color: Qt.alpha(Theme.error, 0.4)
                                Text {
                                    id: errText
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    text: msg.isUser || msg.modelData.errorCode === "" ? "" : root.errorText(msg.modelData.errorCode, msg.modelData.errorDetail)
                                    font.pixelSize: 13
                                    color: Theme.text
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }

                    // welcome: what this page is for, and a few examples that only fill the message box
                    ColumnLayout {
                        visible: root.ch.messages.length === 0
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 56, 640)
                        spacing: 14
                        Item {
                            Layout.alignment: Qt.AlignHCenter
                            width: 44; height: 44
                            Rectangle { anchors.centerIn: parent; width: 26; height: 26; radius: 6; rotation: 45; color: Qt.alpha(Theme.accent, 0.18); border.width: 2; border.color: Theme.accent }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Tr.s(root.agent ? "ai_title_agent" : "ai_title_ask")
                            font.pixelSize: 26
                            font.weight: Font.Bold
                            color: Theme.text
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Tr.s(root.agent ? "ai_sub_agent" : "ai_sub_ask")
                            font.pixelSize: 14
                            color: Theme.textSecondary
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.topMargin: 8
                            text: Tr.s("ai_examples")
                            font.pixelSize: 12
                            color: Theme.textDisabled
                        }
                        Flow {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 8
                            Repeater {
                                model: [1, 2, 3]
                                delegate: AbstractButton {
                                    id: example
                                    required property int modelData
                                    objectName: "aiExample" + modelData
                                    hoverEnabled: true
                                    implicitHeight: 34
                                    implicitWidth: exampleText.implicitWidth + 28
                                    onClicked: { input.text = exampleText.text; input.forceActiveFocus() }
                                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                                    contentItem: Text {
                                        id: exampleText
                                        text: Tr.s((root.agent ? "ai_sa" : "ai_sq") + example.modelData)
                                        font.pixelSize: 13
                                        color: example.hovered ? Theme.text : Theme.textSecondary
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    background: Rectangle { radius: 17; color: example.hovered ? Theme.surfaceHover : Theme.surface; border.width: 1; border.color: Theme.border }
                                }
                            }
                        }
                    }
                }

                // no AI yet: said plainly above the message box, with the way out
                Rectangle {
                    objectName: "aiBanner"
                    visible: !root.ready
                    Layout.fillWidth: true
                    Layout.leftMargin: 28
                    Layout.rightMargin: 28
                    implicitHeight: bannerRow.implicitHeight + 20
                    radius: 10
                    color: Qt.alpha(Theme.warning, 0.1)
                    border.width: 1
                    border.color: Qt.alpha(Theme.warning, 0.35)
                    RowLayout {
                        id: bannerRow
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 12
                        Text {
                            Layout.fillWidth: true
                            text: root.why(root.ch.notReady)
                            font.pixelSize: 13
                            color: Theme.text
                            wrapMode: Text.Wrap
                        }
                        MButton { compact: true; text: Tr.s("ai_settings"); onClicked: settings.open() }
                    }
                }

                // message box
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 28
                    Layout.rightMargin: 28
                    Layout.topMargin: 10
                    Layout.bottomMargin: 22
                    implicitHeight: 132
                    radius: 18
                    color: Theme.backgroundAlt
                    border.width: input.activeFocus ? 2 : 1
                    border.color: input.activeFocus ? Theme.accent : Theme.border
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            TextArea {
                                id: input
                                objectName: "aiInput"
                                placeholderText: Tr.s(root.agent ? "ai_ph_agent" : "ai_ph_ask")
                                placeholderTextColor: Theme.textDisabled
                                color: Theme.text
                                font.pixelSize: 14
                                wrapMode: TextArea.Wrap
                                selectByMouse: true
                                background: null
                                leftPadding: 6
                                topPadding: 4
                                Keys.onPressed: event => {
                                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                                        event.accepted = true
                                        if (!root.ch.busy)
                                            root.send()
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Chip {
                                objectName: "aiFolder"
                                visible: root.agent
                                clickable: true
                                enabled: !root.ch.busy
                                text: root.ch.folder === "" ? Tr.s("ai_folder_none") : root.ch.folder.replace(/^.*[\\/]/, "")
                                onClicked: folderDialog.open()
                            }
                            Chip {
                                id: modelChip
                                objectName: "aiModel"
                                clickable: true
                                enabled: !root.ch.busy
                                text: root.modelLabel + "  ▾"
                                onClicked: modelMenu.popup(modelChip, 0, -modelMenu.implicitHeight - 6)
                            }
                            Chip {
                                id: effortChip
                                objectName: "aiEffort"
                                clickable: true
                                enabled: !root.ch.busy
                                text: Tr.s("effort_label") + ": " + Tr.s("effort_" + root.ch.effort) + "  ▾"
                                onClicked: effortMenu.popup(effortChip, 0, -effortMenu.implicitHeight - 6)
                            }
                            Chip {
                                objectName: "aiWeb"
                                clickable: true
                                enabled: !root.ch.busy
                                text: Tr.s(Ai.webEnabled ? "ai_web_on" : "ai_web_off")
                                onClicked: Ai.webEnabled = !Ai.webEnabled
                            }
                            Chip {
                                id: modeChip
                                objectName: "aiMode"
                                visible: root.agent
                                clickable: true
                                enabled: !root.ch.busy
                                text: Tr.s("mode_label") + ": " + Tr.s("mode_" + root.ch.permission) + "  ▾"
                                onClicked: modeMenu.popup(modeChip, 0, -modeMenu.implicitHeight - 6)
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.ch.busy ? root.statusText(root.ch.status) : ""
                                font.pixelSize: 12
                                color: Theme.accent
                                elide: Text.ElideRight
                            }
                            MButton {
                                objectName: "aiSend"
                                compact: true
                                variant: "primary"
                                enabled: root.ch.busy || (root.ready && input.text.trim() !== "")
                                text: root.ch.busy ? Tr.s("ai_stop") : Tr.s("ai_send")
                                onClicked: root.ch.busy ? root.ch.stop() : root.send()
                            }
                        }
                    }
                }
            }

            // ---- right: what this page knows about the work (sources, or plan and changes); hidden when the window is narrow ---------------
            Rectangle {
                id: panel
                objectName: "aiPanel"
                visible: root.width >= 1000
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                color: "#0D0E11"
                Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.borderSubtle }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 10
                    SideTitle { text: Tr.s(root.agent ? "ai_side_folder" : "ai_side_model") }
                    Text {
                        Layout.fillWidth: true
                        text: root.agent ? (root.ch.folder === "" ? Tr.s("ai_folder_none") : root.ch.folder)
                                         : root.modelLabel
                        font.pixelSize: 13
                        color: Theme.text
                        wrapMode: Text.WrapAnywhere
                    }
                    SideTitle { Layout.topMargin: 14; text: Tr.s("ai_side_privacy") }
                    Text {
                        Layout.fillWidth: true
                        text: Tr.s(Ai.webEnabled ? "privacy_local_web" : "privacy_local")
                        font.pixelSize: 13
                        color: Theme.textSecondary
                        wrapMode: Text.Wrap
                    }
                    SideTitle { visible: root.agent; Layout.topMargin: 14; text: Tr.s("ai_side_mode") }
                    Text {
                        visible: root.agent
                        Layout.fillWidth: true
                        text: Tr.s("ai_mode_" + root.ch.permission)
                        font.pixelSize: 13
                        color: Theme.textSecondary
                        wrapMode: Text.Wrap
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
