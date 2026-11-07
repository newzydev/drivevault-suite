import QtQuick
import QtQuick.Layouts

// Honest feature status: never colour-only, always an icon plus a label.
// kind: available | notImplemented | modelRequired | unsupported | info
Rectangle {
    id: root

    property string kind: "notImplemented"
    property string label: ""   // overrides the default label of the kind
    property string phase: ""   // e.g. "Phase 2"

    readonly property color tone: kind === "available" ? Theme.success
                                  : kind === "modelRequired" ? Theme.warning
                                  : kind === "unsupported" ? Theme.error
                                  : kind === "info" ? Theme.info : Theme.textSecondary
    readonly property string iconForKind: kind === "available" ? "check"
                                          : kind === "modelRequired" ? "database"
                                          : kind === "unsupported" ? "warning"
                                          : kind === "info" ? "info" : "clock"
    readonly property string defaultLabel: {
        switch (kind) {
        case "available": return qsTr("Available")
        case "modelRequired": return qsTr("Model required")
        case "unsupported": return qsTr("Unsupported hardware")
        case "info": return qsTr("Info")
        default: return qsTr("Not implemented")
        }
    }

    implicitHeight: 22
    implicitWidth: row.implicitWidth + 16
    radius: 11
    color: Qt.alpha(tone, 0.14)
    border.color: Qt.alpha(tone, 0.45)

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 5
        Icon { name: root.iconForKind; size: 12; strokeWidth: 2.2; color: root.tone }
        Text {
            text: (root.label !== "" ? root.label : root.defaultLabel) + (root.phase !== "" ? " · " + root.phase : "")
            font.pixelSize: Theme.fontXs
            font.weight: Font.Medium
            color: root.tone
        }
    }
}
