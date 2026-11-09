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
            notice = Tr.error(code)
            Doc.removeRecent(Doc.localPath(path))
        }
    }

    Loader {
        anchors.fill: parent
        sourceComponent: Doc.isOpen ? viewerComponent : homeComponent
    }
    Component {
        id: viewerComponent
        ViewerScreen { onOpenRequested: fileDialog.open() }
    }
    Component {
        id: homeComponent
        HomeScreen {
            onOpenRequested: fileDialog.open()
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
        color: Qt.alpha(Theme.error, 0.16)
        border.color: Qt.alpha(Theme.error, 0.6)
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
}
