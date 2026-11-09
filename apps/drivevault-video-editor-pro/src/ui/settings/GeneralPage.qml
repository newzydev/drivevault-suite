import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

ColumnLayout {
    id: page
    spacing: 0

    signal resetLayoutsRequested()

    Section { title: qsTr("Language and theme") }

    SettingRow {
        title: qsTr("Language")
        description: qsTr("Applies immediately. “Automatic” follows the Windows display language.")
        AppComboBox {
            current: AppSettings.values.language
            options: [
                { value: "auto", label: qsTr("Automatic") },
                { value: "th-TH", label: "ไทย" },
                { value: "en-US", label: "English" }
            ]
            onPicked: value => AppSettings.set("language", value)
        }
    }
    SettingRow {
        title: qsTr("Theme")
        description: qsTr("Premium Dark is the default; Light is available for bright environments.")
        AppComboBox {
            current: AppSettings.values.theme
            options: [ { value: "dark", label: qsTr("Dark") }, { value: "light", label: qsTr("Light") } ]
            onPicked: value => AppSettings.set("theme", value)
        }
    }

    Section { title: qsTr("Startup"); Layout.topMargin: 22 }

    SettingRow {
        title: qsTr("When the application starts")
        AppComboBox {
            current: AppSettings.values.startupBehavior
            options: [ { value: "home", label: qsTr("Show the Home screen") },
                       { value: "lastProject", label: qsTr("Reopen the last project") } ]
            onPicked: value => AppSettings.set("startupBehavior", value)
        }
    }
    SettingRow {
        title: qsTr("Recent projects to remember")
        description: qsTr("The list is shown on the Home screen.")
        AppSlider {
            implicitWidth: 180
            from: 1; to: 30; stepSize: 1
            value: AppSettings.values.recentProjectsLimit
            onMoved: AppSettings.set("recentProjectsLimit", Math.round(value))
        }
        AppText {
            Layout.preferredWidth: 28
            horizontalAlignment: Text.AlignRight
            text: AppSettings.values.recentProjectsLimit
            color: Theme.textSecondary
            font.pixelSize: Theme.fontMd
        }
        AppButton { text: qsTr("Clear list"); enabled: Recents.count > 0; onClicked: Recents.clear() }
    }

    Section { title: qsTr("Reset"); Layout.topMargin: 22 }

    SettingRow {
        title: qsTr("Reset all settings")
        description: qsTr("Restores every preference to its default. Your projects are not touched.")
        AppButton { variant: "danger"; iconName: "refresh"; text: qsTr("Reset…"); onClicked: confirmReset.open() }
    }

    ConfirmDialog {
        id: confirmReset
        title: qsTr("Reset all settings?")
        message: qsTr("All preferences return to their defaults. Projects, media and logs are not affected.")
        primaryText: qsTr("Reset")
        dangerPrimary: true
        onPrimary: { AppSettings.resetToDefaults(); App.toast(qsTr("Settings were reset to defaults."), "success") }
    }
}
