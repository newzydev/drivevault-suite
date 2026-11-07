import QtQuick
import QtQuick.Controls

Slider {
    id: control

    implicitWidth: 200
    implicitHeight: 24
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 4
        radius: 2
        color: Theme.surfaceHover

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: control.enabled ? Theme.accent : Theme.textDisabled
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 16
        height: 16
        radius: 8
        color: control.pressed ? Theme.accentPressed : "#FFFFFF"
        border.width: control.visualFocus ? 3 : 2
        border.color: control.enabled ? Theme.accent : Theme.textDisabled
        scale: control.hovered || control.pressed ? 1.12 : 1

        Behavior on scale { NumberAnimation { duration: Theme.durationFast } }
    }
}
