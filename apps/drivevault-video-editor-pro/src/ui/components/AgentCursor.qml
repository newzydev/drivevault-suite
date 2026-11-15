import QtQuick

// A virtual mouse cursor with a caption, shown while an AI client (Claude Code / Codex over MCP) works on the project. It only draws: it never takes
// mouse input and never edits anything itself. act(x, y, text) moves it to a point of the window; it fades out when nothing happens for a while.
Item {
    id: root

    property string caption: ""
    property real targetX: 0
    property real targetY: 0
    property bool animate: true              // false = jump (used for the first appearance)
    readonly property int travelMs: Theme.animated ? 480 : 0

    visible: opacity > 0
    opacity: 0
    z: 3000
    enabled: false                           // never takes the mouse

    function act(px, py, text) {
        if (text !== undefined)
            caption = text
        if (px === undefined || px === null) {             // no new place: only the caption changes
            if (opacity > 0)
                idleTimer.restart()
            else {
                x = parent.width / 2
                y = parent.height / 2
                opacity = 1
                idleTimer.restart()
            }
            return
        }
        if (opacity === 0) {
            animate = false                                 // appear near the target, then glide the last bit
            x = Math.max(0, px - 140)
            y = Math.max(0, py - 90)
            animate = true
        }
        opacity = 1
        x = px
        y = py
        clickTimer.restart()
        idleTimer.restart()
    }

    Behavior on x { enabled: root.animate; NumberAnimation { duration: root.travelMs; easing.type: Easing.OutCubic } }
    Behavior on y { enabled: root.animate; NumberAnimation { duration: root.travelMs; easing.type: Easing.OutCubic } }
    Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }

    Timer { id: idleTimer; interval: 2600; onTriggered: root.opacity = 0 }
    Timer { id: clickTimer; interval: root.travelMs; onTriggered: pulse.restart() }

    // the click ring when the cursor has arrived
    Rectangle {
        id: ring
        width: 28; height: 28; radius: 14
        x: -14; y: -14
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        opacity: 0
        SequentialAnimation {
            id: pulse
            ParallelAnimation {
                NumberAnimation { target: ring; property: "scale"; from: 0.3; to: 1.5; duration: 380; easing.type: Easing.OutQuad }
                SequentialAnimation {
                    NumberAnimation { target: ring; property: "opacity"; from: 0.9; to: 0.9; duration: 1 }
                    NumberAnimation { target: ring; property: "opacity"; to: 0; duration: 379 }
                }
            }
        }
    }

    // the arrow (tip at the item's origin)
    Canvas {
        id: arrow
        width: 20; height: 26
        onPaint: {
            const c = getContext("2d")
            c.reset()
            c.beginPath()
            c.moveTo(1, 1)
            c.lineTo(1, 20)
            c.lineTo(6, 15.5)
            c.lineTo(9.5, 23)
            c.lineTo(13, 21.5)
            c.lineTo(9.6, 14.2)
            c.lineTo(16, 14.2)
            c.closePath()
            c.fillStyle = Theme.accent
            c.fill()
            c.lineWidth = 1.6
            c.strokeStyle = "#FFFFFF"
            c.lineJoin = "round"
            c.stroke()
        }
        Connections { target: Theme; function onAccentChanged() { arrow.requestPaint() } }
    }

    // what it is doing
    Rectangle {
        id: chip
        readonly property bool flip: root.x + 24 + width > (root.parent ? root.parent.width : 0) - 8
        x: flip ? -width - 4 : 20
        y: 22
        width: Math.min(360, chipText.implicitWidth + 24)
        height: chipText.implicitHeight + 12
        radius: height / 2
        color: Theme.accent
        border.color: Qt.alpha("#FFFFFF", 0.35)
        visible: root.caption !== ""
        AppText {
            id: chipText
            anchors.centerIn: parent
            width: parent.width - 24
            text: root.caption
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSm
            font.weight: Font.DemiBold
            color: Theme.textOnAccent
        }
    }
}
