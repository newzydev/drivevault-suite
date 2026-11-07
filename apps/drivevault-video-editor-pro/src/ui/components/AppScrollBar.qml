import QtQuick
import QtQuick.Controls

ScrollBar {
    id: control

    implicitWidth: orientation === Qt.Vertical ? 10 : 0
    implicitHeight: orientation === Qt.Horizontal ? 10 : 0
    minimumSize: 0.08
    policy: ScrollBar.AsNeeded
    padding: 2

    contentItem: Rectangle {
        radius: 3
        color: control.pressed ? Theme.textSecondary : control.hovered ? Theme.textDisabled : Theme.border
        opacity: control.active || control.hovered ? 1 : 0.6
    }

    background: Item {}
}
