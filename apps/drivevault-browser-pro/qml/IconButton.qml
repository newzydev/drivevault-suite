import QtQuick
import QtQuick.Controls

// A square button with an icon and no text (the name is the tooltip and the accessible name).
AbstractButton {
    id: button

    property string kind: "globe"
    property string tip: ""
    property bool active: false                       // shown as pressed in (a toggle that is on)
    property color activeColor: Theme.accent
    property int box: 32

    implicitWidth: box
    implicitHeight: box
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: tip
    Accessible.role: Accessible.Button
    HoverHandler { cursorShape: button.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }

    readonly property color tint: !enabled ? Theme.textDisabled : (active || hovered) ? Theme.text : Theme.textSecondary

    background: Rectangle {
        radius: 9
        color: button.down ? Theme.surfaceHover : button.active ? Qt.alpha(button.activeColor, 0.18) : button.hovered ? Theme.surfaceHover : "transparent"
        border.width: button.visualFocus ? 2 : 0
        border.color: Theme.text
    }
    contentItem: Item {
        LineIcon { anchors.centerIn: parent; kind: button.kind; color: button.active ? button.activeColor : button.tint }
    }
    ToolTip.visible: hovered && tip !== ""
    ToolTip.delay: 500
    ToolTip.text: tip
}
