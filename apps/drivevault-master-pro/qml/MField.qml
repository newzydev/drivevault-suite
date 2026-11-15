import QtQuick
import QtQuick.Controls

// A one line text box in the style of the hub (the Basic style has none). secret: the text is shown as dots (API keys).
TextField {
    id: field

    property bool secret: false

    echoMode: secret ? TextInput.Password : TextInput.Normal
    color: Theme.text
    placeholderTextColor: Theme.textDisabled
    font.pixelSize: 13
    selectByMouse: true
    leftPadding: 12
    rightPadding: 12
    implicitHeight: 38
    background: Rectangle {
        radius: 10
        color: Theme.backgroundAlt
        border.width: field.activeFocus ? 2 : 1
        border.color: field.activeFocus ? Theme.accent : Theme.border
    }
}
