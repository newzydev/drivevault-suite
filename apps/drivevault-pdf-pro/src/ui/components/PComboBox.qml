import QtQuick
import QtQuick.Controls

// A drop-down list in the style of the program: a rounded field with a chevron, and a dark rounded list whose entries are readable when
// pointed at (a soft accent colour, never a white bar) with a tick on the chosen one. Use it instead of the plain ComboBox, whose list
// is drawn white by the default style. A file may still replace `contentItem` / `background` (the editable stamp box does).
ComboBox {
    id: control

    implicitHeight: 38
    implicitWidth: 160
    hoverEnabled: true
    font.pixelSize: Theme.fontMd

    contentItem: AppText {
        leftPadding: 12
        rightPadding: 34
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.text : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: LineIcon {
        x: control.width - width - 10
        y: (control.height - height) / 2
        name: "chevrondown"
        size: 16
        color: control.enabled ? Theme.textSecondary : Theme.textDisabled
        rotation: control.popup.visible ? 180 : 0
        Behavior on rotation { NumberAnimation { duration: 120 } }
    }

    background: Rectangle {
        radius: 8
        color: Theme.surface
        opacity: control.enabled ? 1 : 0.55
        border.width: control.visualFocus || control.popup.visible ? 2 : 1
        border.color: control.visualFocus || control.popup.visible ? Theme.accent : control.hovered ? Theme.textDisabled : Theme.border
    }

    delegate: ItemDelegate {
        id: item
        required property var modelData
        required property int index
        width: control.width
        height: 36
        padding: 0
        highlighted: control.highlightedIndex === index
        hoverEnabled: true

        contentItem: Item {
            AppText {
                anchors.verticalCenter: parent.verticalCenter
                x: 12
                width: parent.width - 44
                text: control.textRole === "" ? String(item.modelData) : String(item.modelData[control.textRole])
                font: control.font
                color: item.index === control.currentIndex ? Theme.accentText : Theme.text
                elide: Text.ElideRight
            }
            LineIcon {
                visible: item.index === control.currentIndex
                anchors.verticalCenter: parent.verticalCenter
                x: parent.width - width - 12
                name: "check"
                size: 14
                color: Theme.accentText
            }
        }
        background: Rectangle {
            radius: 6
            color: item.highlighted || item.hovered ? Theme.accentSubtle : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 10, 320)
        padding: 5

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
        }

        background: Rectangle {
            radius: 10
            color: Theme.backgroundAlt
            border.width: 1
            border.color: Theme.border
        }

        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 100 } }
    }
}
