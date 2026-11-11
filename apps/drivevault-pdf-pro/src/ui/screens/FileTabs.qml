import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// The open files as tabs, and a "+" to open another file.
Rectangle {
    id: bar

    signal openRequested()
    signal closeTabRequested(int index)
    signal homeRequested()
    signal tabRequested()
    property bool homeSelected: false

    implicitHeight: 44
    color: Theme.background
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 4

        PButton {
            objectName: "homeTab"
            compact: true
            variant: bar.homeSelected ? "secondary" : "ghost"
            glyph: "home"
            tip: Tr.s("home")
            onClicked: bar.homeRequested()
        }

        Repeater {
            model: Doc.tabs
            delegate: Rectangle {
                id: tab
                required property var modelData
                required property int index
                readonly property bool active: !bar.homeSelected && Doc.currentTab === index
                objectName: "tab_" + index
                Layout.alignment: Qt.AlignBottom
                Layout.preferredHeight: 32
                Layout.fillWidth: true
                Layout.preferredWidth: Math.min(220, Math.max(110, label.implicitWidth + 58))
                Layout.maximumWidth: 220
                radius: 8
                color: active ? Theme.backgroundAlt : tabHover.hovered ? Theme.surface : "transparent"
                border.width: active ? 1 : 0
                border.color: Theme.border
                HoverHandler { id: tabHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: { Doc.switchTab(tab.index); bar.tabRequested() } }
                ToolTip.visible: tabHover.hovered && tab.modelData.path !== ""
                ToolTip.delay: 600
                ToolTip.text: tab.modelData.path

                Rectangle {                                         // accent line under the active tab
                    visible: tab.active
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 20
                    height: 2
                    radius: 1
                    color: Theme.accent
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 4
                    spacing: 6
                    AppText {
                        id: label
                        Layout.fillWidth: true
                        text: tab.modelData.title
                        elide: Text.ElideMiddle
                        font.pixelSize: Theme.fontSm
                        font.weight: tab.active ? Font.DemiBold : Font.Normal
                        color: tab.active ? Theme.text : Theme.textSecondary
                    }
                    PButton {
                        objectName: "tabClose_" + tab.index
                        compact: true
                        variant: "ghost"
                        implicitWidth: 24
                        implicitHeight: 24
                        leftPadding: 0
                        rightPadding: 0
                        glyph: "close"
                        tip: Tr.s("closeFile")
                        onClicked: bar.closeTabRequested(tab.index)
                    }
                }
            }
        }
        PButton {
            objectName: "tabNew"
            Layout.alignment: Qt.AlignVCenter
            compact: true
            variant: "ghost"
            text: "+  " + Tr.s("open")
            tip: Tr.s("openFile")
            onClicked: bar.openRequested()
        }
        Item { Layout.fillWidth: true }
    }
}
