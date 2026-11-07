import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

// Text, style and position of a title / lower third / caption clip. Subtitle clips use the same editor.
ColumnLayout {
    id: root

    required property string clipId
    property int revision: 0
    // Empty for clips that are not titles: the controls then show neutral defaults.
    readonly property var info: {
        revision
        const i = clipId !== "" ? Projects.timeline.titleInfo(clipId) : ({})
        return i.text !== undefined ? i : { text: "", font: "Noto Sans Thai", size: 72, color: "#FFFFFF", strokeColor: "#000000",
                                            strokeWidth: 0, shadow: false, bold: true, align: "center", x: 0.5, y: 0.5 }
    }
    readonly property var swatches: ["#FFFFFF", "#000000", "#FACC15", "#F97316", "#EF4444", "#22C55E", "#3B82F6", "#A855F7"]

    Layout.fillWidth: true
    spacing: 8

    function change(fields) {
        const err = Projects.timeline.setTitle(clipId, fields)
        if (err !== "")
            App.toast(err, "warning")
    }

    component FieldLabel: Text {
        Layout.preferredWidth: 78
        font.pixelSize: Theme.fontSm
        color: Theme.textSecondary
        elide: Text.ElideRight
    }
    component Swatches: RowLayout {
        id: sw
        property string current
        signal picked(string colour)
        spacing: 4
        Repeater {
            model: root.swatches
            delegate: Rectangle {
                required property string modelData
                width: 20; height: 20; radius: 10
                color: modelData
                border.width: sw.current.toUpperCase() === modelData ? 2 : 1
                border.color: sw.current.toUpperCase() === modelData ? Theme.accent : Theme.border
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: sw.picked(parent.modelData) }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 84
        radius: Theme.radiusSmall + 2
        color: Theme.backgroundAlt
        border.width: textArea.activeFocus ? 2 : 1
        border.color: textArea.activeFocus ? Theme.accent : Theme.border
        Flickable {
            anchors.fill: parent
            anchors.margins: 2
            clip: true
            contentHeight: textArea.implicitHeight
            ScrollBar.vertical: AppScrollBar {}
            TextArea.flickable: TextArea {
                id: textArea
                objectName: "titleText"
                text: root.info.text !== undefined ? root.info.text : ""
                placeholderText: qsTr("Type the text here")
                font.pixelSize: Theme.fontMd
                color: Theme.text
                wrapMode: TextArea.Wrap
                selectByMouse: true
                selectionColor: Theme.accent
                selectedTextColor: Theme.textOnAccent
                background: null
                onTextChanged: if (activeFocus && text !== root.info.text) root.change({ text: text })
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        FieldLabel { text: qsTr("Font") }
        AppComboBox {
            Layout.fillWidth: true
            implicitWidth: 100
            current: root.info.font
            options: [ { value: "Noto Sans Thai", label: "Noto Sans Thai" }, { value: "Leelawadee UI", label: "Leelawadee UI" },
                       { value: "Segoe UI", label: "Segoe UI" }, { value: "Arial", label: "Arial" },
                       { value: "Georgia", label: "Georgia" }, { value: "Consolas", label: "Consolas" } ]
            onPicked: value => root.change({ font: value })
        }
    }
    RowLayout {
        Layout.fillWidth: true
        FieldLabel { text: qsTr("Size") }
        AppSlider {
            Layout.fillWidth: true
            implicitWidth: 60
            from: 12; to: 300; stepSize: 1
            value: root.info.size !== undefined ? root.info.size : 72
            onMoved: root.change({ size: Math.round(value) })
        }
        Text { Layout.preferredWidth: 36; horizontalAlignment: Text.AlignRight; text: root.info.size; font.pixelSize: Theme.fontSm; color: Theme.textSecondary }
    }
    RowLayout {
        Layout.fillWidth: true
        FieldLabel { text: qsTr("Style") }
        AppButton {
            objectName: "titleBold"
            text: "B"
            font.weight: Font.Bold
            implicitWidth: 34
            variant: root.info.bold ? "primary" : "secondary"
            onClicked: root.change({ bold: !root.info.bold })
        }
        Repeater {
            model: [ { v: "left", n: qsTr("Left") }, { v: "center", n: qsTr("Centre") }, { v: "right", n: qsTr("Right") } ]
            delegate: AppButton {
                required property var modelData
                text: modelData.n
                variant: root.info.align === modelData.v ? "primary" : "secondary"
                onClicked: root.change({ align: modelData.v })
            }
        }
        Item { Layout.fillWidth: true }
    }
    RowLayout {
        Layout.fillWidth: true
        FieldLabel { text: qsTr("Colour") }
        Swatches { current: root.info.color !== undefined ? root.info.color : ""; onPicked: colour => root.change({ color: colour }) }
    }
    RowLayout {
        Layout.fillWidth: true
        FieldLabel { text: qsTr("Outline") }
        AppSlider {
            Layout.fillWidth: true
            implicitWidth: 60
            from: 0; to: 20; stepSize: 0.5
            value: root.info.strokeWidth !== undefined ? root.info.strokeWidth : 0
            onMoved: root.change({ strokeWidth: value })
        }
        Swatches { current: root.info.strokeColor !== undefined ? root.info.strokeColor : ""; onPicked: colour => root.change({ strokeColor: colour }) }
    }
    RowLayout {
        Layout.fillWidth: true
        FieldLabel { text: qsTr("Shadow") }
        AppSwitch { checked: root.info.shadow === true; onToggled: root.change({ shadow: checked }) }
        Item { Layout.fillWidth: true }
    }
    RowLayout {
        Layout.fillWidth: true
        FieldLabel { text: qsTr("Position X") }
        AppSlider {
            Layout.fillWidth: true
            implicitWidth: 60
            from: 0; to: 1
            value: root.info.x !== undefined ? root.info.x : 0.5
            onMoved: root.change({ x: value })
        }
        Text { Layout.preferredWidth: 36; horizontalAlignment: Text.AlignRight; text: Number(root.info.x).toFixed(2); font.pixelSize: Theme.fontSm; color: Theme.textSecondary }
    }
    RowLayout {
        Layout.fillWidth: true
        FieldLabel { text: qsTr("Position Y") }
        AppSlider {
            Layout.fillWidth: true
            implicitWidth: 60
            from: 0; to: 1
            value: root.info.y !== undefined ? root.info.y : 0.5
            onMoved: root.change({ y: value })
        }
        Text { Layout.preferredWidth: 36; horizontalAlignment: Text.AlignRight; text: Number(root.info.y).toFixed(2); font.pixelSize: Theme.fontSm; color: Theme.textSecondary }
    }
}
