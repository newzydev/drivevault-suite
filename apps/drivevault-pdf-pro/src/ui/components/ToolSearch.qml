import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: search
    required property var tools
    signal toolRequested(string key)

    readonly property var results: {
        const query = field.text.trim().toLowerCase()
        return tools.filter(tool => Tr.s(tool.key).toLowerCase().includes(query))
    }
    anchors.centerIn: Overlay.overlay
    width: 460
    height: Math.min(416, Overlay.overlay.height - 48)
    padding: 12
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: {
        field.clear()
        matches.currentIndex = 0
        field.forceActiveFocus()
    }
    function choose(tool) {
        if (tool) {
            close()
            toolRequested(tool.key)
        }
    }
    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }
    contentItem: ColumnLayout {
        spacing: 8
        TextField {
            id: field
            objectName: "toolSearchField"
            Layout.fillWidth: true
            color: Theme.text
            placeholderText: Tr.s("findTools")
            placeholderTextColor: Theme.textSecondary
            font.pixelSize: Theme.fontLg
            selectByMouse: true
            background: Rectangle { radius: Theme.radiusSmall; color: Theme.background; border.color: Theme.accent }
            onTextChanged: matches.currentIndex = 0
            onAccepted: search.choose(search.results[matches.currentIndex])
            Keys.onDownPressed: matches.currentIndex = Math.min(matches.count - 1, matches.currentIndex + 1)
            Keys.onUpPressed: matches.currentIndex = Math.max(0, matches.currentIndex - 1)
        }
        ListView {
            id: matches
            Layout.fillHeight: true
            Layout.fillWidth: true
            clip: true
            model: search.results
            ScrollBar.vertical: ScrollBar {}
            delegate: ToolEntry {
                required property var modelData
                required property int index
                width: ListView.view.width
                tool: modelData
                highlighted: index === matches.currentIndex
                onClicked: search.choose(tool)
            }
            AppText { anchors.centerIn: parent; visible: matches.count === 0; text: Tr.s("noMatches"); color: Theme.textSecondary }
        }
    }
}
