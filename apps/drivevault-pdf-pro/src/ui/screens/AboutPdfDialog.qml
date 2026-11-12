import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

Popup {
    id: dialog
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(480, parent ? parent.width - 48 : 480)
    padding: 24
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: "#B3000000" }
    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Image {
                source: "qrc:/icons/app.png"
                sourceSize.width: 128
                Layout.preferredWidth: 64
                Layout.preferredHeight: 64
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                AppText { Layout.fillWidth: true; text: Tr.s("title"); font.pixelSize: 20; font.weight: Font.Bold; wrapMode: Text.Wrap }
                AppText {
                    objectName: "aboutVersion"
                    Layout.fillWidth: true
                    text: Tr.f2("aboutBuild", Doc.version, Doc.buildNumber)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSm
                }
            }
        }
        AppText { Layout.fillWidth: true; text: Tr.s("aboutLocal"); color: Theme.textSecondary; font.pixelSize: Theme.fontSm; wrapMode: Text.Wrap }
        PButton {
            objectName: "aboutClose"
            Layout.alignment: Qt.AlignRight
            Layout.topMargin: 8
            text: Tr.s("close")
            variant: "primary"
            onClicked: dialog.close()
        }
    }
}
