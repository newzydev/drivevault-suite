import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Browser.Backend

Rectangle {
    id: settings
    radius: 17
    color: Theme.surface
    clip: true

    signal closeRequested()

    property int category: 0
    property string note: ""
    readonly property var categories: [
        { title: "set_general", icon: "globe" },
        { title: "set_privacy", icon: "lock" },
        { title: "set_adblock", icon: "shield" },
        { title: "set_about", icon: "info" }
    ]

    function selectCategory(index) {
        category = index
        settingsScroll.contentY = 0
    }

    component SectionTitle: Text {
        Layout.fillWidth: true
        topPadding: 10
        color: Theme.accent
        textFormat: Text.PlainText
        font.pixelSize: 13
        font.weight: Font.DemiBold
    }

    component SettingRow: RowLayout {
        id: settingRow
        property string label: ""
        property string description: ""
        Layout.fillWidth: true
        Layout.minimumHeight: 54
        spacing: 16
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                Layout.fillWidth: true
                text: settingRow.label
                color: Theme.text
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            Text {
                visible: text !== ""
                Layout.fillWidth: true
                text: settingRow.description
                color: Theme.textSecondary
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }

    component Rule: Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.borderSubtle
    }

component Choice: ComboBox {
        id: cb
        property string current: ""
        signal picked(string id)
        textRole: "name"
        implicitHeight: 34
        Layout.preferredWidth: 230
        currentIndex: { for (let i = 0; i < model.length; ++i) if (model[i].id === current) return i; return 0 }
        onActivated: picked(model[currentIndex].id)
        font.pixelSize: 13
        contentItem: Text { leftPadding: 10; text: cb.displayText; color: Theme.text; font: cb.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle { radius: Theme.radiusSmall; color: Theme.backgroundAlt; border.width: 1; border.color: Theme.border }
        popup.background: Rectangle { color: Theme.surface; border.width: 1; border.color: Theme.border; radius: Theme.radiusSmall }
        delegate: ItemDelegate {
            required property var modelData
            required property int index
            width: cb.width
            highlighted: cb.highlightedIndex === index
            contentItem: Text { text: modelData.name; color: Theme.text; font: cb.font; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: highlighted ? Theme.surfaceHover : "transparent" }
        }
    }
    component Toggle: CheckBox {
        id: ck
        font.pixelSize: 13
        Layout.fillWidth: true
        spacing: 8
        indicator: Rectangle {
            x: ck.leftPadding
            y: (ck.height - height) / 2
            width: 18; height: 18; radius: 5
            color: ck.checked ? Theme.accent : "transparent"
            border.width: 1
            border.color: ck.checked ? Theme.accent : Theme.border
            LineIcon { anchors.centerIn: parent; visible: ck.checked; kind: "check"; size: 13; color: Theme.readableOn(Theme.accent) }
        }
        contentItem: Text { leftPadding: ck.indicator.width + ck.spacing; text: ck.text; color: Theme.text; font: ck.font; wrapMode: Text.WordWrap; verticalAlignment: Text.AlignVCenter }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: settings.width < 700 ? 152 : 220
            color: Theme.backgroundAlt
            radius: settings.radius
            Rectangle {
                anchors.right: parent.right
                width: settings.radius
                height: parent.height
                color: parent.color
            }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 5
                Text {
                    Layout.leftMargin: 8
                    Layout.topMargin: 8
                    Layout.bottomMargin: 14
                    text: Tr.t("settings")
                    color: Theme.text
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: settings.categories
                    delegate: AbstractButton {
                        id: categoryButton
                        required property var modelData
                        required property int index
                        readonly property bool selected: settings.category === index
                        Layout.fillWidth: true
                        implicitHeight: 40
                        hoverEnabled: true
                        text: Tr.t(modelData.title)
                        leftPadding: 12
                        rightPadding: 8
                        Accessible.name: text
                        onClicked: settings.selectCategory(index)
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        background: Rectangle {
                            radius: 9
                            color: categoryButton.selected ? Qt.alpha(Theme.accent, 0.16)
                                : categoryButton.hovered ? Theme.surfaceHover : "transparent"
                            border.width: categoryButton.visualFocus ? 1 : 0
                            border.color: Theme.accent
                        }
                        contentItem: RowLayout {
                            spacing: 9
                            LineIcon { kind: categoryButton.modelData.icon; color: categoryButton.selected ? Theme.accent : Theme.textSecondary }
                            Text {
                                Layout.fillWidth: true
                                text: categoryButton.text
                                color: categoryButton.selected ? Theme.text : Theme.textSecondary
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                Text {
                    Layout.leftMargin: 8
                    Layout.bottomMargin: 8
                    text: "v" + Browser.version
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 66
                Layout.leftMargin: 24
                Layout.rightMargin: 12
                Text {
                    Layout.fillWidth: true
                    text: Tr.t(settings.categories[settings.category].title)
                    color: Theme.text
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }
                IconButton { kind: "close"; tip: Tr.t("close"); onClicked: settings.closeRequested() }
            }

            Flickable {
                id: settingsScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: options.implicitHeight + 32
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                ColumnLayout {
                    id: options
                    x: 24
                    width: settingsScroll.width - 48
                    spacing: 9

                    ColumnLayout {
                        visible: settings.category === 0
                        Layout.fillWidth: true
                        spacing: 5

                        SettingRow {
                            label: Tr.t("set_language")
                            Choice {
                                objectName: "setLanguage"
                                model: [{ id: "th", name: "ไทย" }, { id: "en", name: "English" }]
                                current: Browser.settings.language
                                onPicked: id => Browser.settings.language = id
                            }
                        }
                        Rule {}
                        SettingRow {
                            label: Tr.t("set_home")
                            description: Tr.t("set_home_hint")
                            TextField {
                                Layout.preferredWidth: 230
                                implicitHeight: 34
                                color: Theme.text
                                leftPadding: 10
                                selectByMouse: true
                                text: Browser.settings.homePage
                                onEditingFinished: Browser.settings.homePage = text
                                background: Rectangle {
                                    radius: Theme.radiusSmall
                                    color: Theme.backgroundAlt
                                    border.color: parent.activeFocus ? Theme.accent : Theme.border
                                }
                            }
                        }
                        Rule {}
                        SettingRow {
                            label: Tr.t("set_startup")
                            Choice {
                                model: [{ id: "newtab", name: Tr.t("set_startup_newtab") }, { id: "restore", name: Tr.t("set_startup_restore") }]
                                current: Browser.settings.startup
                                onPicked: id => Browser.settings.startup = id
                            }
                        }
                        Rule {}
                        SettingRow {
                            label: Tr.t("set_search")
                            Choice {
                                model: Browser.settings.engines()
                                current: Browser.settings.searchEngine
                                onPicked: id => Browser.settings.searchEngine = id
                            }
                        }
                        Rule {}
                        Toggle {
                            text: Tr.t("showBar")
                            checked: Browser.settings.showBookmarksBar
                            onToggled: Browser.settings.showBookmarksBar = checked
                        }
                    }

                    ColumnLayout {
                        visible: settings.category === 1
                        Layout.fillWidth: true
                        spacing: 8
                        Toggle {
                            text: Tr.t("set_notifications")
                            checked: Browser.settings.blockNotifications
                            onToggled: Browser.settings.blockNotifications = checked
                        }

                        SectionTitle { text: Tr.t("set_clear") }
                        BButton {
                            text: Tr.t("set_clear_button")
                            onClicked: { Browser.clearBrowsingData(true, true); settings.note = Tr.t("set_cleared") }
                        }
                        Text { Layout.fillWidth: true; text: Tr.t("set_clear_note"); color: Theme.textSecondary; font.pixelSize: 11; wrapMode: Text.WordWrap }
                        Text { visible: settings.note !== ""; text: settings.note; color: Theme.success; font.pixelSize: 12 }
                    }

                    ColumnLayout {
                        visible: settings.category === 2
                        Layout.fillWidth: true
                        spacing: 8
                        Toggle {
                            objectName: "adblockSwitch"
                            text: Tr.t("ab_on")
                            checked: Browser.adblock.enabled
                            onToggled: Browser.adblock.enabled = checked
                        }
                        Toggle {
                            objectName: "cosmeticSwitch"
                            text: Tr.t("ab_cosmetic")
                            enabled: Browser.adblock.enabled
                            checked: Browser.settings.cosmeticEnabled
                            onToggled: Browser.settings.cosmeticEnabled = checked
                        }
                        Toggle {
                            objectName: "defendSwitch"
                            text: Tr.t("ab_defend")
                            enabled: Browser.adblock.enabled
                            checked: Browser.settings.antiDetectEnabled
                            onToggled: Browser.settings.antiDetectEnabled = checked
                        }
                        Text { Layout.fillWidth: true; text: Tr.t("ab_defend_note"); color: Theme.textSecondary; font.pixelSize: 11; wrapMode: Text.WordWrap }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            color: Browser.adblock.lists.totalRules > 0 ? Theme.textSecondary : Theme.warning
                            text: {
                                Browser.adblock.tick
                                const l = Browser.adblock.lists
                                return l.phase === "loading" ? Tr.t("ab_status_loading")
                                     : l.totalRules > 0 ? Tr.t("ab_status_rules", l.totalRules)
                                     : Tr.t("ab_status_none")
                            }
                        }
                        Text { text: Tr.t("ab_lists"); color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                        Repeater {
                            model: { Browser.adblock.tick; return Browser.adblock.listStatus() }
                            delegate: Toggle {
                                id: listToggle
                                required property var modelData
                                text: modelData.name + "  -  " + Tr.t("ab_cat_" + modelData.category)
                                      + "  (" + (modelData.present ? Tr.t("ab_rules_page", modelData.rules, modelData.cosmetic + modelData.scriptlets) + (modelData.date ? ", " + Tr.t("ab_date", modelData.date.substring(0, 10)) : "") : Tr.t("ab_missing")) + ")"
                                checked: Browser.adblock.listEnabled(modelData.id)
                                onToggled: Browser.adblock.setListEnabled(modelData.id, checked)
                            }
                        }
                        BButton {
                            objectName: "updateLists"
                            text: Tr.t("ab_update")
                            enabled: !Browser.adblock.lists.updating
                            onClicked: Browser.adblock.updateLists()
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            color: Browser.adblock.lists.failedCount > 0 ? Theme.errorSoft : Theme.success
                            visible: text !== ""
                            text: {
                                Browser.adblock.tick
                                const l = Browser.adblock.lists
                                if (l.phase === "updating") return Tr.t("ab_updating", l.current)
                                if (l.phase === "nothingOn") return Tr.t("ab_nothing_on")
                                if (l.updatedCount + l.failedCount > 0) return Tr.t("ab_update_result", l.updatedCount, l.failedCount) + (l.failures !== "" ? "  " + l.failures : "")
                                return ""
                            }
                        }
                        Text { Layout.fillWidth: true; text: Tr.t("ab_update_note"); color: Theme.textSecondary; font.pixelSize: 11; wrapMode: Text.WordWrap }
                        Text { Layout.fillWidth: true; text: Tr.t("ab_not_yet"); color: Theme.warning; font.pixelSize: 12; wrapMode: Text.WordWrap }
                    }

                    ColumnLayout {
                        visible: settings.category === 3
                        Layout.fillWidth: true
                        spacing: 8
                        Text { text: Tr.t("app") + "  " + Tr.t("set_version", Browser.version); color: Theme.text; font.pixelSize: 13 }
                        Text { text: Browser.engineInfo; color: Theme.textSecondary; font.pixelSize: 12 }
                        Text { Layout.fillWidth: true; text: Tr.t("set_data", Browser.dataDir); color: Theme.textSecondary; font.pixelSize: 11; wrapMode: Text.WrapAnywhere }
                    }
                }
            }
        }
    }
}
