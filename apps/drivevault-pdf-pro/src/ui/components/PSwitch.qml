import QtQuick
import QtQuick.Controls

// An on / off switch: a dark track with a light knob when off, the accent colour with the knob on the right when on.
Switch {
    id: control

    implicitWidth: 46
    implicitHeight: 28
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }

    indicator: Rectangle {
        implicitWidth: 44
        implicitHeight: 24
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: height / 2
        color: control.checked ? Theme.accent : (control.hovered ? Theme.surfaceHover : Theme.surface)
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.text : control.checked ? Theme.accent : Theme.border
        opacity: control.enabled ? 1 : 0.5
        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 18
            height: 18
            radius: 9
            color: control.checked ? Theme.textOnAccent : Theme.textSecondary
            Behavior on x { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Item {}
}
