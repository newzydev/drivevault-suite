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
        else if (part === "side-hidden")                                       // the list of conversations hidden
            sideOpen = false
        else if (part === "browser-wide") {                                    // the browser expanded over the conversation
            openBrowser()
            browserWide = true
        } else if (part === "tasks") {                                           // two real harmless commands, to look at the list of background tasks
            ch.startDiagnosticTasks()
            tasksOpen = true
        } else if (part === "conversation" && ch.conversations.length > 0)       // open the newest saved conversation (to look at its step list and plan)
            ch.select(ch.conversations[0].id)
    }
    function statusText(status) { return Tr.s(status === "thinking" ? "ai_thinking" : status === "starting" ? "ai_starting" : status === "waiting" ? "ai_waiting" : status === "working" ? "ai_working" : status === "compacting" ? "ai_compacting" : "ai_writing") }
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
        const e = ch.send(input.text, root.attachments)
        if (e === "") {
            input.clear()
            root.attachments = []
        }
    }
    // ---- attachments of the message box: text files (any page) and pictures (agent page, when the model can see) ------------------------------
    property bool sideOpen: true                            // the list of conversations at the left (a button in the header shows / hides it)
    property bool browserWide: false                        // the browser is expanded over the conversation
    property bool browserOpen: false                        // the browser is shown at the right (instead of the panel)
    property bool browserEver: false                        // it was opened once: the view stays alive (the AI uses it)
    onBrowserOpenChanged: if (browserOpen) browserEver = true
    readonly property bool browserShown: root.agent && root.browserOpen
    function openBrowser() { if (Ai.browser.compiledIn) browserOpen = true }
    Connections { target: Ai.browser; function onViewWanted() { root.openBrowser() } }      // the AI opens a page of its work: the user sees it
    property var pinnedManual: undefined                    // the user opened / closed the pinned step box by hand (undefined = automatic: open while it works)
    Connections { target: root.ch; function onBusyChanged() { if (root.ch.busy) root.pinnedManual = undefined } }
    property var attachments: []                         // [{kind: file | image, name, text | data}] waiting for Send
    property string attachNote: ""                          // why the last attachment was refused
    readonly property bool canAttachImages: root.agent && Ai.vision.installed
    function addAttachment(r) {
        if (!r.ok) {
            attachNote = Tr.s("att_err_" + r.error)
            return
        }
        if (r.kind === "image" && !root.agent) {
            attachNote = Tr.s("att_err_agentOnly")
            return
        }
        if (attachments.length >= 6) {
            attachNote = Tr.s("att_err_many")
            return
        }
        attachNote = ""
        const a = attachments.slice()
        a.push(r)
        attachments = a
    }
    function removeAttachment(i) {
        const a = attachments.slice()
        a.splice(i, 1)
        attachments = a
    }
    function pasteImage() {                                 // Ctrl+V with a picture on the clipboard
        if (!Ai.clipboardHasImage())
            return false
        if (!root.agent) {
            attachNote = Tr.s("att_err_agentOnly")
            return true
        }
        addAttachment(Ai.clipboardImage())
        return true
    }
    // ---- time: a clock that runs while the AI works, and the time a finished answer took ------------------------------------------------
    property real nowMs: Date.now()
    Timer { interval: 500; repeat: true; running: root.ch.busy || root.ch.tasksRunning; onTriggered: root.nowMs = Date.now() }
    property bool tasksOpen: false                          // the list of finished background tasks is open
    // a short time for a task: 38s, 2m 05s, 1h 03m
    function fmtShort(ms) {
        const s = Math.max(0, Math.round(ms / 1000)), h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), r = s % 60
        const two = n => (n < 10 ? "0" : "") + n
        return h > 0 ? h + "h " + two(m) + "m" : m > 0 ? m + "m " + two(r) + "s" : r + "s"
    }
    function taskTitle(t) {
        if (t.kind === "compact")
            return Tr.s("task_compact")
        if (t.kind === "download")
            return Tr.s("task_download").replace("%1", t.title)
        return t.title + (t.background === true && t.state === "running" ? "   · " + Tr.s("task_bg") : "") + (t.state === "error" && t.note ? "   · " + t.note : "")
    }
    function fmtDuration(ms) {
        const s = Math.max(0, Math.round(ms / 1000)), h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), r = s % 60
        const two = n => (n < 10 ? "0" : "") + n
        return h > 0 ? h + ":" + two(m) + ":" + two(r) : m + ":" + two(r)
    }
    readonly property real runStartedMs: { const m = root.ch.messages; return root.ch.busy && m.length > 0 ? m[m.length - 1].startedMs : 0 }
    readonly property string runClock: runStartedMs > 0 ? fmtDuration(nowMs - runStartedMs) : ""
    readonly property int todosDone: root.ch.todos.filter(t => t.status === "completed").length

    // line icons for the panels, drawn here (no icon font needed): monitor | folder | branch | globe | terminal | chevronDown | chevronRight
    component Icon: Canvas {
        id: ic
        property string kind: "folder"
        property color color: Theme.textSecondary
        implicitWidth: 16
        implicitHeight: 16
        onColorChanged: requestPaint()
        onKindChanged: requestPaint()
        Component.onCompleted: requestPaint()
        onPaint: {
            const c = getContext("2d")
            c.reset()
            c.strokeStyle = ic.color
            c.fillStyle = ic.color
            c.lineWidth = 1.5
            c.lineCap = "round"
            c.lineJoin = "round"
            c.beginPath()
            if (kind === "monitor") {
                c.rect(2, 3, 12, 8); c.moveTo(8, 11); c.lineTo(8, 13.5); c.moveTo(5.5, 13.5); c.lineTo(10.5, 13.5)
            } else if (kind === "folder") {
                c.moveTo(2, 4.5); c.lineTo(6.2, 4.5); c.lineTo(7.6, 6); c.lineTo(14, 6); c.lineTo(14, 12.5); c.lineTo(2, 12.5); c.closePath()
            } else if (kind === "branch") {
                c.arc(4.5, 3.8, 1.6, 0, Math.PI * 2); c.moveTo(4.5, 5.4); c.lineTo(4.5, 10.6)
                c.moveTo(6.1, 12.2); c.arc(4.5, 12.2, 1.6, 0, Math.PI * 2)
                c.moveTo(12.6, 6.2); c.arc(11, 6.2, 1.6, 0, Math.PI * 2)
                c.moveTo(11, 7.8); c.quadraticCurveTo(11, 10.2, 4.8, 10.6)
            } else if (kind === "globe") {
                c.arc(8, 8, 6, 0, Math.PI * 2); c.moveTo(2, 8); c.lineTo(14, 8)
                c.ellipse(5.2, 2, 5.6, 12)
            } else if (kind === "terminal") {
                c.rect(1.5, 2.5, 13, 11); c.moveTo(4.5, 6); c.lineTo(7, 8.2); c.lineTo(4.5, 10.4); c.moveTo(8.5, 10.6); c.lineTo(11.5, 10.6)
            } else if (kind === "compress") {
                c.moveTo(8, 1.5); c.lineTo(8, 6); c.moveTo(5.5, 4); c.lineTo(8, 6.5); c.lineTo(10.5, 4)
                c.moveTo(8, 14.5); c.lineTo(8, 10); c.moveTo(5.5, 12); c.lineTo(8, 9.5); c.lineTo(10.5, 12)
                c.moveTo(3, 8); c.lineTo(13, 8)
            } else if (kind === "chevronDown") {
                c.moveTo(4, 6); c.lineTo(8, 10); c.lineTo(12, 6)
            } else if (kind === "chevronRight") {
                c.moveTo(6, 4); c.lineTo(10, 8); c.lineTo(6, 12)
            }
            c.stroke()
        }
    }
    // a row of the right panel: an icon, a text, and a small detail at the end (the whole row can be clickable)
    component PanelRow: RowLayout {
        id: pr
        property string icon: ""
        property string text: ""
        property string detail: ""
        property bool bold: false
        property bool clickable: false
        property color detailColor: Theme.textSecondary
        signal clicked()
        Layout.fillWidth: true
        spacing: 12
        Item {
            Layout.preferredWidth: 22
            Layout.preferredHeight: 18
            Icon { anchors.centerIn: parent; kind: pr.icon; color: pr.clickable && rowHover.hovered ? Theme.accent : Theme.textSecondary }
        }
        Text {
            Layout.fillWidth: true
            text: pr.text
            font.pixelSize: 13
            font.weight: pr.bold ? Font.DemiBold : Font.Normal
            color: pr.clickable && rowHover.hovered ? Theme.accent : Theme.text
            elide: Text.ElideRight
            maximumLineCount: 1
        }
        Text {
            visible: pr.detail !== ""
            Layout.maximumWidth: 150
            text: pr.detail
            font.pixelSize: 12
            color: pr.detailColor
            elide: Text.ElideLeft
            maximumLineCount: 1
        }
        TapHandler { enabled: pr.clickable; onTapped: pr.clicked() }
        HoverHandler { id: rowHover; cursorShape: pr.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor }
    }
    // a switch
    component Toggle: Rectangle {
        id: tg
        property bool on: false
        signal toggled()
        implicitWidth: 34
        implicitHeight: 20
        radius: 10
        color: on ? Theme.accent : Theme.border
        Behavior on color { ColorAnimation { duration: 140 } }
        Rectangle {
            y: 3
            x: tg.on ? 17 : 3
            width: 14
            height: 14
            radius: 7
            color: "white"
            Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        }
        MouseArea { anchors.fill: parent; enabled: tg.enabled; cursorShape: Qt.PointingHandCursor; onClicked: tg.toggled() }
    }
    function folderName(path) { return path.replace(/[\\/]+$/, "").split(/[\\/]/).pop() || path }
    function folderParent(path) {
        const p = path.replace(/[\\/]+$/, "")
        const i = Math.max(p.lastIndexOf("/"), p.lastIndexOf("\\"))
        return i > 0 ? p.substring(0, i).replace(/\//g, "\\") : ""
    }
    // the git line of the panel: the branch (and how far ahead / behind), or why there is none
    function gitLine(g) {
        if (g.state === "ok")
            return g.branch + (g.detached ? " (HEAD)" : "") + (g.ahead > 0 ? "  ↑" + g.ahead : "") + (g.behind > 0 ? "  ↓" + g.behind : "")
        if (g.state === "notRepo")
            return Tr.s("git_not_repo")
        if (g.state === "noGit")
            return Tr.s("git_no_git")
        if (g.state === "error")
            return Tr.s("git_error")
        return ""
    }

    // a small ring that turns: this step (or the AI) is working
    component Spinner: Item {
        id: sp
        property int size: 14
        property color color: Theme.accent
        implicitWidth: size
        implicitHeight: size
        Canvas {
            anchors.fill: parent
            onPaint: {
                const c = getContext("2d")
                c.reset()
                c.lineWidth = 2
                c.lineCap = "round"
                c.strokeStyle = sp.color
                c.beginPath()
                c.arc(width / 2, height / 2, width / 2 - 2, 0, Math.PI * 1.5)
                c.stroke()
            }
            Component.onCompleted: requestPaint()
            onWidthChanged: requestPaint()
            RotationAnimator on rotation { from: 0; to: 360; duration: 900; loops: Animation.Infinite; running: sp.visible }
        }
    }

    // the line of the step list for a step: what it does, in the language of the page
    function stepText(kind, target) {
        if (kind === "note") {                           // the texts of the agent's own notes are English in the program: shown in the language of the page
            const notes = [["Made room", "note_room"], ["The answer was too long", "note_cut"], ["The conversation was too long", "note_ctx"], ["The answer was long", "note_long"],
                           ["The model sent nothing", "note_silent"], ["The task list is not finished", "note_todo"],
                           ["Checking its own work", "note_review"]]
            for (const n of notes)
                if (target.indexOf(n[0]) === 0)
                    return Tr.s(n[1])
            if (target.indexOf("Project instructions: ") === 0)
                return Tr.s("note_project").replace("%1", target.substring(22))
            return target
        }
        const key = "step_" + kind
        const t = Tr.s(key)
        if (t === key)
            return target
        return t.replace("%1", target)
    }

    // what the AI is doing right now, with the clock: the live line of the answer (reading the prompt, thinking, writing a file with how much is written) or,
    // before there is one, the status of the page
    function liveLine(m) {
        const live = m.live
        let text = root.statusText(root.ch.status)
        if (live && live.phase !== undefined) {
            const n = Number(live.chars).toLocaleString(Qt.locale("en_US"), "f", 0)
            if (live.phase === "waiting")
                text = Tr.s("live_waiting")
            else if (live.phase === "thinking")
                text = Tr.s("live_thinking").replace("%1", n)
            else if (live.phase === "writing")
                text = Tr.s("live_writing").replace("%1", n)
            else if (live.phase === "tool") {
                if (live.tool === "write_file" || live.tool === "append_file" || live.tool === "edit_file")
                    text = Tr.s(live.target !== "" ? "live_file" : "live_file_unknown").replace("%1", live.target).replace("%2", n)
                else if (live.tool === "run_command")
                    text = Tr.s("live_command")
                else
                    text = Tr.s("live_tool").replace("%1", live.tool)
            }
        }
        return text + (m.startedMs > 0 ? "   " + root.fmtDuration(root.nowMs - m.startedMs) : "")
    }
    // the same as stepText as rich text: the file / command / query of the step in a code font (everything is escaped)
    function stepHtml(kind, target) {
        const esc = s => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        if (kind === "note")
            return esc(stepText(kind, target))
        const shown = target.length > 90 ? target.substring(0, 89) + "…" : target
        const mono = "<span style='font-family:Consolas; color:" + Theme.textSecondary + "'>" + esc(shown) + "</span>"
        const key = "step_" + kind
        const t = Tr.s(key)
        if (t === key)
            return mono
        const parts = t.split("%1")
        return esc(parts[0]) + (parts.length > 1 ? mono + esc(parts.slice(1).join("%1")) : "")
    }

    // three dots that breathe one after the other: "the AI is working"
    component Dots: Row {
        spacing: 4
        Repeater {
            model: 3
            delegate: Rectangle {
                required property int index
                width: 6; height: 6; radius: 3
                color: Theme.accent
                opacity: 0.25
                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    running: true
                    PauseAnimation { duration: index * 170 }
                    NumberAnimation { to: 1; duration: 320; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 0.25; duration: 320; easing.type: Easing.InOutSine }
                    PauseAnimation { duration: (2 - index) * 170 }
                }
            }
        }
    }

    // the diff of an edit_file: lines "- old" in red, "+ new" in green (the text is escaped)
    function diffHtml(text) {
        const esc = s => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        return "<div style='white-space:pre-wrap'>" + text.split("\n").map(line => {
            const color = line.indexOf("- ") === 0 ? Theme.error : line.indexOf("+ ") === 0 ? Theme.success : Theme.textSecondary
            return "<span style='color:" + color + "'>" + esc(line) + "</span>"
        }).join("<br/>") + "</div>"
    }
    // Shift+Tab: the next mode of the agent (ask first, accept edits, plan, read only)
    function nextMode() {
        const order = ["ask", "edit", "plan", "read"]
        ch.setPermission(order[(order.indexOf(ch.permission) + 1) % order.length])
    }

    FolderDialog {
        id: folderDialog
        title: Tr.s("ai_folder_choose")
        onAccepted: root.ch.setFolder(selectedFolder.toString())
    }
    AiSettingsDialog { id: settings; channel: root.ch; agent: root.agent }
    FileDialog {
        id: attachDialog
        title: Tr.s("att_choose")
        fileMode: FileDialog.OpenFiles
        nameFilters: root.canAttachImages ? [Tr.s("att_filter_all") + " (*)", Tr.s("att_filter_pictures") + " (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"]
                                          : [Tr.s("att_filter_text") + " (*)"]
        onAccepted: { for (const u of selectedFiles) root.addAttachment(Ai.readAttachment(u.toString())) }
    }
    // the "+" of the message box: attach a file or a picture
    MMenu {
        id: attachMenu
        implicitWidth: 300
        MMenuItem { objectName: "attachFile"; text: Tr.s("att_file"); onTriggered: attachDialog.open() }
        MMenuItem {
            objectName: "attachImage"
            text: Tr.s("att_image") + (root.canAttachImages ? "" : "  (" + Tr.s(root.agent ? "att_need_vision" : "att_agent_only") + ")")
            enabled: root.canAttachImages
            onTriggered: attachDialog.open()
        }
    }

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
        Layout.maximumWidth: implicitWidth                     // in a row that is too narrow (the browser is open) a chip gets shorter and its text is cut with ...
        Layout.minimumWidth: 56
        Layout.fillWidth: true
        radius: 14
        color: clickable && chipMouse.containsMouse ? Theme.surfaceHover : "transparent"
        Behavior on color { ColorAnimation { duration: 140 } }
        border.width: 1
        border.color: Theme.border
        Text { id: chipText; anchors.centerIn: parent; width: Math.min(implicitWidth, chip.width - 16); elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 12; font.weight: Font.Medium; color: Theme.textSecondary }
        MouseArea { id: chipMouse; anchors.fill: parent; enabled: chip.clickable; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: chip.clicked() }
    }
    component SideTitle: Text {
        font.pixelSize: 12
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.6
        color: Theme.textSecondary
    }

        component StepsPanel: Rectangle {
            id: stepsBox
            property var message: null                  // the answer whose steps are shown (a map of the message model)
            property bool open: true
            property bool hidden: false
            property int maxHeight: 280
            signal toggle()
            // the notes about a cut answer ("too long", "long") are housekeeping: they are not shown, the file just goes on
            readonly property var steps: !message || !message.stepList ? [] : message.stepList.filter(s => !(s.kind === "note"
                                             && (s.target.indexOf("The answer was too long") === 0 || s.target.indexOf("The answer was long") === 0)))
            readonly property int total: steps.length
            readonly property int finished: steps.filter(s => s.state === "done" || s.state === "error").length
            readonly property bool working: !!message && message.streaming === true
            visible: total > 0 && !hidden
            Layout.fillWidth: true
            implicitHeight: stepsColumn.implicitHeight + 24
            radius: 12
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
            ColumnLayout {
                id: stepsColumn
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                // header: a click opens / closes the list (a finished one is closed by default)
                RowLayout {
                    spacing: 10
                    Item {
                        implicitWidth: 18
                        implicitHeight: 18
                        Spinner { visible: stepsBox.working; anchors.centerIn: parent; size: 14 }
                        Text { visible: !stepsBox.working; anchors.centerIn: parent; text: "✓"; font.pixelSize: 15; font.bold: true; color: Theme.success }
                    }
                    Text { text: Tr.s(stepsBox.working ? "steps_title" : "steps_title_done"); font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text }
                    Text { text: Tr.s("steps_count").replace("%1", stepsBox.finished).replace("%2", stepsBox.total); font.pixelSize: 12; color: Theme.textSecondary }
                    Item { Layout.fillWidth: true }
                    Icon { kind: stepsBox.open ? "chevronDown" : "chevronRight" }
                    TapHandler {
                        onTapped: stepsBox.toggle()
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
                Flickable {
                    id: stepFlick
                    visible: stepsBox.open
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(stepRows.implicitHeight, stepsBox.maxHeight)
                    contentHeight: stepRows.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    ScrollBar.vertical: ScrollBar {}
                    onContentHeightChanged: if (stepsBox.working) contentY = Math.max(0, contentHeight - height)      // follows the newest step
                    ColumnLayout {
                        id: stepRows
                        width: stepFlick.width - 10
                        spacing: 6
                        Repeater {
                            model: stepsBox.open ? stepsBox.steps : []
                            delegate: RowLayout {
                                id: stepRow
                                required property var modelData
                                readonly property bool isNote: modelData.kind === "note"
                                Layout.fillWidth: true
                                spacing: 10
                                Item {
                                    Layout.alignment: Qt.AlignTop
                                    implicitWidth: 18
                                    implicitHeight: 18
                                    Spinner { visible: stepRow.modelData.state === "running"; anchors.centerIn: parent; size: 14 }
                                    Text { visible: stepRow.modelData.state === "done"; anchors.centerIn: parent; text: "✓"; font.pixelSize: 15; font.bold: true; color: Theme.success }
                                    Text { visible: stepRow.modelData.state === "error"; anchors.centerIn: parent; text: "✗"; font.pixelSize: 14; font.bold: true; color: Theme.error }
                                    Text { visible: stepRow.modelData.state === "waiting"; anchors.centerIn: parent; text: "●"; font.pixelSize: 13; color: Theme.warning }
                                    Text { visible: stepRow.modelData.state === "stopped"; anchors.centerIn: parent; text: "–"; font.pixelSize: 15; color: Theme.textDisabled }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    textFormat: Text.RichText                    // the verb in the text font, the file / command in a code font
                                    text: root.stepHtml(stepRow.modelData.kind, stepRow.modelData.target)
                                    font.pixelSize: 13
                                    font.italic: stepRow.isNote
                                    color: stepRow.modelData.state === "error" ? Theme.error : stepRow.isNote || stepRow.modelData.state === "stopped" ? Theme.textDisabled : Theme.text
                                    wrapMode: Text.NoWrap
                                    clip: true
                                }
                                Text {
                                    Layout.maximumWidth: stepFlick.width * 0.4
                                    visible: text !== ""
                                    text: stepRow.modelData.state === "waiting" ? Tr.s("steps_waiting")
                                          : (stepRow.modelData.state === "error" || stepRow.modelData.kind === "command" || stepRow.modelData.kind === "edit" || stepRow.modelData.kind === "write") && stepRow.modelData.state !== "running" ? stepRow.modelData.note : ""
                                    font.pixelSize: 12
                                    color: stepRow.modelData.state === "waiting" ? Theme.warning : stepRow.modelData.state === "error" ? Theme.error : Theme.textDisabled
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }
                            }
                        }
                    }
                }
                // what it does between the steps: thinking, writing the answer
                RowLayout {
                    visible: stepsBox.working && root.ch.status !== "waiting"
                    spacing: 10
                    Spinner { size: 14 }
                    Text { text: root.liveLine(stepsBox.message); font.pixelSize: 13; color: Theme.accent }
                }
            }
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
                visible: root.sideOpen
                Layout.preferredWidth: root.width < 900 ? 210 : 270            // the same width whether the browser is open or not
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
                        property var collapsed: ({})                // folder -> its group is closed
                        // agent page: the conversations are grouped by the folder they work in; a header per folder with a "+" for a new conversation there
                        section.property: "folder"
                        section.criteria: ViewSection.FullString
                        section.delegate: Item {
                            id: groupHeader
                            required property string section
                            readonly property bool closed: convList.collapsed[section] === true
                            readonly property string name: section === "" ? Tr.s("group_none") : section.replace(/[\\/]+$/, "").split(/[\\/]/).pop() || section
                            width: convList.width
                            height: root.agent ? 36 : 0
                            visible: root.agent
                            Rectangle {
                                anchors.fill: parent
                                anchors.topMargin: 2
                                radius: 9
                                color: groupMouse.containsMouse ? Theme.surfaceHover : "transparent"
                                Behavior on color { ColorAnimation { duration: 140 } }
                            }
                            MouseArea {
                                id: groupMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    const m = Object.assign({}, convList.collapsed)
                                    m[groupHeader.section] = !groupHeader.closed
                                    convList.collapsed = m
                                }
                                ToolTip.visible: containsMouse && groupHeader.section !== ""
                                ToolTip.text: groupHeader.section
                                ToolTip.delay: 600
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 6
                                spacing: 8
                                Icon { kind: groupHeader.closed ? "chevronRight" : "chevronDown"; Layout.preferredWidth: 14; Layout.preferredHeight: 14 }
                                Icon { kind: "folder"; color: Theme.accent }
                                Text {
                                    Layout.fillWidth: true
                                    text: groupHeader.name
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    color: Theme.text
                                    elide: Text.ElideRight
                                }
                                Rectangle {                          // "+": a new conversation in this folder
                                    objectName: "aiGroupNew"
                                    visible: groupHeader.section !== ""
                                    Layout.preferredWidth: 24
                                    Layout.preferredHeight: 24
                                    radius: 7
                                    color: plusMouse.containsMouse ? Qt.alpha(Theme.accent, 0.22) : "transparent"
                                    border.width: plusMouse.containsMouse ? 1 : 0
                                    border.color: Qt.alpha(Theme.accent, 0.6)
                                    Behavior on color { ColorAnimation { duration: 120 } }
                                    Text { anchors.centerIn: parent; text: "+"; font.pixelSize: 17; font.weight: Font.DemiBold; color: plusMouse.containsMouse ? Theme.accent : Theme.textSecondary }
                                    MouseArea {
                                        id: plusMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: { root.ch.newConversationIn(groupHeader.section); input.forceActiveFocus() }
                                        ToolTip.visible: containsMouse
                                        ToolTip.text: Tr.s("group_new")
                                        ToolTip.delay: 400
                                    }
                                }
                            }
                        }
                        delegate: Rectangle {
                            id: row
                            required property var modelData
                            readonly property bool current: modelData.id === root.ch.currentId
                            readonly property bool hidden: root.agent && convList.collapsed[modelData.folder] === true && !current
                            width: convList.width
                            height: hidden ? 0 : 38
                            visible: !hidden
                            Behavior on height { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                            radius: 9
                            color: current ? Qt.alpha(Theme.accent, 0.16) : rowMouse.containsMouse ? Theme.surfaceHover : "transparent"
                            Behavior on color { ColorAnimation { duration: 140 } }
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: root.agent ? 30 : 12                 // under its group header
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
                visible: !(root.browserShown && root.browserWide)
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
                    MButton {                                 // shows / hides the list of conversations at the left
                        objectName: "aiSideToggle"
                        variant: "ghost"
                        compact: true
                        text: "☰"
                        font.pixelSize: 16
                        onClicked: root.sideOpen = !root.sideOpen
                        Accessible.name: Tr.s(root.sideOpen ? "side_hide" : "side_show")
                    }
                    Text { text: Tr.s(root.agent ? "ai_new_agent" : "ai_new_ask"); font.pixelSize: 15; font.weight: Font.DemiBold; color: Theme.text }
                    Chip { text: root.modelLabel }
                    Chip {                                    // the clock of the running job, fades in and out
                        objectName: "aiClock"
                        text: "⏱  " + root.runClock
                        opacity: root.ch.busy && root.runClock !== "" ? 1 : 0
                        visible: opacity > 0
                        Behavior on opacity { NumberAnimation { duration: 250 } }
                    }
                    Item { Layout.fillWidth: true }
                    MButton {                                 // the browser belongs to the Agentic AI page: the AI opens its web pages there, the user can browse too
                        objectName: "aiBrowser"
                        visible: root.agent && Ai.browser.compiledIn
                        compact: true
                        text: Tr.s("tab_browser")
                        onClicked: root.browserOpen = !root.browserOpen
                    }
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
                        model: root.ch.messageModel                 // a real list model: only the row that changed is updated, the boxes are not rebuilt (no flicker)
                        // The list follows the newest text while it is written - until the user scrolls up (then it stays where they are), and again when they
                        // scroll back to the end or send a new message.
                        property bool follow: true
                        property bool programmatic: false
                        function toEnd() {
                            programmatic = true
                            positionViewAtEnd()
                            programmatic = false
                        }
                        onContentYChanged: if (!programmatic) follow = atYEnd
                        onContentHeightChanged: if (follow) Qt.callLater(toEnd)
                        property string cid: root.ch.currentId
                        onCidChanged: { expandedMap = ({}); seen = 0 }
                        property var expandedMap: ({})             // answer index -> its step list is open (a click on the header changes it)
                        property int seen: 0                      // messages that already had their entrance (only a new one animates)
                        onCountChanged: { follow = true; Qt.callLater(toEnd) }
                        ScrollBar.vertical: ScrollBar {}
                        delegate: ColumnLayout {
                            id: msg
                            required property var modelData
                            required property int index
                            readonly property bool isUser: modelData.role === "user"
                            width: list.width - 14
                            spacing: 6
                            // a new message fades in and slides up; an old one that is only rebuilt stays as it is
                            transform: Translate { id: msgShift }
                            Component.onCompleted: {
                                if (index >= list.seen) {
                                    list.seen = index + 1
                                    enter.start()
                                }
                            }
                            ParallelAnimation {
                                id: enter
                                NumberAnimation { target: msg; property: "opacity"; from: 0; to: 1; duration: 280; easing.type: Easing.OutCubic }
                                NumberAnimation { target: msgShift; property: "y"; from: 16; to: 0; duration: 320; easing.type: Easing.OutCubic }
                            }

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
                            Text {                                                  // what the user attached to the question
                                visible: msg.isUser && msg.modelData.attachNames !== undefined && msg.modelData.attachNames.length > 0
                                Layout.alignment: Qt.AlignRight
                                Layout.maximumWidth: msg.width * 0.82
                                elide: Text.ElideRight
                                text: msg.isUser && msg.modelData.attachNames !== undefined ? "📎 " + msg.modelData.attachNames.join(",  ") : ""
                                font.pixelSize: 12
                                color: Theme.textSecondary
                            }

                            // ---- the plan the AI keeps (todo_write): a checklist, a tick in front of what is done; on the newest answer ----
                            Rectangle {
                                id: planBox
                                objectName: "aiPlanBox"
                                visible: root.agent && !msg.isUser && msg.index === list.count - 1 && root.ch.todos.length > 0
                                Layout.fillWidth: true
                                implicitHeight: planColumn.implicitHeight + 24
                                radius: 12
                                color: Theme.surface
                                border.width: 1
                                border.color: root.todosDone === root.ch.todos.length ? Qt.alpha(Theme.success, 0.5) : Theme.border
                                Behavior on border.color { ColorAnimation { duration: 300 } }
                                ColumnLayout {
                                    id: planColumn
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 8
                                    RowLayout {
                                        spacing: 10
                                        Text { text: Tr.s("plan_title"); font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text }
                                        Text { text: root.todosDone + " / " + root.ch.todos.length; font.pixelSize: 12; color: Theme.textSecondary }
                                        Rectangle {
                                            Layout.fillWidth: true
                                            height: 4
                                            radius: 2
                                            color: Theme.border
                                            Rectangle {
                                                height: parent.height
                                                radius: parent.radius
                                                color: root.todosDone === root.ch.todos.length ? Theme.success : Theme.accent
                                                width: root.ch.todos.length > 0 ? parent.width * root.todosDone / root.ch.todos.length : 0
                                                Behavior on width { NumberAnimation { duration: 450; easing.type: Easing.OutCubic } }
                                            }
                                        }
                                    }
                                    Repeater {
                                        model: planBox.visible ? root.ch.todos : []
                                        delegate: RowLayout {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            spacing: 10
                                            Item {
                                                Layout.alignment: Qt.AlignTop
                                                implicitWidth: 18
                                                implicitHeight: 18
                                                Text { visible: modelData.status === "completed"; anchors.centerIn: parent; text: "✓"; font.pixelSize: 15; font.bold: true; color: Theme.success }
                                                Spinner { visible: modelData.status === "in_progress" && root.ch.busy; anchors.centerIn: parent; size: 14 }
                                                Text { visible: modelData.status === "in_progress" && !root.ch.busy; anchors.centerIn: parent; text: "●"; font.pixelSize: 13; color: Theme.accent }
                                                Rectangle { visible: modelData.status === "pending"; anchors.centerIn: parent; width: 12; height: 12; radius: 6; color: "transparent"; border.width: 1.5; border.color: Theme.textDisabled }
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: modelData.content
                                                font.pixelSize: 13
                                                font.strikeout: modelData.status === "completed"
                                                color: modelData.status === "completed" ? Theme.textDisabled : modelData.status === "in_progress" ? Theme.text : Theme.textSecondary
                                                wrapMode: Text.Wrap
                                            }
                                        }
                                    }
                                }
                            }

                            // ---- what the AI does: one line per step (reading, creating, editing, running ...) - spinner while it runs, a tick when it is done ----
                            StepsPanel {
                                id: stepsBox
                                objectName: "aiStepsBox"
                                message: msg.isUser ? null : msg.modelData
                                hidden: msg.index === list.count - 1                    // the newest answer's steps are in the box above the message box
                                open: list.expandedMap[msg.index] === true              // older answers: only the header (a click opens it)
                                Layout.fillWidth: true
                                onToggle: {
                                    const m = Object.assign({}, list.expandedMap)
                                    m[msg.index] = !stepsBox.open
                                    list.expandedMap = m
                                }
                            }
                            // ---- "the conversation was compacted": the model's summary replaces the old messages from here on (a click shows it) ----
                            Rectangle {
                                id: compactCard
                                objectName: "aiCompactCard"
                                readonly property bool isCompact: !msg.isUser && msg.modelData.compact === true
                                readonly property bool done: isCompact && msg.modelData.summary !== ""
                                readonly property bool open: list.expandedMap["c" + msg.index] === true
                                visible: isCompact
                                Layout.fillWidth: true
                                implicitHeight: compactColumn.implicitHeight + 24
                                radius: 12
                                color: Theme.surface
                                border.width: 1
                                border.color: done ? Qt.alpha(Theme.success, 0.45) : Theme.border
                                ColumnLayout {
                                    id: compactColumn
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 8
                                    RowLayout {
                                        spacing: 10
                                        Item {
                                            implicitWidth: 18
                                            implicitHeight: 18
                                            Spinner { visible: msg.modelData.streaming === true; anchors.centerIn: parent; size: 14 }
                                            Icon { visible: msg.modelData.streaming !== true; anchors.centerIn: parent; kind: "compress"; color: compactCard.done ? Theme.success : Theme.textDisabled }
                                        }
                                        Text {
                                            text: msg.modelData.streaming === true ? Tr.s("compact_running") : compactCard.done ? Tr.s("compact_done") : Tr.s("compact_failed")
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                            color: Theme.text
                                        }
                                        Text {
                                            visible: compactCard.done
                                            text: Tr.s("compact_stats").replace("%1", Math.round(msg.modelData.tokensBefore).toLocaleString(Qt.locale("en_US"), "f", 0)).replace("%2", Math.round(msg.modelData.tokensAfter).toLocaleString(Qt.locale("en_US"), "f", 0))
                                            font.pixelSize: 12
                                            color: Theme.textSecondary
                                        }
                                        Item { Layout.fillWidth: true }
                                        Icon { visible: compactCard.done; kind: compactCard.open ? "chevronDown" : "chevronRight" }
                                        TapHandler {
                                            enabled: compactCard.done
                                            onTapped: {
                                                const m = Object.assign({}, list.expandedMap)
                                                m["c" + msg.index] = !compactCard.open
                                                list.expandedMap = m
                                            }
                                        }
                                        HoverHandler { cursorShape: compactCard.done ? Qt.PointingHandCursor : Qt.ArrowCursor }
                                    }
                                    Text {
                                        visible: compactCard.open && compactCard.done
                                        Layout.fillWidth: true
                                        text: msg.modelData.summary
                                        font.pixelSize: 13
                                        color: Theme.textSecondary
                                        wrapMode: Text.Wrap
                                    }
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
                            RowLayout {
                                visible: !msg.isUser && msg.modelData.streaming && msg.modelData.text === "" && msg.modelData.stepList.length === 0 && msg.modelData.compact !== true
                                spacing: 10
                                Dots {}
                                Text {
                                    text: root.liveLine(msg.modelData)
                                    font.pixelSize: 13
                                    color: Theme.textSecondary
                                }
                            }
                            // how long it took: live while it works, final when it is done
                            RowLayout {
                                objectName: "aiTimeLine"
                                visible: !msg.isUser && (msg.modelData.streaming ? msg.modelData.startedMs > 0 && msg.modelData.text !== "" : msg.modelData.durationMs > 0)
                                spacing: 8
                                Dots { visible: msg.modelData.streaming }
                                Text {
                                    text: (msg.modelData.streaming ? Tr.s("ai_time_running").replace("%1", root.fmtDuration(root.nowMs - msg.modelData.startedMs))
                                                                   : Tr.s("ai_time_used").replace("%1", root.fmtDuration(msg.modelData.durationMs)))
                                          + (msg.modelData.steps > 0 ? "   ·   " + Tr.s("ai_steps").replace("%1", msg.modelData.steps) : "")
                                    font.pixelSize: 12
                                    color: Theme.textDisabled
                                }
                            }
                            Text {
                                visible: !msg.isUser && msg.modelData.stopped
                                text: Tr.s("ai_stopped")
                                font.pixelSize: 12
                                font.italic: true
                                color: Theme.textDisabled
                            }
                            // something waits for the user: a file change (with a diff for edit_file), a command, or a plan
                            Rectangle {
                                id: approvalCard
                                objectName: "aiApproval"
                                readonly property var appr: msg.isUser ? null : msg.modelData.approval
                                readonly property string kind: appr && appr.kind !== undefined ? appr.kind : ""
                                readonly property bool isDiff: kind === "change" && appr.diff === true
                                visible: kind !== ""
                                onVisibleChanged: if (visible) pop.restart()
                                Component.onCompleted: if (visible) pop.start()
                                transformOrigin: Item.Top
                                ParallelAnimation {
                                    id: pop
                                    NumberAnimation { target: approvalCard; property: "scale"; from: 0.96; to: 1; duration: 240; easing.type: Easing.OutBack }
                                    NumberAnimation { target: approvalCard; property: "opacity"; from: 0; to: 1; duration: 200 }
                                }
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
                                        text: approvalCard.kind === "change"
                                              ? Tr.s(approvalCard.appr.op === "append" ? "appr_append" : approvalCard.isDiff ? "appr_edit" : approvalCard.appr.replaces ? "appr_replace" : "appr_new").replace("%1", approvalCard.appr.path).replace("%2", Math.round(approvalCard.appr.bytes))
                                              : approvalCard.kind === "command" ? Tr.s("appr_cmd")
                                              : approvalCard.kind === "plan" ? Tr.s("appr_plan") : ""
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        color: Theme.text
                                        wrapMode: Text.Wrap
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        implicitHeight: Math.min(previewText.implicitHeight + 16, approvalCard.kind === "plan" ? 320 : 200)
                                        radius: 8
                                        color: Theme.background
                                        clip: true
                                        Flickable {
                                            anchors.fill: parent
                                            contentHeight: previewText.implicitHeight + 16
                                            boundsBehavior: Flickable.StopAtBounds
                                            clip: true
                                            ScrollBar.vertical: ScrollBar {}
                                            Text {
                                                id: previewText
                                                x: 8
                                                y: 8
                                                width: parent.width - 16
                                                textFormat: approvalCard.isDiff ? Text.RichText : Text.PlainText
                                                text: approvalCard.kind === "change" ? (approvalCard.isDiff ? root.diffHtml(approvalCard.appr.preview) : approvalCard.appr.preview)
                                                      : approvalCard.kind === "command" ? approvalCard.appr.command
                                                      : approvalCard.kind === "plan" ? approvalCard.appr.plan : ""
                                                font.family: approvalCard.kind === "plan" ? Qt.application.font.family : "Consolas"
                                                font.pixelSize: approvalCard.kind === "plan" ? 13 : 12
                                                color: approvalCard.kind === "plan" ? Theme.text : Theme.textSecondary
                                                wrapMode: approvalCard.kind === "plan" ? Text.Wrap : Text.WrapAnywhere
                                            }
                                        }
                                    }
                                    Text { visible: approvalCard.kind === "change" && approvalCard.appr.replaces === true; Layout.fillWidth: true; text: Tr.s("appr_backup"); font.pixelSize: 12; color: Theme.textSecondary; wrapMode: Text.Wrap }
                                    Text { visible: approvalCard.kind === "command"; Layout.fillWidth: true; text: Tr.s("appr_cmd_warn"); font.pixelSize: 12; color: Theme.warning; wrapMode: Text.Wrap }
                                    RowLayout {
                                        spacing: 8
                                        MButton { objectName: "approvalAllow"; variant: "primary"; text: Tr.s(approvalCard.kind === "plan" ? "appr_plan_allow" : "appr_allow"); onClicked: root.ch.answerApproval(true) }
                                        MButton { objectName: "approvalDeny"; text: Tr.s(approvalCard.kind === "plan" ? "appr_plan_deny" : "appr_deny"); onClicked: root.ch.answerApproval(false) }
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
                        id: welcome
                        visible: root.ch.messages.length === 0
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 56, 640)
                        spacing: 14
                        onVisibleChanged: if (visible) welcomeIn.restart()
                        Component.onCompleted: if (visible) welcomeIn.start()
                        NumberAnimation { id: welcomeIn; target: welcome; property: "opacity"; from: 0; to: 1; duration: 520; easing.type: Easing.OutCubic }
                        Item {
                            Layout.alignment: Qt.AlignHCenter
                            width: 44; height: 44
                            Rectangle {
                                id: diamond
                                anchors.centerIn: parent
                                width: 26; height: 26; radius: 6; rotation: 45
                                color: Qt.alpha(Theme.accent, 0.18); border.width: 2; border.color: Theme.accent
                                SequentialAnimation on scale {                    // it breathes while the page waits for a task
                                    running: root.ch.messages.length === 0
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 1.14; duration: 1500; easing.type: Easing.InOutSine }
                                    NumberAnimation { to: 1; duration: 1500; easing.type: Easing.InOutSine }
                                }
                            }
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

                // what the AI does: pinned above the message box (the steps of the newest answer; a click on the header opens / closes it)
                StepsPanel {
                    objectName: "aiStepsPinned"
                    Layout.fillWidth: true
                    Layout.leftMargin: 28
                    Layout.rightMargin: 28
                    Layout.topMargin: 6
                    maxHeight: 220
                    message: root.ch.lastMessage
                    hidden: root.ch.lastMessage.role !== "assistant"
                    open: root.pinnedManual !== undefined ? root.pinnedManual : (root.ch.lastMessage.streaming === true)      // open while it works, closed when it is done
                    onToggle: root.pinnedManual = !open
                }

                // message box
                Rectangle {
                    id: inputBox
                    Layout.fillWidth: true
                    Layout.leftMargin: 28
                    Layout.rightMargin: 28
                    Layout.topMargin: 6
                    Layout.bottomMargin: 22
                    implicitHeight: 132 + (root.attachments.length > 0 ? 44 : 0) + (root.attachNote !== "" ? 20 : 0)
                    radius: 18
                    color: Theme.backgroundAlt
                    border.width: input.activeFocus ? 2 : 1
                    border.color: input.activeFocus ? Theme.accent : Theme.border
                    Behavior on border.color { ColorAnimation { duration: 160 } }
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6
                        // what is attached to the next message (a picture shows a small preview); ✕ takes it off
                        Flow {
                            objectName: "aiAttachments"
                            visible: root.attachments.length > 0
                            Layout.fillWidth: true
                            spacing: 8
                            Repeater {
                                model: root.attachments
                                delegate: Rectangle {
                                    id: att
                                    required property var modelData
                                    required property int index
                                    height: 36
                                    width: attRow.implicitWidth + 16
                                    radius: 10
                                    color: Theme.surface
                                    border.width: 1
                                    border.color: Theme.border
                                    Row {
                                        id: attRow
                                        anchors.centerIn: parent
                                        spacing: 8
                                        Image {
                                            visible: att.modelData.kind === "image"
                                            width: 26
                                            height: 26
                                            anchors.verticalCenter: parent.verticalCenter
                                            fillMode: Image.PreserveAspectCrop
                                            source: att.modelData.kind === "image" ? "data:image/jpeg;base64," + att.modelData.data : ""
                                        }
                                        Text { visible: att.modelData.kind !== "image"; anchors.verticalCenter: parent.verticalCenter; text: "📄"; font.pixelSize: 14 }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: Math.min(implicitWidth, 220)
                                            elide: Text.ElideMiddle
                                            text: att.modelData.name
                                            font.pixelSize: 12
                                            color: Theme.text
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: "✕"
                                            font.pixelSize: 12
                                            color: Theme.textSecondary
                                            MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor; onClicked: root.removeAttachment(att.index) }
                                        }
                                    }
                                }
                            }
                        }
                        Text { visible: root.attachNote !== ""; Layout.fillWidth: true; text: root.attachNote; font.pixelSize: 12; color: Theme.warning; elide: Text.ElideRight }
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
                                    } else if (event.key === Qt.Key_Escape && root.ch.busy) {            // like Claude Code: Esc stops the work
                                        event.accepted = true
                                        root.ch.stop()
                                    } else if (event.key === Qt.Key_Backtab && root.agent && !root.ch.busy) {   // Shift+Tab: the next mode
                                        event.accepted = true
                                        root.nextMode()
                                    } else if (event.matches(StandardKey.Paste) && root.pasteImage()) {          // Ctrl+V with a picture on the clipboard: it is attached
                                        event.accepted = true
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            // "+": attach a file or a picture (like Claude)
                            Rectangle {
                                id: plusButton
                                objectName: "aiAttach"
                                implicitWidth: 32
                                implicitHeight: 32
                                radius: 16
                                color: plusMouse.containsMouse ? Theme.surfaceHover : "transparent"
                                border.width: 1
                                border.color: Theme.border
                                Text { anchors.centerIn: parent; text: "+"; font.pixelSize: 20; color: Theme.textSecondary }
                                MouseArea {
                                    id: plusMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: attachMenu.popup(plusButton, 0, -attachMenu.implicitHeight - 6)
                                }
                                Accessible.role: Accessible.Button
                                Accessible.name: Tr.s("att_add")
                            }
                            Text {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: root.ch.busy ? root.statusText(root.ch.status) + (root.runClock !== "" ? "   ⏱ " + root.runClock : "") : ""
                                font.pixelSize: 12
                                color: Theme.accent
                                elide: Text.ElideRight
                            }
                            // the settings of this message, next to Send (like Claude): folder, model, effort, web, mode
                            Chip {
                                objectName: "aiFolder"
                                visible: root.agent && !root.browserShown            // (the browser is open: no room; the folder is in the left list)
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
                            MButton {
                                objectName: "aiSend"
                                compact: true
                                variant: "primary"
                                enabled: root.ch.busy || (root.ready && (input.text.trim() !== "" || root.attachments.length > 0))
                                text: root.ch.busy ? Tr.s("ai_stop") : Tr.s("ai_send")
                                onClicked: root.ch.busy ? root.ch.stop() : root.send()
                            }
                        }
                    }
                }
            }

            // ---- right: the browser (Qt WebEngine) takes the place of the panel when it is open and pushes the conversation aside; created when first opened, then kept ----
            Loader {
                id: browserLoader
                objectName: "browserLoader"
                visible: root.browserShown
                active: Ai.browser.compiledIn && root.browserEver
                Layout.preferredWidth: Math.max(440, Math.round(root.width * 0.4))
                Layout.fillWidth: root.browserWide                     // expanded: it takes the place of the conversation too
                Layout.fillHeight: true
                sourceComponent: BrowserView {
                    expanded: root.browserWide
                    onCloseRequested: { root.browserWide = false; root.browserOpen = false }
                    onExpandToggled: root.browserWide = !root.browserWide
                }
            }

            // ---- right: what this page knows about the work (sources, or plan and changes); hidden when the window is narrow or the browser is open ---------------
            Rectangle {
                id: panel
                objectName: "aiPanel"
                visible: root.width >= 1000 && !root.browserShown
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                color: "#0D0E11"
                Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.borderSubtle }
                Flickable {
                    id: panelScroll
                    anchors.fill: parent
                    contentHeight: panelColumn.implicitHeight + 40
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    ScrollBar.vertical: ScrollBar {}
                    ColumnLayout {
                        id: panelColumn
                        x: 20
                        y: 20
                        width: panelScroll.width - 40
                        spacing: 10

                        // ---- the Q&A page: the model and the privacy note, as before ----
                        SideTitle { visible: !root.agent; text: Tr.s("ai_side_model") }
                        Text { visible: !root.agent; Layout.fillWidth: true; text: root.modelLabel; font.pixelSize: 13; color: Theme.text; wrapMode: Text.WrapAnywhere }
                        SideTitle { visible: !root.agent; Layout.topMargin: 14; text: Tr.s("ai_side_privacy") }
                        Text { visible: !root.agent; Layout.fillWidth: true; text: Tr.s(Ai.webEnabled ? "privacy_local_web" : "privacy_local"); font.pixelSize: 13; color: Theme.textSecondary; wrapMode: Text.Wrap }

                        // ---- environment: where the work happens ----
                        SideTitle { visible: root.agent; text: Tr.s("env_title") }
                        PanelRow { visible: root.agent; icon: "monitor"; text: Tr.s("env_computer"); bold: true }
                        PanelRow {
                            objectName: "aiEnvFolder"
                            visible: root.agent
                            icon: "folder"
                            text: root.ch.folder === "" ? Tr.s("env_no_folder") : root.folderName(root.ch.folder)
                            detail: root.ch.folder === "" ? "" : root.folderParent(root.ch.folder)
                            clickable: !root.ch.busy
                            onClicked: folderDialog.open()
                        }
                        PanelRow {
                            objectName: "aiEnvGit"
                            visible: root.agent && root.ch.git.state !== "none"
                            icon: "branch"
                            text: root.gitLine(root.ch.git)
                            detailColor: root.ch.git.state === "ok" ? Theme.textSecondary : Theme.warning
                        }

                        // ---- context: how much of the model window this conversation uses ----
                        Rectangle { Layout.fillWidth: true; Layout.topMargin: 6; height: 1; color: Theme.borderSubtle }
                        RowLayout {
                            objectName: "aiContext"
                            Layout.fillWidth: true
                            spacing: 10
                            Text { text: Tr.s("ctx_title"); font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text }
                            Item { Layout.fillWidth: true }
                            Text { text: root.ch.context.percent + "%"; font.pixelSize: 12; color: Theme.textSecondary }
                            Rectangle {
                                Layout.preferredWidth: 46
                                Layout.preferredHeight: 6
                                radius: 3
                                color: Theme.border
                                Rectangle {
                                    height: parent.height
                                    radius: parent.radius
                                    width: parent.width * Math.min(100, root.ch.context.percent) / 100
                                    color: root.ch.context.percent >= 90 ? Theme.error : root.ch.context.percent >= 70 ? Theme.warning : Theme.accent
                                    Behavior on width { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
                                    Behavior on color { ColorAnimation { duration: 300 } }
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: (root.ch.context.exact ? "" : Tr.s("ctx_estimate") + " ") + Tr.s("ctx_tokens").replace("%1", root.ch.context.used.toLocaleString(Qt.locale("en_US"), "f", 0)).replace("%2", root.ch.context.window.toLocaleString(Qt.locale("en_US"), "f", 0))
                            font.pixelSize: 11
                            color: Theme.textDisabled
                            wrapMode: Text.Wrap
                        }
                        // what the next message carries from this conversation, and when it is compacted by itself; "Compact now" does it at once
                        Text {
                            objectName: "aiCompactInfo"
                            Layout.fillWidth: true
                            text: Tr.s("ctx_history").replace("%1", root.ch.context.history.toLocaleString(Qt.locale("en_US"), "f", 0)).replace("%2", root.ch.context.autoAt.toLocaleString(Qt.locale("en_US"), "f", 0))
                            font.pixelSize: 11
                            color: Theme.textDisabled
                            wrapMode: Text.Wrap
                        }
                        Rectangle {
                            objectName: "aiCompactNow"
                            visible: root.ch.context.canCompact
                            Layout.preferredWidth: compactNowText.implicitWidth + 24
                            Layout.preferredHeight: 28
                            radius: 14
                            color: compactNowHover.hovered ? Theme.surfaceHover : "transparent"
                            border.width: 1
                            border.color: Theme.border
                            Behavior on color { ColorAnimation { duration: 140 } }
                            Text { id: compactNowText; anchors.centerIn: parent; text: Tr.s("compact_now"); font.pixelSize: 12; font.weight: Font.Medium; color: Theme.textSecondary }
                            TapHandler { onTapped: root.ch.compactNow("") }
                            HoverHandler { id: compactNowHover; cursorShape: Qt.PointingHandCursor }
                            ToolTip.visible: compactNowHover.hovered
                            ToolTip.text: Tr.s("compact_tip")
                            ToolTip.delay: 500
                        }

                        // ---- changes: what git says changed in the folder ----
                        Rectangle { visible: root.agent; Layout.fillWidth: true; Layout.topMargin: 6; height: 1; color: Theme.borderSubtle }
                        RowLayout {
                            objectName: "aiChanges"
                            visible: root.agent
                            Layout.fillWidth: true
                            spacing: 10
                            Text { text: Tr.s("chg_title"); font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text }
                            Rectangle {
                                visible: root.ch.git.state === "ok" && root.ch.git.count > 0
                                Layout.preferredWidth: Math.max(20, countText.implicitWidth + 12)
                                Layout.preferredHeight: 18
                                radius: 9
                                color: Qt.alpha(Theme.accent, 0.2)
                                Text { id: countText; anchors.centerIn: parent; text: root.ch.git.count !== undefined ? String(root.ch.git.count) : ""; font.pixelSize: 11; font.weight: Font.DemiBold; color: Theme.accent }
                            }
                            Item { Layout.fillWidth: true }
                            Text { visible: root.ch.git.state === "ok" && root.ch.git.added > 0; text: "+" + root.ch.git.added; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.success }
                            Text { visible: root.ch.git.state === "ok" && root.ch.git.removed > 0; text: "−" + root.ch.git.removed; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.error }
                            Text {
                                text: "↻"
                                font.pixelSize: 15
                                color: refreshHover.hovered ? Theme.text : Theme.textSecondary
                                TapHandler { onTapped: root.ch.refreshGit() }
                                HoverHandler { id: refreshHover; cursorShape: Qt.PointingHandCursor }
                                ToolTip.visible: refreshHover.hovered
                                ToolTip.text: Tr.s("chg_refresh")
                                ToolTip.delay: 400
                            }
                        }
                        Text {
                            visible: root.agent && root.ch.git.state !== "ok"
                            Layout.fillWidth: true
                            text: root.gitLine(root.ch.git)
                            font.pixelSize: 12
                            color: Theme.textDisabled
                            wrapMode: Text.Wrap
                        }
                        Text { visible: root.agent && root.ch.git.state === "ok" && root.ch.git.count === 0; Layout.fillWidth: true; text: Tr.s("chg_none"); font.pixelSize: 12; color: Theme.textDisabled }
                        Repeater {
                            model: root.agent && root.ch.git.state === "ok" ? root.ch.git.files.slice(0, 12) : []
                            delegate: RowLayout {
                                id: changeRow
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 8
                                Rectangle {
                                    Layout.preferredWidth: 18
                                    Layout.preferredHeight: 18
                                    radius: 5
                                    color: Qt.alpha(changeRow.modelData.status === "D" ? Theme.error : changeRow.modelData.status === "M" || changeRow.modelData.status === "R" ? Theme.warning : changeRow.modelData.status === "U" ? Theme.error : Theme.success, 0.18)
                                    Text {
                                        anchors.centerIn: parent
                                        text: changeRow.modelData.status
                                        font.pixelSize: 11
                                        font.weight: Font.Bold
                                        color: changeRow.modelData.status === "D" || changeRow.modelData.status === "U" ? Theme.error : changeRow.modelData.status === "M" || changeRow.modelData.status === "R" ? Theme.warning : Theme.success
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: changeRow.modelData.path
                                    font.pixelSize: 12
                                    color: Theme.text
                                    elide: Text.ElideMiddle
                                    maximumLineCount: 1
                                }
                                Text { visible: changeRow.modelData.added > 0; text: "+" + changeRow.modelData.added; font.pixelSize: 11; color: Theme.success }
                                Text { visible: changeRow.modelData.removed > 0; text: "−" + changeRow.modelData.removed; font.pixelSize: 11; color: Theme.error }
                            }
                        }
                        Text {
                            visible: root.agent && root.ch.git.state === "ok" && root.ch.git.count > 12
                            Layout.fillWidth: true
                            text: Tr.s("chg_more").replace("%1", root.ch.git.count - 12)
                            font.pixelSize: 12
                            color: Theme.textDisabled
                        }

                        // ---- background tasks: commands (also the ones that run for ever), a compaction, the model download; a square stops one ----
                        Rectangle { visible: root.agent && root.ch.tasks.length > 0; Layout.fillWidth: true; Layout.topMargin: 6; height: 1; color: Theme.borderSubtle }
                        ColumnLayout {
                            objectName: "aiTasks"
                            visible: root.agent && root.ch.tasks.length > 0
                            Layout.fillWidth: true
                            spacing: 8
                            SideTitle { text: Tr.s("tasks_title") }
                            Repeater {
                                model: root.agent ? root.ch.tasks.filter(t => t.state === "running") : []
                                delegate: RowLayout {
                                    id: taskRow
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Item {
                                        Layout.preferredWidth: 22
                                        Layout.preferredHeight: 18
                                        Spinner { anchors.centerIn: parent; size: 14 }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: root.taskTitle(taskRow.modelData)
                                        font.pixelSize: 13
                                        color: Theme.text
                                        elide: Text.ElideMiddle
                                        maximumLineCount: 1
                                    }
                                    Text {
                                        text: taskRow.modelData.progress >= 0 ? Math.round(taskRow.modelData.progress * 100) + "%"
                                              : taskRow.modelData.startedMs > 0 ? root.fmtShort(root.nowMs - taskRow.modelData.startedMs) : ""
                                        font.pixelSize: 12
                                        color: Theme.textSecondary
                                    }
                                    Rectangle {                          // the square: stop this task
                                        objectName: "aiTaskStop"
                                        Layout.preferredWidth: 22
                                        Layout.preferredHeight: 22
                                        radius: 6
                                        color: stopHover.hovered ? Qt.alpha(Theme.error, 0.28) : "transparent"
                                        Behavior on color { ColorAnimation { duration: 120 } }
                                        Rectangle { anchors.centerIn: parent; width: 9; height: 9; radius: 2; color: stopHover.hovered ? Theme.error : Theme.text }
                                        TapHandler { onTapped: root.ch.stopTask(taskRow.modelData.id) }
                                        HoverHandler { id: stopHover; cursorShape: Qt.PointingHandCursor }
                                        ToolTip.visible: stopHover.hovered
                                        ToolTip.text: Tr.s("tasks_stop")
                                        ToolTip.delay: 400
                                    }
                                }
                            }
                            // the finished ones: one row with their number, a click shows them
                            RowLayout {
                                id: finishedRow
                                readonly property var finished: root.agent ? root.ch.tasks.filter(t => t.state !== "running") : []
                                visible: finished.length > 0
                                Layout.fillWidth: true
                                spacing: 10
                                Item {
                                    Layout.preferredWidth: 22
                                    Layout.preferredHeight: 18
                                    Icon { anchors.centerIn: parent; kind: root.tasksOpen ? "chevronDown" : "chevronRight" }
                                }
                                Text { Layout.fillWidth: true; text: Tr.s("tasks_finished").replace("%1", finishedRow.finished.length); font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text }
                                TapHandler { onTapped: root.tasksOpen = !root.tasksOpen }
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                            }
                            Repeater {
                                model: root.tasksOpen ? finishedRow.finished.slice(0, 12) : []
                                delegate: RowLayout {
                                    id: doneRow
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Item {
                                        Layout.preferredWidth: 22
                                        Layout.preferredHeight: 18
                                        Text { visible: doneRow.modelData.state === "done"; anchors.centerIn: parent; text: "✓"; font.pixelSize: 15; font.bold: true; color: Theme.success }
                                        Text { visible: doneRow.modelData.state === "error"; anchors.centerIn: parent; text: "✗"; font.pixelSize: 14; font.bold: true; color: Theme.error }
                                        Text { visible: doneRow.modelData.state === "stopped"; anchors.centerIn: parent; text: "–"; font.pixelSize: 15; color: Theme.textDisabled }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: root.taskTitle(doneRow.modelData)
                                        font.pixelSize: 12
                                        color: doneRow.modelData.state === "error" ? Theme.error : Theme.textSecondary
                                        elide: Text.ElideMiddle
                                        maximumLineCount: 1
                                    }
                                    Text {
                                        visible: doneRow.modelData.endedMs > 0 && doneRow.modelData.startedMs > 0
                                        text: root.fmtShort(doneRow.modelData.endedMs - doneRow.modelData.startedMs)
                                        font.pixelSize: 11
                                        color: Theme.textDisabled
                                    }
                                }
                            }
                        }

                        // ---- connectors: what the AI can reach, and what it needs your answer for ----
                        Rectangle { visible: root.agent; Layout.fillWidth: true; Layout.topMargin: 6; height: 1; color: Theme.borderSubtle }
                        SideTitle { visible: root.agent; text: Tr.s("con_title") }
                        RowLayout {
                            objectName: "aiConnWeb"
                            visible: root.agent
                            Layout.fillWidth: true
                            spacing: 12
                            Item { Layout.preferredWidth: 22; Layout.preferredHeight: 18; Icon { anchors.centerIn: parent; kind: "globe" } }
                            Text { Layout.fillWidth: true; text: Tr.s("con_web"); font.pixelSize: 13; color: Theme.text; elide: Text.ElideRight }
                            Toggle { on: Ai.webEnabled; enabled: !root.ch.busy; opacity: enabled ? 1 : 0.5; onToggled: Ai.webEnabled = !Ai.webEnabled }
                        }
                        PanelRow {
                            visible: root.agent
                            icon: "branch"
                            text: Tr.s("con_git")
                            detail: root.ch.git.state === "noGit" ? Tr.s("con_missing") : Tr.s("con_ready")
                            detailColor: root.ch.git.state === "noGit" ? Theme.warning : Theme.success
                        }
                        PanelRow {
                            visible: root.agent
                            icon: "terminal"
                            text: Tr.s("con_cmd")
                            detail: root.ch.permission === "ask" ? Tr.s("con_ask") : root.ch.permission === "edit" ? Tr.s("con_auto") : Tr.s("con_off")
                            detailColor: root.ch.permission === "ask" || root.ch.permission === "edit" ? Theme.success : Theme.textDisabled
                        }

                        // ---- what the AI may do in this mode ----
                        SideTitle { visible: root.agent; Layout.topMargin: 10; text: Tr.s("ai_side_mode") }
                        Text {
                            visible: root.agent
                            Layout.fillWidth: true
                            text: Tr.s("ai_mode_" + root.ch.permission)
                            font.pixelSize: 12
                            color: Theme.textSecondary
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
}
