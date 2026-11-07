import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

// The top-left tool panel (CapCut style): a row of tabs - Import, Audio, Text, Effects, Transitions, Filters, Adjustment, AI - and
// the content of the chosen tab. Every tile calls the same timeline operations as the inspector (undoable), nothing here is a mock-up.
Rectangle {
    id: root

    property alias media: mediaPanel
    function openImport() { selectTab("import"); mediaPanel.openImport() }

    property string tab: AppSettings.state("tool.tab", "import")
    readonly property var tabs: [
        { key: "import",      icon: "import",   label: qsTr("Import") },
        { key: "audio",       icon: "audio",    label: qsTr("Audio") },
        { key: "text",        icon: "text",     label: qsTr("Text") },
        { key: "effects",     icon: "bolt",     label: qsTr("Effects") },
        { key: "transitions", icon: "layers",   label: qsTr("Transitions") },
        { key: "filters",     icon: "paint",    label: qsTr("Filters") },
        { key: "adjust",      icon: "sliders",  label: qsTr("Adjustment") },
        { key: "ai",          icon: "sparkles", label: qsTr("AI"), workspace: "ai" }
    ]
    readonly property int tabIndex: { for (let i = 0; i < tabs.length; ++i) if (tabs[i].key === tab) return i; return 0 }

    function selectTab(key) {
        tab = key
        AppSettings.setState("tool.tab", key)
    }

    // ---- what the tiles work on --------------------------------------------------------------------------------------------------
    property int rev: 0
    Connections {
        target: Projects.timeline
        function onClipsChanged() { root.rev++ }
        function onSelectionChanged() { root.rev++ }
    }
    readonly property string videoId: { rev; return Projects.timeline.selectedClipOfKind("video") }
    function report(error) { if (error !== "") App.toast(error, "warning") }
    function needClip() {
        if (videoId !== "")
            return true
        App.toast(qsTr("Select a video clip on the timeline first."), "info")
        return false
    }
    // value that lies `f` of the way from the default to the end of the range (f < 0: towards the minimum)
    function mix(key, f) {
        const i = Projects.timeline.paramInfo(key)
        return f >= 0 ? i.def + f * (i.max - i.def) : i.def + f * (i.def - i.min)
    }
    function applyParams(group, values) {
        if (!needClip())
            return
        if (group !== "")
            report(Projects.timeline.resetParams(videoId, group))
        for (const key of Object.keys(values))
            report(Projects.timeline.setParam(videoId, key, mix(key, values[key]), Projects.playheadFrame))
    }
    function setTransition(dir, type) {
        if (needClip())
            report(Projects.timeline.setTransition(videoId, dir, type, type === "" ? 0 : Math.round(Projects.nominalFps / 2)))
    }

    color: Theme.backgroundAlt
    radius: Theme.radius
    border.color: Theme.borderSubtle
    border.width: 1
    clip: true

    component Tile: Rectangle {
        id: tile
        property string iconName
        property string label
        property color tint: Theme.accent
        signal activated()
        Layout.fillWidth: true
        implicitWidth: 104
        implicitHeight: 84
        radius: Theme.radius
        color: ma.containsMouse ? Theme.surfaceHover : Theme.surface
        border.width: 1
        border.color: ma.containsMouse ? Qt.alpha(tint, 0.7) : Theme.borderSubtle
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width - 12
            spacing: 6
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 36; height: 36; radius: 10
                color: Qt.alpha(tile.tint, 0.18)
                Icon { anchors.centerIn: parent; name: tile.iconName; size: 18; color: tile.tint }
            }
            Text {
                Layout.fillWidth: true
                text: tile.label
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.fontXs
                font.weight: Font.Medium
                color: Theme.text
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: tile.activated()
        }
    }
    component Hint: Text {
        Layout.fillWidth: true
        font.pixelSize: Theme.fontXs
        color: Theme.textDisabled
        wrapMode: Text.Wrap
    }
    component Heading: Text {
        Layout.fillWidth: true
        font.pixelSize: Theme.fontXs
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
        color: Theme.textSecondary
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- tab row -------------------------------------------------------------------------------------------------------------
        Item {
            Layout.fillWidth: true
            implicitHeight: 62
            Flickable {
                id: tabFlick
                anchors.fill: parent
                contentWidth: tabRow.implicitWidth
                contentHeight: height
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick
                clip: true
                Row {
                    id: tabRow
                    height: parent.height
                    leftPadding: 6
                    rightPadding: 6
                    spacing: 0
                    Repeater {
                        model: root.tabs
                        delegate: Item {
                            id: tabButton
                            required property var modelData
                            required property int index
                            readonly property bool current: root.tab === modelData.key
                            objectName: "toolTab_" + modelData.key
                            width: Math.max(30, Math.floor((tabFlick.width - 12) / root.tabs.length))      // the tabs share the width (icons only when narrow)
                            height: tabRow.height
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 4
                                radius: Theme.radiusSmall + 2
                                color: tabButton.current ? Theme.accentSubtle : tabMouse.containsMouse ? Theme.surfaceHover : "transparent"
                            }
                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 3
                                Icon {
                                    Layout.alignment: Qt.AlignHCenter
                                    name: tabButton.modelData.icon
                                    size: 19
                                    color: tabButton.current ? Theme.accentText : Theme.textSecondary
                                }
                                Text {
                                    id: tabLabel
                                    visible: tabButton.width >= 54
                                    Layout.alignment: Qt.AlignHCenter
                                    text: tabButton.modelData.label
                                    font.pixelSize: Theme.fontXs
                                    font.weight: tabButton.current ? Font.DemiBold : Font.Medium
                                    color: tabButton.current ? Theme.accentText : Theme.textSecondary
                                }
                            }
                            Rectangle {
                                visible: tabButton.current
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: parent.width - 20; height: 2; radius: 1
                                color: Theme.accent
                            }
                            ToolTip.visible: tabMouse.containsMouse && !tabLabel.visible
                            ToolTip.text: tabButton.modelData.label
                            ToolTip.delay: 400
                            MouseArea {
                                id: tabMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (tabButton.modelData.workspace)
                                        App.workspace = tabButton.modelData.workspace
                                    else
                                        root.selectTab(tabButton.modelData.key)
                                }
                            }
                        }
                    }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
        }

        // ---- tab content -----------------------------------------------------------------------------------------------------------
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: Math.min(root.tabIndex, 6)

            // 0 Import: the project media browser
            MediaPanel {
                id: mediaPanel
                color: "transparent"
                border.width: 0
                radius: 0
            }

            // 1 Audio: one strip per audio track
            ScrollView {
                id: audioPage
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: audioPage.availableWidth - 28
                    x: 14
                    y: 12
                    spacing: 12
                    Heading { text: qsTr("Audio tracks") }
                    MixerSection { Layout.fillWidth: true }
                    Hint { text: qsTr("Import music or sound effects with the Import tab and drag them to an audio track. The master level is always shown at the right of the timeline.") }
                }
            }

            // 2 Text
            ScrollView {
                id: textPage
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: textPage.availableWidth - 28
                    x: 14
                    y: 12
                    spacing: 12
                    Heading { text: qsTr("Add text at the playhead") }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        uniformCellWidths: true
                        columnSpacing: 10
                        rowSpacing: 10
                        Tile { objectName: "text_title"; iconName: "text"; label: qsTr("Title"); onActivated: root.report(Projects.timeline.addTitle(-1, Projects.playheadFrame, "title")) }
                        Tile { objectName: "text_lowerThird"; iconName: "text"; label: qsTr("Lower third"); onActivated: root.report(Projects.timeline.addTitle(-1, Projects.playheadFrame, "lowerThird")) }
                        Tile { objectName: "text_caption"; iconName: "text"; label: qsTr("Caption"); onActivated: root.report(Projects.timeline.addTitle(-1, Projects.playheadFrame, "caption")) }
                    }
                    Hint { text: qsTr("Select the text clip on the timeline to change its words, font and colour in the inspector. Automatic captions are in the AI tab.") }
                }
            }

            // 3 Effects
            ScrollView {
                id: effectsPage
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: effectsPage.availableWidth - 28
                    x: 14
                    y: 12
                    spacing: 12
                    Heading { text: qsTr("Effects for the selected clip") }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        uniformCellWidths: true
                        columnSpacing: 10
                        rowSpacing: 10
                        Tile { objectName: "fx_blur"; iconName: "bolt"; label: qsTr("Soft blur"); onActivated: root.applyParams("effects", { blur: 0.35 }) }
                        Tile { objectName: "fx_blurStrong"; iconName: "bolt"; label: qsTr("Strong blur"); onActivated: root.applyParams("effects", { blur: 0.8 }) }
                        Tile { objectName: "fx_sharpen"; iconName: "bolt"; label: qsTr("Sharpen"); onActivated: root.applyParams("effects", { sharpen: 0.5 }) }
                        Tile { objectName: "fx_vignette"; iconName: "bolt"; label: qsTr("Vignette"); onActivated: root.applyParams("effects", { vignette: 0.5 }) }
                        Tile { objectName: "fx_reset"; iconName: "refresh"; tint: Theme.textSecondary; label: qsTr("Remove effects"); onActivated: root.applyParams("effects", ({})) }
                    }
                    Hint { text: qsTr("A tile replaces the effects of the selected clip with its setting. Fine control (crop, amounts, keyframes) is in the inspector.") }
                }
            }

            // 4 Transitions
            ScrollView {
                id: transitionsPage
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: transitionsPage.availableWidth - 28
                    x: 14
                    y: 12
                    spacing: 12
                    Heading { text: qsTr("Transition in (start of the clip)") }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        uniformCellWidths: true
                        columnSpacing: 10
                        rowSpacing: 10
                        Tile { objectName: "trIn_dissolve"; iconName: "layers"; label: qsTr("Cross dissolve"); onActivated: root.setTransition("in", "dissolve") }
                        Tile { objectName: "trIn_dip"; iconName: "layers"; label: qsTr("Dip to black"); onActivated: root.setTransition("in", "dip") }
                        Tile { objectName: "trIn_fade"; iconName: "layers"; label: qsTr("Fade from black"); onActivated: root.setTransition("in", "fade") }
                        Tile { objectName: "trIn_wipe"; iconName: "layers"; label: qsTr("Wipe"); onActivated: root.setTransition("in", "wipe") }
                        Tile { objectName: "trIn_slide"; iconName: "layers"; label: qsTr("Slide"); onActivated: root.setTransition("in", "slide") }
                        Tile { objectName: "trIn_zoom"; iconName: "layers"; label: qsTr("Zoom"); onActivated: root.setTransition("in", "zoom") }
                        Tile { objectName: "trIn_none"; iconName: "close"; tint: Theme.textSecondary; label: qsTr("None"); onActivated: root.setTransition("in", "") }
                    }
                    Heading { text: qsTr("Transition out (end of the clip)") }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        uniformCellWidths: true
                        columnSpacing: 10
                        rowSpacing: 10
                        Tile { objectName: "trOut_fade"; iconName: "layers"; label: qsTr("Fade to black"); onActivated: root.setTransition("out", "fade") }
                        Tile { objectName: "trOut_dip"; iconName: "layers"; label: qsTr("Dip to black"); onActivated: root.setTransition("out", "dip") }
                        Tile { objectName: "trOut_none"; iconName: "close"; tint: Theme.textSecondary; label: qsTr("None"); onActivated: root.setTransition("out", "") }
                    }
                    Hint { text: qsTr("An \"In\" transition blends from the clip before it when the clips touch, otherwise from black. The length is set in the inspector.") }
                }
            }

            // 5 Filters: colour looks made of the real colour parameters
            ScrollView {
                id: filtersPage
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: filtersPage.availableWidth - 28
                    x: 14
                    y: 12
                    spacing: 12
                    Heading { text: qsTr("Looks for the selected clip") }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        uniformCellWidths: true
                        columnSpacing: 10
                        rowSpacing: 10
                        Tile { objectName: "look_vivid"; iconName: "paint"; label: qsTr("Vivid"); onActivated: root.applyParams("color", { saturation: 0.45, contrast: 0.2 }) }
                        Tile { objectName: "look_warm"; iconName: "sun"; label: qsTr("Warm"); onActivated: root.applyParams("color", { temperature: 0.45, saturation: 0.1 }) }
                        Tile { objectName: "look_cool"; iconName: "paint"; label: qsTr("Cool"); onActivated: root.applyParams("color", { temperature: -0.45, tint: -0.1 }) }
                        Tile { objectName: "look_mono"; iconName: "paint"; label: qsTr("Black & white"); onActivated: root.applyParams("color", { saturation: -1, contrast: 0.15 }) }
                        Tile { objectName: "look_faded"; iconName: "paint"; label: qsTr("Faded"); onActivated: root.applyParams("color", { contrast: -0.3, brightness: 0.12, saturation: -0.25 }) }
                        Tile { objectName: "look_cinema"; iconName: "film"; label: qsTr("Cinematic"); onActivated: root.applyParams("color", { contrast: 0.3, saturation: -0.15, temperature: -0.2 }) }
                        Tile { objectName: "look_none"; iconName: "refresh"; tint: Theme.textSecondary; label: qsTr("Original"); onActivated: root.applyParams("color", ({})) }
                    }
                    Hint { text: qsTr("A look sets the colour controls (exposure, contrast, saturation ...) of the clip; change them freely in the Adjustment tab. Your own LUT files are imported in the inspector.") }
                }
            }

            // 6 Adjustment: the main colour sliders of the selected clip
            ScrollView {
                id: adjustPage
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: adjustPage.availableWidth - 28
                    x: 14
                    y: 12
                    spacing: 10
                    Heading { text: qsTr("Adjustment") }
                    Hint { visible: root.videoId === ""; text: qsTr("Select a video clip on the timeline to adjust it.") }
                    Repeater {
                        model: root.videoId === "" ? [] : [
                            { key: "exposure", label: qsTr("Exposure") }, { key: "brightness", label: qsTr("Brightness") },
                            { key: "contrast", label: qsTr("Contrast") }, { key: "saturation", label: qsTr("Saturation") },
                            { key: "temperature", label: qsTr("Temperature") }, { key: "tint", label: qsTr("Tint") } ]
                        delegate: ParamRow {
                            required property var modelData
                            clipId: root.videoId
                            paramKey: modelData.key
                            label: modelData.label
                            revision: root.rev
                        }
                    }
                }
            }
        }
    }
}
