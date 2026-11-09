import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Generic confirmation: primary action, optional secondary (e.g. "Don't save") and cancel.
AppDialog {
    id: control

    property string message
    property string primaryText: qsTr("OK")
    property string secondaryText
    property string cancelText: qsTr("Cancel")
    property bool dangerPrimary: false

    signal primary()
    signal secondary()
    signal cancelled()

    implicitWidth: 460
    showClose: false
    closePolicy: Popup.CloseOnEscape
    onClosed: if (!handled) cancelled()
    onAboutToShow: handled = false

    property bool handled: false

    AppText {
        Layout.fillWidth: true
        text: control.message
        font.pixelSize: Theme.fontMd
        color: Theme.textSecondary
        wrapMode: Text.Wrap
    }

    footerContent: [
        Item { Layout.fillWidth: true },
        AppButton {
            objectName: "confirmCancel"
            variant: "ghost"
            text: control.cancelText
            onClicked: control.close()
        },
        AppButton {
            objectName: "confirmSecondary"
            visible: control.secondaryText !== ""
            text: control.secondaryText
            onClicked: { control.handled = true; control.close(); control.secondary() }
        },
        AppButton {
            objectName: "confirmPrimary"
            variant: control.dangerPrimary ? "danger" : "primary"
            text: control.primaryText
            focus: true
            onClicked: { control.handled = true; control.close(); control.primary() }
        }
    ]
}
