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
        spacing: 10
        AppText { text: Tr.s("preferences"); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            rowSpacing: 8
            AppText { Layout.fillWidth: true; text: Tr.s("displayLanguage") }
            LanguageSwitch { objectName: "settingsLanguage"; Layout.alignment: Qt.AlignRight }
            AppText { Layout.fillWidth: true; text: Tr.s("lightTheme") }
            Switch {
                objectName: "settingsTheme"
                Layout.alignment: Qt.AlignRight
                Accessible.name: Tr.s("lightTheme")
                checked: Doc.lightTheme
                onToggled: Doc.lightTheme = checked
            }
            AppText { Layout.fillWidth: true; text: Tr.s("darkPagePreference") }
            Switch {
                objectName: "settingsDarkPages"
                Layout.alignment: Qt.AlignRight
                Accessible.name: Tr.s("darkPagePreference")
                checked: Doc.darkPages
                onToggled: Doc.darkPages = checked
            }
        }
        PButton {
            objectName: "settingsClose"
            Layout.alignment: Qt.AlignRight
            text: Tr.s("close")
            onClicked: dialog.close()
        }
    }
}
