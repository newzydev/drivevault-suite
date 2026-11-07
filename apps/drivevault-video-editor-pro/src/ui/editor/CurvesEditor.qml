import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

// RGB / master tone curves. Click on the graph to add a point, drag to move, double-click a point (or right-click)
// to delete it. The first and the last point only move vertically.
ColumnLayout {
    id: root

    required property string clipId
    property int revision: 0
    property string channel: "master"
    property var points: [[0, 0], [1, 1]]
    property int dragIndex: -1

    Layout.fillWidth: true
    spacing: 6

    function load() {
        const stored = Projects.timeline.curve(clipId, channel)
        points = stored.length >= 2 ? stored : [[0, 0], [1, 1]]
        graph.requestPaint()
    }
    function commit() {
        const err = Projects.timeline.setCurve(clipId, channel, points)
        if (err !== "")
            App.toast(err, "warning")
    }
    onClipIdChanged: load()
    onChannelChanged: load()
    onRevisionChanged: if (dragIndex < 0) load()
    Component.onCompleted: load()

    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: [ { id: "master", label: qsTr("Luma"), c: Theme.text }, { id: "r", label: "R", c: "#EF4444" },
                     { id: "g", label: "G", c: "#22C55E" }, { id: "b", label: "B", c: "#3B82F6" } ]
            delegate: AppButton {
                required property var modelData
                objectName: "curveChannel_" + modelData.id
                text: modelData.label
                variant: root.channel === modelData.id ? "primary" : "ghost"
                implicitHeight: 26
                onClicked: root.channel = modelData.id
            }
        }
        Item { Layout.fillWidth: true }
        AppButton {
            text: qsTr("Reset")
            variant: "ghost"
            implicitHeight: 26
            enabled: root.points.length > 2 || root.points[0][1] !== 0 || root.points[1][1] !== 1
            onClicked: Projects.timeline.setCurve(root.clipId, root.channel, [])
        }
    }

    Canvas {
        id: graph
        objectName: "curveGraph"
        Layout.fillWidth: true
        Layout.preferredHeight: width
        Layout.maximumHeight: 260
        renderTarget: Canvas.Image
        Accessible.name: qsTr("Tone curve")

        readonly property real pad: 8
        function toX(v) { return pad + v * (width - 2 * pad) }
        function toY(v) { return height - pad - v * (height - 2 * pad) }
        function fromX(px) { return Math.max(0, Math.min(1, (px - pad) / (width - 2 * pad))) }
        function fromY(py) { return Math.max(0, Math.min(1, (height - pad - py) / (height - 2 * pad))) }

        Connections { target: Theme; function onDarkChanged() { graph.requestPaint() } }
        onWidthChanged: requestPaint()

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = Theme.surface
            ctx.fillRect(0, 0, width, height)
            ctx.strokeStyle = Theme.borderSubtle
            ctx.lineWidth = 1
            for (let i = 0; i <= 4; ++i) {
                const gx = Math.round(toX(i / 4)) + 0.5
                const gy = Math.round(toY(i / 4)) + 0.5
                ctx.beginPath(); ctx.moveTo(gx, pad); ctx.lineTo(gx, height - pad); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(pad, gy); ctx.lineTo(width - pad, gy); ctx.stroke()
            }
            ctx.strokeStyle = Theme.border
            ctx.beginPath(); ctx.moveTo(toX(0), toY(0)); ctx.lineTo(toX(1), toY(1)); ctx.stroke()

            const colour = root.channel === "r" ? "#EF4444" : root.channel === "g" ? "#22C55E" : root.channel === "b" ? "#3B82F6" : Theme.text
            const pts = root.points
            ctx.strokeStyle = colour
            ctx.lineWidth = 2
            ctx.beginPath()
            ctx.moveTo(toX(0), toY(pts[0][1]))
            for (let i = 0; i < pts.length; ++i)
                ctx.lineTo(toX(pts[i][0]), toY(pts[i][1]))
            ctx.lineTo(toX(1), toY(pts[pts.length - 1][1]))
            ctx.stroke()
            for (let i = 0; i < pts.length; ++i) {
                ctx.fillStyle = i === root.dragIndex ? Theme.accent : colour
                ctx.beginPath()
                ctx.arc(toX(pts[i][0]), toY(pts[i][1]), 5, 0, Math.PI * 2)
                ctx.fill()
            }
        }

        function hit(px, py) {
            for (let i = 0; i < root.points.length; ++i) {
                const dx = toX(root.points[i][0]) - px
                const dy = toY(root.points[i][1]) - py
                if (dx * dx + dy * dy <= 100)
                    return i
            }
            return -1
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: mouse => {
                let i = graph.hit(mouse.x, mouse.y)
                if (mouse.button === Qt.RightButton) {
                    if (i > 0 && i < root.points.length - 1) {
                        const p = root.points.slice()
                        p.splice(i, 1)
                        root.points = p
                        root.commit()
                        graph.requestPaint()
                    }
                    return
                }
                if (i < 0) {
                    const p = root.points.slice()
                    p.push([graph.fromX(mouse.x), graph.fromY(mouse.y)])
                    p.sort((a, b) => a[0] - b[0])
                    root.points = p
                    i = p.findIndex(q => Math.abs(q[0] - graph.fromX(mouse.x)) < 1e-9)
                    root.commit()
                }
                root.dragIndex = i
                graph.requestPaint()
            }
            onPositionChanged: mouse => {
                if (root.dragIndex < 0)
                    return
                const i = root.dragIndex
                const p = root.points.slice()
                const last = p.length - 1
                let x = (i === 0 || i === last) ? p[i][0] : graph.fromX(mouse.x)
                if (i > 0 && i < last)
                    x = Math.max(p[i - 1][0] + 0.01, Math.min(p[i + 1][0] - 0.01, x))
                p[i] = [x, graph.fromY(mouse.y)]
                root.points = p
                root.commit()
                graph.requestPaint()
            }
            onReleased: { root.dragIndex = -1; graph.requestPaint() }
            onDoubleClicked: mouse => {
                const i = graph.hit(mouse.x, mouse.y)
                if (i > 0 && i < root.points.length - 1) {
                    const p = root.points.slice()
                    p.splice(i, 1)
                    root.points = p
                    root.commit()
                    graph.requestPaint()
                }
            }
        }
    }
}
