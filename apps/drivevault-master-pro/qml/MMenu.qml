import QtQuick
import QtQuick.Controls

// A dark, rounded pop-up menu (the "more" menus of the top bar and of a program card).
Menu {
    id: menu
    padding: 6
    topPadding: 6
    bottomPadding: 6
    implicitWidth: 240
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent      // a press on the owner button must not close the menu first (its click would reopen it)

    background: Rectangle {
        radius: 12
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
    }
}
