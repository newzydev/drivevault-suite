import QtQuick
import QtQuick.Controls

// An on / off switch in the style of the hub (the Basic style's default is hard to see on the dark background): amber when on.
Switch {
    id: control

    implicitWidth: 48
    implicitHeight: 28
    hoverEnabled: true
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    indicator: Rectangle {
        implicitWidth: 48
        implicitHeight: 26
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: 13
        color: control.checked ? Theme.accent : Theme.surfaceHover
        border.width: 1
        border.color: control.checked ? Theme.accent : Theme.border
        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            y: 3
            width: 20
            height: 20
            radius: 10
            color: control.checked ? Theme.readableOn(Theme.accent) : Theme.textSecondary
            Behavior on x { NumberAnimation { duration: 110 } }
        }
    }
    contentItem: Item {}
}
