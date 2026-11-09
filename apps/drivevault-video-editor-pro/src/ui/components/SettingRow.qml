import QtQuick
import QtQuick.Layouts

// One labelled setting: title + description on the left, the control on the right.
Item {
    id: root

    property string title
    property string description
    property bool dimmed: false
    default property alias control: controlHolder.data

    Layout.fillWidth: true
    implicitHeight: Math.max(textColumn.implicitHeight, controlHolder.implicitHeight) + 28

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.borderSubtle
    }

    RowLayout {
        anchors.fill: parent
        anchors.topMargin: 14
        anchors.bottomMargin: 14
        spacing: 24
        opacity: root.dimmed ? 0.5 : 1

        ColumnLayout {
            id: textColumn
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 3
            AppText {
                Layout.fillWidth: true
                text: root.title
                font.pixelSize: Theme.fontMd
                font.weight: Font.Medium
                color: Theme.text
                wrapMode: Text.Wrap
            }
            AppText {
                Layout.fillWidth: true
                visible: root.description !== ""
                text: root.description
                font.pixelSize: Theme.fontSm
                color: Theme.textSecondary
                wrapMode: Text.Wrap
            }
        }
        RowLayout {
            id: controlHolder
            Layout.alignment: Qt.AlignVCenter
            spacing: 8
        }
    }
}
