import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// A web address in a document is opened only after the reader says yes (and only http / https / mailto are ever opened).
Popup {
    id: dlg

    property string uri: ""
    signal rejected()

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    width: Math.min(480, (parent ? parent.width : 480) - 48)
    padding: 24
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#B3000000" }

    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 14
        AppText { Layout.fillWidth: true; text: Tr.s("openLinkTitle"); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold; wrapMode: Text.Wrap }
        AppText { Layout.fillWidth: true; text: Tr.s("openLinkText"); color: Theme.textSecondary; wrapMode: Text.Wrap }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: uriText.implicitHeight + 20
            radius: 8
            color: Theme.surface
            border.color: Theme.border
            AppText {
                id: uriText
                objectName: "linkUri"
                anchors.fill: parent
                anchors.margins: 10
                text: dlg.uri
                wrapMode: Text.WrapAnywhere
                font.pixelSize: Theme.fontSm
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PButton { objectName: "linkCancel"; variant: "ghost"; text: Tr.s("cancel"); onClicked: dlg.close() }
            PButton {
                objectName: "linkOpen"
                variant: "primary"
                text: Tr.s("openLink")
                onClicked: { const ok = Doc.openUri(dlg.uri); dlg.close(); if (!ok) dlg.rejected() }
            }
        }
    }
}
