import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

// Welcome screen: branding, New / Open, recent projects, quick start.
Rectangle {
    id: root

    signal newProjectRequested()
    signal openProjectRequested()
    signal openRecentRequested(string path)

    color: Theme.background

    // Soft accent glow behind the brand block (purely decorative, cheap gradient).
    Rectangle {
        width: 760
        height: 760
        radius: 380
        x: -220
        y: -260
        opacity: Theme.dark ? 0.16 : 0.10
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.accent }
            GradientStop { position: 1.0; color: Qt.alpha(Theme.accent, 0) }
        }
    }

    // Top-right utility buttons
    RowLayout {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 18
        spacing: 4
        z: 2

        IconButton {
            iconName: "globe"
            tip: qsTr("Switch language")
            onClicked: AppSettings.set("language", Localization.currentLanguage === "th-TH" ? "en-US" : "th-TH")
        }
        IconButton {
            iconName: "info"
            tip: qsTr("About")
            onClicked: App.aboutOpen = true
        }
        IconButton {
            iconName: "sliders"
            tip: qsTr("Settings") + "  (Ctrl+,)"
            onClicked: App.settingsOpen = true
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 56
        anchors.topMargin: 64
        spacing: 56

        // ---- Brand + actions ---------------------------------------------------------------
        ColumnLayout {
            Layout.preferredWidth: 400
            Layout.maximumWidth: 460
            Layout.fillHeight: true
            spacing: 0

            Logo { size: 76 }

            Text {
                Layout.topMargin: 26
                Layout.fillWidth: true
                text: "DriveVault"
                font.pixelSize: Theme.fontHero
                font.weight: Font.Bold
                color: Theme.text
            }
            Text {
                Layout.fillWidth: true
                text: "Video Editor Pro"
                font.pixelSize: Theme.fontXxl
                font.weight: Font.Medium
                color: Theme.accentText
            }
            Text {
                Layout.topMargin: 10
                Layout.fillWidth: true
                text: qsTr("Professional Power. Beautiful Simplicity.")
                font.pixelSize: Theme.fontLg
                color: Theme.textSecondary
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.topMargin: 34
                spacing: 12
                AppButton {
                    objectName: "homeNewProject"
                    variant: "primary"
                    iconName: "plus"
                    text: qsTr("New Project")
                    implicitHeight: 42
                    leftPadding: 20
                    rightPadding: 20
                    font.pixelSize: Theme.fontLg
                    onClicked: root.newProjectRequested()
                }
                AppButton {
                    objectName: "homeOpenProject"
                    iconName: "folder"
                    text: qsTr("Open Project")
                    implicitHeight: 42
                    leftPadding: 20
                    rightPadding: 20
                    font.pixelSize: Theme.fontLg
                    onClicked: root.openProjectRequested()
                }
            }

            // Quick start
            Text {
                Layout.topMargin: 44
                text: qsTr("Quick start")
                font.pixelSize: Theme.fontSm
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.8
                color: Theme.textSecondary
            }

            Repeater {
                model: [
                    { n: "1", title: qsTr("Create a project"), desc: qsTr("Choose a name, resolution and frame rate."), status: "available" },
                    { n: "2", title: qsTr("Import your media"), desc: qsTr("Video, audio and images - or just drag files in from Explorer."), status: "available" },
                    { n: "3", title: qsTr("Edit and export"), desc: qsTr("Arrange clips on the timeline and render an MP4."), status: "available" }
                ]
                delegate: RowLayout {
                    required property var modelData
                    Layout.topMargin: 12
                    Layout.fillWidth: true
                    spacing: 12

                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        width: 26
                        height: 26
                        radius: 13
                        color: Theme.accentSubtle
                        Text {
                            anchors.centerIn: parent
                            text: modelData.n
                            font.pixelSize: Theme.fontSm
                            font.weight: Font.Bold
                            color: Theme.accentText
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            spacing: 8
                            Text {
                                text: modelData.title
                                font.pixelSize: Theme.fontMd
                                font.weight: Font.Medium
                                color: Theme.text
                            }
                            StatusBadge {
                                visible: modelData.status !== "available"
                                kind: modelData.status
                                phase: "Phase 2"
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.desc
                            font.pixelSize: Theme.fontSm
                            color: Theme.textSecondary
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }

            Text {
                text: "v" + SysInfo.version + "  ·  Build " + SysInfo.buildNumber
                font.pixelSize: Theme.fontXs
                color: Theme.textDisabled
            }
        }

        // ---- Recent projects -------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusLarge
            color: Theme.backgroundAlt
            border.color: Theme.borderSubtle

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 20
                    Layout.bottomMargin: 12
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Recent Projects")
                        font.pixelSize: Theme.fontXl
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }
                    AppButton {
                        visible: Recents.count > 0
                        variant: "ghost"
                        iconName: "trash"
                        text: qsTr("Clear list")
                        onClicked: Recents.clear()
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    EmptyState {
                        anchors.centerIn: parent
                        visible: Recents.count === 0
                        iconName: "clock"
                        title: qsTr("No recent projects")
                        description: qsTr("Projects you create or open will appear here so you can jump back in quickly.")
                        AppButton {
                            Layout.alignment: Qt.AlignHCenter
                            variant: "primary"
                            iconName: "plus"
                            text: qsTr("Create your first project")
                            onClicked: root.newProjectRequested()
                        }
                    }

                    ListView {
                        id: list
                        anchors.fill: parent
                        anchors.margins: 10
                        visible: Recents.count > 0
                        model: Recents
                        clip: true
                        spacing: 4
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: AppScrollBar {}

                        delegate: ItemDelegate {
                            id: card
                            required property int index
                            required property string name
                            required property string path
                            required property string lastOpenedText
                            required property bool exists

                            width: ListView.view.width - 6
                            height: 72
                            padding: 0
                            hoverEnabled: true
                            focusPolicy: Qt.StrongFocus
                            onClicked: exists ? root.openRecentRequested(path) : App.toast(qsTr("The project file was not found. Remove it from the list or restore the file."), "warning")

                            background: Rectangle {
                                radius: Theme.radius
                                color: card.hovered || card.visualFocus ? Theme.surface : "transparent"
                                border.width: card.visualFocus ? 2 : 0
                                border.color: Theme.accent
                            }

                            contentItem: RowLayout {
                                spacing: 14
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 8

                                Rectangle {
                                    Layout.preferredWidth: 78
                                    Layout.preferredHeight: 48
                                    radius: Theme.radiusSmall
                                    color: Theme.surfaceElevated
                                    border.color: Theme.border
                                    Icon {
                                        anchors.centerIn: parent
                                        name: card.exists ? "film" : "warning"
                                        size: 22
                                        color: card.exists ? Theme.textSecondary : Theme.warning
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Text {
                                        Layout.fillWidth: true
                                        text: card.name
                                        font.pixelSize: Theme.fontMd
                                        font.weight: Font.DemiBold
                                        color: Theme.text
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: card.path
                                        font.pixelSize: Theme.fontXs
                                        color: Theme.textSecondary
                                        elide: Text.ElideMiddle
                                    }
                                    RowLayout {
                                        spacing: 8
                                        Text {
                                            text: qsTr("Last opened") + ": " + card.lastOpenedText
                                            font.pixelSize: Theme.fontXs
                                            color: Theme.textDisabled
                                        }
                                        StatusBadge {
                                            visible: !card.exists
                                            kind: "unsupported"
                                            label: qsTr("File not found")
                                        }
                                    }
                                }
                                IconButton {
                                    iconName: "close"
                                    tip: qsTr("Remove from list")
                                    onClicked: Recents.removeAt(card.index)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
