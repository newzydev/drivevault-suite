import QtQuick
import QtQuick.Controls

// variant: "primary" (accent) | "secondary" | "ghost". `compact` = a small square-ish button (icons / symbols).
Button {
    id: control

    property string variant: "secondary"
    property bool compact: false
    property string tip: ""

    implicitHeight: compact ? 32 : 40
    implicitWidth: Math.max(compact ? 32 : 40, contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: compact ? 10 : 18
    rightPadding: leftPadding
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: tip !== "" ? tip : text

    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    ToolTip.visible: tip !== "" && hovered
    ToolTip.delay: 500
    ToolTip.text: tip

    contentItem: AppText {
        text: control.text
        font.pixelSize: control.compact ? Theme.fontMd : Theme.fontMd
        font.weight: control.variant === "primary" ? Font.DemiBold : Font.Medium
        color: !control.enabled ? Theme.textDisabled : control.variant === "primary" ? Theme.textOnAccent : Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: control.compact ? 8 : 10
        color: !control.enabled ? "transparent"
               : control.variant === "primary" ? (control.down ? Theme.accentPressed : control.hovered ? Theme.accentHover : Theme.accent)
               : control.variant === "ghost" ? (control.down ? Theme.border : control.hovered ? Theme.surfaceHover : "transparent")
               : (control.down ? Theme.border : control.hovered ? Theme.surfaceHover : Theme.surface)
        border.width: control.visualFocus ? 2 : control.variant === "secondary" ? 1 : 0
        border.color: control.visualFocus ? Theme.text : Theme.border
    }
}
