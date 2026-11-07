import QtQuick
import QtQuick.Layouts
import DriveVault.Backend

Rectangle {
    id: root

    property real pxPerFrame: 4

    implicitHeight: 28
    color: Theme.backgroundAlt

    Rectangle { width: parent.width; height: 1; color: Theme.borderSubtle }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 16

        RowLayout {
            spacing: 6
            Icon {
                name: Projects.dirty ? "warning" : "check"
                size: 13
                color: Projects.dirty ? Theme.warning : Theme.success
            }
            Text {
                text: Projects.dirty ? qsTr("Unsaved changes") : qsTr("All changes saved")
                font.pixelSize: Theme.fontXs
                color: Theme.textSecondary
            }
        }
        Text {
            Layout.fillWidth: true
            text: Projects.filePath
            font.pixelSize: Theme.fontXs
            color: Theme.textDisabled
            elide: Text.ElideMiddle
        }
        // Live playback metrics
        Text {
            visible: Playback.playing
            text: qsTr("Preview") + " " + Playback.playbackFps.toFixed(1) + " fps  ·  " + qsTr("dropped") + " " + Playback.droppedFrames
                  + "  ·  " + qsTr("render") + " " + Playback.renderMs.toFixed(0) + " ms  ·  " + Playback.audioBackend
            font.pixelSize: Theme.fontXs
            font.features: { "tnum": 1 }
            color: Theme.textSecondary
        }
        StatusBadge {
            kind: "available"
            label: "FFmpeg " + Playback.engineDescription.split(" ")[1].replace("n", "").split(".").slice(0, 2).join(".")
        }
        Text {
            text: Projects.width + " × " + Projects.height + "  ·  " + Projects.frameRateLabel + " fps  ·  " + (Projects.sampleRate / 1000) + " kHz"
            font.pixelSize: Theme.fontXs
            color: Theme.textSecondary
        }
        Text {
            text: qsTr("Zoom") + " " + Math.round(root.pxPerFrame / 4 * 100) + "%"
            font.pixelSize: Theme.fontXs
            color: Theme.textSecondary
        }
    }
}
