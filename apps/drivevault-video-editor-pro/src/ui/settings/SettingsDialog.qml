import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

// Settings window: category navigation on the left, page on the right. Every change is saved immediately.
Popup {
    id: control

    signal resetLayoutsRequested()

    readonly property var pages: [
        { key: "general",     icon: "globe",    label: qsTr("General") },
        { key: "appearance",  icon: "paint",    label: qsTr("Appearance") },
        { key: "performance", icon: "bolt",     label: qsTr("Performance") },
        { key: "editing",     icon: "scissors", label: qsTr("Editing") },
        { key: "audio",       icon: "volume",   label: qsTr("Audio") },
        { key: "ai",          icon: "sparkles", label: qsTr("AI") },
        { key: "export",      icon: "export",   label: qsTr("Export") },
        { key: "about",       icon: "info",     label: qsTr("About") }
    ]

    function pageTitle() {
        for (const p of pages)
            if (p.key === App.settingsPage)
                return p.label
        return ""
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    padding: 0
    focus: true
    closePolicy: Popup.CloseOnEscape
    width: Math.min(1040, (parent ? parent.width : 1040) - 64)
    height: Math.min(720, (parent ? parent.height : 720) - 64)
    visible: App.settingsOpen
    onClosed: App.settingsOpen = false
    Overlay.modal: Rectangle { color: Theme.overlay }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal }
            NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationFast } }

    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surface
        border.color: Theme.border
    }

    Component { id: generalPage; GeneralPage { onResetLayoutsRequested: control.resetLayoutsRequested() } }
    Component { id: appearancePage; AppearancePage { onResetLayoutsRequested: control.resetLayoutsRequested() } }
    Component { id: performancePage; PerformancePage {} }
    Component { id: editingPage; EditingPage {} }
    Component { id: audioPage; AudioPage {} }
    Component { id: aiPage; AiPage {} }
    Component { id: exportPage; ExportSettingsPage {} }
    Component { id: aboutPage; AboutPage {} }

    readonly property var pageComponents: ({
        "general": generalPage, "appearance": appearancePage, "performance": performancePage,
        "editing": editingPage, "audio": audioPage, "ai": aiPage, "export": exportPage, "about": aboutPage
    })

    contentItem: RowLayout {
        spacing: 0

        // ---- Navigation ----------------------------------------------------------------------
        Rectangle {
            Layout.preferredWidth: 230
            Layout.fillHeight: true
            color: Theme.backgroundAlt
            radius: Theme.radiusLarge

            Rectangle {   // square off the right-hand corners
                anchors.right: parent.right
                width: parent.radius
                height: parent.height
                color: parent.color
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 4

                Text {
                    Layout.margins: 8
                    Layout.bottomMargin: 14
                    text: qsTr("Settings")
                    font.pixelSize: Theme.fontXl
                    font.weight: Font.Bold
                    color: Theme.text
                }

                Repeater {
                    model: control.pages
                    delegate: AbstractButton {
                        id: navItem
                        objectName: "settingsNav_" + modelData.key
                        required property var modelData
                        readonly property bool current: App.settingsPage === modelData.key
                        Layout.fillWidth: true
                        implicitHeight: 38
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: modelData.label
                        onClicked: App.settingsPage = modelData.key

                        contentItem: RowLayout {
                            spacing: 10
                            Item { Layout.preferredWidth: 4 }
                            Icon {
                                name: navItem.modelData.icon
                                size: 17
                                color: navItem.current ? Theme.accentText : Theme.textSecondary
                            }
                            Text {
                                Layout.fillWidth: true
                                text: navItem.modelData.label
                                font.pixelSize: Theme.fontMd
                                font.weight: navItem.current ? Font.DemiBold : Font.Normal
                                color: navItem.current ? Theme.text : Theme.textSecondary
                            }
                        }
                        background: Rectangle {
                            radius: Theme.radiusSmall + 2
                            color: navItem.current ? Theme.accentSubtle : navItem.hovered ? Theme.surface : "transparent"
                            border.width: navItem.visualFocus ? 2 : 0
                            border.color: Theme.accent
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Text {
                    Layout.margins: 8
                    text: "v" + SysInfo.version
                    font.pixelSize: Theme.fontXs
                    color: Theme.textDisabled
                }
            }
        }

        // ---- Page ----------------------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 32
                Layout.rightMargin: 14
                Layout.topMargin: 16
                Layout.bottomMargin: 8
                Text {
                    Layout.fillWidth: true
                    text: control.pageTitle()
                    font.pixelSize: Theme.fontXxl
                    font.weight: Font.Bold
                    color: Theme.text
                }
                IconButton { iconName: "close"; tip: qsTr("Close"); onClicked: control.close() }
            }

            // Restart banner (only when a setting that needs it was changed)
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 32
                Layout.rightMargin: 32
                visible: AppSettings.restartRequired
                implicitHeight: bannerRow.implicitHeight + 20
                radius: Theme.radius
                color: Qt.alpha(Theme.warning, 0.12)
                border.color: Qt.alpha(Theme.warning, 0.5)

                RowLayout {
                    id: bannerRow
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10
                    Icon { name: "refresh"; color: Theme.warning; size: 17 }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Some changes take effect after restarting the application.")
                        font.pixelSize: Theme.fontSm
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }
                    AppButton { text: qsTr("Restart now"); variant: "primary"; onClicked: App.restart() }
                }
            }

            Flickable {
                id: flick
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: loader.height + 40
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}

                Loader {
                    id: loader
                    x: 32
                    y: 8
                    width: flick.width - 64
                    height: item ? item.implicitHeight : 0
                    sourceComponent: control.pageComponents[App.settingsPage]
                    onLoaded: flick.contentY = 0
                }
            }
        }
    }
}
