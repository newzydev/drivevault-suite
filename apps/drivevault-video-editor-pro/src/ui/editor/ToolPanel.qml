import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Backend

// The top-left tool panel (CapCut style): a row of tabs - Import, Audio, Text, Effects, Transitions, Filters, Adjustment, AI - and
// the content of the chosen tab. Every tile calls the same timeline operations as the inspector (undoable), nothing here is a mock-up.
Rectangle {
    id: root

    property alias media: mediaPanel
    signal mixerRequested()
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
        { key: "ai",          icon: "sparkles", label: qsTr("AI") }
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
    // sets real values (not fractions); `group` is reset first ("" = nothing)
    function applyAbs(group, values) {
        if (!needClip())
            return
        if (group !== "")
            report(Projects.timeline.resetParams(videoId, group))
        for (const key of Object.keys(values))
            report(Projects.timeline.setParam(videoId, key, values[key], Projects.playheadFrame))
    }
    function setTransition(dir, type) {
        if (needClip())
            report(Projects.timeline.setTransition(videoId, dir, type, type === "" ? 0 : Math.round(Projects.nominalFps / 2)))
    }

    // ---- the catalogs ----------------------------------------------------------------------------------------------------------------
    // Text styles: the same ids as the table in TimelineModel::addTitle (font / colour / stroke only draw the tile).
    readonly property var textGroups: [
        { title: qsTr("Basic"), items: [
            { id: "title", label: qsTr("Title"), color: "#FFFFFF" },
            { id: "lowerThird", label: qsTr("Lower third"), color: "#FFFFFF", shadow: true },
            { id: "caption", label: qsTr("Caption"), color: "#FFFFFF", stroke: "#000000" },
            { id: "yellowSub", label: qsTr("Subtitle"), color: "#FFE600", stroke: "#000000" },
            { id: "cornerTag", label: qsTr("Tag"), color: "#FFFFFF", shadow: true },
            { id: "bigNumber", label: qsTr("01"), color: "#FFFFFF", shadow: true } ] },
        { title: qsTr("Bold and outline"), items: [
            { id: "bigBold", label: qsTr("BIG TEXT"), color: "#FFFFFF", shadow: true },
            { id: "outline", label: qsTr("Outline"), color: "#FFFFFF", stroke: "#111111" },
            { id: "impact", label: qsTr("IMPACT"), color: "#FFFFFF", stroke: "#000000", font: "Impact", bold: false },
            { id: "redAlert", label: qsTr("ALERT"), color: "#FF3B30", stroke: "#FFFFFF" } ] },
        { title: qsTr("Colour"), items: [
            { id: "neonPink", label: qsTr("Neon"), color: "#FF4FD8", stroke: "#8A0F72" },
            { id: "neonBlue", label: qsTr("Neon"), color: "#4FE0FF", stroke: "#0A5066" },
            { id: "gold", label: qsTr("Gold"), color: "#FFD34D", stroke: "#6B4A00" } ] },
        { title: qsTr("Fonts"), items: [
            { id: "elegant", label: qsTr("Elegant"), color: "#F4E9D4", font: "Georgia", bold: false },
            { id: "script", label: qsTr("Handwriting"), color: "#FFFFFF", font: "Segoe Script", bold: false },
            { id: "typewriter", label: qsTr("Typewriter"), color: "#E8E8E8", font: "Consolas", bold: false } ] }
    ]
    // Effects: `amounts` draw the preview, `params` are the real clip parameters (values, not fractions); group = what is reset first
    readonly property var effectGroups: [
        { title: qsTr("Blur and sharpen"), items: [
            { id: "blurLight", label: qsTr("Light blur"), amounts: { blur: 0.12 }, params: { blur: 6 } },
            { id: "blur", label: qsTr("Soft blur"), amounts: { blur: 0.35 }, params: { blur: 17.5 } },
            { id: "blurStrong", label: qsTr("Strong blur"), amounts: { blur: 0.8 }, params: { blur: 40 } },
            { id: "sharpen", label: qsTr("Sharpen"), amounts: { sharpen: 0.5 }, params: { sharpen: 1.5 } },
            { id: "sharpenStrong", label: qsTr("Extra sharp"), amounts: { sharpen: 0.9 }, params: { sharpen: 2.7 } },
            { id: "reset", label: qsTr("Remove effects"), amounts: { reset: true }, params: {} } ] },
        { title: qsTr("Vignette and mood"), items: [
            { id: "vignette", label: qsTr("Vignette"), amounts: { vignette: 0.5 }, params: { vignette: 0.5 } },
            { id: "vignetteStrong", label: qsTr("Dark edges"), amounts: { vignette: 1 }, params: { vignette: 1 } },
            { id: "dream", label: qsTr("Dream"), amounts: { blur: 0.2, vignette: 0.35 }, params: { blur: 10, vignette: 0.35 } },
            { id: "softPortrait", label: qsTr("Soft portrait"), amounts: { blur: 0.1, vignette: 0.25 }, params: { blur: 5, vignette: 0.25 } },
            { id: "focus", label: qsTr("Focus"), amounts: { vignette: 0.7, sharpen: 0.5 }, params: { vignette: 0.7, sharpen: 1.5 } },
            { id: "crisp", label: qsTr("Crisp"), amounts: { sharpen: 0.8, vignette: 0.2 }, params: { sharpen: 2.4, vignette: 0.2 } } ] },
        { title: qsTr("Frame and zoom"), items: [
            { id: "letterbox", label: qsTr("Letterbox"), amounts: { barsV: 0.12 }, params: { cropT: 0.12, cropB: 0.12 } },
            { id: "letterboxWide", label: qsTr("Wide bars"), amounts: { barsV: 0.18 }, params: { cropT: 0.18, cropB: 0.18 } },
            { id: "cinematicBars", label: qsTr("Cinema bars"), amounts: { barsV: 0.12, vignette: 0.4 }, params: { cropT: 0.12, cropB: 0.12, vignette: 0.4 } },
            { id: "pillarbox", label: qsTr("Side bars"), amounts: { barsH: 0.1 }, params: { cropL: 0.1, cropR: 0.1 } },
            { id: "zoomIn", label: qsTr("Zoom 125%"), amounts: { zoom: 1.25 }, group: "", params: { scale: 1.25, rotation: 0 } },
            { id: "zoomBig", label: qsTr("Zoom 160%"), amounts: { zoom: 1.6 }, group: "", params: { scale: 1.6, rotation: 0 } },
            { id: "tilt", label: qsTr("Tilt right"), amounts: { tilt: 5, zoom: 1.12 }, group: "", params: { scale: 1.12, rotation: 5 } },
            { id: "tiltLeft", label: qsTr("Tilt left"), amounts: { tilt: -5, zoom: 1.12 }, group: "", params: { scale: 1.12, rotation: -5 } },
            { id: "normalSize", label: qsTr("Normal size"), amounts: {}, group: "", params: { scale: 1, rotation: 0 } } ] }
    ]
    readonly property var transitionInGroups: [
        { title: qsTr("Transition in (start of the clip)"), items: [
            { id: "dissolve", label: qsTr("Cross dissolve") }, { id: "dip", label: qsTr("Dip to black") }, { id: "fade", label: qsTr("Fade from black") },
            { id: "flash", label: qsTr("Flash") }, { id: "none", label: qsTr("None") } ] },
        { title: qsTr("Wipe"), items: [
            { id: "wipe", label: qsTr("Wipe right") }, { id: "wipeLeft", label: qsTr("Wipe left") },
            { id: "wipeDown", label: qsTr("Wipe down") }, { id: "wipeUp", label: qsTr("Wipe up") } ] },
        { title: qsTr("Slide"), items: [
            { id: "slide", label: qsTr("Slide from right") }, { id: "slideLeft", label: qsTr("Slide from left") },
            { id: "slideUp", label: qsTr("Slide from bottom") }, { id: "slideDown", label: qsTr("Slide from top") } ] },
        { title: qsTr("Zoom and spin"), items: [
            { id: "zoom", label: qsTr("Zoom in") }, { id: "zoomOut", label: qsTr("Zoom out") }, { id: "spin", label: qsTr("Spin") } ] }
    ]
    readonly property var transitionOutGroups: [
        { title: qsTr("Transition out (end of the clip)"), items: [
            { id: "fade", label: qsTr("Fade to black") }, { id: "dip", label: qsTr("Dip to black") }, { id: "flash", label: qsTr("Flash") },
            { id: "wipe", label: qsTr("Wipe out") }, { id: "slide", label: qsTr("Slide out") }, { id: "zoom", label: qsTr("Shrink away") },
            { id: "spin", label: qsTr("Spin") }, { id: "none", label: qsTr("None") } ] }
    ]
    // Looks: fractions of the way from the default to the end of the range (same as applyParams)
    readonly property var lookGroups: [
        { title: qsTr("Basic"), items: [
            { id: "vivid", label: qsTr("Vivid"), amounts: { saturation: 0.45, contrast: 0.2 } },
            { id: "warm", label: qsTr("Warm"), amounts: { temperature: 0.45, saturation: 0.1 } },
            { id: "cool", label: qsTr("Cool"), amounts: { temperature: -0.45, tint: -0.1 } },
            { id: "mono", label: qsTr("Black & white"), amounts: { saturation: -1, contrast: 0.15 } },
            { id: "faded", label: qsTr("Faded"), amounts: { contrast: -0.3, brightness: 0.12, saturation: -0.25 } },
            { id: "cinema", label: qsTr("Cinematic"), amounts: { contrast: 0.3, saturation: -0.15, temperature: -0.2 } },
            { id: "bright", label: qsTr("Bright"), amounts: { brightness: 0.2, contrast: 0.05 } },
            { id: "dark", label: qsTr("Dark"), amounts: { brightness: -0.2, contrast: 0.1 } },
            { id: "none", label: qsTr("Original"), amounts: {} } ] },
        { title: qsTr("Film"), items: [
            { id: "film", label: qsTr("Film"), amounts: { contrast: 0.2, saturation: -0.15, temperature: 0.2, brightness: 0.05 } },
            { id: "vintage", label: qsTr("Vintage"), amounts: { saturation: -0.35, temperature: 0.4, contrast: -0.1, tint: 0.1 } },
            { id: "sepia", label: qsTr("Sepia"), amounts: { saturation: -0.7, temperature: 0.6, contrast: 0.05 } },
            { id: "retro", label: qsTr("Retro"), amounts: { saturation: 0.2, temperature: 0.3, contrast: -0.2, tint: 0.15 } },
            { id: "noir", label: qsTr("Noir"), amounts: { saturation: -1, contrast: 0.5, brightness: -0.1 } },
            { id: "bleach", label: qsTr("Bleach"), amounts: { saturation: -0.5, contrast: 0.4 } } ] },
        { title: qsTr("Mood"), items: [
            { id: "sunset", label: qsTr("Sunset"), amounts: { temperature: 0.6, saturation: 0.3, brightness: 0.05 } },
            { id: "golden", label: qsTr("Golden hour"), amounts: { temperature: 0.5, saturation: 0.15, contrast: 0.1, brightness: 0.1 } },
            { id: "ocean", label: qsTr("Ocean"), amounts: { temperature: -0.6, tint: -0.15, saturation: 0.15 } },
            { id: "forest", label: qsTr("Forest"), amounts: { tint: -0.35, saturation: 0.25, temperature: -0.1 } },
            { id: "neon", label: qsTr("Neon night"), amounts: { saturation: 0.7, contrast: 0.3, tint: 0.4, temperature: -0.3 } },
            { id: "pastel", label: qsTr("Pastel"), amounts: { saturation: -0.3, brightness: 0.2, contrast: -0.25 } },
            { id: "moody", label: qsTr("Moody"), amounts: { brightness: -0.15, contrast: 0.3, saturation: -0.2, temperature: -0.15 } },
            { id: "tealOrange", label: qsTr("Teal and orange"), amounts: { temperature: 0.25, tint: -0.2, contrast: 0.25, saturation: 0.2 } },
            { id: "hdr", label: qsTr("Punchy"), amounts: { contrast: 0.4, saturation: 0.4 } } ] }
    ]

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
    // A catalog of tiles in groups: groups = [{ title, items: [{ id, label, ... }] }]. `kind` is "look" | "effect" | "transition" | "text".
    component Catalog: ColumnLayout {
        id: cat
        property var groups: []
        property string prefix
        property string kind: "look"
        property string direction: "in"
        signal picked(var item)
        Layout.fillWidth: true
        spacing: 12
        Repeater {
            model: cat.groups
            delegate: ColumnLayout {
                id: groupBox
                required property var modelData
                Layout.fillWidth: true
                spacing: 8
                Heading { text: groupBox.modelData.title }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    uniformCellWidths: true
                    columnSpacing: 10
                    rowSpacing: 10
                    Repeater {
                        model: cat.kind === "text" ? [] : groupBox.modelData.items
                        delegate: PreviewTile {
                            required property var modelData
                            objectName: cat.prefix + modelData.id
                            kind: cat.kind
                            amounts: modelData.amounts || ({})
                            transition: cat.kind === "transition" ? modelData.id : "none"
                            direction: cat.direction
                            label: modelData.label
                            onActivated: cat.picked(modelData)
                        }
                    }
                    Repeater {
                        model: cat.kind === "text" ? groupBox.modelData.items : []
                        delegate: TextTile {
                            required property var modelData
                            objectName: cat.prefix + modelData.id
                            textStyle: modelData
                            onActivated: cat.picked(modelData)
                        }
                    }
                }
            }
        }
    }
    // a text style: the words drawn in that style
    component TextTile: Rectangle {
        id: tt
        property var textStyle
        signal activated()
        Layout.fillWidth: true
        implicitWidth: 104
        implicitHeight: 84
        radius: Theme.radius
        color: tma.containsMouse ? Theme.surfaceHover : Theme.surface
        border.width: 1
        border.color: tma.containsMouse ? Qt.alpha(Theme.accent, 0.7) : Theme.borderSubtle
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 4
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 6
                color: "#14161C"
                clip: true
                Text {
                    anchors.centerIn: parent
                    width: parent.width - 8
                    horizontalAlignment: Text.AlignHCenter
                    text: "Aa"
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    font.family: tt.textStyle.font || "Noto Sans Thai"
                    font.pixelSize: 24
                    font.bold: tt.textStyle.bold !== false
                    color: tt.textStyle.color || "#FFFFFF"
                    style: (tt.textStyle.stroke || "") !== "" ? Text.Outline : (tt.textStyle.shadow ? Text.Raised : Text.Normal)
                    styleColor: (tt.textStyle.stroke || "") !== "" ? tt.textStyle.stroke : "#000000"
                }
            }
            Text {
                Layout.fillWidth: true
                text: tt.textStyle.label
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.fontXs
                font.weight: Font.Medium
                color: Theme.text
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }
        MouseArea {
            id: tma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: tt.activated()
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
        elide: Text.ElideRight
        maximumLineCount: 1
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
            currentIndex: root.tabIndex

            // 0 Import: the project media browser
            MediaPanel {
                id: mediaPanel
                color: "transparent"
                border.width: 0
                radius: 0
            }

            // 1 Audio: bring sound in (the mixer is a tab of the timeline panel)
            ScrollView {
                id: audioPage
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: audioPage.availableWidth - 28
                    x: 14
                    y: 12
                    spacing: 12
                    Heading { text: qsTr("Audio") }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        uniformCellWidths: true
                        columnSpacing: 10
                        rowSpacing: 10
                        Tile { objectName: "audio_import"; iconName: "import"; label: qsTr("Import audio"); onActivated: root.openImport() }
                        Tile { objectName: "audio_addTrack"; iconName: "audio"; label: qsTr("Add audio track"); onActivated: root.report(Projects.addTrack("audio")) }
                        Tile { objectName: "audio_mixer"; iconName: "sliders"; label: qsTr("Open mixer"); onActivated: root.mixerRequested() }
                    }
                    Hint { text: qsTr("Import music or sound effects with the Import tab and drag them to an audio track. The mixer (levels, EQ, pan) is the Mixer tab of the timeline panel; the master level is always shown at its right.") }
                }
            }

            // 2 Text: styles (the same table as TimelineModel::addTitle)
            ScrollView {
                id: textPage
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: textPage.availableWidth - 28
                    x: 14
                    y: 12
                    spacing: 12
                    Catalog { prefix: "text_"; kind: "text"; groups: root.textGroups; onPicked: item => root.report(Projects.timeline.addTitle(-1, Projects.playheadFrame, item.id)) }
                    Hint { text: qsTr("A style adds a text clip at the playhead. Select it on the timeline to change its words, font, colour and place in the inspector. Automatic captions are in the AI tab.") }
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
                    Catalog { prefix: "fx_"; kind: "effect"; groups: root.effectGroups; onPicked: item => root.applyAbs(item.group === undefined ? "effects" : item.group, item.params) }
                    Hint { text: qsTr("An effect replaces the effects of the selected clip with its setting (zoom and tilt change the size and rotation of the clip; \"Normal size\" undoes them). Fine control is in the inspector.") }
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
                    Catalog { prefix: "trIn_"; kind: "transition"; direction: "in"; groups: root.transitionInGroups; onPicked: item => root.setTransition("in", item.id === "none" ? "" : item.id) }
                    Catalog { prefix: "trOut_"; kind: "transition"; direction: "out"; groups: root.transitionOutGroups; onPicked: item => root.setTransition("out", item.id === "none" ? "" : item.id) }
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
                    Catalog { prefix: "look_"; kind: "look"; groups: root.lookGroups; onPicked: item => root.applyParams("color", item.amounts) }
                    Hint { text: qsTr("A look sets the colour controls (brightness, contrast, saturation ...) of the clip; change them freely in the Adjustment tab. Your own LUT files are imported in the inspector.") }
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

            // 7 AI: assistant, captions, silence, smart cuts, enhancement, tracking (offline); shown here, where its tab is
            AiPanel {
                id: aiPage
            }
        }
    }
}
