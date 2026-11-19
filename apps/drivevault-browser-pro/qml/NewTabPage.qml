import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Browser.Backend

// The page of an empty tab: a search box and the pages visited most (a private window says what private means instead).
Rectangle {
    id: page
    property bool privateMode: false
    signal go(string text)
    color: Theme.background

    property var sites: privateMode ? [] : Browser.history.topSites(8)
    Connections {
        target: Browser.history
        function onChanged() { page.sites = page.privateMode ? [] : Browser.history.topSites(8) }
    }
    function focusSearch() { box.forceActiveFocus() }

    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Math.max(40, parent.height * 0.16)
        width: Math.min(640, parent.width - 48)
        spacing: 24

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 12
            Rectangle {
                width: 44; height: 44; radius: 12
                color: page.privateMode ? Theme.privateAccent : Theme.accent
                LineIcon { anchors.centerIn: parent; size: 26; kind: page.privateMode ? "mask" : "globe"; color: Theme.readableOn(parent.color) }
            }
            Text {
                text: Tr.t("app")
                color: Theme.text
                font.pixelSize: 30
                font.weight: Font.DemiBold
            }
        }

        TextField {
            id: box
            objectName: "ntpSearch"
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            placeholderText: Tr.t("ntp_search")
            placeholderTextColor: Theme.textDisabled
            color: Theme.text
            font.pixelSize: 15
            leftPadding: 44
            selectByMouse: true
            background: Rectangle {
                radius: 23
                color: Theme.surface
                border.width: box.activeFocus ? 2 : 1
                border.color: box.activeFocus ? (page.privateMode ? Theme.privateAccent : Theme.accent) : Theme.border
                LineIcon { x: 16; anchors.verticalCenter: parent.verticalCenter; kind: "search"; color: Theme.textSecondary }
            }
            onAccepted: { if (text.trim() !== "") { page.go(text); text = "" } }
        }

        Text {
            visible: page.privateMode
            Layout.fillWidth: true
            Layout.topMargin: 12
            text: Tr.t("ntp_private_title")
            color: Theme.privateAccent
            font.pixelSize: 16
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            visible: page.privateMode
            Layout.fillWidth: true
            text: Tr.t("ntp_private_body")
            color: Theme.textSecondary
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            lineHeight: 1.3
        }

        Text {
            visible: !page.privateMode
            Layout.topMargin: 8
            text: Tr.t("ntp_top")
            color: Theme.textSecondary
            font.pixelSize: 12
            font.weight: Font.Medium
        }
        Text {
            visible: !page.privateMode && page.sites.length === 0
            text: Tr.t("ntp_none")
            color: Theme.textDisabled
            font.pixelSize: 13
        }
        Flow {
            visible: !page.privateMode
            Layout.fillWidth: true
            spacing: 12
            Repeater {
                model: page.sites
                delegate: Rectangle {
                    required property var modelData
                    width: 140; height: 96; radius: Theme.radius
                    color: tileHover.hovered ? Theme.surfaceHover : Theme.surface
                    border.width: 1
                    border.color: Theme.borderSubtle
                    HoverHandler { id: tileHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: page.go(modelData.url) }
                    Column {
                        anchors.centerIn: parent
                        spacing: 8
                        width: parent.width - 20
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 36; height: 36; radius: 18
                            color: Qt.alpha(Theme.accent, 0.25)
                            Text {
                                anchors.centerIn: parent
                                text: (Browser.hostOf(modelData.url).replace(/^www\./, "") || "?").charAt(0).toUpperCase()
                                color: Theme.accent
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                            }
                        }
                        Text {
                            width: parent.width
                            text: modelData.title || Browser.prettyUrl(modelData.url)
                            color: Theme.text
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }
        }
    }
}
