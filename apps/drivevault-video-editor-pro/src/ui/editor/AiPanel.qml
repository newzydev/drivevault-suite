import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

// AI workspace: assistant, captions (whisper.cpp), silence detection, smart cuts, audio enhancement, tracking and the
// model manager. Analyses never change the project; every "apply" button is an explicit confirmation and one undo step.
Item {
    id: root
    objectName: "aiPanel"

    readonly property bool enabled: AppSettings.values.aiEnabled
    readonly property bool hasClips: Projects.timeline.clipCount > 0

    function report(err) { if (err !== "") App.toast(err, "warning") }

    // accordion: one section open at a time (remembered)
    property string openSection: AppSettings.state("ai.section", "assistant")
    function toggleSection(id) {
        openSection = openSection === id ? "" : id
        AppSettings.setState("ai.section", openSection)
    }

    component Note: Text {
        Layout.fillWidth: true
        font.pixelSize: Theme.fontXs
        color: Theme.textDisabled
        wrapMode: Text.Wrap
    }
    component Lbl: Text {
        Layout.preferredWidth: 92
        font.pixelSize: Theme.fontSm
        color: Theme.textSecondary
        elide: Text.ElideRight
    }

    ConfirmDialog {
        id: silenceDialog
        title: qsTr("Remove the silent passages?")
        message: qsTr("%n silent passage(s) will be cut out of every track and the gaps closed. You can undo this.", "", Ai.silences.length)
        primaryText: qsTr("Remove")
        dangerPrimary: true
        onPrimary: root.report(Ai.applySilenceCuts())
    }
    ConfirmDialog {
        id: splitDialog
        title: qsTr("Split at every suggestion?")
        message: qsTr("The sequence is split at all %n suggested point(s). Nothing is deleted and you can undo this.", "", Ai.suggestions.length)
        primaryText: qsTr("Split")
        onPrimary: root.report(Ai.applyAllSuggestions())
    }

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        ColumnLayout {
            id: column
            x: 16
            y: 16
            width: parent.width - 32
            spacing: 16

            // ---- master switch + task status -------------------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Icon { name: "sparkle"; size: 18; color: Theme.accentText }
                Text { Layout.fillWidth: true; text: qsTr("AI features"); font.pixelSize: Theme.fontLg; font.weight: Font.DemiBold; color: Theme.text }
                AppSwitch { objectName: "aiEnable"; checked: root.enabled; onToggled: AppSettings.set("aiEnabled", checked) }
            }
            Note { text: qsTr("Optional and fully offline: nothing is sent to the cloud. Editing works the same with AI switched off.") }

            Rectangle {
                Layout.fillWidth: true
                visible: Ai.busy
                implicitHeight: busyCol.implicitHeight + 24
                radius: Theme.radius
                color: Theme.surface
                border.color: Theme.border
                ColumnLayout {
                    id: busyCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    Text { Layout.fillWidth: true; text: Ai.message; font.pixelSize: Theme.fontSm; color: Theme.text; wrapMode: Text.Wrap }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 6
                        radius: 3
                        color: Theme.surfaceHover
                        Rectangle { width: parent.width * Ai.progress; height: parent.height; radius: 3; color: Theme.accent }
                    }
                    RowLayout {
                        Text { Layout.fillWidth: true; text: Math.round(Ai.progress * 100) + "%"; font.pixelSize: Theme.fontXs; color: Theme.textSecondary }
                        AppButton { objectName: "aiCancel"; variant: "danger"; text: qsTr("Cancel"); implicitHeight: 28; onClicked: Ai.cancel() }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: !Ai.busy && Ai.message !== ""
                text: Ai.message
                font.pixelSize: Theme.fontSm
                color: Theme.textSecondary
                wrapMode: Text.Wrap
            }

            EmptyState {
                Layout.fillWidth: true
                visible: !root.enabled
                maxTextWidth: parent.width - 16
                iconName: "sparkle"
                title: qsTr("AI features are off")
                description: qsTr("Turn the switch on to use captions, silence detection, cut suggestions, audio enhancement, tracking and the assistant.")
            }

            // ---- assistant -----------------------------------------------------------------------------------
            ParamSection {
                id: assistantSection
                visible: root.enabled
                title: qsTr("Assistant")
                controlled: true
                expanded: root.openSection === "assistant"
                onToggleRequested: root.toggleSection("assistant")
                property var parsed: Ai.parseAssistant(askField.text)
                AppTextField {
                    id: askField
                    objectName: "aiAsk"
                    Layout.fillWidth: true
                    placeholderText: qsTr("e.g. “create Thai subtitles”")
                    onAccepted: runButton.clicked()
                }
                Text {
                    Layout.fillWidth: true
                    visible: askField.text !== ""
                    text: assistantSection.parsed.summary
                    font.pixelSize: Theme.fontSm
                    color: assistantSection.parsed.valid ? Theme.text : Theme.warning
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    AppButton {
                        id: runButton
                        objectName: "aiAskRun"
                        variant: "primary"
                        text: qsTr("Do it")
                        enabled: askField.text !== "" && assistantSection.parsed.valid && !Ai.busy
                        onClicked: root.report(Ai.runAssistant(askField.text))
                    }
                    Item { Layout.fillWidth: true }
                }
                Note { text: qsTr("The assistant understands a fixed set of Thai and English requests (it is not a chat model). It only runs the features below and never edits anything outside the project. Results are shown for your confirmation.") }
            }

            // ---- captions -----------------------------------------------------------------------------------
            ParamSection {
                visible: root.enabled
                title: qsTr("Auto captions")
                controlled: true
                expanded: root.openSection === "captions"
                onToggleRequested: root.toggleSection("captions")
                RowLayout {
                    Layout.fillWidth: true
                    StatusBadge {
                        objectName: "captionStatus"
                        kind: Ai.whisperStatus === "available" ? "available" : Ai.whisperStatus === "modelRequired" ? "modelRequired" : "unsupported"
                        label: Ai.whisperStatus === "engineMissing" ? qsTr("Engine missing") : ""
                    }
                    Item { Layout.fillWidth: true }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Lbl { text: qsTr("Language") }
                    AppComboBox {
                        id: langBox
                        Layout.fillWidth: true
                        implicitWidth: 100
                        current: "th"
                        options: [ { value: "th", label: "ไทย" }, { value: "en", label: "English" }, { value: "auto", label: qsTr("Detect automatically") } ]
                        onPicked: value => current = value
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    AppButton {
                        objectName: "aiTranscribe"
                        variant: "primary"
                        iconName: "text"
                        text: qsTr("Transcribe timeline")
                        enabled: root.hasClips && !Ai.busy && Ai.whisperStatus === "available"
                        onClicked: root.report(Ai.transcribe(langBox.current))
                    }
                    Item { Layout.fillWidth: true }
                }
                Note { visible: Ai.whisperStatus === "modelRequired"; text: qsTr("A speech model is required. Download or import one in the model manager below.") }
                Note { visible: Ai.whisperStatus === "engineMissing"; text: qsTr("The speech engine (whisper.cpp) was not found next to the application.") }
                Repeater {
                    model: Ai.captions.slice(0, 8)
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8
                        Text { text: modelData.timecode; font.pixelSize: Theme.fontXs; font.features: { "tnum": 1 }; color: Theme.accentText }
                        Text { Layout.fillWidth: true; text: modelData.text; font.pixelSize: Theme.fontSm; color: Theme.text; elide: Text.ElideRight }
                    }
                }
                Note { visible: Ai.captions.length > 8; text: qsTr("… and %n more phrase(s).", "", Ai.captions.length - 8) }
                RowLayout {
                    Layout.fillWidth: true
                    visible: Ai.captions.length > 0
                    AppButton { objectName: "aiApplyCaptions"; variant: "primary"; text: qsTr("Add to timeline"); onClicked: root.report(Ai.applyCaptions()) }
                    AppButton { variant: "ghost"; text: qsTr("Discard"); onClicked: Ai.clearCaptions() }
                    Item { Layout.fillWidth: true }
                }
                Note { text: qsTr("Automatic captions are not always accurate, especially with noise, music or names. Added captions are normal subtitle clips: select one to correct its text; File → Export or the Export workspace saves them as SRT.") }
            }

            // ---- silence --------------------------------------------------------------------------------------
            ParamSection {
                visible: root.enabled
                title: qsTr("Silence detection")
                controlled: true
                expanded: root.openSection === "silence"
                onToggleRequested: root.toggleSection("silence")
                RowLayout {
                    Layout.fillWidth: true
                    Lbl { text: qsTr("Threshold") }
                    AppSlider { id: silDb; objectName: "silDb"; Layout.fillWidth: true; implicitWidth: 60; from: -60; to: -20; stepSize: 1; value: -35 }
                    Text { Layout.preferredWidth: 52; horizontalAlignment: Text.AlignRight; text: silDb.value.toFixed(0) + " dB"; font.pixelSize: Theme.fontSm; color: Theme.textSecondary }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Lbl { text: qsTr("Min. length") }
                    AppSlider { id: silLen; Layout.fillWidth: true; implicitWidth: 60; from: 0.2; to: 3; stepSize: 0.1; value: 0.5 }
                    Text { Layout.preferredWidth: 52; horizontalAlignment: Text.AlignRight; text: silLen.value.toFixed(1) + " s"; font.pixelSize: Theme.fontSm; color: Theme.textSecondary }
                }
                RowLayout {
                    Layout.fillWidth: true
                    AppButton {
                        objectName: "aiSilence"
                        variant: "primary"
                        text: qsTr("Find silent passages")
                        enabled: root.hasClips && !Ai.busy
                        onClicked: root.report(Ai.detectSilence(silDb.value, Math.round(silLen.value * 1000)))
                    }
                    Item { Layout.fillWidth: true }
                }
                Repeater {
                    model: Ai.silences.slice(0, 6)
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8
                        Text { Layout.fillWidth: true; text: modelData.startTimecode + "  →  " + modelData.endTimecode; font.pixelSize: Theme.fontSm; font.features: { "tnum": 1 }; color: Theme.text }
                        Text { text: modelData.seconds.toFixed(1) + " s"; font.pixelSize: Theme.fontSm; color: Theme.textSecondary }
                        IconButton { iconName: "play"; iconSize: 14; implicitWidth: 24; implicitHeight: 24; tip: qsTr("Go to this passage"); onClicked: Projects.playheadFrame = modelData.start }
                    }
                }
                Note { visible: Ai.silences.length > 6; text: qsTr("… and %n more. All of them are highlighted in the timeline.", "", Ai.silences.length - 6) }
                RowLayout {
                    Layout.fillWidth: true
                    visible: Ai.silences.length > 0
                    AppButton { objectName: "aiApplySilence"; variant: "danger"; iconName: "scissors"; text: qsTr("Remove all…"); onClicked: silenceDialog.open() }
                    AppButton { variant: "ghost"; text: qsTr("Clear"); onClicked: Ai.clearSilences() }
                    Item { Layout.fillWidth: true }
                }
            }

            // ---- smart cuts -----------------------------------------------------------------------------------
            ParamSection {
                visible: root.enabled
                title: qsTr("Smart cut suggestions")
                controlled: true
                expanded: root.openSection === "cuts"
                onToggleRequested: root.toggleSection("cuts")
                RowLayout {
                    Layout.fillWidth: true
                    Lbl { text: qsTr("Sensitivity") }
                    AppSlider { id: cutSens; objectName: "cutSens"; Layout.fillWidth: true; implicitWidth: 60; from: 0; to: 1; value: 0.5 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    AppButton {
                        objectName: "aiCuts"
                        variant: "primary"
                        text: qsTr("Suggest cut points")
                        enabled: root.hasClips && !Ai.busy
                        onClicked: root.report(Ai.suggestCuts(cutSens.value, silDb.value, Math.round(silLen.value * 1000)))
                    }
                    Item { Layout.fillWidth: true }
                }
                Note { text: qsTr("Looks for scene changes inside clips and for pauses (using the silence settings above). Nothing is cut until you split.") }
                Repeater {
                    model: Ai.suggestions.slice(0, 10)
                    delegate: RowLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: 6
                        Text { text: modelData.timecode; font.pixelSize: Theme.fontSm; font.features: { "tnum": 1 }; color: Theme.accentText }
                        Text { Layout.fillWidth: true; text: modelData.label; font.pixelSize: Theme.fontSm; color: Theme.text; elide: Text.ElideRight }
                        IconButton { iconName: "play"; iconSize: 14; implicitWidth: 24; implicitHeight: 24; tip: qsTr("Go to this point"); onClicked: Projects.playheadFrame = modelData.frame }
                        IconButton { iconName: "scissors"; iconSize: 14; implicitWidth: 24; implicitHeight: 24; tip: qsTr("Split here"); onClicked: root.report(Ai.applySuggestion(index)) }
                    }
                }
                Note { visible: Ai.suggestions.length > 10; text: qsTr("… and %n more.", "", Ai.suggestions.length - 10) }
                RowLayout {
                    Layout.fillWidth: true
                    visible: Ai.suggestions.length > 0
                    AppButton { objectName: "aiSplitAll"; text: qsTr("Split at all…"); onClicked: splitDialog.open() }
                    AppButton { variant: "ghost"; text: qsTr("Clear"); onClicked: Ai.clearSuggestions() }
                    Item { Layout.fillWidth: true }
                }
            }

            // ---- audio enhancement ----------------------------------------------------------------------------
            ParamSection {
                visible: root.enabled
                title: qsTr("Audio enhancement")
                controlled: true
                expanded: root.openSection === "enhance"
                onToggleRequested: root.toggleSection("enhance")
                RowLayout {
                    Layout.fillWidth: true
                    Lbl { text: qsTr("Noise reduction") }
                    AppSlider { id: enhStrength; objectName: "enhStrength"; Layout.fillWidth: true; implicitWidth: 60; from: 0; to: 1; value: 0.7 }
                    Text { Layout.preferredWidth: 40; horizontalAlignment: Text.AlignRight; text: Math.round(enhStrength.value * 100) + "%"; font.pixelSize: Theme.fontSm; color: Theme.textSecondary }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Lbl { text: qsTr("Remove rumble") }
                    AppSwitch { id: enhHp; checked: true }
                    Item { Layout.fillWidth: true }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Lbl { text: qsTr("Even out level") }
                    AppSwitch { id: enhNorm; checked: true }
                    Item { Layout.fillWidth: true }
                }
                RowLayout {
                    Layout.fillWidth: true
                    AppButton {
                        objectName: "aiEnhance"
                        variant: "primary"
                        text: qsTr("Enhance selected audio")
                        enabled: Projects.timeline.selectedClipOfKind("audio") !== "" && !Ai.busy
                        onClicked: root.report(Ai.enhanceSelected(enhStrength.value, enhHp.checked, enhNorm.checked))
                    }
                    Item { Layout.fillWidth: true }
                }
                Note { text: qsTr("Select an audio clip first. Uses spectral noise reduction (classical signal processing, no neural model) and writes a cleaned copy into the project's AI folder; the clip then uses the copy and Undo restores the original.") }
            }

            // ---- tracking --------------------------------------------------------------------------------------
            ParamSection {
                id: trackSection
                visible: root.enabled
                title: qsTr("Object tracking")
                controlled: true
                expanded: root.openSection === "tracking"
                onToggleRequested: root.toggleSection("tracking")
                property var region: Ai.trackRegion.w !== undefined ? Ai.trackRegion : ({ x: 0.4, y: 0.4, w: 0.2, h: 0.2 })
                function setRegion(key, v) {
                    const r = Object.assign({}, region)
                    r[key] = v
                    r.x = Math.min(r.x, 1 - r.w)
                    r.y = Math.min(r.y, 1 - r.h)
                    Ai.trackRegion = r
                }
                RowLayout {
                    Layout.fillWidth: true
                    Lbl { text: qsTr("Show region") }
                    AppSwitch {
                        objectName: "trackShow"
                        checked: Ai.trackRegion.w !== undefined
                        onToggled: Ai.trackRegion = checked ? ({ x: 0.4, y: 0.4, w: 0.2, h: 0.2 }) : ({})
                    }
                    Item { Layout.fillWidth: true }
                }
                Repeater {
                    model: [ { k: "x", n: qsTr("Left"), lo: 0, hi: 0.95 }, { k: "y", n: qsTr("Top"), lo: 0, hi: 0.95 },
                             { k: "w", n: qsTr("Width"), lo: 0.04, hi: 0.8 }, { k: "h", n: qsTr("Height"), lo: 0.04, hi: 0.8 } ]
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        visible: Ai.trackRegion.w !== undefined
                        Lbl { text: modelData.n }
                        AppSlider {
                            Layout.fillWidth: true
                            implicitWidth: 60
                            from: modelData.lo; to: modelData.hi
                            value: Ai.trackRegion[modelData.k] !== undefined ? Ai.trackRegion[modelData.k] : 0
                            onMoved: trackSection.setRegion(modelData.k, value)
                        }
                    }
                }
                Note { text: qsTr("Select a video clip, move the playhead to a frame where the subject is visible, place the rectangle over it (drag it on the monitor or use the sliders) and track.") }
                RowLayout {
                    Layout.fillWidth: true
                    AppButton {
                        objectName: "aiTrack"
                        variant: "primary"
                        text: qsTr("Track from playhead")
                        enabled: Ai.trackRegion.w !== undefined && !Ai.busy
                        onClicked: root.report(Ai.startTracking(Ai.trackRegion.x, Ai.trackRegion.y, Ai.trackRegion.w, Ai.trackRegion.h))
                    }
                    Item { Layout.fillWidth: true }
                }
                Text {
                    Layout.fillWidth: true
                    visible: Ai.tracking.frames !== undefined
                    text: Ai.tracking.frames !== undefined ? qsTr("%n frame(s) tracked, average match %1 %", "", Ai.tracking.frames).arg(Math.round(Ai.tracking.confidence * 100)) : ""
                    font.pixelSize: Theme.fontSm
                    color: Theme.text
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: Ai.tracking.frames !== undefined
                    AppButton {
                        objectName: "aiStabilize"
                        text: qsTr("Stabilize clip")
                        onClicked: root.report(Ai.applyTracking("stabilize", ""))
                    }
                    AppButton {
                        objectName: "aiFollow"
                        text: qsTr("Selected clip follows")
                        enabled: Projects.timeline.primarySelectedId !== "" && Projects.timeline.primarySelectedId !== Ai.tracking.clipId
                        onClicked: root.report(Ai.applyTracking("follow", Projects.timeline.primarySelectedId))
                    }
                    AppButton { variant: "ghost"; text: qsTr("Clear"); onClicked: Ai.clearTracking() }
                    Item { Layout.fillWidth: true }
                }
                Note { text: qsTr("Stabilize counter-moves the tracked clip; \"follows\" moves another video or image clip (for example a logo on a layer above) with the subject. Both write Position keyframes you can edit in the Inspector.") }
            }

            // ---- model manager ---------------------------------------------------------------------------------
            ParamSection {
                visible: root.enabled
                title: qsTr("Speech models")
                controlled: true
                expanded: root.openSection === "models"
                onToggleRequested: root.toggleSection("models")
                ModelManagerView {}
            }
        }
    }
}
