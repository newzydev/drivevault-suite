import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

// One animatable clip parameter: label, slider, value, keyframe diamond (add / remove at the playhead) and a
// reset. Everything goes through the timeline model, so every change is one undoable step.
RowLayout {
    id: row

    required property string clipId
    required property string paramKey
    property string label
    property int revision: 0              // bumped by the inspector whenever the clip data changed
    property real displayScale: 1         // value shown = value * displayScale
    property int decimals: 2
    property string unit: ""
    readonly property var info: Projects.timeline.paramInfo(paramKey)
    readonly property real value: { revision; Projects.playheadFrame; return Projects.timeline.paramValue(clipId, paramKey, Projects.playheadFrame) }
    readonly property bool animated: { revision; return Projects.timeline.paramAnimated(clipId, paramKey) }
    readonly property bool keyed: { revision; Projects.playheadFrame; return Projects.timeline.keyframeAt(clipId, paramKey, Projects.playheadFrame) }
    readonly property bool changed: Math.abs(value - info.def) > 1e-6 || animated

    Layout.fillWidth: true
    Layout.minimumWidth: 0
    spacing: 6

    function commit(v) {
        const err = Projects.timeline.setParam(clipId, paramKey, v, Projects.playheadFrame)
        if (err !== "")
            App.toast(err, "warning")
    }

    AppText {
        Layout.preferredWidth: 78
        text: row.label
        font.pixelSize: Theme.fontSm
        color: row.changed ? Theme.text : Theme.textSecondary
        elide: Text.ElideRight
    }
    AppSlider {
        id: slider
        objectName: "param_" + row.paramKey
        Layout.fillWidth: true
        implicitWidth: 60
        from: row.info.min
        to: row.info.max
        value: row.value
        onMoved: row.commit(value)
        Accessible.name: row.label
        onPressedChanged: if (!pressed) value = Qt.binding(() => row.value)
    }
    TextInput {
        id: field
        Layout.preferredWidth: 46
        horizontalAlignment: Text.AlignRight
        text: (row.value * row.displayScale).toFixed(row.decimals)
        font.pixelSize: Theme.fontSm
        font.features: { "tnum": 1 }
        color: activeFocus ? Theme.text : Theme.textSecondary
        selectByMouse: true
        selectionColor: Theme.accent
        selectedTextColor: Theme.textOnAccent
        validator: DoubleValidator { notation: DoubleValidator.StandardNotation }
        onEditingFinished: {
            const v = parseFloat(text.replace(",", "."))
            if (!isNaN(v))
                row.commit(v / row.displayScale)
            text = Qt.binding(() => (row.value * row.displayScale).toFixed(row.decimals))
        }
        Accessible.name: row.label
    }
    // keyframe diamond
    Item {
        Layout.preferredWidth: 20
        Layout.preferredHeight: 20
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Keyframe")
        Rectangle {
            anchors.centerIn: parent
            width: 10
            height: 10
            rotation: 45
            color: row.keyed ? Theme.accent : "transparent"
            border.width: 1.5
            border.color: row.animated ? Theme.accent : Theme.textDisabled
        }
        MouseArea {
            id: keyArea
            objectName: "key_" + row.paramKey
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.PointingHandCursor
            onClicked: mouse => {
                if (mouse.button === Qt.RightButton) {
                    if (row.animated)
                        keyMenu.popup()
                    return
                }
                const err = Projects.timeline.toggleKeyframe(row.clipId, row.paramKey, Projects.playheadFrame)
                if (err !== "")
                    App.toast(err, "warning")
            }
            AppToolTip { visible: keyArea.containsMouse; text: qsTr("Add or remove a keyframe at the playhead (right-click for options)") }
        }
        AppMenu {
            id: keyMenu
            AppMenuItem { text: qsTr("Linear"); enabled: row.keyed; onTriggered: Projects.timeline.setKeyInterpolation(row.clipId, row.paramKey, Projects.playheadFrame, "linear") }
            AppMenuItem { text: qsTr("Ease in / out"); enabled: row.keyed; onTriggered: Projects.timeline.setKeyInterpolation(row.clipId, row.paramKey, Projects.playheadFrame, "ease") }
            AppMenuItem { text: qsTr("Hold"); enabled: row.keyed; onTriggered: Projects.timeline.setKeyInterpolation(row.clipId, row.paramKey, Projects.playheadFrame, "hold") }
            AppMenuSeparator {}
            AppMenuItem { text: qsTr("Remove all keyframes"); onTriggered: Projects.timeline.clearKeyframes(row.clipId, row.paramKey) }
        }
    }
}
