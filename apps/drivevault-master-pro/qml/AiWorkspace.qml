import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The page of the AI Q&A tab and of the Agentic AI tab: list of conversations / tasks on the left, the conversation in the middle with the message box at the
// bottom, and a side panel (sources, or plan and changes) on the right. NO AI IS CONNECTED YET: the page says so, the message box only keeps what is typed
// (nothing is sent anywhere) and the buttons that need an AI are disabled. Nothing here pretends to answer.
Item {
    id: root

    property string mode: "ask"                       // ask | agent
    readonly property bool agent: mode === "agent"
    readonly property bool connected: false           // becomes real when an AI backend exists
    signal tabPicked(string key)

    // ---- small parts ---------------------------------------------------------------------------------------------------------
    component Chip: Rectangle {
        property alias text: chipText.text
        property bool muted: true
        implicitHeight: 28
        implicitWidth: chipText.implicitWidth + 22
        radius: 14
        color: "transparent"
        border.width: 1
        border.color: Theme.border
        Text { id: chipText; anchors.centerIn: parent; font.pixelSize: 12; font.weight: Font.Medium; color: parent.muted ? Theme.textDisabled : Theme.textSecondary }
    }
    component SideTitle: Text {
        font.pixelSize: 12
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.6
        color: Theme.textSecondary
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- slim header: the tabs --------------------------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 64
            color: "#17130E"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
            TabSwitcher {
                anchors.centerIn: parent
                current: root.mode
                prefix: "aiTab_"
                onPicked: key => root.tabPicked(key)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- left: conversations / tasks -------------------------------------------------------------------------------------
            Rectangle {
                id: side
                objectName: "aiSidebar"
                Layout.preferredWidth: root.width < 900 ? 210 : 270
                Layout.fillHeight: true
                color: "#0D0E11"
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.borderSubtle }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 14
                    MButton {
                        objectName: "aiNew"
                        Layout.fillWidth: true
                        variant: "primary"
                        enabled: root.connected
                        text: "+  " + Tr.s(root.agent ? "ai_new_agent" : "ai_new_ask")
                    }
                    SideTitle { text: Tr.s(root.agent ? "ai_list_agent" : "ai_list_ask") }
                    Text {
                        Layout.fillWidth: true
                        text: Tr.s("ai_list_empty")
                        font.pixelSize: 13
                        color: Theme.textDisabled
                        wrapMode: Text.Wrap
                    }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        spacing: 8
                        Rectangle { width: 8; height: 8; radius: 4; color: root.connected ? Theme.success : Theme.textDisabled }
                        Text { objectName: "aiStatus"; text: Tr.s("ai_status_off"); font.pixelSize: 13; color: Theme.textSecondary }
                    }
                }
            }

            // ---- middle: the conversation --------------------------------------------------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // title row
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 28
                    Layout.rightMargin: 28
                    Layout.topMargin: 16
                    spacing: 10
                    Text { text: Tr.s(root.agent ? "ai_new_agent" : "ai_new_ask"); font.pixelSize: 15; font.weight: Font.DemiBold; color: Theme.text }
                    Chip { text: Tr.s("ai_status_off") }
                    Item { Layout.fillWidth: true }
                }

                // empty conversation: what this page is for, and a few examples that only fill the message box
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 56, 640)
                        spacing: 14
                        Item {                                                  // a small diamond mark, drawn
                            Layout.alignment: Qt.AlignHCenter
                            width: 44; height: 44
                            Rectangle { anchors.centerIn: parent; width: 26; height: 26; radius: 6; rotation: 45; color: Qt.alpha(Theme.accent, 0.18); border.width: 2; border.color: Theme.accent }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Tr.s(root.agent ? "ai_title_agent" : "ai_title_ask")
                            font.pixelSize: 26
                            font.weight: Font.Bold
                            color: Theme.text
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Tr.s(root.agent ? "ai_sub_agent" : "ai_sub_ask")
                            font.pixelSize: 14
                            color: Theme.textSecondary
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.topMargin: 8
                            text: Tr.s("ai_examples")
                            font.pixelSize: 12
                            color: Theme.textDisabled
                        }
                        Flow {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 8
                            Repeater {
                                model: [1, 2, 3]
                                delegate: AbstractButton {
                                    id: example
                                    required property int modelData
                                    objectName: "aiExample" + modelData
                                    hoverEnabled: true
                                    implicitHeight: 34
                                    implicitWidth: exampleText.implicitWidth + 28
                                    onClicked: { input.text = exampleText.text; input.forceActiveFocus() }
                                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                                    contentItem: Text {
                                        id: exampleText
                                        text: Tr.s((root.agent ? "ai_sa" : "ai_sq") + example.modelData)
                                        font.pixelSize: 13
                                        color: example.hovered ? Theme.text : Theme.textSecondary
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    background: Rectangle { radius: 17; color: example.hovered ? Theme.surfaceHover : Theme.surface; border.width: 1; border.color: Theme.border }
                                }
                            }
                        }
                    }
                }

                // not connected: said plainly above the message box
                Rectangle {
                    objectName: "aiBanner"
                    Layout.fillWidth: true
                    Layout.leftMargin: 28
                    Layout.rightMargin: 28
                    implicitHeight: bannerText.implicitHeight + 20
                    radius: 10
                    color: Qt.alpha(Theme.warning, 0.1)
                    border.width: 1
                    border.color: Qt.alpha(Theme.warning, 0.35)
                    Text {
                        id: bannerText
                        anchors.fill: parent
                        anchors.margins: 10
                        text: Tr.s("ai_banner")
                        font.pixelSize: 13
                        color: Theme.text
                        wrapMode: Text.Wrap
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                // message box
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 28
                    Layout.rightMargin: 28
                    Layout.topMargin: 10
                    Layout.bottomMargin: 22
                    implicitHeight: 132
                    radius: 18
                    color: Theme.backgroundAlt
                    border.width: input.activeFocus ? 2 : 1
                    border.color: input.activeFocus ? Theme.accent : Theme.border
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            TextArea {
                                id: input
                                objectName: "aiInput"
                                placeholderText: Tr.s(root.agent ? "ai_ph_agent" : "ai_ph_ask")
                                placeholderTextColor: Theme.textDisabled
                                color: Theme.text
                                font.pixelSize: 14
                                wrapMode: TextArea.Wrap
                                selectByMouse: true
                                background: null
                                leftPadding: 6
                                topPadding: 4
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            MButton { objectName: "aiAttach"; compact: true; variant: "ghost"; enabled: false; text: "+"; Layout.preferredWidth: 32; leftPadding: 0; rightPadding: 0 }
                            Chip { visible: root.agent; text: Tr.s("ai_folder") }
                            Chip { visible: root.agent; text: Tr.s("ai_perm") }
                            Item { Layout.fillWidth: true }
                            Chip { text: Tr.s("ai_model") }
                            MButton {
                                objectName: "aiSend"
                                compact: true
                                variant: "primary"
                                enabled: root.connected && input.text.trim() !== ""
                                text: Tr.s("ai_send")
                            }
                        }
                    }
                }
            }

            // ---- right: sources (Q&A) or plan and changes (agent); hidden when the window is narrow -----------------------------------
            Rectangle {
                id: panel
                objectName: "aiPanel"
                visible: root.width >= 1000
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                color: "#0D0E11"
                Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.borderSubtle }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 10
                    SideTitle { text: Tr.s(root.agent ? "ai_side_plan" : "ai_side_sources") }
                    Text { Layout.fillWidth: true; text: Tr.s(root.agent ? "ai_side_plan_empty" : "ai_side_sources_empty"); font.pixelSize: 13; color: Theme.textDisabled; wrapMode: Text.Wrap }
                    SideTitle { visible: root.agent; Layout.topMargin: 14; text: Tr.s("ai_side_changes") }
                    Text { visible: root.agent; Layout.fillWidth: true; text: Tr.s("ai_side_changes_empty"); font.pixelSize: 13; color: Theme.textDisabled; wrapMode: Text.Wrap }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
