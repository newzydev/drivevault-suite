import QtQuick
import QtQuick.Controls

Switch {
    id: control

    implicitWidth: 44
    implicitHeight: 24
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    indicator: Rectangle {
        implicitWidth: 44
        implicitHeight: 24
        radius: height / 2
        color: control.checked ? (control.enabled ? Theme.accent : Theme.textDisabled) : Theme.surfaceHover
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.accentHover : (control.checked ? "transparent" : Theme.border)
        opacity: control.enabled ? 1 : 0.5

        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Rectangle {
            width: 18
            height: 18
            radius: 9
            y: 3
            x: control.checked ? parent.width - width - 3 : 3
            color: control.checked ? Theme.textOnAccent : Theme.textSecondary

            Behavior on x { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
        }
    }

    contentItem: Item {}
}
