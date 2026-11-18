import QtQuick
import QtQuick.Controls

// An entry of MMenu: `danger` colours it red (uninstall), `checkable` shows a tick.
MenuItem {
    id: item

    property bool danger: false
    property string hint: ""                       // an optional second line (what the entry is good for): smaller, grey, wraps

    implicitHeight: hint === "" ? 38 : Math.max(38, contentItem.implicitHeight + 16)
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
        Column {
            width: item.availableWidth - (item.checkable ? 26 : 0)
            spacing: 2
            anchors.verticalCenter: parent.verticalCenter
            Text {
                width: parent.width
                text: item.text
                font: item.font
                color: !item.enabled ? Theme.textDisabled : item.danger ? Theme.errorSoft : Theme.text
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                visible: item.hint !== ""
                width: parent.width
                text: item.hint
                font.pixelSize: 11
                color: Theme.textSecondary
                wrapMode: Text.Wrap
            }
        }
    }
    background: Rectangle {
        radius: 8
        color: item.highlighted && item.enabled ? (item.danger ? Qt.alpha(Theme.error, 0.16) : Theme.surfaceHover) : "transparent"
    }
}
