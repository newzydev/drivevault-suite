import QtQuick

// A small line icon drawn here (no icon font): globe | expand | shrink | close | back | forward | reload | plus | lock | info | sidebar | sites | stop
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
        if (kind === "globe") {
            c.arc(8, 8, 6, 0, Math.PI * 2); c.moveTo(2, 8); c.lineTo(14, 8); c.ellipse(5.2, 2, 5.6, 12)
        } else if (kind === "expand") {                         // two arrows pointing outwards (corners)
            c.moveTo(9.5, 2.5); c.lineTo(13.5, 2.5); c.lineTo(13.5, 6.5); c.moveTo(13.5, 2.5); c.lineTo(8.8, 7.2)
            c.moveTo(6.5, 13.5); c.lineTo(2.5, 13.5); c.lineTo(2.5, 9.5); c.moveTo(2.5, 13.5); c.lineTo(7.2, 8.8)
        } else if (kind === "shrink") {                         // two arrows pointing inwards
            c.moveTo(13, 6); c.lineTo(10, 6); c.lineTo(10, 3); c.moveTo(10, 6); c.lineTo(14, 2)
            c.moveTo(3, 10); c.lineTo(6, 10); c.lineTo(6, 13); c.moveTo(6, 10); c.lineTo(2, 14)
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
        } else if (kind === "plus") {
            c.moveTo(8, 3); c.lineTo(8, 13); c.moveTo(3, 8); c.lineTo(13, 8)
        } else if (kind === "lock") {
            c.rect(3.5, 7.2, 9, 6.3); c.moveTo(5.4, 7.2); c.lineTo(5.4, 5.2); c.arc(8, 5.2, 2.6, Math.PI, 0)
            c.lineTo(10.6, 7.2)
        } else if (kind === "info") {
            c.arc(8, 8, 6, 0, Math.PI * 2); c.moveTo(8, 7.2); c.lineTo(8, 11.2); c.moveTo(8, 4.9); c.lineTo(8, 5.1)
        } else if (kind === "sidebar") {                        // a window with a bar at the left
            c.rect(2, 3, 12, 10); c.moveTo(6, 3); c.lineTo(6, 13)
        } else if (kind === "sites") {                          // a shield
            c.moveTo(8, 2); c.lineTo(13, 4); c.lineTo(13, 8); c.quadraticCurveTo(13, 12, 8, 14); c.quadraticCurveTo(3, 12, 3, 8); c.lineTo(3, 4); c.closePath()
        }
        c.stroke()
    }
}
