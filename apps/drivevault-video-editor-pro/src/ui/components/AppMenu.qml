import QtQuick
import QtQuick.Controls

Menu {
    id: control

    padding: 5
    topPadding: 6
    bottomPadding: 6
    delegate: AppMenuItem {}

    background: Rectangle {
        implicitWidth: 240
        radius: Theme.radius
        color: Theme.surfaceElevated
        border.color: Theme.border
    }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast } }
}
