import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

ColumnLayout {
    spacing: 0

    Section { title: qsTr("New projects") }

    SettingRow {
        title: qsTr("Default frame rate")
        description: qsTr("Pre-selected in the New Project dialog.")
        AppComboBox {
            current: AppSettings.values.defaultFps
            options: [
                { value: "23.976", label: "23.976 fps" }, { value: "24", label: "24 fps" }, { value: "25", label: "25 fps" },
                { value: "29.97", label: "29.97 fps" }, { value: "30", label: "30 fps" }, { value: "50", label: "50 fps" },
                { value: "59.94", label: "59.94 fps" }, { value: "60", label: "60 fps" }
            ]
            onPicked: value => AppSettings.set("defaultFps", value)
        }
    }
    PathSetting {
        title: qsTr("Default project location")
        settingKey: "defaultProjectLocation"
        defaultPath: Projects.defaultLocation("")
        Layout.fillWidth: true
    }

    SettingRow {
        title: qsTr("Copy imported media into the project")
        description: qsTr("Imported files are copied to the \"medias\" folder next to the project file, so the project keeps working when the originals move.")
        AppSwitch {
            checked: AppSettings.values.copyMediaToProject
            onToggled: AppSettings.set("copyMediaToProject", checked)
        }
    }

    Section { title: qsTr("Timeline"); Layout.topMargin: 22 }

    SettingRow {
        title: qsTr("Snapping")
        description: qsTr("Clips and the playhead snap to edit points (active once clips can be placed).")
        AppSwitch {
            checked: AppSettings.values.snapping
            onToggled: AppSettings.set("snapping", checked)
        }
    }

    Section { title: qsTr("Auto save"); Layout.topMargin: 22 }

    SettingRow {
        title: qsTr("Save automatically")
        description: qsTr("Unsaved work is written to a recovery copy; your project file is never overwritten by auto save.")
        AppSwitch {
            checked: AppSettings.values.autoSaveEnabled
            onToggled: AppSettings.set("autoSaveEnabled", checked)
        }
    }
    SettingRow {
        title: qsTr("Auto save interval")
        dimmed: !AppSettings.values.autoSaveEnabled
        AppSlider {
            implicitWidth: 180
            enabled: AppSettings.values.autoSaveEnabled
            from: 1; to: 60; stepSize: 1
            value: AppSettings.values.autoSaveMinutes
            onMoved: AppSettings.set("autoSaveMinutes", Math.round(value))
        }
        AppText {
            Layout.preferredWidth: 70
            horizontalAlignment: Text.AlignRight
            text: AppSettings.values.autoSaveMinutes + " " + qsTr("min")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontMd
        }
    }
}
