import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

// Main editor shell: menu, workspace switcher, resizable panels, timeline and status bar.
Rectangle {
    id: root

    signal newRequested()
    signal openRequested()
    signal saveRequested()
    signal saveAsRequested()
    signal closeProjectRequested()
    signal homeRequested()

    color: Theme.background

    // ---- Layout state ------------------------------------------------------------------------
    property bool showMedia: AppSettings.state("panel.media", true)
    property bool showInspector: AppSettings.state("panel.inspector", true)
    property bool showTimeline: AppSettings.state("panel.timeline", true)
    property real pxPerFrame: 4
    property string tool: "select"          // select | ripple | roll | slip | slide

    readonly property var presets: ({
        "media":   { media: 560, inspector: 300, timeline: 250 },
        "edit":    { media: 460, inspector: 320, timeline: 330 },
        "color":   { media: 400, inspector: 430, timeline: 250 },
        "audio":   { media: 400, inspector: 340, timeline: 400 },
        "effects": { media: 420, inspector: 450, timeline: 290 },
        "ai":      { media: 400, inspector: 420, timeline: 280 }
    })

    function setPanel(name, visible) {
        AppSettings.setState("panel." + name, visible)
        if (name === "media") showMedia = visible
        else if (name === "inspector") showInspector = visible
        else showTimeline = visible
    }

    function sizesFor(workspace) {
        const saved = AppSettings.state("ws." + workspace, null)
        return saved ? saved : (presets[workspace] || presets.edit)
    }

    function applyWorkspaceLayout() {
        if (App.workspace === "export")
            return
        const s = sizesFor(App.workspace)
        toolPanel.SplitView.preferredWidth = Math.max(380, s.media)
        inspectorPanel.SplitView.preferredWidth = s.inspector
        timelineArea.SplitView.preferredHeight = s.timeline
    }

    function saveWorkspaceLayout() {
        if (App.workspace === "export")
            return
        AppSettings.setState("ws." + App.workspace, {
            media: Math.round(toolPanel.width),
            inspector: Math.round(inspectorPanel.width),
            timeline: Math.round(timelineArea.height)
        })
    }

    function resetLayout() {
        for (const key of Object.keys(presets))
            AppSettings.setState("ws." + key, null)
        setPanel("media", true)
        setPanel("inspector", true)
        setPanel("timeline", true)
        applyWorkspaceLayout()
    }

    function zoomBy(factor) {
        pxPerFrame = timelinePanel.clampZoom(pxPerFrame * factor)
    }

    Component.onCompleted: Qt.callLater(applyWorkspaceLayout)
    Connections {
        target: App
        function onWorkspaceChanged() { Qt.callLater(root.applyWorkspaceLayout) }
    }

    function report(error) { if (error !== "") App.toast(error, "warning") }
    function importMedia() { toolPanel.openImport() }
    function showToolTab(key) { setPanel("media", true); toolPanel.selectTab(key) }
    function addSelectedMedia() { if (App.selectedMediaId !== "") toolPanel.media.addToTimeline(App.selectedMediaId) }
    function jumpMarker(forward) {
        const f = forward ? Projects.timeline.nextMarker(Projects.playheadFrame) : Projects.timeline.prevMarker(Projects.playheadFrame)
        if (f >= 0)
            Projects.playheadFrame = f
    }
    function trimSelected(start) {
        const id = Projects.timeline.primarySelectedId
        if (id === "")
            return
        report(start ? Projects.timeline.trimStart(id, Projects.playheadFrame) : Projects.timeline.trimEnd(id, Projects.playheadFrame))
    }

    // bottom panel: "timeline" | "mixer" (remembered)
    property string bottomTab: AppSettings.state("bottom.tab", "timeline")
    function selectBottomTab(key) {
        bottomTab = key
        AppSettings.setState("bottom.tab", key)
    }

    Shortcut { sequence: "="; enabled: root.visible; onActivated: root.zoomBy(1.25) }
    Shortcut { sequence: "+"; enabled: root.visible; onActivated: root.zoomBy(1.25) }
    Shortcut { sequence: "-"; enabled: root.visible; onActivated: root.zoomBy(0.8) }
    // Editing and transport shortcuts (plain keys never fire while a text field has the focus)
    Shortcut { sequence: "Space"; enabled: root.visible; onActivated: Playback.togglePlay() }
    Shortcut { sequence: "Ctrl+K"; enabled: root.visible; onActivated: root.report(Projects.timeline.splitAt(Projects.playheadFrame)) }
    Shortcut { sequence: "Delete"; enabled: root.visible; onActivated: root.report(Projects.timeline.deleteSelected()) }
    Shortcut { sequence: "Shift+Delete"; enabled: root.visible; onActivated: root.report(Projects.timeline.rippleDeleteSelected()) }
    Shortcut { sequence: "Ctrl+Z"; enabled: root.visible; onActivated: Projects.timeline.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; enabled: root.visible; onActivated: Projects.timeline.redo() }
    Shortcut { sequence: "V"; enabled: root.visible; onActivated: root.tool = "select" }
    Shortcut { sequence: "B"; enabled: root.visible; onActivated: root.tool = "ripple" }
    Shortcut { sequence: "N"; enabled: root.visible; onActivated: root.tool = "roll" }
    Shortcut { sequence: "Y"; enabled: root.visible; onActivated: root.tool = "slip" }
    Shortcut { sequence: "U"; enabled: root.visible; onActivated: root.tool = "slide" }
    Shortcut { sequence: "M"; enabled: root.visible; onActivated: Projects.timeline.addMarker(Projects.playheadFrame, "") }
    Shortcut { sequence: "Shift+M"; enabled: root.visible; onActivated: root.jumpMarker(true) }
    Shortcut { sequence: "Ctrl+Shift+M"; enabled: root.visible; onActivated: root.jumpMarker(false) }
    Shortcut { sequence: "I"; enabled: root.visible; onActivated: Projects.timeline.setInPoint(Projects.playheadFrame) }
    Shortcut { sequence: "O"; enabled: root.visible; onActivated: Projects.timeline.setOutPoint(Projects.playheadFrame) }
    Shortcut { sequence: "Alt+X"; enabled: root.visible; onActivated: Projects.timeline.clearRange() }
    Shortcut { sequence: "Ctrl+C"; enabled: root.visible; onActivated: root.report(Projects.timeline.copySelected()) }
    Shortcut { sequence: "Ctrl+X"; enabled: root.visible; onActivated: root.report(Projects.timeline.cutSelected()) }
    Shortcut { sequence: "Ctrl+V"; enabled: root.visible; onActivated: root.report(Projects.timeline.paste(Projects.playheadFrame)) }
    Shortcut { sequence: "Ctrl+G"; enabled: root.visible; onActivated: root.report(Projects.timeline.groupSelected()) }
    Shortcut { sequence: "Ctrl+Shift+G"; enabled: root.visible; onActivated: root.report(Projects.timeline.ungroupSelected()) }
    Shortcut { sequence: "Ctrl+A"; enabled: root.visible; onActivated: Projects.timeline.selectAll() }
    Shortcut { sequence: "Ctrl+I"; enabled: root.visible; onActivated: root.importMedia() }
    Shortcut { sequence: "Ctrl+M"; enabled: root.visible; onActivated: App.workspace = "export" }
    Shortcut { sequence: "Left"; enabled: root.visible; onActivated: Projects.stepFrames(-1) }
    Shortcut { sequence: "Right"; enabled: root.visible; onActivated: Projects.stepFrames(1) }
    Shortcut { sequence: "Home"; enabled: root.visible; onActivated: Playback.goToStart() }
    Shortcut { sequence: "End"; enabled: root.visible; onActivated: Playback.goToEnd() }
    Shortcut { sequence: "Q"; enabled: root.visible; onActivated: root.trimSelected(true) }
    Shortcut { sequence: "W"; enabled: root.visible; onActivated: root.trimSelected(false) }

    // Files dragged in from Explorer anywhere on the editor are imported.
    DropArea {
        anchors.fill: parent
        z: -1
        onDropped: drop => {
            if (drop.hasUrls) {
                drop.accept()
                Projects.media.importUrls(drop.urls)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        EditorMenuBar {
            Layout.fillWidth: true
            editor: root
            onNewRequested: root.newRequested()
            onOpenRequested: root.openRequested()
            onSaveRequested: root.saveRequested()
            onSaveAsRequested: root.saveAsRequested()
            onCloseProjectRequested: root.closeProjectRequested()
            onHomeRequested: root.homeRequested()
        }

        WorkspaceBar {
            Layout.fillWidth: true
            onHomeRequested: root.homeRequested()
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            SplitView {
                id: vertical
                anchors.fill: parent
                anchors.margins: 6
                orientation: Qt.Vertical
                visible: App.workspace !== "export"
                onResizingChanged: if (!resizing) root.saveWorkspaceLayout()

                handle: Rectangle {
                    implicitWidth: 6
                    implicitHeight: 6
                    color: SplitHandle.pressed ? Theme.accent : SplitHandle.hovered ? Theme.border : "transparent"
                    radius: 3
                }

                SplitView {
                    id: horizontal
                    SplitView.fillHeight: true
                    SplitView.minimumHeight: 240
                    orientation: Qt.Horizontal
                    onResizingChanged: if (!resizing) root.saveWorkspaceLayout()

                    handle: Rectangle {
                        implicitWidth: 6
                        implicitHeight: 6
                        color: SplitHandle.pressed ? Theme.accent : SplitHandle.hovered ? Theme.border : "transparent"
                        radius: 3
                    }

                    ToolPanel {
                        id: toolPanel
                        onMixerRequested: root.selectBottomTab("mixer")
                        visible: root.showMedia
                        SplitView.preferredWidth: 460
                        SplitView.minimumWidth: 320
                        SplitView.maximumWidth: 760
                    }
                    ProgramMonitor {
                        id: monitorPanel
                        SplitView.fillWidth: true
                        SplitView.minimumWidth: 360
                    }
                    InspectorPanel {
                        id: inspectorPanel
                        visible: root.showInspector
                        SplitView.preferredWidth: 320
                        SplitView.minimumWidth: 240
                        SplitView.maximumWidth: 640
                    }
                }

                // timeline + the master level meter (always visible at its right edge)
                Item {
                    id: timelineArea
                    visible: root.showTimeline
                    SplitView.preferredHeight: 362
                    SplitView.minimumHeight: 170
                    SplitView.maximumHeight: 640

                    RowLayout {
                        anchors.fill: parent
                        spacing: 6
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 4

                            // tabs of the bottom panel: Timeline | Mixer
                            Row {
                                id: bottomTabs
                                spacing: 4
                                Repeater {
                                    model: [ { key: "timeline", label: qsTr("Timeline"), icon: "layers" }, { key: "mixer", label: qsTr("Mixer"), icon: "sliders" } ]
                                    delegate: Rectangle {
                                        id: bottomTab
                                        required property var modelData
                                        readonly property bool current: root.bottomTab === modelData.key
                                        objectName: "bottomTab_" + modelData.key
                                        width: tabRow.implicitWidth + 24
                                        height: 26
                                        radius: Theme.radiusSmall + 2
                                        color: current ? Theme.accentSubtle : tabMouse.containsMouse ? Theme.surfaceHover : "transparent"
                                        Row {
                                            id: tabRow
                                            anchors.centerIn: parent
                                            spacing: 6
                                            Icon { anchors.verticalCenter: parent.verticalCenter; name: bottomTab.modelData.icon; size: 15; color: bottomTab.current ? Theme.accentText : Theme.textSecondary }
                                            AppText {
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: bottomTab.modelData.label
                                                font.pixelSize: Theme.fontSm
                                                font.weight: bottomTab.current ? Font.DemiBold : Font.Medium
                                                color: bottomTab.current ? Theme.accentText : Theme.textSecondary
                                            }
                                        }
                                        Rectangle {
                                            visible: bottomTab.current
                                            anchors.bottom: parent.bottom
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: parent.width - 20; height: 2; radius: 1
                                            color: Theme.accent
                                        }
                                        MouseArea {
                                            id: tabMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.selectBottomTab(bottomTab.modelData.key)
                                        }
                                    }
                                }
                            }

                            StackLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                currentIndex: root.bottomTab === "mixer" ? 1 : 0
                                TimelinePanel {
                                    id: timelinePanel
                                    pxPerFrame: root.pxPerFrame
                                    tool: root.tool
                                    onToolRequested: t => root.tool = t
                                    onZoomRequested: value => root.pxPerFrame = value
                                }
                                MixerPanel { id: mixerPanel }
                            }
                        }
                        MasterMeter {
                            Layout.fillHeight: true
                            Layout.preferredWidth: 84
                        }
                    }
                }
            }

            ExportWorkspace {
                anchors.fill: parent
                anchors.margins: 6
                visible: App.workspace === "export"
            }
        }

        EditorStatusBar {
            Layout.fillWidth: true
            pxPerFrame: root.pxPerFrame
        }
    }
}
