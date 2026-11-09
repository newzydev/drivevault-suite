import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

ColumnLayout {
    id: page
    spacing: 0

    signal resetLayoutsRequested()

    Section { title: qsTr("Colour") }

    SettingRow {
        title: qsTr("Accent colour")
        description: qsTr("Used for selections, focus and primary buttons.")

        Repeater {
            model: Theme.accentPresets
            delegate: AbstractButton {
                id: swatch
                required property var modelData
                readonly property bool current: AppSettings.values.accentColor.toLowerCase() === modelData.color.toLowerCase()
                implicitWidth: 28
                implicitHeight: 28
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.name: modelData.name
                onClicked: AppSettings.set("accentColor", modelData.color)

                contentItem: Item {}
                background: Rectangle {
                    radius: 14
                    color: swatch.modelData.color
                    border.width: swatch.current || swatch.visualFocus ? 3 : (swatch.hovered ? 2 : 0)
                    border.color: swatch.current ? Theme.text : Theme.textSecondary
                    Icon {
                        visible: swatch.current
                        anchors.centerIn: parent
                        name: "check"
                        size: 14
                        strokeWidth: 2.6
                        color: Theme.luminance(Qt.color(swatch.modelData.color)) > 0.55 ? "#14161B" : "#FFFFFF"
                    }
                }
            }
        }
        AppTextField {
            id: hex
            implicitWidth: 100
            text: AppSettings.values.accentColor
            maximumLength: 7
            invalid: !/^#[0-9A-Fa-f]{6}$/.test(text)
            onEditingFinished: {
                if (!invalid)
                    AppSettings.set("accentColor", text)
                else
                    text = Qt.binding(() => AppSettings.values.accentColor)
            }
        }
    }

    Section { title: qsTr("Scaling and text"); Layout.topMargin: 22 }

    SettingRow {
        title: qsTr("Interface scale")
        description: qsTr("Scales the whole interface for high-DPI or distant displays. Requires a restart.")
        AppComboBox {
            current: Math.round(AppSettings.values.uiScale * 100)
            options: [ { value: 75, label: "75%" }, { value: 90, label: "90%" }, { value: 100, label: "100%" },
                       { value: 110, label: "110%" }, { value: 125, label: "125%" }, { value: 150, label: "150%" },
                       { value: 175, label: "175%" }, { value: 200, label: "200%" } ]
            onPicked: value => AppSettings.set("uiScale", value / 100)
        }
    }
    SettingRow {
        title: qsTr("Text size")
        description: qsTr("Makes text smaller or larger. Applies immediately.")
        AppSlider {
            implicitWidth: 180
            from: -2; to: 4; stepSize: 1
            snapMode: Slider.SnapAlways
            value: AppSettings.values.fontSizeOffset
            onMoved: AppSettings.set("fontSizeOffset", Math.round(value))
        }
        AppText {
            Layout.preferredWidth: 36
            horizontalAlignment: Text.AlignRight
            text: (AppSettings.values.fontSizeOffset > 0 ? "+" : "") + AppSettings.values.fontSizeOffset
            color: Theme.textSecondary
            font.pixelSize: Theme.fontMd
        }
    }

    Section { title: qsTr("Motion"); Layout.topMargin: 22 }

    SettingRow {
        title: qsTr("Interface animations")
        description: qsTr("Hover, fade and panel transitions. Turn off for the most direct response.")
        AppSwitch {
            checked: AppSettings.values.animations
            onToggled: AppSettings.set("animations", checked)
        }
    }

    Section { title: qsTr("Workspace"); Layout.topMargin: 22 }

    SettingRow {
        title: qsTr("Panel layout")
        description: qsTr("Panel sizes are remembered per workspace. Reset them to the built-in layouts.")
        AppButton { iconName: "panelLeft"; text: qsTr("Reset layouts"); onClicked: page.resetLayoutsRequested() }
    }
}
