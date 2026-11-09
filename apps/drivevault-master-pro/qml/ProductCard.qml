import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Master.Backend

// One DriveVault program: its own accent colour, status and the actions that make sense right now.
// Layout: one full-width main action per row (the first is the recommended one), then Folder / Uninstall side by side.
Rectangle {
    id: card

    required property var product
    signal uninstallRequested(var product)
    signal browseRequested()
    signal error(string text)

    readonly property color accent: product.accent
    readonly property string status: product.status
    readonly property string key: product.key
    readonly property bool hasUpdate: product.updateAvailable === true || product.onlineUpdate === true

    objectName: "card_" + key
    implicitWidth: 320
    // every card is as tall as the tallest one (Main sets equalHeight from the neededHeight of all cards)
    readonly property real neededHeight: body.y + body.implicitHeight + 20
    property real equalHeight: 0
    implicitHeight: Math.max(neededHeight, equalHeight)
    radius: Theme.radius + 2
    color: Theme.backgroundAlt
    border.width: 1
    border.color: hover.hovered ? card.accent : Theme.borderSubtle
    opacity: status === "comingSoon" ? 0.75 : 1
    HoverHandler { id: hover }
    transform: Translate {                                                   // the card lifts a little under the mouse
        y: hover.hovered && card.status !== "comingSoon" ? -3 : 0
        Behavior on y { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
    }
    Behavior on border.color { ColorAnimation { duration: 120 } }

    // coloured header
    Rectangle {
        id: band
        width: parent.width
        height: 96
        radius: card.radius
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: card.accent }
            GradientStop { position: 1.0; color: Qt.darker(card.accent, 1.9) }
        }
        Rectangle {                                                          // soft light on the upper half
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height * 0.6
            radius: card.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.16) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
            }
        }
        Rectangle {
            x: 20
            anchors.verticalCenter: parent.verticalCenter
            width: 56; height: 56; radius: 14
            color: Qt.rgba(1, 1, 1, 0.22)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.45)
            Text {
                anchors.centerIn: parent
                text: card.product.glyph
                font.pixelSize: card.product.glyph.length > 2 ? 17 : 28
                font.weight: Font.Bold
                color: "#FFFFFF"
            }
        }
        Text {
            x: 92
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 108
            text: card.product.name.replace("DriveVault ", "")
            font.pixelSize: 18
            font.weight: Font.DemiBold
            color: "#FFFFFF"
            wrapMode: Text.Wrap
            maximumLineCount: 2
        }
    }

    ColumnLayout {
        id: body
        x: 20
        y: 112
        width: parent.width - 40
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                id: chip
                objectName: "status_" + card.key
                readonly property bool update: card.hasUpdate
                implicitWidth: chipText.implicitWidth + 20
                implicitHeight: 24
                radius: 12
                color: Qt.alpha(tone, 0.16)
                border.width: 1
                border.color: Qt.alpha(tone, 0.6)
                readonly property color tone: update ? Theme.info : status === "installed" ? Theme.success : status === "broken" ? Theme.error
                                              : status === "comingSoon" ? Theme.textDisabled : Theme.textSecondary
                Text {
                    id: chipText
                    anchors.centerIn: parent
                    text: chip.update ? Tr.s("update") : Tr.s(status)
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    color: chip.tone
                }
            }
            Text {
                visible: card.product.installed === true
                text: Tr.s("version") + " " + card.product.version
                font.pixelSize: 12
                color: Theme.textSecondary
            }
            Item { Layout.fillWidth: true }
        }

        Text {
            Layout.fillWidth: true
            Layout.minimumHeight: 54
            text: card.product.description
            font.pixelSize: 13
            color: Theme.textSecondary
            wrapMode: Text.Wrap
            lineHeightMode: Text.FixedHeight
            lineHeight: thaiLine.implicitHeight                 // every language uses the height of a Thai line
            Text { id: thaiLine; visible: false; text: "ก"; font: parent.font }
            maximumLineCount: 3
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            visible: status === "notInstalled" || status === "broken"
            text: card.product.installerPath !== "" ? Tr.s("installerFound").replace("%1", card.product.installerVersion) : Tr.s("noInstaller")
            font.pixelSize: 12
            color: card.product.installerPath !== "" ? Theme.success : Theme.textDisabled
            wrapMode: Text.Wrap
            lineHeightMode: Text.FixedHeight
            lineHeight: thaiLine2.implicitHeight                 // every language uses the height of a Thai line
            Text { id: thaiLine2; visible: false; text: "ก"; font: parent.font }
        }

        // ---- main actions (full width) -------------------------------------------------------------------------------------------
        MButton {
            objectName: "onlineUpdate_" + card.key
            Layout.fillWidth: true
            visible: card.product.onlineUpdate === true
            variant: "primary"; tint: card.accent
            text: Tr.s("onlineUpdate").replace("%1", card.product.onlineVersion)
            enabled: !Hub.busy && !Hub.checking
            onClicked: { const e = Hub.downloadAndInstall(card.product.id); if (e !== "") card.error(e) }
        }
        MButton {
            objectName: "update_" + card.key
            Layout.fillWidth: true
            visible: card.product.updateAvailable === true
            variant: card.product.onlineUpdate === true ? "secondary" : "primary"; tint: card.accent
            text: Tr.s("updateBtn")
            enabled: !Hub.busy
            onClicked: { const e = Hub.installProduct(card.product.id); if (e !== "") card.error(e) }
        }
        MButton {
            objectName: "onlineInstall_" + card.key
            Layout.fillWidth: true
            visible: card.product.onlineInstall === true
            variant: card.product.installerPath === "" ? "primary" : "secondary"; tint: card.accent
            text: Tr.s("onlineInstall").replace("%1", card.product.onlineVersion)
            enabled: !Hub.busy && !Hub.checking
            onClicked: { const e = Hub.downloadAndInstall(card.product.id); if (e !== "") card.error(e) }
        }
        MButton {
            objectName: "install_" + card.key
            Layout.fillWidth: true
            visible: (status === "notInstalled" || status === "broken") && card.product.installerPath !== ""
            variant: "primary"; tint: card.accent
            text: Tr.s("install")
            enabled: !Hub.busy
            onClicked: { const e = Hub.installProduct(card.product.id); if (e !== "") card.error(e) }
        }
        RowLayout {                                                       // open + a quiet "more" button (folder, uninstall)
            Layout.fillWidth: true
            visible: status === "installed"
            spacing: 8
            MButton {
                objectName: "launch_" + card.key
                Layout.fillWidth: true
                variant: card.hasUpdate ? "secondary" : "primary"; tint: card.accent
                text: Tr.s("launch")
                enabled: !Hub.busy
                onClicked: { const e = Hub.launch(card.product.id); if (e !== "") card.error(e) }
            }
            MButton {
                objectName: "more_" + card.key
                variant: "secondary"
                Layout.preferredWidth: 40
                leftPadding: 0; rightPadding: 0
                text: "⋯"
                font.pixelSize: 18
                onClicked: moreMenu.visible ? moreMenu.close() : moreMenu.popup(0, height + 4)
                MMenu {
                    id: moreMenu
                    implicitWidth: 220
                    MMenuItem {
                        objectName: "folder_" + card.key
                        text: Tr.s("folder")
                        onTriggered: { const e = Hub.openFolder(card.product.id); if (e !== "") card.error(e) }
                    }
                    MMenuItem {
                        objectName: "uninstall_" + card.key
                        danger: true
                        text: Tr.s("uninstall")
                        enabled: card.product.canUninstall === true && !Hub.busy
                        onTriggered: card.uninstallRequested(card.product)
                    }
                }
            }
        }
        MButton {
            objectName: "browse_" + card.key
            Layout.fillWidth: true
            visible: (status === "notInstalled" || status === "broken") && card.product.installerPath === ""
            text: Tr.s("browse")
            onClicked: card.browseRequested()
        }
    }
}
