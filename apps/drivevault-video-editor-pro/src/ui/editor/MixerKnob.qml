import QtQuick
import DriveVault.Backend

// A rotary knob of the mixer: drag up / down (or use the wheel) to change, double-click to reset, a 270 degree arc shows the value.
Item {
    id: knob

    property string label
    property real from: -1
    property real to: 1
    property real value: 0
    property real resetValue: 0
    property int decimals: 1
    property string suffix: ""
    property color tint: Theme.accent
    property var formatter: null                     // optional function(value) -> text
    signal moved(real value)

    readonly property real fraction: to > from ? Math.max(0, Math.min(1, (value - from) / (to - from))) : 0
    readonly property real resetFraction: to > from ? Math.max(0, Math.min(1, (resetValue - from) / (to - from))) : 0
    readonly property string valueText: formatter ? formatter(value) : value.toFixed(decimals) + suffix

    implicitWidth: 56
    implicitHeight: 74
    opacity: enabled ? 1 : 0.4
    onFractionChanged: arc.requestPaint()
    onTintChanged: arc.requestPaint()

    Text {
        id: caption
        anchors.top: parent.top
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: knob.label
        elide: Text.ElideRight
        maximumLineCount: 1
        font.pixelSize: Theme.fontXs
        font.weight: Font.DemiBold
        color: Theme.textSecondary
    }

    Item {
        id: dial
        anchors.top: caption.bottom
        anchors.topMargin: 3
        anchors.horizontalCenter: parent.horizontalCenter
        width: 40
        height: 40

        Canvas {
            id: arc
            anchors.fill: parent
            antialiasing: true
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const c = width / 2, r = c - 3
                const start = Math.PI * 0.75, span = Math.PI * 1.5
                ctx.lineWidth = 3
                ctx.lineCap = "round"
                ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.12)
                ctx.beginPath(); ctx.arc(c, c, r, start, start + span); ctx.stroke()
                ctx.strokeStyle = knob.tint
                ctx.beginPath()
                const a = start + span * knob.resetFraction, b = start + span * knob.fraction
                ctx.arc(c, c, r, Math.min(a, b), Math.max(a, b))
                ctx.stroke()
            }
        }
        Rectangle {                                       // the cap
            anchors.centerIn: parent
            width: 26; height: 26; radius: 13
            color: ma.containsMouse || ma.pressed ? Theme.surfaceHover : Theme.surface
            border.width: 1
            border.color: Theme.border
            Rectangle {                                   // the pointer
                width: 2; height: 9; radius: 1
                color: knob.tint
                x: parent.width / 2 - 1
                y: 3
                transformOrigin: Item.Bottom
                antialiasing: true
                // transform around the cap centre: the pointer is `parent.height / 2 - 3` long
                transform: Rotation { origin.x: 1; origin.y: 10; angle: -135 + 270 * knob.fraction }
            }
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SizeVerCursor
            property real pressY: 0
            property real startValue: 0
            onPressed: mouse => { pressY = mouse.y; startValue = knob.value }
            onPositionChanged: mouse => {
                if (!pressed)
                    return
                const v = startValue + (pressY - mouse.y) / 120 * (knob.to - knob.from)
                knob.moved(Math.max(knob.from, Math.min(knob.to, v)))
            }
            onDoubleClicked: knob.moved(knob.resetValue)
            onWheel: wheel => {
                const step = (knob.to - knob.from) / 60 * (wheel.angleDelta.y > 0 ? 1 : -1)
                knob.moved(Math.max(knob.from, Math.min(knob.to, knob.value + step)))
            }
        }
    }

    Text {
        anchors.top: dial.bottom
        anchors.topMargin: 2
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: knob.valueText
        elide: Text.ElideRight
        maximumLineCount: 1
        font.pixelSize: 10
        font.features: { "tnum": 1 }
        color: Theme.textSecondary
    }
}
