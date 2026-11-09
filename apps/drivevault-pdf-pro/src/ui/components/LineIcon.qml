import QtQuick

// Small line icons drawn with the canvas (no icon font is shipped). name: sidebar | page | pages2 | rotate | dark | thumbs | outline | comment
Canvas {
    id: icon

    property string name: ""
    property color color: Theme.text
    property real size: 18

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size
    antialiasing: true
    onColorChanged: requestPaint()
    onNameChanged: requestPaint()
    onSizeChanged: requestPaint()
    Component.onCompleted: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const s = size / 24                       // the icons are drawn on a 24 x 24 grid
        ctx.scale(s, s)
        ctx.strokeStyle = color
        ctx.fillStyle = color
        ctx.lineWidth = 1.7
        ctx.lineJoin = "round"
        ctx.lineCap = "round"
        function roundRect(x, y, w, h, r) {
            ctx.beginPath()
            ctx.moveTo(x + r, y)
            ctx.arcTo(x + w, y, x + w, y + h, r)
            ctx.arcTo(x + w, y + h, x, y + h, r)
            ctx.arcTo(x, y + h, x, y, r)
            ctx.arcTo(x, y, x + w, y, r)
            ctx.closePath()
        }
        switch (name) {
        case "sidebar":
            roundRect(3, 4, 18, 16, 2.5); ctx.stroke()
            ctx.beginPath(); ctx.moveTo(9.5, 4); ctx.lineTo(9.5, 20); ctx.stroke()
            ctx.globalAlpha = 0.45; roundRect(3, 4, 6.5, 16, 2.5); ctx.fill(); ctx.globalAlpha = 1
            break
        case "page":
            roundRect(6.5, 3, 11, 18, 2); ctx.stroke()
            break
        case "pages2":
            roundRect(2.5, 4.5, 8.5, 15, 1.8); ctx.stroke()
            roundRect(13, 4.5, 8.5, 15, 1.8); ctx.stroke()
            break
        case "rotate":
            ctx.beginPath(); ctx.arc(12, 12, 7.5, -Math.PI * 0.35, Math.PI * 1.25, false); ctx.stroke()
            ctx.beginPath(); ctx.moveTo(15.2, 2.6); ctx.lineTo(18.6, 5.8); ctx.lineTo(14.2, 7.2); ctx.closePath(); ctx.fill()
            break
        case "dark":
            ctx.beginPath(); ctx.arc(12, 12, 8, 0, Math.PI * 2); ctx.stroke()
            ctx.beginPath(); ctx.arc(12, 12, 8, -Math.PI / 2, Math.PI / 2, false); ctx.closePath(); ctx.fill()
            break
        case "thumbs":
            roundRect(3.5, 3.5, 7, 7, 1.6); ctx.stroke()
            roundRect(13.5, 3.5, 7, 7, 1.6); ctx.stroke()
            roundRect(3.5, 13.5, 7, 7, 1.6); ctx.stroke()
            roundRect(13.5, 13.5, 7, 7, 1.6); ctx.stroke()
            break
        case "outline":
            for (let i = 0; i < 3; ++i) {
                const y = 6 + i * 6
                ctx.beginPath(); ctx.arc(5, y, 1.3, 0, Math.PI * 2); ctx.fill()
                ctx.beginPath(); ctx.moveTo(9, y); ctx.lineTo(i === 1 ? 17 : 21, y); ctx.stroke()
            }
            break
        case "comment":
            roundRect(3.5, 4, 17, 12, 2.5); ctx.stroke()
            ctx.beginPath(); ctx.moveTo(8, 16); ctx.lineTo(8, 20.5); ctx.lineTo(12.5, 16); ctx.stroke()
            ctx.beginPath(); ctx.moveTo(7.5, 8.5); ctx.lineTo(16.5, 8.5); ctx.moveTo(7.5, 11.8); ctx.lineTo(13.5, 11.8); ctx.stroke()
            break
        }
    }
}
