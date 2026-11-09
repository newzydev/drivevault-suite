import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

// One media item (thumbnail cell or list row). Drag it onto the timeline, double-click to add at the playhead.
Item {
    id: cell
    objectName: "media_" + name

    required property int index
    required property string mediaId
    required property string name
    required property string kind
    required property string status
    required property string thumbnail
    required property string durationText
    required property string infoText
    required property string errorText

    property bool listMode: false
    readonly property bool selected: App.selectedMediaId === mediaId
    readonly property bool usable: status === "ready"

    signal addRequested()
    signal contextRequested()

    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: Theme.radiusSmall + 2
        color: cell.selected ? Theme.accentSubtle : (area.containsMouse ? Theme.surfaceHover : "transparent")
        border.width: cell.selected ? 1 : 0
        border.color: Theme.accent
    }

    GridLayout {
        anchors.fill: parent
        anchors.margins: 6
        columns: cell.listMode ? 2 : 1
        columnSpacing: 10
        rowSpacing: 4

        // ---- thumbnail --------------------------------------------------------------------------
        Rectangle {
            id: thumb
            Layout.preferredWidth: cell.listMode ? 76 : -1
            Layout.fillWidth: !cell.listMode
            Layout.preferredHeight: cell.listMode ? 43 : -1
            Layout.fillHeight: !cell.listMode
            radius: Theme.radiusSmall
            color: cell.kind === "audio" ? Theme.audioTrack : Theme.surfaceElevated
            clip: true

            Image {
                anchors.fill: parent
                visible: cell.thumbnail !== "" && cell.usable
                source: cell.thumbnail
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                sourceSize.width: 256
            }
            Icon {
                anchors.centerIn: parent
                visible: cell.usable && cell.thumbnail === ""
                name: cell.kind === "audio" ? "audio" : cell.kind === "image" ? "image" : "film"
                size: 22
                color: Theme.textSecondary
            }
            // importing spinner
            Icon {
                id: spinner
                anchors.centerIn: parent
                visible: cell.status === "importing"
                name: "refresh"
                size: 20
                color: Theme.accentText
                RotationAnimator on rotation { running: spinner.visible; from: 0; to: 360; duration: 1100; loops: Animation.Infinite }
            }
            // missing file
            Rectangle {
                anchors.fill: parent
                visible: cell.status === "missing"
                color: Qt.alpha(Theme.error, 0.25)
                Icon { anchors.centerIn: parent; name: "warning"; size: 22; color: Theme.error }
            }
            Rectangle {
                visible: cell.usable && cell.kind !== "image"
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 3
                width: durationLabel.implicitWidth + 8
                height: 16
                radius: 3
                color: "#B3000000"
                AppText {
                    id: durationLabel
                    anchors.centerIn: parent
                    text: cell.durationText
                    font.pixelSize: Theme.fontXs
                    color: "#FFFFFF"
                }
            }
            Icon {
                visible: cell.usable
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 4
                name: cell.kind === "audio" ? "audio" : cell.kind === "image" ? "image" : "film"
                size: 12
                color: "#FFFFFF"
                opacity: 0.85
            }
        }

        // ---- text --------------------------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: 1
            AppText {
                Layout.fillWidth: true
                text: cell.name
                font.pixelSize: Theme.fontSm
                font.weight: Font.Medium
                color: cell.status === "missing" ? Theme.error : Theme.text
                elide: Text.ElideMiddle
            }
            AppText {
                Layout.fillWidth: true
                visible: cell.listMode
                text: cell.status === "missing" ? qsTr("File not found") : cell.status === "importing" ? qsTr("Importing…") : cell.infoText
                font.pixelSize: Theme.fontXs
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
        }
    }

    // ---- interaction ---------------------------------------------------------------------------------
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        drag.target: cell.usable ? ghost : undefined
        drag.threshold: 8
        onPressed: mouse => {
            if (mouse.button === Qt.RightButton)
                return
            App.selectedMediaId = cell.mediaId
            Projects.timeline.clearSelection()
            ghost.parent = Overlay.overlay
            const p = area.mapToItem(Overlay.overlay, mouse.x, mouse.y)
            ghost.x = p.x - 24
            ghost.y = p.y - 16
        }
        onReleased: mouse => {
            if (ghost.Drag.active)
                ghost.Drag.drop()
            ghost.parent = cell
        }
        onClicked: mouse => { if (mouse.button === Qt.RightButton) { App.selectedMediaId = cell.mediaId; cell.contextRequested() } }
        onDoubleClicked: mouse => { if (cell.usable && mouse.button === Qt.LeftButton) cell.addRequested() }
    }

    AppToolTip {
        visible: area.containsMouse && !area.drag.active && !ghost.visible
        delay: 700
        text: cell.status === "error" ? cell.errorText : cell.name + (cell.infoText !== "" ? "\n" + cell.infoText : "")
    }

    // The thing that follows the pointer while dragging (reparented to the window overlay).
    Item {
        id: ghost
        property string mediaId: cell.mediaId
        width: 150
        height: 32
        z: 10000
        visible: area.drag.active
        Drag.active: area.drag.active
        Drag.keys: ["dvmedia"]
        Drag.hotSpot.x: 24
        Drag.hotSpot.y: 16
        Drag.supportedActions: Qt.CopyAction

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSmall
            color: Theme.surfaceElevated
            border.color: Theme.accent
            opacity: 0.95
            RowLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 6
                Icon { name: cell.kind === "audio" ? "audio" : cell.kind === "image" ? "image" : "film"; size: 14; color: Theme.accentText }
                AppText { Layout.fillWidth: true; text: cell.name; elide: Text.ElideMiddle; font.pixelSize: Theme.fontSm; color: Theme.text }
            }
        }
    }
}
