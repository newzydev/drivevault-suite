import QtQuick
import QtQuick.Layouts

// Group heading + optional note inside a settings page.
ColumnLayout {
    id: root

    property string title
    property string note

    Layout.fillWidth: true
    Layout.topMargin: 6
    spacing: 4

    Text {
        text: root.title
        font.pixelSize: Theme.fontXs
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
        color: Theme.accentText
    }
    Text {
        Layout.fillWidth: true
        visible: root.note !== ""
        text: root.note
        font.pixelSize: Theme.fontSm
        color: Theme.textSecondary
        wrapMode: Text.Wrap
    }
}
