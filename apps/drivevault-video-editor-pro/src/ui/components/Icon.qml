import QtQuick
import QtQuick.Shapes
import "IconData.js" as IconData

// Vector icon drawn from IconData.js. Colour and size are fully themable.
Item {
    id: root

    property string name
    property color color: Theme.text
    property real size: 18
    property real strokeWidth: 1.8
    property bool filled: false

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    Shape {
        anchors.centerIn: parent
        width: 24
        height: 24
        scale: root.size / 24
        preferredRendererType: Shape.CurveRenderer
        antialiasing: true

        ShapePath {
            strokeColor: root.color
            strokeWidth: root.strokeWidth
            fillColor: root.filled ? root.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathSvg { path: IconData.pathFor(root.name) }
        }
    }
}
