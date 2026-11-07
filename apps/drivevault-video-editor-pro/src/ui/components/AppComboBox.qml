import QtQuick
import QtQuick.Controls

// options: [{ value: "x", label: qsTr("X") }, ...]; `current` is the selected value.
ComboBox {
    id: control

    property var options: []
    property var current
    signal picked(var value)

    model: options
    textRole: "label"
    valueRole: "value"
    implicitHeight: Theme.controlHeight
    implicitWidth: 200
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.pixelSize: Theme.fontMd

    function syncIndex() {
        const i = indexOfValue(current)
        if (i !== currentIndex)
            currentIndex = i
    }
    onOptionsChanged: Qt.callLater(syncIndex)
    onCurrentChanged: Qt.callLater(syncIndex)
    Component.onCompleted: Qt.callLater(syncIndex)
    onActivated: picked(currentValue)

    contentItem: Text {
        leftPadding: 12
        rightPadding: 32
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.text : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Icon {
        x: control.width - width - 10
        y: (control.height - height) / 2
        name: "chevronDown"
        size: 16
        color: Theme.textSecondary
    }

    background: Rectangle {
        radius: Theme.radiusSmall + 2
        color: control.enabled ? Theme.backgroundAlt : "transparent"
        border.width: control.visualFocus || control.popup.visible ? 2 : 1
        border.color: control.visualFocus || control.popup.visible ? Theme.accent
                      : control.hovered ? Theme.textDisabled : Theme.border
    }

    delegate: ItemDelegate {
        id: item
        required property var modelData
        required property int index
        width: control.width
        height: 32
        padding: 0
        highlighted: control.highlightedIndex === index

        contentItem: Item {
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 12
                width: parent.width - 44
                text: item.modelData.label
                font: control.font
                color: item.index === control.currentIndex ? Theme.accentText : Theme.text
                elide: Text.ElideRight
            }
            Icon {
                visible: item.index === control.currentIndex
                anchors.verticalCenter: parent.verticalCenter
                x: parent.width - width - 10
                name: "check"
                size: 14
                color: Theme.accentText
            }
        }
        background: Rectangle {
            radius: Theme.radiusSmall
            color: item.highlighted ? Theme.accentSubtle : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
        padding: 4

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollBar.vertical: AppScrollBar {}
        }

        background: Rectangle {
            radius: Theme.radius
            color: Theme.surfaceElevated
            border.color: Theme.border
        }

        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast } }
    }
}
