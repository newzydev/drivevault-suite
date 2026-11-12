import QtQuick

// Small line icons drawn with the canvas (no icon font is shipped). name: sidebar | page | pages2 | rotate | dark | thumbs | outline | comment
// Also: search print split compress fitwidth fitpage export tools attach bookmark
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
        function strokePath(points) {
            ctx.beginPath()
            ctx.moveTo(points[0][0], points[0][1])
            for (let i = 1; i < points.length; ++i)
                ctx.lineTo(points[i][0], points[i][1])
            ctx.stroke()
        }
        switch (name) {
        case "sun":
            ctx.beginPath(); ctx.arc(12, 12, 4, 0, Math.PI * 2); ctx.stroke()
            for (let i = 0; i < 8; ++i) {
                const angle = i * Math.PI / 4
                strokePath([[12 + 7 * Math.cos(angle), 12 + 7 * Math.sin(angle)],
                            [12 + 9 * Math.cos(angle), 12 + 9 * Math.sin(angle)]])
            }
            break
        case "moon":
            ctx.beginPath()
            ctx.moveTo(19.5, 15)
            ctx.bezierCurveTo(13, 17, 7, 11, 9, 4.5)
            ctx.bezierCurveTo(2, 7, 3, 18, 10, 20)
            ctx.bezierCurveTo(14, 21, 18, 19, 19.5, 15)
            ctx.stroke()
            break
        case "settings":
            ctx.beginPath()
            for (let i = 0; i < 32; ++i) {
                const angle = i * Math.PI / 16
                const radius = i % 4 === 0 || i % 4 === 3 ? 7.5 : 9.5
                const x = 12 + radius * Math.cos(angle), y = 12 + radius * Math.sin(angle)
                if (i === 0) ctx.moveTo(x, y)
                else ctx.lineTo(x, y)
            }
            ctx.closePath(); ctx.stroke()
            ctx.beginPath(); ctx.arc(12, 12, 3, 0, Math.PI * 2); ctx.stroke()
            break
        case "info":
            ctx.beginPath(); ctx.arc(12, 12, 9, 0, Math.PI * 2); ctx.stroke()
            ctx.beginPath(); ctx.arc(12, 8, 0.9, 0, Math.PI * 2); ctx.fill()
            strokePath([[12, 11], [12, 16]])
            break
        case "home":
            strokePath([[3, 11], [12, 4], [21, 11]])
            strokePath([[5, 10], [5, 21], [10, 21], [10, 15], [14, 15], [14, 21], [19, 21], [19, 10]])
            break
        case "forward":
            strokePath([[5, 12], [19, 12]])
            strokePath([[13, 6], [19, 12], [13, 18]])
            break
        case "close":
            strokePath([[6, 6], [18, 18]])
            strokePath([[18, 6], [6, 18]])
            break
        case "minimize":
            strokePath([[6, 12], [18, 12]])
            break
        case "maximize":
            ctx.strokeRect(6, 6, 12, 12)
            break
        case "restore":
            ctx.strokeRect(5, 9, 10, 10)
            strokePath([[9, 9], [9, 5], [19, 5], [19, 15], [15, 15]])
            break
        case "folder":
            strokePath([[3, 19], [3, 6], [9, 6], [11, 8], [20, 8], [20, 11]])
            strokePath([[3, 19], [7, 11], [22, 11], [19, 19], [3, 19]])
            break
        case "scan":
            strokePath([[3, 8], [3, 4], [8, 4]])
            strokePath([[16, 4], [21, 4], [21, 8]])
            strokePath([[21, 16], [21, 20], [16, 20]])
            strokePath([[8, 20], [3, 20], [3, 16]])
            strokePath([[3, 12], [21, 12]])
            break
        case "download":
            roundRect(5, 3, 14, 18, 2); ctx.stroke()
            strokePath([[12, 7], [12, 16]])
            strokePath([[8.5, 12.5], [12, 16], [15.5, 12.5]])
            break
        case "edit":
            strokePath([[12, 4], [4, 4], [4, 20], [20, 20], [20, 13]])
            strokePath([[9, 15], [10, 11], [18, 3], [21, 6], [13, 14], [9, 15]])
            break
        case "form":
            roundRect(3, 6, 18, 12, 2); ctx.stroke()
            strokePath([[9, 3], [15, 3]])
            strokePath([[12, 3], [12, 21]])
            strokePath([[9, 21], [15, 21]])
            break
        case "shield":
            strokePath([[12, 3], [20, 6], [20, 12], [18, 17], [12, 21],
                        [6, 17], [4, 12], [4, 6], [12, 3]])
            strokePath([[8.5, 12], [11, 14.5], [15.5, 9.5]])
            break
        case "redact":
            roundRect(3, 7, 18, 10, 2); ctx.stroke()
            strokePath([[6, 10], [18, 10]])
            strokePath([[6, 14], [18, 14]])
            break
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
        case "search":
            ctx.beginPath(); ctx.arc(10.5, 10.5, 6, 0, Math.PI * 2); ctx.stroke()
            strokePath([[15, 15], [20.5, 20.5]])
            break
        case "print":
            strokePath([[7, 9], [7, 3.5], [17, 3.5], [17, 9]])
            roundRect(3.5, 9, 17, 8, 2); ctx.stroke()
            strokePath([[7, 14], [7, 20.5], [17, 20.5], [17, 14]])
            break
        case "split":
            roundRect(3.5, 4, 7, 16, 1.5); ctx.stroke()
            roundRect(13.5, 4, 7, 16, 1.5); ctx.stroke()
            strokePath([[10.5, 12], [13.5, 12]])
            break
        case "compress":
            strokePath([[4, 4], [10, 10]]); strokePath([[10, 5.5], [10, 10], [5.5, 10]])
            strokePath([[20, 4], [14, 10]]); strokePath([[14, 5.5], [14, 10], [18.5, 10]])
            strokePath([[4, 20], [10, 14]]); strokePath([[10, 18.5], [10, 14], [5.5, 14]])
            strokePath([[20, 20], [14, 14]]); strokePath([[14, 18.5], [14, 14], [18.5, 14]])
            break
        case "fitwidth":
            strokePath([[4, 5], [4, 19]]); strokePath([[20, 5], [20, 19]])
            strokePath([[7, 12], [17, 12]]); strokePath([[10, 9], [7, 12], [10, 15]]); strokePath([[14, 9], [17, 12], [14, 15]])
            break
        case "fitpage":
            strokePath([[4, 9], [4, 4], [9, 4]]); strokePath([[15, 4], [20, 4], [20, 9]])
            strokePath([[20, 15], [20, 20], [15, 20]]); strokePath([[9, 20], [4, 20], [4, 15]])
            roundRect(8.5, 7.5, 7, 9, 1); ctx.stroke()
            break
        case "export":
            strokePath([[12, 4], [12, 15]]); strokePath([[7.5, 10.5], [12, 15], [16.5, 10.5]])
            strokePath([[4, 15], [4, 20], [20, 20], [20, 15]])
            break
        case "tools":
            strokePath([[4.5, 19.5], [13, 11]])
            ctx.beginPath(); ctx.arc(16, 8, 4, 0, Math.PI * 2); ctx.stroke()
            strokePath([[4, 20], [5.5, 18.5]])
            break
        case "attach":
            strokePath([[16, 7], [8.5, 14.5], [8.5, 17], [11, 17], [18.5, 9.5], [18.5, 6], [15, 5.5], [6, 14.5], [6, 18.5], [10, 19]])
            break
        case "bookmark":
            strokePath([[6.5, 4], [17.5, 4], [17.5, 20.5], [12, 16], [6.5, 20.5], [6.5, 4]])
            break
        }
    }
}
