import QtQuick
import QtQuick.Controls

// variant: "primary" (filled with `tint`) | "secondary" | "ghost" | "danger"
Button {
    id: control

    property string variant: "secondary"
    property color tint: Theme.accent

    implicitHeight: 34
    implicitWidth: Math.max(34, contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: 14
    rightPadding: 14
    font.pixelSize: 13
    font.weight: Font.Medium
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    readonly property color fill: {
        if (!enabled)
            return variant === "primary" ? Theme.surfaceHover : "transparent"
        switch (variant) {
        case "primary": return down ? Qt.darker(tint, 1.25) : hovered ? Qt.lighter(tint, 1.12) : tint
        case "danger": return down ? Qt.darker(Theme.error, 1.3) : hovered ? Theme.error : "transparent"
        case "ghost": return hovered ? Theme.surfaceHover : "transparent"
        default: return down ? Theme.surfaceHover : hovered ? Theme.surfaceHover : Theme.surface
        }
    }
    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? Theme.textDisabled
               : control.variant === "primary" ? Theme.readableOn(control.tint)
               : control.variant === "danger" ? (control.hovered ? "#FFFFFF" : Theme.error) : Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.fill
        border.width: control.variant === "primary" || control.variant === "ghost" ? (control.visualFocus ? 2 : 0) : 1
        border.color: control.visualFocus ? Theme.text : control.variant === "danger" ? Theme.error : Theme.border
    }
}
