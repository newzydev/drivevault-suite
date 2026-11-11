import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ItemDelegate {
    id: entry
    required property var tool
    property bool card: false

    text: Tr.s(tool.key)
    readonly property string description: card && tool.key !== "openFile" ? Tr.s(tool.key + "Desc") : ""
    implicitHeight: card ? 94 : 40
    leftPadding: 10
    rightPadding: 10
    hoverEnabled: true
    HoverHandler { cursorShape: entry.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    ToolTip.visible: hovered && (titleLabel.truncated || descriptionLabel.truncated)
    ToolTip.delay: 500
    ToolTip.text: description === "" ? text : text + "\n" + description
    contentItem: ColumnLayout {
        spacing: 5
        RowLayout {
            spacing: 8
            Layout.fillWidth: true
            LineIcon { name: entry.tool.icon; color: entry.enabled ? entry.tool.tint : Theme.textDisabled; size: 20 }
            AppText {
                id: titleLabel
                objectName: "toolTitle"
                Layout.fillWidth: true
                text: entry.text
                font.weight: entry.card ? Font.DemiBold : Font.Medium
                color: entry.enabled ? Theme.text : Theme.textDisabled
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
            }
        }
        AppText {
            id: descriptionLabel
            objectName: "toolDescription"
            Layout.fillWidth: true
            visible: text !== ""
            text: entry.description
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSm
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
        }
    }
    background: Rectangle {
        radius: Theme.radiusSmall
        color: entry.highlighted || entry.hovered ? Theme.surfaceHover : entry.card ? Theme.backgroundAlt : "transparent"
        border.width: entry.visualFocus || entry.card ? 1 : 0
        border.color: entry.visualFocus ? Theme.accentText : Theme.border
    }
}
