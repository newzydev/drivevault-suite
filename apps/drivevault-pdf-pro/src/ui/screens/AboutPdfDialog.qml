import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

Popup {
    id: dialog
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(410, parent.width - 48)
    padding: 24
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { radius: Theme.radiusLarge; color: Theme.backgroundAlt; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            spacing: 12
            Image {
                source: "qrc:/icons/app.png"
                sourceSize.width: 80
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                AppText { Layout.fillWidth: true; text: Tr.s("title"); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold }
                AppText {
                    objectName: "aboutVersion"
                    Layout.fillWidth: true
                    text: Tr.f2("aboutBuild", Doc.version, Doc.buildNumber)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSm
                }
            }
        }
        AppText { Layout.fillWidth: true; text: Tr.s("aboutLocal"); wrapMode: Text.Wrap }
        PButton { objectName: "aboutClose"; Layout.alignment: Qt.AlignRight; text: Tr.s("close"); onClicked: dialog.close() }
    }
}
