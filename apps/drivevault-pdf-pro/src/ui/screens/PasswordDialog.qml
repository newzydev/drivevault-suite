import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Asks for the password of an encrypted file. The password is only handed to the engine; it is not stored anywhere.
Popup {
    id: dlg

    property string path: ""
    property bool wrong: false
    signal submitted(string path, string password)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    width: Math.min(440, (parent ? parent.width : 440) - 48)
    padding: 24
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#B3000000" }

    onOpened: { field.text = ""; field.forceActiveFocus() }

    function submit() {
        if (field.text === "")
            return
        dlg.submitted(dlg.path, field.text)
    }

    background: Rectangle {
        radius: Theme.radius
        color: Theme.backgroundAlt
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: 14
        AppText {
            Layout.fillWidth: true
            text: Tr.s("passwordTitle")
            font.pixelSize: Theme.fontXl
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
        }
        AppText {
            Layout.fillWidth: true
            text: Tr.f("passwordText", dlg.path.split(/[\\/]/).pop())
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
        TextField {
            id: field
            objectName: "passwordField"
            Layout.fillWidth: true
            implicitHeight: 42
            echoMode: TextInput.Password
            placeholderText: Tr.s("password")
            placeholderTextColor: Theme.textDisabled
            color: Theme.text
            font.pixelSize: Theme.fontMd
            selectByMouse: true
            leftPadding: 14
            rightPadding: 14
            verticalAlignment: TextInput.AlignVCenter
            onAccepted: dlg.submit()
            Keys.onEscapePressed: dlg.close()
            background: Rectangle {
                radius: 10
                color: Theme.surface
                border.width: field.activeFocus ? 2 : 1
                border.color: dlg.wrong ? Theme.error : field.activeFocus ? Theme.accent : Theme.border
            }
        }
        AppText {
            Layout.fillWidth: true
            visible: dlg.wrong
            text: Tr.s("wrongPassword")
            color: Theme.errorSoft
            font.pixelSize: Theme.fontSm
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PButton { objectName: "passwordCancel"; variant: "ghost"; text: Tr.s("cancel"); onClicked: dlg.close() }
            PButton { objectName: "passwordOpen"; variant: "primary"; text: Tr.s("open"); enabled: field.text !== ""; onClicked: dlg.submit() }
        }
    }
}
