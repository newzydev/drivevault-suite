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
            AppText {
                text: "DriveVault Video Editor Pro"
                font.pixelSize: Theme.fontXl
                font.weight: Font.Bold
                color: Theme.text
            }
            AppText {
                text: qsTr("Version") + " " + SysInfo.version + "  ·  Build " + SysInfo.buildNumber
                font.pixelSize: Theme.fontMd
                color: Theme.textSecondary
            }
            AppText {
                text: qsTr("Professional Power. Beautiful Simplicity.")
                font.pixelSize: Theme.fontSm
                color: Theme.accentText
            }
        }
    }

    AppText {
        Layout.fillWidth: true
        text: qsTr("Professional video editing for Windows: media import, a multitrack timeline with undo / redo, effects, titles, colour and audio mixing, "
                   + "offline AI tools (silence and scene detection, captions, noise reduction, tracking), project save / load and MP4 export. "
                   + "Everything runs on this computer.")
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
