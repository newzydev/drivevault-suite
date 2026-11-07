import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

// Audio mixer: one strip per audio track (gain, pan, solo, mute). The strips drive the same mixer that preview
// and export use.
ColumnLayout {
    id: root

    Layout.fillWidth: true
    spacing: 10

    // The master level meter lives at the right edge of the timeline (MasterMeter.qml) so that it is always visible.

    Repeater {
        model: Projects.tracks
        delegate: ColumnLayout {
            id: strip
            required property int index
            required property string trackId
            required property string name
            required property string kind
            required property bool muted
            required property bool solo
            required property real gainDb
            required property real pan

            visible: kind === "audio"
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? implicitHeight : 0
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Rectangle { width: 4; height: 16; radius: 2; color: Theme.success }
                Text { Layout.fillWidth: true; Layout.leftMargin: 4; text: strip.name; font.pixelSize: Theme.fontSm; font.weight: Font.DemiBold; color: Theme.text; elide: Text.ElideRight }
                AppButton {
                    objectName: "trackSolo" + strip.index
                    text: "S"
                    implicitWidth: 28; implicitHeight: 24; leftPadding: 0; rightPadding: 0
                    variant: strip.solo ? "primary" : "secondary"
                    onClicked: Projects.tracks.toggle(strip.index, "solo")
                    AppToolTip { visible: parent.hovered; text: qsTr("Solo") }
                }
                AppButton {
                    objectName: "trackMute" + strip.index
                    text: "M"
                    implicitWidth: 28; implicitHeight: 24; leftPadding: 0; rightPadding: 0
                    variant: strip.muted ? "danger" : "secondary"
                    onClicked: Projects.tracks.toggle(strip.index, "muted")
                    AppToolTip { visible: parent.hovered; text: qsTr("Mute") }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.preferredWidth: 78; text: qsTr("Volume"); font.pixelSize: Theme.fontSm; color: Theme.textSecondary }
                AppSlider {
                    objectName: "trackGain" + strip.index
                    Layout.fillWidth: true
                    implicitWidth: 60
                    from: -60; to: 12
                    value: strip.gainDb
                    onMoved: Projects.tracks.setGain(strip.index, value)
                    onPressedChanged: if (!pressed) value = Qt.binding(() => strip.gainDb)
                    Accessible.name: qsTr("Track volume")
                }
                Text { Layout.preferredWidth: 52; horizontalAlignment: Text.AlignRight; text: strip.gainDb.toFixed(1) + " dB"; font.pixelSize: Theme.fontSm; font.features: { "tnum": 1 }; color: Theme.textSecondary }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.preferredWidth: 78; text: qsTr("Pan"); font.pixelSize: Theme.fontSm; color: Theme.textSecondary }
                AppSlider {
                    objectName: "trackPan" + strip.index
                    Layout.fillWidth: true
                    implicitWidth: 60
                    from: -1; to: 1
                    value: strip.pan
                    onMoved: Projects.tracks.setPan(strip.index, Math.abs(value) < 0.04 ? 0 : value)
                    onPressedChanged: if (!pressed) value = Qt.binding(() => strip.pan)
                    Accessible.name: qsTr("Track pan")
                }
                Text {
                    Layout.preferredWidth: 52
                    horizontalAlignment: Text.AlignRight
                    text: Math.abs(strip.pan) < 0.005 ? qsTr("Centre") : (strip.pan < 0 ? "L " : "R ") + Math.round(Math.abs(strip.pan) * 100)
                    font.pixelSize: Theme.fontSm
                    color: Theme.textSecondary
                }
            }
        }
    }
}
