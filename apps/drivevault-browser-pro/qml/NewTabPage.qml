import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Browser.Backend

Rectangle {
    id: page

    property bool privateMode: false
    property bool guestSelected: false
    property bool shortcutsExpanded: true
    property bool iconsReady: false
    property var sites: privateMode ? [] : Browser.history.topSites(8)
    readonly property var shortcutSites: privateMode ? [] : Browser.shortcuts.items
    readonly property var visibleSites: siteTabs.currentIndex === 0
        ? (shortcutsExpanded ? shortcutSites : []).concat([
            { title: Tr.t("ntp_add_shortcut"), action: "add", glyph: "plus" },
            { title: Tr.t(shortcutsExpanded ? "ntp_show_less" : "ntp_show_more"), action: "toggle",
                glyph: shortcutsExpanded ? "shortcutLess" : "shortcutMore" }
        ])
        : sites
    readonly property color accent: privateMode ? Theme.privateAccent : Theme.accent

    signal go(string text)
    signal contextRequested(point position)

    color: Theme.background
    onVisibleChanged: if (!visible) iconsReady = false

    // The favicon provider needs the window's WebEngineView to be registered first.
    Timer {
        interval: 120
        running: page.visible && !page.privateMode
        onTriggered: page.iconsReady = true
    }

    Connections {
        target: Browser.history
        function onChanged() { page.sites = page.privateMode ? [] : Browser.history.topSites(8) }
    }

    function focusSearch() { box.forceActiveFocus() }
    function isDefaultShortcut(url) { return /^https:\/\/www\.google\.com\/?$/i.test(url) }
    function openShortcutEditor(item) {
        addShortcut.editId = item ? item.id : ""
        shortcutName.text = item ? item.title : ""
        shortcutUrl.text = item ? item.url : ""
        addShortcut.open()
    }
    function saveShortcut() {
        const input = shortcutUrl.text.trim()
        const candidate = /^https?:\/\//i.test(input) ? input : "https://" + input
        if (/\s|\\/.test(input) || !Browser.hostOf(candidate)
                || (!/^https?:\/\//i.test(input) && input.includes("://"))) {
            addShortcut.errorText = Tr.t("ntp_bad_shortcut_url")
            return
        }
        const url = Browser.resolve(candidate).toString()
        const saved = addShortcut.editId
            ? Browser.shortcuts.edit(addShortcut.editId, shortcutName.text, url)
            : Browser.shortcuts.add(shortcutName.text, url, "other") !== ""
        if (!saved) {
            addShortcut.errorText = Tr.t("ntp_duplicate_shortcut")
            return
        }
        shortcutsExpanded = true
        addShortcut.close()
    }

    // Account button in the top right corner, like the home page of Google.
    RowLayout {
        id: accountRow
        visible: !page.privateMode
        z: 1
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 16
        spacing: 8
        BButton {
            objectName: "homeGuestButton"
            visible: !ProfileSession.signedIn && !page.guestSelected
            text: Tr.t("ntp_guest")
            onClicked: {
                page.guestSelected = true
                page.focusSearch()
            }
        }
        BButton {
            objectName: "homeSignInButton"
            visible: !ProfileSession.signedIn
            text: Tr.t("ntp_sign_in")
            variant: "primary"
            onClicked: ProfileSession.signIn()
        }
        AbstractButton {
            id: avatarButton
            visible: ProfileSession.signedIn
            implicitWidth: 36
            implicitHeight: 36
            hoverEnabled: true
            Accessible.name: ProfileSession.name + ", " + ProfileSession.email
            onClicked: ProfileSession.showOptions()
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            background: Rectangle {
                radius: 18
                color: Theme.surface
                border.width: 1
                border.color: avatarButton.hovered || avatarButton.visualFocus ? page.accent : Theme.border
            }
            contentItem: Item {
                Image {
                    id: avatarImage
                    anchors.fill: parent
                    anchors.margins: 2
                    source: ProfileSession.picture
                    fillMode: Image.PreserveAspectCrop
                }
                LineIcon { anchors.centerIn: parent; visible: avatarImage.status !== Image.Ready; kind: "globe"; color: page.accent }
            }
        }
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: point => page.contextRequested(point.position)
    }

    Flickable {
        id: workspace
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, dashboard.y + dashboard.height + 24)
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        ColumnLayout {
            id: dashboard
            width: Math.min(640, Math.max(0, workspace.width - 40))
            x: (workspace.width - width) / 2
            y: Math.min(150, Math.max(70, workspace.height * 0.22))
            spacing: 28

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: false
                Layout.preferredWidth: Math.min(dashboard.width, 62 + brand.implicitWidth)
                Layout.maximumWidth: dashboard.width
                spacing: 14

                Rectangle {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    radius: 14
                    color: page.accent
                    LineIcon { anchors.centerIn: parent; size: 28; kind: page.privateMode ? "mask" : "globe"; color: Theme.readableOn(page.accent) }
                }
                Text {
                    id: brand
                    Layout.fillWidth: true
                    text: Tr.t("app")
                    color: Theme.text
                    font.pixelSize: dashboard.width < 440 ? 24 : 44
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }

            TextField {
                id: box
                objectName: "ntpSearch"
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                placeholderText: Tr.t("ntp_search")
                placeholderTextColor: Theme.textSecondary
                color: Theme.text
                font.pixelSize: 16
                leftPadding: 52
                rightPadding: 24
                selectByMouse: true
                HoverHandler { id: boxHover; cursorShape: Qt.IBeamCursor }
                background: Rectangle {
                    radius: height / 2
                    color: box.activeFocus || boxHover.hovered ? Theme.surfaceHover : Theme.surface
                    border.width: box.activeFocus ? 2 : 1
                    border.color: box.activeFocus ? page.accent : Theme.border
                    LineIcon { x: 20; anchors.verticalCenter: parent.verticalCenter; kind: "search"; color: Theme.textSecondary }
                }
                onAccepted: { if (text.trim() !== "") { page.go(text); text = "" } }
            }
            Text {
                visible: page.privateMode
                Layout.fillWidth: true
                text: Tr.t("ntp_private_title")
                color: page.accent
                font.pixelSize: 16
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
            }
            Text {
                visible: page.privateMode
                Layout.fillWidth: true
                text: Tr.t("ntp_private_body")
                color: Theme.textSecondary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 1.3
            }

                        ColumnLayout {
                visible: !page.privateMode
                Layout.fillWidth: true
                Layout.topMargin: 2
                spacing: 14

                Item {
                    Layout.fillWidth: true
                    implicitHeight: siteTabs.implicitHeight
                    TabBar {
                        id: siteTabs
                        objectName: "workspaceTabs"
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(parent.width, 252)
                        padding: 3
                        spacing: 3
                        background: Rectangle {
                            radius: height / 2
                            color: Theme.surface
                            border.color: Theme.border
                        }
                        WorkspaceTab { text: Tr.t("ntp_shortcuts") }
                        WorkspaceTab { text: Tr.t("ntp_top") }
                    }
                }

                Text {
                    visible: page.visibleSites.length === 0
                    Layout.fillWidth: true
                    topPadding: 12
                    bottomPadding: 12
                    text: Tr.t("ntp_history_empty")
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }

                Item {
                    visible: page.visibleSites.length > 0
                    Layout.fillWidth: true
                    implicitHeight: siteGrid.implicitHeight
                    Flow {
                        id: siteGrid
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: columns * tileWidth + Math.max(0, columns - 1) * spacing
                        readonly property int tileWidth: 78
                        readonly property int columns: Math.min(page.visibleSites.length,
                            Math.max(1, Math.floor((dashboard.width + spacing) / (tileWidth + spacing))))
                        spacing: 9

                        Repeater {
                            model: page.visibleSites
                            delegate: AbstractButton {
                                id: shortcut
                                required property var modelData
                                readonly property string host: modelData.url ? Browser.hostOf(modelData.url).replace(/^www\./, "") : ""
                                readonly property string iconPage: modelData.url && /^https?:\/\//i.test(modelData.url) ? modelData.url : ""
                                readonly property string iconHome: {
                                    const origin = iconPage.match(/^https?:\/\/[^/?#]+/i)
                                    return origin ? origin[0] + "/" : ""
                                }
                                width: siteGrid.tileWidth
                                height: 78
                                padding: 4
                                hoverEnabled: true
                                text: modelData.title || host
                                Accessible.name: text
                                onClicked: {
                                    if (modelData.action === "add") page.openShortcutEditor(null)
                                    else if (modelData.action === "toggle") page.shortcutsExpanded = !page.shortcutsExpanded
                                    else page.go(modelData.url)
                                }
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                background: Rectangle {
                                    radius: 12
                                    color: shortcut.down ? Theme.border
                                        : shortcut.hovered ? Theme.surface : "transparent"
                                    border.width: shortcut.visualFocus ? 1 : 0
                                    border.color: page.accent
                                }
                                contentItem: Column {
                                    spacing: 7
                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: 44
                                        height: 44
                                        radius: width / 2
                                        color: Theme.surface
                                        border.color: Theme.border
                                        WorkspaceIcon {
                                            id: siteIcon
                                            anchors.centerIn: parent
                                            width: 26
                                            height: 26
                                            bundled: page.isDefaultShortcut(shortcut.iconHome)
                                            source: {
                                                if (!page.visible || !shortcut.iconPage) return ""
                                                if (bundled)
                                                    return "qrc:/qt/qml/DriveVault/Browser/resources/workspace/google-favicon.png"
                                                return page.iconsReady ? "image://favicon/" + shortcut.iconPage : ""
                                            }
                                        }
                                        WorkspaceIcon {
                                            id: fallbackIcon
                                            anchors.centerIn: parent
                                            width: siteIcon.width
                                            height: siteIcon.height
                                            source: {
                                                if (!page.visible || !siteIcon.finished || siteIcon.hasIcon) return ""
                                                return shortcut.iconHome && shortcut.iconHome !== shortcut.iconPage
                                                    ? "image://favicon/" + shortcut.iconHome : ""
                                            }
                                        }
                                        Text {
                                            anchors.centerIn: parent
                                            visible: !shortcut.modelData.glyph && !siteIcon.hasIcon && !fallbackIcon.hasIcon
                                            text: (shortcut.host || "?").charAt(0).toUpperCase()
                                            color: "#BCEFFA"
                                            font.pixelSize: 18
                                            font.weight: Font.DemiBold
                                        }
                                        LineIcon {
                                            anchors.centerIn: parent
                                            visible: !!shortcut.modelData.glyph
                                            kind: shortcut.modelData.glyph || "plus"
                                            size: 18
                                            color: "#BCEFFA"
                                        }
                                    }
                                    Text {
                                        id: shortcutLabel
                                        width: parent.width
                                        text: shortcut.text
                                        color: Theme.text
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }
                                ToolTip.visible: hovered && shortcutLabel.truncated
                                ToolTip.delay: 700
                                ToolTip.text: text

                                IconButton {
                                    anchors.top: parent.top
                                    anchors.right: parent.right
                                    z: 1
                                    box: 24
                                    kind: "menu"
                                    visible: !!shortcut.modelData.id && siteTabs.currentIndex === 0
                                        && (shortcut.hovered || shortcut.visualFocus || hovered || visualFocus
                                            || (shortcutActions.visible && shortcutActions.target.id === shortcut.modelData.id))
                                    tip: Tr.t("ntp_shortcut_actions", shortcut.text)
                                    onClicked: {
                                        shortcutActions.target = shortcut.modelData
                                        const pos = mapToItem(page, width, height)
                                        shortcutActions.popup(page, Math.max(0, pos.x - shortcutActions.width), pos.y)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Menu {
        id: shortcutActions
        property var target
        width: 150
        padding: 5
        background: Rectangle { radius: 10; color: Theme.backgroundAlt; border.color: Theme.border }
        delegate: MenuItem {
            id: actionItem
            contentItem: Text { text: actionItem.text; color: Theme.text; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { radius: 6; color: actionItem.highlighted ? Theme.surfaceHover : "transparent" }
        }
        Action { text: Tr.t("ntp_edit_shortcut"); onTriggered: page.openShortcutEditor(shortcutActions.target) }
        Action { text: Tr.t("ntp_remove_shortcut"); onTriggered: Browser.shortcuts.remove(shortcutActions.target.id) }
    }

    Popup {
        id: addShortcut
        objectName: "addShortcut"
        property string errorText: ""
        property string editId: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        width: Math.min(460, parent.width - 48)
        padding: 22
        background: Rectangle { radius: 16; color: Theme.backgroundAlt; border.color: Theme.border }
        Overlay.modal: Rectangle { color: "#B000080F" }
        onAboutToShow: errorText = ""
        onOpened: shortcutName.forceActiveFocus()

        contentItem: ColumnLayout {
            spacing: 12
            Text { text: Tr.t(addShortcut.editId ? "ntp_edit_shortcut" : "ntp_add_shortcut"); color: Theme.text; font.pixelSize: 19; font.weight: Font.DemiBold }
            Text { text: Tr.t("ntp_shortcut_name"); color: Theme.textSecondary }
            ShortcutField {
                id: shortcutName
                Accessible.name: Tr.t("ntp_shortcut_name")
                onAccepted: shortcutUrl.forceActiveFocus()
            }
            Text { text: "URL"; color: Theme.textSecondary }
            ShortcutField {
                id: shortcutUrl
                Accessible.name: "URL"
                inputMethodHints: Qt.ImhUrlCharactersOnly
                onTextEdited: addShortcut.errorText = ""
                onAccepted: if (saveShortcutButton.enabled) page.saveShortcut()
            }
            Text {
                visible: text !== ""
                Layout.fillWidth: true
                text: addShortcut.errorText
                color: Theme.errorSoft
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Layout.topMargin: 8
                BButton { text: Tr.t("cancel"); onClicked: addShortcut.close() }
                BButton {
                    id: saveShortcutButton
                    text: Tr.t("ntp_shortcut_done")
                    variant: "primary"
                    enabled: shortcutName.text.trim() !== "" && shortcutUrl.text.trim() !== ""
                    onClicked: page.saveShortcut()
                }
            }
        }
    }

    component ShortcutField: TextField {
        Layout.fillWidth: true
        implicitHeight: 36
        leftPadding: 10
        rightPadding: 10
        selectByMouse: true
        color: Theme.text
        background: Rectangle {
            radius: 8
            color: Theme.surface
            border.color: parent.activeFocus ? Theme.accent : Theme.border
        }
    }

    component WorkspaceIcon: Image {
        id: icon
        property bool bundled: false
        property bool hasPixels: false
        property bool checkedPixels: false
        readonly property bool hasIcon: bundled ? status === Image.Ready : hasPixels
        readonly property bool finished: status === Image.Error || checkedPixels || hasIcon
        readonly property bool canCheck: !bundled && status === Image.Ready && !!Window.window && Window.window.visible
        sourceSize.width: 40
        sourceSize.height: 40
        fillMode: Image.PreserveAspectFit
        cache: false

        onStatusChanged: {
            hasPixels = false
            checkedPixels = false
        }
        onCanCheckChanged: if (canCheck && !checkedPixels) Qt.callLater(checkPixels)

        function checkPixels() {
            if (!canCheck || checkedPixels) return
            const loadedSource = source.toString()
            // Qt's favicon provider can return a transparent image for a cache miss.
            if (!grabToImage(result => {
                if (!icon || icon.source.toString() !== loadedSource) return
                icon.hasPixels = Browser.hasIconPixels(result)
                icon.checkedPixels = true
            }, Qt.size(26, 26))) checkedPixels = true
        }
    }

    component WorkspaceTab: TabButton {
        id: tab
        implicitHeight: 30
        hoverEnabled: true
        font.pixelSize: 13
        font.weight: Font.DemiBold
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        background: Rectangle {
            radius: height / 2
            color: tab.checked ? Theme.surfaceHover : tab.hovered ? Theme.surface : "transparent"
            border.width: tab.checked || tab.visualFocus ? 1 : 0
            border.color: tab.visualFocus ? page.accent : Theme.border
        }
        contentItem: Text {
            text: tab.text
            font: tab.font
            color: tab.checked ? Theme.text : Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
}
