import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

ApplicationWindow {
    id: win
    visible: true
    width: 1240
    height: 800
    minimumWidth: 960
    minimumHeight: 560
    title: (Doc.isOpen ? Doc.fileName + " - " : "") + Tr.s("title")
    color: Theme.background

    property string notice: ""
    property bool noticeIsError: true
    property int windowedVisibility: Window.Windowed

    function showNotice(text, isError) { noticeIsError = isError; notice = text }
    function toggleFullScreen() {
        if (visibility === Window.FullScreen) {
            visibility = windowedVisibility
        } else {
            windowedVisibility = visibility === Window.Maximized ? Window.Maximized : Window.Windowed
            visibility = Window.FullScreen
        }
    }

    Connections {
        target: Doc
        function onPrintFinished(ok, message) {
            if (ok)
                win.showNotice(Tr.s("printDone"), false)
            else if (message === "cancelled")
                win.showNotice(Tr.s("printCancelled"), false)
            else
                win.showNotice(Tr.s("printFailed") + (message !== "" ? " (" + message + ")" : ""), true)
        }
    }

    function openPath(path, password) {
        const code = Doc.open(path, password === undefined ? "" : password)
        if (code === "") {
            passwordDialog.close()
            notice = ""
        } else if (code === "password") {
            passwordDialog.path = Doc.localPath(path)
            passwordDialog.wrong = password !== undefined && password !== ""
            if (!passwordDialog.opened)
                passwordDialog.open()
        } else {
            passwordDialog.close()
            showNotice(Tr.error(code), true)
            Doc.removeRecent(Doc.localPath(path))
        }
    }

    Loader {
        id: screenLoader
        anchors.fill: parent
        sourceComponent: Doc.isOpen ? viewerComponent : homeComponent
    }
    onClosing: { if (screenLoader.item && screenLoader.item.flushSave) screenLoader.item.flushSave() }      // keep the view of the open file
    Component {
        id: viewerComponent
        ViewerScreen {
            onOpenRequested: fileDialog.open()
            onOrganizeRequested: organizer.begin("organize")
            onSplitRequested: splitDialog.open()
            onCompressRequested: compressDialog.open()
        }
    }
    Component {
        id: homeComponent
        HomeScreen {
            onOpenRequested: fileDialog.open()
            onMergeRequested: organizer.begin("merge")
            onOpenPathRequested: path => win.openPath(path)
        }
    }

    // an error message (a file that cannot be opened)
    Rectangle {
        objectName: "noticeBanner"
        visible: win.notice !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 14
        width: Math.min(560, parent.width - 32)
        height: noticeRow.implicitHeight + 20
        radius: 10
        color: Qt.alpha(win.noticeIsError ? Theme.error : Theme.success, 0.16)
        border.color: Qt.alpha(win.noticeIsError ? Theme.error : Theme.success, 0.6)
        z: 10
        RowLayout {
            id: noticeRow
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10
            AppText { Layout.fillWidth: true; text: win.notice; wrapMode: Text.Wrap }
            PButton { compact: true; variant: "ghost"; text: "✕"; tip: Tr.s("dismiss"); onClicked: win.notice = "" }
        }
    }

    FileDialog {
        id: fileDialog
        title: Tr.s("openDialogTitle")
        nameFilters: [Tr.s("pdfFiles")]
        onAccepted: win.openPath(selectedFile.toString())
    }
    OrganizerDialog {
        id: organizer
        onOpenResultRequested: path => win.openPath(path)
    }
    SplitDialog { id: splitDialog }
    CompressDialog {
        id: compressDialog
        onOpenResultRequested: path => win.openPath(path)
    }
    PasswordDialog {
        id: passwordDialog
        onSubmitted: (path, password) => win.openPath(path, password)
    }

    // drop a PDF file on the window
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: drop => {
            for (const url of drop.urls) {
                const s = url.toString()
                if (s.toLowerCase().endsWith(".pdf")) {
                    win.openPath(s)
                    return
                }
            }
        }
    }

    Shortcut { sequences: [StandardKey.Open]; onActivated: fileDialog.open() }
    Shortcut { sequences: ["F11"]; onActivated: win.toggleFullScreen() }
    Shortcut { sequences: ["Escape"]; enabled: win.visibility === Window.FullScreen; onActivated: win.toggleFullScreen() }
}
