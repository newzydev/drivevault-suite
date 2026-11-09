import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// What is shown when no file is open: the open button, a drop hint and the recent files.
Item {
    id: home

    signal openRequested()
    signal mergeRequested()
    signal batchRequested()
    signal repairRequested()
    signal openPathRequested(string path)

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#2A1313" }
            GradientStop { position: 0.55; color: "#17110F" }
            GradientStop { position: 1.0; color: Theme.background }
        }
        Rectangle { width: 460; height: 460; radius: 230; x: parent.width - 330; y: -250; color: Qt.alpha(Theme.accent, 0.12) }
        Rectangle { width: 300; height: 300; radius: 150; x: -90; y: parent.height - 200; color: Qt.alpha(Theme.accent, 0.06) }
    }

    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(640, parent.width - 64)
        spacing: 12

        Image {
            Layout.alignment: Qt.AlignHCenter
            source: "qrc:/icons/app.png"
            sourceSize.width: 192
            Layout.preferredWidth: 96
            Layout.preferredHeight: 96
            smooth: true
        }
        AppText {
            Layout.fillWidth: true
            text: Tr.s("title")
            font.pixelSize: Theme.fontHero
            font.weight: Font.Bold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            Layout.minimumHeight: titleMeasure.implicitHeight
            AppText { id: titleMeasure; visible: false; text: "ก"; font: parent.font }
        }
        AppText {
            Layout.fillWidth: true
            text: Tr.s("tagline")
            color: Theme.accentText
            font.pixelSize: Theme.fontLg
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
        AppText {
            Layout.fillWidth: true
            text: Tr.s("homeText")
            color: Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
        Item { Layout.preferredHeight: 6 }
        PButton {
            objectName: "homeOpen"
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 240
            variant: "primary"
            text: Tr.s("openFile")
            onClicked: home.openRequested()
        }
        AppText {
            Layout.fillWidth: true
            text: Tr.s("dropHint")
            color: Theme.textDisabled
            font.pixelSize: Theme.fontSm
            horizontalAlignment: Text.AlignHCenter
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 10
            PButton { objectName: "homeMerge"; text: Tr.s("toolMerge"); onClicked: home.mergeRequested() }
            PButton { objectName: "homeBatch"; text: Tr.s("batchPdf"); enabled: !Doc.outputBusy; onClicked: home.batchRequested() }
            PButton { objectName: "homeRepair"; text: Tr.s("repairPdf"); enabled: !Doc.outputBusy; onClicked: home.repairRequested() }
        }

        Item { Layout.preferredHeight: 18 }
        AppText {
            Layout.fillWidth: true
            text: Tr.s("recent")
            font.pixelSize: Theme.fontLg
            font.weight: Font.DemiBold
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderSubtle }
        AppText {
            Layout.fillWidth: true
            visible: Doc.recentFiles.length === 0
            text: Tr.s("noRecent")
            color: Theme.textDisabled
        }
        Repeater {
            model: Doc.recentFiles.slice(0, 6)
            delegate: Rectangle {
                id: row
                required property string modelData
                required property int index
                objectName: "recent_" + index
                Layout.fillWidth: true
                implicitHeight: 52
                radius: 10
                color: rowHover.hovered ? Theme.surfaceHover : "transparent"
                HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: home.openPathRequested(row.modelData) }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 8
                    spacing: 12
                    Rectangle {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 34
                        radius: 4
                        color: Theme.accentSubtle
                        border.color: Qt.alpha(Theme.accent, 0.5)
                        AppText { anchors.centerIn: parent; text: "PDF"; font.pixelSize: 9; font.weight: Font.Bold; color: Theme.accentText }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        AppText { Layout.fillWidth: true; text: row.modelData.split(/[\\/]/).pop(); elide: Text.ElideRight; font.weight: Font.Medium }
                        AppText { Layout.fillWidth: true; text: row.modelData; elide: Text.ElideMiddle; color: Theme.textDisabled; font.pixelSize: Theme.fontSm }
                    }
                    PButton {
                        compact: true
                        variant: "ghost"
                        text: "✕"
                        tip: Tr.s("removeRecent")
                        opacity: rowHover.hovered ? 1 : 0
                        onClicked: Doc.removeRecent(row.modelData)
                    }
                }
            }
        }
    }
}
