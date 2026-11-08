import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.Backend

// Inspector: details of the selected clip or media item and the controls of the selected clip - transform,
// colour, grading, curves, effects, transitions, audio, titles - plus the audio mixer and the project summary.
Rectangle {
    id: root

    readonly property string clipId: Projects.timeline.primarySelectedId
    readonly property var clipData: clipId !== "" ? Projects.timeline.clipInfo(clipId) : ({})
    readonly property var mediaInfo: App.selectedMediaId !== "" ? Projects.media.describe(App.selectedMediaId)
                                    : (clipId !== "" && clipData.mediaId ? Projects.media.describe(clipData.mediaId) : ({}))
    readonly property bool hasSelection: clipId !== "" || App.selectedMediaId !== ""

    // Clip data changes (edits, undo, keyframes ...) bump the revision so that the parameter rows refresh.
    readonly property string ws: App.workspace   // each workspace shows the controls that belong to it
    property int rev: 0
    Connections {
        target: Projects.timeline
        function onClipsChanged() { root.rev++ }
        function onSelectionChanged() { root.rev++ }
    }

    // The clip each section edits: the picture clip, the sound clip and the subtitle clip of the selection.
    readonly property string videoId: { rev; return Projects.timeline.selectedClipOfKind("video") }
    readonly property string audioId: { rev; return Projects.timeline.selectedClipOfKind("audio") }
    readonly property string subtitleId: { rev; return Projects.timeline.selectedClipOfKind("subtitle") }
    readonly property var videoData: { rev; return videoId !== "" ? Projects.timeline.clipInfo(videoId) : ({}) }
    readonly property var audioData: { rev; return audioId !== "" ? Projects.timeline.clipInfo(audioId) : ({}) }
    readonly property bool videoIsTitle: videoData.type === "title"
    readonly property bool videoIsMedia: videoId !== "" && !videoIsTitle
    readonly property bool videoIsImageOrVideo: videoId !== ""

    color: Theme.backgroundAlt
    radius: Theme.radius
    border.color: Theme.borderSubtle
    clip: true

    FileDialog {
        id: lutDialog
        title: qsTr("Choose a colour look (.cube)")
        nameFilters: [qsTr("3D LUT (*.cube)"), qsTr("All files (*)")]
        onAccepted: root.report(Projects.timeline.setLut(root.videoId, selectedFile.toString()))
    }
    function report(error) { if (error !== "") App.toast(error, "warning") }

    component InfoRows: GridLayout {
        id: rows
        property var items: []
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 12
        rowSpacing: 8
        Repeater {
            model: rows.items
            delegate: Item {
                required property var modelData
                Layout.columnSpan: 2
                Layout.fillWidth: true
                implicitHeight: Math.max(k.implicitHeight, v.implicitHeight)
                Text { id: k; width: 92; text: modelData.k; font.pixelSize: Theme.fontSm; color: Theme.textSecondary }
                Text {
                    id: v
                    x: 104
                    width: parent.width - 104
                    text: String(modelData.v)
                    font.pixelSize: Theme.fontSm
                    font.features: modelData.mono ? { "tnum": 1 } : {}
                    color: modelData.warn ? Theme.error : Theme.text
                    elide: Text.ElideMiddle                         // a long value is cut with ... (the full text is the tooltip)
                    maximumLineCount: 1
                    HoverHandler { id: valueHover }
                    AppToolTip { visible: valueHover.hovered && v.truncated; text: String(modelData.v) }
                }
            }
        }
    }
    component Heading: Text {
        font.pixelSize: Theme.fontXs
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
        color: Theme.accentText
    }
    component VP: ParamRow {
        clipId: root.videoId
        revision: root.rev
    }
    component AP: ParamRow {
        clipId: root.audioId
        revision: root.rev
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PanelHeader {
            Layout.fillWidth: true
            title: qsTr("Inspector")
            iconName: "sliders"
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: content.implicitHeight + 32
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}

            ColumnLayout {
                id: content
                x: 16
                y: 16
                width: parent.width - 32
                spacing: 16

                EmptyState {
                    Layout.fillWidth: true
                    visible: !root.hasSelection
                    maxTextWidth: parent.width - 16
                    iconName: "cursor"
                    title: qsTr("Nothing selected")
                    description: qsTr("Select a clip on the timeline or a media item to see its details here.")
                }

                // ---- selected clip -----------------------------------------------------------------
                Heading { visible: root.clipId !== ""; text: qsTr("Clip") }
                InfoRows {
                    visible: root.clipId !== ""
                    items: root.clipId === "" ? [] : [
                        { k: qsTr("Name"), v: root.clipData.name },
                        { k: qsTr("Track"), v: root.clipData.trackId + (root.clipData.linked ? "  ·  " + qsTr("linked") : "") },
                        { k: qsTr("Start"), v: root.clipData.startTimecode, mono: true },
                        { k: qsTr("Duration"), v: root.clipData.durationTimecode, mono: true },
                        { k: qsTr("End"), v: root.clipData.endTimecode, mono: true },
                        { k: qsTr("Source in"), v: root.clipData.sourceInTimecode, mono: true },
                        { k: qsTr("Status"), v: root.clipData.offline ? qsTr("Media file is missing") : qsTr("OK"), warn: root.clipData.offline }
                    ]
                }
                Text {
                    Layout.fillWidth: true
                    visible: root.clipId !== "" && Projects.timeline.selectedCount > 1
                    text: qsTr("%n clips selected", "", Projects.timeline.selectedCount)
                    font.pixelSize: Theme.fontSm
                    color: Theme.textSecondary
                }

                // ---- title / subtitle text -----------------------------------------------------------
                ParamSection {
                    objectName: "sectionTitle"
                    visible: root.videoIsTitle
                    title: qsTr("Title")
                    clipId: root.videoId
                    revision: root.rev
                    TitleEditor { clipId: root.videoId; revision: root.rev }
                }
                ParamSection {
                    objectName: "sectionSubtitle"
                    visible: root.subtitleId !== ""
                    title: qsTr("Subtitle")
                    clipId: root.subtitleId
                    revision: root.rev
                    TitleEditor { clipId: root.subtitleId; revision: root.rev }
                }

                // ---- transform ------------------------------------------------------------------------
                ParamSection {
                    objectName: "sectionTransform"
                    visible: root.videoId !== "" && root.ws !== "color" && root.ws !== "audio"
                    title: qsTr("Transform")
                    group: "transform"
                    clipId: root.videoId
                    revision: root.rev
                    VP { label: qsTr("Opacity"); paramKey: "opacity"; displayScale: 100; decimals: 0; unit: "%" }
                    VP { label: qsTr("Position X"); paramKey: "posX"; displayScale: 100; decimals: 0 }
                    VP { label: qsTr("Position Y"); paramKey: "posY"; displayScale: 100; decimals: 0 }
                    VP { label: qsTr("Scale"); paramKey: "scale"; displayScale: 100; decimals: 0 }
                    VP { label: qsTr("Rotation"); paramKey: "rotation"; decimals: 1 }
                }

                // ---- colour ---------------------------------------------------------------------------
                ParamSection {
                    objectName: "sectionColor"
                    visible: root.videoId !== "" && (root.ws === "edit" || root.ws === "color" || root.ws === "media")
                    title: qsTr("Colour")
                    group: "color"
                    clipId: root.videoId
                    revision: root.rev
                    VP { label: qsTr("Exposure"); paramKey: "exposure" }
                    VP { label: qsTr("Brightness"); paramKey: "brightness" }
                    VP { label: qsTr("Contrast"); paramKey: "contrast" }
                    VP { label: qsTr("Saturation"); paramKey: "saturation" }
                    VP { label: qsTr("Temperature"); paramKey: "temperature" }
                    VP { label: qsTr("Tint"); paramKey: "tint" }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        AppButton {
                            objectName: "lutImport"
                            iconName: "paint"
                            text: root.videoData.lut ? qsTr("Change look…") : qsTr("Import look…")
                            onClicked: lutDialog.open()
                        }
                        AppButton {
                            visible: !!root.videoData.lut
                            variant: "ghost"
                            text: qsTr("Remove")
                            onClicked: root.report(Projects.timeline.setLut(root.videoId, ""))
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !!root.videoData.lut
                        text: root.videoData.lut ? root.videoData.lut : ""
                        font.pixelSize: Theme.fontXs
                        color: Theme.textSecondary
                        elide: Text.ElideMiddle
                    }
                    VP { visible: !!root.videoData.lut; label: qsTr("Look strength"); paramKey: "lutIntensity" }
                }

                ParamSection {
                    objectName: "sectionGrade"
                    visible: root.videoId !== "" && (root.ws === "edit" || root.ws === "color")
                    title: qsTr("Lift · Gamma · Gain")
                    group: "grade"
                    clipId: root.videoId
                    revision: root.rev
                    defaultExpanded: false
                    Repeater {
                        model: [ { n: qsTr("Lift"), k: "lift" }, { n: qsTr("Gamma"), k: "gamma" }, { n: qsTr("Gain"), k: "gain" } ]
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 4
                            Text { text: modelData.n; font.pixelSize: Theme.fontSm; font.weight: Font.DemiBold; color: Theme.text }
                            VP { label: "R"; paramKey: modelData.k + "R" }
                            VP { label: "G"; paramKey: modelData.k + "G" }
                            VP { label: "B"; paramKey: modelData.k + "B" }
                        }
                    }
                }

                ParamSection {
                    objectName: "sectionCurves"
                    visible: root.videoId !== "" && (root.ws === "edit" || root.ws === "color")
                    title: qsTr("Curves")
                    clipId: root.videoId
                    revision: root.rev
                    defaultExpanded: false
                    CurvesEditor { clipId: root.videoId; revision: root.rev }
                }

                // ---- effects ----------------------------------------------------------------------------
                ParamSection {
                    objectName: "sectionEffects"
                    visible: root.videoId !== "" && root.ws !== "color" && root.ws !== "audio"
                    title: qsTr("Effects")
                    group: "effects"
                    clipId: root.videoId
                    revision: root.rev
                    VP { label: qsTr("Blur"); paramKey: "blur"; decimals: 1 }
                    VP { label: qsTr("Sharpen"); paramKey: "sharpen" }
                    VP { label: qsTr("Vignette"); paramKey: "vignette" }
                    VP { label: qsTr("Crop left"); paramKey: "cropL"; displayScale: 100; decimals: 0 }
                    VP { label: qsTr("Crop right"); paramKey: "cropR"; displayScale: 100; decimals: 0 }
                    VP { label: qsTr("Crop top"); paramKey: "cropT"; displayScale: 100; decimals: 0 }
                    VP { label: qsTr("Crop bottom"); paramKey: "cropB"; displayScale: 100; decimals: 0 }
                }

                // ---- transitions ---------------------------------------------------------------------------
                ParamSection {
                    objectName: "sectionTransitions"
                    visible: root.videoId !== "" && root.ws !== "color" && root.ws !== "audio"
                    title: qsTr("Transitions")
                    clipId: root.videoId
                    revision: root.rev
                    Repeater {
                        model: [ { dir: "in", label: qsTr("In"),
                                   types: [ { value: "", label: qsTr("None") }, { value: "dissolve", label: qsTr("Cross dissolve") },
                                            { value: "dip", label: qsTr("Dip to black") }, { value: "fade", label: qsTr("Fade from black") },
                                            { value: "wipe", label: qsTr("Wipe right") }, { value: "wipeLeft", label: qsTr("Wipe left") },
                                            { value: "wipeDown", label: qsTr("Wipe down") }, { value: "wipeUp", label: qsTr("Wipe up") },
                                            { value: "slide", label: qsTr("Slide from right") }, { value: "slideLeft", label: qsTr("Slide from left") },
                                            { value: "slideUp", label: qsTr("Slide from bottom") }, { value: "slideDown", label: qsTr("Slide from top") },
                                            { value: "zoom", label: qsTr("Zoom in") }, { value: "zoomOut", label: qsTr("Zoom out") },
                                            { value: "spin", label: qsTr("Spin") }, { value: "flash", label: qsTr("Flash") } ] },
                                 { dir: "out", label: qsTr("Out"),
                                   types: [ { value: "", label: qsTr("None") }, { value: "fade", label: qsTr("Fade to black") },
                                            { value: "dip", label: qsTr("Dip to black") }, { value: "wipe", label: qsTr("Wipe out") },
                                            { value: "slide", label: qsTr("Slide out") }, { value: "zoom", label: qsTr("Shrink away") },
                                            { value: "spin", label: qsTr("Spin") }, { value: "flash", label: qsTr("Flash") } ] } ]
                        delegate: ColumnLayout {
                            id: tr
                            required property var modelData
                            readonly property string type: root.videoData[modelData.dir === "in" ? "transIn" : "transOut"] || ""
                            readonly property real frames: root.videoData[modelData.dir === "in" ? "transInFrames" : "transOutFrames"] || 0
                            Layout.fillWidth: true
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                Text { Layout.preferredWidth: 78; text: tr.modelData.label; font.pixelSize: Theme.fontSm; color: Theme.textSecondary }
                                AppComboBox {
                                    objectName: "transition_" + tr.modelData.dir
                                    Layout.fillWidth: true
                                    implicitWidth: 100
                                    current: tr.type
                                    options: tr.modelData.types
                                    onPicked: value => root.report(Projects.timeline.setTransition(root.videoId, tr.modelData.dir, value,
                                                                                                   value === "" ? 0 : Math.max(tr.frames, Math.round(Projects.nominalFps / 2))))
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                visible: tr.type !== ""
                                Text { Layout.preferredWidth: 78; text: qsTr("Duration"); font.pixelSize: Theme.fontSm; color: Theme.textSecondary }
                                AppSlider {
                                    Layout.fillWidth: true
                                    implicitWidth: 60
                                    from: 2; to: Math.max(4, Math.min(Projects.nominalFps * 4, (root.videoData.duration || 8) - 1)); stepSize: 1
                                    value: tr.frames
                                    onMoved: root.report(Projects.timeline.setTransition(root.videoId, tr.modelData.dir, tr.type, Math.round(value)))
                                }
                                Text { Layout.preferredWidth: 46; horizontalAlignment: Text.AlignRight; text: (tr.frames / Math.max(1, Projects.nominalFps)).toFixed(2) + " s"; font.pixelSize: Theme.fontSm; font.features: { "tnum": 1 }; color: Theme.textSecondary }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("An \"In\" transition blends from the clip before it when the clips touch, otherwise from black.")
                        font.pixelSize: Theme.fontXs
                        color: Theme.textDisabled
                        wrapMode: Text.Wrap
                    }
                }

                // ---- audio ---------------------------------------------------------------------------------
                ParamSection {
                    objectName: "sectionAudio"
                    visible: root.audioId !== "" && root.ws !== "color" && root.ws !== "effects"
                    title: qsTr("Audio")
                    group: "audio"
                    clipId: root.audioId
                    revision: root.rev
                    AP { label: qsTr("Volume"); paramKey: "volume"; decimals: 1; unit: "dB" }
                    AP { label: qsTr("Bass"); paramKey: "eqLow"; decimals: 1 }
                    AP { label: qsTr("Mid"); paramKey: "eqMid"; decimals: 1 }
                    AP { label: qsTr("Treble"); paramKey: "eqHigh"; decimals: 1 }
                    AP { label: qsTr("Noise gate"); paramKey: "gate"; decimals: 0 }
                    Repeater {
                        model: [ { key: "fadeIn", label: qsTr("Fade in") }, { key: "fadeOut", label: qsTr("Fade out") } ]
                        delegate: RowLayout {
                            id: fade
                            required property var modelData
                            readonly property real frames: root.audioData[modelData.key] || 0
                            Layout.fillWidth: true
                            Text { Layout.preferredWidth: 78; text: fade.modelData.label; font.pixelSize: Theme.fontSm; color: fade.frames > 0 ? Theme.text : Theme.textSecondary }
                            AppSlider {
                                objectName: "slider_" + fade.modelData.key
                                Layout.fillWidth: true
                                implicitWidth: 60
                                from: 0; to: Math.max(1, Math.min(Projects.nominalFps * 10, (root.audioData.duration || 1) / 2)); stepSize: 1
                                value: fade.frames
                                onMoved: {
                                    const fi = fade.modelData.key === "fadeIn" ? Math.round(value) : (root.audioData.fadeIn || 0)
                                    const fo = fade.modelData.key === "fadeOut" ? Math.round(value) : (root.audioData.fadeOut || 0)
                                    root.report(Projects.timeline.setFades(root.audioId, fi, fo))
                                }
                            }
                            Text { Layout.preferredWidth: 46; horizontalAlignment: Text.AlignRight; text: (fade.frames / Math.max(1, Projects.nominalFps)).toFixed(2) + " s"; font.pixelSize: Theme.fontSm; font.features: { "tnum": 1 }; color: Theme.textSecondary }
                        }
                    }
                }

                // ---- media -----------------------------------------------------------------------------
                Heading { visible: root.hasSelection && root.mediaInfo.name !== undefined; text: qsTr("Media") }
                InfoRows {
                    visible: root.hasSelection && root.mediaInfo.name !== undefined
                    items: root.mediaInfo.name === undefined ? [] : [
                        { k: qsTr("Name"), v: root.mediaInfo.name },
                        { k: qsTr("Type"), v: root.mediaInfo.kind === "audio" ? qsTr("Audio") : root.mediaInfo.kind === "image" ? qsTr("Image") : qsTr("Video") },
                        { k: qsTr("Duration"), v: root.mediaInfo.durationText },
                        { k: qsTr("Format"), v: root.mediaInfo.infoText },
                        { k: qsTr("Status"), v: root.mediaInfo.status === "ready" ? qsTr("OK") : root.mediaInfo.status === "missing" ? qsTr("File not found") : root.mediaInfo.status, warn: root.mediaInfo.status === "missing" },
                        { k: qsTr("Path"), v: root.mediaInfo.path }
                    ]
                }
                AppButton {
                    visible: App.selectedMediaId !== "" && root.mediaInfo.status === "ready"
                    iconName: "plus"
                    text: qsTr("Add to Timeline")
                    onClicked: {
                        const err = Projects.timeline.addMedia(App.selectedMediaId, -1, Projects.playheadFrame)
                        if (err !== "")
                            App.toast(err, "warning")
                    }
                }

                // preview proxy of the selected media (large videos)
                RowLayout {
                    id: proxyRow
                    Layout.fillWidth: true
                    readonly property string mediaId: App.selectedMediaId !== "" ? App.selectedMediaId : (root.clipData.mediaId || "")
                    readonly property string proxyState: { Proxies.proxyCount; Proxies.busy; return mediaId !== "" ? Proxies.state(mediaId) : "notNeeded" }
                    visible: proxyState !== "notNeeded"
                    spacing: 8
                    AppButton {
                        objectName: "mediaProxy"
                        visible: proxyRow.proxyState === "none"
                        iconName: "film"
                        text: qsTr("Create preview proxy")
                        onClicked: root.report(Proxies.createProxy(proxyRow.mediaId))
                    }
                    StatusBadge { visible: proxyRow.proxyState === "ready"; kind: "available"; label: qsTr("Proxy ready") }
                    StatusBadge { visible: proxyRow.proxyState === "building"; kind: "info"; label: qsTr("Building proxy…") }
                    StatusBadge { visible: proxyRow.proxyState === "unsupported"; kind: "unsupported"; label: qsTr("No proxy: variable frame rate") }
                    Item { Layout.fillWidth: true }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }

                // ---- project -----------------------------------------------------------------------------
                Heading { text: qsTr("Project") }
                InfoRows {
                    visible: Projects.isOpen
                    items: [
                        { k: qsTr("Name"), v: Projects.name },
                        { k: qsTr("Resolution"), v: Projects.width + " × " + Projects.height },
                        { k: qsTr("Frame rate"), v: Projects.frameRateLabel + " fps" },
                        { k: qsTr("Audio"), v: (Projects.sampleRate / 1000) + " kHz" },
                        { k: qsTr("Duration"), v: Projects.timecodeFor(Projects.timeline.durationFrames), mono: true },
                        { k: qsTr("Media"), v: Projects.media.count },
                        { k: qsTr("Clips"), v: Projects.timeline.clipCount },
                        { k: qsTr("File"), v: Projects.filePath }
                    ]
                }
            }
        }
    }
}
