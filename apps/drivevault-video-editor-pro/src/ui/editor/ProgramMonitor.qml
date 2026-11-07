import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import DriveVault.Backend

// Program monitor: the real composite of the timeline at the playhead (same renderer as the export),
// transport controls, preview quality, safe areas and a full-screen monitor window.
Rectangle {
    id: root

    property bool safeAreas: false
    readonly property bool hasClips: Projects.timeline.clipCount > 0

    color: Theme.backgroundAlt
    radius: Theme.radius
    border.color: Theme.borderSubtle
    clip: true

    function toggleFullscreen() { fullscreen.visible = !fullscreen.visible }

    // ---- full-screen monitor ----------------------------------------------------------------------
    Window {
        id: fullscreen
        visible: false
        visibility: visible ? Window.FullScreen : Window.Hidden
        color: "#000000"
        title: qsTr("Program Monitor")
        FrameView { anchors.fill: parent; engine: Playback }
        MouseArea { anchors.fill: parent; onDoubleClicked: fullscreen.visible = false; onClicked: Playback.togglePlay() }
        Shortcut { sequence: "Escape"; enabled: fullscreen.visible; onActivated: fullscreen.visible = false }
        Shortcut { sequence: "Space"; enabled: fullscreen.visible; onActivated: Playback.togglePlay() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PanelHeader {
            Layout.fillWidth: true
            title: qsTr("Program Monitor")
            iconName: "film"
            IconButton {
                iconName: "safeArea"
                checkable: true
                checked: root.safeAreas
                tip: qsTr("Safe areas")
                onToggled: root.safeAreas = checked
            }
            IconButton { iconName: "fit"; tip: qsTr("Full-screen monitor") + "  (Esc)"; onClicked: root.toggleFullscreen() }
        }

        Item {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true

            readonly property real aspect: Projects.isOpen && Projects.height > 0 ? Projects.width / Projects.height : 16 / 9

            Rectangle {
                id: frame
                anchors.centerIn: parent
                width: Math.max(80, Math.min(stage.width - 32, (stage.height - 32) * stage.aspect))
                height: width / stage.aspect
                color: Theme.monitor
                border.color: Theme.border

                FrameView {
                    anchors.fill: parent
                    anchors.margins: 1
                    engine: Playback
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    visible: !root.hasClips
                    spacing: 8
                    width: parent.width - 24
                    Icon { Layout.alignment: Qt.AlignHCenter; name: "film"; size: 30; color: "#5A5E6B" }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("No media on the timeline")
                        font.pixelSize: Theme.fontMd
                        color: "#9A9EAB"
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Double-click a media item or drag it onto the timeline.")
                        font.pixelSize: Theme.fontSm
                        color: "#6E7280"
                        wrapMode: Text.Wrap
                    }
                }

                // Click the picture to select a clip; frame, corner handles (scale) and a round handle (rotate) appear
                TransformBox {
                    id: transformBox
                    anchors.fill: parent
                    frame: frame
                    z: 15
                    visible: root.hasClips
                    onFullscreenRequested: root.toggleFullscreen()
                }

                // Region chosen for object tracking (AI workspace): drag to move, drag the corner to resize
                Rectangle {
                    id: trackBox
                    objectName: "trackBox"
                    readonly property var r: Ai.trackRegion
                    visible: r.w !== undefined
                    x: (r.x || 0) * frame.width
                    y: (r.y || 0) * frame.height
                    width: (r.w || 0) * frame.width
                    height: (r.h || 0) * frame.height
                    color: Qt.alpha(Theme.accent, 0.12)
                    border.width: 2
                    border.color: Theme.accent
                    z: 20
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.SizeAllCursor
                        drag.target: dragProxy
                        property real px: 0
                        property real py: 0
                        onPressed: mouse => { px = mouse.x; py = mouse.y }
                        onPositionChanged: mouse => {
                            if (!pressed)
                                return
                            const nx = Math.max(0, Math.min(1 - trackBox.r.w, trackBox.r.x + (mouse.x - px) / frame.width))
                            const ny = Math.max(0, Math.min(1 - trackBox.r.h, trackBox.r.y + (mouse.y - py) / frame.height))
                            Ai.trackRegion = { x: nx, y: ny, w: trackBox.r.w, h: trackBox.r.h }
                        }
                        Item { id: dragProxy }
                    }
                    Rectangle {
                        width: 12; height: 12
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        color: Theme.accent
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeFDiagCursor
                            property real px: 0
                            property real py: 0
                            onPressed: mouse => { px = mouse.x; py = mouse.y }
                            onPositionChanged: mouse => {
                                if (!pressed)
                                    return
                                const nw = Math.max(0.04, Math.min(1 - trackBox.r.x, trackBox.r.w + (mouse.x - px) / frame.width))
                                const nh = Math.max(0.04, Math.min(1 - trackBox.r.y, trackBox.r.h + (mouse.y - py) / frame.height))
                                Ai.trackRegion = { x: trackBox.r.x, y: trackBox.r.y, w: nw, h: nh }
                            }
                        }
                    }
                }

                // Action safe (90 %) and title safe (80 %) guides
                Rectangle {
                    visible: root.safeAreas
                    anchors.centerIn: parent
                    width: parent.width * 0.9
                    height: parent.height * 0.9
                    color: "transparent"
                    border.color: Qt.alpha(Theme.warning, 0.8)
                }
                Rectangle {
                    visible: root.safeAreas
                    anchors.centerIn: parent
                    width: parent.width * 0.8
                    height: parent.height * 0.8
                    color: "transparent"
                    border.color: Qt.alpha(Theme.info, 0.8)
                }

                Text {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 8
                    text: Projects.isOpen ? (Projects.width + " × " + Projects.height + "  ·  " + Projects.frameRateLabel + " fps") : ""
                    font.pixelSize: Theme.fontXs
                    color: "#8A8E9B"
                    style: Text.Outline
                    styleColor: "#99000000"
                }
            }

            MouseArea {
                anchors.fill: parent
                z: -1
                onDoubleClicked: root.toggleFullscreen()
            }
        }

        // Transport bar
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 52
            color: Theme.backgroundAlt
            Rectangle { width: parent.width; height: 1; color: Theme.borderSubtle }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                spacing: 6

                Text {
                    Layout.preferredWidth: 130
                    text: Projects.timecode
                    font.pixelSize: Theme.fontXl
                    font.weight: Font.DemiBold
                    font.features: { "tnum": 1 }
                    color: Theme.accentText
                    Accessible.name: qsTr("Timecode")
                }

                Item { Layout.fillWidth: true }

                IconButton {
                    iconName: "skipBack"
                    tip: qsTr("Go to start") + "  (Home)"
                    enabled: Projects.playheadFrame > 0
                    onClicked: Playback.goToStart()
                }
                IconButton {
                    iconName: "stepBack"
                    tip: qsTr("Previous frame") + "  (←)"
                    enabled: Projects.playheadFrame > 0
                    onClicked: Projects.stepFrames(-1)
                }
                IconButton {
                    objectName: "playButton"
                    iconName: Playback.playing ? "pause" : "play"
                    filledIcon: !Playback.playing
                    implicitWidth: 40
                    implicitHeight: 34
                    iconSize: 20
                    enabled: root.hasClips
                    tip: (Playback.playing ? qsTr("Pause") : qsTr("Play")) + "  (Space)"
                    onClicked: Playback.togglePlay()
                }
                IconButton {
                    iconName: "stepForward"
                    tip: qsTr("Next frame") + "  (→)"
                    onClicked: Projects.stepFrames(1)
                }
                IconButton {
                    iconName: "skipForward"
                    tip: qsTr("Go to end") + "  (End)"
                    enabled: root.hasClips
                    onClicked: Playback.goToEnd()
                }

                Item { Layout.fillWidth: true }

                AppComboBox {
                    implicitWidth: 120
                    implicitHeight: Theme.controlHeightSmall
                    current: AppSettings.values.previewQuality
                    options: [
                        { value: "full", label: qsTr("Full") },
                        { value: "half", label: qsTr("1/2") },
                        { value: "quarter", label: qsTr("1/4") }
                    ]
                    onPicked: value => AppSettings.set("previewQuality", value)
                    AppToolTip { visible: parent.hovered; text: qsTr("Preview quality") }
                }
            }
        }
    }
}
