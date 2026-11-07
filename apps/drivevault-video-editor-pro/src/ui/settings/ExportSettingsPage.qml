import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

ColumnLayout {
    spacing: 0

    Section {
        title: qsTr("Defaults for new exports")
        note: qsTr("The same values are used in the Export workspace.")
    }

    SettingRow {
        title: qsTr("Format")
        AppComboBox {
            current: AppSettings.values.exportFormat
            options: [ { value: "mp4", label: "MP4 (H.264 + AAC)" }, { value: "mov", label: "MOV" }, { value: "mkv", label: "MKV" } ]
            onPicked: value => AppSettings.set("exportFormat", value)
        }
    }
    SettingRow {
        title: qsTr("Preset")
        AppComboBox {
            current: AppSettings.values.exportPreset
            options: [ { value: "720p", label: "720p" }, { value: "1080p", label: "1080p" }, { value: "1440p", label: "1440p" },
                       { value: "4k", label: "4K UHD" }, { value: "custom", label: qsTr("Custom") } ]
            onPicked: value => AppSettings.set("exportPreset", value)
        }
    }
    SettingRow {
        title: qsTr("Quality")
        AppSlider {
            implicitWidth: 180
            from: 0; to: 100; stepSize: 1
            value: AppSettings.values.exportQuality
            onMoved: AppSettings.set("exportQuality", Math.round(value))
        }
        Text {
            Layout.preferredWidth: 36
            horizontalAlignment: Text.AlignRight
            text: AppSettings.values.exportQuality
            color: Theme.textSecondary
            font.pixelSize: Theme.fontMd
        }
    }
    SettingRow {
        title: qsTr("Audio bitrate")
        AppComboBox {
            current: AppSettings.values.exportAudioBitrate
            options: [ { value: 128, label: "128 kbps" }, { value: 192, label: "192 kbps" },
                       { value: 256, label: "256 kbps" }, { value: 320, label: "320 kbps" } ]
            onPicked: value => AppSettings.set("exportAudioBitrate", value)
        }
    }
    PathSetting {
        Layout.fillWidth: true
        title: qsTr("Output folder")
        settingKey: "exportFolder"
        defaultPath: SysInfo.defaultExportDirectory
    }
}
