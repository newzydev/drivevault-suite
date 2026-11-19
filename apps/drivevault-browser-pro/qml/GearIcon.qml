import QtQuick
import QtQuick.Shapes

// A gear (eight teeth and a hub), drawn as a line icon on a 24 x 24 grid; the colour follows the text colour of the button it sits in.
Item {
    id: root

    property color color: Theme.text
    property real size: 18
    property real strokeWidth: 1.8

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
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M10.43 5.18L10.66 2.49L13.34 2.49L13.57 5.18A7 7 0 0 1 15.71 6.06L17.78 4.33L19.67 6.22L17.94 8.29A7 7 0 0 1 18.82 10.43L21.51 10.66L21.51 13.34L18.82 13.57A7 7 0 0 1 17.94 15.71L19.67 17.78L17.78 19.67L15.71 17.94A7 7 0 0 1 13.57 18.82L13.34 21.51L10.66 21.51L10.43 18.82A7 7 0 0 1 8.29 17.94L6.22 19.67L4.33 17.78L6.06 15.71A7 7 0 0 1 5.18 13.57L2.49 13.34L2.49 10.66L5.18 10.43A7 7 0 0 1 6.06 8.29L4.33 6.22L6.22 4.33L8.29 6.06A7 7 0 0 1 10.43 5.18z" }
        }
        ShapePath {
            strokeColor: root.color
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            PathSvg { path: "M12 12m-3 0a3 3 0 1 0 6 0a3 3 0 1 0-6 0" }
        }
    }
}