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

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: openRow.implicitHeight + 36
            radius: 20
            border.color: Theme.light ? "#e5bfcd" : "#63404b"
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.light ? "#fbe2e7" : "#48262f" }
                GradientStop { position: 0.58; color: Theme.light ? "#f7e7ef" : "#34232e" }
                GradientStop { position: 1; color: Theme.light ? "#f0e9f4" : "#26232d" }
            }

            RowLayout {
                id: openRow
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 28
                anchors.topMargin: 18
                anchors.bottomMargin: 18
                spacing: 20
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    AppText {
                        Layout.fillWidth: true
                        text: Tr.s("welcome")
                        font.pixelSize: 29
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
            columns: dashboard.width >= 1030 ? 6 : 3
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
