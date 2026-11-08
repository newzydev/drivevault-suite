import QtQuick
import DriveVault.Backend

// A vertical fader (-60 .. +12 dB) with a dB scale; double-click resets to 0 dB.
Item {
    id: fader

    property real value: 0                  // dB
    property color tint: Theme.accent
    readonly property real minDb: -60
    readonly property real maxDb: 12
    readonly property var marks: [12, 6, 0, -6, -12, -24, -48, -60]
    signal moved(real db)

    function posOf(db) { return 1 - (Math.max(minDb, Math.min(maxDb, db)) - minDb) / (maxDb - minDb) }     // 0 = top
    function dbAt(y) { return Math.round((maxDb - Math.max(0, Math.min(1, (y - 10) / (height - 20))) * (maxDb - minDb)) * 2) / 2 }

    implicitWidth: 64
    implicitHeight: 160
    opacity: enabled ? 1 : 0.4

    Item {
        id: scale
        x: 0; y: 10; width: 22; height: parent.height - 20
        Repeater {
            model: fader.marks
            delegate: Text {
                required property int modelData
                width: scale.width
                horizontalAlignment: Text.AlignRight
                y: fader.posOf(modelData) * scale.height - height / 2
                text: modelData > 0 ? "+" + modelData : String(modelData)
                font.pixelSize: 9
                font.features: { "tnum": 1 }
                color: modelData === 0 ? Theme.text : Theme.textSecondary
            }
        }
    }
    Rectangle {                                                // the slot
        id: slot
        x: 30; y: 10; width: 6; height: parent.height - 20
        radius: 3
        color: "#0B0C0F"
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.08)
        Rectangle {                                            // filled part
            anchors.bottom: parent.bottom
            width: parent.width
            height: parent.height * (1 - fader.posOf(fader.value))
            radius: 3
            color: Qt.alpha(fader.tint, 0.55)
        }
    }
    Rectangle {                                                // the 0 dB line
        x: 26; width: 14; height: 1
        y: 10 + fader.posOf(0) * (fader.height - 20)
        color: Qt.rgba(1, 1, 1, 0.35)
    }
    Rectangle {                                                // the handle
        id: handle
        x: 23; width: 28; height: 14; radius: 4
        y: 10 + fader.posOf(fader.value) * (fader.height - 20) - height / 2
        color: ma.pressed || ma.containsMouse ? "#D9DCE3" : "#BFC3CC"
        border.width: 1
        border.color: "#2A2D35"
        Rectangle { anchors.centerIn: parent; width: parent.width - 8; height: 2; radius: 1; color: "#3A3E48" }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.SizeVerCursor
        onPressed: mouse => fader.moved(fader.dbAt(mouse.y))
        onPositionChanged: mouse => { if (pressed) fader.moved(fader.dbAt(mouse.y)) }
        onDoubleClicked: fader.moved(0)
        onWheel: wheel => fader.moved(Math.max(fader.minDb, Math.min(fader.maxDb, fader.value + (wheel.angleDelta.y > 0 ? 0.5 : -0.5))))
    }
}
