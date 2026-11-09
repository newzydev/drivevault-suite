import QtQuick
import QtQuick.Controls

// An entry of PMenu.
MenuItem {
    id: item

    implicitHeight: 36
    leftPadding: 14
    rightPadding: 14
    font.pixelSize: Theme.fontMd

    HoverHandler { cursorShape: item.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }

    contentItem: AppText {
        text: item.text
        color: item.enabled ? Theme.text : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 8
        color: item.highlighted && item.enabled ? Theme.surfaceHover : "transparent"
    }
}
