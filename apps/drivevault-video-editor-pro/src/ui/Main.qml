import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.Backend

ApplicationWindow {
    id: window

    // ---- Window ----------------------------------------------------------------------------
    title: (Projects.isOpen ? Projects.name + (Projects.dirty ? " *" : "") + " — " : "") + "DriveVault Video Editor Pro"
    minimumWidth: 1100
    minimumHeight: 700
    width: 1440
    height: 900
    visible: true
    color: Theme.background

    property bool forceClose: false
    property var pendingAction: null

    function restoreGeometry() {
        const g = AppSettings.state("window", null)
        if (!g || !g.w || !g.h)
            return
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
        if (g.maximized)
            showMaximized()
    }

    function saveGeometry() {
        const maximized = visibility === Window.Maximized
        const previous = AppSettings.state("window", {})
        AppSettings.setState("window", maximized
            ? { x: previous.x, y: previous.y, w: previous.w, h: previous.h, maximized: true }
            : { x: x, y: y, w: width, h: height, maximized: false })
    }

    function applyTitleBar() {
        WinIntegration.applyTitleBarTheme(window, Theme.dark, Theme.backgroundAlt, Theme.text)
    }

    Connections { target: Theme; function onDarkChanged() { window.applyTitleBar() } }

    Component.onCompleted: {
        restoreGeometry()
        applyTitleBar()
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
    Timer { id: geometryTimer; interval: 600; onTriggered: if (window.visibility === Window.Windowed) window.saveGeometry() }

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

    // ---- Content ------------------------------------------------------------------------------
    HomeScreen {
        anchors.fill: parent
        visible: App.screen === "home"
        onNewProjectRequested: App.newProjectOpen = true
        onOpenProjectRequested: openDialog.open()
        onOpenRecentRequested: path => window.doOpenPath(path)
    }

    Loader {
        id: editorLoader
        anchors.fill: parent
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
}
