import QtQuick
import QtQuick.Controls

// An entry of MMenu: `danger` colours it red (uninstall), `checkable` shows a tick.
MenuItem {
    id: item

    property bool danger: false

    implicitHeight: 38
    leftPadding: 14
    rightPadding: 14
    font.pixelSize: 13
    HoverHandler { cursorShape: item.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }

    contentItem: Row {
        spacing: 10
        Text {
            visible: item.checkable
            width: 16
            text: item.checked ? "✓" : ""
            font.pixelSize: 13
            font.weight: Font.Bold
            color: Theme.accent
            verticalAlignment: Text.AlignVCenter
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: item.text
            font: item.font
            color: !item.enabled ? Theme.textDisabled : item.danger ? Theme.errorSoft : Theme.text
            verticalAlignment: Text.AlignVCenter
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    background: Rectangle {
        radius: 8
        color: item.highlighted && item.enabled ? (item.danger ? Qt.alpha(Theme.error, 0.16) : Theme.surfaceHover) : "transparent"
    }
}
