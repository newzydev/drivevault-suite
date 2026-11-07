import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

ColumnLayout {
    spacing: 0

    RowLayout {
        Layout.fillWidth: true
        Layout.bottomMargin: 18
        spacing: 18
        Logo { size: 64 }
        ColumnLayout {
            spacing: 2
            Text { text: SysInfo.productName; font.pixelSize: Theme.fontXl; font.weight: Font.Bold; color: Theme.text }
            Text { text: qsTr("Professional Power. Beautiful Simplicity."); font.pixelSize: Theme.fontMd; color: Theme.accentText }
        }
    }

    Section { title: qsTr("Version") }

    Repeater {
        model: [
            { k: qsTr("Version"), v: SysInfo.version },
            { k: qsTr("Build number"), v: SysInfo.buildNumber },
            { k: qsTr("Operating system"), v: SysInfo.operatingSystem },
            { k: qsTr("Architecture"), v: SysInfo.architecture },
            { k: qsTr("Qt"), v: SysInfo.qtVersion },
            { k: qsTr("Compiler"), v: SysInfo.compiler }
        ]
        delegate: SettingRow {
            required property var modelData
            title: modelData.k
            Text { text: modelData.v; color: Theme.textSecondary; font.pixelSize: Theme.fontMd; elide: Text.ElideRight; Layout.maximumWidth: 380 }
        }
    }

    Section { title: qsTr("Folders"); Layout.topMargin: 22 }

    SettingRow {
        title: qsTr("Settings and recent projects")
        description: SysInfo.dataDirectory
        AppButton { iconName: "folder"; text: qsTr("Open"); onClicked: SysInfo.openFolder(SysInfo.dataDirectory) }
    }
    SettingRow {
        title: qsTr("Log files")
        description: SysInfo.logDirectory
        AppButton { iconName: "folder"; text: qsTr("Open"); onClicked: SysInfo.openFolder(SysInfo.logDirectory) }
    }

    Section { title: qsTr("License and third-party notices"); Layout.topMargin: 22 }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: 8
        text: qsTr("DriveVault Video Editor Pro is proprietary software. All rights reserved.")
        font.pixelSize: Theme.fontMd
        color: Theme.text
        wrapMode: Text.Wrap
    }

    Repeater {
        model: [
            { name: "Qt 6.8", license: "LGPL v3 - " + qsTr("dynamically linked; the Qt libraries can be replaced by the user.") },
            { name: "FFmpeg 8.1", license: "LGPL v3 - " + qsTr("shared build without GPL or non-free parts, dynamically linked.") },
            { name: "whisper.cpp / ggml", license: "MIT - " + qsTr("speech recognition engine (separate program).") },
            { name: "OpenAI Whisper models", license: "MIT - " + qsTr("not installed; downloaded or imported by you.") },
            { name: "Inter", license: "SIL Open Font License 1.1" },
            { name: "Noto Sans Thai", license: "SIL Open Font License 1.1" },
            { name: "Microsoft Visual C++ runtime", license: qsTr("Redistributable under the Visual Studio license terms.") }
        ]
        delegate: SettingRow {
            required property var modelData
            title: modelData.name
            description: modelData.license
        }
    }

    RowLayout {
        Layout.topMargin: 12
        spacing: 8
        AppButton {
            objectName: "aboutNotices"
            iconName: "file"
            text: qsTr("Third-Party Notices")
            onClicked: { const p = SysInfo.documentPath("THIRD-PARTY-NOTICES.txt"); if (p === "" || !SysInfo.openFile(p)) App.toast(qsTr("This document was not found next to the application."), "warning") }
        }
        AppButton {
            iconName: "folder"
            text: qsTr("Open licenses folder")
            onClicked: { const p = SysInfo.documentPath("LICENSE.txt"); if (p !== "") SysInfo.showInFolder(p); else App.toast(qsTr("This document was not found next to the application."), "warning") }
        }
        Item { Layout.fillWidth: true }
    }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: 12
        text: qsTr("Full texts are installed with the application in the licenses folder.")
        font.pixelSize: Theme.fontSm
        color: Theme.textDisabled
        wrapMode: Text.Wrap
    }
}
