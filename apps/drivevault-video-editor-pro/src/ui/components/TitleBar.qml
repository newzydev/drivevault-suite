import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

// The title bar of the window, the same as in DriveVault Master Pro and PDF Pro: the brand at the left (drag it to move the window, double click to
// maximize / restore), the language switch, one "more" menu, and the window buttons. The window has no Windows frame (Qt.FramelessWindowHint).
Rectangle {
    id: bar

    required property Window hostWindow
    readonly property bool maximized: hostWindow.visibility === Window.Maximized
    readonly property bool thai: Localization.currentLanguage === "th-TH"

    implicitHeight: 64
    color: Theme.dark ? "#0D0E11" : Theme.backgroundAlt

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

        // Only the brand and the empty space drag the window; the controls own their pointer events.
        Item {
            objectName: "titleDragArea"
            Layout.fillWidth: true
            Layout.fillHeight: true
            RowLayout {
                anchors.fill: parent
                spacing: 12
                Logo { size: 34; Layout.alignment: Qt.AlignVCenter }
                AppText {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    verticalAlignment: Text.AlignVCenter
                    text: "DriveVault Video Editor Pro"
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

        // language: one small switch
        Rectangle {
            objectName: "languageSwitch"
            implicitHeight: 32
            implicitWidth: langRow.implicitWidth + 6
            radius: 16
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
            Row {
                id: langRow
                anchors.centerIn: parent
                spacing: 2
                Repeater {
                    model: [ { code: "th-TH", label: "ไทย", name: "langTh" }, { code: "en-US", label: "EN", name: "langEn" } ]
                    delegate: AbstractButton {
                        required property var modelData
                        objectName: modelData.name
                        readonly property bool current: Localization.currentLanguage === modelData.code
                        implicitWidth: 46
                        implicitHeight: 26
                        hoverEnabled: true
                        focusPolicy: Qt.NoFocus
                        onClicked: AppSettings.set("language", modelData.code)
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        contentItem: AppText {
                            text: parent.modelData.label
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            color: parent.current ? Theme.textOnAccent : Theme.textSecondary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle { radius: 13; color: parent.current ? Theme.accent : parent.hovered ? Theme.surfaceHover : "transparent" }
                    }
                }
            }
        }

        // everything else (rarely needed) is in this menu
        AppButton {
            id: moreButton
            objectName: "titleMenuButton"
            Layout.preferredWidth: 34
            Layout.preferredHeight: 32
            Layout.rightMargin: 4
            implicitHeight: 32
            leftPadding: 0
            rightPadding: 0
            text: "⋯"
            font.pixelSize: 18
            tip: qsTr("More options")
            onClicked: titleMenu.visible ? titleMenu.close() : titleMenu.popup(width - titleMenu.implicitWidth, height + 6)
            AppMenu {
                id: titleMenu
                AppMenuItem { objectName: "menuSettings"; text: qsTr("Settings…"); shortcutText: "Ctrl+,"; onTriggered: bar.settingsRequested() }
                AppMenuItem { objectName: "menuAbout"; text: qsTr("About DriveVault Video Editor Pro"); shortcutText: "F1"; onTriggered: bar.aboutRequested() }
            }
        }

        // window buttons: one group, rounded like the other compact buttons, close together
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; Layout.leftMargin: 4; color: Theme.border }
        Row {
            spacing: 2
            WindowButton { objectName: "winMinimize"; kind: "minimize"; text: qsTr("Minimize"); onClicked: bar.hostWindow.showMinimized() }
            WindowButton { objectName: "winMaximize"; kind: bar.maximized ? "restore" : "maximize"; text: bar.maximized ? qsTr("Restore") : qsTr("Maximize"); onClicked: bar.toggleMaximized() }
            WindowButton { objectName: "winClose"; kind: "close"; text: qsTr("Close"); onClicked: bar.hostWindow.close() }
        }
    }
}
