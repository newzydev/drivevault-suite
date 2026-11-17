import QtQuick
import QtQuick.Controls

// A square button with an icon and no text (the name is the tooltip and the accessible name). kind: a LineIcon kind, or "gear" (the settings gear of the program).
AbstractButton {
    id: button

    property string kind: "globe"
    property string tip: ""
    property bool active: false                       // shown as pressed in (a toggle that is on)
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
        color: button.down ? Theme.surfaceHover : button.active ? Qt.alpha(Theme.accent, 0.18) : button.hovered ? Theme.surfaceHover : "transparent"
        border.width: button.visualFocus ? 2 : button.active ? 1 : 0
        border.color: button.visualFocus ? Theme.text : Qt.alpha(Theme.accent, 0.6)
    }
    contentItem: Item {
        LineIcon { visible: button.kind !== "gear"; anchors.centerIn: parent; kind: button.kind; color: button.active ? Theme.accent : button.tint }
        GearIcon { visible: button.kind === "gear"; anchors.centerIn: parent; size: 17; color: button.tint }
    }
    ToolTip.visible: hovered && tip !== ""
    ToolTip.delay: 500
    ToolTip.text: tip
}
