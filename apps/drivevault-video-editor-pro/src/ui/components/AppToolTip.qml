import QtQuick
import QtQuick.Controls

ToolTip {
    id: control

    delay: 450
    timeout: 6000
    padding: 8
    leftPadding: 10
    rightPadding: 10
    font.pixelSize: Theme.fontSm

    contentItem: Text {          // plain Text: the fixed line height of AppText makes the popup size polish in a loop
        text: control.text
        font: control.font
        color: Theme.text
        wrapMode: Text.Wrap
    }

    background: Rectangle {
        color: Theme.surfaceElevated
        border.color: Theme.border
        radius: Theme.radiusSmall
    }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationFast } }
}
