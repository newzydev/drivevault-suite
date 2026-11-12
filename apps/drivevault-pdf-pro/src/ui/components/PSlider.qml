import QtQuick
import QtQuick.Controls

// A slider in the style of the program: a thin track that fills with the accent colour and a round handle.
Slider {
    id: control

    implicitHeight: 32
    hoverEnabled: true
    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: control.availableWidth
        height: 4
        radius: 2
        color: Theme.border
        opacity: control.enabled ? 1 : 0.5
        Rectangle { width: control.visualPosition * parent.width; height: parent.height; radius: 2; color: Theme.accent }
    }
    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: 16
        implicitHeight: 16
        radius: 8
        color: control.pressed ? Theme.accentPressed : Theme.text
        border.width: control.visualFocus ? 3 : 2
        border.color: control.hovered || control.pressed || control.visualFocus ? Theme.accent : Theme.border
        opacity: control.enabled ? 1 : 0.5
    }
}
