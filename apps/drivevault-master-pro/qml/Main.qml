import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.Master.Backend

ApplicationWindow {
    id: win
    visible: true
    width: 1120
    height: 740
    minimumWidth: 760
    minimumHeight: 560
    title: "DriveVault Master Pro"
    color: Theme.background

    // ---- the window opens the way it was closed: maximized or normal, with its normal size and place ----------------------------
    property int normalX: x
    property int normalY: y
    property int normalWidth: width
    property int normalHeight: height
    onXChanged: if (visibility === Window.Windowed) normalX = x
    onYChanged: if (visibility === Window.Windowed) normalY = y
    onWidthChanged: if (visibility === Window.Windowed) normalWidth = width
    onHeightChanged: if (visibility === Window.Windowed) normalHeight = height
    function pointOnAScreen(px, py) {
        const screens = Qt.application.screens
        for (let i = 0; i < screens.length; ++i) {
            const s = screens[i]
            if (px >= s.virtualX && px < s.virtualX + s.width && py >= s.virtualY && py < s.virtualY + s.height)
                return true
        }
        return false
    }
    Component.onCompleted: {
        const s = Hub.windowState()
        if (s.width > 0 && s.height > 0) {
            width = Math.max(minimumWidth, s.width)
            height = Math.max(minimumHeight, s.height)
            if (pointOnAScreen(s.x + 80, s.y + 20)) {      // never open off screen (a monitor that is gone)
                x = s.x
                y = s.y
            }
        }
        normalX = x; normalY = y; normalWidth = width; normalHeight = height
        if (s.maximized)
            visibility = Window.Maximized
        Hub.checkForUpdatesOnStart()
    }
    onClosing: Hub.saveWindowState(visibility === Window.Maximized, normalX, normalY, normalWidth, normalHeight)

    // ---- sections, search and the numbers of the hero ------------------------------------------------------------------------
    // all program cards get the height of the tallest one
    property var neededByKey: ({})
    property real cardHeight: 0
    function setNeeded(key, h) {
        if (neededByKey[key] === h)
            return
        const copy = Object.assign({}, neededByKey)
        copy[key] = h
        neededByKey = copy
        let m = 0
        for (const k of Object.keys(copy))
            m = Math.max(m, copy[k])
        cardHeight = m
    }
    readonly property var sections: [ { key: "multimedia" }, { key: "office" }, { key: "other" } ]
    function matches(p) {
        const q = searchField.text.trim().toLowerCase()
        return q === "" || (p.name + " " + p.description).toLowerCase().indexOf(q) >= 0
    }
    function countIn(section) {
        let n = 0
        const list = Hub.products
        for (let i = 0; i < list.length; ++i)
            if (list[i].section === section && matches(list[i]))
                ++n
        return n
    }
    function totalMatches() { return countIn("multimedia") + countIn("office") + countIn("other") }
    function installedCount() { let n = 0; for (const p of Hub.products) if (p.installed === true) ++n; return n }
    function updateCount() { let n = 0; for (const p of Hub.products) if (p.updateAvailable === true || p.onlineUpdate === true) ++n; if (Hub.selfUpdateVersion !== "") ++n; return n }

    property string notice: ""
    function showNotice(text) { notice = text; noticeTimer.restart() }

    // Programs installed / uninstalled elsewhere (a product installer, Windows Settings) show up on their own.
    Timer { interval: 5000; repeat: true; running: win.visible && !Hub.busy; onTriggered: Hub.refresh() }
    Timer { id: noticeTimer; interval: 6000; onTriggered: win.notice = "" }
    Connections { target: Hub; function onMessageChanged() { win.showNotice(Hub.message) } }

    FileDialog {
        id: setupDialog
        title: Tr.s("browse")
        currentFolder: StandardPaths.writableLocation(StandardPaths.DownloadLocation)     // fixed start folder (not the program folder)
        nameFilters: [Tr.s("setupFiles")]
        onAccepted: { const e = Hub.installFromFile(selectedFile.toString()); if (e !== "") win.showNotice(e) }
    }
    FolderDialog {
        id: folderDialog
        title: Tr.s("addFolder")
        currentFolder: StandardPaths.writableLocation(StandardPaths.DownloadLocation)
        onAccepted: { const e = Hub.addInstallerFolder(selectedFolder.toString()); if (e !== "") win.showNotice(e) }
    }
    AboutDialog { id: aboutDialog }

    Popup {
        id: confirm
        property var product: ({ name: "", id: "" })
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        dim: true
        width: Math.min(440, win.width - 48)
        padding: 24
        Overlay.modal: Rectangle { color: "#B3000000" }
        background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }
        contentItem: ColumnLayout {
            spacing: 14
            Text { Layout.fillWidth: true; text: Tr.s("confirmUninstall").replace("%1", confirm.product.name); font.pixelSize: 17; font.weight: Font.DemiBold; color: Theme.text; wrapMode: Text.Wrap }
            Text { Layout.fillWidth: true; text: Tr.s("confirmText"); font.pixelSize: 13; color: Theme.textSecondary; wrapMode: Text.Wrap }
            RowLayout {
                Item { Layout.fillWidth: true }
                MButton { objectName: "confirmCancel"; variant: "ghost"; text: Tr.s("cancel"); onClicked: confirm.close() }
                MButton {
                    objectName: "confirmUninstall"
                    variant: "danger"
                    text: Tr.s("uninstall")
                    onClicked: { confirm.close(); const e = Hub.uninstall(confirm.product.id); if (e !== "") win.showNotice(e) }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- top bar ----------------------------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 64
            color: "#0D0E11"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 28
                anchors.rightMargin: 24
                spacing: 12
                Image { source: "qrc:/qt/qml/DriveVault/Master/resources/icons/app.png"; Layout.preferredWidth: 34; Layout.preferredHeight: 34; sourceSize.width: 68; smooth: true }
                Text { text: Tr.s("title"); font.pixelSize: 17; font.weight: Font.Bold; color: Theme.text }
                Item { Layout.fillWidth: true }
                // language: one small switch
                Rectangle {
                    implicitHeight: 32
                    implicitWidth: langRow.implicitWidth + 6
                    radius: 16
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border
                    Row {
                        id: langRow
                        anchors.centerIn: parent
                        spacing: 2
                        Repeater {
                            model: [ { code: "th", label: "ไทย", name: "langTh" }, { code: "en", label: "EN", name: "langEn" } ]
                            delegate: AbstractButton {
                                required property var modelData
                                objectName: modelData.name
                                readonly property bool current: Hub.language === modelData.code
                                implicitWidth: 46; implicitHeight: 26
                                hoverEnabled: true
                                onClicked: Hub.language = modelData.code
                                contentItem: Text {
                                    text: parent.modelData.label
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    color: parent.current ? Theme.readableOn(Theme.accent) : Theme.textSecondary
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle { radius: 13; color: parent.current ? Theme.accent : parent.hovered ? Theme.surfaceHover : "transparent" }
                            }
                        }
                    }
                }
                MButton {
                    objectName: "checkUpdates"
                    compact: true
                    text: Hub.checking ? Tr.s("checking") : Tr.s("checkUpdates")
                    enabled: !Hub.busy && !Hub.checking
                    onClicked: { const e = Hub.checkForUpdates(); if (e !== "") win.showNotice(e) }
                }
                // everything else (rarely needed) is in this menu
                MButton {
                    objectName: "moreMenu"
                    compact: true
                    Layout.preferredWidth: 34
                    leftPadding: 0; rightPadding: 0
                    text: "⋯"
                    font.pixelSize: 18
                    onClicked: topMenu.popup(width - topMenu.implicitWidth, height + 6)
                    MMenu {
                        id: topMenu
                        implicitWidth: 280
                        MMenuItem { objectName: "addFolder"; text: Tr.s("addFolder"); onTriggered: folderDialog.open() }
                        MMenuItem { objectName: "openInstallers"; text: Tr.s("openInstallers"); onTriggered: Hub.openInstallersFolder() }
                        MenuSeparator { padding: 4; contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
                        MMenuItem { objectName: "autoCheck"; text: Tr.s("autoCheck"); checkable: true; checked: Hub.autoCheck; onTriggered: Hub.autoCheck = checked }
                        MenuSeparator { padding: 4; contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
                        MMenuItem { objectName: "about"; text: Tr.s("about"); onTriggered: aboutDialog.open() }
                    }
                }
            }
        }

        // ---- Master Pro itself has a newer release ------------------------------------------------------------------------------
        Rectangle {
            objectName: "selfUpdateBanner"
            Layout.fillWidth: true
            visible: Hub.selfUpdateVersion !== ""
            implicitHeight: visible ? 56 : 0
            color: Qt.alpha(Theme.accent, 0.12)
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Qt.alpha(Theme.accent, 0.4) }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 28
                anchors.rightMargin: 24
                spacing: 14
                Text {
                    Layout.fillWidth: true
                    text: Tr.s("selfUpdate").replace("%1", Hub.selfUpdateVersion)
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.text
                    elide: Text.ElideRight
                }
                MButton {
                    objectName: "selfUpdate"
                    variant: "primary"
                    compact: true
                    text: Tr.s("selfUpdateBtn")
                    enabled: !Hub.busy && !Hub.checking
                    onClicked: { const e = Hub.updateSelf(); if (e !== "") win.showNotice(e) }
                }
            }
        }

        // ---- hero + programs by section --------------------------------------------------------------------------------------------
        Flickable {
            id: scroller
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: page.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: page
                width: scroller.width
                spacing: 0

                // hero: a warm glow, the headline and the search field
                Rectangle {
                    id: hero
                    objectName: "hero"
                    Layout.fillWidth: true
                    implicitHeight: 250
                    clip: true
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#2A1D0A" }
                        GradientStop { position: 0.55; color: "#17130E" }
                        GradientStop { position: 1.0; color: Theme.background }
                    }
                    Rectangle { width: 460; height: 460; radius: 230; x: hero.width - 330; y: -250; color: Qt.alpha(Theme.accent, 0.13) }
                    Rectangle { width: 300; height: 300; radius: 150; x: hero.width - 150; y: 60; color: Qt.alpha(Theme.accent, 0.07) }
                    Rectangle { width: 340; height: 340; radius: 170; x: -120; y: 90; color: Qt.alpha("#8B5CF6", 0.07) }

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 40
                        anchors.rightMargin: 40
                        spacing: 10
                        Text {
                            Layout.fillWidth: true
                            text: Tr.s("heroTitle")
                            font.pixelSize: 32
                            font.weight: Font.Bold
                            color: Theme.text
                            wrapMode: Text.Wrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Tr.s("subtitle")
                            font.pixelSize: 15
                            color: Theme.textSecondary
                            wrapMode: Text.Wrap
                        }
                        Item { Layout.preferredHeight: 6 }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 16

                            TextField {
                                id: searchField
                                objectName: "searchField"
                                Layout.fillWidth: true
                                Layout.maximumWidth: 560
                                implicitHeight: 50
                                placeholderText: Tr.s("searchPlaceholder")
                                placeholderTextColor: Theme.textDisabled
                                color: Theme.text
                                font.pixelSize: 15
                                selectByMouse: true
                                leftPadding: 50
                                rightPadding: 44
                                verticalAlignment: TextInput.AlignVCenter
                                background: Rectangle {
                                    radius: height / 2
                                    color: "#CC1B1D24"
                                    border.width: searchField.activeFocus ? 2 : 1
                                    border.color: searchField.activeFocus ? Theme.accent : Theme.border
                                    Behavior on border.color { ColorAnimation { duration: 120 } }
                                }
                                Item {                                     // magnifier, drawn (no icon font needed)
                                    x: 20
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 18; height: 18
                                    Rectangle { x: 0; y: 0; width: 12; height: 12; radius: 6; color: "transparent"; border.width: 2; border.color: searchField.activeFocus ? Theme.accent : Theme.textSecondary }
                                    Rectangle { x: 10; y: 12; width: 8; height: 2; radius: 1; rotation: 45; color: searchField.activeFocus ? Theme.accent : Theme.textSecondary }
                                }
                                Text {                                     // clear
                                    visible: searchField.text !== ""
                                    anchors.right: parent.right
                                    anchors.rightMargin: 18
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "✕"
                                    font.pixelSize: 14
                                    color: Theme.textSecondary
                                    MouseArea { anchors.fill: parent; anchors.margins: -8; cursorShape: Qt.PointingHandCursor; onClicked: searchField.clear() }
                                }
                            }

                            // only when something needs attention
                            Rectangle {
                                visible: win.updateCount() > 0
                                height: 30
                                width: updatesLabel.implicitWidth + 24
                                radius: 15
                                color: Qt.alpha(Theme.info, 0.14)
                                border.width: 1
                                border.color: Qt.alpha(Theme.info, 0.5)
                                Text { id: updatesLabel; anchors.centerIn: parent; text: Tr.s("updatesCount").replace("%1", win.updateCount()); font.pixelSize: 12; font.weight: Font.Medium; color: Theme.info }
                            }
                        }
                    }
                }

                // the sections
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 40
                    Layout.rightMargin: 40
                    Layout.topMargin: 8
                    Layout.bottomMargin: 40
                    spacing: 30

                    Text {
                        objectName: "noResults"
                        visible: win.totalMatches() === 0
                        Layout.fillWidth: true
                        text: Tr.s("noResults")
                        font.pixelSize: 15
                        color: Theme.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        topPadding: 40
                    }

                    Repeater {
                        model: win.sections
                        delegate: ColumnLayout {
                            id: block
                            required property var modelData
                            objectName: "section_" + modelData.key
                            visible: win.countIn(modelData.key) > 0
                            Layout.fillWidth: true
                            spacing: 16

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                Text { text: Tr.s("sec_" + block.modelData.key); font.pixelSize: 19; font.weight: Font.Bold; color: Theme.text }
                                Rectangle {
                                    height: 22
                                    width: countText.implicitWidth + 16
                                    radius: 11
                                    color: Theme.surface
                                    Text { id: countText; anchors.centerIn: parent; text: win.countIn(block.modelData.key); font.pixelSize: 12; font.weight: Font.Medium; color: Theme.textSecondary }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderSubtle }
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: 22
                                Repeater {
                                    model: Hub.products
                                    delegate: ProductCard {
                                        required property var modelData
                                        product: modelData
                                        visible: modelData.section === block.modelData.key && win.matches(modelData)
                                        equalHeight: win.cardHeight
                                        onNeededHeightChanged: win.setNeeded(modelData.key, neededHeight)
                                        Component.onCompleted: win.setNeeded(modelData.key, neededHeight)
                                        onUninstallRequested: p => { confirm.product = p; confirm.open() }
                                        onBrowseRequested: setupDialog.open()
                                        onError: text => win.showNotice(text)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- status line ---------------------------------------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 36
            color: "#0D0E11"
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.borderSubtle }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 28
                anchors.rightMargin: 24
                spacing: 10
                BusyIndicator { running: Hub.busy || Hub.checking; visible: Hub.busy || Hub.checking; implicitWidth: 18; implicitHeight: 18 }
                ProgressBar {
                    objectName: "downloadProgress"
                    visible: Hub.downloadProgress >= 0
                    value: Hub.downloadProgress
                    implicitWidth: 140
                }
                Text {
                    objectName: "statusText"
                    Layout.fillWidth: true
                    text: Hub.busy || Hub.checking ? Hub.message : win.notice
                    font.pixelSize: 12
                    color: Hub.busy || Hub.checking ? Theme.accent : Theme.textSecondary
                    elide: Text.ElideRight
                }
                Text { text: "v" + Hub.version; font.pixelSize: 12; color: Theme.textDisabled }
            }
        }
    }
}
