import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Master.Backend

Popup {
    id: dlg
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    padding: 0
    width: Math.min(480, parent ? parent.width - 48 : 480)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: "#B3000000" }

    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 12
        Layout.margins: 24
        RowLayout {
            Layout.margins: 24
            Layout.bottomMargin: 0
            spacing: 16
            Image { source: "qrc:/qt/qml/DriveVault/Master/resources/icons/app.png"; Layout.preferredWidth: 64; Layout.preferredHeight: 64; sourceSize.width: 128; smooth: true }
            ColumnLayout {
                spacing: 2
                Text { text: "DriveVault Master Pro"; font.pixelSize: 20; font.weight: Font.Bold; color: Theme.text }
                Text { text: Tr.s("version") + " " + Hub.version + "  ·  build " + Hub.buildNumber; font.pixelSize: 12; color: Theme.textSecondary }
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 24; Layout.rightMargin: 24
            text: Tr.s("aboutText")
            wrapMode: Text.Wrap; lineHeight: Tr.bodyLine
            font.pixelSize: 13
            color: Theme.textSecondary
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 24; Layout.rightMargin: 24
            text: Tr.s("honest")
            wrapMode: Text.Wrap; lineHeight: Tr.bodyLine
            font.pixelSize: 12
            color: Theme.textDisabled
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 24; Layout.rightMargin: 24
            text: Tr.s("suiteFolder") + ": " + Hub.suiteDirectory
            wrapMode: Text.WrapAnywhere
            font.pixelSize: 12
            color: Theme.textSecondary
        }
        RowLayout {
            Layout.margins: 24
            Layout.topMargin: 8
            MButton { text: Tr.s("notices"); onClicked: Hub.openDocument("THIRD-PARTY-NOTICES.txt") }
            Item { Layout.fillWidth: true }
            MButton { variant: "primary"; text: Tr.s("close"); onClicked: dlg.close() }
        }
    }
}
