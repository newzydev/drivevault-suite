import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Master.Backend

// The settings of an AI page: which AI it uses (a local model, an API, Claude Code or Codex) and what that AI needs. Everything here is saved on this PC;
// an API key is stored encrypted for this Windows user. Nothing is downloaded until a Download button is pressed.
Popup {
    id: dlg

    property var channel
    property bool agent: false
    readonly property string provider: channel ? channel.provider : ""

    // the fields start from the saved values each time the window opens
    property string openaiBase: ""
    property string openaiModel: ""
    property string openaiKey: ""
    property string anthropicModel: ""
    property string anthropicKey: ""
    property string notice: ""
    property string modelLink: ""

    function load() {
        openaiBase = Ai.openai.baseUrl
        openaiModel = Ai.openai.model
        openaiKey = ""
        anthropicModel = Ai.anthropic.model
        anthropicKey = ""
        notice = ""
        Ai.detectPrograms()
    }
    function say(text) { notice = text; noticeTimer.restart() }
    function sizeOf(x) { return x }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(780, parent.width - 48)
    height: Math.min(700, parent.height - 48)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    onAboutToShow: load()
    Overlay.modal: Rectangle { color: "#B3000000" }
    background: Rectangle { radius: 16; color: Theme.backgroundAlt; border.width: 1; border.color: Theme.border }
    Timer { id: noticeTimer; interval: 2500; onTriggered: dlg.notice = "" }

    component Heading: Text { font.pixelSize: 12; font.weight: Font.DemiBold; font.capitalization: Font.AllUppercase; font.letterSpacing: 0.6; color: Theme.textSecondary; Layout.topMargin: 18 }
    component Note: Text { Layout.fillWidth: true; font.pixelSize: 13; color: Theme.textSecondary; wrapMode: Text.Wrap }
    component Card: Rectangle {
        default property alias content: cardColumn.data
        Layout.fillWidth: true
        implicitHeight: cardColumn.implicitHeight + 24
        radius: 12
        color: Theme.surface
        border.width: 1
        border.color: Theme.borderSubtle
        ColumnLayout { id: cardColumn; anchors.fill: parent; anchors.margins: 12; spacing: 8 }
    }

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.bottomMargin: 8
            Text { text: Tr.s("ai_settings"); font.pixelSize: 18; font.weight: Font.Bold; color: Theme.text; Layout.fillWidth: true }
            Text { visible: dlg.notice !== ""; text: dlg.notice; font.pixelSize: 13; color: Theme.success }
            MButton { objectName: "aiSettingsClose"; compact: true; text: Tr.s("close"); onClicked: dlg.close() }
        }
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 20
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: scroll.availableWidth - 18
                spacing: 8

                // ---- which AI this page uses -----------------------------------------------------------------------------------
                Heading { text: Tr.s(dlg.agent ? "set_provider_agent" : "set_provider_ask"); Layout.topMargin: 4 }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: dlg.channel ? dlg.channel.providers : []
                        delegate: AbstractButton {
                            id: pbtn
                            required property string modelData
                            objectName: "provider_" + modelData
                            readonly property bool picked: dlg.provider === modelData
                            hoverEnabled: true
                            implicitHeight: 36
                            implicitWidth: ptext.implicitWidth + 32
                            onClicked: dlg.channel.setProvider(modelData)
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            contentItem: Text {
                                id: ptext
                                text: Tr.s("prov_" + pbtn.modelData)
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                color: pbtn.picked ? Theme.readableOn(Theme.accent) : Theme.text
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle { radius: 18; color: pbtn.picked ? Theme.accent : pbtn.hovered ? Theme.surfaceHover : Theme.surface; border.width: 1; border.color: pbtn.picked ? Theme.accent : Theme.border }
                        }
                    }
                }
                Note { text: Tr.s("set_provider_hint"); visible: dlg.provider === "" }
                Note { text: Tr.s("privacy_" + (dlg.provider === "local" && Ai.webEnabled ? "local_web" : dlg.provider)); visible: dlg.provider !== "" }

                // ---- a model on this computer ----------------------------------------------------------------------------------------
                ColumnLayout {
                    visible: dlg.provider === "local"
                    Layout.fillWidth: true
                    spacing: 8

                    Heading { text: Tr.s("set_web") }
                    Card {
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text { Layout.fillWidth: true; text: Tr.s("set_web_on"); font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text }
                            AiSwitch { objectName: "webSwitch"; checked: Ai.webEnabled; onToggled: Ai.webEnabled = checked }
                        }
                        Text { Layout.fillWidth: true; text: Tr.s("set_web_note"); font.pixelSize: 12; color: Theme.textSecondary; wrapMode: Text.Wrap }
                    }

                    Heading { text: Tr.s("set_engine") }
                    Card {
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle { width: 8; height: 8; radius: 4; color: Ai.engineFound ? Theme.success : Theme.warning }
                            Text { Layout.fillWidth: true; text: Ai.engineFound ? Tr.s("set_engine_ok") : Tr.s("why_noEngine"); font.pixelSize: 13; color: Theme.text; wrapMode: Text.Wrap }
                        }
                    }

                    Heading { text: Tr.s("set_models_here") }
                    Note { visible: Ai.installedModels.length === 0; text: Tr.s("set_models_none") }
                    Repeater {
                        model: Ai.installedModels
                        delegate: Card {
                            id: installedCard
                            required property var modelData
                            readonly property bool inUse: Ai.localModel === modelData.path
                            RowLayout {
                            Layout.fillWidth: true
                                spacing: 10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { text: installedCard.modelData.name; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                    Text { Layout.fillWidth: true; text: installedCard.modelData.sizeText; font.pixelSize: 12; color: Theme.textSecondary }
                                }
                                MButton {
                                    objectName: "useModel"
                                    compact: true
                                    variant: installedCard.inUse ? "secondary" : "primary"
                                    enabled: !installedCard.inUse
                                    text: Tr.s(installedCard.inUse ? "set_in_use" : "set_use")
                                    onClicked: Ai.selectLocalModel(installedCard.modelData.path)
                                }
                            }
                        }
                    }

                    Heading { text: Tr.s("set_catalog") }
                    Note { text: Tr.s("set_catalog_note") }
                    Repeater {
                        model: Ai.catalog
                        delegate: Card {
                            id: catalogCard
                            required property var modelData
                            RowLayout {
                            Layout.fillWidth: true
                                spacing: 10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { Layout.fillWidth: true; text: catalogCard.modelData.title; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text }
                                    Text { Layout.fillWidth: true; text: catalogCard.modelData.sizeText + "  -  " + Tr.s("level_" + catalogCard.modelData.level); font.pixelSize: 12; color: Theme.textSecondary }
                                }
                                Text { visible: catalogCard.modelData.installed; text: Tr.s("set_installed"); font.pixelSize: 13; color: Theme.success }
                                MButton {
                                    objectName: "download_" + catalogCard.modelData.id
                                    visible: !catalogCard.modelData.installed
                                    compact: true
                                    enabled: !Ai.downloading
                                    text: Tr.s("set_download") + " (" + catalogCard.modelData.sizeText + ")"
                                    onClicked: { const e = Ai.downloadCatalog(catalogCard.modelData.id); if (e !== "") dlg.say(e) }
                                }
                            }
                        }
                    }

                    Heading { text: Tr.s("set_hf") }
                    Note { text: Tr.s("set_hf_note") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        MField { id: linkField; objectName: "hfLink"; Layout.fillWidth: true; placeholderText: "https://huggingface.co/owner/repo/resolve/main/model.gguf"; text: dlg.modelLink; onTextEdited: dlg.modelLink = text }
                        MButton { objectName: "hfCheck"; text: Tr.s("set_check"); enabled: linkField.text.trim() !== "" && Ai.probe.state !== "checking"; onClicked: Ai.probeLink(linkField.text) }
                    }
                    Card {
                        visible: Ai.probe.state === "checking" || Ai.probe.state === "ready" || Ai.probe.state === "failed"
                        Text {
                            Layout.fillWidth: true
                            text: Ai.probe.state === "checking" ? Tr.s("set_hf_checking")
                                  : Ai.probe.state === "failed" ? Tr.s("hf_" + Ai.probe.error)
                                  : Ai.probe.fileName + "\n" + Ai.probe.repo + "  -  " + Ai.probe.sizeText + "\n" + Tr.s("set_hf_ok")
                            font.pixelSize: 13
                            color: Ai.probe.state === "failed" ? Theme.errorSoft : Theme.text
                            wrapMode: Text.Wrap
                        }
                        MButton {
                            objectName: "hfDownload"
                            visible: Ai.probe.state === "ready"
                            compact: true
                            variant: "primary"
                            enabled: !Ai.downloading
                            text: Tr.s("set_download") + " (" + Ai.probe.sizeText + ")"
                            onClicked: { const e = Ai.downloadProbed(); if (e !== "") dlg.say(e) }
                        }
                    }

                    Card {
                        objectName: "downloadCard"
                        visible: Ai.downloading || Ai.downloadCode === "failed" || Ai.downloadCode === "cancelled"
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: Ai.downloadTitle + "  -  " + Tr.s("dl_" + Ai.downloadCode); font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text; elide: Text.ElideRight }
                            MButton { compact: true; visible: Ai.downloading; text: Tr.s("cancel"); onClicked: Ai.cancelDownload() }
                        }
                        ProgressBar { Layout.fillWidth: true; visible: Ai.downloading; value: Ai.downloadProgress }
                        Text { visible: Ai.downloadMessage !== ""; Layout.fillWidth: true; text: Ai.downloadMessage; font.pixelSize: 12; color: Theme.errorSoft; wrapMode: Text.Wrap }
                    }
                    Note { text: Tr.s("set_models_folder") + "  " + Ai.modelsFolder }
                }

                // ---- an API: OpenAI style ------------------------------------------------------------------------------------------------
                ColumnLayout {
                    visible: dlg.provider === "openai"
                    Layout.fillWidth: true
                    spacing: 8
                    Heading { text: Tr.s("set_api_address") }
                    MField { objectName: "openaiBase"; Layout.fillWidth: true; text: dlg.openaiBase; onTextEdited: dlg.openaiBase = text; placeholderText: "https://api.openai.com/v1" }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: [ { n: "OpenAI", u: "https://api.openai.com/v1" }, { n: "OpenRouter", u: "https://openrouter.ai/api/v1" },
                                     { n: "Ollama", u: "http://localhost:11434/v1" }, { n: "LM Studio", u: "http://localhost:1234/v1" } ]
                            delegate: AbstractButton {
                                id: preset
                                required property var modelData
                                hoverEnabled: true
                                implicitHeight: 28
                                implicitWidth: presetText.implicitWidth + 24
                                onClicked: dlg.openaiBase = modelData.u
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                contentItem: Text { id: presetText; text: preset.modelData.n; font.pixelSize: 12; color: Theme.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: 14; color: preset.hovered ? Theme.surfaceHover : "transparent"; border.width: 1; border.color: Theme.border }
                            }
                        }
                    }
                    Heading { text: Tr.s("set_api_model") }
                    MField { objectName: "openaiModel"; Layout.fillWidth: true; text: dlg.openaiModel; onTextEdited: dlg.openaiModel = text; placeholderText: Tr.s("set_api_model_ph") }
                    Heading { text: Tr.s("set_api_key") }
                    MField { objectName: "openaiKey"; Layout.fillWidth: true; secret: true; text: dlg.openaiKey; onTextEdited: dlg.openaiKey = text
                             placeholderText: Ai.openai.keySet ? Tr.s("set_key_saved") : Tr.s("set_key_ph") }
                    Note { text: Tr.s("set_key_note") }
                    RowLayout {
                        spacing: 8
                        MButton { objectName: "openaiSave"; variant: "primary"; text: Tr.s("set_save"); onClicked: { Ai.setOpenAi(dlg.openaiBase, dlg.openaiModel, dlg.openaiKey); dlg.openaiKey = ""; dlg.say(Tr.s("set_saved")) } }
                        MButton { visible: Ai.openai.keySet; text: Tr.s("set_clear_key"); onClicked: { Ai.clearKey("openai"); dlg.say(Tr.s("set_saved")) } }
                    }
                }

                // ---- an API: Anthropic ---------------------------------------------------------------------------------------------------
                ColumnLayout {
                    visible: dlg.provider === "anthropic"
                    Layout.fillWidth: true
                    spacing: 8
                    Heading { text: Tr.s("set_api_model") }
                    MField { objectName: "anthropicModel"; Layout.fillWidth: true; text: dlg.anthropicModel; onTextEdited: dlg.anthropicModel = text; placeholderText: "claude-sonnet-5-5" }
                    Heading { text: Tr.s("set_api_key") }
                    MField { objectName: "anthropicKey"; Layout.fillWidth: true; secret: true; text: dlg.anthropicKey; onTextEdited: dlg.anthropicKey = text
                             placeholderText: Ai.anthropic.keySet ? Tr.s("set_key_saved") : "sk-ant-…" }
                    Note { text: Tr.s("set_key_note") }
                    RowLayout {
                        spacing: 8
                        MButton { objectName: "anthropicSave"; variant: "primary"; text: Tr.s("set_save"); onClicked: { Ai.setAnthropic(dlg.anthropicModel, dlg.anthropicKey); dlg.anthropicKey = ""; dlg.say(Tr.s("set_saved")) } }
                        MButton { visible: Ai.anthropic.keySet; text: Tr.s("set_clear_key"); onClicked: { Ai.clearKey("anthropic"); dlg.say(Tr.s("set_saved")) } }
                    }
                }

                // ---- Claude Code / Codex ---------------------------------------------------------------------------------------------------
                ColumnLayout {
                    visible: dlg.provider === "claude" || dlg.provider === "codex"
                    Layout.fillWidth: true
                    spacing: 8
                    Heading { text: Tr.s("set_program") }
                    Card {
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle { width: 8; height: 8; radius: 4; color: Ai.cli[dlg.provider] && Ai.cli[dlg.provider].found ? Theme.success : Theme.warning }
                            Text {
                                Layout.fillWidth: true
                                text: Ai.cli[dlg.provider] && Ai.cli[dlg.provider].found
                                      ? Ai.cli[dlg.provider].path + (Ai.cli[dlg.provider].version !== "" ? "\n" + Ai.cli[dlg.provider].version : "")
                                      : Tr.s("why_noProgram")
                                font.pixelSize: 13
                                color: Theme.text
                                wrapMode: Text.WrapAnywhere
                            }
                            MButton { compact: true; text: Tr.s("set_check_again"); onClicked: Ai.detectPrograms() }
                        }
                    }
                    Note { text: Tr.s("set_signin_" + dlg.provider) }
                    Note { text: Tr.s("set_program_note") }
                }
            }
        }
    }
}
