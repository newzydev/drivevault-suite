import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.Backend

// A setting that stores a folder path; empty value means "use the default".
SettingRow {
    id: root

    property string settingKey
    property string defaultPath
    readonly property string current: AppSettings.values[settingKey] ? AppSettings.values[settingKey] : ""

    description: (current !== "" ? current : defaultPath)
                 + (current === "" ? "  (" + qsTr("default") + ")" : "")

    FolderDialog {
        id: dialog
        title: root.title
        onAccepted: AppSettings.set(root.settingKey, Projects.toLocalPath(selectedFolder.toString()))
    }

    AppButton { iconName: "folder"; text: qsTr("Browse…"); onClicked: dialog.open() }
    IconButton {
        iconName: "refresh"
        subtle: false
        enabled: root.current !== ""
        tip: qsTr("Restore default")
        onClicked: AppSettings.set(root.settingKey, "")
    }
}
