import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

// Application menu + project title.
Rectangle {
    id: root

    property var editor
    readonly property string videoClip: Projects.timeline.selectedClipOfKind("video")
    function transition(direction, type) {
        if (videoClip === "")
            return
        const frames = Math.max(2, Math.round(Projects.nominalFps / 2))
        editor.report(Projects.timeline.setTransition(videoClip, direction, type, type === "" ? 0 : frames))
    }
    function addTrack(kind) { editor.report(Projects.addTrack(kind)) }
    function openDocument(name) {
        const path = SysInfo.documentPath(name)
        if (path === "" || !SysInfo.openFile(path))
            App.toast(qsTr("This document was not found next to the application."), "warning")
    }

    signal newRequested()
    signal openRequested()
    signal saveRequested()
    signal saveAsRequested()
    signal closeProjectRequested()
    signal homeRequested()

    implicitHeight: 40
    color: Theme.backgroundAlt

    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 10
        spacing: 6

        Logo { size: 24; Layout.alignment: Qt.AlignVCenter }

        MenuBar {
            id: menuBar
            Layout.leftMargin: 6
            implicitHeight: 32
            background: Item {}

            delegate: MenuBarItem {
                id: barItem
                implicitHeight: 30
                leftPadding: 12
                rightPadding: 12
                contentItem: AppText {
                    text: barItem.text
                    font.pixelSize: Theme.fontMd
                    color: barItem.highlighted ? Theme.text : Theme.textSecondary
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: barItem.highlighted || barItem.down ? Theme.surfaceHover : "transparent"
                }
            }

            AppMenu {
                title: qsTr("File")
                AppMenuItem { text: qsTr("New Project…"); shortcutText: "Ctrl+N"; onTriggered: root.newRequested() }
                AppMenuItem { text: qsTr("Open Project…"); shortcutText: "Ctrl+O"; onTriggered: root.openRequested() }
                AppMenu {
                    title: qsTr("Open Recent")
                    enabled: Recents.count > 0
                    Repeater {
                        model: Recents
                        delegate: AppMenuItem {
                            required property string name
                            required property string path
                            text: name
                            onTriggered: Projects.openProject(path)
                        }
                    }
                }
                AppMenuSeparator {}
                AppMenuItem { text: qsTr("Save"); shortcutText: "Ctrl+S"; enabled: Projects.isOpen; onTriggered: root.saveRequested() }
                AppMenuItem { text: qsTr("Save As…"); shortcutText: "Ctrl+Shift+S"; enabled: Projects.isOpen; onTriggered: root.saveAsRequested() }
                AppMenuItem { text: qsTr("Close Project"); enabled: Projects.isOpen; onTriggered: root.closeProjectRequested() }
                AppMenuSeparator {}
                AppMenuItem { text: qsTr("Import Media…"); shortcutText: "Ctrl+I"; enabled: Projects.isOpen; onTriggered: editor.importMedia() }
                AppMenuItem { text: qsTr("Export…"); shortcutText: "Ctrl+M"; enabled: Projects.isOpen; onTriggered: App.workspace = "export" }
                AppMenuSeparator {}
                AppMenuItem { text: qsTr("Settings…"); shortcutText: "Ctrl+,"; onTriggered: App.settingsOpen = true }
                AppMenuItem { text: qsTr("Exit"); shortcutText: "Alt+F4"; onTriggered: App.quit() }
            }

            AppMenu {
                title: qsTr("Edit")
                AppMenuItem { text: qsTr("Undo") + (Projects.timeline.canUndo ? " " + Projects.timeline.undoText : ""); shortcutText: "Ctrl+Z"; enabled: Projects.timeline.canUndo; onTriggered: Projects.timeline.undo() }
                AppMenuItem { text: qsTr("Redo") + (Projects.timeline.canRedo ? " " + Projects.timeline.redoText : ""); shortcutText: "Ctrl+Shift+Z"; enabled: Projects.timeline.canRedo; onTriggered: Projects.timeline.redo() }
                AppMenuSeparator {}
                AppMenuItem { text: qsTr("Select All Clips"); shortcutText: "Ctrl+A"; enabled: Projects.timeline.clipCount > 0; onTriggered: Projects.timeline.selectAll() }
                AppMenuItem { text: qsTr("Delete"); shortcutText: "Del"; enabled: Projects.timeline.selectedCount > 0; onTriggered: editor.report(Projects.timeline.deleteSelected()) }
                AppMenuItem { text: qsTr("Ripple Delete"); shortcutText: "Shift+Del"; enabled: Projects.timeline.selectedCount > 0; onTriggered: editor.report(Projects.timeline.rippleDeleteSelected()) }
                AppMenuSeparator {}
                AppMenuItem { text: qsTr("Cut"); shortcutText: "Ctrl+X"; enabled: Projects.timeline.selectedCount > 0; onTriggered: editor.report(Projects.timeline.cutSelected()) }
                AppMenuItem { text: qsTr("Copy"); shortcutText: "Ctrl+C"; enabled: Projects.timeline.selectedCount > 0; onTriggered: editor.report(Projects.timeline.copySelected()) }
                AppMenuItem { text: qsTr("Paste at Playhead"); shortcutText: "Ctrl+V"; enabled: Projects.timeline.canPaste; onTriggered: editor.report(Projects.timeline.paste(Projects.playheadFrame)) }
                AppMenuSeparator {}
                AppMenuItem { text: qsTr("Group"); shortcutText: "Ctrl+G"; enabled: Projects.timeline.selectedCount > 1; onTriggered: editor.report(Projects.timeline.groupSelected()) }
                AppMenuItem { text: qsTr("Ungroup"); shortcutText: "Ctrl+Shift+G"; enabled: Projects.timeline.selectedCount > 0; onTriggered: editor.report(Projects.timeline.ungroupSelected()) }
            }

            AppMenu {
                title: qsTr("Clip")
                AppMenuItem { text: qsTr("Add Selected Media to Timeline"); enabled: App.selectedMediaId !== ""; onTriggered: editor.addSelectedMedia() }
                AppMenuSeparator {}
                AppMenuItem { text: qsTr("Split at Playhead"); shortcutText: "Ctrl+K"; enabled: Projects.timeline.clipCount > 0; onTriggered: editor.report(Projects.timeline.splitAt(Projects.playheadFrame)) }
                AppMenuItem { text: qsTr("Trim Start to Playhead"); shortcutText: "Q"; enabled: Projects.timeline.selectedCount > 0; onTriggered: editor.trimSelected(true) }
                AppMenuItem { text: qsTr("Trim End to Playhead"); shortcutText: "W"; enabled: Projects.timeline.selectedCount > 0; onTriggered: editor.trimSelected(false) }
                AppMenuSeparator {}
                AppMenu {
                    title: qsTr("Editing Tool")
                    AppMenuItem { text: qsTr("Select"); shortcutText: "V"; checkable: true; checked: editor.tool === "select"; onTriggered: editor.tool = "select" }
                    AppMenuItem { text: qsTr("Ripple Trim"); shortcutText: "B"; checkable: true; checked: editor.tool === "ripple"; onTriggered: editor.tool = "ripple" }
                    AppMenuItem { text: qsTr("Roll Edit"); shortcutText: "N"; checkable: true; checked: editor.tool === "roll"; onTriggered: editor.tool = "roll" }
                    AppMenuItem { text: qsTr("Slip"); shortcutText: "Y"; checkable: true; checked: editor.tool === "slip"; onTriggered: editor.tool = "slip" }
                    AppMenuItem { text: qsTr("Slide"); shortcutText: "U"; checkable: true; checked: editor.tool === "slide"; onTriggered: editor.tool = "slide" }
                }
                AppMenuSeparator {}
                AppMenuItem { text: qsTr("Add Title"); enabled: Projects.isOpen; onTriggered: editor.report(Projects.timeline.addTitle(-1, Projects.playheadFrame, "title")) }
                AppMenuItem { text: qsTr("Add Lower Third"); enabled: Projects.isOpen; onTriggered: editor.report(Projects.timeline.addTitle(-1, Projects.playheadFrame, "lowerThird")) }
                AppMenuItem { text: qsTr("Add Caption"); enabled: Projects.isOpen; onTriggered: editor.report(Projects.timeline.addTitle(-1, Projects.playheadFrame, "caption")) }
                AppMenuItem { text: qsTr("Add Subtitle at Playhead"); enabled: Projects.isOpen; onTriggered: editor.report(Projects.timeline.addSubtitle(Projects.playheadFrame, Math.round(Projects.nominalFps * 3), qsTr("Subtitle"))) }
            }

            AppMenu {
                title: qsTr("Sequence")
                AppMenuItem { text: qsTr("Go to Start"); shortcutText: "Home"; onTriggered: Playback.goToStart() }
                AppMenuItem { text: qsTr("Go to End"); shortcutText: "End"; onTriggered: Playback.goToEnd() }
                AppMenuItem { text: Playback.playing ? qsTr("Pause") : qsTr("Play"); shortcutText: "Space"; enabled: Projects.timeline.clipCount > 0; onTriggered: Playback.togglePlay() }
                AppMenuSeparator {}
                AppMenu {
                    title: qsTr("Add Track")
                    enabled: Projects.isOpen
                    AppMenuItem { text: qsTr("Video Track"); onTriggered: root.addTrack("video") }
                    AppMenuItem { text: qsTr("Audio Track"); onTriggered: root.addTrack("audio") }
                    AppMenuItem { text: qsTr("Subtitle Track"); onTriggered: root.addTrack("subtitle") }
                }
                AppMenuSeparator {}
                AppMenuItem { text: qsTr("Add Marker"); shortcutText: "M"; enabled: Projects.isOpen; onTriggered: Projects.timeline.addMarker(Projects.playheadFrame, "") }
                AppMenuItem { text: qsTr("Next Marker"); shortcutText: "Shift+M"; enabled: Projects.timeline.markers.length > 0; onTriggered: editor.jumpMarker(true) }
                AppMenuItem { text: qsTr("Previous Marker"); shortcutText: "Ctrl+Shift+M"; enabled: Projects.timeline.markers.length > 0; onTriggered: editor.jumpMarker(false) }
                AppMenuSeparator {}
                AppMenuItem { text: qsTr("Mark In"); shortcutText: "I"; enabled: Projects.isOpen; onTriggered: Projects.timeline.setInPoint(Projects.playheadFrame) }
                AppMenuItem { text: qsTr("Mark Out"); shortcutText: "O"; enabled: Projects.isOpen; onTriggered: Projects.timeline.setOutPoint(Projects.playheadFrame) }
                AppMenuItem { text: qsTr("Clear In / Out"); shortcutText: "Alt+X"; enabled: Projects.timeline.inPoint >= 0 || Projects.timeline.outPoint >= 0; onTriggered: Projects.timeline.clearRange() }
            }

            AppMenu {
                title: qsTr("Effects")
                AppMenuItem { text: qsTr("Effects Tab"); onTriggered: editor.showToolTab("effects") }
                AppMenuItem { text: qsTr("Colour Workspace"); onTriggered: App.workspace = "color" }
                AppMenuSeparator {}
                AppMenu {
                    title: qsTr("Transition on Selected Clip")
                    enabled: root.videoClip !== ""
                    AppMenuItem { text: qsTr("Cross Dissolve (in)"); onTriggered: root.transition("in", "dissolve") }
                    AppMenuItem { text: qsTr("Dip to Black (in)"); onTriggered: root.transition("in", "dip") }
                    AppMenuItem { text: qsTr("Fade from Black (in)"); onTriggered: root.transition("in", "fade") }
                    AppMenuItem { text: qsTr("Wipe (in)"); onTriggered: root.transition("in", "wipe") }
                    AppMenuItem { text: qsTr("Slide (in)"); onTriggered: root.transition("in", "slide") }
                    AppMenuItem { text: qsTr("Zoom (in)"); onTriggered: root.transition("in", "zoom") }
                    AppMenuSeparator {}
                    AppMenuItem { text: qsTr("Fade to Black (out)"); onTriggered: root.transition("out", "fade") }
                    AppMenuItem { text: qsTr("Dip to Black (out)"); onTriggered: root.transition("out", "dip") }
                    AppMenuSeparator {}
                    AppMenuItem { text: qsTr("Remove Transitions"); onTriggered: { root.transition("in", ""); root.transition("out", "") } }
                }
                AppMenuItem {
                    text: qsTr("Reset Selected Clip Effects")
                    enabled: root.videoClip !== ""
                    onTriggered: {
                        for (const g of ["transform", "color", "grade", "effects"])
                            editor.report(Projects.timeline.resetParams(root.videoClip, g))
                    }
                }
            }

            AppMenu {
                title: qsTr("View")
                AppMenuItem { text: qsTr("Project Media Panel"); checkable: true; checked: editor ? editor.showMedia : true; onTriggered: editor.setPanel("media", checked) }
                AppMenuItem { text: qsTr("Inspector Panel"); checkable: true; checked: editor ? editor.showInspector : true; onTriggered: editor.setPanel("inspector", checked) }
                AppMenuItem { text: qsTr("Timeline Panel"); checkable: true; checked: editor ? editor.showTimeline : true; onTriggered: editor.setPanel("timeline", checked) }
                AppMenuSeparator {}
                AppMenuItem { text: qsTr("Zoom In Timeline"); shortcutText: "+"; onTriggered: editor.zoomBy(1.25) }
                AppMenuItem { text: qsTr("Zoom Out Timeline"); shortcutText: "-"; onTriggered: editor.zoomBy(0.8) }
                AppMenuSeparator {}
                AppMenuItem { text: qsTr("Reset Panel Layout"); onTriggered: editor.resetLayout() }
            }

            AppMenu {
                title: qsTr("Window")
                AppMenuItem { text: qsTr("Home"); onTriggered: root.homeRequested() }
                AppMenuSeparator {}
                AppMenuItem { text: qsTr("Edit Workspace"); checkable: true; checked: App.workspace === "edit"; onTriggered: App.workspace = "edit" }
                AppMenuItem { text: qsTr("Color Workspace"); checkable: true; checked: App.workspace === "color"; onTriggered: App.workspace = "color" }
                AppMenuItem { text: qsTr("Export Workspace"); checkable: true; checked: App.workspace === "export"; onTriggered: App.workspace = "export" }
            }

            AppMenu {
                title: qsTr("Help")
                AppMenuItem { text: qsTr("User Guide"); onTriggered: root.openDocument(Localization.currentLanguage === "th-TH" ? "user-guide.th.html" : "user-guide.en.html") }
                AppMenuItem { text: qsTr("Release Notes"); onTriggered: root.openDocument("release-notes.txt") }
                AppMenuItem { text: qsTr("Third-Party Notices"); onTriggered: root.openDocument("THIRD-PARTY-NOTICES.txt") }
                AppMenuSeparator {}
                AppMenuItem { text: qsTr("Open Log Folder"); onTriggered: SysInfo.openFolder(SysInfo.logDirectory) }
                AppMenuItem { text: qsTr("Open Data Folder"); onTriggered: SysInfo.openFolder(SysInfo.dataDirectory) }
                AppMenuSeparator {}
                AppMenuItem { text: qsTr("About DriveVault Video Editor Pro"); onTriggered: App.aboutOpen = true }
            }
        }

        Item { Layout.fillWidth: true }

        // Project title (centered visually by the spacers)
        RowLayout {
            spacing: 8
            Icon { name: "film"; size: 15; color: Theme.textSecondary }
            AppText {
                text: Projects.isOpen ? Projects.name : ""
                font.pixelSize: Theme.fontMd
                font.weight: Font.DemiBold
                color: Theme.text
                elide: Text.ElideRight
                Layout.maximumWidth: 320
            }
            Rectangle {
                visible: Projects.dirty
                width: 8; height: 8; radius: 4
                color: Theme.warning
                Accessible.name: qsTr("Unsaved changes")
            }
            AppText {
                visible: Projects.dirty
                text: qsTr("Unsaved")
                font.pixelSize: Theme.fontXs
                color: Theme.warning
            }
        }

        Item { Layout.fillWidth: true }

        IconButton { objectName: "menuSave"; iconName: "save"; tip: qsTr("Save") + "  (Ctrl+S)"; enabled: Projects.dirty; onClicked: root.saveRequested() }
        IconButton { iconName: "sliders"; tip: qsTr("Settings") + "  (Ctrl+,)"; onClicked: App.settingsOpen = true }
    }
}
