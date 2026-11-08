import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

// The audio mixer (a tab of the timeline panel): one channel strip per audio track - Gain / High / Mid / Low knobs of the clip that is under the
// playhead on that track, pan, the track fader with solo and mute - and the master fader at the right. Everything drives the same parameters that
// the inspector, the preview and the export use (clip volume, eqHigh / eqMid / eqLow, track gain / pan, project master level).
Rectangle {
    id: root

    color: Theme.backgroundAlt
    radius: Theme.radius
    border.color: Theme.borderSubtle
    border.width: 1
    clip: true

    property int rev: 0
    Connections {
        target: Projects.timeline
        function onClipsChanged() { root.rev++ }
        function onSelectionChanged() { root.rev++ }
    }
    Connections {
        target: Projects
        function onPlayheadChanged() { root.rev++ }
    }
    function report(error) { if (error !== "") App.toast(error, "warning") }
    function panText(p) { return Math.abs(p) < 0.005 ? qsTr("Centre") : (p < 0 ? "L " : "R ") + Math.round(Math.abs(p) * 100) }
    function dbText(v) { return (v > 0 ? "+" : "") + v.toFixed(1) + " dB" }

    // one strip: a rounded panel with a colour bar on top; everything inside is centred
    component Strip: Rectangle {
        id: strip
        property color tint: Theme.success
        default property alias content: body.data
        height: parent ? parent.height : 300
        radius: Theme.radius
        color: Theme.surface
        border.width: 1
        border.color: Theme.borderSubtle
        Rectangle { x: 1; y: 1; width: parent.width - 2; height: 3; radius: 1.5; color: strip.tint }
        ColumnLayout {
            id: body
            anchors.fill: parent
            anchors.margins: 10
            anchors.topMargin: 12
            spacing: 6
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        Flickable {
            id: strips
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: Math.max(stripRow.width, width)
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            clip: true
            ScrollBar.horizontal: ScrollBar { policy: stripRow.width > strips.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }

            Row {
                id: stripRow
                x: Math.max(0, (strips.width - width) / 2)          // few strips: centred in the panel
                height: strips.height - (stripRow.width > strips.width ? 10 : 0)
                spacing: 8
                Repeater {
                    model: Projects.tracks
                    delegate: Strip {
                        id: channel
                        required property int index
                        required property string trackId
                        required property string name
                        required property string kind
                        required property bool muted
                        required property bool solo
                        required property real gainDb
                        required property real pan

                        // the clip of this track under the playhead (the knobs edit it)
                        readonly property string clipId: { root.rev; return kind === "audio" ? Projects.timeline.clipAt(index, Projects.playheadFrame) : "" }
                        readonly property bool hasClip: clipId !== ""
                        function pv(key) { root.rev; return hasClip ? Projects.timeline.paramValue(clipId, key, Projects.playheadFrame) : 0 }
                        function setP(key, v) { root.report(Projects.timeline.setParam(clipId, key, v, Projects.playheadFrame)) }

                        visible: kind === "audio"
                        width: visible ? 262 : 0

                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: channel.name
                            elide: Text.ElideRight
                            maximumLineCount: 1
                            font.pixelSize: Theme.fontMd
                            font.weight: Font.Bold
                            color: Theme.text
                        }
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: channel.hasClip ? qsTr("Clip under the playhead") : qsTr("No clip at the playhead")
                            elide: Text.ElideRight
                            maximumLineCount: 1
                            font.pixelSize: 10
                            color: Theme.textDisabled
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 10

                            // knobs
                            ColumnLayout {
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 0
                                MixerKnob {
                                    objectName: "mixGain" + channel.index
                                    enabled: channel.hasClip
                                    label: qsTr("Gain")
                                    implicitWidth: 156
                                    from: -60; to: 12; resetValue: 0
                                    value: channel.pv("volume")
                                    formatter: root.dbText
                                    onMoved: v => channel.setP("volume", v)
                                }
                                MixerKnob {
                                    objectName: "mixHigh" + channel.index
                                    enabled: channel.hasClip
                                    label: qsTr("High")
                                    implicitWidth: 156
                                    from: -18; to: 18; resetValue: 0
                                    value: channel.pv("eqHigh")
                                    formatter: root.dbText
                                    onMoved: v => channel.setP("eqHigh", v)
                                }
                                MixerKnob {
                                    objectName: "mixMid" + channel.index
                                    enabled: channel.hasClip
                                    label: qsTr("Mid")
                                    implicitWidth: 156
                                    from: -18; to: 18; resetValue: 0
                                    value: channel.pv("eqMid")
                                    formatter: root.dbText
                                    onMoved: v => channel.setP("eqMid", v)
                                }
                                MixerKnob {
                                    objectName: "mixLow" + channel.index
                                    enabled: channel.hasClip
                                    label: qsTr("Low")
                                    implicitWidth: 156
                                    from: -18; to: 18; resetValue: 0
                                    value: channel.pv("eqLow")
                                    formatter: root.dbText
                                    onMoved: v => channel.setP("eqLow", v)
                                }
                                Rectangle { Layout.fillWidth: true; Layout.topMargin: 3; Layout.bottomMargin: 3; height: 1; color: Theme.borderSubtle }
                                MixerKnob {
                                    objectName: "trackPan" + channel.index
                                    label: qsTr("Pan")
                                    implicitWidth: 156
                                    tint: Theme.success
                                    from: -1; to: 1; resetValue: 0
                                    value: channel.pan
                                    formatter: root.panText
                                    onMoved: v => Projects.tracks.setPan(channel.index, Math.abs(v) < 0.04 ? 0 : v)
                                }
                            }

                            Rectangle { Layout.fillHeight: true; Layout.topMargin: 4; Layout.bottomMargin: 4; width: 1; color: Theme.borderSubtle }

                            // fader, level and solo / mute
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 4
                                MixerFader {
                                    objectName: "trackGain" + channel.index
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.fillHeight: true
                                    Layout.minimumHeight: 60
                                    value: channel.gainDb
                                    tint: Theme.success
                                    onMoved: db => Projects.tracks.setGain(channel.index, db)
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: root.dbText(channel.gainDb)
                                    font.pixelSize: 10
                                    font.features: { "tnum": 1 }
                                    color: Theme.textSecondary
                                }
                                RowLayout {
                                    Layout.alignment: Qt.AlignHCenter
                                    spacing: 4
                                    AppButton {
                                        objectName: "trackSolo" + channel.index
                                        text: "S"
                                        implicitWidth: 30; implicitHeight: 24; leftPadding: 0; rightPadding: 0
                                        variant: channel.solo ? "primary" : "secondary"
                                        onClicked: Projects.tracks.toggle(channel.index, "solo")
                                        AppToolTip { visible: parent.hovered; text: qsTr("Solo") }
                                    }
                                    AppButton {
                                        objectName: "trackMute" + channel.index
                                        text: "M"
                                        implicitWidth: 30; implicitHeight: 24; leftPadding: 0; rightPadding: 0
                                        variant: channel.muted ? "danger" : "secondary"
                                        onClicked: Projects.tracks.toggle(channel.index, "muted")
                                        AppToolTip { visible: parent.hovered; text: qsTr("Mute") }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Text {
                visible: stripRow.width === 0
                anchors.centerIn: parent
                text: qsTr("No audio track yet. Add one with the \"+ Audio\" button of the timeline.")
                color: Theme.textDisabled
                font.pixelSize: Theme.fontSm
            }
        }

        // master fader: the level of everything that is heard and exported (the meter at the far right shows the result)
        Strip {
            objectName: "masterStrip"
            Layout.fillHeight: true
            Layout.preferredWidth: 112
            tint: Theme.accent
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Master")
                font.pixelSize: Theme.fontMd
                font.weight: Font.Bold
                color: Theme.text
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Volume")
                font.pixelSize: 10
                color: Theme.textDisabled
            }
            MixerFader {
                objectName: "masterGain"
                Layout.alignment: Qt.AlignHCenter
                Layout.fillHeight: true
                Layout.minimumHeight: 60
                tint: Theme.accent
                value: Projects.masterGainDb
                onMoved: db => Projects.setMasterGainDb(db)
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: root.dbText(Projects.masterGainDb)
                font.pixelSize: 10
                font.features: { "tnum": 1 }
                color: Theme.textSecondary
            }
        }
    }
}
