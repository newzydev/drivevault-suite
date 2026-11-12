import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

// Welcome screen: branding, New / Open, recent projects, quick start.
Rectangle {
    id: root

    signal newProjectRequested()
    signal openProjectRequested()
    signal folderProjectRequested()
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

    // (language, About and Settings are in the title bar now)

    RowLayout {
        anchors.fill: parent
        anchors.margins: 56
        anchors.topMargin: 56
        spacing: 56

        // ---- Brand + actions ---------------------------------------------------------------
        ColumnLayout {
            Layout.preferredWidth: 440
            Layout.maximumWidth: 480
            Layout.fillHeight: true
            spacing: 0

            Logo { size: 76 }

            AppText {
                Layout.topMargin: 26
                Layout.fillWidth: true
                text: "DriveVault"
                font.pixelSize: Theme.fontHero
                font.weight: Font.Bold
                color: Theme.text
            }
            AppText {
                Layout.fillWidth: true
                text: "Video Editor Pro"
                font.pixelSize: Theme.fontXxl
                font.weight: Font.Medium
                color: Theme.accentText
            }
            AppText {
                Layout.topMargin: 10
                Layout.fillWidth: true
                text: qsTr("Professional Power. Beautiful Simplicity.")
                font.pixelSize: Theme.fontLg
                color: Theme.textSecondary
                wrapMode: Text.Wrap
            }

            ColumnLayout {
                Layout.topMargin: 34
                Layout.fillWidth: true
                spacing: 10
                AppButton {
                    objectName: "homeNewProject"
                    Layout.fillWidth: true
                    variant: "primary"
                    iconName: "plus"
                    text: qsTr("New Project")
                    implicitHeight: 46
                    font.pixelSize: Theme.fontLg
                    onClicked: root.newProjectRequested()
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    AppButton {
                        objectName: "homeOpenProject"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        iconName: "folder"
                        text: qsTr("Open Project")
                        implicitHeight: 42
                        leftPadding: 12
                        rightPadding: 12
                        onClicked: root.openProjectRequested()
                    }
                    AppButton {
                        objectName: "homeFolderProject"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        iconName: "import"
                        text: qsTr("Project from folder")
                        implicitHeight: 42
                        leftPadding: 12
                        rightPadding: 12
                        onClicked: root.folderProjectRequested()
                        AppToolTip { visible: parent.hovered; text: qsTr("Choose a folder: a project named after it is created and every video, picture and audio file in it is imported.") }
                    }
                }
            }

            // Quick start
            AppText {
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
                        AppText {
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
                            AppText {
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
                        AppText {
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

            AppText {
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
                    Layout.rightMargin: 31
                    Layout.bottomMargin: 12
                    AppText {
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
                        implicitHeight: 32
                        rightPadding: 8
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
                        readonly property bool scrolls: contentHeight > height

                        delegate: ItemDelegate {
                            id: card
                            required property int index
                            required property string name
                            required property string path
                            required property string lastOpenedText
                            required property bool exists

                            width: ListView.view.width - (ListView.view.scrolls ? 12 : 0)
                            height: 72
                            padding: 0
                            leftPadding: 12
                            rightPadding: 20                  // the remove button keeps its distance from the edge of the row
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
                                    AppText {
                                        Layout.fillWidth: true
                                        text: card.name
                                        font.pixelSize: Theme.fontMd
                                        font.weight: Font.DemiBold
                                        color: Theme.text
                                        elide: Text.ElideRight
                                    }
                                    AppText {
                                        Layout.fillWidth: true
                                        text: card.path
                                        font.pixelSize: Theme.fontXs
                                        color: Theme.textSecondary
                                        elide: Text.ElideMiddle
                                    }
                                    RowLayout {
                                        spacing: 8
                                        AppText {
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
