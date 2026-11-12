import QtQuick
import QtQuick.Controls

// A check box in the style of the program: a 20 px rounded square that fills with the accent colour and shows a drawn tick.
CheckBox {
    id: control

    property bool wrapLabel: false
    property int labelWeight: Font.Normal

    implicitHeight: Math.max(32, contentItem.implicitHeight + 8)
    implicitWidth: indicator.width + spacing + contentItem.implicitWidth
    spacing: 10
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }

    indicator: Rectangle {
        implicitWidth: 20
        implicitHeight: 20
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: 6
        color: control.checked ? Theme.accent : control.hovered && control.enabled ? Theme.surfaceHover : Theme.surface
        border.width: control.visualFocus ? 3 : 2
        border.color: !control.enabled ? Theme.border : control.checked || control.visualFocus ? Theme.accent : Theme.textDisabled
        opacity: control.enabled ? 1 : 0.55
        Canvas {
            anchors.centerIn: parent
            width: 14
            height: 14
            visible: control.checkState !== Qt.Unchecked
            onVisibleChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = "#FFFFFF"
                ctx.lineWidth = 2.2
                ctx.lineCap = "round"
                ctx.lineJoin = "round"
                ctx.beginPath()
                if (control.checkState === Qt.PartiallyChecked) {
                    ctx.moveTo(3, 7); ctx.lineTo(11, 7)
                } else {
                    ctx.moveTo(2.5, 7.5); ctx.lineTo(5.8, 10.8); ctx.lineTo(11.5, 3.5)
                }
                ctx.stroke()
            }
        }
    }
    contentItem: AppText {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        textFormat: Text.PlainText
        wrapMode: control.wrapLabel ? Text.Wrap : Text.NoWrap
        elide: control.wrapLabel ? Text.ElideNone : Text.ElideRight
        font.weight: control.labelWeight
        color: control.enabled ? Theme.text : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
    }
}
