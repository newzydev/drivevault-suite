import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

ScrollView {
    id: home
    required property var tools
    signal toolRequested(string key)
    signal openPathRequested(string path)
    signal folderOpenFailed()
    readonly property string recentQuery: recentSearch.text.trim().toLowerCase()
    readonly property var matchingFiles: Doc.recentFiles.filter(path => path.toLowerCase().includes(recentQuery))

    contentWidth: availableWidth
    contentHeight: dashboard.y + dashboard.implicitHeight + dashboard.inset
    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    ColumnLayout {
        id: dashboard
        width: Math.min(1280, home.availableWidth - 2 * inset)
        readonly property int inset: home.availableWidth < 800 ? 20 : 32
        x: Math.round((home.availableWidth - width) / 2)
        y: inset
        spacing: 14

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: openRow.implicitHeight + 48
            radius: 20
            border.color: Theme.light ? "#e6ccd0" : "#58363d"
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.light ? "#fbe7e9" : "#3c232a" }
                GradientStop { position: 1; color: Theme.light ? "#fff8f8" : "#222128" }
            }

            RowLayout {
                id: openRow
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 28
                anchors.topMargin: 24
                anchors.bottomMargin: 24
                spacing: 20
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    AppText {
                        Layout.fillWidth: true
                        text: "DriveVault PDF Pro"
                        font.pixelSize: Theme.fontHero
                        font.weight: Font.Bold
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14
                        PButton {
                            objectName: "homeOpen"
                            text: Tr.s("openFile")
                            variant: "primary"
                            enabled: !Doc.outputBusy
                            onClicked: home.toolRequested("openFile")
                        }
                        AppText {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: Tr.s("dropHint")
                            color: Theme.textSecondary
                        }
                    }
                }
                Item {
                    visible: dashboard.width >= 640
                    Layout.preferredWidth: 154
                    Layout.preferredHeight: 104
                    Rectangle {
                        x: 70; y: 6; width: 68; height: 88
                        radius: 12
                        rotation: 13
                        color: Theme.light ? "#ecd3e4" : "#463645"
                        border.color: Theme.light ? "#d3a6c1" : "#6a4e60"
                    }
                    Rectangle {
                        x: 38; y: 9; width: 71; height: 90
                        radius: 12
                        rotation: -9
                        color: Theme.light ? "#fff5f7" : "#402e39"
                        border.color: Theme.light ? "#c37a8c" : "#9a6371"
                        Column {
                            x: 13; y: 15; spacing: 9
                            LineIcon { name: "page"; size: 24; color: Theme.light ? "#a83a55" : "#ffc0c9" }
                            Rectangle { width: 43; height: 3; radius: 2; color: "#c48090" }
                            Rectangle { width: 29; height: 3; radius: 2; color: "#936273" }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            spacing: 8
            AppText {
                Layout.fillWidth: true
                text: Tr.s("recommendedTools")
                font.pixelSize: Theme.fontXl
                font.weight: Font.DemiBold
            }
            PButton { objectName: "homeBatch"; compact: true; text: Tr.s("batchPdf"); enabled: !Doc.outputBusy; onClicked: home.toolRequested("batchPdf") }
            PButton { objectName: "homeRepair"; compact: true; text: Tr.s("repairPdf"); enabled: !Doc.outputBusy; onClicked: home.toolRequested("repairPdf") }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: dashboard.width >= 1030 ? 6 : dashboard.width >= 635 ? 3 : 2
            uniformCellWidths: true
            columnSpacing: 10
            rowSpacing: 10
            Repeater {
                model: home.tools
                delegate: ToolEntry {
                    required property var modelData
                    objectName: tool.key === "toolMerge" ? "homeMerge" : "recommended_" + tool.key
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    tool: modelData
                    card: true
                    enabled: !Doc.outputBusy
                    onClicked: home.toolRequested(tool.key)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 10
            AppText {
                Layout.fillWidth: true
                text: Tr.s("recent")
                font.pixelSize: Theme.fontXl
                font.weight: Font.DemiBold
            }
            TextField {
                id: recentSearch
                objectName: "recentSearch"
                visible: Doc.recentFiles.length > 0
                Layout.preferredWidth: Math.min(320, dashboard.width / 2)
                implicitHeight: 38
                leftPadding: 12
                rightPadding: 12
                selectByMouse: true
                placeholderText: Tr.s("searchRecent")
                Accessible.name: placeholderText
                color: Theme.text
                placeholderTextColor: Theme.textSecondary
                background: Rectangle {
                    radius: Theme.radius
                    color: Theme.toolCard
                    border.color: recentSearch.activeFocus ? Theme.accentText : Theme.border
                }
                Keys.onEscapePressed: clear()
            }
        }
        AppText {
            visible: home.matchingFiles.length === 0
            Layout.bottomMargin: 12
            text: Doc.recentFiles.length === 0 ? Tr.s("noRecent") : Tr.s("noMatchingRecent")
            color: Theme.textSecondary
        }
        Repeater {
            model: home.matchingFiles
            delegate: Rectangle {
                id: recentRow
                required property string modelData
                required property int index
                objectName: "recent_" + index
                Layout.fillWidth: true
                implicitHeight: 58
                radius: Theme.radius
                color: openRecent.hovered ? Theme.surfaceHover : Theme.toolCard
                border.color: openRecent.visualFocus ? Theme.accentText : Theme.borderSubtle
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 8
                    spacing: 12
                    ItemDelegate {
                        id: openRecent
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: recentRow.modelData.split(/[\\/]/).pop()
                        enabled: !Doc.outputBusy
                        hoverEnabled: true
                        background: null
                        onClicked: home.openPathRequested(recentRow.modelData)
                        HoverHandler { cursorShape: openRecent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                        ToolTip.visible: hovered && recentPath.truncated
                        ToolTip.delay: 500
                        ToolTip.text: recentRow.modelData
                        contentItem: RowLayout {
                            spacing: 12
                            Rectangle {
                                Layout.preferredWidth: 34
                                Layout.preferredHeight: 34
                                radius: 9
                                color: Theme.accentSubtle
                                LineIcon { anchors.centerIn: parent; name: "page"; color: Theme.accentText }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                AppText { Layout.fillWidth: true; text: openRecent.text; elide: Text.ElideRight; font.weight: Font.Medium }
                                AppText {
                                    id: recentPath
                                    Layout.fillWidth: true
                                    text: recentRow.modelData
                                    elide: Text.ElideMiddle
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSm
                                }
                            }
                        }
                    }
                    PButton {
                        compact: true
                        variant: "ghost"
                        glyph: "folder"
                        tip: Tr.s("openRecentFolder")
                        onClicked: {
                            if (!Doc.openRecentFolder(recentRow.modelData))
                                home.folderOpenFailed()
                        }
                    }
                    PButton {
                        compact: true
                        variant: "ghost"
                        glyph: "close"
                        tip: Tr.s("removeRecent")
                        onClicked: {
                            Doc.removeRecent(recentRow.modelData)
                            if (Doc.recentFiles.length === 0)
                                recentSearch.clear()
                        }
                    }
                }
            }
        }
    }
}
