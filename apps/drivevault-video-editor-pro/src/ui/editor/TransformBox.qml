import QtQuick
import DriveVault.Backend

// Direct manipulation of a clip in the program monitor: click the picture to select the clip under the pointer, then drag inside the
// frame to move it, drag a corner to scale it, drag the round handle above it to rotate it. Everything goes through the same clip
// parameters as the inspector (Position X / Y, Scale, Rotation), so it is undoable and exported exactly as shown.
Item {
    id: root

    required property Item frame                     // the monitor picture: its size is the canvas
    signal fullscreenRequested()

    readonly property real canvasW: Math.max(1, frame.width)
    readonly property real canvasH: Math.max(1, frame.height)
    readonly property string clipId: { rev; return Projects.timeline.selectedClipOfKind("video") }
    property int rev: 0
    Connections {
        target: Projects.timeline
        function onClipsChanged() { root.rev++ }
        function onSelectionChanged() { root.rev++ }
    }
    Connections {
        target: Projects
        function onPlayheadChanged() { root.rev++ }
    }

    // frame pixels of a clip: centre, size and rotation (the compositor fits the picture into the canvas, then moves / rotates / scales it)
    function geometryOf(id) {
        rev
        if (id === "")
            return null
        const info = Projects.timeline.clipInfo(id)
        const f = Math.max(info.start, Math.min(info.start + info.duration - 1, Projects.playheadFrame))
        let aw = canvasW, ah = canvasH
        if (info.type !== "title") {
            const m = Projects.media.describe(info.mediaId)
            if (m.width > 0 && m.height > 0) { aw = m.width; ah = m.height }
        }
        const srcAspect = aw / ah
        const fitW = srcAspect > canvasW / canvasH ? canvasW : canvasH * srcAspect
        const fitH = srcAspect > canvasW / canvasH ? canvasW / srcAspect : canvasH
        const scale = Projects.timeline.paramValue(id, "scale", f)
        return {
            id: id, frame: f,
            cx: canvasW / 2 + Projects.timeline.paramValue(id, "posX", f) * canvasW,
            cy: canvasH / 2 + Projects.timeline.paramValue(id, "posY", f) * canvasH,
            w: fitW * scale, h: fitH * scale,
            rot: Projects.timeline.paramValue(id, "rotation", f),
            locked: false
        }
    }
    readonly property var geom: geometryOf(clipId)

    // the video clip under a point of the frame (topmost video track first)
    function clipAtPoint(px, py) {
        const f = Projects.playheadFrame
        for (let row = 0; row < Projects.tracks.count; ++row) {
            if (Projects.timeline.trackKind(row) !== "video")
                continue
            const id = Projects.timeline.clipAt(row, f)
            if (id === "")
                continue
            const g = geometryOf(id)
            if (!g)
                continue
            const a = -g.rot * Math.PI / 180
            const dx = px - g.cx, dy = py - g.cy
            const lx = dx * Math.cos(a) - dy * Math.sin(a)
            const ly = dx * Math.sin(a) + dy * Math.cos(a)
            if (Math.abs(lx) <= g.w / 2 && Math.abs(ly) <= g.h / 2)
                return id
        }
        return ""
    }

    function setParam(key, value) {
        const g = geom
        if (!g)
            return
        const err = Projects.timeline.setParam(g.id, key, value, g.frame)
        if (err !== "")
            App.toast(err, "warning")
    }

    // click on the picture: select the clip under the pointer (nothing there: clear the selection)
    MouseArea {
        id: pick
        objectName: "monitorPick"
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: mouse => {
            const id = root.clipAtPoint(mouse.x, mouse.y)
            if (id !== "")
                Projects.timeline.select(id)
            else
                Projects.timeline.clearSelection()
        }
        onDoubleClicked: root.fullscreenRequested()
    }

    Item {
        id: box
        objectName: "transformBox"
        visible: root.geom !== null && !Playback.playing && Projects.isOpen
        readonly property var g: root.geom
        x: g ? g.cx - g.w / 2 : 0
        y: g ? g.cy - g.h / 2 : 0
        width: g ? g.w : 0
        height: g ? g.h : 0
        rotation: g ? g.rot : 0
        transformOrigin: Item.Center

        Rectangle { anchors.fill: parent; color: "transparent"; border.width: 2; border.color: Theme.accent }

        // move
        MouseArea {
            id: moveArea
            objectName: "tbMove"
            anchors.fill: parent
            cursorShape: Qt.SizeAllCursor
            property real startX: 0
            property real startY: 0
            property real posX0: 0
            property real posY0: 0
            onPressed: mouse => {
                const p = mapToItem(root, mouse.x, mouse.y)
                startX = p.x; startY = p.y
                posX0 = Projects.timeline.paramValue(box.g.id, "posX", box.g.frame)
                posY0 = Projects.timeline.paramValue(box.g.id, "posY", box.g.frame)
            }
            onPositionChanged: mouse => {
                if (!pressed)
                    return
                const p = mapToItem(root, mouse.x, mouse.y)
                let nx = posX0 + (p.x - startX) / root.canvasW
                let ny = posY0 + (p.y - startY) / root.canvasH
                if (Math.abs(nx) < 0.008) nx = 0                      // snap to the centre
                if (Math.abs(ny) < 0.008) ny = 0
                root.setParam("posX", Math.round(nx * 1000) / 1000)
                root.setParam("posY", Math.round(ny * 1000) / 1000)
            }
        }

        // scale: the four corners
        Repeater {
            model: [ { fx: 0, fy: 0, cursor: Qt.SizeFDiagCursor }, { fx: 1, fy: 0, cursor: Qt.SizeBDiagCursor },
                     { fx: 0, fy: 1, cursor: Qt.SizeBDiagCursor }, { fx: 1, fy: 1, cursor: Qt.SizeFDiagCursor } ]
            delegate: Rectangle {
                id: corner
                required property var modelData
                required property int index
                objectName: "tbCorner" + index
                width: 14; height: 14; radius: 3
                x: modelData.fx * box.width - width / 2
                y: modelData.fy * box.height - height / 2
                color: "#FFFFFF"
                border.width: 2
                border.color: Theme.accent
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: corner.modelData.cursor
                    property real d0: 1
                    property real s0: 1
                    onPressed: mouse => {
                        const p = mapToItem(root, mouse.x, mouse.y)
                        d0 = Math.max(4, Math.hypot(p.x - box.g.cx, p.y - box.g.cy))
                        s0 = Projects.timeline.paramValue(box.g.id, "scale", box.g.frame)
                    }
                    onPositionChanged: mouse => {
                        if (!pressed)
                            return
                        const p = mapToItem(root, mouse.x, mouse.y)
                        const d = Math.hypot(p.x - box.g.cx, p.y - box.g.cy)
                        const s = Math.max(0.05, Math.min(8, s0 * d / d0))
                        root.setParam("scale", Math.round(s * 100) / 100)
                    }
                }
            }
        }

        // rotate: the round handle above the top edge
        Item {
            x: box.width / 2
            y: -30
            Rectangle { x: -0.5; y: 6; width: 1; height: 24; color: Theme.accent }
            Rectangle {
                id: rotateHandle
                objectName: "tbRotate"
                x: -8; y: -8
                width: 16; height: 16; radius: 8
                color: "#FFFFFF"
                border.width: 2
                border.color: Theme.accent
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.CrossCursor
                    onPositionChanged: mouse => {
                        if (!pressed)
                            return
                        const p = mapToItem(root, mouse.x, mouse.y)
                        let a = Math.atan2(p.x - box.g.cx, -(p.y - box.g.cy)) * 180 / Math.PI      // 0 = the handle straight above the centre
                        for (const s of [-180, -135, -90, -45, 0, 45, 90, 135, 180])                // snap to 45 degrees
                            if (Math.abs(a - s) < 3) a = s
                        root.setParam("rotation", Math.round(a * 10) / 10)
                    }
                }
            }
        }
    }
}
