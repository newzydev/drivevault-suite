import QtQuick
import QtQuick.Layouts
import DriveVault.Backend

// A tile of the Effects / Transitions / Filters tabs with a picture: the effect (or look, or transition) is applied to a drawn sample picture
// and plays when the mouse is over the tile. Clicking applies the real thing to the selected clip (the parent decides what `activated` does).
Rectangle {
    id: tile

    property string label
    property string kind: "look"                     // "look" | "effect" | "transition"
    // look / effect: the amounts reached at the end of the animation (-1 .. 1; blur, vignette and sharpen 0 .. 1)
    property var amounts: ({})
    // transition: "dissolve" | "dip" | "fade" | "wipe" | "slide" | "zoom" | "none", and "in" | "out"
    property string transition: "none"
    property string direction: "in"
    property color tint: Theme.accent
    signal activated()

    readonly property bool hovered: ma.containsMouse
    property real t: kind === "transition" ? 0.4 : 1       // animation progress; the resting picture shows the result (transition: the middle)

    Layout.fillWidth: true
    implicitWidth: 104
    implicitHeight: 96
    radius: Theme.radius
    color: hovered ? Theme.surfaceHover : Theme.surface
    border.width: 1
    border.color: hovered ? Qt.alpha(tint, 0.7) : Theme.borderSubtle
    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

    SequentialAnimation {
        running: tile.hovered && Theme.animated
        loops: Animation.Infinite
        onRunningChanged: if (!running) tile.t = tile.kind === "transition" ? 0.4 : 1
        NumberAnimation { target: tile; property: "t"; from: 0; to: 1; duration: tile.kind === "transition" ? 1100 : 900; easing.type: Easing.InOutQuad }
        PauseAnimation { duration: 600 }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 5

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 40

            Rectangle {                                               // the picture with its rounded frame
                id: frameBox
                anchors.fill: parent
                radius: 6
                color: "#000000"
                clip: true

                // look / effect: the sample picture with the amounts scaled by the progress
                PreviewScene {
                    anchors.fill: parent
                    visible: tile.kind !== "transition"
                    brightness: (tile.amounts.brightness || 0) * tile.t
                    contrast: (tile.amounts.contrast || 0) * tile.t
                    saturation: (tile.amounts.saturation || 0) * tile.t
                    temperature: (tile.amounts.temperature || 0) * tile.t
                    tint: (tile.amounts.tint || 0) * tile.t
                    blur: (tile.amounts.blur || 0) * tile.t
                    vignette: (tile.amounts.vignette || 0) * tile.t
                    sharpen: (tile.amounts.sharpen || 0) * tile.t
                }

                // transition: picture A, then picture B arrives
                Item {
                    anchors.fill: parent
                    visible: tile.kind === "transition"
                    readonly property real p: tile.t
                    readonly property string type: tile.transition
                    readonly property bool leaving: tile.direction === "out"

                    PreviewScene {                                    // A
                        anchors.fill: parent
                        variant: 0
                        opacity: parent.leaving ? 1 - parent.p
                                 : parent.type === "dip" ? Math.max(0, 1 - 2 * parent.p)
                                 : parent.type === "fade" ? 0 : 1
                    }
                    Item {                                            // B (comes in)
                        id: incoming
                        visible: !parent.leaving
                        x: parent.type === "slide" ? (1 - parent.p) * parent.width : 0
                        width: parent.type === "wipe" ? Math.max(0, parent.p * parent.width) : parent.width
                        height: parent.height
                        clip: true
                        opacity: parent.type === "dissolve" || parent.type === "fade" ? parent.p
                                 : parent.type === "dip" ? Math.max(0, 2 * parent.p - 1)
                                 : parent.type === "zoom" ? Math.min(1, 0.3 + parent.p) : 1
                        PreviewScene {
                            width: frameBox.width
                            height: frameBox.height
                            variant: 1
                            transformOrigin: Item.Center
                            scale: parent.parent.type === "zoom" ? 0.25 + 0.75 * parent.parent.p : 1
                        }
                    }
                    Rectangle {                                       // "none": nothing happens
                        anchors.fill: parent
                        visible: parent.type === "none"
                        color: "transparent"
                    }
                    Rectangle {                                       // out: the picture goes to black
                        anchors.fill: parent
                        z: -1
                        color: "#000000"
                    }
                }
            }
            Rectangle {                                               // "None": a cross over a dimmed picture
                anchors.fill: parent
                radius: 6
                visible: tile.kind === "transition" && tile.transition === "none"
                color: Qt.alpha("#000000", 0.55)
                Icon { anchors.centerIn: parent; name: "close"; size: 20; color: "#FFFFFF" }
            }
            Rectangle {
                anchors.fill: parent
                radius: 6
                visible: tile.kind === "effect" && (tile.amounts.reset === true)
                color: Qt.alpha("#000000", 0.55)
                Icon { anchors.centerIn: parent; name: "refresh"; size: 20; color: "#FFFFFF" }
            }
        }

        Text {
            Layout.fillWidth: true
            text: tile.label
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontXs
            font.weight: Font.Medium
            color: Theme.text
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: tile.activated()
    }
}
