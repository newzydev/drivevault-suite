import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

AppDialog {
    id: control

    title: qsTr("About")
    implicitWidth: 520

    RowLayout {
        Layout.fillWidth: true
        spacing: 20
        Logo { size: 72 }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            Text {
                text: "DriveVault Video Editor Pro"
                font.pixelSize: Theme.fontXl
                font.weight: Font.Bold
                color: Theme.text
            }
            Text {
                text: qsTr("Version") + " " + SysInfo.version + "  ·  Build " + SysInfo.buildNumber
                font.pixelSize: Theme.fontMd
                color: Theme.textSecondary
            }
            Text {
                text: qsTr("Professional Power. Beautiful Simplicity.")
                font.pixelSize: Theme.fontSm
                color: Theme.accentText
            }
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Phase 2 build: media import, preview with audio, multitrack timeline editing with undo / redo, project save / load and MP4 export. "
                   + "Effects, titles, color and audio mixing arrive in Phase 3, AI in Phase 4.")
        font.pixelSize: Theme.fontSm
        color: Theme.textSecondary
        wrapMode: Text.Wrap
    }

    footerContent: [
        Item { Layout.fillWidth: true },
        AppButton {
            text: qsTr("Details")
            variant: "ghost"
            onClicked: { control.close(); App.settingsPage = "about"; App.settingsOpen = true }
        },
        AppButton {
            text: qsTr("Close")
            variant: "primary"
            onClicked: control.close()
        }
    ]
}
