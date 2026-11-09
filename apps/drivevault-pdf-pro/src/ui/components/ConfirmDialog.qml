import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A question with two answers ("confirm" is the risky one). Used before things that would lose work.
Popup {
    id: dlg

    property string title: ""
    property string text: ""
    property string confirmText: ""
    property string cancelText: ""
    signal confirmed()

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    width: Math.min(460, (parent ? parent.width : 460) - 48)
    padding: 24
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#B3000000" }
    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    contentItem: ColumnLayout {
        spacing: 14
        AppText { Layout.fillWidth: true; text: dlg.title; font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold; wrapMode: Text.Wrap }
        AppText { Layout.fillWidth: true; text: dlg.text; color: Theme.textSecondary; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PButton { objectName: "confirmCancel"; variant: "ghost"; text: dlg.cancelText; onClicked: dlg.close() }
            PButton { objectName: "confirmYes"; variant: "primary"; text: dlg.confirmText; onClicked: { dlg.close(); dlg.confirmed() } }
        }
    }
}
