import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts

// The Google account of the suite in a title bar (the same file in every program; colours and texts come from the program).
// Signed out: a "Sign in with Google" button. Waiting for the browser: the same place says so, a click cancels.
// Signed in: round picture + name; a click opens a card with the e-mail and "Sign out" (for ALL DriveVault programs).
Item {
    id: chip

    required property var account                       // dv::account::GoogleAccount (the program registers one)
    property color accent: "#8B5CF6"
    property color accentTextColor: "#FFFFFF"
    property color textColor: "#EEF0F4"
    property color mutedColor: "#9AA0AD"
    property color surfaceColor: "#17181D"
    property color hoverColor: "#22242B"
    property color borderColor: "#2C2F38"
    property string signInText: "Sign in with Google"
    property string waitingText: "Waiting for Google..."
    property string signOutText: "Sign out"
    property string signOutHint: "Signs you out of all DriveVault programs"
    property var errorTexts: ({})                       // code ("unavailable", "failed", "denied", "timeout") -> text
    property bool compact: false                        // picture only (a narrow window)
    property int maxNameWidth: 150

    implicitHeight: 34
    implicitWidth: pill.implicitWidth
    objectName: "accountChip"

    Accessible.role: Accessible.Button
    Accessible.name: account.signedIn ? account.name + ", " + account.email : (account.busy ? chip.waitingText : chip.signInText)

    Rectangle {
        id: pill
        anchors.fill: parent
        implicitWidth: row.implicitWidth + 8 + (chip.account.signedIn ? 12 : 14)
        radius: height / 2
        color: chip.account.signedIn ? (hit.hovered || card.visible ? chip.hoverColor : chip.surfaceColor)
                                     : (chip.account.busy ? chip.surfaceColor : (hit.hovered ? Qt.lighter(chip.accent, 1.12) : chip.accent))
        border.width: chip.account.signedIn || chip.account.busy ? 1 : 0
        border.color: chip.borderColor

        RowLayout {
            id: row
            anchors.verticalCenter: parent.verticalCenter
            x: 4
            spacing: 8

            // the picture (round) or, without one, the first letter
            Item {
                visible: chip.account.signedIn
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                Image {
                    id: photo
                    anchors.fill: parent
                    source: chip.account.picture
                    sourceSize: Qt.size(52, 52)
                    fillMode: Image.PreserveAspectCrop
                    visible: false
                    smooth: true
                }
                Rectangle { id: photoMask; anchors.fill: parent; radius: width / 2; visible: false; layer.enabled: true }
                MultiEffect {
                    anchors.fill: parent
                    source: photo
                    maskEnabled: true
                    maskSource: photoMask
                    visible: photo.status === Image.Ready
                }
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: chip.accent
                    visible: photo.status !== Image.Ready
                    Text {
                        anchors.centerIn: parent
                        text: chip.account.name.length > 0 ? chip.account.name.charAt(0).toUpperCase() : "?"
                        color: chip.accentTextColor
                        font.pixelSize: 13
                        font.weight: Font.Bold
                    }
                }
            }
            // "G" mark of the sign-in button
            Rectangle {
                visible: !chip.account.signedIn && !chip.account.busy
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                radius: 11
                color: "#FFFFFF"
                Text { anchors.centerIn: parent; text: "G"; color: "#4285F4"; font.pixelSize: 14; font.weight: Font.Bold }
            }
            // waiting: three dots that breathe
            Row {
                visible: chip.account.busy
                spacing: 3
                Layout.alignment: Qt.AlignVCenter
                Repeater {
                    model: 3
                    delegate: Rectangle {
                        required property int index
                        width: 6; height: 6; radius: 3
                        color: chip.accent
                        SequentialAnimation on opacity {
                            running: chip.account.busy
                            loops: Animation.Infinite
                            PauseAnimation { duration: index * 160 }
                            NumberAnimation { to: 0.25; duration: 360 }
                            NumberAnimation { to: 1; duration: 360 }
                            PauseAnimation { duration: (2 - index) * 160 }
                        }
                    }
                }
            }
            Text {
                visible: !chip.compact || !chip.account.signedIn
                Layout.maximumWidth: chip.maxNameWidth
                Layout.alignment: Qt.AlignVCenter
                text: chip.account.signedIn ? chip.account.name : (chip.account.busy ? chip.waitingText : chip.signInText)
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: chip.account.signedIn || chip.account.busy ? chip.textColor : chip.accentTextColor
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
        }

        HoverHandler { id: hit; cursorShape: Qt.PointingHandCursor }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: {
                if (chip.account.signedIn) card.visible ? card.close() : card.open()
                else if (chip.account.busy) chip.account.cancelSignIn()
                else chip.account.signIn()
            }
        }
    }

    // signed in: who it is and how to leave
    Popup {
        id: card
        y: chip.height + 8
        x: chip.width - width
        width: 280
        padding: 16
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: Rectangle { radius: 14; color: chip.surfaceColor; border.width: 1; border.color: chip.borderColor }
        contentItem: ColumnLayout {
            spacing: 10
            RowLayout {
                spacing: 12
                Item {
                    Layout.preferredWidth: 52
                    Layout.preferredHeight: 52
                    Image { id: bigPhoto; anchors.fill: parent; source: chip.account.picture; sourceSize: Qt.size(104, 104); fillMode: Image.PreserveAspectCrop; visible: false; smooth: true }
                    Rectangle { id: bigMask; anchors.fill: parent; radius: width / 2; visible: false; layer.enabled: true }
                    MultiEffect { anchors.fill: parent; source: bigPhoto; maskEnabled: true; maskSource: bigMask; visible: bigPhoto.status === Image.Ready }
                    Rectangle {
                        anchors.fill: parent; radius: width / 2; color: chip.accent; visible: bigPhoto.status !== Image.Ready
                        Text { anchors.centerIn: parent; text: chip.account.name.length > 0 ? chip.account.name.charAt(0).toUpperCase() : "?"; color: chip.accentTextColor; font.pixelSize: 22; font.weight: Font.Bold }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { Layout.fillWidth: true; text: chip.account.name; textFormat: Text.PlainText; elide: Text.ElideRight; color: chip.textColor; font.pixelSize: 15; font.weight: Font.DemiBold }
                    Text { Layout.fillWidth: true; text: chip.account.email; textFormat: Text.PlainText; elide: Text.ElideRight; color: chip.mutedColor; font.pixelSize: 12 }
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: chip.borderColor }
            AbstractButton {
                id: out
                objectName: "accountSignOut"
                Layout.fillWidth: true
                implicitHeight: 36
                hoverEnabled: true
                onClicked: { card.close(); chip.account.signOut() }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                background: Rectangle { radius: 10; color: out.hovered ? chip.hoverColor : "transparent"; border.width: 1; border.color: chip.borderColor }
                contentItem: Text { text: chip.signOutText; color: chip.textColor; font.pixelSize: 13; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
            Text { Layout.fillWidth: true; text: chip.signOutHint; color: chip.mutedColor; font.pixelSize: 11; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter }
        }
    }

    // a sign-in that did not work: the reason, under the button
    Popup {
        id: problem
        y: chip.height + 8
        x: chip.width - width
        width: 300
        padding: 12
        modal: false
        visible: chip.account.error !== "" && !chip.account.signedIn
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        onClosed: chip.account.clearError()
        background: Rectangle { radius: 12; color: chip.surfaceColor; border.width: 1; border.color: "#EF4444" }
        contentItem: Text {
            text: chip.errorTexts[chip.account.error] !== undefined ? chip.errorTexts[chip.account.error] : chip.account.error
            wrapMode: Text.WordWrap
            color: chip.textColor
            font.pixelSize: 12
        }
        Timer { running: problem.visible; interval: 9000; onTriggered: problem.close() }
    }
}
