import QtQuick
import QtQuick.Layouts

// Header strip of a dockable panel (title on the left, tool buttons on the right).
Rectangle {
    id: root

    property string title
    property string iconName
    default property alias tools: toolRow.data

    implicitHeight: 38
    color: Theme.backgroundAlt

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.borderSubtle
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 8
        spacing: 8

        Icon {
            visible: root.iconName !== ""
            name: root.iconName
            size: 16
            color: Theme.textSecondary
        }
        Text {
            Layout.fillWidth: true
            text: root.title
            font.pixelSize: Theme.fontSm
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.6
            color: Theme.textSecondary
            elide: Text.ElideRight
        }
        RowLayout {
            id: toolRow
            spacing: 2
        }
    }
}
