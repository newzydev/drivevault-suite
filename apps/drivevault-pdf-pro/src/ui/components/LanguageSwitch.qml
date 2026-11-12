import QtQuick
import QtQuick.Controls
import DriveVault.PdfPro.Backend

Rectangle {
    id: control
    objectName: "languageToggle"
    implicitWidth: choices.implicitWidth + 6
    implicitHeight: 32
    radius: 16
    color: Theme.surface
    border.color: Theme.border

    Row {
        id: choices
        anchors.centerIn: parent
        spacing: 2
        Repeater {
            model: [
                { code: "th", label: "languageThaiShort", tip: "switchToThai" },
                { code: "en", label: "languageEnglishShort", tip: "switchToEnglish" }
            ]
            delegate: AbstractButton {
                id: choice
                required property var modelData
                objectName: control.objectName + "_" + modelData.code
                implicitWidth: 46
                implicitHeight: 26
                hoverEnabled: true
                readonly property bool current: Doc.language === modelData.code
                text: Tr.s(modelData.label)
                Accessible.name: Tr.s(modelData.tip)
                Accessible.checkable: true
                Accessible.checked: current
                onClicked: Doc.language = modelData.code
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                contentItem: Text {                          // plain Text like Master Pro, so the letters sit in the middle of the pill
                    text: choice.text
                    color: choice.current ? Theme.textOnAccent : Theme.textSecondary
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 13
                    color: choice.current ? Theme.accent : choice.hovered ? Theme.surfaceHover : "transparent"
                    border.width: choice.visualFocus ? 1 : 0
                    border.color: Theme.text
                }
            }
        }
    }
}
