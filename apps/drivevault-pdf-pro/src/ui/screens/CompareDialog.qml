import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// Compare the open file with another PDF: which pages differ, by text (lines added / removed) and by picture. Nothing is written.
Popup {
    id: dlg
    objectName: "compareDialog"

    property string otherPath: ""
    property string error: ""
    signal pageRequested(int page)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    width: Math.min(760, (parent ? parent.width : 760) - 48)
    height: Comparer.hasResult || Comparer.busy ? Math.min(680, (parent ? parent.height : 680) - 48) : Math.min(contentItem.implicitHeight + topPadding + bottomPadding, (parent ? parent.height : 680) - 48)
    padding: 24
    closePolicy: Comparer.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#B3000000" }
    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    function run(password) {
        error = ""
        const code = Comparer.start(otherPath, password === undefined ? "" : password)
        if (code === "same")
            error = Tr.s("compareSame")
        else if (code !== "")
            error = Tr.s("busyNow")
    }
    onAboutToShow: { otherPath = ""; error = ""; Comparer.clear() }

    Connections {
        target: Comparer
        function onFinished(ok, err) {
            if (ok) { dlg.error = ""; return }
            dlg.error = err === "cancelled" ? Tr.s("compareCancelled") : err === "password" ? Tr.s("comparePassword") : Tr.s(err === "file" ? "errFile" : "errFormat")
        }
    }

    contentItem: ColumnLayout {
        spacing: 10
        AppText { Layout.fillWidth: true; text: Tr.s("compareTitle") + (Doc.isOpen ? " - " + Doc.fileName : ""); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold; elide: Text.ElideMiddle }
        AppText { Layout.fillWidth: true; text: Tr.s("compareHint"); color: Theme.textDisabled; font.pixelSize: Theme.fontSm; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PButton { objectName: "comparePick"; text: Tr.s("comparePick"); enabled: !Comparer.busy; onClicked: pickDialog.open() }
            AppText { objectName: "compareOther"; Layout.fillWidth: true; text: dlg.otherPath === "" ? Tr.s("compareNone") : Doc.localPath(dlg.otherPath); color: dlg.otherPath === "" ? Theme.textDisabled : Theme.text; elide: Text.ElideMiddle }
            PButton { objectName: "compareRun"; variant: "primary"; text: Comparer.busy ? Tr.s("comparing") : Tr.s("compareRun"); enabled: dlg.otherPath !== "" && !Comparer.busy; onClicked: dlg.run() }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: dlg.error !== "" && Comparer.summary.error === "password"
            PTextField { id: otherPassword; objectName: "comparePasswordField"; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: Tr.s("password"); onAccepted: dlg.run(text) }
            PButton { compact: true; text: Tr.s("open"); onClicked: dlg.run(otherPassword.text) }
        }
        AppText { objectName: "compareError"; Layout.fillWidth: true; visible: dlg.error !== ""; text: dlg.error; color: Theme.errorSoft; wrapMode: Text.Wrap }
        ProgressBar { Layout.fillWidth: true; visible: Comparer.busy; from: 0; to: 1; value: Comparer.progress }

        AppText {
            objectName: "compareSummary"
            Layout.fillWidth: true
            visible: Comparer.hasResult
            wrapMode: Text.Wrap
            font.weight: Font.DemiBold
            color: Comparer.summary.different > 0 ? Theme.warning : Theme.success
            text: Comparer.hasResult ? (Comparer.summary.different === 0 ? Tr.s("compareIdentical") : Tr.f2("compareDifferent", Comparer.summary.different, Comparer.pages.length))
                                       + "   (" + Tr.f2("comparePageCounts", Comparer.summary.pagesA, Comparer.summary.pagesB) + ")" : ""
        }
        ListView {
            id: list
            objectName: "compareList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: Comparer.hasResult
            clip: true
            spacing: 6
            model: Comparer.pages
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                width: ListView.view.width - 12
                implicitHeight: info.implicitHeight + 16
                radius: 8
                color: Theme.surface
                border.color: modelData.status === "same" ? Theme.borderSubtle : Qt.alpha(Theme.warning, 0.6)
                ColumnLayout {
                    id: info
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        AppText { text: Tr.f("pageN", row.modelData.page + 1); font.weight: Font.DemiBold }
                        AppText { Layout.fillWidth: true; text: Tr.s("compare_" + row.modelData.status) + (row.modelData.status === "different" ? "  ·  " + Tr.f("compareText", Math.round(row.modelData.similarity * 100)) + "  ·  " + Tr.f("comparePicture", (row.modelData.pixels * 100).toFixed(1)) : ""); color: row.modelData.status === "same" ? Theme.success : Theme.warning; elide: Text.ElideRight }
                        PButton { compact: true; visible: row.modelData.status !== "onlyB"; text: Tr.s("compareGo"); onClicked: { dlg.pageRequested(row.modelData.page); dlg.close() } }
                    }
                    AppText { Layout.fillWidth: true; visible: row.modelData.removed > 0; text: "− " + Tr.f("compareRemoved", row.modelData.removed) + ": " + row.modelData.removedSample.join(" | "); color: Theme.errorSoft; font.pixelSize: Theme.fontSm; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight }
                    AppText { Layout.fillWidth: true; visible: row.modelData.added > 0; text: "+ " + Tr.f("compareAdded", row.modelData.added) + ": " + row.modelData.addedSample.join(" | "); color: Theme.success; font.pixelSize: Theme.fontSm; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight }
                }
            }
        }
        Item { Layout.fillHeight: true; visible: !Comparer.hasResult && Comparer.busy }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            PButton { objectName: "compareClose"; variant: "ghost"; text: Comparer.busy ? Tr.s("cancel") : Tr.s("close"); onClicked: { if (Comparer.busy) Comparer.cancel(); else dlg.close() } }
        }
    }

    FileDialog {
        id: pickDialog
        title: Tr.s("comparePick")
        nameFilters: [Tr.s("pdfFiles")]
        onAccepted: { dlg.otherPath = selectedFile.toString(); dlg.error = ""; Comparer.clear() }
    }
}
