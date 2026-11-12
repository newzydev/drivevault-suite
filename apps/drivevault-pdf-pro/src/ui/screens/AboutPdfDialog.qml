import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// The same layout as the About window of Master Pro: icon, name, version and build; what it is; a line of honest scope; the notices and Close.
Popup {
    id: dialog
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(480, parent ? parent.width - 48 : 480)
    padding: 0
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: "#B3000000" }
    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.margins: 24
            Layout.bottomMargin: 0
            spacing: 16
            Image {
                source: "qrc:/icons/app.png"
                sourceSize.width: 128
                Layout.preferredWidth: 64
                Layout.preferredHeight: 64
                smooth: true
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { Layout.fillWidth: true; text: Tr.s("title"); font.pixelSize: 20; font.weight: Font.Bold; color: Theme.text; wrapMode: Text.Wrap }
                Text {
                    objectName: "aboutVersion"
                    Layout.fillWidth: true
                    text: Tr.f2("aboutBuild", Doc.version, Doc.buildNumber)
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
            }
        }
        AppText {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            text: Tr.s("aboutLocal")
            color: Theme.textSecondary
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
        AppText {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            text: Tr.s("aboutHonest")
            color: Theme.textDisabled
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.margins: 24
            Layout.topMargin: 8
            PButton { objectName: "aboutNotices"; text: Tr.s("notices"); onClicked: Doc.openDocument("THIRD-PARTY-NOTICES.txt") }
            Item { Layout.fillWidth: true }
            PButton {
                objectName: "aboutClose"
                text: Tr.s("close")
                variant: "primary"
                onClicked: dialog.close()
            }
        }
    }
}
