import QtQuick
import QtQuick.Controls

TextField {
    id: control

    property bool invalid: false
    property string iconName

    implicitHeight: Theme.controlHeight
    implicitWidth: 220
    leftPadding: (iconName !== "" ? 34 : 12)
    rightPadding: 12
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    font.pixelSize: Theme.fontMd
    color: Theme.text
    placeholderTextColor: Theme.textDisabled
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    selectByMouse: true
    hoverEnabled: true

    background: Rectangle {
        radius: Theme.radiusSmall + 2
        color: control.enabled ? Theme.backgroundAlt : "transparent"
        border.width: control.activeFocus || control.invalid ? 2 : 1
        border.color: control.invalid ? Theme.error
                      : control.activeFocus ? Theme.accent
                      : control.hovered ? Theme.textDisabled : Theme.border

        Icon {
            visible: control.iconName !== ""
            name: control.iconName
            size: 16
            color: Theme.textSecondary
            x: 11
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
