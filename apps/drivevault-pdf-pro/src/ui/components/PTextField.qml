import QtQuick
import QtQuick.Controls

// A text field in the style of the program: rounded, accent border when it has the focus.
TextField {
    id: control

    implicitHeight: 38
    leftPadding: 12
    rightPadding: 12
    color: Theme.text
    placeholderTextColor: Theme.textDisabled
    selectionColor: Qt.alpha(Theme.accent, 0.45)
    selectedTextColor: Theme.text
    font.pixelSize: Theme.fontMd
    selectByMouse: true
    verticalAlignment: TextInput.AlignVCenter
    background: Rectangle {
        radius: 8
        color: Theme.surface
        opacity: control.enabled ? 1 : 0.55
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.accent : control.hovered ? Theme.textDisabled : Theme.border
    }
}
