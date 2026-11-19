import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Browser.Backend

Rectangle {
    id: page

    property bool privateMode: false
    property var sites: privateMode ? [] : Browser.history.topSites(8)
    readonly property color accent: privateMode ? Theme.privateAccent : Theme.accent

    signal go(string text)
    signal contextRequested(point position)

    color: Theme.background

    Connections {
        target: Browser.history
        function onChanged() { page.sites = page.privateMode ? [] : Browser.history.topSites(8) }
    }

    function focusSearch() { box.forceActiveFocus() }

    Image {
        anchors.fill: parent
        visible: !page.privateMode
        source: "qrc:/qt/qml/DriveVault/Browser/resources/workspace/coast.jpg"
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }

    Rectangle {
        anchors.fill: parent
        visible: !page.privateMode
        gradient: Gradient {
            GradientStop { position: 0; color: "#BC08101A" }
            GradientStop { position: 0.55; color: "#5008101A" }
            GradientStop { position: 1; color: "#9008101A" }
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: point => page.contextRequested(point.position)
    }

    Flickable {
        id: workspace
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, dashboard.y + dashboard.height + 24)
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        ColumnLayout {
            id: dashboard
            width: Math.min(700, Math.max(0, workspace.width - 40))
            x: (workspace.width - width) / 2
            y: Math.min(128, Math.max(20, workspace.height * 0.18))
            spacing: 22

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: false
                Layout.preferredWidth: Math.min(dashboard.width, 62 + brand.implicitWidth)
                Layout.maximumWidth: dashboard.width
                spacing: 14

                Rectangle {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    radius: 14
                    color: page.accent
                    LineIcon { anchors.centerIn: parent; size: 28; kind: page.privateMode ? "mask" : "globe"; color: Theme.readableOn(page.accent) }
                }
                Text {
                    id: brand
                    Layout.fillWidth: true
                    text: Tr.t("app")
                    color: Theme.text
                    font.pixelSize: dashboard.width < 440 ? 22 : 36
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }

            TextField {
                id: box
                objectName: "ntpSearch"
                Layout.fillWidth: true
                Layout.preferredHeight: 54
                placeholderText: Tr.t("ntp_search")
                placeholderTextColor: Theme.textSecondary
                color: Theme.text
                font.pixelSize: 15
                leftPadding: 46
                rightPadding: 22
                selectByMouse: true
                background: Rectangle {
                    radius: height / 2
                    color: "#F219242F"
                    border.width: box.activeFocus ? 2 : 1
                    border.color: box.activeFocus ? page.accent : "#668EA9BA"
                    LineIcon { x: 18; anchors.verticalCenter: parent.verticalCenter; kind: "search"; color: page.accent }
                }
                onAccepted: { if (text.trim() !== "") { page.go(text); text = "" } }
            }

            Text {
                visible: page.privateMode
                Layout.fillWidth: true
                text: Tr.t("ntp_private_title")
                color: page.accent
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

            Rectangle {
                visible: !page.privateMode
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Math.min(dashboard.width, Math.max(148, Math.min(page.sites.length, 6) * 110 + 26))
                Layout.topMargin: 6
                implicitHeight: shortcuts.implicitHeight + 32
                color: "#CD111C26"
                border.width: 1
                border.color: "#4D8EA9BA"
                radius: 18

                ColumnLayout {
                    id: shortcuts
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 10

                    Text {
                        Layout.alignment: page.sites.length <= 1 ? Qt.AlignHCenter : Qt.AlignLeft
                        text: Tr.t(page.sites.length ? "ntp_top" : "ntp_shortcuts")
                        color: Theme.text
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Flow {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: page.sites.length <= 1 ? Math.min(104, shortcuts.width) : shortcuts.width
                        spacing: 6
                        Repeater {
                            model: page.sites.length ? page.sites : [{ title: "Google", url: "https://www.google.com/" }]
                            delegate: AbstractButton {
                                id: shortcut
                                required property var modelData
                                readonly property string host: Browser.hostOf(modelData.url).replace(/^www\./, "")
                                width: Math.min(104, shortcuts.width)
                                height: 76
                                hoverEnabled: true
                                text: modelData.title || host
                                Accessible.name: text
                                onClicked: page.go(modelData.url)
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                background: Rectangle {
                                    radius: 12
                                    color: shortcut.hovered || shortcut.down ? "#66829AA9" : "transparent"
                                    border.width: shortcut.visualFocus ? 1 : 0
                                    border.color: page.accent
                                }
                                contentItem: Column {
                                    spacing: 6
                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: 36
                                        height: 36
                                        radius: 12
                                        color: "#203947"
                                        Text {
                                            anchors.centerIn: parent
                                            text: (shortcut.host || "?").charAt(0).toUpperCase()
                                            color: "#9DEBFA"
                                            font.pixelSize: 16
                                            font.weight: Font.DemiBold
                                        }
                                    }
                                    Text {
                                        width: parent.width
                                        text: shortcut.text
                                        color: Theme.text
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }
                                ToolTip.visible: hovered && text !== ""
                                ToolTip.delay: 600
                                ToolTip.text: text
                            }
                        }
                    }
                }
            }
        }
    }
}
