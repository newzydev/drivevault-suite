import QtQuick
import QtQuick.Controls

// An entry of MMenu: `danger` colours it red (uninstall), `checkable` shows a tick.
MenuItem {
    id: item

    property bool danger: false
    property string hint: ""                       // an optional second line (what the entry is good for): smaller, grey, wraps
    property bool markSelected: false              // the chosen entry of a list of choices (the models): tinted with the accent and bold, so it is clear at a glance

    implicitHeight: hint === "" ? 38 : Math.max(38, contentItem.implicitHeight + 16)
    leftPadding: 14
    rightPadding: 14
    font.pixelSize: 13
    HoverHandler { cursorShape: item.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }

    contentItem: Row {
        spacing: 10
        Item {                                     // the tick is drawn (a font glyph looked different on every PC)
            visible: item.checkable
            width: 16; height: 16
            anchors.verticalCenter: parent.verticalCenter
            LineIcon { visible: item.checked; anchors.centerIn: parent; kind: "check"; size: 14; color: Theme.accent }
        }
        Column {
            width: item.availableWidth - (item.checkable ? 26 : 0)
            spacing: 2
            anchors.verticalCenter: parent.verticalCenter
            Text {
                width: parent.width
                text: item.text
                font.pixelSize: item.font.pixelSize
                font.weight: item.markSelected && item.checked ? Font.DemiBold : Font.Normal
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
        color: item.highlighted && item.enabled ? (item.danger ? Qt.alpha(Theme.error, 0.16) : Theme.surfaceHover)
               : item.markSelected && item.checked ? Qt.alpha(Theme.accent, 0.13) : "transparent"
        border.width: item.markSelected && item.checked ? 1 : 0
        border.color: Qt.alpha(Theme.accent, 0.45)
    }
}
