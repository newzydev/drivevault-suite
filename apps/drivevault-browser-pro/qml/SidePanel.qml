import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.Browser.Backend

// The panel at the right: history, bookmarks, downloads or settings (what Chrome shows as its own pages).
Rectangle {
    id: panel
    property string kind: "history"               // history | bookmarks | downloads | settings
    property bool privateMode: false
    signal openUrl(string url, bool newTab)
    signal closeRequested()

    color: Theme.backgroundAlt
    border.width: 1
    border.color: Theme.borderSubtle
    clip: true

    function titleOf(k) { return k === "history" ? Tr.t("history") : k === "bookmarks" ? Tr.t("bookmarks") : k === "downloads" ? Tr.t("downloads") : Tr.t("settings") }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            Layout.leftMargin: 16
            Layout.rightMargin: 8
            Text {
                Layout.fillWidth: true
                text: panel.titleOf(panel.kind)
                color: Theme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            IconButton { kind: "close"; tip: Tr.t("close"); objectName: "panelClose"; onClicked: panel.closeRequested() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: panel.kind === "history" ? historyView : panel.kind === "bookmarks" ? bookmarksView
                           : panel.kind === "downloads" ? downloadsView : settingsView
        }
    }

    // ---- shared pieces ------------------------------------------------------------------------------------------------
    component PanelField: TextField {
        color: Theme.text
        placeholderTextColor: Theme.textDisabled
        font.pixelSize: 13
        selectByMouse: true
        leftPadding: 12
        rightPadding: 12
        implicitHeight: 34
        background: Rectangle {
            radius: Theme.radiusSmall
            color: Theme.surface
            border.width: parent.activeFocus ? 2 : 1
            border.color: parent.activeFocus ? Theme.accent : Theme.border
        }
    }
    component Empty: Text {
        color: Theme.textDisabled
        font.pixelSize: 13
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
    }
    component SectionTitle: Text {
        color: Theme.textSecondary
        font.pixelSize: 12
        font.weight: Font.Medium
        topPadding: 8
    }

    // ---- history ---------------------------------------------------------------------------------------------------------
    Component {
        id: historyView
        ColumnLayout {
            id: hv
            spacing: 8
            property var rows: Browser.history.search(find.text, 300)
            Connections { target: Browser.history; function onChanged() { hv.rows = Browser.history.search(find.text, 300) } }
            PanelField {
                id: find
                objectName: "historySearch"
                Layout.fillWidth: true
                Layout.margins: 12
                Layout.bottomMargin: 0
                placeholderText: Tr.t("panel_search_history")
                onTextChanged: hv.rows = Browser.history.search(text, 300)
            }
            BButton {
                Layout.leftMargin: 12
                text: Tr.t("panel_clear_history")
                enabled: hv.rows.length > 0
                onClicked: confirmClear.open()
            }
            Empty { visible: hv.rows.length === 0; Layout.fillWidth: true; Layout.margins: 24; text: Tr.t("panel_no_history") }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: hv.rows
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: 52
                    color: hov.hovered ? Theme.surfaceHover : "transparent"
                    HoverHandler { id: hov; cursorShape: Qt.PointingHandCursor }
                    TapHandler { acceptedButtons: Qt.LeftButton; onTapped: panel.openUrl(modelData.url, false) }
                    TapHandler { acceptedButtons: Qt.MiddleButton; onTapped: panel.openUrl(modelData.url, true) }
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 44
                        spacing: 2
                        Item { Layout.fillHeight: true }
                        Text { Layout.fillWidth: true; text: modelData.title || modelData.url; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight }
                        Text {
                            Layout.fillWidth: true
                            text: Qt.formatDateTime(new Date(modelData.ms), "dd/MM HH:mm") + "  ·  " + Browser.prettyUrl(modelData.url)
                            color: Theme.textSecondary; font.pixelSize: 11; elide: Text.ElideRight
                        }
                        Item { Layout.fillHeight: true }
                    }
                    IconButton {
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        visible: hov.hovered
                        kind: "close"; box: 28; tip: Tr.t("bm_delete")
                        onClicked: Browser.history.remove(modelData.url)
                    }
                }
            }
            MessageDialog {
                id: confirmClear
                title: Tr.t("history")
                text: Tr.t("panel_clear_confirm")
                buttons: MessageDialog.Yes | MessageDialog.No
                onButtonClicked: function (button) { if (button === MessageDialog.Yes) Browser.history.clear() }
            }
        }
    }

    // ---- bookmarks ---------------------------------------------------------------------------------------------------------
    Component {
        id: bookmarksView
        ColumnLayout {
            id: bv
            spacing: 4
            property string note: ""
            property var rows: Browser.bookmarks.items
            Connections { target: Browser.bookmarks; function onChanged() { bv.rows = Browser.bookmarks.items } }
            RowLayout {
                Layout.margins: 12
                spacing: 8
                BButton { text: Tr.t("bm_import"); onClicked: importDialog.open() }
                BButton { text: Tr.t("bm_export"); enabled: bv.rows.length > 0; onClicked: exportDialog.open() }
            }
            Text {
                visible: bv.note !== ""
                Layout.leftMargin: 16
                text: bv.note
                color: Theme.success
                font.pixelSize: 12
            }
            Empty { visible: bv.rows.length === 0; Layout.fillWidth: true; Layout.margins: 24; text: Tr.t("panel_no_bookmarks") }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                model: bv.rows
                section.property: "folder"
                section.delegate: SectionTitle {
                    required property string section
                    leftPadding: 16
                    text: section === "bar" ? Tr.t("bm_bar") : Tr.t("bm_other")
                }
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    width: ListView.view.width
                    height: 48
                    color: hov.hovered ? Theme.surfaceHover : "transparent"
                    HoverHandler { id: hov; cursorShape: Qt.PointingHandCursor }
                    TapHandler { acceptedButtons: Qt.LeftButton; onTapped: panel.openUrl(row.modelData.url, false) }
                    TapHandler { acceptedButtons: Qt.MiddleButton; onTapped: panel.openUrl(row.modelData.url, true) }
                    TapHandler { acceptedButtons: Qt.RightButton; onTapped: rowMenu.popup() }
                    Menu {
                        id: rowMenu
                        MenuItem { text: Tr.t("bm_openNewTab"); onTriggered: panel.openUrl(row.modelData.url, true) }
                        MenuItem {
                            text: row.modelData.folder === "bar" ? Tr.t("bm_moveOther") : Tr.t("bm_moveBar")
                            onTriggered: Browser.bookmarks.move(row.modelData.id, row.modelData.folder === "bar" ? "other" : "bar")
                        }
                        MenuItem { text: Tr.t("bm_delete"); onTriggered: Browser.bookmarks.remove(row.modelData.id) }
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 44
                        spacing: 2
                        Item { Layout.fillHeight: true }
                        Text { Layout.fillWidth: true; text: row.modelData.title; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight }
                        Text { Layout.fillWidth: true; text: Browser.prettyUrl(row.modelData.url); color: Theme.textSecondary; font.pixelSize: 11; elide: Text.ElideRight }
                        Item { Layout.fillHeight: true }
                    }
                    IconButton {
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        visible: hov.hovered
                        kind: "trash"; box: 28; tip: Tr.t("bm_delete")
                        onClicked: Browser.bookmarks.remove(row.modelData.id)
                    }
                }
            }
            FileDialog {
                id: importDialog
                title: Tr.t("bm_import")
                fileMode: FileDialog.OpenFile
                nameFilters: ["HTML (*.html *.htm)"]
                onAccepted: bv.note = Tr.t("bm_imported", Browser.bookmarks.importHtml(Browser.localPath(selectedFile)))
            }
            FileDialog {
                id: exportDialog
                title: Tr.t("bm_export")
                fileMode: FileDialog.SaveFile
                nameFilters: ["HTML (*.html)"]
                defaultSuffix: "html"
                onAccepted: bv.note = Browser.bookmarks.exportHtml(Browser.localPath(selectedFile)) ? Tr.t("bm_exported") : Tr.t("bm_export_failed")
            }
        }
    }

    // ---- downloads ---------------------------------------------------------------------------------------------------------
    Component {
        id: downloadsView
        ColumnLayout {
            spacing: 4
            Text {
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 10
                Layout.fillWidth: true
                text: Tr.t("dl_saved_in", Browser.downloads.folder)
                color: Theme.textSecondary
                font.pixelSize: 11
                elide: Text.ElideMiddle
            }
            BButton {
                Layout.leftMargin: 12
                text: Tr.t("dl_clear")
                enabled: list.count > 0
                onClicked: Browser.downloads.clearFinished()
            }
            Empty { visible: list.count === 0; Layout.fillWidth: true; Layout.margins: 24; text: Tr.t("panel_no_downloads") }
            ListView {
                id: list
                objectName: "downloadList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                model: Browser.downloads
                delegate: Rectangle {
                    id: item
                    required property int index
                    required property string name
                    required property string state
                    required property double received
                    required property double total
                    required property bool risky
                    width: ListView.view.width
                    height: col.implicitHeight + 20
                    color: "transparent"
                    ColumnLayout {
                        id: col
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 16
                        spacing: 4
                        Text { Layout.fillWidth: true; text: item.name; color: Theme.text; font.pixelSize: 13; elide: Text.ElideMiddle }
                        ProgressBar {
                            Layout.fillWidth: true
                            visible: item.state === "running" || item.state === "paused"
                            from: 0; to: item.total > 0 ? item.total : 1
                            value: item.total > 0 ? item.received : 0
                            indeterminate: item.total <= 0
                            implicitHeight: 4
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Tr.t("dl_" + item.state) + (item.total > 0 ? "  ·  " + (item.received / 1048576).toFixed(1) + " / " + (item.total / 1048576).toFixed(1) + " MB" : "")
                            color: item.state === "failed" ? Theme.errorSoft : Theme.textSecondary
                            font.pixelSize: 11
                        }
                        Text {
                            visible: item.risky
                            Layout.fillWidth: true
                            text: Tr.t("dl_risky")
                            color: Theme.warning
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                        RowLayout {
                            spacing: 4
                            IconButton { visible: item.state === "running"; kind: "stop"; box: 28; tip: Tr.t("dl_pause"); onClicked: Browser.downloads.pause(item.index) }
                            IconButton { visible: item.state === "paused"; kind: "forward"; box: 28; tip: Tr.t("dl_resume"); onClicked: Browser.downloads.resume(item.index) }
                            IconButton { visible: item.state === "running" || item.state === "paused"; kind: "close"; box: 28; tip: Tr.t("dl_cancel"); onClicked: Browser.downloads.cancel(item.index) }
                            IconButton { visible: item.state === "done"; kind: "folder"; box: 28; tip: Tr.t("dl_show"); onClicked: Browser.downloads.showInFolder(item.index) }
                            IconButton { visible: item.state !== "running" && item.state !== "paused"; kind: "trash"; box: 28; tip: Tr.t("dl_remove"); onClicked: Browser.downloads.remove(item.index) }
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
                }
            }
        }
    }

    // ---- settings ---------------------------------------------------------------------------------------------------------
    Component {
        id: settingsView
        Flickable {
            id: sv
            contentWidth: width
            contentHeight: sc.implicitHeight + 32
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            property string note: ""

            component Choice: ComboBox {
                id: cb
                property string current: ""
                signal picked(string id)
                textRole: "name"
                implicitHeight: 34
                Layout.fillWidth: true
                currentIndex: { for (let i = 0; i < model.length; ++i) if (model[i].id === current) return i; return 0 }
                onActivated: picked(model[currentIndex].id)
                font.pixelSize: 13
                contentItem: Text { leftPadding: 10; text: cb.displayText; color: Theme.text; font: cb.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                background: Rectangle { radius: Theme.radiusSmall; color: Theme.surface; border.width: 1; border.color: Theme.border }
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

            ColumnLayout {
                id: sc
                x: 16
                width: sv.width - 32
                y: 8
                spacing: 8

                SectionTitle { text: Tr.t("set_general") }
                Text { text: Tr.t("set_language"); color: Theme.text; font.pixelSize: 13 }
                Choice {
                    objectName: "setLanguage"
                    model: [ { id: "th", name: "ไทย" }, { id: "en", name: "English" } ]
                    current: Browser.settings.language
                    onPicked: id => Browser.settings.language = id
                }
                Text { text: Tr.t("set_home"); color: Theme.text; font.pixelSize: 13 }
                PanelField {
                    Layout.fillWidth: true
                    text: Browser.settings.homePage
                    placeholderText: Tr.t("set_home_hint")
                    onEditingFinished: Browser.settings.homePage = text
                }
                Text { text: Tr.t("set_startup"); color: Theme.text; font.pixelSize: 13 }
                Choice {
                    model: [ { id: "newtab", name: Tr.t("set_startup_newtab") }, { id: "restore", name: Tr.t("set_startup_restore") } ]
                    current: Browser.settings.startup
                    onPicked: id => Browser.settings.startup = id
                }
                Text { text: Tr.t("set_search"); color: Theme.text; font.pixelSize: 13 }
                Choice {
                    model: Browser.settings.engines()
                    current: Browser.settings.searchEngine
                    onPicked: id => Browser.settings.searchEngine = id
                }
                Toggle {
                    text: Tr.t("showBar")
                    checked: Browser.settings.showBookmarksBar
                    onToggled: Browser.settings.showBookmarksBar = checked
                }

                SectionTitle { text: Tr.t("set_privacy") }
                Toggle {
                    text: Tr.t("set_notifications")
                    checked: Browser.settings.blockNotifications
                    onToggled: Browser.settings.blockNotifications = checked
                }

                SectionTitle { text: Tr.t("set_adblock") }
                Toggle {
                    objectName: "adblockSwitch"
                    text: Tr.t("ab_on")
                    checked: Browser.adblock.enabled
                    onToggled: Browser.adblock.enabled = checked
                }
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
                              + "  (" + (modelData.present ? Tr.t("ab_rules", modelData.rules) + (modelData.date ? ", " + Tr.t("ab_date", modelData.date.substring(0, 10)) : "") : Tr.t("ab_missing")) + ")"
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

                SectionTitle { text: Tr.t("set_clear") }
                BButton {
                    text: Tr.t("set_clear_button")
                    onClicked: { Browser.clearBrowsingData(true, true); sv.note = Tr.t("set_cleared") }
                }
                Text { Layout.fillWidth: true; text: Tr.t("set_clear_note"); color: Theme.textSecondary; font.pixelSize: 11; wrapMode: Text.WordWrap }
                Text { visible: sv.note !== ""; text: sv.note; color: Theme.success; font.pixelSize: 12 }

                SectionTitle { text: Tr.t("set_about") }
                Text { text: Tr.t("app") + "  " + Tr.t("set_version", Browser.version); color: Theme.text; font.pixelSize: 13 }
                Text { text: Browser.engineInfo; color: Theme.textSecondary; font.pixelSize: 12 }
                Text { Layout.fillWidth: true; text: Tr.t("set_data", Browser.dataDir); color: Theme.textSecondary; font.pixelSize: 11; wrapMode: Text.WrapAnywhere }
            }
        }
    }
}
