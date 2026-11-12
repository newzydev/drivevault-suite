import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

Rectangle {
    id: bar
    required property Window hostWindow
    readonly property bool maximized: hostWindow.visibility === Window.Maximized
    implicitHeight: 64
    color: Theme.titleBar
    signal settingsRequested()
    signal aboutRequested()

    function toggleMaximized() {
        if (maximized)
            hostWindow.showNormal()
        else
            hostWindow.showMaximized()
    }

    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 28
        anchors.rightMargin: 24
        spacing: 12

        // Only the brand and blank space drag the window. Controls own their pointer events.
        Item {
            objectName: "titleDragArea"
            Layout.fillWidth: true
            Layout.fillHeight: true
            RowLayout {
                anchors.fill: parent
                spacing: 12
                Image {
                    source: "qrc:/icons/app.png"
                    sourceSize.width: 68
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 34
                }
                Text {                                      // a plain Text like Master Pro: AppText's fixed Thai line height would sit the title too high
                    Layout.fillWidth: true
                    text: Tr.s("title")
                    elide: Text.ElideRight
                    font.pixelSize: 17
                    font.weight: Font.Bold
                    color: Theme.text
                }
            }
            DragHandler {
                target: null
                onActiveChanged: if (active) bar.hostWindow.startSystemMove()
            }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onDoubleTapped: bar.toggleMaximized()
            }
        }

        LanguageSwitch {}
        PButton {
            objectName: "titleMenuButton"
            compact: true
            Layout.preferredWidth: 34
            Layout.rightMargin: 4
            leftPadding: 0
            rightPadding: 0
            labelPixelSize: 18
            cornerRadius: 9
            text: "⋯"
            tip: Tr.s("moreOptions")
            onClicked: titleMenu.visible ? titleMenu.close() : titleMenu.popup(width - titleMenu.width, height + 6)
            PMenu {
                id: titleMenu
                width: 250
                PMenuItem {
                    objectName: "themeToggle"
                    text: Tr.s(Theme.light ? "switchToDark" : "switchToLight")
                    onTriggered: Doc.lightTheme = !Doc.lightTheme
                }
                PMenuItem {
                    objectName: "settingsButton"
                    text: Tr.s("preferences")
                    onTriggered: bar.settingsRequested()
                }
                MenuSeparator { padding: 4; contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
                PMenuItem {
                    objectName: "aboutButton"
                    text: Tr.s("about")
                    onTriggered: bar.aboutRequested()
                }
            }
        }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; Layout.leftMargin: 4; color: Theme.border }
        Row {
            spacing: 2
            WindowButton {
                objectName: "winMinimize"
                kind: "minimize"
                text: Tr.s("minimizeWindow")
                onClicked: bar.hostWindow.showMinimized()
            }
            WindowButton {
                objectName: "winMaximize"
                kind: bar.maximized ? "restore" : "maximize"
                text: Tr.s(bar.maximized ? "restoreWindow" : "maximizeWindow")
                onClicked: bar.toggleMaximized()
            }
            WindowButton {
                objectName: "winClose"
                kind: "close"
                text: Tr.s("close")
                onClicked: bar.hostWindow.close()
            }
        }
    }
}
