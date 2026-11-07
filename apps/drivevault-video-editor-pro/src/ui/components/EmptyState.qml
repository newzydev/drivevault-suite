import QtQuick
import QtQuick.Layouts

// Centered empty / error / loading placeholder. Extra children (buttons) are appended below the text.
ColumnLayout {
    id: root

    property string iconName: "folder"
    property string title
    property string description
    property bool isError: false
    property real maxTextWidth: 300
    default property alias actions: actionsColumn.data

    spacing: 10

    Rectangle {
        Layout.alignment: Qt.AlignHCenter
        width: 56
        height: 56
        radius: 28
        color: root.isError ? Qt.alpha(Theme.error, 0.15) : Theme.accentSubtle

        Icon {
            anchors.centerIn: parent
            name: root.iconName
            size: 26
            color: root.isError ? Theme.error : Theme.accentText
        }
    }
    Text {
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: root.maxTextWidth
        text: root.title
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Theme.fontLg
        font.weight: Font.DemiBold
        color: Theme.text
        wrapMode: Text.Wrap
    }
    Text {
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: root.maxTextWidth
        Layout.fillWidth: true
        visible: root.description !== ""
        text: root.description
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Theme.fontMd
        color: Theme.textSecondary
        wrapMode: Text.Wrap
    }
    ColumnLayout {
        id: actionsColumn
        Layout.alignment: Qt.AlignHCenter
        spacing: 8
    }
}
