import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

ColumnLayout {
    spacing: 0

    Section {
        title: qsTr("Playback")
        note: qsTr("Applies to the next time you press Play.")
    }

    SettingRow {
        title: qsTr("Output device")
        description: qsTr("The device used for preview playback. “System default” follows the Windows setting.")
        AppComboBox {
            implicitWidth: 280
            current: AppSettings.values.audioOutputDevice
            options: {
                const list = []
                for (const name of Playback.audioDevices)
                    list.push({ value: name, label: name === "default" ? qsTr("System default") : name })
                // A device that is no longer present is still shown so the choice is not lost silently.
                if (Playback.audioDevices.indexOf(AppSettings.values.audioOutputDevice) < 0)
                    list.push({ value: AppSettings.values.audioOutputDevice, label: AppSettings.values.audioOutputDevice + " (" + qsTr("not found") + ")" })
                return list
            }
            onPicked: value => AppSettings.set("audioOutputDevice", value)
        }
    }
    SettingRow {
        title: qsTr("Preview volume")
        description: qsTr("Affects playback in the editor only, not exported files.")
        AppSlider {
            implicitWidth: 180
            from: 0; to: 100; stepSize: 1
            value: AppSettings.values.previewVolume
            onMoved: AppSettings.set("previewVolume", Math.round(value))
        }
        AppText {
            Layout.preferredWidth: 40
            horizontalAlignment: Text.AlignRight
            text: AppSettings.values.previewVolume + "%"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontMd
        }
    }
    SettingRow {
        title: qsTr("Latency")
        description: qsTr("Lower values respond faster but can crackle on slower computers.")
        AppSlider {
            implicitWidth: 180
            from: 10; to: 500; stepSize: 10
            value: AppSettings.values.audioLatencyMs
            onMoved: AppSettings.set("audioLatencyMs", Math.round(value))
        }
        AppText {
            Layout.preferredWidth: 56
            horizontalAlignment: Text.AlignRight
            text: AppSettings.values.audioLatencyMs + " ms"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontMd
        }
    }
}
