import QtQuick
import QtQuick.Effects

// A small drawn landscape that the previews of effects and filters work on (no media file is needed). The colour controls change every colour
// the picture is made of (so they also work without a GPU); blur uses the graphics card when there is one.
Item {
    id: scene

    property int variant: 0                      // 0 = day, 1 = dusk (the second picture of a transition preview)
    // the amounts of a look / effect (same ranges as the clip parameters, -1 .. 1)
    property real brightness: 0
    property real contrast: 0
    property real saturation: 0
    property real temperature: 0
    property real tint: 0
    property real blur: 0                        // 0 .. 1
    property real vignette: 0                    // 0 .. 1
    property real sharpen: 0                     // 0 .. 1
    property real zoom: 1                        // picture scale (zoom effects)
    property real tilt: 0                        // picture rotation in degrees
    property real barsV: 0                       // black bars at the top and bottom, fraction of the height each
    property real barsH: 0                       // black bars left and right, fraction of the width each

    readonly property bool gpu: GraphicsInfo.api !== GraphicsInfo.Software
    readonly property var palettes: [
        { skyTop: "#4F9CE8", skyMid: "#9FD0F5", skyLow: "#FBD9A8", sun: "#FFE08A", hillBack: "#5BA870", hillFront: "#3E8E5A", wall: "#E9D3B5", roof: "#C75B4A" },
        { skyTop: "#2A2F6B", skyMid: "#8A5A9A", skyLow: "#F08A6E", sun: "#FFF1C9", hillBack: "#3C4F7A", hillFront: "#26334F", wall: "#B9A7C7", roof: "#7B3F5E" }
    ]
    readonly property var pal: palettes[variant]

    // every colour of the picture goes through the look
    function adj(hex) {
        const c = Qt.color(hex)
        const c2 = contrast + sharpen * 0.25
        const sat = saturation + sharpen * 0.1
        let r = c.r, g = c.g, b = c.b
        r += temperature * 0.25; b -= temperature * 0.25
        g -= tint * 0.08; r += tint * 0.04; b += tint * 0.04
        r += brightness * 0.3; g += brightness * 0.3; b += brightness * 0.3
        r = (r - 0.5) * (1 + c2) + 0.5; g = (g - 0.5) * (1 + c2) + 0.5; b = (b - 0.5) * (1 + c2) + 0.5
        const l = 0.299 * r + 0.587 * g + 0.114 * b
        r = l + (r - l) * (1 + sat); g = l + (g - l) * (1 + sat); b = l + (b - l) * (1 + sat)
        return Qt.rgba(Math.max(0, Math.min(1, r)), Math.max(0, Math.min(1, g)), Math.max(0, Math.min(1, b)), 1)
    }

    implicitWidth: 160
    implicitHeight: 90
    clip: true

    Item {
        id: art
        anchors.fill: parent
        scale: scene.zoom
        rotation: scene.tilt
        layer.enabled: scene.gpu && scene.blur > 0.01
        layer.effect: MultiEffect {
            blurEnabled: true
            blurMax: 32
            blur: scene.blur
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: scene.adj(scene.pal.skyTop) }
                GradientStop { position: 0.55; color: scene.adj(scene.pal.skyMid) }
                GradientStop { position: 1.0; color: scene.adj(scene.pal.skyLow) }
            }
        }
        Rectangle {                                                  // sun / moon
            width: parent.height * 0.34; height: width; radius: width / 2
            x: parent.width * 0.68; y: parent.height * 0.14
            color: scene.adj(scene.pal.sun)
        }
        Rectangle {                                                  // far hill
            width: parent.width * 1.1; height: parent.height * 1.2; radius: width / 2
            x: -parent.width * 0.15; y: parent.height * 0.62
            color: scene.adj(scene.pal.hillBack)
        }
        Rectangle {                                                  // house
            id: wall
            width: parent.width * 0.16; height: parent.height * 0.2
            x: parent.width * 0.18; y: parent.height * 0.5
            color: scene.adj(scene.pal.wall)
            Rectangle {
                width: parent.width * 0.78; height: width; rotation: 45
                x: (parent.width - width) / 2; y: -height * 0.62
                color: scene.adj(scene.pal.roof)
                z: -1
            }
        }
        Rectangle {                                                  // near hill
            width: parent.width * 1.3; height: parent.height * 1.4; radius: width / 2
            x: parent.width * 0.2; y: parent.height * 0.74
            color: scene.adj(scene.pal.hillFront)
        }
    }

    Rectangle { visible: scene.barsV > 0; width: parent.width; height: parent.height * scene.barsV; color: "#000000" }
    Rectangle { visible: scene.barsV > 0; y: parent.height * (1 - scene.barsV); width: parent.width; height: parent.height * scene.barsV; color: "#000000" }
    Rectangle { visible: scene.barsH > 0; width: parent.width * scene.barsH; height: parent.height; color: "#000000" }
    Rectangle { visible: scene.barsH > 0; x: parent.width * (1 - scene.barsH); width: parent.width * scene.barsH; height: parent.height; color: "#000000" }

    // vignette: the edges fade to black (four soft bands)
    Repeater {
        model: scene.vignette > 0.01 ? 4 : 0
        delegate: Rectangle {
            required property int index
            readonly property bool horizontal: index < 2
            width: horizontal ? scene.width : scene.width * 0.38
            height: horizontal ? scene.height * 0.5 : scene.height
            x: index === 3 ? scene.width - width : 0
            y: index === 1 ? scene.height - height : 0
            gradient: Gradient {
                orientation: horizontal ? Gradient.Vertical : Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, (index === 1 || index === 3) ? 0 : 0.85 * scene.vignette) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, (index === 1 || index === 3) ? 0.85 * scene.vignette : 0) }
            }
        }
    }
}
