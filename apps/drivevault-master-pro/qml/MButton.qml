import QtQuick
import QtQuick.Controls

// variant: "primary" (accent gradient with `tint`) | "secondary" | "ghost" | "danger"
// compact: smaller button for secondary actions.
Button {
    id: control

    property string variant: "secondary"
    property color tint: Theme.accent
    property bool compact: false

    implicitHeight: compact ? 32 : 40
    implicitWidth: Math.max(compact ? 32 : 40, contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: compact ? 12 : 18
    rightPadding: leftPadding
    font.pixelSize: compact ? 12 : 13
    font.weight: variant === "primary" ? Font.DemiBold : Font.Medium
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    readonly property bool isPrimary: variant === "primary"
    readonly property color textColor: !enabled ? Theme.textDisabled
                                       : isPrimary ? Theme.readableOn(tint)
                                       : variant === "danger" ? (hovered ? Theme.errorSoft : Theme.error)
                                       : Theme.text

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.textColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: control.compact ? 9 : 11
        // soft glow under the primary button (a second rounded rectangle, no shader needed)
        Rectangle {
            visible: control.isPrimary && control.enabled
            z: -1
            anchors.fill: parent
            anchors.topMargin: 3
            anchors.bottomMargin: -3
            radius: parent.radius
            color: Qt.alpha(control.tint, control.hovered ? 0.34 : 0.2)
        }
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: !control.enabled ? (control.isPrimary ? Theme.surfaceHover : "transparent")
                       : control.isPrimary ? (control.down ? Qt.darker(control.tint, 1.15) : Qt.lighter(control.tint, control.hovered ? 1.28 : 1.16))
                       : control.variant === "danger" ? (control.hovered ? Qt.alpha(Theme.error, 0.22) : Qt.alpha(Theme.error, 0.06))
                       : control.variant === "ghost" ? (control.hovered ? Theme.surfaceHover : "transparent")
                       : (control.hovered ? Theme.surfaceHover : Qt.lighter(Theme.surface, 1.08))
            }
            GradientStop {
                position: 1.0
                color: !control.enabled ? (control.isPrimary ? Theme.surfaceHover : "transparent")
                       : control.isPrimary ? (control.down ? Qt.darker(control.tint, 1.4) : Qt.darker(control.tint, control.hovered ? 1.0 : 1.12))
                       : control.variant === "danger" ? (control.hovered ? Qt.alpha(Theme.error, 0.16) : Qt.alpha(Theme.error, 0.03))
                       : control.variant === "ghost" ? (control.hovered ? Theme.surfaceHover : "transparent")
                       : (control.hovered ? Theme.surfaceHover : Theme.surface)
            }
        }
        border.width: control.visualFocus ? 2 : (control.isPrimary || control.variant === "ghost") ? 0 : 1
        border.color: control.visualFocus ? Theme.text
                      : control.variant === "danger" ? Qt.alpha(Theme.error, control.hovered ? 0.8 : 0.45)
                      : control.hovered ? Theme.textDisabled : Theme.border
    }
}
