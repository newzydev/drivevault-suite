import QtQuick
import QtQuick.Controls

// variant: "primary" | "secondary" | "ghost" | "danger"
Button {
    id: control

    property string variant: "secondary"
    property string iconName
    property string tip

    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(implicitHeight, contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: 14
    rightPadding: 14
    spacing: 8
    font.pixelSize: Theme.fontMd
    font.weight: Font.Medium
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    readonly property color fillColor: {
        if (!enabled)
            return variant === "primary" ? Theme.surfaceHover : "transparent"
        switch (variant) {
        case "primary": return down ? Theme.accentPressed : hovered ? Theme.accentHover : Theme.accent
        case "danger": return down ? Qt.darker(Theme.error, 1.3) : hovered ? Qt.lighter(Theme.error, 1.1) : Theme.error
        case "ghost": return down ? Theme.surfaceHover : hovered ? Theme.surfaceHover : "transparent"
        default: return down ? Theme.surfaceHover : hovered ? Theme.surfaceElevated : Theme.surface
        }
    }
    readonly property color labelColor: {
        if (!enabled)
            return Theme.textDisabled
        if (variant === "primary")
            return Theme.textOnAccent
        if (variant === "danger")
            return "#FFFFFF"
        return Theme.text
    }

    contentItem: Row {
        spacing: control.spacing
        layoutDirection: Qt.LeftToRight

        Icon {
            visible: control.iconName !== ""
            name: control.iconName
            size: 16
            color: control.labelColor
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: control.text !== ""
            text: control.text
            font: control.font
            color: control.labelColor
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    background: Rectangle {
        radius: Theme.radiusSmall + 2
        color: control.fillColor
        border.width: control.variant === "secondary" || control.visualFocus ? 1 : 0
        border.color: control.visualFocus ? Theme.accentHover : (control.enabled ? Theme.border : Theme.borderSubtle)

        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }

    Rectangle {
        // Focus ring drawn outside the button so it never changes the layout.
        anchors.fill: parent
        anchors.margins: -3
        radius: Theme.radiusSmall + 4
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        opacity: control.visualFocus ? 0.6 : 0
        visible: opacity > 0
    }

    AppToolTip {
        visible: control.hovered && control.tip !== ""
        text: control.tip
    }
}
