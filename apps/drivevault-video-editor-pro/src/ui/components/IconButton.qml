import QtQuick
import QtQuick.Controls

// Square icon-only button. `checked` styling is used for toggles (checkable: true).
Button {
    id: control

    property string iconName
    property string tip
    property real iconSize: 18
    property bool filledIcon: false
    property bool subtle: true   // transparent background until hovered

    implicitWidth: Theme.controlHeightSmall + 2
    implicitHeight: Theme.controlHeightSmall + 2
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: tip !== "" ? tip : iconName

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.iconName
            size: control.iconSize
            filled: control.filledIcon
            color: !control.enabled ? Theme.textDisabled
                   : control.checked ? Theme.accentText
                   : control.hovered ? Theme.text : Theme.textSecondary
        }
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.checked ? Theme.accentSubtle
               : control.down ? Theme.surfaceHover
               : control.hovered && control.enabled ? Theme.surfaceHover
               : control.subtle ? "transparent" : Theme.surface
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.accent

        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }

    AppToolTip {
        visible: control.hovered && control.tip !== ""
        text: control.tip
    }
}
