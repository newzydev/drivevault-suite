import QtQuick
import QtQuick.Layouts

// Transient notifications. Call show(message, kind) with kind = info | success | warning | error.
Item {
    id: root

    property int maxVisible: 4

    function show(message, kind) {
        if (toasts.count >= maxVisible)
            toasts.remove(0)
        toasts.append({ message: message, kind: kind || "info", stamp: Date.now() })
    }

    ListModel { id: toasts }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 36
        spacing: 8

        Repeater {
            model: toasts
            delegate: Rectangle {
                id: toast
                required property int index
                required property string message
                required property string kind
                readonly property color tone: kind === "success" ? Theme.success
                                              : kind === "warning" ? Theme.warning
                                              : kind === "error" ? Theme.error : Theme.info

                width: Math.min(520, root.width - 48)
                height: row.implicitHeight + 24
                radius: Theme.radius
                color: Theme.surfaceElevated
                border.color: Theme.border
                opacity: 0

                Rectangle {
                    width: 4
                    height: parent.height - 16
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    radius: 2
                    color: toast.tone
                }

                RowLayout {
                    id: row
                    anchors.fill: parent
                    anchors.leftMargin: 24
                    anchors.rightMargin: 14
                    spacing: 10
                    Icon {
                        name: toast.kind === "success" ? "check" : toast.kind === "info" ? "info" : "warning"
                        color: toast.tone
                        size: 18
                    }
                    Text {
                        Layout.fillWidth: true
                        text: toast.message
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontMd
                        color: Theme.text
                    }
                }

                Timer {
                    interval: toast.kind === "error" ? 7000 : 4000
                    running: true
                    onTriggered: if (toast.index >= 0 && toast.index < toasts.count) toasts.remove(toast.index)
                }

                Component.onCompleted: fade.start()
                NumberAnimation { id: fade; target: toast; property: "opacity"; to: 1; duration: Theme.durationNormal }

                MouseArea {
                    anchors.fill: parent
                    onClicked: if (toast.index >= 0 && toast.index < toasts.count) toasts.remove(toast.index)
                }
            }
        }
    }
}
