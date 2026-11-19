import QtQuick

// A small line icon drawn here (no icon font).
// kinds: globe close back forward reload stop plus lock info star starfill download menu home search history bookmark check folder shield trash find mask warning
Canvas {
    id: ic
    property string kind: "globe"
    property color color: Theme.textSecondary
    property real size: 16
    implicitWidth: size
    implicitHeight: size
    width: size
    height: size
    onColorChanged: requestPaint()
    onKindChanged: requestPaint()
    onSizeChanged: requestPaint()
    Component.onCompleted: requestPaint()
    onPaint: {
        const c = getContext("2d")
        c.reset()
        c.scale(size / 16, size / 16)
        c.strokeStyle = ic.color
        c.fillStyle = ic.color
        c.lineWidth = 1.6
        c.lineCap = "round"
        c.lineJoin = "round"
        c.beginPath()
        let fill = false
        if (kind === "globe") {
            c.arc(8, 8, 6, 0, Math.PI * 2); c.moveTo(2, 8); c.lineTo(14, 8); c.ellipse(5.2, 2, 5.6, 12)
        } else if (kind === "close") {
            c.moveTo(3.5, 3.5); c.lineTo(12.5, 12.5); c.moveTo(12.5, 3.5); c.lineTo(3.5, 12.5)
        } else if (kind === "back") {
            c.moveTo(9.8, 3.2); c.lineTo(5, 8); c.lineTo(9.8, 12.8)
        } else if (kind === "forward") {
            c.moveTo(6.2, 3.2); c.lineTo(11, 8); c.lineTo(6.2, 12.8)
        } else if (kind === "reload") {
            c.arc(8, 8, 5, Math.PI * 0.15, Math.PI * 1.75); c.moveTo(13.6, 2.6); c.lineTo(13.2, 6.4); c.lineTo(9.4, 5.9)
        } else if (kind === "stop") {
            c.moveTo(4, 4); c.lineTo(12, 12); c.moveTo(12, 4); c.lineTo(4, 12)
        } else if (kind === "shortcutLess" || kind === "shortcutMore") {
            const up = kind === "shortcutLess"
            c.moveTo(3, up ? 10 : 6); c.lineTo(8, up ? 5 : 11); c.lineTo(13, up ? 10 : 6)
        } else if (kind === "plus") {
            c.moveTo(8, 3); c.lineTo(8, 13); c.moveTo(3, 8); c.lineTo(13, 8)
        } else if (kind === "lock") {
            c.rect(3.5, 7.2, 9, 6.3); c.moveTo(5.4, 7.2); c.lineTo(5.4, 5.2); c.arc(8, 5.2, 2.6, Math.PI, 0); c.lineTo(10.6, 7.2)
        } else if (kind === "info") {
            c.arc(8, 8, 6, 0, Math.PI * 2); c.moveTo(8, 7.2); c.lineTo(8, 11.2); c.moveTo(8, 4.9); c.lineTo(8, 5.1)
        } else if (kind === "star" || kind === "starfill") {
            const cx = 8, cy = 8.4, R = 6.2, r = 2.7
            for (let i = 0; i < 10; ++i) {
                const a = -Math.PI / 2 + i * Math.PI / 5, rad = i % 2 === 0 ? R : r
                if (i === 0) c.moveTo(cx + rad * Math.cos(a), cy + rad * Math.sin(a)); else c.lineTo(cx + rad * Math.cos(a), cy + rad * Math.sin(a))
            }
            c.closePath()
            fill = kind === "starfill"
        } else if (kind === "download") {
            c.moveTo(8, 2.5); c.lineTo(8, 10); c.moveTo(4.8, 7); c.lineTo(8, 10.2); c.lineTo(11.2, 7); c.moveTo(3, 13); c.lineTo(13, 13)
        } else if (kind === "menu") {
            c.arc(8, 3.4, 0.9, 0, Math.PI * 2); c.moveTo(8.9, 8); c.arc(8, 8, 0.9, 0, Math.PI * 2); c.moveTo(8.9, 12.6); c.arc(8, 12.6, 0.9, 0, Math.PI * 2)
            fill = true
        } else if (kind === "home") {
            c.moveTo(2.5, 8); c.lineTo(8, 3); c.lineTo(13.5, 8); c.moveTo(4, 7); c.lineTo(4, 13); c.lineTo(12, 13); c.lineTo(12, 7)
        } else if (kind === "search") {
            c.arc(7, 7, 4.2, 0, Math.PI * 2); c.moveTo(10.2, 10.2); c.lineTo(13.5, 13.5)
        } else if (kind === "history") {
            c.arc(8, 8, 5.6, 0, Math.PI * 2); c.moveTo(8, 4.8); c.lineTo(8, 8); c.lineTo(10.4, 9.4)
        } else if (kind === "bookmark") {
            c.moveTo(4, 2.5); c.lineTo(12, 2.5); c.lineTo(12, 13.5); c.lineTo(8, 10.5); c.lineTo(4, 13.5); c.closePath()
        } else if (kind === "check") {
            c.moveTo(3, 8.5); c.lineTo(6.5, 12); c.lineTo(13, 4.5)
        } else if (kind === "folder") {
            c.moveTo(2, 4.5); c.lineTo(6.5, 4.5); c.lineTo(8, 6); c.lineTo(14, 6); c.lineTo(14, 12.5); c.lineTo(2, 12.5); c.closePath()
        } else if (kind === "shield") {
            c.moveTo(8, 2); c.lineTo(13, 4); c.lineTo(13, 8); c.quadraticCurveTo(13, 12, 8, 14); c.quadraticCurveTo(3, 12, 3, 8); c.lineTo(3, 4); c.closePath()
        } else if (kind === "trash") {
            c.moveTo(3, 4.5); c.lineTo(13, 4.5); c.moveTo(6, 4.5); c.lineTo(6, 2.8); c.lineTo(10, 2.8); c.lineTo(10, 4.5)
            c.moveTo(4.2, 4.5); c.lineTo(5, 13.5); c.lineTo(11, 13.5); c.lineTo(11.8, 4.5); c.moveTo(7, 7); c.lineTo(7, 11); c.moveTo(9, 7); c.lineTo(9, 11)
        } else if (kind === "find") {
            c.arc(6.8, 6.8, 3.8, 0, Math.PI * 2); c.moveTo(9.6, 9.6); c.lineTo(13, 13); c.moveTo(5.2, 6.8); c.lineTo(8.4, 6.8)
        } else if (kind === "mask") {                           // the private window: a mask
            c.moveTo(2, 6); c.quadraticCurveTo(8, 3, 14, 6); c.lineTo(13, 9.5); c.quadraticCurveTo(10.5, 11, 8.6, 9.2); c.lineTo(7.4, 9.2); c.quadraticCurveTo(5.5, 11, 3, 9.5); c.closePath()
        } else if (kind === "pip") {                            // a window with a small window inside it (picture in picture)
            c.rect(2, 3, 12, 10); c.rect(7.6, 7.6, 5, 3.6)
            fill = false
        } else if (kind === "video") {                          // a film frame with a play triangle (the videos of this page)
            c.rect(2, 3, 12, 10); c.moveTo(6.8, 5.6); c.lineTo(10.6, 8); c.lineTo(6.8, 10.4); c.closePath()
        } else if (kind === "warning") {
            c.moveTo(8, 2.5); c.lineTo(14, 13); c.lineTo(2, 13); c.closePath(); c.moveTo(8, 6.5); c.lineTo(8, 9.5); c.moveTo(8, 11.3); c.lineTo(8, 11.5)
        }
        if (fill) c.fill()
        c.stroke()
    }
}
