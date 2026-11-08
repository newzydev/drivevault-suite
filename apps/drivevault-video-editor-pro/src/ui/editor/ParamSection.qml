import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

// Collapsible inspector section with a reset button. Its children go into the content column.
ColumnLayout {
    id: section

    property string title
    property string group                 // parameter group that "Reset" restores ("" = no reset)
    property string clipId
    property int revision: 0
    property bool defaultExpanded: true
    property bool controlled: false          // true: the owner sets `expanded` (accordion) and reacts to toggleRequested()
    signal toggleRequested()
    property bool expanded: AppSettings.state("insp." + group + title, defaultExpanded)
    default property alias content: body.data

    Layout.fillWidth: true
    Layout.minimumWidth: 0
    spacing: 8

    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        MouseArea {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (section.controlled) {
                    section.toggleRequested()
                    return
                }
                section.expanded = !section.expanded
                AppSettings.setState("insp." + section.group + section.title, section.expanded)
            }
            RowLayout {
                anchors.fill: parent
                spacing: 6
                Icon { name: section.expanded ? "chevronDown" : "chevronRight"; size: 14; color: Theme.textSecondary }
                Text {
                    Layout.fillWidth: true
                    text: section.title
                    font.pixelSize: Theme.fontXs
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.8
                    color: Theme.accentText
                }
            }
        }
        IconButton {
            visible: section.group !== ""
            enabled: { section.revision; return section.clipId !== "" && Projects.timeline.paramsModified(section.clipId, section.group) }
            iconName: "refresh"
            iconSize: 14
            implicitWidth: 24
            implicitHeight: 24
            tip: qsTr("Reset")
            onClicked: Projects.timeline.resetParams(section.clipId, section.group)
        }
    }
    ColumnLayout {
        id: body
        Layout.fillWidth: true
        visible: section.expanded
        spacing: 6
    }
}
