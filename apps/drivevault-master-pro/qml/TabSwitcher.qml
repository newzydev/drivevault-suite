import QtQuick
import QtQuick.Controls

// The three tabs at the top of the page: Programs | AI Q&A | Agentic AI. Used on the Programs page (in the hero) and on the AI pages (in their slim header).
Rectangle {
    id: bar

    property string current: "programs"
    property string prefix: "tab_"                   // objectName prefix: one set per place, so tests can find each button
    signal picked(string key)

    readonly property var keys: ["programs", "ask", "agent", "browser"]

    implicitHeight: 40
    implicitWidth: tabRow.implicitWidth + 8
    radius: 20
    color: "#E61B1D24"
    border.width: 1
    border.color: Theme.border

    Row {
        id: tabRow
        anchors.centerIn: parent
        spacing: 2
        Repeater {
            model: bar.keys
            delegate: AbstractButton {
                id: tabButton
                required property string modelData
                objectName: bar.prefix + modelData
                readonly property bool isCurrent: bar.current === modelData
                implicitWidth: Math.max(96, tabLabel.implicitWidth + 40)
                implicitHeight: 32
                hoverEnabled: true
                onClicked: bar.picked(modelData)
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                Accessible.role: Accessible.PageTab
                Accessible.name: Tr.s("tab_" + modelData)
                contentItem: Text {
                    id: tabLabel
                    text: Tr.s("tab_" + tabButton.modelData)
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: tabButton.isCurrent ? Theme.readableOn(Theme.accent) : tabButton.hovered ? Theme.text : Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { radius: 16; color: tabButton.isCurrent ? Theme.accent : tabButton.hovered ? Theme.surfaceHover : "transparent" }
            }
        }
    }
}
