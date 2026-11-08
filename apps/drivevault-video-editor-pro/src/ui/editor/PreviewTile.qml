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
    // transition: a type of the Compositor (dissolve, dip, fade, wipe, wipeLeft, slide, zoom, spin, flash ...) or "none", and "in" | "out"
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
                    zoom: 1 + ((tile.amounts.zoom || 1) - 1) * tile.t
                    tilt: (tile.amounts.tilt || 0) * tile.t
                    barsV: (tile.amounts.barsV || 0) * tile.t
                    barsH: (tile.amounts.barsH || 0) * tile.t
                }

                // transition: the clip that moves is picture B (in) or picture A (out, over black); `s` is how far it is shown, as in the Compositor
                Item {
                    id: trans
                    anchors.fill: parent
                    visible: tile.kind === "transition"
                    readonly property string type: tile.transition
                    readonly property bool leaving: tile.direction === "out"
                    readonly property real s: leaving ? 1 - tile.t : tile.t     // 0 hidden .. 1 fully shown

                    Rectangle { anchors.fill: parent; z: -2; color: "#000000" }
                    PreviewScene {                                    // A stays underneath while B comes in
                        anchors.fill: parent
                        z: -1
                        variant: 0
                        visible: !trans.leaving
                        opacity: trans.type === "dip" ? Math.max(0, 1 - 2 * trans.s) : trans.type === "fade" ? 0 : 1
                    }
                    Item {                                            // window onto the moving picture (wipes shrink it)
                        id: mover
                        readonly property string ty: trans.type
                        readonly property real s: trans.s
                        x: (ty === "wipeLeft" ? 1 - s : 0) * parent.width
                        y: (ty === "wipeUp" ? 1 - s : 0) * parent.height
                        width: (ty === "wipe" ? Math.max(0.001, s) : ty === "wipeLeft" ? s : 1) * parent.width
                        height: (ty === "wipeDown" ? Math.max(0.001, s) : ty === "wipeUp" ? s : 1) * parent.height
                        clip: true
                        visible: ty !== "none"
                        opacity: ty === "dissolve" || ty === "fade" || ty === "zoomOut" || ty === "spin" || ty === "flash" || ty === "zoom" ? Math.min(1, s)
                                 : ty === "dip" ? Math.max(0, 2 * s - 1) : 1
                        PreviewScene {
                            x: -mover.x + (mover.ty === "slide" ? 1 - mover.s : mover.ty === "slideLeft" ? mover.s - 1 : 0) * trans.width
                            y: -mover.y + (mover.ty === "slideUp" ? 1 - mover.s : mover.ty === "slideDown" ? mover.s - 1 : 0) * trans.height
                            width: trans.width
                            height: trans.height
                            variant: trans.leaving ? 0 : 1
                            transformOrigin: Item.Center
                            scale: mover.ty === "zoom" || mover.ty === "spin" ? 0.3 + 0.7 * mover.s : mover.ty === "zoomOut" ? 1 + 0.8 * (1 - mover.s) : 1
                            rotation: mover.ty === "spin" ? -180 * (1 - mover.s) : 0
                        }
                    }
                    Rectangle {                                       // flash: white over everything in the middle
                        anchors.fill: parent
                        visible: trans.type === "flash"
                        color: "#FFFFFF"
                        opacity: 1 - Math.abs(2 * trans.s - 1)
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
