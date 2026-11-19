import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Browser.Backend

Rectangle {
    id: page

    property bool privateMode: false
    property var sites: privateMode ? [] : Browser.history.topSites(8)
    readonly property var shortcutSites: privateMode ? [] : [{ title: "Google", url: "https://www.google.com/" }]
        .concat(Browser.bookmarks.items.filter(b => !/^https:\/\/www\.google\.com\/?$/i.test(b.url)).slice(0, 7))
    readonly property var visibleSites: siteTabs.currentIndex === 0 ? shortcutSites : sites
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
                placeholderTextColor: page.privateMode ? Theme.textSecondary : "#496171"
                color: page.privateMode ? Theme.text : "#142633"
                font.pixelSize: 15
                leftPadding: 46
                rightPadding: 22
                selectByMouse: true
                background: Rectangle {
                    radius: height / 2
                    color: page.privateMode ? "#F219242F" : "#F4F9FD"
                    border.width: box.activeFocus ? 2 : 1
                    border.color: box.activeFocus ? page.accent : "#8EA9BA"
                    LineIcon { x: 18; anchors.verticalCenter: parent.verticalCenter; kind: "search"; color: page.privateMode ? page.accent : "#22637B" }
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

                        ColumnLayout {
                visible: !page.privateMode
                Layout.fillWidth: true
                Layout.topMargin: 2
                spacing: 14

                Item {
                    Layout.fillWidth: true
                    implicitHeight: siteTabs.implicitHeight
                    TabBar {
                        id: siteTabs
                        objectName: "workspaceTabs"
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(parent.width, 252)
                        padding: 3
                        spacing: 3
                        background: Rectangle {
                            radius: height / 2
                            color: "#B00B151E"
                            border.color: "#507790A1"
                        }
                        WorkspaceTab { text: Tr.t("ntp_shortcuts") }
                        WorkspaceTab { text: Tr.t("ntp_top") }
                    }
                }

                Text {
                    visible: page.visibleSites.length === 0
                    Layout.fillWidth: true
                    topPadding: 12
                    bottomPadding: 12
                    text: Tr.t("ntp_history_empty")
                    color: Theme.text
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }

                Item {
                    visible: page.visibleSites.length > 0
                    Layout.fillWidth: true
                    implicitHeight: siteGrid.implicitHeight
                    Flow {
                        id: siteGrid
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: columns * tileWidth + Math.max(0, columns - 1) * spacing
                        readonly property int tileWidth: 78
                        readonly property int columns: Math.min(page.visibleSites.length,
                            Math.max(1, Math.floor((dashboard.width + spacing) / (tileWidth + spacing))))
                        spacing: 9

                        Repeater {
                            model: page.visibleSites
                            delegate: AbstractButton {
                                id: shortcut
                                required property var modelData
                                readonly property string host: Browser.hostOf(modelData.url).replace(/^www\./, "")
                                width: siteGrid.tileWidth
                                height: 78
                                padding: 4
                                hoverEnabled: true
                                text: modelData.title || host
                                Accessible.name: text
                                onClicked: page.go(modelData.url)
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                background: Rectangle {
                                    radius: 12
                                    color: shortcut.down ? "#60577A8B"
                                        : shortcut.hovered ? "#40819BAC" : "transparent"
                                    border.width: shortcut.visualFocus ? 1 : 0
                                    border.color: page.accent
                                }
                                contentItem: Column {
                                    spacing: 7
                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: 44
                                        height: 44
                                        radius: width / 2
                                        color: "#E0182A36"
                                        border.color: "#6979919F"
                                        Text {
                                            anchors.centerIn: parent
                                            text: (shortcut.host || "?").charAt(0).toUpperCase()
                                            color: "#BCEFFA"
                                            font.pixelSize: 18
                                            font.weight: Font.DemiBold
                                        }
                                    }
                                    Text {
                                        id: shortcutLabel
                                        width: parent.width
                                        text: shortcut.text
                                        color: Theme.text
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                        style: Text.Outline
                                        styleColor: "#A0001016"
                                    }
                                }
                                ToolTip.visible: hovered && shortcutLabel.truncated
                                ToolTip.delay: 700
                                ToolTip.text: text
                            }
                        }
                    }
                }
            }
        }
    }

    component WorkspaceTab: TabButton {
        id: tab
        implicitHeight: 30
        hoverEnabled: true
        font.pixelSize: 13
        font.weight: Font.DemiBold
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        background: Rectangle {
            radius: height / 2
            color: tab.checked ? "#285266" : tab.hovered ? "#223542" : "transparent"
            border.width: tab.checked || tab.visualFocus ? 1 : 0
            border.color: tab.visualFocus ? page.accent : "#527C8B"
        }
        contentItem: Text {
            text: tab.text
            font: tab.font
            color: tab.checked ? "#B4F0FA" : Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
}
