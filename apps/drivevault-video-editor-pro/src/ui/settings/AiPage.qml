import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

ColumnLayout {
    spacing: 0

    Section {
        title: qsTr("AI assistant")
        note: qsTr("AI is an optional module. Editing works fully without it, and nothing is processed in the cloud.")
    }

    SettingRow {
        title: qsTr("Enable AI features")
        description: qsTr("Shows the AI workspace tools. Everything runs on this computer.")
        AppSwitch {
            checked: AppSettings.values.aiEnabled
            onToggled: AppSettings.set("aiEnabled", checked)
        }
    }
    SettingRow {
        title: qsTr("Processing device")
        dimmed: !AppSettings.values.aiEnabled
        AppComboBox {
            enabled: AppSettings.values.aiEnabled
            current: AppSettings.values.aiDevice
            options: [ { value: "auto", label: qsTr("Automatic") }, { value: "cpu", label: "CPU" }, { value: "gpu", label: "GPU" } ]
            onPicked: value => AppSettings.set("aiDevice", value)
        }
    }
    PathSetting {
        Layout.fillWidth: true
        title: qsTr("Model storage location")
        settingKey: "aiModelsLocation"
        defaultPath: SysInfo.dataDirectory + "\\models"
    }
    SettingRow {
        title: qsTr("Offline only")
        description: qsTr("Never send audio, video or transcripts off this computer. Model downloads are always started by you.")
        AppSwitch {
            checked: AppSettings.values.aiOfflineOnly
            onToggled: AppSettings.set("aiOfflineOnly", checked)
        }
    }

    Section { title: qsTr("Speech to text"); Layout.topMargin: 22 }
    SettingRow {
        title: qsTr("Speech engine")
        description: qsTr("Automatic uses the OpenThai model for Thai (when it is installed) and whisper.cpp for every other language.")
        AppComboBox {
            objectName: "speechEngine"
            current: AppSettings.values.aiSpeechEngine
            options: [ { value: "auto", label: qsTr("Automatic") }, { value: "openthai", label: qsTr("OpenThai (Thai only)") }, { value: "whisper", label: "whisper.cpp" } ]
            onPicked: value => AppSettings.set("aiSpeechEngine", value)
        }
    }
    SettingRow {
        title: qsTr("OpenThai 2.0 ASR model")
        description: Ai.openThaiStatus === "notBuilt" ? qsTr("This build has no ONNX Runtime, so the model cannot run.")
                     : Ai.openThaiStatus === "available" ? qsTr("Installed. It runs on this computer; nothing is uploaded.")
                     : qsTr("Not installed. Press Download below (%1), or put the files of huggingface.co/iapp/OpenThai2.0-ASR-Streaming-ONNX (encoder.onnx, encoder.onnx.data, decoder_joint.onnx, tokens.txt) in %2").arg(Ai.openThaiInstaller.sizeText).arg(Ai.openThaiFolder)
        StatusBadge { kind: Ai.openThaiStatus === "available" ? "available" : Ai.openThaiStatus === "modelRequired" ? "modelRequired" : "unsupported" }
    }
    SettingRow {
        visible: Ai.openThaiStatus === "modelRequired" || Ai.openThaiInstaller.busy
        title: qsTr("Download the OpenThai model")
        description: Ai.openThaiInstaller.busy ? Ai.openThaiInstaller.message
                     : Ai.openThaiInstaller.message !== "" ? Ai.openThaiInstaller.message
                     : qsTr("From huggingface.co/iapp/OpenThai2.0-ASR-Streaming-ONNX, %1. It starts only when you press the button; each file is checked with its SHA-256.").arg(Ai.openThaiInstaller.sizeText)
        RowLayout {
            spacing: 10
            Rectangle {
                visible: Ai.openThaiInstaller.busy
                Layout.preferredWidth: 120
                height: 6
                radius: 3
                color: Theme.surfaceHover
                Rectangle { width: parent.width * Ai.openThaiInstaller.progress; height: parent.height; radius: 3; color: Theme.accent }
            }
            AppText { visible: Ai.openThaiInstaller.busy; text: Math.round(Ai.openThaiInstaller.progress * 100) + "%"; font.pixelSize: Theme.fontXs; color: Theme.textSecondary }
            AppButton {
                objectName: "openThaiDownload"
                visible: !Ai.openThaiInstaller.busy
                variant: "primary"
                iconName: "import"
                text: qsTr("Download")
                onClicked: { const e = Ai.openThaiInstaller.download(); if (e !== "") App.toast(e, "warning") }
            }
            AppButton { visible: Ai.openThaiInstaller.busy; variant: "ghost"; text: qsTr("Cancel"); onClicked: Ai.openThaiInstaller.cancel() }
        }
    }
    SettingRow {
        title: qsTr("OpenThai model folder")
        description: Ai.openThaiFolder
        AppButton { text: qsTr("Open folder"); onClicked: SysInfo.openFolder(Ai.openThaiFolder) }
    }

    Section {
        title: qsTr("Connect Claude Code / Codex (MCP)")
        note: qsTr("Lets an AI coding assistant on this computer work on the open project: list media and clips, edit the timeline, run the AI helpers. Every edit is one undo step. It listens on this computer only and needs the private token below.")
        Layout.topMargin: 22
    }
    SettingRow {
        title: qsTr("Enable MCP server")
        description: Mcp.running ? qsTr("Running at %1").arg(Mcp.url) : (Mcp.error !== "" ? Mcp.error : qsTr("Off. Nothing is listening."))
        AppSwitch {
            objectName: "mcpSwitch"
            checked: AppSettings.values.mcpEnabled
            onToggled: AppSettings.set("mcpEnabled", checked)
        }
    }
    SettingRow {
        title: qsTr("Port")
        description: qsTr("Restart the server (switch it off and on) after changing it.")
        AppTextField {
            implicitWidth: 120
            text: String(AppSettings.values.mcpPort)
            inputMethodHints: Qt.ImhDigitsOnly
            onEditingFinished: AppSettings.set("mcpPort", parseInt(text))
        }
    }
    SettingRow {
        visible: Mcp.running
        title: qsTr("Claude Code")
        description: qsTr("Copy this command and run it once in a terminal. It contains your private token.")
        AppButton { text: qsTr("Copy command"); onClicked: Mcp.copyText(Mcp.claudeCodeCommand) }
    }
    SettingRow {
        visible: Mcp.running
        title: qsTr("Codex")
        description: qsTr("Copy this block into ~/.codex/config.toml, then set the token variable shown in its last line.")
        AppButton { text: qsTr("Copy config"); onClicked: Mcp.copyText(Mcp.codexConfig) }
    }
    SettingRow {
        visible: Mcp.running
        title: qsTr("New token")
        description: qsTr("The old token stops working at once; connect your assistant again.")
        AppButton { text: qsTr("Create new token"); onClicked: Mcp.regenerateToken() }
    }

    Section { title: qsTr("Feature status"); Layout.topMargin: 22 }

    Repeater {
        model: [
            { name: qsTr("Speech to text and auto captions"), desc: qsTr("OpenThai 2.0 for Thai, whisper.cpp for other languages"), key: "captions" },
            { name: qsTr("Silence detection"), desc: qsTr("Highlight and remove silent passages") },
            { name: qsTr("Smart cut suggestions"), desc: qsTr("Scene changes and audio activity") },
            { name: qsTr("Audio enhancement"), desc: qsTr("Spectral noise reduction (signal processing, no neural model)") },
            { name: qsTr("Object tracking"), desc: qsTr("Follow a subject across frames") },
            { name: qsTr("Natural language assistant"), desc: qsTr("Typed commands from plain language") }
        ]
        delegate: SettingRow {
            required property var modelData
            title: modelData.name
            description: modelData.desc
            StatusBadge {
                // captions: available when either engine can run (Thai: OpenThai or whisper.cpp; other languages: whisper.cpp)
                readonly property bool captionsOk: Ai.whisperStatus === "available" || Ai.openThaiStatus === "available"
                kind: modelData.key === "captions" ? (captionsOk ? "available" : Ai.whisperStatus === "modelRequired" ? "modelRequired" : "unsupported") : "available"
                label: modelData.key === "captions" && !captionsOk && Ai.whisperStatus === "engineMissing" ? qsTr("Engine missing") : ""
            }
        }
    }

    Section { title: qsTr("Model manager"); Layout.topMargin: 22 }

    ModelManagerView {
        Layout.fillWidth: true
        Layout.topMargin: 8
    }
}
