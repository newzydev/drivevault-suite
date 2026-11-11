import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ItemDelegate {
    id: entry
    required property var tool
    property bool card: false

    text: Tr.s(tool.key)
    implicitHeight: card ? 94 : 40
    leftPadding: 10
    rightPadding: 10
    hoverEnabled: true
    HoverHandler { cursorShape: entry.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    contentItem: ColumnLayout {
        spacing: 5
        RowLayout {
            spacing: 8
            Layout.fillWidth: true
            LineIcon { name: entry.tool.icon; color: entry.enabled ? entry.tool.tint : Theme.textDisabled; size: 20 }
            AppText {
                Layout.fillWidth: true
                text: entry.text
                font.weight: entry.card ? Font.DemiBold : Font.Medium
                color: entry.enabled ? Theme.text : Theme.textDisabled
                wrapMode: entry.card ? Text.Wrap : Text.NoWrap
                elide: Text.ElideRight
            }
        }
        AppText {
            Layout.fillWidth: true
            visible: entry.card && entry.tool.key !== "openFile"
            text: visible ? Tr.s(entry.tool.key + "Desc") : ""
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSm
            wrapMode: Text.Wrap
        }
    }
    background: Rectangle {
        radius: Theme.radiusSmall
        color: entry.highlighted || entry.hovered ? Theme.surfaceHover : entry.card ? Theme.backgroundAlt : "transparent"
        border.width: entry.visualFocus || entry.card ? 1 : 0
        border.color: entry.visualFocus ? Theme.accentText : Theme.border
    }
}
