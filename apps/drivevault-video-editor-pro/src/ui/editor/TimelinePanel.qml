import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.Backend

// Multitrack timeline: ruler (timecode, zoom, scroll), draggable playhead, track headers (hide / mute / lock),
// clips with thumbnails and waveforms - move, trim, split, delete, drop media from the media panel.
Rectangle {
    id: root

    property real pxPerFrame: 4
    property string tool: "select"        // select | ripple | roll | slip | slide
    signal zoomRequested(real value)
    signal toolRequested(string tool)

    readonly property var tl: Projects.timeline
    readonly property int headerWidth: 168
    readonly property int rulerHeight: 30
    readonly property int laneHeight: 46
    readonly property int fps: Math.max(1, Projects.nominalFps)
    readonly property int totalFrames: fps * 60 * 30   // 30 minutes of addressable timeline

    color: Theme.backgroundAlt
    radius: Theme.radius
    border.color: Theme.borderSubtle
    clip: true

    function frameAtViewportX(x) {
        return Math.max(0, Math.min(totalFrames, Math.round((x + lanes.contentX) / pxPerFrame)))
    }

    function followPlayhead() {
        const x = Projects.playheadFrame * pxPerFrame
        if (x < lanes.contentX || x > lanes.contentX + lanes.width - 30)
            lanes.contentX = Math.max(0, Math.min(lanes.contentWidth - lanes.width, x - 40))
    }
    Connections {
        target: Projects
        function onPlayheadChanged() { if (Playback.playing) root.followPlayhead() }
    }

    // media kind -> the kind of track it belongs on
    function primaryKind(mediaId) {
        const d = Projects.media.describe(mediaId)
        return d.kind === "audio" ? "audio" : "video"
    }

    function report(error) { if (error !== "") App.toast(error, "warning") }
    function splitAtPlayhead() { report(tl.splitAt(Projects.playheadFrame)) }
    property string markerId: ""
    function addMarkerAtPlayhead() { tl.addMarker(Projects.playheadFrame, "") }
    function addTitle(preset) { report(tl.addTitle(-1, Projects.playheadFrame, preset)) }
    function addSubtitleAtPlayhead() { report(tl.addSubtitle(Projects.playheadFrame, Math.round(Projects.nominalFps * 3), qsTr("Subtitle"))) }
    function addTrack(kind) {
        const err = Projects.addTrack(kind)
        if (err !== "")
            App.toast(err, "warning")
    }
    function markerAtX(x) {
        const list = tl.markers
        for (let i = 0; i < list.length; ++i)
            if (Math.abs(list[i].frame * pxPerFrame - lanes.contentX - x) <= 7)
                return list[i]
        return null
    }

    FileDialog {
        id: srtImport
        title: qsTr("Import subtitles (.srt)")
        nameFilters: [qsTr("SubRip subtitles (*.srt)"), qsTr("All files (*)")]
        onAccepted: root.report(root.tl.importSrt(selectedFile.toString()))
    }
    FileDialog {
        id: srtExport
        title: qsTr("Export subtitles (.srt)")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "srt"
        nameFilters: [qsTr("SubRip subtitles (*.srt)")]
        onAccepted: root.report(root.tl.exportSrt(selectedFile.toString()))
    }
    AppDialog {
        id: markerDialog
        title: qsTr("Rename marker")
        implicitWidth: 380
        onOpened: { markerName.text = root.markerName; markerName.forceActiveFocus(); markerName.selectAll() }
        AppTextField { id: markerName; Layout.fillWidth: true; onAccepted: markerOk.clicked() }
        footerContent: [
            Item { Layout.fillWidth: true },
            AppButton { variant: "ghost"; text: qsTr("Cancel"); onClicked: markerDialog.close() },
            AppButton { id: markerOk; variant: "primary"; text: qsTr("OK"); onClicked: { root.tl.renameMarker(root.markerId, markerName.text); markerDialog.close() } }
        ]
    }
    property string markerName: ""
    AppMenu {
        id: markerMenu
        AppMenuItem { text: qsTr("Rename…"); onTriggered: markerDialog.open() }
        AppMenuItem { text: qsTr("Delete marker"); onTriggered: root.tl.removeMarker(root.markerId) }
    }
    AppMenu {
        id: addMenu
        AppMenuItem { text: qsTr("Title"); onTriggered: root.addTitle("title") }
        AppMenuItem { text: qsTr("Lower third"); onTriggered: root.addTitle("lowerThird") }
        AppMenuItem { text: qsTr("Caption"); onTriggered: root.addTitle("caption") }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Subtitle at playhead"); onTriggered: root.addSubtitleAtPlayhead() }
        AppMenuItem { text: qsTr("Import subtitles (.srt)…"); onTriggered: srtImport.open() }
        AppMenuItem { text: qsTr("Export subtitles (.srt)…"); enabled: root.tl.subtitleCount > 0; onTriggered: srtExport.open() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Add video track"); onTriggered: root.addTrack("video") }
        AppMenuItem { text: qsTr("Add audio track"); onTriggered: root.addTrack("audio") }
        AppMenuItem { text: qsTr("Add subtitle track"); onTriggered: root.addTrack("subtitle") }
    }
    AppMenu {
        id: trackMenu
        property int row: -1
        AppMenuItem { text: qsTr("Remove track"); enabled: trackMenu.row >= 0 && !root.tl.trackUsed(trackMenu.row)
                      onTriggered: { const e = Projects.removeTrack(trackMenu.row); if (e !== "") App.toast(e, "warning") } }
        AppMenuItem { text: qsTr("Lock / unlock"); onTriggered: Projects.tracks.toggle(trackMenu.row, "locked") }
    }

    AppMenu {
        id: clipMenu
        AppMenuItem { text: qsTr("Split at Playhead"); shortcutText: "Ctrl+K"; onTriggered: root.splitAtPlayhead() }
        AppMenuItem { text: qsTr("Delete"); shortcutText: "Del"; onTriggered: root.report(root.tl.deleteSelected()) }
        AppMenuItem { text: qsTr("Ripple Delete"); shortcutText: "Shift+Del"; onTriggered: root.report(root.tl.rippleDeleteSelected()) }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Cut"); shortcutText: "Ctrl+X"; onTriggered: root.report(root.tl.cutSelected()) }
        AppMenuItem { text: qsTr("Copy"); shortcutText: "Ctrl+C"; onTriggered: root.report(root.tl.copySelected()) }
        AppMenuItem { text: qsTr("Paste at Playhead"); shortcutText: "Ctrl+V"; enabled: root.tl.canPaste; onTriggered: root.report(root.tl.paste(Projects.playheadFrame)) }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Group"); shortcutText: "Ctrl+G"; enabled: root.tl.selectedCount > 1; onTriggered: root.report(root.tl.groupSelected()) }
        AppMenuItem { text: qsTr("Ungroup"); shortcutText: "Ctrl+Shift+G"; onTriggered: root.report(root.tl.ungroupSelected()) }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PanelHeader {
            Layout.fillWidth: true
            title: qsTr("Timeline")
            iconName: "layers"

            IconButton { iconName: "undo"; enabled: root.tl.canUndo; tip: qsTr("Undo") + "  (Ctrl+Z)"; onClicked: root.tl.undo() }
            IconButton { iconName: "redo"; enabled: root.tl.canRedo; tip: qsTr("Redo") + "  (Ctrl+Shift+Z)"; onClicked: root.tl.redo() }
            IconButton { iconName: "scissors"; enabled: root.tl.clipCount > 0; tip: qsTr("Split at Playhead") + "  (Ctrl+K)"; onClicked: root.splitAtPlayhead() }
            IconButton { iconName: "trash"; enabled: root.tl.selectedCount > 0; tip: qsTr("Delete") + "  (Del)"; onClicked: root.report(root.tl.deleteSelected()) }
            Rectangle { width: 1; height: 18; color: Theme.border }
            Repeater {
                model: [ { t: "select", k: "V", n: qsTr("Select") }, { t: "ripple", k: "B", n: qsTr("Ripple trim") },
                         { t: "roll", k: "N", n: qsTr("Roll edit") }, { t: "slip", k: "Y", n: qsTr("Slip") }, { t: "slide", k: "U", n: qsTr("Slide") } ]
                delegate: AppButton {
                    required property var modelData
                    objectName: "tool_" + modelData.t
                    text: modelData.k
                    implicitWidth: 28; implicitHeight: 26; leftPadding: 0; rightPadding: 0
                    variant: root.tool === modelData.t ? "primary" : "ghost"
                    onClicked: root.toolRequested(modelData.t)
                    AppToolTip { visible: parent.hovered; text: parent.modelData.n + "  (" + parent.modelData.k + ")" }
                }
            }
            Rectangle { width: 1; height: 18; color: Theme.border }
            IconButton { objectName: "addMarker"; iconName: "bolt"; enabled: root.tl.clipCount > 0; tip: qsTr("Add marker") + "  (M)"; onClicked: root.addMarkerAtPlayhead() }
            IconButton { objectName: "addMenu"; iconName: "plus"; enabled: Projects.isOpen; tip: qsTr("Add titles, subtitles and tracks"); onClicked: addMenu.popup(this, 0, height) }
            Rectangle { width: 1; height: 18; color: Theme.border }
            IconButton {
                iconName: "magnet"
                checkable: true
                checked: AppSettings.values.snapping
                tip: qsTr("Snapping")
                onToggled: AppSettings.set("snapping", checked)
            }
            IconButton { iconName: "zoomOut"; tip: qsTr("Zoom out") + "  (-)"; onClicked: root.zoomRequested(Math.max(0.25, root.pxPerFrame * 0.8)) }
            AppSlider {
                implicitWidth: 110
                from: Math.log(0.25)
                to: Math.log(40)
                value: Math.log(root.pxPerFrame)
                onMoved: root.zoomRequested(Math.exp(value))
                Accessible.name: qsTr("Timeline zoom")
            }
            IconButton { iconName: "zoomIn"; tip: qsTr("Zoom in") + "  (+)"; onClicked: root.zoomRequested(Math.min(40, root.pxPerFrame * 1.25)) }
            IconButton {
                iconName: "fit"
                tip: qsTr("Fit timeline")
                onClicked: {
                    const dur = Math.max(root.tl.durationFrames, root.fps * 5)
                    root.zoomRequested(Math.max(0.25, Math.min(40, (lanes.width - 40) / dur)))
                    lanes.contentX = 0
                }
            }
        }

        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true

            // ---- Track header column ---------------------------------------------------------
            Rectangle {
                id: headerColumn
                width: root.headerWidth
                height: parent.height
                color: Theme.backgroundAlt

                Rectangle {
                    id: corner
                    width: parent.width
                    height: root.rulerHeight
                    color: Theme.backgroundAlt
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 14
                        text: qsTr("Tracks")
                        font.pixelSize: Theme.fontXs
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.8
                        color: Theme.textDisabled
                    }
                }

                Item {
                    y: root.rulerHeight
                    width: parent.width
                    height: parent.height - root.rulerHeight
                    clip: true

                    Column {
                        y: -lanes.contentY
                        width: parent.width

                        Repeater {
                            model: Projects.tracks
                            delegate: Rectangle {
                                id: header
                                required property int index
                                required property string name
                                required property string kind
                                required property bool muted
                                required property bool locked
                                required property bool trackVisible
                                required property bool solo

                                width: root.headerWidth
                                height: root.laneHeight
                                color: "transparent"

                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton
                                    onClicked: { trackMenu.row = header.index; trackMenu.popup() }
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 4
                                    spacing: 2

                                    Rectangle {
                                        width: 4; height: 22; radius: 2
                                        color: header.kind === "video" ? Theme.info : header.kind === "subtitle" ? Theme.warning : Theme.success
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 6
                                        text: header.name
                                        font.pixelSize: Theme.fontMd
                                        font.weight: Font.DemiBold
                                        color: Theme.text
                                        elide: Text.ElideRight
                                    }
                                    IconButton {
                                        objectName: "trackToggle" + header.index
                                        iconSize: 15
                                        implicitWidth: 26
                                        implicitHeight: 26
                                        checkable: true
                                        checked: header.kind !== "audio" ? !header.trackVisible : header.muted
                                        iconName: header.kind !== "audio" ? (header.trackVisible ? "eye" : "eyeOff")
                                                                          : (header.muted ? "volumeOff" : "volume")
                                        tip: header.kind !== "audio" ? qsTr("Toggle track visibility") : qsTr("Mute track")
                                        onClicked: Projects.tracks.toggle(header.index, header.kind !== "audio" ? "visible" : "muted")
                                    }
                                    IconButton {
                                        objectName: "trackLock" + header.index
                                        iconSize: 15
                                        implicitWidth: 26
                                        implicitHeight: 26
                                        checkable: true
                                        checked: header.locked
                                        iconName: header.locked ? "lock" : "unlock"
                                        tip: qsTr("Lock track")
                                        onClicked: Projects.tracks.toggle(header.index, "locked")
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.borderSubtle }
            }

            // ---- Ruler + lanes ----------------------------------------------------------------
            Item {
                id: scrollArea
                x: root.headerWidth
                width: parent.width - root.headerWidth
                height: parent.height
                clip: true

                Flickable {
                    id: lanes
                    y: root.rulerHeight
                    width: parent.width
                    height: parent.height - root.rulerHeight
                    contentWidth: root.totalFrames * root.pxPerFrame
                    contentHeight: Projects.tracks.count * root.laneHeight
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    flickableDirection: Flickable.HorizontalAndVerticalFlick
                    interactive: false          // clips use the mouse; scrolling is by wheel and scroll bars
                    ScrollBar.horizontal: AppScrollBar {}
                    ScrollBar.vertical: AppScrollBar {}

                    onContentXChanged: ruler.requestPaint()

                    WheelHandler {
                        acceptedModifiers: Qt.NoModifier
                        onWheel: event => {
                            if (lanes.contentHeight > lanes.height && Math.abs(event.angleDelta.y) >= Math.abs(event.angleDelta.x))
                                lanes.contentY = Math.max(0, Math.min(lanes.contentHeight - lanes.height, lanes.contentY - event.angleDelta.y / 2))
                            else
                                lanes.contentX = Math.max(0, Math.min(lanes.contentWidth - lanes.width, lanes.contentX - (event.angleDelta.x || event.angleDelta.y)))
                        }
                    }
                    WheelHandler {
                        acceptedModifiers: Qt.ControlModifier
                        onWheel: event => root.zoomRequested(Math.max(0.25, Math.min(40, root.pxPerFrame * (event.angleDelta.y > 0 ? 1.15 : 0.87))))
                    }
                    WheelHandler {
                        acceptedModifiers: Qt.ShiftModifier
                        onWheel: event => lanes.contentX = Math.max(0, Math.min(lanes.contentWidth - lanes.width, lanes.contentX - (event.angleDelta.y || event.angleDelta.x)))
                    }

                    // lane backgrounds
                    Column {
                        width: lanes.contentWidth
                        Repeater {
                            model: Projects.tracks
                            delegate: Rectangle {
                                required property int index
                                required property string kind
                                required property bool muted
                                required property bool trackVisible
                                required property bool locked
                                width: lanes.contentWidth
                                height: root.laneHeight
                                color: kind === "video" ? Theme.videoTrack : kind === "subtitle" ? Theme.subtitleTrack : Theme.audioTrack
                                opacity: (kind !== "audio" ? trackVisible : !muted) ? 1 : 0.45
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
                                Rectangle { anchors.fill: parent; visible: locked; color: Qt.alpha(Theme.background, 0.35) }
                            }
                        }
                    }

                    // clips
                    Item {
                        id: clipLayer
                        width: lanes.contentWidth
                        height: lanes.contentHeight

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            onClicked: { root.tl.clearSelection(); App.selectedMediaId = "" }
                        }

                        Repeater {
                            model: Projects.timeline
                            delegate: TimelineClip {
                                pxPerFrame: root.pxPerFrame
                                laneHeight: root.laneHeight
                                surface: clipLayer
                                snapEnabled: AppSettings.values.snapping
                                tool: root.tool
                                onContextRequested: clipMenu.popup()
                            }
                        }

                        // Where a dragged media item would land
                        Rectangle {
                            id: dropGhost
                            visible: false
                            radius: 4
                            color: Qt.alpha(Theme.accent, 0.35)
                            border.color: Theme.accent
                            border.width: 2
                            z: 200
                        }
                        DropArea {
                            anchors.fill: parent
                            keys: ["dvmedia"]

                            function target(drag) {
                                const id = drag.source.mediaId
                                const kind = root.primaryKind(id)
                                const laneIdx = Math.floor(drag.y / root.laneHeight)
                                const track = (laneIdx >= 0 && laneIdx < root.tl.trackCount() && root.tl.trackKind(laneIdx) === kind
                                               && !root.tl.trackLocked(laneIdx)) ? laneIdx : root.tl.trackIndexForKind(kind)
                                const frames = root.tl.mediaFrames(id)
                                const want = Math.max(0, Math.round(drag.x / root.pxPerFrame))
                                return { id: id, track: track, frames: frames, start: root.tl.firstFreeStart(track, want, frames), want: want }
                            }
                            onPositionChanged: drag => {
                                const t = target(drag)
                                if (t.track < 0) {
                                    dropGhost.visible = false
                                    return
                                }
                                dropGhost.x = t.start * root.pxPerFrame
                                dropGhost.y = t.track * root.laneHeight + 2
                                dropGhost.width = Math.max(4, t.frames * root.pxPerFrame)
                                dropGhost.height = root.laneHeight - 4
                                dropGhost.visible = true
                            }
                            onExited: dropGhost.visible = false
                            onDropped: drop => {
                                dropGhost.visible = false
                                const t = target(drop)
                                drop.accept()
                                root.report(root.tl.addMedia(t.id, t.track, t.want))
                            }
                        }
                    }
                }

                // Hint for an empty timeline (does not scroll)
                ColumnLayout {
                    visible: root.tl.clipCount === 0 && Projects.tracks.count > 0
                    anchors.horizontalCenter: lanes.horizontalCenter
                    anchors.verticalCenter: lanes.verticalCenter
                    width: Math.min(parent.width - 40, 440)
                    spacing: 4
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: qsTr("Drag media here to start editing")
                        font.pixelSize: Theme.fontLg
                        font.weight: Font.Medium
                        color: Theme.textSecondary
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: qsTr("or double-click a media item to add it at the playhead.")
                        font.pixelSize: Theme.fontSm
                        color: Theme.textDisabled
                    }
                }

                // silent passages found by the AI (before they are cut) and suggested cut points
                Repeater {
                    model: Ai.silences
                    delegate: Rectangle {
                        required property var modelData
                        y: root.rulerHeight
                        height: parent.height - root.rulerHeight
                        x: modelData.start * root.pxPerFrame - lanes.contentX
                        width: Math.max(2, (modelData.end - modelData.start) * root.pxPerFrame)
                        visible: x + width >= 0 && x <= scrollArea.width
                        color: Qt.alpha(Theme.warning, 0.22)
                        border.width: 1
                        border.color: Qt.alpha(Theme.warning, 0.7)
                        z: 30
                    }
                }
                Repeater {
                    model: Ai.suggestions
                    delegate: Rectangle {
                        required property var modelData
                        y: root.rulerHeight
                        height: parent.height - root.rulerHeight
                        x: modelData.frame * root.pxPerFrame - lanes.contentX
                        width: 2
                        visible: x >= 0 && x <= scrollArea.width
                        color: modelData.kind === "scene" ? Theme.info : Theme.warning
                        z: 31
                    }
                }

                // export range (In / Out) and markers over the lanes
                Rectangle {
                    visible: root.tl.inPoint >= 0 || root.tl.outPoint >= 0
                    y: root.rulerHeight
                    height: parent.height - root.rulerHeight
                    x: (root.tl.inPoint >= 0 ? root.tl.inPoint : 0) * root.pxPerFrame - lanes.contentX
                    width: ((root.tl.outPoint >= 0 ? root.tl.outPoint + 1 : root.tl.durationFrames) - (root.tl.inPoint >= 0 ? root.tl.inPoint : 0)) * root.pxPerFrame
                    color: Qt.alpha(Theme.accent, 0.10)
                    border.width: 0
                    z: 0
                }
                Repeater {
                    model: root.tl.markers
                    delegate: Rectangle {
                        required property var modelData
                        x: modelData.frame * root.pxPerFrame - lanes.contentX
                        y: root.rulerHeight
                        width: 1
                        height: parent.height - root.rulerHeight
                        visible: x >= 0 && x <= scrollArea.width
                        color: Qt.alpha(modelData.color, 0.7)
                        z: 40
                    }
                }

                // Ruler
                Canvas {
                    id: ruler
                    width: parent.width
                    height: root.rulerHeight
                    renderTarget: Canvas.Image

                    Connections {
                        target: root
                        function onPxPerFrameChanged() { ruler.requestPaint() }
                        function onFpsChanged() { ruler.requestPaint() }
                    }
                    Connections {
                        target: Theme
                        function onDarkChanged() { ruler.requestPaint() }
                    }
                    Connections { target: root.tl; function onClipsChanged() { ruler.requestPaint() } }
                    Connections { target: root.tl; function onMarkersChanged() { ruler.requestPaint() } }
                    Connections { target: root.tl; function onRangeChanged() { ruler.requestPaint() } }
                    onWidthChanged: requestPaint()

                    function stepFrames() {
                        const f = root.fps
                        const candidates = [1, 2, 5, 10, 15, f, 2 * f, 5 * f, 10 * f, 15 * f, 30 * f, 60 * f,
                                            120 * f, 300 * f, 600 * f, 1800 * f]
                        for (let i = 0; i < candidates.length; ++i) {
                            if (candidates[i] * root.pxPerFrame >= 96)
                                return candidates[i]
                        }
                        return candidates[candidates.length - 1]
                    }

                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.fillStyle = Theme.backgroundAlt
                        ctx.fillRect(0, 0, width, height)
                        ctx.font = "11px 'Inter', 'Noto Sans Thai', sans-serif"
                        ctx.textBaseline = "middle"

                        const step = stepFrames()
                        const minor = step >= 4 ? step / 4 : step
                        const first = Math.floor(lanes.contentX / root.pxPerFrame / minor) * minor
                        const last = (lanes.contentX + width) / root.pxPerFrame
                        ctx.lineWidth = 1
                        for (let f = first; f <= last; f += minor) {
                            const x = Math.round(f * root.pxPerFrame - lanes.contentX) + 0.5
                            const major = Math.abs(f % step) < 0.001
                            ctx.strokeStyle = major ? Theme.textSecondary : Theme.border
                            ctx.beginPath()
                            ctx.moveTo(x, major ? height * 0.45 : height * 0.72)
                            ctx.lineTo(x, height)
                            ctx.stroke()
                            if (major) {
                                ctx.fillStyle = Theme.textSecondary
                                ctx.fillText(Projects.timecodeFor(Math.round(f)), x + 9, height * 0.3)
                            }
                        }
                        // In / Out range
                        if (root.tl.inPoint >= 0 || root.tl.outPoint >= 0) {
                            const a = (root.tl.inPoint >= 0 ? root.tl.inPoint : 0) * root.pxPerFrame - lanes.contentX
                            const b = (root.tl.outPoint >= 0 ? root.tl.outPoint + 1 : root.tl.durationFrames) * root.pxPerFrame - lanes.contentX
                            ctx.fillStyle = Qt.alpha(Theme.accent, 0.25)
                            ctx.fillRect(a, height - 6, Math.max(2, b - a), 6)
                        }
                        // markers
                        const marks = root.tl.markers
                        for (let i = 0; i < marks.length; ++i) {
                            const mx = marks[i].frame * root.pxPerFrame - lanes.contentX
                            if (mx < -40 || mx > width)
                                continue
                            ctx.fillStyle = marks[i].color
                            ctx.beginPath()
                            ctx.moveTo(mx - 5, 2); ctx.lineTo(mx + 5, 2); ctx.lineTo(mx + 5, 9); ctx.lineTo(mx, 14); ctx.lineTo(mx - 5, 9)
                            ctx.closePath()
                            ctx.fill()
                            if (marks[i].name !== "") {
                                ctx.fillStyle = Theme.text
                                ctx.fillText(marks[i].name, mx + 8, 9)
                            }
                        }
                        // end of the sequence
                        const endX = root.tl.durationFrames * root.pxPerFrame - lanes.contentX
                        if (root.tl.durationFrames > 0 && endX >= 0 && endX <= width) {
                            ctx.fillStyle = Theme.accent
                            ctx.fillRect(Math.round(endX) - 1, height - 6, 2, 6)
                        }
                        ctx.strokeStyle = Theme.borderSubtle
                        ctx.beginPath()
                        ctx.moveTo(0, height - 0.5)
                        ctx.lineTo(width, height - 0.5)
                        ctx.stroke()
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: mouse => {
                            const m = root.markerAtX(mouse.x)
                            if (mouse.button === Qt.RightButton) {
                                if (m) {
                                    root.markerId = m.id
                                    root.markerName = m.name
                                    markerMenu.popup()
                                }
                                return
                            }
                            Projects.playheadFrame = m ? m.frame : root.frameAtViewportX(mouse.x)
                        }
                        onPositionChanged: mouse => { if (pressed && (mouse.buttons & Qt.LeftButton)) Projects.playheadFrame = root.frameAtViewportX(mouse.x) }
                    }
                }

                // Playhead
                Item {
                    id: playhead
                    readonly property real px: Projects.playheadFrame * root.pxPerFrame - lanes.contentX
                    visible: px >= -1 && px <= scrollArea.width
                    x: Math.round(px)
                    width: 1
                    height: parent.height
                    z: 50

                    Rectangle { x: 0; width: 2; height: parent.height; color: Theme.accent }
                    Rectangle {
                        x: -6
                        y: 0
                        width: 14
                        height: 14
                        radius: 3
                        color: Theme.accent
                    }
                }
            }
        }
    }
}
