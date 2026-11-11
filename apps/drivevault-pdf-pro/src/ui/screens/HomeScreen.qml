import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

ScrollView {
    id: home
    required property var tools
    signal toolRequested(string key)
    signal openPathRequested(string path)

    contentWidth: availableWidth
    contentHeight: dashboard.y + dashboard.implicitHeight
    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    ColumnLayout {
        id: dashboard
        width: home.availableWidth - 72
        x: 36
        y: 28
        spacing: 16

        AppText {
            Layout.fillWidth: true
            text: Tr.s("welcome")
            wrapMode: Text.Wrap
            font.pixelSize: 30
            font.weight: Font.Bold
        }
        AppText {
            Layout.fillWidth: true
            Layout.topMargin: -10
            text: Tr.s("tagline")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLg
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 6
            implicitHeight: shortcuts.implicitHeight + 36
            radius: Theme.radiusLarge
            color: Theme.surface
            border.color: Theme.border

            ColumnLayout {
                id: shortcuts
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12
                AppText { text: Tr.s("recommendedTools"); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold }
                GridLayout {
                    Layout.fillWidth: true
                    columns: Math.max(2, Math.min(home.tools.length, Math.floor(width / 158)))
                    columnSpacing: 10
                    rowSpacing: 10
                    Repeater {
                        model: home.tools
                        delegate: ToolEntry {
                            required property var modelData
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
                    spacing: 12
                    ToolEntry {
                        objectName: "homeOpen"
                        Layout.preferredWidth: 152
                        tool: ({ key: "openFile", icon: "folder", tint: Theme.text })
                        card: true
                        enabled: !Doc.outputBusy
                        onClicked: home.toolRequested(tool.key)
                    }
                    AppText {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: Tr.s("dropHint")
                        color: Theme.textSecondary
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    PButton { objectName: "homeMerge"; compact: true; text: Tr.s("toolMerge"); enabled: !Doc.outputBusy; onClicked: home.toolRequested("toolMerge") }
                    PButton { objectName: "homeBatch"; compact: true; text: Tr.s("batchPdf"); enabled: !Doc.outputBusy; onClicked: home.toolRequested("batchPdf") }
                    PButton { objectName: "homeRepair"; compact: true; text: Tr.s("repairPdf"); enabled: !Doc.outputBusy; onClicked: home.toolRequested("repairPdf") }
                }
            }
        }

        AppText {
            Layout.topMargin: 10
            text: Tr.s("recent")
            font.pixelSize: Theme.fontXl
            font.weight: Font.DemiBold
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
        AppText {
            visible: Doc.recentFiles.length === 0
            text: Tr.s("noRecent")
            color: Theme.textSecondary
        }
        Repeater {
            model: Doc.recentFiles.slice(0, 6)
            delegate: Rectangle {
                id: recentRow
                required property string modelData
                required property int index
                objectName: "recent_" + index
                Layout.fillWidth: true
                implicitHeight: 48
                radius: Theme.radiusSmall
                color: recentHover.hovered ? Theme.surfaceHover : "transparent"
                HoverHandler { id: recentHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: home.openPathRequested(recentRow.modelData) }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 8
                    spacing: 12
                    LineIcon { name: "page"; color: Theme.accentText }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        AppText { Layout.fillWidth: true; text: recentRow.modelData.split(/[\\/]/).pop(); elide: Text.ElideRight; font.weight: Font.Medium }
                        AppText { Layout.fillWidth: true; text: recentRow.modelData; elide: Text.ElideMiddle; color: Theme.textSecondary; font.pixelSize: Theme.fontSm }
                    }
                    PButton {
                        compact: true
                        variant: "ghost"
                        glyph: "close"
                        tip: Tr.s("removeRecent")
                        onClicked: Doc.removeRecent(recentRow.modelData)
                    }
                }
            }
        }
        Item { Layout.preferredHeight: 32 }
    }
}
