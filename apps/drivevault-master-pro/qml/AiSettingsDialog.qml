import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.Master.Backend

// The settings of the AI pages (both use the model on this PC): the web switch, the engine, the models and their download. Everything here is saved on this
// PC. Nothing is downloaded until a Download button is pressed.
Popup {
    id: dlg

    property var channel
    property bool agent: false

    property string notice: ""
    property string modelLink: ""

    function load() { notice = "" }
    function say(text) { notice = text; noticeTimer.restart() }

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

                // ---- a model on this computer ----------------------------------------------------------------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Note { text: Tr.s(Ai.useRemote ? (Ai.webEnabled ? "privacy_remote_web" : "privacy_remote") : (Ai.webEnabled ? "privacy_local_web" : "privacy_local")) }
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

                    // online models: the user's own Hugging Face token (kept sealed by Windows, never shown again); a model is chosen in the menu of the message box
                    Heading { text: Tr.s("set_remote") }
                    Note { text: Tr.s("set_remote_intro") }
                    Repeater {
                        model: Ai.remoteProviders
                        delegate: Card {
                            id: providerCard
                            required property var modelData
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text { text: providerCard.modelData.name; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text }
                                Rectangle {
                                    radius: 8; color: Qt.alpha(providerCard.modelData.free ? Theme.success : Theme.warning, 0.16)
                                    implicitWidth: freeTag.implicitWidth + 14; implicitHeight: 20
                                    Text { id: freeTag; anchors.centerIn: parent; font.pixelSize: 11; color: providerCard.modelData.free ? Theme.success : Theme.warning
                                           text: Tr.s(providerCard.modelData.free ? "remote_free" : "remote_paid") }
                                }
                                Item { Layout.fillWidth: true }
                                Text { text: providerCard.modelData.keyHost; font.pixelSize: 11; color: Theme.textSecondary }
                            }
                            Text { Layout.fillWidth: true; text: Tr.s("set_remote_help_" + providerCard.modelData.id); font.pixelSize: 12; color: Theme.textSecondary; wrapMode: Text.Wrap }
                            RowLayout {
                                Layout.fillWidth: true
                                visible: providerCard.modelData.keySet
                                spacing: 8
                                Rectangle { width: 8; height: 8; radius: 4; color: Theme.success }
                                Text { Layout.fillWidth: true; text: Tr.s("set_remote_saved").replace("%1", providerCard.modelData.keyHint); font.pixelSize: 13; color: Theme.text; wrapMode: Text.Wrap }
                                MButton { objectName: "remoteKeyClear_" + providerCard.modelData.id; compact: true; text: Tr.s("set_remote_clear"); onClicked: Ai.clearRemoteKey(providerCard.modelData.id) }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                MField { id: keyField; objectName: "remoteKeyField_" + providerCard.modelData.id; Layout.fillWidth: true; secret: true; placeholderText: Tr.s("set_remote_ph"); onAccepted: keySave.clicked() }
                                MButton {
                                    id: keySave
                                    objectName: "remoteKeySave_" + providerCard.modelData.id
                                    variant: "primary"
                                    text: Tr.s("set_remote_save")
                                    enabled: keyField.text.trim() !== ""
                                    onClicked: {
                                        const e = Ai.setRemoteKey(providerCard.modelData.id, keyField.text)
                                        if (e === "") { keyField.clear(); dlg.say(Tr.s("set_remote_ok")) }
                                        else dlg.say(Tr.s("set_remote_err_" + e))
                                    }
                                }
                            }
                        }
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

                    // the vision file of the chosen model: pictures for the AI (it looks at the page it made); a download only when this button is pressed
                    Heading { text: Tr.s("vision_title") }
                    Card {
                        visible: Ai.localModel !== ""
                        Text { Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12; color: Theme.textSecondary
                               text: Ai.vision.offered ? Tr.s("vision_help").replace("%1", Ai.vision.sizeText) : Tr.s("vision_none") }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: Ai.vision.offered
                            spacing: 8
                            Rectangle { width: 8; height: 8; radius: 4; color: Ai.vision.installed ? Theme.success : Theme.textDisabled }
                            Text { Layout.fillWidth: true; font.pixelSize: 13; color: Theme.text; text: Ai.vision.installed ? Tr.s("vision_installed") : "" }
                            MButton {
                                objectName: "downloadVision"
                                compact: true
                                variant: "primary"
                                visible: !Ai.vision.installed
                                enabled: !Ai.downloading
                                text: Tr.s("vision_download")
                                onClicked: Ai.downloadVision()
                            }
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
            }
        }
    }
}
