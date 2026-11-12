import QtQuick
import QtQuick.Controls

// variant: "primary" (accent) | "secondary" | "ghost". `compact` = a small square-ish button (icons / symbols).
Button {
    id: control

    property string variant: "secondary"
    property bool compact: false
    property string tip: ""
    property string glyph: ""                     // a drawn icon (see LineIcon.qml) instead of the text; not "icon": Button has that property
    property int labelPixelSize: 0                 // 0 = the normal text size
    property int cornerRadius: -1                  // -1 = the normal radius

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

    contentItem: Item {
        implicitWidth: control.glyph !== "" ? 18 : label.implicitWidth
        implicitHeight: label.implicitHeight
        AppText {
            id: label
            anchors.fill: parent
            visible: control.glyph === ""
            text: control.text
            font.weight: control.variant === "primary" ? Font.DemiBold : Font.Medium
            font.pixelSize: control.labelPixelSize > 0 ? control.labelPixelSize : Theme.fontMd
            color: !control.enabled ? (control.variant === "primary" ? Qt.alpha(Theme.textOnAccent, 0.62) : Theme.textDisabled) : control.variant === "primary" ? Theme.textOnAccent : Theme.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        LineIcon {
            anchors.centerIn: parent
            visible: control.glyph !== ""
            name: control.glyph
            color: !control.enabled ? Theme.textDisabled : Theme.text
        }
    }
    background: Rectangle {
        radius: control.cornerRadius >= 0 ? control.cornerRadius : (control.compact ? 8 : 10)
        color: !control.enabled ? (control.variant === "primary" ? Qt.alpha(Theme.accent, 0.3) : "transparent")
               : control.variant === "primary" ? (control.down ? Theme.accentPressed : control.hovered ? Theme.accentHover : Theme.accent)
               : control.variant === "ghost" ? (control.down ? Theme.border : control.hovered ? Theme.surfaceHover : "transparent")
               : (control.down ? Theme.border : control.hovered ? Theme.surfaceHover : Theme.surface)
        border.width: control.visualFocus ? 2 : control.variant === "secondary" ? 1 : 0
        border.color: control.visualFocus ? Theme.text : Theme.border
    }
}
