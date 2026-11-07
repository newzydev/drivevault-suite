import QtQuick
import DriveVault.Backend

// The level line of a clip ("rubber band"): shown while the mouse is over the clip. Dragging it up or down changes the opacity of a
// video clip or the volume of an audio clip (the same parameters as the inspector, one undo step per change). Presses away from the
// line fall through to the clip (select / move / trim).
Item {
    id: root

    required property string clipId
    required property string paramKey          // "opacity" | "volume"
    property int startFrame: 0
    property int durationFrames: 1
    property bool shown: false
    property bool editable: true

    readonly property var info: Projects.timeline.paramInfo(paramKey)
    property int rev: 0
    Connections {
        target: Projects.timeline
        function onClipsChanged() { root.rev++ }
    }
    // the value at the playhead (inside the clip); animated parameters are changed at that frame like in the inspector
    readonly property int frame: Math.max(startFrame, Math.min(startFrame + durationFrames - 1, Projects.playheadFrame))
    readonly property real value: { rev; return Projects.timeline.paramValue(clipId, paramKey, frame) }
    readonly property real fraction: Math.max(0, Math.min(1, (value - info.min) / (info.max - info.min)))
    readonly property real pad: 5
    readonly property real lineY: pad + (1 - fraction) * Math.max(1, height - 2 * pad)
    property bool dragging: false
    readonly property bool near: area.containsMouse && Math.abs(area.mouseY - lineY) <= 8

    visible: (shown || dragging) && width > 16 && height > 12

    function label(v) { return paramKey === "volume" ? v.toFixed(1) + " dB" : Math.round(v * 100) + " %" }
    function setFromY(y) {
        const f = 1 - Math.max(0, Math.min(1, (y - pad) / Math.max(1, height - 2 * pad)))
        let v = info.min + f * (info.max - info.min)
        v = paramKey === "volume" ? Math.round(v * 2) / 2 : Math.round(v * 100) / 100       // 0.5 dB / 1 % steps
        const err = Projects.timeline.setParam(clipId, paramKey, v, frame)
        if (err !== "")
            App.toast(err, "warning")
    }

    Rectangle {                                               // the line
        x: 0
        y: root.lineY - 1
        width: parent.width
        height: root.near || root.dragging ? 3 : 2
        color: root.dragging ? Theme.accentHover : "#FFFFFF"
        opacity: root.near || root.dragging ? 1 : 0.75
    }
    Rectangle {                                               // handle
        x: 6
        y: root.lineY - 5
        width: 10; height: 10; radius: 5
        color: Theme.accent
        border.width: 1
        border.color: "#FFFFFF"
    }
    Rectangle {                                               // value while the mouse is on the line
        visible: root.near || root.dragging
        x: Math.max(4, Math.min(root.width - width - 4, area.mouseX + 12))
        y: Math.max(0, Math.min(root.height - height, root.lineY - height - 6))
        width: levelText.implicitWidth + 12
        height: 18
        radius: 4
        color: Theme.surfaceElevated
        border.width: 1
        border.color: Theme.border
        Text {
            id: levelText
            anchors.centerIn: parent
            text: root.label(root.value)
            font.pixelSize: Theme.fontXs
            font.features: { "tnum": 1 }
            color: Theme.text
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.editable
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        cursorShape: root.near || root.dragging ? Qt.SizeVerCursor : Qt.ArrowCursor
        onPressed: mouse => {
            mouse.accepted = Math.abs(mouse.y - root.lineY) <= 8        // away from the line: the clip handles the click
            if (mouse.accepted)
                root.dragging = true
        }
        onPositionChanged: mouse => { if (root.dragging) root.setFromY(mouse.y) }
        onReleased: root.dragging = false
        onCanceled: root.dragging = false
    }
}
