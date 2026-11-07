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

        // ---- header ---------------------------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 84
            color: Theme.backgroundAlt
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 28
                anchors.rightMargin: 24
                spacing: 16
                Image { source: "qrc:/qt/qml/DriveVault/Master/resources/icons/app.png"; Layout.preferredWidth: 48; Layout.preferredHeight: 48; sourceSize.width: 96; smooth: true }
                ColumnLayout {
                    spacing: 2
                    Text { text: Tr.s("title"); font.pixelSize: 22; font.weight: Font.Bold; color: Theme.text }
                    Text { text: Tr.s("subtitle"); font.pixelSize: 13; color: Theme.textSecondary }
                }
                Item { Layout.fillWidth: true }
                Text { text: Tr.s("language"); font.pixelSize: 12; color: Theme.textDisabled }
                MButton { objectName: "langTh"; text: "ไทย"; variant: Hub.language === "th" ? "primary" : "secondary"; implicitWidth: 56; leftPadding: 8; rightPadding: 8; onClicked: Hub.language = "th" }
                MButton { objectName: "langEn"; text: "EN"; variant: Hub.language === "en" ? "primary" : "secondary"; implicitWidth: 48; leftPadding: 8; rightPadding: 8; onClicked: Hub.language = "en" }
                MButton {
                    objectName: "checkUpdates"
                    text: Hub.checking ? Tr.s("checking") : Tr.s("checkUpdates")
                    enabled: !Hub.busy && !Hub.checking
                    onClicked: { const e = Hub.checkForUpdates(); if (e !== "") win.showNotice(e) }
                }
                MButton { objectName: "refresh"; text: Tr.s("refresh"); onClicked: Hub.refresh() }
                MButton { objectName: "about"; text: Tr.s("about"); variant: "ghost"; onClicked: aboutDialog.open() }
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

        // ---- programs ------------------------------------------------------------------------------------------------
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: grid.implicitHeight + 64
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                x: 28
                y: 24
                width: parent.width - 56
                spacing: 16
                Text { text: Tr.s("programs"); font.pixelSize: 12; font.weight: Font.DemiBold; font.capitalization: Font.AllUppercase; font.letterSpacing: 1; color: Theme.textDisabled }
                Flow {
                    id: grid
                    Layout.fillWidth: true
                    spacing: 20
                    Repeater {
                        model: Hub.products
                        delegate: ProductCard {
                            required property var modelData
                            product: modelData
                            onUninstallRequested: p => { confirm.product = p; confirm.open() }
                            onBrowseRequested: setupDialog.open()
                            onError: text => win.showNotice(text)
                        }
                    }
                }
            }
        }

        // ---- installer folders + status ----------------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: footer.implicitHeight + 24
            color: Theme.backgroundAlt
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.borderSubtle }
            ColumnLayout {
                id: footer
                anchors.fill: parent
                anchors.margins: 12
                anchors.leftMargin: 28
                anchors.rightMargin: 24
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text { text: Tr.s("installers"); font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.textSecondary }
                    Text { Layout.fillWidth: true; text: Hub.installerFolders.join("   ·   "); font.pixelSize: 12; color: Theme.textDisabled; elide: Text.ElideMiddle }
                    MButton { objectName: "addFolder"; text: Tr.s("addFolder"); implicitHeight: 28; onClicked: folderDialog.open() }
                    MButton { objectName: "openInstallers"; text: Tr.s("openInstallers"); implicitHeight: 28; onClicked: Hub.openInstallersFolder() }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    BusyIndicator { running: Hub.busy || Hub.checking; visible: Hub.busy || Hub.checking; implicitWidth: 20; implicitHeight: 20 }
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
                        font.pixelSize: 13
                        color: Hub.busy || Hub.checking ? Theme.accent : Theme.textSecondary
                        elide: Text.ElideRight
                    }
                    CheckBox {
                        id: autoCheckBox
                        objectName: "autoCheck"
                        checked: Hub.autoCheck
                        onToggled: Hub.autoCheck = checked
                        padding: 0
                        spacing: 8
                        contentItem: Text {
                            leftPadding: autoCheckBox.indicator.width + autoCheckBox.spacing
                            text: Tr.s("autoCheck")
                            font.pixelSize: 12
                            color: Theme.textSecondary
                            verticalAlignment: Text.AlignVCenter
                        }
                        indicator: Rectangle {
                            implicitWidth: 18; implicitHeight: 18
                            y: (autoCheckBox.height - height) / 2
                            radius: 5
                            color: autoCheckBox.checked ? Theme.accent : "transparent"
                            border.width: 1
                            border.color: autoCheckBox.checked ? Theme.accent : Theme.border
                            Text { anchors.centerIn: parent; visible: autoCheckBox.checked; text: "✓"; font.pixelSize: 12; font.weight: Font.Bold; color: Theme.readableOn(Theme.accent) }
                        }
                    }
                    Text { text: "v" + Hub.version; font.pixelSize: 12; color: Theme.textDisabled }
                }
            }
        }
    }
}
