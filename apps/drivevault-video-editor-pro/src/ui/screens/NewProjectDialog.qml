import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.Backend

// Project configuration: name, location, resolution, frame rate, audio sample rate.
AppDialog {
    id: control

    title: qsTr("New Project")
    subtitle: qsTr("Set up the basics. You can adjust most of these later.")
    implicitWidth: 600

    signal created()

    property string errorText: ""
    property string location: Projects.defaultLocation(AppSettings.values.defaultProjectLocation)
    property string resolutionKey: "1920x1080"
    property string frameRate: AppSettings.values.defaultFps
    property int sampleRate: 48000

    readonly property bool custom: resolutionKey === "custom"

    onAboutToShow: {
        errorText = ""
        nameField.text = qsTr("Untitled Project")
        location = Projects.defaultLocation(AppSettings.values.defaultProjectLocation)
        frameRate = AppSettings.values.defaultFps
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    function resolution() {
        if (custom)
            return [parseInt(customWidth.text), parseInt(customHeight.text)]
        const p = resolutionKey.split("x")
        return [parseInt(p[0]), parseInt(p[1])]
    }

    function submit() {
        const r = resolution()
        const err = Projects.createProject(nameField.text, location, r[0], r[1], frameRate, sampleRate)
        if (err !== "") {
            errorText = err
            return
        }
        close()
        created()
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose project location")
        currentFolder: "file:///" + control.location.replace(/\\/g, "/")
        onAccepted: control.location = Projects.toLocalPath(selectedFolder.toString())
    }

    // Name
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 6
        AppText { text: qsTr("Project name"); font.pixelSize: Theme.fontSm; font.weight: Font.Medium; color: Theme.textSecondary }
        AppTextField {
            id: nameField
            objectName: "npName"
            Layout.fillWidth: true
            maximumLength: 120
            invalid: control.errorText !== "" && Projects.validateName(text) !== ""
            onAccepted: control.submit()
            onTextChanged: control.errorText = ""
        }
    }

    // Location
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 6
        AppText { text: qsTr("Location"); font.pixelSize: Theme.fontSm; font.weight: Font.Medium; color: Theme.textSecondary }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            AppTextField {
                Layout.fillWidth: true
                text: control.location
                onTextEdited: control.location = text
            }
            AppButton {
                iconName: "folder"
                text: qsTr("Browse…")
                onClicked: folderDialog.open()
            }
        }
        AppText {
            Layout.fillWidth: true
            text: qsTr("A folder with the project name is created inside this location.")
            font.pixelSize: Theme.fontXs
            color: Theme.textDisabled
            wrapMode: Text.Wrap
        }
    }

    // Resolution / fps / audio
    GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 16
        rowSpacing: 6

        AppText { text: qsTr("Resolution"); font.pixelSize: Theme.fontSm; font.weight: Font.Medium; color: Theme.textSecondary }
        AppText { text: qsTr("Frame rate"); font.pixelSize: Theme.fontSm; font.weight: Font.Medium; color: Theme.textSecondary }

        AppComboBox {
            Layout.fillWidth: true
            current: control.resolutionKey
            options: [
                { value: "1920x1080", label: "1080p  ·  1920 × 1080" },
                { value: "1280x720", label: "720p  ·  1280 × 720" },
                { value: "2560x1440", label: "1440p  ·  2560 × 1440" },
                { value: "3840x2160", label: "4K UHD  ·  3840 × 2160" },
                { value: "1080x1920", label: qsTr("Vertical") + "  ·  1080 × 1920" },
                { value: "custom", label: qsTr("Custom") }
            ]
            onPicked: value => control.resolutionKey = value
        }
        AppComboBox {
            Layout.fillWidth: true
            current: control.frameRate
            options: [
                { value: "23.976", label: "23.976 fps" },
                { value: "24", label: "24 fps" },
                { value: "25", label: "25 fps (PAL)" },
                { value: "29.97", label: "29.97 fps (NTSC)" },
                { value: "30", label: "30 fps" },
                { value: "50", label: "50 fps" },
                { value: "59.94", label: "59.94 fps" },
                { value: "60", label: "60 fps" }
            ]
            onPicked: value => control.frameRate = value
        }

        RowLayout {
            visible: control.custom
            Layout.columnSpan: 2
            spacing: 8
            AppTextField { id: customWidth; implicitWidth: 100; text: "1920"; validator: IntValidator { bottom: 64; top: 16384 } }
            AppText { text: "×"; color: Theme.textSecondary; font.pixelSize: Theme.fontMd }
            AppTextField { id: customHeight; implicitWidth: 100; text: "1080"; validator: IntValidator { bottom: 64; top: 16384 } }
            AppText { text: qsTr("pixels (64 – 16384)"); color: Theme.textDisabled; font.pixelSize: Theme.fontXs }
        }

        AppText { text: qsTr("Audio sample rate"); font.pixelSize: Theme.fontSm; font.weight: Font.Medium; color: Theme.textSecondary; Layout.columnSpan: 2 }
        AppComboBox {
            Layout.columnSpan: 2
            Layout.preferredWidth: 240
            current: control.sampleRate
            options: [
                { value: 48000, label: "48 kHz" },
                { value: 44100, label: "44.1 kHz" },
                { value: 96000, label: "96 kHz" }
            ]
            onPicked: value => control.sampleRate = value
        }
    }

    // Inline error
    Rectangle {
        Layout.fillWidth: true
        visible: control.errorText !== ""
        implicitHeight: errorRow.implicitHeight + 20
        radius: Theme.radiusSmall + 2
        color: Qt.alpha(Theme.error, 0.12)
        border.color: Qt.alpha(Theme.error, 0.5)

        RowLayout {
            id: errorRow
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8
            Icon { name: "warning"; color: Theme.error; size: 16 }
            AppText {
                Layout.fillWidth: true
                text: control.errorText
                color: Theme.text
                font.pixelSize: Theme.fontSm
                wrapMode: Text.Wrap
            }
        }
    }

    footerContent: [
        Item { Layout.fillWidth: true },
        AppButton {
            variant: "ghost"
            text: qsTr("Cancel")
            onClicked: control.close()
        },
        AppButton {
            objectName: "npCreate"
            variant: "primary"
            iconName: "check"
            text: qsTr("Create Project")
            onClicked: control.submit()
        }
    ]
}
