import QtQuick
import QtQuick.Layouts
import DriveVault.Backend

// The master output level, always visible at the right edge of the timeline: a stereo LED-style meter (green / amber / red,
// the usual colours of audio tools) on a dark track with a dB scale next to it.
Rectangle {
    id: root

    readonly property int segments: 48
    readonly property color trackColor: "#0B0C0F"
    // 0 dB at the top, -60 dB at the bottom
    function position(level) {
        if (level <= 0.001)
            return 0
        return Math.max(0, Math.min(1, (20 * Math.log(level) / Math.LN10 + 60) / 60))
    }
    function segmentColor(i) {          // i = 0 is the top segment
        const p = (i + 0.5) / segments
        return p < 0.05 ? Theme.error : p < 0.2 ? Theme.warning : Theme.success
    }
    readonly property var marks: [ { db: 0, y: 0 }, { db: -6, y: 0.1 }, { db: -12, y: 0.2 }, { db: -24, y: 0.4 }, { db: -48, y: 0.8 } ]

    implicitWidth: 84
    color: Theme.backgroundAlt
    radius: Theme.radius
    border.color: Theme.borderSubtle
    border.width: 1

    // the stack of LED segments (top = 0 dB); drawn dim for the track and bright for the lit part
    component SegmentStack: Column {
        id: stack
        property real alpha: 1
        property real barHeight: 0
        spacing: 1
        Repeater {
            model: root.segments
            delegate: Rectangle {
                required property int index
                width: stack.width
                height: Math.max(1, (stack.barHeight - 4 - (root.segments - 1)) / root.segments)
                radius: 1
                color: Qt.alpha(root.segmentColor(index), stack.alpha)
            }
        }
    }

    // one channel: dim segments always, bright segments up to the level
    component Bar: Rectangle {
        id: bar
        property real level: 0
        implicitWidth: 12
        radius: 3
        color: root.trackColor
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.06)
        clip: true

        SegmentStack { x: 2; y: 2; width: bar.width - 4; barHeight: bar.height; alpha: 0.16 }
        Item {                                                  // the lit part grows from the bottom
            width: parent.width
            height: parent.height * root.position(bar.level)
            anchors.bottom: parent.bottom
            clip: true
            SegmentStack { x: 2; y: 2 - (bar.height - parent.height); width: bar.width - 4; barHeight: bar.height; alpha: 1 }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Master")
            font.pixelSize: Theme.fontXs
            font.weight: Font.DemiBold
            color: Theme.textSecondary
        }

        Item {                                                  // scale + meters, centred as one group
            Layout.fillWidth: true
            Layout.fillHeight: true
            Row {
                id: group
                anchors.horizontalCenter: parent.horizontalCenter
                height: parent.height
                spacing: 12

                Item {
                    id: scaleBox
                    width: 24
                    height: parent.height
                    Repeater {
                        model: root.marks
                        delegate: Text {
                            required property var modelData
                            width: scaleBox.width
                            y: Math.max(0, Math.min(scaleBox.height - height, modelData.y * scaleBox.height - height / 2))
                            horizontalAlignment: Text.AlignRight
                            text: modelData.db === 0 ? "0" : String(modelData.db)
                            font.pixelSize: 10
                            font.features: { "tnum": 1 }
                            color: Theme.textSecondary
                        }
                    }
                }
                Row {
                    height: parent.height
                    spacing: 4
                    Bar { objectName: "meterL"; height: parent.height; level: Playback.levelL }
                    Bar { objectName: "meterR"; height: parent.height; level: Playback.levelR }
                }
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.leftMargin: 36
            text: qsTr("L   R")
            font.pixelSize: 10
            color: Theme.textSecondary
        }
    }
}
