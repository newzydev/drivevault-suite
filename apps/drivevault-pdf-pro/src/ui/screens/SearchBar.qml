import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// Find in the document: text, options, the number of hits, previous / next. The search starts a moment after typing.
Rectangle {
    id: bar

    property int startPage: 0
    property bool matchCase: false
    property bool wholeWord: false
    readonly property string query: field.text
    signal closeRequested()

    implicitHeight: 48
    color: Theme.backgroundAlt
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }

    function focusField() { field.forceActiveFocus(); field.selectAll() }
    function run() { Doc.search(field.text, bar.matchCase, bar.wholeWord, bar.startPage) }
    Timer { id: delay; interval: 220; onTriggered: bar.run() }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 8

        TextField {
            id: field
            objectName: "searchField"
            Layout.preferredWidth: 300
            implicitHeight: 34
            placeholderText: Tr.s("findPlaceholder")
            placeholderTextColor: Theme.textDisabled
            color: Theme.text
            font.pixelSize: Theme.fontMd
            selectByMouse: true
            leftPadding: 12
            rightPadding: 12
            verticalAlignment: TextInput.AlignVCenter
            onTextChanged: delay.restart()
            onAccepted: {
                if (delay.running) { delay.stop(); bar.run() } else Doc.searchStep((Qt.application.keyboardModifiers & Qt.ShiftModifier) ? -1 : 1)
            }
            Keys.onEscapePressed: bar.closeRequested()
            background: Rectangle {
                radius: 9
                color: Theme.surface
                border.width: field.activeFocus ? 2 : 1
                border.color: field.activeFocus ? Theme.accent : Theme.border
            }
        }
        PButton {
            objectName: "searchCase"
            compact: true
            text: "Aa"
            tip: Tr.s("matchCase")
            variant: bar.matchCase ? "secondary" : "ghost"
            onClicked: { bar.matchCase = !bar.matchCase; bar.run() }
        }
        PButton {
            objectName: "searchWord"
            compact: true
            text: "ab"
            tip: Tr.s("wholeWord")
            variant: bar.wholeWord ? "secondary" : "ghost"
            onClicked: { bar.wholeWord = !bar.wholeWord; bar.run() }
        }
        PButton { objectName: "searchPrev"; compact: true; text: "↑"; tip: Tr.s("prevHit"); enabled: Doc.searchCount > 0; onClicked: Doc.searchStep(-1) }
        PButton { objectName: "searchNext"; compact: true; text: "↓"; tip: Tr.s("nextHit"); enabled: Doc.searchCount > 0; onClicked: Doc.searchStep(1) }
        AppText {
            objectName: "searchCount"
            Layout.minimumWidth: 120
            text: Doc.searching ? Tr.s("searching")
                  : field.text === "" ? ""
                  : Doc.searchCount === 0 ? Tr.s("noMatches")
                  : Tr.f2("hitsOf", Doc.searchCurrent + 1, Doc.searchCount)
            color: Doc.searchCount === 0 && !Doc.searching && field.text !== "" ? Theme.errorSoft : Theme.textSecondary
            font.pixelSize: Theme.fontSm
        }
        ProgressBar {
            visible: Doc.searching
            Layout.preferredWidth: 90
            from: 0
            to: 1
            value: Doc.searchProgress
        }
        Item { Layout.fillWidth: true }
        PButton { objectName: "searchClose"; compact: true; variant: "ghost"; text: "✕"; tip: Tr.s("closeSearch"); onClicked: bar.closeRequested() }
    }
}
