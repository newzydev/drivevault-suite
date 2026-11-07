import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.Backend

// Project media browser: import (dialogs and drag & drop from Explorer), search, sort, thumbnails / list,
// relink of missing files. Drag items onto the timeline or double-click to add them.
Rectangle {
    id: root

    property bool gridMode: AppSettings.state("media.grid", true)
    readonly property var library: Projects.media
    readonly property var view: Projects.mediaView

    function openImport() { importDialog.open() }
    function addToTimeline(mediaId) {
        const err = Projects.timeline.addMedia(mediaId, -1, Projects.playheadFrame)
        if (err !== "")
            App.toast(err, "warning")
    }
    function setGrid(value) {
        gridMode = value
        AppSettings.setState("media.grid", value)
    }

    color: Theme.backgroundAlt
    radius: Theme.radius
    border.color: dropArea.containsDrag ? Theme.accent : Theme.borderSubtle
    border.width: dropArea.containsDrag ? 2 : 1
    clip: true

    FileDialog {
        id: importDialog
        title: qsTr("Import media")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Media files (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mpg *.mpeg *.wmv *.mp3 *.wav *.aac *.flac *.m4a *.ogg *.png *.jpg *.jpeg *.webp *.bmp)"),
                      qsTr("All files (*)")]
        onAccepted: root.library.importUrls(selectedFiles)
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Import a folder")
        onAccepted: {
            const n = root.library.importFolder(selectedFolder.toString())
            if (n === 0)
                App.toast(qsTr("No media files were found in this folder."), "info")
        }
    }
    FileDialog {
        id: relinkDialog
        property string mediaId
        title: qsTr("Locate the missing file")
        onAccepted: {
            const err = root.library.relink(mediaId, selectedFile.toString())
            App.toast(err === "" ? qsTr("Media relinked.") : err, err === "" ? "success" : "error")
        }
    }
    FolderDialog {
        id: relinkFolderDialog
        title: qsTr("Search a folder for the missing files")
        onAccepted: {
            const n = root.library.relinkInFolder(selectedFolder.toString())
            App.toast(n > 0 ? qsTr("%n file(s) relinked.", "", n) : qsTr("No matching files were found."), n > 0 ? "success" : "warning")
        }
    }

    // Files dragged in from Explorer
    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: drop => {
            if (drop.hasUrls) {
                drop.accept()
                root.library.importUrls(drop.urls)
            }
        }
    }

    AppMenu {
        id: contextMenu
        property string mediaId
        property string status
        property string path
        AppMenuItem { text: qsTr("Add to Timeline"); enabled: contextMenu.status === "ready"; onTriggered: root.addToTimeline(contextMenu.mediaId) }
        AppMenuItem { text: qsTr("Show in Explorer"); onTriggered: SysInfo.showInFolder(contextMenu.path) }
        AppMenuItem {
            text: qsTr("Relink…")
            enabled: contextMenu.status === "missing"
            onTriggered: { relinkDialog.mediaId = contextMenu.mediaId; relinkDialog.open() }
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: qsTr("Remove from Project")
            onTriggered: {
                const err = Projects.removeMedia(contextMenu.mediaId)
                if (err !== "")
                    App.toast(err, "warning")
                else if (App.selectedMediaId === contextMenu.mediaId)
                    App.selectedMediaId = ""
            }
        }
    }

    AppMenu {
        id: sortMenu
        Repeater {
            model: [ { key: "added", label: qsTr("Date added") }, { key: "name", label: qsTr("Name") },
                     { key: "duration", label: qsTr("Duration") }, { key: "kind", label: qsTr("Type") } ]
            delegate: AppMenuItem {
                required property var modelData
                text: modelData.label
                checkable: true
                checked: root.view.sortKey === modelData.key
                onTriggered: root.view.sortKey = modelData.key
            }
        }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Ascending"); checkable: true; checked: root.view.ascending; onTriggered: root.view.ascending = !root.view.ascending }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PanelHeader {
            Layout.fillWidth: true
            title: qsTr("Project Media")
            iconName: "folder"
            IconButton { iconName: "grid"; checkable: true; checked: root.gridMode; tip: qsTr("Thumbnail view"); onClicked: root.setGrid(true) }
            IconButton { iconName: "list"; checkable: true; checked: !root.gridMode; tip: qsTr("List view"); onClicked: root.setGrid(false) }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            Layout.bottomMargin: 6
            spacing: 6
            AppTextField {
                Layout.fillWidth: true
                iconName: "search"
                placeholderText: qsTr("Search media")
                text: root.view.filterText
                onTextEdited: root.view.filterText = text
            }
            IconButton { iconName: "sliders"; subtle: false; tip: qsTr("Sort"); onClicked: sortMenu.popup() }
            IconButton { iconName: "import"; subtle: false; tip: qsTr("Import files…") + "  (Ctrl+I)"; onClicked: importDialog.open() }
            IconButton { iconName: "folder"; subtle: false; tip: qsTr("Import a folder…"); onClicked: folderDialog.open() }
        }

        // Missing media banner
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            visible: root.library.missingCount > 0
            implicitHeight: banner.implicitHeight + 14
            radius: Theme.radiusSmall + 2
            color: Qt.alpha(Theme.error, 0.12)
            border.color: Qt.alpha(Theme.error, 0.5)
            RowLayout {
                id: banner
                anchors.fill: parent
                anchors.margins: 7
                spacing: 6
                Icon { name: "warning"; size: 15; color: Theme.error }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("%n media file(s) are missing.", "", root.library.missingCount)
                    font.pixelSize: Theme.fontSm
                    color: Theme.text
                    wrapMode: Text.Wrap
                }
                AppButton { text: qsTr("Search folder…"); implicitHeight: 26; font.pixelSize: Theme.fontSm; onClicked: relinkFolderDialog.open() }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            EmptyState {
                anchors.centerIn: parent
                width: parent.width - 32
                maxTextWidth: parent.width - 48
                visible: root.library.count === 0
                iconName: "film"
                title: qsTr("No media yet")
                description: qsTr("Import video, audio or images, or drag files here from Explorer.")
                AppButton {
                    Layout.alignment: Qt.AlignHCenter
                    variant: "primary"
                    iconName: "import"
                    text: qsTr("Import media…")
                    onClicked: importDialog.open()
                }
            }

            Text {
                anchors.centerIn: parent
                visible: root.library.count > 0 && root.view.count === 0
                text: qsTr("No media matches your search.")
                font.pixelSize: Theme.fontMd
                color: Theme.textSecondary
            }

            GridView {
                id: grid
                anchors.fill: parent
                anchors.margins: 6
                visible: root.gridMode && root.library.count > 0
                clip: true
                model: root.view
                readonly property int columns: Math.max(1, Math.floor(width / 120))
                cellWidth: Math.floor(width / columns)
                cellHeight: 104
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}
                delegate: MediaItemView {
                    width: grid.cellWidth
                    height: grid.cellHeight
                    onAddRequested: root.addToTimeline(mediaId)
                    onContextRequested: root.showContext(mediaId, status)
                }
            }

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 6
                visible: !root.gridMode && root.library.count > 0
                clip: true
                model: root.view
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}
                delegate: MediaItemView {
                    width: list.width - 6
                    height: 56
                    listMode: true
                    onAddRequested: root.addToTimeline(mediaId)
                    onContextRequested: root.showContext(mediaId, status)
                }
            }
        }

        // Footer: busy indicator
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 24
            color: "transparent"
            visible: root.library.busyCount > 0
            RowLayout {
                anchors.centerIn: parent
                spacing: 6
                Icon { id: busyIcon; name: "refresh"; size: 13; color: Theme.accentText
                       RotationAnimator on rotation { running: busyIcon.visible; from: 0; to: 360; duration: 1100; loops: Animation.Infinite } }
                Text { text: qsTr("Processing media…"); font.pixelSize: Theme.fontXs; color: Theme.textSecondary }
            }
        }
    }

    function showContext(mediaId, status) {
        contextMenu.mediaId = mediaId
        contextMenu.status = status
        contextMenu.path = root.library.describe(mediaId).path
        contextMenu.popup()
    }
}
