import QtQuick
import QtQuick.Controls

// A text button: variant "primary" (accent), "secondary" (grey) or "danger".
Button {
    id: b
    property string variant: "secondary"
    implicitHeight: 32
    leftPadding: 14
    rightPadding: 14
    hoverEnabled: true
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    readonly property color fill: variant === "primary" ? Theme.accent : variant === "danger" ? Theme.error : Theme.surface
    background: Rectangle {
        radius: Theme.radiusSmall
        color: !b.enabled ? Theme.surface : b.down ? Qt.darker(b.fill, 1.25) : b.hovered ? (b.variant === "secondary" ? Theme.surfaceHover : Qt.lighter(b.fill, 1.1)) : b.fill
        border.width: b.variant === "secondary" || b.visualFocus ? 1 : 0
        border.color: b.visualFocus ? Theme.text : Theme.border
        opacity: b.enabled ? 1 : 0.5
    }
    contentItem: Text {
        text: b.text
        color: b.variant === "secondary" ? Theme.text : Theme.readableOn(b.fill)
        font.pixelSize: 13
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
