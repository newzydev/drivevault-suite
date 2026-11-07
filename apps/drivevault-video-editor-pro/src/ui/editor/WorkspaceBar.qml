import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

// Workspace switcher: Home, Edit, Color, Export. (Import, Audio, Text, Effects, Transitions, Filters, Adjustment and AI are tabs of the tool panel at the left.)
Rectangle {
    id: root

    signal homeRequested()

    implicitHeight: 44
    color: Theme.background

    readonly property var items: [
        { key: "home",    icon: "home",     label: qsTr("Home") },
        { key: "edit",    icon: "scissors", label: qsTr("Edit") },
        { key: "color",   icon: "paint",    label: qsTr("Color") },
        { key: "export",  icon: "export",   label: qsTr("Export") }
    ]

    RowLayout {
        anchors.centerIn: parent
        spacing: 4

        Repeater {
            model: root.items
            delegate: AbstractButton {
                id: tab
                objectName: "ws_" + modelData.key
                required property var modelData
                readonly property bool current: modelData.key === App.workspace

                implicitHeight: 32
                implicitWidth: row.implicitWidth + 28
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.name: modelData.label
                Accessible.role: Accessible.PageTab
                onClicked: modelData.key === "home" ? root.homeRequested() : App.workspace = modelData.key

                contentItem: Item {
                    implicitWidth: row.implicitWidth
                    implicitHeight: row.implicitHeight
                    Row {
                        id: row
                        spacing: 7
                        anchors.centerIn: parent
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: tab.modelData.icon
                            size: 16
                            color: tab.current ? Theme.accentText : tab.hovered ? Theme.text : Theme.textSecondary
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: tab.modelData.label
                            font.pixelSize: Theme.fontMd
                            font.weight: tab.current ? Font.DemiBold : Font.Normal
                            color: tab.current ? Theme.text : tab.hovered ? Theme.text : Theme.textSecondary
                        }
                    }
                }

                background: Rectangle {
                    radius: 16
                    color: tab.current ? Theme.accentSubtle : tab.hovered ? Theme.surface : "transparent"
                    border.width: tab.visualFocus ? 2 : 0
                    border.color: Theme.accent
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                }
            }
        }
    }
}
