import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.Backend

ApplicationWindow {
    id: window

    // ---- Window ----------------------------------------------------------------------------
    title: (Projects.isOpen ? Projects.name + (Projects.dirty ? " *" : "") + " — " : "") + "DriveVault Video Editor Pro"     // shown in the task bar
    flags: Qt.Window | Qt.FramelessWindowHint        // our own title bar (TitleBar) replaces the Windows one, like Master Pro and PDF Pro
    minimumWidth: 1100
    minimumHeight: 700
    width: 1440
    height: 900
    visible: true
    color: Theme.background

    property bool forceClose: false
    property var pendingAction: null

    // The window opens the way it was closed: maximized or normal, with its normal size and place. The maximized flag does not depend on a stored
    // normal size (a window that was maximized from the first start has none).
    function restoreGeometry() {
        const g = AppSettings.state("window", null)
        if (!g)
            return
        if (g.w && g.h) {
            const w = Math.max(minimumWidth, g.w)
            const h = Math.max(minimumHeight, g.h)
            // Only restore the position if it is still on a visible screen.
            const onScreen = g.x > -w + 120 && g.y >= 0 && g.x < Screen.desktopAvailableWidth - 120
                             && g.y < Screen.desktopAvailableHeight - 80
            width = w
            height = h
            if (onScreen) {
                x = g.x
                y = g.y
            }
        }
        if (g.maximized)
            showMaximized()
    }

    function saveGeometry() {
        const maximized = visibility === Window.Maximized
        const previous = AppSettings.state("window", {}) || {}      // the stored value can be null
        if (maximized)       // keep the normal geometry that was stored (the current one is the size of the screen)
            AppSettings.setState("window", { x: previous.x, y: previous.y, w: previous.w, h: previous.h, maximized: true })
        else
            AppSettings.setState("window", { x: x, y: y, w: width, h: height, maximized: false })
    }

    Component.onCompleted: {
        restoreGeometry()
        Qt.callLater(startup)
    }

    function startup() {
        // Only when nothing was opened from the command line.
        if (Projects.isOpen || AppSettings.values.startupBehavior !== "lastProject" || Recents.count === 0)
            return
        if (Recents.existsAt(0)) {
            const err = Projects.openProject(Recents.pathAt(0))
            if (err !== "")
                App.toast(err, "error")
        }
    }

    // ---- Navigation -------------------------------------------------------------------------
    Connections {
        target: Projects
        function onProjectChanged() { App.screen = Projects.isOpen ? "editor" : "home" }
        function onRecoveryAvailable(path) { recoveryDialog.open() }
    }
    Connections {
        target: App
        function onToast(message, kind) { toasts.show(message, kind) }
    }
    Connections {
        target: Projects.media
        function onImportFailed(name, message) { toasts.show(qsTr("Cannot import %1: %2").arg(name).arg(message), "error") }
        function onImportSkipped(name) { toasts.show(qsTr("%1 is already in the project.").arg(name), "info") }
    }
    Connections {
        target: AppSettings
        function onSaveFailed(message) { toasts.show(qsTr("Cannot save settings: %1").arg(message), "error") }
    }

    // ---- Actions (project level) ----------------------------------------------------------------
    // Runs `next` immediately, or after the user decided what to do with unsaved changes.
    function guardUnsaved(next) {
        if (Projects.isOpen && Projects.dirty) {
            pendingAction = next
            unsavedDialog.open()
        } else {
            next()
        }
    }

    function doSave() {
        const err = Projects.save()
        if (err !== "") {
            toasts.show(err, "error")
            return false
        }
        toasts.show(qsTr("Project saved."), "success")
        return true
    }

    function doOpenPath(path) {
        guardUnsaved(function () {
            const err = Projects.openProject(path)
            if (err !== "")
                toasts.show(err, "error")
        })
    }

    function newProject() { guardUnsaved(function () { App.newProjectOpen = true }) }
    function openProject() { guardUnsaved(function () { openDialog.open() }) }
    function saveProject() { if (Projects.isOpen) doSave() }
    function saveProjectAs() { if (Projects.isOpen) saveDialog.open() }
    function goHome() { guardUnsaved(function () { Projects.closeProject() }) }

    Shortcut { sequences: [StandardKey.New]; onActivated: window.newProject() }
    Shortcut { sequences: [StandardKey.Open]; onActivated: window.openProject() }
    Shortcut { sequences: [StandardKey.Save]; enabled: Projects.isOpen; onActivated: window.saveProject() }
    Shortcut { sequences: ["Ctrl+Shift+S"]; enabled: Projects.isOpen; onActivated: window.saveProjectAs() }
    Shortcut { sequences: ["Ctrl+,"]; onActivated: App.settingsOpen = true }
    Shortcut { sequences: ["F1"]; onActivated: App.aboutOpen = true }

    onClosing: close => {
        if (!forceClose && Projects.isOpen && Projects.dirty) {
            close.accepted = false
            guardUnsaved(function () { window.forceClose = true; window.close() })
        } else {
            saveGeometry()
        }
    }
    onWidthChanged: geometryTimer.restart()
    onHeightChanged: geometryTimer.restart()
    onVisibilityChanged: geometryTimer.restart()
    Timer { id: geometryTimer; interval: 600; onTriggered: if (window.visibility === Window.Windowed || window.visibility === Window.Maximized) window.saveGeometry() }

    // ---- File dialogs ------------------------------------------------------------------------
    FileDialog {
        id: openDialog
        title: qsTr("Open project")
        nameFilters: [qsTr("DriveVault projects (*.dvproj)"), qsTr("All files (*)")]
        currentFolder: "file:///" + Projects.defaultLocation(AppSettings.values.defaultProjectLocation).replace(/\\/g, "/")
        onAccepted: {
            const err = Projects.openProject(Projects.toLocalPath(selectedFile.toString()))
            if (err !== "")
                toasts.show(err, "error")
        }
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save project as")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "dvproj"
        nameFilters: [qsTr("DriveVault projects (*.dvproj)")]
        onAccepted: {
            const err = Projects.saveAs(Projects.toLocalPath(selectedFile.toString()))
            if (err !== "")
                toasts.show(err, "error")
            else
                toasts.show(qsTr("Project saved."), "success")
        }
    }

    FolderDialog {
        id: folderProjectDialog
        title: qsTr("Choose the folder with your clips, pictures and audio")
        onAccepted: {
            const err = Projects.createProjectFromFolder(selectedFolder.toString(), Projects.defaultLocation(AppSettings.values.defaultProjectLocation))
            if (err !== "")
                toasts.show(err, "error")
            else
                toasts.show(qsTr("Project created from the folder. The files are being imported; then press \"Place all clips on the timeline\" in the Import tab."), "success")
        }
    }

    // ---- Title bar and content ------------------------------------------------------------------
    TitleBar {
        id: titleBar
        objectName: "titleBar"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        hostWindow: window
        onSettingsRequested: App.settingsOpen = true
        onAboutRequested: App.aboutOpen = true
    }

    HomeScreen {
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: App.screen === "home"
        onNewProjectRequested: App.newProjectOpen = true
        onOpenProjectRequested: openDialog.open()
        onFolderProjectRequested: window.guardUnsaved(function () { folderProjectDialog.open() })
        onOpenRecentRequested: path => window.doOpenPath(path)
    }

    Loader {
        id: editorLoader
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        active: App.screen === "editor"
        sourceComponent: EditorScreen {
            onNewRequested: window.newProject()
            onOpenRequested: window.openProject()
            onSaveRequested: window.saveProject()
            onSaveAsRequested: window.saveProjectAs()
            onCloseProjectRequested: window.goHome()
            onHomeRequested: window.goHome()
        }
    }

    // ---- Dialogs & overlays ----------------------------------------------------------------------
    NewProjectDialog {
        visible: App.newProjectOpen
        onClosed: App.newProjectOpen = false
        onCreated: toasts.show(qsTr("Project created."), "success")
    }

    SettingsDialog {
        onResetLayoutsRequested: {
            for (const key of ["media", "edit", "color", "audio", "effects"])
                AppSettings.setState("ws." + key, null)
            for (const panel of ["media", "inspector", "timeline"])
                AppSettings.setState("panel." + panel, true)
            if (editorLoader.item)
                editorLoader.item.resetLayout()
            toasts.show(qsTr("Panel layouts were reset."), "success")
        }
    }

    AboutDialog {
        visible: App.aboutOpen
        onClosed: App.aboutOpen = false
    }

    ConfirmDialog {
        id: unsavedDialog
        title: qsTr("Save changes?")
        message: qsTr("“%1” has unsaved changes. Do you want to save them before continuing?").arg(Projects.name)
        primaryText: qsTr("Save")
        secondaryText: qsTr("Don't save")
        onPrimary: {
            const next = window.pendingAction
            window.pendingAction = null
            if (window.doSave() && next)
                next()
        }
        onSecondary: {
            const next = window.pendingAction
            window.pendingAction = null
            // Discarding also drops the recovery copy so it is not offered again.
            if (next)
                next()
        }
        onCancelled: window.pendingAction = null
    }

    ConfirmDialog {
        id: recoveryDialog
        title: qsTr("Recover unsaved work?")
        message: qsTr("A newer automatic backup of this project was found. It may contain changes from a session that ended unexpectedly.")
        primaryText: qsTr("Restore backup")
        secondaryText: qsTr("Discard backup")
        cancelText: qsTr("Decide later")
        onPrimary: {
            const err = Projects.restoreRecovery()
            toasts.show(err === "" ? qsTr("Backup restored. Review the project and save it.") : err, err === "" ? "success" : "error")
        }
        onSecondary: Projects.discardRecovery()
    }

    ToastHost {
        id: toasts
        anchors.fill: parent
        z: 1000
    }

    // A window without the Windows frame has no resize edges: thin strips along the sides and corners start the system resize.
    component ResizeEdge: MouseArea {
        required property int edges
        enabled: window.visibility === Window.Windowed
        hoverEnabled: true
        z: 2000
        onPressed: mouse => { mouse.accepted = window.startSystemResize(edges) }
    }
    ResizeEdge { edges: Qt.LeftEdge; cursorShape: Qt.SizeHorCursor; x: 0; y: 10; width: 5; height: window.height - 20 }
    ResizeEdge { edges: Qt.RightEdge; cursorShape: Qt.SizeHorCursor; x: window.width - 5; y: 10; width: 5; height: window.height - 20 }
    ResizeEdge { edges: Qt.TopEdge; cursorShape: Qt.SizeVerCursor; x: 10; y: 0; width: window.width - 20; height: 5 }
    ResizeEdge { edges: Qt.BottomEdge; cursorShape: Qt.SizeVerCursor; x: 10; y: window.height - 5; width: window.width - 20; height: 5 }
    ResizeEdge { edges: Qt.LeftEdge | Qt.TopEdge; cursorShape: Qt.SizeFDiagCursor; x: 0; y: 0; width: 10; height: 10 }
    ResizeEdge { edges: Qt.RightEdge | Qt.BottomEdge; cursorShape: Qt.SizeFDiagCursor; x: window.width - 10; y: window.height - 10; width: 10; height: 10 }
    ResizeEdge { edges: Qt.RightEdge | Qt.TopEdge; cursorShape: Qt.SizeBDiagCursor; x: window.width - 10; y: 0; width: 10; height: 10 }
    ResizeEdge { edges: Qt.LeftEdge | Qt.BottomEdge; cursorShape: Qt.SizeBDiagCursor; x: 0; y: window.height - 10; width: 10; height: 10 }
}
