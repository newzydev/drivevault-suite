import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ItemDelegate {
    id: entry
    required property var tool
    property bool card: false
    readonly property color toolColor: Theme.light ? Qt.darker(tool.tint, 1.75) : tool.tint

    text: Tr.s(tool.key)
    readonly property string description: card ? Tr.s(tool.key + "Desc") : ""
    implicitHeight: card ? 114 : 40
    leftPadding: card ? 15 : 10
    rightPadding: leftPadding
    topPadding: card ? 12 : 0
    bottomPadding: topPadding
    hoverEnabled: true
    HoverHandler { cursorShape: entry.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    ToolTip.visible: hovered && (titleLabel.truncated || descriptionLabel.truncated)
    ToolTip.delay: 500
    ToolTip.text: description === "" ? text : text + "\n" + description
    contentItem: GridLayout {
        columns: 2
        columnSpacing: 8
        rowSpacing: 4
        Rectangle {
            Layout.row: 0
            Layout.column: 0
            Layout.preferredWidth: entry.card ? 36 : 20
            Layout.preferredHeight: entry.card ? 36 : 20
            radius: 10
            color: entry.card ? Qt.alpha(entry.toolColor, 0.15) : "transparent"
            LineIcon {
                anchors.centerIn: parent
                name: entry.tool.icon
                color: entry.enabled ? entry.toolColor : Theme.textDisabled
                size: 20
            }
        }
        LineIcon {
            Layout.row: 0
            Layout.column: 1
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            visible: entry.card
            name: "forward"
            color: entry.hovered ? entry.toolColor : Theme.textDisabled
            size: 16
        }
        AppText {
            id: titleLabel
            objectName: "toolTitle"
            Layout.row: entry.card ? 1 : 0
            Layout.column: entry.card ? 0 : 1
            Layout.columnSpan: entry.card ? 2 : 1
            Layout.fillWidth: true
            text: entry.text
            font.weight: entry.card ? Font.DemiBold : Font.Medium
            color: entry.enabled ? Theme.text : Theme.textDisabled
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
        }
        AppText {
            id: descriptionLabel
            objectName: "toolDescription"
            Layout.row: 2
            Layout.column: 0
            Layout.columnSpan: 2
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
        radius: entry.card ? 14 : Theme.radiusSmall
        color: entry.highlighted || entry.hovered ? Theme.surfaceHover : entry.card ? Theme.toolCard : "transparent"
        border.width: entry.visualFocus || entry.card ? 1 : 0
        border.color: entry.visualFocus ? entry.toolColor
                      : entry.card && entry.hovered ? Qt.alpha(entry.toolColor, 0.7) : Theme.border
    }
}
