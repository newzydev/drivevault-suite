import QtQuick
import QtQuick.Effects
import DriveVault.Browser.Backend

Rectangle {
    id: avatar
    implicitWidth: 36
    implicitHeight: implicitWidth
    radius: width / 2
    color: Theme.surface
    border.color: Theme.border

    Rectangle {
        id: mask
        width: picture.width
        height: picture.height
        radius: width / 2
        visible: false
        layer.enabled: true
    }
    Image {
        id: picture
        anchors.fill: parent
        anchors.margins: 2
        source: ProfileSession.picture
        visible: false
        fillMode: Image.PreserveAspectCrop
    }
    MultiEffect {
        anchors.fill: picture
        visible: picture.status === Image.Ready
        source: picture
        maskEnabled: true
        maskSource: mask
    }
    LineIcon {
        anchors.centerIn: parent
        visible: picture.status !== Image.Ready
        kind: "globe"
        color: Theme.accent
    }
}
