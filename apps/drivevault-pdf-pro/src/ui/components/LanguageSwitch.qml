import QtQuick
import QtQuick.Controls
import DriveVault.PdfPro.Backend

// Thai / English switch (a small pill), the same as in Master Pro.
Rectangle {
    implicitHeight: 32
    implicitWidth: row.implicitWidth + 6
    radius: 16
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 2
        Repeater {
            model: [ { code: "th", label: "ไทย", name: "langTh" }, { code: "en", label: "EN", name: "langEn" } ]
            delegate: AbstractButton {
                required property var modelData
                objectName: modelData.name
                readonly property bool current: Doc.language === modelData.code
                implicitWidth: 46
                implicitHeight: 26
                hoverEnabled: true
                onClicked: Doc.language = modelData.code
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                contentItem: AppText {
                    text: parent.modelData.label
                    font.pixelSize: Theme.fontSm
                    font.weight: Font.DemiBold
                    color: parent.current ? Theme.textOnAccent : Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 13
                    color: parent.current ? Theme.accent : parent.hovered ? Theme.surfaceHover : "transparent"
                }
            }
        }
    }
}
