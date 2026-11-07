import QtQuick
import QtQuick.Shapes

// DriveVault brand mark: rounded tile, vault dial ring and play triangle (original artwork).
Item {
    id: root

    property real size: 64
    width: size
    height: size

    Rectangle {
        anchors.fill: parent
        radius: root.size * 0.24
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: Qt.lighter(Theme.accent, 1.28) }
            GradientStop { position: 1.0; color: Qt.darker(Theme.accent, 1.45) }
        }
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        antialiasing: true

        ShapePath {
            strokeColor: Qt.rgba(1, 1, 1, 0.6)
            strokeWidth: root.size * 0.035
            fillColor: "transparent"
            PathAngleArc {
                centerX: root.size / 2
                centerY: root.size / 2
                radiusX: root.size * 0.30
                radiusY: root.size * 0.30
                startAngle: 0
                sweepAngle: 360
            }
        }
        ShapePath {
            strokeColor: "transparent"
            fillColor: "#FFFFFF"
            startX: root.size * 0.5 - root.size * 0.12
            startY: root.size * 0.5 - root.size * 0.18
            PathLine { x: root.size * 0.5 - root.size * 0.12; y: root.size * 0.5 + root.size * 0.18 }
            PathLine { x: root.size * 0.5 + root.size * 0.20; y: root.size * 0.5 }
            PathLine { x: root.size * 0.5 - root.size * 0.12; y: root.size * 0.5 - root.size * 0.18 }
        }
    }
}
