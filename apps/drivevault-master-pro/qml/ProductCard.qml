import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Master.Backend

// One DriveVault program: its own accent colour, status and the actions that make sense right now.
Rectangle {
    id: card

    required property var product
    signal uninstallRequested(var product)
    signal browseRequested()
    signal error(string text)

    readonly property color accent: product.accent
    readonly property string status: product.status
    readonly property string key: product.key

    objectName: "card_" + key
    implicitWidth: 320
    implicitHeight: body.implicitHeight + 104
    radius: Theme.radius
    color: Theme.backgroundAlt
    border.width: 1
    border.color: hover.hovered ? card.accent : Theme.borderSubtle
    opacity: status === "comingSoon" ? 0.75 : 1
    HoverHandler { id: hover }
    Behavior on border.color { ColorAnimation { duration: 120 } }

    // coloured header
    Rectangle {
        id: band
        width: parent.width
        height: 96
        radius: Theme.radius
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: card.accent }
            GradientStop { position: 1.0; color: Qt.darker(card.accent, 1.9) }
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
                text: card.product.name.replace("DriveVault ", "").charAt(0)
                font.pixelSize: 28
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
                readonly property bool update: card.product.updateAvailable === true
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
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8
            MButton {
                objectName: "launch_" + card.key
                visible: status === "installed"
                variant: "primary"; tint: card.accent
                text: Tr.s("launch")
                enabled: !Hub.busy
                onClicked: { const e = Hub.launch(card.product.id); if (e !== "") card.error(e) }
            }
            MButton {
                objectName: "update_" + card.key
                visible: card.product.updateAvailable === true
                variant: "secondary"
                text: Tr.s("updateBtn")
                enabled: !Hub.busy
                onClicked: { const e = Hub.installProduct(card.product.id); if (e !== "") card.error(e) }
            }
            MButton {
                objectName: "install_" + card.key
                visible: (status === "notInstalled" || status === "broken") && card.product.installerPath !== ""
                variant: "primary"; tint: card.accent
                text: Tr.s("install")
                enabled: !Hub.busy
                onClicked: { const e = Hub.installProduct(card.product.id); if (e !== "") card.error(e) }
            }
            MButton {
                objectName: "browse_" + card.key
                visible: (status === "notInstalled" || status === "broken") && card.product.installerPath === ""
                text: Tr.s("browse")
                onClicked: card.browseRequested()
            }
            MButton {
                objectName: "folder_" + card.key
                visible: status === "installed"
                text: Tr.s("folder")
                onClicked: { const e = Hub.openFolder(card.product.id); if (e !== "") card.error(e) }
            }
            MButton {
                objectName: "uninstall_" + card.key
                visible: card.product.canUninstall === true
                variant: "danger"
                text: Tr.s("uninstall")
                enabled: !Hub.busy
                onClicked: card.uninstallRequested(card.product)
            }
        }
    }
}
