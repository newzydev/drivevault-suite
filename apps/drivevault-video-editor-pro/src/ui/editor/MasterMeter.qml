import QtQuick
import QtQuick.Layouts
import DriveVault.Backend

// The master output level, always visible at the right edge of the timeline (stereo, dB scale).
Rectangle {
    id: root

    // 0 dB at the top, -60 dB at the bottom
    function position(level) {
        if (level <= 0.001)
            return 0
        return Math.max(0, Math.min(1, (20 * Math.log(level) / Math.LN10 + 60) / 60))
    }
    readonly property var marks: [ { db: 0, y: 0 }, { db: -6, y: 0.1 }, { db: -12, y: 0.2 }, { db: -24, y: 0.4 }, { db: -48, y: 0.8 } ]

    implicitWidth: 64
    color: Theme.backgroundAlt
    radius: Theme.radius
    border.color: Theme.borderSubtle
    border.width: 1

    component Bar: Rectangle {
        property real level: 0
        implicitWidth: 11
        radius: 3
        color: Theme.surfaceHover
        clip: true
        Item {
            width: parent.width
            height: parent.height * root.position(parent.level)
            anchors.bottom: parent.bottom
            clip: true
            Rectangle {
                width: parent.width
                height: parent.parent.height
                anchors.bottom: parent.bottom
                radius: 3
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.error }
                    GradientStop { position: 0.12; color: Theme.warning }
                    GradientStop { position: 0.3; color: Theme.success }
                    GradientStop { position: 1.0; color: Qt.darker(Theme.success, 1.5) }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Master")
            font.pixelSize: Theme.fontXs
            font.weight: Font.DemiBold
            color: Theme.textSecondary
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Item {
                id: scaleBox
                width: 24
                height: parent.height
                Repeater {
                    model: root.marks
                    delegate: Text {
                        required property var modelData
                        x: 0
                        y: Math.min(scaleBox.height - height, Math.max(0, modelData.y * scaleBox.height - height / 2))
                        width: scaleBox.width
                        horizontalAlignment: Text.AlignRight
                        text: modelData.db === 0 ? "0" : String(modelData.db)
                        font.pixelSize: 9
                        font.features: { "tnum": 1 }
                        color: Theme.textDisabled
                    }
                }
            }
            Row {
                anchors.right: parent.right
                height: parent.height
                spacing: 3
                Bar { objectName: "meterL"; height: parent.height; level: Playback.levelL }
                Bar { objectName: "meterR"; height: parent.height; level: Playback.levelR }
            }
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("L   R")
            font.pixelSize: 9
            color: Theme.textDisabled
        }
    }
}
