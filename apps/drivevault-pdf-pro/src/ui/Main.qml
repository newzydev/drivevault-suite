import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

ApplicationWindow {
    id: win
    visible: true
    width: 1240
    height: 800
    minimumWidth: 960
    minimumHeight: 560
    flags: Qt.Window | Qt.FramelessWindowHint
    title: (Doc.isOpen ? Doc.fileName + " - " : "") + Tr.s("title")
    color: Theme.background
    palette.window: Theme.background
    palette.windowText: Theme.text
    palette.base: Theme.backgroundAlt
    palette.alternateBase: Theme.surface
    palette.text: Theme.text
    palette.placeholderText: Theme.textSecondary
    palette.button: Theme.surface
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.textOnAccent
    palette.toolTipBase: Theme.surface
    palette.toolTipText: Theme.text

    property string notice: ""
    property bool noticeIsError: true
    property bool rememberWindow: true
    property int windowedVisibility: Window.Windowed
    property rect normalGeometry
    property bool showHome: !Doc.isOpen
    property bool showTools: true
    property bool showReaderTools: true
    readonly property bool toolsVisible: showHome || !Doc.isOpen ? showTools : showReaderTools
    property string category: "all"
    property string pendingTool: ""
    property var queuedFiles: []
    property var openFailures: []
    property bool openingFiles: false
    property var closedFiles: []
    readonly property var tools: [
        { key: "createBlankPdf", icon: "page", tint: "#6094ff", group: "convert" },
        { key: "createFromImages", icon: "pages2", tint: "#4dc987", group: "convert" },
        { key: "exportImages", icon: "download", tint: "#6094ff", group: "convert" },
        { key: "toolForm", icon: "form", tint: "#b282ff", group: "sign" },
        { key: "editExisting", icon: "edit", tint: "#f472b6", group: "edit" },
        { key: "addContent", icon: "edit", tint: "#f472b6", group: "edit" },
        { key: "toolMerge", icon: "pages2", tint: "#6094ff", group: "edit" },
        { key: "toolOrganize", icon: "thumbs", tint: "#4dc987", group: "edit" },
        { key: "addComments", icon: "comment", tint: "#fbb04c", group: "edit" },
        { key: "thaiOcr", icon: "scan", tint: "#4dc987", group: "convert" },
        { key: "protectPdf", icon: "shield", tint: "#6094ff", group: "sign" },
        { key: "redactTitle", icon: "redact", tint: "#f87171", group: "edit" },
        { key: "toolCompress", icon: "download", tint: "#f87171", group: "convert" },
        { key: "toolSplit", icon: "pages2", tint: "#4dc987", group: "edit" },
        { key: "unlockPdf", icon: "shield", tint: "#5fc7de", group: "sign" },
        { key: "maintenance", icon: "outline", tint: "#5fc7de", group: "edit" },
        { key: "batchPdf", icon: "thumbs", tint: "#fbb04c", group: "convert" },
        { key: "repairPdf", icon: "rotate", tint: "#fbb04c", group: "edit" }
    ]

    function runTool(tool) {
        if (Doc.outputBusy)
            return
        if (tool === "openFile") {
            fileDialog.open()
        } else if (tool === "createBlankPdf" || tool === "createFromImages") {
            createPdfDialog.begin(tool === "createFromImages")
        } else if (tool === "toolMerge") {
            organizer.begin("merge")
        } else if (tool === "batchPdf") {
            batchDialog.open()
        } else if (tool === "repairPdf") {
            maintenanceDialog.begin(true)
        } else if (!Doc.isOpen) {
            pendingTool = tool
            fileDialog.open()
        } else {
            showHome = false
            Qt.callLater(function () {
                if (viewerLoader.item)
                    viewerLoader.item.runTool(tool)
            })
        }
    }

    function showNotice(text, isError) { noticeIsError = isError; notice = text }
    function toggleFullScreen() {
        if (visibility === Window.FullScreen) {
            visibility = windowedVisibility
        } else {
            windowedVisibility = visibility === Window.Maximized ? Window.Maximized : Window.Windowed
            visibility = Window.FullScreen
        }
    }

    function captureNormalGeometry() {
        if (visibility === Window.Windowed)
            normalGeometry = Qt.rect(x, y, width, height)
    }

    function restoreWindowState() {
        const state = Doc.windowState()
        const geometry = state.normalGeometry
        if (geometry.width > 0 && geometry.height > 0) {
            width = Math.max(minimumWidth, geometry.width)
            height = Math.max(minimumHeight, geometry.height)
            x = geometry.x
            y = geometry.y
        }
        captureNormalGeometry()
        if (state.maximized)
            showMaximized()
    }

    // Coalesce geometry signals so maximizing cannot overwrite the normal rectangle.
    onXChanged: Qt.callLater(captureNormalGeometry)
    onYChanged: Qt.callLater(captureNormalGeometry)
    onWidthChanged: Qt.callLater(captureNormalGeometry)
    onHeightChanged: Qt.callLater(captureNormalGeometry)
    onVisibilityChanged: {
        if (win.visibility === Window.Windowed || win.visibility === Window.Maximized)
            windowedVisibility = win.visibility
        Qt.callLater(captureNormalGeometry)
    }
    Component.onCompleted: {
        if (rememberWindow)
            restoreWindowState()
    }

    Connections {
        target: Doc
        function onDocumentChanged() { win.showHome = !Doc.isOpen }
        function onPrintFinished(ok, message) {
            if (ok)
                win.showNotice(Tr.s("printDone"), false)
            else if (message === "cancelled")
                win.showNotice(Tr.s("printCancelled"), false)
            else
                win.showNotice(Tr.s("printFailed") + (message !== "" ? " (" + message + ")" : ""), true)
        }
    }

    function openPath(path, password) {
        const code = Doc.open(path, password === undefined ? "" : password)
        if (code === "") {
            const tool = pendingTool
            pendingTool = ""
            passwordDialog.close()
            notice = ""
            showHome = false
            if (tool !== "")
                runTool(tool)
        } else if (code === "password") {
            passwordDialog.path = Doc.localPath(path)
            passwordDialog.wrong = password !== undefined && password !== ""
            if (!passwordDialog.opened)
                passwordDialog.open()
        } else {
            pendingTool = ""
            passwordDialog.close()
            if (openingFiles)
                openFailures.push(Doc.localPath(path).split(/[\\/]/).pop() + " — " + Tr.error(code))
            else
                showNotice(Tr.error(code), true)
            Doc.removeRecent(Doc.localPath(path))
        }
        return code
    }

    function openPaths(paths) {
        if (paths.length === 0 || openingFiles || passwordDialog.visible)
            return
        if (paths.length === 1) {
            openPath(paths[0])
            return
        }
        queuedFiles = paths.slice()
        openFailures = []
        openingFiles = true
        Qt.callLater(openNextFile)
    }

    function openNextFile() {
        if (!openingFiles || passwordDialog.visible || (viewerLoader.item && viewerLoader.item.recoveryPromptVisible))
            return
        if (queuedFiles.length === 0) {
            openingFiles = false
            if (openFailures.length > 0)
                showNotice(openFailures.join("\n"), true)
            return
        }
        if (openPath(queuedFiles.shift()) !== "password")
            Qt.callLater(openNextFile)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PdfTitleBar {
            id: titleBar
            Layout.fillWidth: true
            visible: win.visibility !== Window.FullScreen
            hostWindow: win
            onSettingsRequested: preferencesDialog.open()
            onAboutRequested: aboutDialog.open()
        }

        FileTabs {
            Layout.fillWidth: true
            homeSelected: win.showHome || !Doc.isOpen
            onHomeRequested: win.showHome = true
            onTabRequested: win.showHome = false
            onOpenRequested: win.runTool("openFile")
            onCloseTabRequested: index => viewerLoader.item.closeTabAt(index)
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 52
            color: Theme.backgroundAlt
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 5
                Repeater {
                    model: [
                        { key: "all", label: "allTools" }, { key: "edit", label: "editCategory" },
                        { key: "convert", label: "convertCategory" }, { key: "sign", label: "protectCategory" }
                    ]
                    delegate: PButton {
                        required property var modelData
                        text: Tr.s(modelData.label)
                        variant: "ghost"
                        compact: true
                        onClicked: {
                            const show = win.category !== modelData.key || !win.toolsVisible
                            if (win.showHome || !Doc.isOpen)
                                win.showTools = show
                            else
                                win.showReaderTools = show
                            win.category = modelData.key
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.width - 12
                            height: 2
                            color: Theme.accent
                            visible: win.toolsVisible && win.category === parent.modelData.key
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                PButton {
                    objectName: "findTools"
                    text: Tr.s("findTools") + "    Ctrl+K"
                    compact: true
                    onClicked: commandSearch.open()
                }
            }
        }
        Item {
            id: readerToolbarHost
            visible: viewerLoader.visible
            Layout.fillWidth: true
            Layout.preferredHeight: viewerLoader.item ? viewerLoader.item.toolbarHeight : 0
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Rectangle {
                objectName: "toolPanel"
                visible: win.toolsVisible
                Layout.preferredWidth: win.width < 1100 ? 218 : 254
                Layout.fillHeight: true
                color: Theme.backgroundAlt
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        AppText {
                            Layout.fillWidth: true
                            leftPadding: 10
                            font.weight: Font.DemiBold
                            font.pixelSize: Theme.fontLg
                            text: Tr.s(({ all: "allTools", edit: "editCategory", convert: "convertCategory",
                                        sign: "protectCategory" })[win.category])
                        }
                        PButton {
                            compact: true
                            variant: "ghost"
                            glyph: "close"
                            tip: Tr.s("dismiss")
                            onClicked: {
                                if (win.showHome || !Doc.isOpen)
                                    win.showTools = false
                                else
                                    win.showReaderTools = false
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: win.tools.filter(tool => win.category === "all" || tool.group === win.category)
                        ScrollBar.vertical: ScrollBar {}
                        delegate: ToolEntry {
                            required property var modelData
                            width: ListView.view.width
                            tool: modelData
                            enabled: !Doc.outputBusy
                            onClicked: win.runTool(tool.key)
                        }
                    }
                }
            }
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Loader {
                    id: viewerLoader
                    anchors.fill: parent
                    active: Doc.isOpen
                    visible: active && !win.showHome
                    sourceComponent: viewerComponent
                }
                Loader {
                    anchors.fill: parent
                    active: win.showHome || !Doc.isOpen
                    visible: active
                    sourceComponent: homeComponent
                }
            }
        }
    }

    // Frameless windows use the platform resize operation from these border hit areas.
    component ResizeEdge: MouseArea {
        required property int edges
        enabled: win.visibility === Window.Windowed
        hoverEnabled: true
        z: 2000
        onPressed: mouse => { mouse.accepted = win.startSystemResize(edges) }
    }
    ResizeEdge { edges: Qt.LeftEdge; cursorShape: Qt.SizeHorCursor; x: 0; y: 10; width: 5; height: win.height - 20 }
    ResizeEdge { edges: Qt.RightEdge; cursorShape: Qt.SizeHorCursor; x: win.width - 5; y: 10; width: 5; height: win.height - 20 }
    ResizeEdge { edges: Qt.TopEdge; cursorShape: Qt.SizeVerCursor; x: 10; y: 0; width: win.width - 20; height: 5 }
    ResizeEdge { edges: Qt.BottomEdge; cursorShape: Qt.SizeVerCursor; x: 10; y: win.height - 5; width: win.width - 20; height: 5 }
    ResizeEdge { edges: Qt.LeftEdge | Qt.TopEdge; cursorShape: Qt.SizeFDiagCursor; x: 0; y: 0; width: 10; height: 10 }
    ResizeEdge { edges: Qt.RightEdge | Qt.BottomEdge; cursorShape: Qt.SizeFDiagCursor; x: win.width - 10; y: win.height - 10; width: 10; height: 10 }
    ResizeEdge { edges: Qt.RightEdge | Qt.TopEdge; cursorShape: Qt.SizeBDiagCursor; x: win.width - 10; y: 0; width: 10; height: 10 }
    ResizeEdge { edges: Qt.LeftEdge | Qt.BottomEdge; cursorShape: Qt.SizeBDiagCursor; x: 0; y: win.height - 10; width: 10; height: 10 }
    property bool closeConfirmed: false
    onClosing: close => {
        if (Editor.anyModified && !closeConfirmed) {
            close.accepted = false
            windowAsk.open()
            return
        }
        if (viewerLoader.item)
            viewerLoader.item.flushSave()
        if (rememberWindow) {
            captureNormalGeometry()
            Doc.saveWindowState(windowedVisibility === Window.Maximized, normalGeometry)
        }
    }
    Component {
        id: viewerComponent
        ViewerScreen {
            toolbarHost: readerToolbarHost
            onOpenRequested: fileDialog.open()
            onOpenPathRequested: path => win.openPath(path)
            onFileClosed: path => {
                win.closedFiles = win.closedFiles.filter(file => file !== path).concat([path]).slice(-10)
            }
            onRecoveryPromptClosed: if (win.openingFiles) Qt.callLater(win.openNextFile)
            onOrganizeRequested: organizer.begin("organize")
            onSplitRequested: splitDialog.open()
            onCompressRequested: compressDialog.open()
            onFormRequested: formDialog.open()
            onImageExportRequested: page => { imageExportDialog.currentPage = page; imageExportDialog.open() }
            onSecurityRequested: unlock => { securityDialog.unlock = unlock; securityDialog.open() }
            onMaintenanceRequested: repair => maintenanceDialog.begin(repair)
            onBatchRequested: batchDialog.open()
            onOcrRequested: page => { ocrDialog.currentPage = page; ocrDialog.open() }
        }
    }
    Component {
        id: homeComponent
        HomeScreen {
            tools: ["toolOrganize", "thaiOcr", "toolForm", "editExisting", "protectPdf", "toolMerge"]
                .map(key => win.tools.find(tool => tool.key === key))
            onToolRequested: key => win.runTool(key)
            onOpenPathRequested: path => win.openPath(path)
            onFolderOpenFailed: win.showNotice(Tr.s("recentFolderFailed"), true)
        }
    }

    // an error message (a file that cannot be opened)
    Rectangle {
        objectName: "noticeBanner"
        visible: win.notice !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 104 + (titleBar.visible ? titleBar.height : 0)
        width: Math.min(560, parent.width - 32)
        height: noticeRow.implicitHeight + 20
        radius: 10
        color: Qt.alpha(win.noticeIsError ? Theme.error : Theme.success, 0.16)
        border.color: Qt.alpha(win.noticeIsError ? Theme.error : Theme.success, 0.6)
        z: 10
        RowLayout {
            id: noticeRow
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10
            AppText { Layout.fillWidth: true; text: win.notice; wrapMode: Text.Wrap }
            PButton { compact: true; variant: "ghost"; glyph: "close"; tip: Tr.s("dismiss"); onClicked: win.notice = "" }
        }
    }

    FileDialog {
        id: fileDialog
        title: Tr.s("openDialogTitle")
        nameFilters: [Tr.s("pdfFiles")]
        fileMode: win.pendingTool === "" ? FileDialog.OpenFiles : FileDialog.OpenFile
        onAccepted: win.openPaths(selectedFiles.map(url => url.toString()))
        onRejected: win.pendingTool = ""
    }
    OrganizerDialog {
        id: organizer
        onOpenResultRequested: path => win.openPath(path)
    }
    SplitDialog { id: splitDialog }
    ImageExportDialog { id: imageExportDialog }
    OcrDialog {
        id: ocrDialog
        onOpenResult: path => win.openPath(path)
    }
    MaintenanceDialog {
        id: maintenanceDialog
        onOpenResult: path => win.openPath(path)
    }
    BatchDialog { id: batchDialog }
    CreatePdfDialog {
        id: createPdfDialog
        onOpenResult: path => win.openPath(path)
    }
    SecurityDialog {
        id: securityDialog
        onOpenResultRequested: path => win.openPath(path)
    }
    FormDialog {
        id: formDialog
        onOpenResultRequested: path => win.openPath(path)
    }
    ConfirmDialog {
        id: windowAsk
        title: Tr.s("closeUnsavedTitle")
        text: Tr.s("closeUnsavedAll")
        confirmText: Tr.s("closeAnyway")
        cancelText: Tr.s("keepEditing")
        onConfirmed: { Editor.forgetRecovery(); win.closeConfirmed = true; win.close() }
    }
    CompressDialog {
        id: compressDialog
        onOpenResultRequested: path => win.openPath(path)
    }
    PasswordDialog {
        id: passwordDialog
        onSubmitted: (path, password) => win.openPath(path, password)
        onClosed: {
            win.pendingTool = ""
            if (win.openingFiles)
                Qt.callLater(win.openNextFile)
        }
    }

    ToolSearch {
        id: commandSearch
        tools: [{ key: "openFile", icon: "folder", tint: Theme.text }].concat(win.tools)
        onToolRequested: key => win.runTool(key)
    }

    // Open PDF files in their drop order.
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: drop => {
            if (!win.openingFiles && !passwordDialog.visible)
                win.openPaths(drop.urls.map(url => url.toString())
                              .filter(url => Doc.localPath(url).toLowerCase().endsWith(".pdf")))
        }
    }

    PreferencesDialog { id: preferencesDialog }
    AboutPdfDialog { id: aboutDialog }

    Shortcut { sequences: [StandardKey.Open]; onActivated: win.runTool("openFile") }
    Shortcut {
        sequences: ["Ctrl+Shift+T"]
        enabled: win.closedFiles.length > 0 && !win.openingFiles && !passwordDialog.visible
            && !(viewerLoader.item && viewerLoader.item.recoveryPromptVisible)
        onActivated: {
            const path = win.closedFiles[win.closedFiles.length - 1]
            win.closedFiles = win.closedFiles.slice(0, -1)
            win.openPath(path)
        }
    }
    Shortcut { sequences: ["Ctrl+K"]; onActivated: commandSearch.open() }
    Shortcut { sequences: ["F11"]; onActivated: win.toggleFullScreen() }
    Shortcut { sequences: ["Escape"]; enabled: win.visibility === Window.FullScreen; onActivated: win.toggleFullScreen() }
}
