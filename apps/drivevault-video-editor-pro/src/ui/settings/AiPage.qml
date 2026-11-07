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

    Section { title: qsTr("Feature status"); Layout.topMargin: 22 }

    Repeater {
        model: [
            { name: qsTr("Speech to text and auto captions"), desc: qsTr("whisper.cpp — Thai and English"), key: "captions" },
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
                kind: modelData.key === "captions" ? (Ai.whisperStatus === "available" ? "available" : Ai.whisperStatus === "modelRequired" ? "modelRequired" : "unsupported") : "available"
                label: modelData.key === "captions" && Ai.whisperStatus === "engineMissing" ? qsTr("Engine missing") : ""
            }
        }
    }

    Section { title: qsTr("Model manager"); Layout.topMargin: 22 }

    ModelManagerView {
        Layout.fillWidth: true
        Layout.topMargin: 8
    }
}
