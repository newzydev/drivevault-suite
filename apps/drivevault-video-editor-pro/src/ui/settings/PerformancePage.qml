import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

ColumnLayout {
    spacing: 0

    Section {
        title: qsTr("Playback")
    }

    SettingRow {
        title: qsTr("Hardware video decoding (experimental)")
        description: qsTr("Decode very large pictures (4K and above) on the GPU through Direct3D 11. On the test computer this was slower than the CPU because every picture is copied back to memory; it mainly lowers the CPU load. Falls back to the CPU automatically.")
        AppSwitch {
            objectName: "hwDecodeSwitch"
            checked: AppSettings.values.hardwareAcceleration
            onToggled: AppSettings.set("hardwareAcceleration", checked)
        }
    }
    SettingRow {
        title: qsTr("Preview quality")
        description: qsTr("Lower quality makes playback lighter on slower computers. Also available in the Program Monitor.")
        AppComboBox {
            current: AppSettings.values.previewQuality
            options: [ { value: "full", label: qsTr("Full") }, { value: "half", label: qsTr("Half") },
                       { value: "quarter", label: qsTr("Quarter") } ]
            onPicked: value => AppSettings.set("previewQuality", value)
        }
    }

    Section { title: qsTr("Hardware detected"); Layout.topMargin: 22; note: qsTr("Found by really opening each device, not by guessing from the GPU name.") }

    Repeater {
        model: Playback.hardwareDecoders()
        delegate: SettingRow {
            required property var modelData
            title: qsTr("Decoder: %1").arg(modelData.label)
            StatusBadge { kind: modelData.available ? "available" : "unsupported"; label: modelData.available ? "" : qsTr("Not available") }
        }
    }
    Repeater {
        model: ExportJob.encoders
        delegate: SettingRow {
            required property var modelData
            title: qsTr("Encoder: %1").arg(modelData.label)
            description: modelData.hardware ? qsTr("Used automatically for export before software encoders.") : qsTr("Software fallback")
            StatusBadge { kind: "available" }
        }
    }
    Component.onCompleted: ExportJob.detectEncoders()

    Section { title: qsTr("Proxy editing"); Layout.topMargin: 22; note: qsTr("Small stand-ins for large videos make the preview smooth. Export always uses the original files.") }

    SettingRow {
        title: qsTr("Use proxies in the preview")
        description: qsTr("When a proxy exists for a clip, the preview reads it instead of the original.")
        AppSwitch {
            objectName: "useProxiesSwitch"
            checked: AppSettings.values.useProxies
            onToggled: AppSettings.set("useProxies", checked)
        }
    }
    SettingRow {
        id: proxyRow
        title: qsTr("Proxies")
        description: Proxies.busy ? Proxies.message + "  " + Math.round(Proxies.progress * 100) + " %"
                     : qsTr("%n proxy file(s), %1 MB", "", Proxies.proxyCount).arg((Proxies.proxyBytes() / 1048576).toFixed(1))
        AppButton {
            objectName: "proxyCreate"
            text: qsTr("Create for large videos")
            iconName: "film"
            enabled: !Proxies.busy && Projects.isOpen
            onClicked: {
                const n = Proxies.createMissingProxies()
                App.toast(n > 0 ? qsTr("Building %n proxy file(s)…", "", n) : qsTr("No video in this project needs a proxy."), n > 0 ? "info" : "warning")
            }
        }
        AppButton { visible: Proxies.busy; variant: "ghost"; text: qsTr("Cancel"); onClicked: Proxies.cancel() }
        AppButton {
            text: qsTr("Delete proxies")
            iconName: "trash"
            enabled: !Proxies.busy && Proxies.proxyCount > 0
            onClicked: { Proxies.clearProxies(); App.toast(qsTr("Proxies deleted."), "success") }
        }
    }

    Section { title: qsTr("Playback statistics"); Layout.topMargin: 22; note: qsTr("Live numbers of the preview (play something to see them).") }

    SettingRow { title: qsTr("Playback speed"); description: Playback.playbackFps.toFixed(1) + " fps  ·  " + qsTr("%n dropped frame(s)", "", Playback.droppedFrames) }
    SettingRow { title: qsTr("Time to render a frame"); description: Playback.renderMs.toFixed(1) + " ms" }
    SettingRow { title: qsTr("Frame cache"); description: qsTr("%1 % of frames served from the cache").arg(Math.round(Playback.cacheHitRate * 100)) }
    SettingRow {
        title: qsTr("Decoding")
        description: (Playback.hardwareDecoding ? qsTr("GPU decoding is active") : qsTr("CPU decoding")) + "  ·  " + (Playback.usingProxies ? qsTr("proxies in use") : qsTr("original files"))
    }

    Section { title: qsTr("Cache and memory"); Layout.topMargin: 22 }

    PathSetting {
        title: qsTr("Cache location")
        settingKey: "cacheLocation"
        defaultPath: SysInfo.defaultCacheDirectory
        Layout.fillWidth: true
    }
    SettingRow {
        title: qsTr("Cache size limit")
        description: qsTr("Thumbnails and waveforms are trimmed (oldest first) to this size when the application starts.")
        AppComboBox {
            current: AppSettings.values.cacheLimitMb
            options: [ { value: 1024, label: "1 GB" }, { value: 2048, label: "2 GB" }, { value: 4096, label: "4 GB" },
                       { value: 8192, label: "8 GB" }, { value: 16384, label: "16 GB" }, { value: 32768, label: "32 GB" } ]
            onPicked: value => AppSettings.set("cacheLimitMb", value)
        }
    }
    SettingRow {
        id: cacheRow
        property real bytes: Projects.media.cacheSizeBytes()
        title: qsTr("Cache in use")
        description: (bytes / 1048576).toFixed(1) + " MB"
        AppButton {
            iconName: "trash"
            text: qsTr("Clear cache")
            onClicked: { Projects.media.clearCache(); cacheRow.bytes = Projects.media.cacheSizeBytes(); App.toast(qsTr("Cache cleared. Thumbnails and waveforms are rebuilt as needed."), "success") }
        }
    }
    SettingRow {
        title: qsTr("Memory limit")
        description: qsTr("A quarter of this (at most 1 GB) holds rendered preview frames, so scrubbing and replaying are instant.")
        AppComboBox {
            current: AppSettings.values.memoryLimitMb
            options: [ { value: 1024, label: "1 GB" }, { value: 2048, label: "2 GB" }, { value: 4096, label: "4 GB" },
                       { value: 8192, label: "8 GB" }, { value: 16384, label: "16 GB" } ]
            onPicked: value => AppSettings.set("memoryLimitMb", value)
        }
    }
}
