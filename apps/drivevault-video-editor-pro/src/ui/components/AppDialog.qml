import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Modal dialog with title bar, scrollable-free body and a button row.
// Children go into the body; use `footerContent` for the buttons.
Popup {
    id: control

    property string title
    property string subtitle
    property bool showClose: true
    property int bodyPadding: 24
    default property alias content: body.data
    property alias footerContent: footerRow.data
    property bool hasFooter: footerRow.children.length > 0

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    focus: true

    implicitWidth: 520
    width: Math.min(implicitWidth, (parent ? parent.width : implicitWidth) - 48)
    height: Math.min(implicitHeight, (parent ? parent.height : implicitHeight) - 48)

    Overlay.modal: Rectangle { color: Theme.overlay }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal }
            NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationFast }
    }

    background: Rectangle {
        radius: Theme.radiusLarge
        color: Theme.surface
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            id: header
            Layout.fillWidth: true
            Layout.leftMargin: control.bodyPadding
            Layout.rightMargin: 12
            Layout.topMargin: 18
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: control.title
                    font.pixelSize: Theme.fontXl
                    font.weight: Font.DemiBold
                    color: Theme.text
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: control.subtitle !== ""
                    text: control.subtitle
                    font.pixelSize: Theme.fontSm
                    color: Theme.textSecondary
                    wrapMode: Text.Wrap
                }
            }
            IconButton {
                visible: control.showClose
                iconName: "close"
                tip: qsTr("Close")
                onClicked: control.close()
            }
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: control.bodyPadding
            spacing: 14
        }

        Rectangle {
            visible: control.hasFooter
            Layout.fillWidth: true
            height: 1
            color: Theme.borderSubtle
        }
        RowLayout {
            id: footerRow
            visible: control.hasFooter
            Layout.fillWidth: true
            Layout.margins: 18
            Layout.leftMargin: control.bodyPadding
            Layout.rightMargin: control.bodyPadding
            spacing: 10
        }
    }
}
