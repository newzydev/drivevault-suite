import QtQuick
import QtQuick.Controls

// A dark, rounded pop-up menu (right click menu of the pages).
Menu {
    id: menu
    padding: 6
    implicitWidth: 220

    background: Rectangle {
        radius: 12
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
    }
}
