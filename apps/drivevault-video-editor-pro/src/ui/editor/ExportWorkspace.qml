import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.Backend

// Export workspace: renders the timeline to MP4 / MOV / MKV (H.264 + AAC) with progress, cancel and a
// validation of the finished file. Everything here runs the real exporter.
Rectangle {
    id: root

    readonly property bool running: ExportJob.state === "running"
    readonly property bool hasClips: Projects.timeline.clipCount > 0
    readonly property string container: AppSettings.values.exportFormat
    readonly property string preset: AppSettings.values.exportPreset
    property string fileName: ""
    readonly property string folder: AppSettings.values.exportFolder !== "" ? AppSettings.values.exportFolder : SysInfo.defaultExportDirectory
    readonly property size outSize: {
        Projects.width; Projects.height   // re-evaluate when the project changes
        return ExportJob.outputSize(preset, AppSettings.values.exportCustomWidth, AppSettings.values.exportCustomHeight)
    }
    readonly property bool hasAudio: ExportJob.hasAudio
    readonly property bool audioOnly: container === "m4a"
    readonly property bool hasRange: Projects.timeline.inPoint >= 0 || Projects.timeline.outPoint >= 0
    property var userPresets: AppSettings.state("exportPresets", [])

    function applyPreset(p) {
        AppSettings.set("exportFormat", p.format)
        AppSettings.set("exportPreset", p.preset)
        AppSettings.set("exportQuality", p.quality)
        AppSettings.set("exportAudioBitrate", p.audioKbps)
    }
    function saveCurrentPreset(name) {
        const list = userPresets.filter(p => p.label !== name)
        list.push({ id: "user:" + name, label: name, description: qsTr("Saved preset"), format: container, preset: preset,
                    quality: AppSettings.values.exportQuality, audioKbps: AppSettings.values.exportAudioBitrate })
        AppSettings.setState("exportPresets", list)
        userPresets = list
    }
    function deletePreset(id) {
        const list = userPresets.filter(p => p.id !== id)
        AppSettings.setState("exportPresets", list)
        userPresets = list
    }
    Connections {
        target: Projects.timeline
        function onRangeChanged() { if (!root.hasRange) ExportJob.range = "all" }
    }

    color: Theme.backgroundAlt
    radius: Theme.radius
    border.color: Theme.borderSubtle

    Component.onCompleted: {
        fileName = Projects.name
        ExportJob.detectEncoders()
    }
    Connections { target: Projects; function onProjectChanged() { root.fileName = Projects.name; ExportJob.reset() } }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose export folder")
        onAccepted: AppSettings.set("exportFolder", Projects.toLocalPath(selectedFolder.toString()))
    }

    function startExport() {
        const err = ExportJob.start(fileName, folder, container, preset, AppSettings.values.exportCustomWidth,
                                    AppSettings.values.exportCustomHeight, AppSettings.values.exportQuality,
                                    AppSettings.values.exportAudioBitrate, AppSettings.values.exportEncoder)
        if (err !== "")
            App.toast(err, "warning")
    }
    FileDialog {
        id: srtDialog
        title: qsTr("Export subtitles (.srt)")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "srt"
        nameFilters: [qsTr("SubRip subtitles (*.srt)")]
        onAccepted: {
            const err = Projects.timeline.exportSrt(selectedFile.toString())
            App.toast(err !== "" ? err : qsTr("Subtitles exported."), err !== "" ? "warning" : "success")
        }
    }
    AppDialog {
        id: presetDialog
        title: qsTr("Save export preset")
        implicitWidth: 380
        onOpened: { presetName.text = ""; presetName.forceActiveFocus() }
        AppTextField { id: presetName; Layout.fillWidth: true; placeholderText: qsTr("Preset name"); maximumLength: 40; onAccepted: presetOk.clicked() }
        footerContent: [
            Item { Layout.fillWidth: true },
            AppButton { variant: "ghost"; text: qsTr("Cancel"); onClicked: presetDialog.close() },
            AppButton { id: presetOk; variant: "primary"; text: qsTr("Save"); enabled: presetName.text.trim() !== ""
                        onClicked: { root.saveCurrentPreset(presetName.text.trim()); presetDialog.close() } }
        ]
    }
    function exportStill() {
        const err = ExportJob.exportFrame(fileName + "_" + Projects.playheadFrame, folder, Projects.playheadFrame)
        App.toast(err !== "" ? err : qsTr("Frame saved: %1").arg(ExportJob.outputPath), err !== "" ? "warning" : "success")
    }
    function requestExport() {
        const path = ExportJob.outputFilePath(fileName, folder, container)
        if (ExportJob.fileExists(path))
            overwrite.open()
        else
            startExport()
    }

    ConfirmDialog {
        id: overwrite
        title: qsTr("Replace existing file?")
        message: qsTr("A file with this name already exists in the output folder. Exporting replaces it.")
        primaryText: qsTr("Replace")
        dangerPrimary: true
        onPrimary: root.startExport()
    }

    // The actions stay visible while the form scrolls.
    Rectangle {
        id: actionBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: root.running ? 0 : 64
        visible: !root.running
        color: Theme.backgroundAlt
        radius: Theme.radius
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.borderSubtle }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            spacing: 10
            Item { Layout.fillWidth: true }
            AppButton {
                objectName: "exportFrame"
                iconName: "image"
                text: qsTr("Save Frame (PNG)")
                implicitHeight: 40
                enabled: root.hasClips && root.fileName.trim() !== ""
                onClicked: root.exportStill()
            }
            AppButton {
                objectName: "exportSrt"
                iconName: "text"
                text: qsTr("Export Subtitles…")
                implicitHeight: 40
                visible: Projects.timeline.subtitleCount > 0
                onClicked: srtDialog.open()
            }
            AppButton {
                objectName: "exportStart"
                variant: "primary"
                iconName: "export"
                text: qsTr("Export")
                implicitHeight: 40
                leftPadding: 22
                rightPadding: 22
                enabled: root.hasClips && root.fileName.trim() !== "" && (!root.audioOnly || root.hasAudio)
                onClicked: root.requestExport()
            }
        }
    }

    Flickable {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: actionBar.visible ? actionBar.top : parent.bottom
        contentHeight: column.implicitHeight + 64
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        ColumnLayout {
            id: column
            x: Math.max(24, (parent.width - width) / 2)
            y: 32
            width: Math.min(780, parent.width - 48)
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Icon { name: "export"; size: 26; color: Theme.accentText }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Export")
                    font.pixelSize: Theme.fontXxl
                    font.weight: Font.Bold
                    color: Theme.text
                }
                StatusBadge { kind: "available"; label: qsTr("H.264 + AAC") }
            }

            Text {
                Layout.fillWidth: true
                text: root.hasClips
                      ? qsTr("Sequence: %1  ·  %2 × %3  ·  %4 fps").arg(Projects.timecodeFor(Projects.timeline.durationFrames))
                            .arg(Projects.width).arg(Projects.height).arg(Projects.frameRateLabel)
                        + (root.hasAudio ? "  ·  " + qsTr("with audio") : "  ·  " + qsTr("no audio"))
                      : qsTr("The timeline is empty. Add clips in the Edit workspace, then come back to export.")
                font.pixelSize: Theme.fontMd
                color: root.hasClips ? Theme.textSecondary : Theme.warning
                wrapMode: Text.Wrap
            }

            // ---- presets -----------------------------------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                enabled: !root.running
                Text { text: qsTr("Presets"); font.pixelSize: Theme.fontXs; font.weight: Font.DemiBold; font.capitalization: Font.AllUppercase; font.letterSpacing: 0.8; color: Theme.accentText }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: ExportJob.builtinPresets().concat(root.userPresets)
                        delegate: AppButton {
                            required property var modelData
                            objectName: "preset_" + modelData.id
                            text: modelData.label
                            implicitHeight: 30
                            variant: "secondary"
                            onClicked: root.applyPreset(modelData)
                            AppToolTip { visible: parent.hovered; text: parent.modelData.description }
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                enabled: String(parent.modelData.id).indexOf("user:") === 0
                                onTapped: root.deletePreset(parent.modelData.id)
                            }
                        }
                    }
                    AppButton { objectName: "presetSave"; variant: "ghost"; iconName: "plus"; text: qsTr("Save current…"); implicitHeight: 30; onClicked: presetDialog.open() }
                }
                Text {
                    visible: root.userPresets.length > 0
                    text: qsTr("Right-click one of your own presets to delete it.")
                    font.pixelSize: Theme.fontXs
                    color: Theme.textDisabled
                }
            }

            // ---- form ---------------------------------------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                enabled: !root.running
                opacity: root.running ? 0.55 : 1

                SettingRow {
                    title: qsTr("File name")
                    description: ExportJob.outputFilePath(root.fileName, root.folder, root.container)
                    AppTextField {
                        implicitWidth: 260
                        text: root.fileName
                        onTextEdited: root.fileName = text
                        maximumLength: 120
                    }
                }
                SettingRow {
                    title: qsTr("Output folder")
                    description: root.folder
                    AppButton { iconName: "folder"; text: qsTr("Browse…"); onClicked: folderDialog.open() }
                }
                SettingRow {
                    title: qsTr("Format")
                    description: qsTr("Container for the exported file (H.264 video, AAC audio). M4A exports the sound only.")
                    AppComboBox {
                        current: root.container
                        options: [ { value: "mp4", label: "MP4" }, { value: "mov", label: "MOV" }, { value: "mkv", label: "MKV" },
                                   { value: "m4a", label: qsTr("M4A (audio only)") } ]
                        onPicked: value => AppSettings.set("exportFormat", value)
                    }
                }
                SettingRow {
                    title: qsTr("Range")
                    description: root.hasRange ? qsTr("Between the In and Out marks set in the timeline (I / O).")
                                               : qsTr("Set In and Out marks in the timeline (I / O) to export only a part.")
                    AppComboBox {
                        objectName: "exportRange"
                        enabled: root.hasRange
                        current: root.hasRange ? ExportJob.range : "all"
                        options: [ { value: "all", label: qsTr("Whole sequence") }, { value: "inout", label: qsTr("In to Out") } ]
                        onPicked: value => ExportJob.range = value
                    }
                }
                SettingRow {
                    title: qsTr("Resolution")
                    dimmed: root.audioOnly
                    description: qsTr("Output: %1 × %2 (the project's aspect ratio is kept)").arg(root.outSize.width).arg(root.outSize.height)
                    RowLayout {
                        visible: root.preset === "custom"
                        spacing: 6
                        AppTextField {
                            implicitWidth: 80
                            text: AppSettings.values.exportCustomWidth
                            validator: IntValidator { bottom: 64; top: 8192 }
                            onEditingFinished: AppSettings.set("exportCustomWidth", parseInt(text) || 1920)
                        }
                        Text { text: "×"; color: Theme.textSecondary; font.pixelSize: Theme.fontMd }
                        AppTextField {
                            implicitWidth: 80
                            text: AppSettings.values.exportCustomHeight
                            validator: IntValidator { bottom: 64; top: 8192 }
                            onEditingFinished: AppSettings.set("exportCustomHeight", parseInt(text) || 1080)
                        }
                    }
                    AppComboBox {
                        current: root.preset
                        options: [ { value: "720p", label: "720p" }, { value: "1080p", label: "1080p" }, { value: "1440p", label: "1440p" },
                                   { value: "4k", label: "4K UHD" }, { value: "custom", label: qsTr("Custom") } ]
                        onPicked: value => AppSettings.set("exportPreset", value)
                    }
                }
                SettingRow {
                    title: qsTr("Quality")
                    dimmed: root.audioOnly
                    description: {
                        Projects.timeline.durationFrames
                        const size = ExportJob.estimatedSize(root.preset, AppSettings.values.exportCustomWidth, AppSettings.values.exportCustomHeight,
                                                             AppSettings.values.exportQuality, AppSettings.values.exportAudioBitrate, root.container)
                        return size !== "" ? qsTr("Estimated file size: about %1").arg(size) : qsTr("Higher quality produces larger files.")
                    }
                    AppSlider {
                        implicitWidth: 200
                        from: 0; to: 100; stepSize: 1
                        value: AppSettings.values.exportQuality
                        onMoved: AppSettings.set("exportQuality", Math.round(value))
                    }
                    Text {
                        Layout.preferredWidth: 36
                        text: AppSettings.values.exportQuality
                        horizontalAlignment: Text.AlignRight
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontMd
                    }
                }
                SettingRow {
                    title: qsTr("Audio bitrate")
                    dimmed: !root.hasAudio
                    description: root.hasAudio ? "" : qsTr("The sequence has no audio, so the file will have no audio track.")
                    AppComboBox {
                        enabled: root.hasAudio
                        current: AppSettings.values.exportAudioBitrate
                        options: [ { value: 128, label: "128 kbps" }, { value: 192, label: "192 kbps" },
                                   { value: 256, label: "256 kbps" }, { value: 320, label: "320 kbps" } ]
                        onPicked: value => AppSettings.set("exportAudioBitrate", value)
                    }
                }
                SettingRow {
                    title: qsTr("Video encoder")
                    dimmed: root.audioOnly
                    description: ExportJob.detectingEncoders ? qsTr("Detecting available encoders…")
                                 : ExportJob.encoders.length === 0 ? qsTr("No H.264 encoder was found.")
                                 : qsTr("Automatic picks the first encoder that works, hardware first.")
                    AppComboBox {
                        implicitWidth: 260
                        current: AppSettings.values.exportEncoder
                        options: {
                            const list = [ { value: "auto", label: qsTr("Automatic") } ]
                            for (const e of ExportJob.encoders)
                                list.push({ value: e.id, label: e.label })
                            return list
                        }
                        onPicked: value => AppSettings.set("exportEncoder", value)
                    }
                }
            }

            // ---- progress ------------------------------------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                visible: root.running
                implicitHeight: progressColumn.implicitHeight + 36
                radius: Theme.radius
                color: Theme.surface
                border.color: Theme.border

                ColumnLayout {
                    id: progressColumn
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        Icon { id: busy; name: "refresh"; size: 18; color: Theme.accentText
                               RotationAnimator on rotation { running: busy.visible; from: 0; to: 360; duration: 1100; loops: Animation.Infinite } }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Rendering…")
                            font.pixelSize: Theme.fontLg
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }
                        Text {
                            text: Math.round(ExportJob.progress * 100) + "%"
                            font.pixelSize: Theme.fontLg
                            font.features: { "tnum": 1 }
                            color: Theme.accentText
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 8
                        radius: 4
                        color: Theme.surfaceHover
                        Rectangle {
                            width: parent.width * ExportJob.progress
                            height: parent.height
                            radius: 4
                            color: Theme.accent
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("%1 of %2 frames  ·  %3 fps  ·  time left %4").arg(ExportJob.framesDone).arg(ExportJob.totalFrames)
                                  .arg(ExportJob.throughputFps.toFixed(1)).arg(ExportJob.etaText === "" ? "–" : ExportJob.etaText)
                            font.pixelSize: Theme.fontSm
                            font.features: { "tnum": 1 }
                            color: Theme.textSecondary
                        }
                        AppButton { objectName: "exportCancel"; variant: "danger"; iconName: "close"; text: qsTr("Cancel"); onClicked: ExportJob.cancel() }
                    }
                }
            }

            // ---- result ------------------------------------------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                visible: ExportJob.state === "done"
                implicitHeight: doneColumn.implicitHeight + 36
                radius: Theme.radius
                color: Qt.alpha(Theme.success, 0.08)
                border.color: Qt.alpha(Theme.success, 0.5)

                ColumnLayout {
                    id: doneColumn
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 10

                    RowLayout {
                        spacing: 10
                        Icon { name: "check"; size: 20; color: Theme.success; strokeWidth: 2.4 }
                        Text { text: qsTr("Export complete"); font.pixelSize: Theme.fontLg; font.weight: Font.DemiBold; color: Theme.text }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: ExportJob.outputPath
                        font.pixelSize: Theme.fontSm
                        color: Theme.textSecondary
                        wrapMode: Text.WrapAnywhere
                    }
                    Text {
                        Layout.fillWidth: true
                        readonly property var r: ExportJob.result
                        text: r.width ? (r.width + " × " + r.height + "  ·  " + Number(r.fps).toFixed(2) + " fps  ·  " + Projects.timecodeFor(Math.round(r.durationMs / 1000 * Projects.nominalFps))
                                         + "  ·  " + (r.hasAudio ? r.audioCodec.toUpperCase() + " " + (r.audioRate / 1000) + " kHz" : qsTr("no audio"))
                                         + "  ·  " + (r.fileSize / 1048576).toFixed(1) + " MB") : ""
                        font.pixelSize: Theme.fontSm
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Checked: resolution, frame rate, duration and audio match the sequence, and the picture decodes.")
                        font.pixelSize: Theme.fontSm
                        color: Theme.success
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Encoder: %1  ·  speed: %2 frames/s  ·  took %3 s").arg(ExportJob.encoderUsed)
                              .arg(Number(ExportJob.result.throughputFps).toFixed(1)).arg(Number(ExportJob.result.seconds).toFixed(1))
                        font.pixelSize: Theme.fontXs
                        color: Theme.textDisabled
                    }
                    RowLayout {
                        spacing: 8
                        AppButton { variant: "primary"; iconName: "film"; text: qsTr("Play"); onClicked: SysInfo.openFile(ExportJob.outputPath) }
                        AppButton { iconName: "folder"; text: qsTr("Show in Folder"); onClicked: SysInfo.showInFolder(ExportJob.outputPath) }
                        AppButton { variant: "ghost"; text: qsTr("New Export"); onClicked: ExportJob.reset() }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: ExportJob.state === "failed" || ExportJob.state === "cancelled"
                implicitHeight: failColumn.implicitHeight + 36
                radius: Theme.radius
                color: Qt.alpha(ExportJob.state === "failed" ? Theme.error : Theme.warning, 0.08)
                border.color: Qt.alpha(ExportJob.state === "failed" ? Theme.error : Theme.warning, 0.5)

                ColumnLayout {
                    id: failColumn
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 10
                    RowLayout {
                        spacing: 10
                        Icon { name: "warning"; size: 20; color: ExportJob.state === "failed" ? Theme.error : Theme.warning }
                        Text {
                            text: ExportJob.state === "failed" ? qsTr("Export failed") : qsTr("Export cancelled")
                            font.pixelSize: Theme.fontLg
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }
                    }
                    Text { Layout.fillWidth: true; text: ExportJob.message; font.pixelSize: Theme.fontSm; color: Theme.textSecondary; wrapMode: Text.Wrap }
                    AppButton { text: qsTr("Back"); onClicked: ExportJob.reset() }
                }
            }
        }
    }
}
