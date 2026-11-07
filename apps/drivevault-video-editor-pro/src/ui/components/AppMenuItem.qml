import QtQuick
import QtQuick.Controls

MenuItem {
    id: control

    // Optional text shown at the right (e.g. "Ctrl+S"); taken from the action when present.
    property string shortcutText: action && action.shortcut !== undefined ? String(action.shortcut) : ""

    implicitHeight: 30
    implicitWidth: label.implicitWidth + (shortcutText !== "" ? shortcutLabel.implicitWidth + 40 : 0) + 56
    padding: 0
    hoverEnabled: true

    contentItem: Item {
        Icon {
            visible: control.checkable && control.checked
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            name: "check"
            size: 14
            color: Theme.accentText
        }
        Text {
            id: label
            x: 32
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font.pixelSize: Theme.fontMd
            color: control.enabled ? Theme.text : Theme.textDisabled
        }
        Text {
            id: shortcutLabel
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            visible: control.shortcutText !== ""
            text: control.shortcutText
            font.pixelSize: Theme.fontSm
            color: Theme.textDisabled
        }
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.highlighted && control.enabled ? Theme.accentSubtle : "transparent"
    }
}
