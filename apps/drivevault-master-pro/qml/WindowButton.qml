import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

// A button of our own title bar: kind = "minimize" | "maximize" | "restore" | "close". The symbol is drawn (no icon font).
AbstractButton {
    id: control

    property string kind: "minimize"
    readonly property bool isClose: kind === "close"
    readonly property color symbol: isClose && hovered ? "#FFFFFF" : Theme.text

    implicitWidth: 46
    implicitHeight: 32
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.name: text

    HoverHandler { cursorShape: Qt.ArrowCursor }

    background: Rectangle {
        color: control.isClose ? (control.down ? Qt.darker(Theme.error, 1.2) : control.hovered ? Theme.error : "transparent")
                               : (control.down ? Theme.border : control.hovered ? Theme.surfaceHover : "transparent")
    }
    contentItem: Item {
        Shape {
            anchors.centerIn: parent
            width: 12; height: 12
            preferredRendererType: Shape.CurveRenderer
            ShapePath {                                              // minimize: a line
                strokeColor: control.kind === "minimize" ? control.symbol : "transparent"
                strokeWidth: 1.4; fillColor: "transparent"
                startX: 1; startY: 6
                PathLine { x: 11; y: 6 }
            }
            ShapePath {                                              // maximize: a square
                strokeColor: control.kind === "maximize" ? control.symbol : "transparent"
                strokeWidth: 1.4; fillColor: "transparent"; joinStyle: ShapePath.MiterJoin
                startX: 1.5; startY: 1.5
                PathLine { x: 10.5; y: 1.5 }
                PathLine { x: 10.5; y: 10.5 }
                PathLine { x: 1.5; y: 10.5 }
                PathLine { x: 1.5; y: 1.5 }
            }
            ShapePath {                                              // restore: two squares, one behind the other
                strokeColor: control.kind === "restore" ? control.symbol : "transparent"
                strokeWidth: 1.4; fillColor: "transparent"; joinStyle: ShapePath.MiterJoin
                startX: 1.5; startY: 3.5
                PathLine { x: 8.5; y: 3.5 }
                PathLine { x: 8.5; y: 10.5 }
                PathLine { x: 1.5; y: 10.5 }
                PathLine { x: 1.5; y: 3.5 }
            }
            ShapePath {
                strokeColor: control.kind === "restore" ? control.symbol : "transparent"
                strokeWidth: 1.4; fillColor: "transparent"; joinStyle: ShapePath.MiterJoin
                startX: 3.5; startY: 3.5
                PathLine { x: 3.5; y: 1.5 }
                PathLine { x: 10.5; y: 1.5 }
                PathLine { x: 10.5; y: 8.5 }
                PathLine { x: 8.5; y: 8.5 }
            }
            ShapePath {                                              // close: a cross
                strokeColor: control.kind === "close" ? control.symbol : "transparent"
                strokeWidth: 1.4; fillColor: "transparent"
                startX: 1; startY: 1
                PathLine { x: 11; y: 11 }
                PathMove { x: 11; y: 1 }
                PathLine { x: 1; y: 11 }
            }
        }
    }
}
