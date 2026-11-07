import QtQuick
import QtQuick.Controls
import DriveVault.Backend

// One clip on the timeline. Drag the body to move (snapping, other track of the same kind), drag the
// edges to trim. The change is committed to the timeline model (undoable) when the mouse is released.
Item {
    id: clipRoot
    objectName: "clip_" + clipId

    required property string clipId
    required property string mediaId
    required property int trackIndex
    required property real startFrame
    required property real durationFrames
    required property real sourceInFrame
    required property string name
    required property string kind
    required property bool selected
    required property bool offline
    required property string clipType
    required property string transIn
    required property string transOut
    required property bool animated
    property string tool: "select"          // select | ripple | roll | slip | slide

    property real pxPerFrame: 4
    property var panel                      // the TimelinePanel: lane geometry of every track
    readonly property int laneHeight: panel ? panel.heightOf(shownTrack) : 46
    property Item surface
    property bool snapEnabled: true
    signal contextRequested(string clipId)

    // provisional geometry while the user drags
    property bool dragging: false
    property bool trimming: false
    property real dragStart: startFrame
    property int dragTrack: trackIndex
    property real trimFrom: startFrame
    property real trimTo: startFrame + durationFrames
    property string thumb: ""

    readonly property real shownStart: dragging ? dragStart : trimming ? trimFrom : startFrame
    readonly property real shownEnd: dragging ? dragStart + durationFrames : trimming ? trimTo : startFrame + durationFrames
    readonly property int shownTrack: dragging ? dragTrack : trackIndex
    readonly property int snapTolerance: Math.max(2, Math.round(8 / pxPerFrame))

    x: shownStart * pxPerFrame
    y: (panel ? panel.topOf(shownTrack) : shownTrack * laneHeight) + 2
    width: Math.max(3, (shownEnd - shownStart) * pxPerFrame)
    height: laneHeight - 4
    z: dragging || trimming ? 100 : selected ? 5 : 1
    opacity: dragging ? 0.85 : 1

    function refreshThumb() { thumb = kind === "video" && clipType !== "title" ? Projects.media.thumbnailUrl(mediaId) : "" }
    Component.onCompleted: refreshThumb()
    Connections { target: Projects.media; function onLibraryChanged() { clipRoot.refreshThumb() } }

    Rectangle {
        id: body
        anchors.fill: parent
        radius: 4
        color: clipRoot.clipType === "title" ? (clipRoot.kind === "subtitle" ? Theme.clipSubtitle : Theme.clipTitle)
               : clipRoot.kind === "audio" ? Theme.clipAudio : Theme.clipVideo
        border.width: clipRoot.selected ? 2 : 1
        border.color: clipRoot.selected ? Theme.accent : Qt.darker(color, 1.4)
        clip: true

        Image {
            anchors.fill: parent
            anchors.topMargin: 15
            visible: clipRoot.kind === "video" && clipRoot.clipType !== "title" && clipRoot.thumb !== ""
            source: clipRoot.thumb
            fillMode: Image.Tile
            horizontalAlignment: Image.AlignLeft
            verticalAlignment: Image.AlignVCenter
            sourceSize.height: Math.max(8, clipRoot.laneHeight - 19)
            opacity: 0.6
            asynchronous: true
        }
        WaveformItem {
            anchors.fill: parent
            anchors.topMargin: 14
            anchors.margins: 1
            visible: clipRoot.kind === "audio" && clipRoot.clipType !== "title"
            library: Projects.media
            mediaId: clipRoot.mediaId
            sourceStartUs: Projects.framesToMicroseconds(clipRoot.sourceInFrame + (clipRoot.shownStart - clipRoot.startFrame))
            spanUs: Projects.framesToMicroseconds(clipRoot.shownEnd - clipRoot.shownStart)
            color: Qt.alpha("#FFFFFF", 0.8)
        }

        Rectangle {
            width: parent.width
            height: 15
            color: "#66000000"
            Text {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: clipRoot.animated ? 18 : 4
                verticalAlignment: Text.AlignVCenter
                text: clipRoot.name
                elide: Text.ElideRight
                font.pixelSize: Theme.fontXs
                color: Theme.clipText
            }
            // keyframes on this clip
            Rectangle {
                visible: clipRoot.animated
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                width: 7; height: 7; rotation: 45
                color: Theme.warning
            }
        }

        // title clips show a T
        Text {
            visible: clipRoot.clipType === "title" && clipRoot.width > 40
            anchors.fill: parent
            anchors.topMargin: 15
            anchors.leftMargin: 8
            verticalAlignment: Text.AlignVCenter
            text: "T"
            font.pixelSize: Theme.fontLg
            font.weight: Font.Bold
            color: Qt.alpha(Theme.clipText, 0.55)
        }

        // transition badges
        Canvas {
            visible: clipRoot.transIn !== ""
            width: 14; height: 14
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            onPaint: { const c = getContext("2d"); c.reset(); c.fillStyle = "#F59E0B"; c.beginPath(); c.moveTo(0, 14); c.lineTo(14, 14); c.lineTo(0, 0); c.closePath(); c.fill() }
        }
        Canvas {
            visible: clipRoot.transOut !== ""
            width: 14; height: 14
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            onPaint: { const c = getContext("2d"); c.reset(); c.fillStyle = "#F59E0B"; c.beginPath(); c.moveTo(14, 14); c.lineTo(0, 14); c.lineTo(14, 0); c.closePath(); c.fill() }
        }

        // Missing media: hatched red overlay
        Rectangle {
            anchors.fill: parent
            visible: clipRoot.offline
            color: Qt.alpha(Theme.error, 0.45)
            Icon { anchors.centerIn: parent; visible: parent.width > 30; name: "warning"; size: 16; color: "#FFFFFF" }
        }
    }

    // ---- move ---------------------------------------------------------------------------------------
    MouseArea {
        id: bodyArea
        anchors.fill: parent
        anchors.leftMargin: 7
        anchors.rightMargin: 7
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        cursorShape: clipRoot.tool === "slip" ? Qt.SizeHorCursor : clipRoot.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor

        property real pressX: 0
        property real origStart: 0
        property bool moved: false
        property int toolDelta: 0

        onPressed: mouse => {
            App.selectedMediaId = ""
            if (mouse.button === Qt.RightButton) {
                if (!clipRoot.selected)
                    Projects.timeline.select(clipRoot.clipId, false)
                return
            }
            const additive = (mouse.modifiers & Qt.ControlModifier) || (mouse.modifiers & Qt.ShiftModifier)
            if (!clipRoot.selected || additive)
                Projects.timeline.select(clipRoot.clipId, additive)
            pressX = mapToItem(clipRoot.surface, mouse.x, 0).x
            origStart = clipRoot.startFrame
            moved = false
            toolDelta = 0
        }
        onPositionChanged: mouse => {
            if (!pressed || !(mouse.buttons & Qt.LeftButton))
                return
            const p = mapToItem(clipRoot.surface, mouse.x, mouse.y)
            if (!moved && Math.abs(p.x - pressX) < 4 && clipRoot.panel.rowAtY(p.y) === clipRoot.trackIndex)
                return
            moved = true
            const desired = origStart + Math.round((p.x - pressX) / clipRoot.pxPerFrame)
            if (clipRoot.tool === "slip" || clipRoot.tool === "slide") {
                toolDelta = desired - origStart
                if (clipRoot.tool === "slide") {
                    clipRoot.dragStart = desired
                    clipRoot.dragTrack = clipRoot.trackIndex
                    clipRoot.dragging = true
                }
                return
            }
            const track = clipRoot.panel.rowAtY(p.y)
            const r = Projects.timeline.clampMove(clipRoot.clipId, desired, track, clipRoot.snapEnabled, clipRoot.snapTolerance)
            clipRoot.dragStart = r.start
            clipRoot.dragTrack = r.trackIndex
            clipRoot.dragging = true
        }
        onReleased: mouse => finishMove()
        onCanceled: finishMove()
        onClicked: mouse => { if (mouse.button === Qt.RightButton) clipRoot.contextRequested(clipRoot.clipId) }

        function finishMove() {
            if (clipRoot.tool === "slip" || clipRoot.tool === "slide") {
                const delta = toolDelta
                toolDelta = 0
                clipRoot.dragging = false
                if (delta === 0)
                    return
                const e = clipRoot.tool === "slip" ? Projects.timeline.slip(clipRoot.clipId, -delta)
                                                    : Projects.timeline.slide(clipRoot.clipId, delta)
                if (e !== "")
                    App.toast(e, "warning")
                return
            }
            if (!clipRoot.dragging)
                return
            clipRoot.dragging = false
            const err = Projects.timeline.moveClip(clipRoot.clipId, clipRoot.dragStart, clipRoot.dragTrack)
            if (err !== "")
                App.toast(err, "warning")
        }
    }

    // ---- level line: opacity (video) / volume (audio), shown while the mouse is over the clip ---------------------------------------
    HoverHandler { id: clipHover }
    ClipLevelLine {
        objectName: "levelLine_" + clipRoot.clipId
        anchors.fill: parent
        clipId: clipRoot.clipId
        paramKey: clipRoot.kind === "audio" ? "volume" : "opacity"
        startFrame: clipRoot.startFrame
        durationFrames: clipRoot.durationFrames
        shown: clipHover.hovered && !clipRoot.dragging && !clipRoot.trimming
        editable: clipRoot.kind !== "subtitle" && !clipRoot.dragging && !clipRoot.trimming
        visible: clipRoot.kind !== "subtitle" && (shown || dragging)
    }

    // ---- trim handles -------------------------------------------------------------------------------------
    Repeater {
        model: 2
        delegate: MouseArea {
            id: handle
            required property int index
            objectName: (index === 0 ? "clipTrimStart_" : "clipTrimEnd_") + clipRoot.clipId
            readonly property bool isLeft: index === 0
            x: isLeft ? 0 : clipRoot.width - width
            width: 8
            height: clipRoot.height
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            z: 2

            Rectangle {
                anchors.fill: parent
                radius: 3
                color: Theme.accent
                opacity: handle.containsMouse || handle.pressed || (clipRoot.selected && clipRoot.width > 24) ? 0.9 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
            }

            property real pressX: 0
            property real edgeAtPress: 0

            onPressed: mouse => {
                App.selectedMediaId = ""
                if (!clipRoot.selected)
                    Projects.timeline.select(clipRoot.clipId, false)
                clipRoot.trimFrom = clipRoot.startFrame
                clipRoot.trimTo = clipRoot.startFrame + clipRoot.durationFrames
                clipRoot.trimming = true
                pressX = mapToItem(clipRoot.surface, mouse.x, 0).x
                edgeAtPress = isLeft ? clipRoot.startFrame : clipRoot.startFrame + clipRoot.durationFrames
            }
            onPositionChanged: mouse => {
                if (!pressed)
                    return
                // relative to where the edge was grabbed, so the edge does not jump to the pointer
                const frame = edgeAtPress + Math.round((mapToItem(clipRoot.surface, mouse.x, 0).x - pressX) / clipRoot.pxPerFrame)
                if (clipRoot.tool === "roll" || clipRoot.tool === "ripple") {
                    // these tools change the neighbours too: the preview only follows the pointer
                    if (isLeft)
                        clipRoot.trimFrom = Math.min(frame, clipRoot.startFrame + clipRoot.durationFrames - 1)
                    else
                        clipRoot.trimTo = Math.max(frame, clipRoot.startFrame + 1)
                } else if (isLeft)
                    clipRoot.trimFrom = Projects.timeline.clampTrimStart(clipRoot.clipId, frame, clipRoot.snapEnabled, clipRoot.snapTolerance)
                else
                    clipRoot.trimTo = Projects.timeline.clampTrimEnd(clipRoot.clipId, frame, clipRoot.snapEnabled, clipRoot.snapTolerance)
            }
            onReleased: finish()
            onCanceled: finish()

            function finish() {
                if (!clipRoot.trimming)
                    return
                clipRoot.trimming = false
                let err
                if (clipRoot.tool === "ripple")
                    err = isLeft ? Projects.timeline.rippleTrimStart(clipRoot.clipId, clipRoot.trimFrom)
                                 : Projects.timeline.rippleTrimEnd(clipRoot.clipId, clipRoot.trimTo)
                else if (clipRoot.tool === "roll") {
                    // the cut between this clip and its neighbour: the end of this clip, or the end of the previous one
                    const cutOwner = isLeft ? Projects.timeline.clipAt(clipRoot.trackIndex, clipRoot.startFrame - 1) : clipRoot.clipId
                    err = cutOwner === "" ? qsTr("There is no clip next to this edge to roll.")
                                          : Projects.timeline.rollEdit(cutOwner, isLeft ? clipRoot.trimFrom : clipRoot.trimTo)
                } else
                    err = isLeft ? Projects.timeline.trimStart(clipRoot.clipId, clipRoot.trimFrom)
                                 : Projects.timeline.trimEnd(clipRoot.clipId, clipRoot.trimTo)
                if (err !== "")
                    App.toast(err, "warning")
            }
        }
    }

    AppToolTip {
        visible: bodyArea.containsMouse && !clipRoot.dragging && !clipRoot.trimming
        delay: 800
        text: clipRoot.name + (clipRoot.offline ? "\n" + qsTr("Media file is missing") : "")
    }
}
