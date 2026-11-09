import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DriveVault.PdfPro.Backend

// Text on many pages at once: a watermark, page numbers, a header, a footer. {n} is the page number, {total} the number of pages.
Popup {
    id: dlg
    objectName: "markDialog"

    property string type: "watermark"
    property color markColor: "#808080"
    readonly property var swatches: ["#000000", "#EF4444", "#F59E0B", "#22C55E", "#3B82F6", "#8B5CF6", "#FFFFFF", "#6B7280"]

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    dim: true
    width: Math.min(560, (parent ? parent.width : 560) - 48)
    padding: 24
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#B3000000" }
    background: Rectangle { radius: Theme.radius; color: Theme.backgroundAlt; border.color: Theme.border }

    function load(t) {
        type = t
        const m = Object.keys(Editor.mark(t)).length > 0 ? Editor.mark(t) : Editor.markDefaults(t)
        textField.text = m.text
        fontBox.currentIndex = Math.max(0, Editor.fonts.indexOf(m.font))
        sizeBox.value = Math.round(m.size)
        markColor = m.color
        opacitySlider.value = Math.round(m.opacity * 100)
        fromBox.value = m.from
        toBox.value = m.to
        alignBox.currentIndex = m.align
        boldBox.checked = m.bold
    }
    function apply() {
        Editor.setMark(type, { text: textField.text, font: fontBox.currentText, size: sizeBox.value, color: markColor, opacity: opacitySlider.value / 100,
                               from: fromBox.value, to: toBox.value, align: alignBox.currentIndex, bold: boldBox.checked })
    }
    onAboutToShow: load(type)

    contentItem: ColumnLayout {
        spacing: 10
        AppText { Layout.fillWidth: true; text: Tr.s("marksTitle"); font.pixelSize: Theme.fontXl; font.weight: Font.DemiBold }
        ButtonGroup { id: types }
        Flow {
            Layout.fillWidth: true
            spacing: 14
            Repeater {
                model: ["watermark", "pageNumber", "header", "footer"]
                delegate: RadioButton {
                    id: rb
                    required property string modelData
                    objectName: "mark_" + modelData
                    ButtonGroup.group: types
                    text: Tr.s("mark_" + modelData)
                    checked: dlg.type === modelData
                    contentItem: AppText { leftPadding: 28; text: rb.text; verticalAlignment: Text.AlignVCenter }
                    onClicked: dlg.load(modelData)
                }
            }
        }
        AppText { Layout.fillWidth: true; text: Tr.s("markHint"); color: Theme.textDisabled; font.pixelSize: Theme.fontSm; wrapMode: Text.Wrap }
        TextField {
            id: textField
            objectName: "markText"
            Layout.fillWidth: true
            implicitHeight: 38
            color: Theme.text
            font.pixelSize: Theme.fontMd
            leftPadding: 10
            selectByMouse: true
            background: Rectangle { radius: 8; color: Theme.surface; border.color: textField.activeFocus ? Theme.accent : Theme.border }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            ComboBox {
                id: fontBox
                objectName: "markFont"
                Layout.fillWidth: true
                implicitHeight: 34
                model: Editor.fonts
                font.pixelSize: Theme.fontMd
                contentItem: AppText { leftPadding: 10; text: fontBox.displayText; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: 8; color: Theme.surface; border.color: fontBox.activeFocus ? Theme.accent : Theme.border }
            }
            SpinBox { id: sizeBox; objectName: "markSize"; from: 4; to: 400; value: 12; editable: true }
            CheckBox { id: boldBox; text: "B"; contentItem: AppText { leftPadding: 28; text: parent.text; font.weight: Font.Bold; verticalAlignment: Text.AlignVCenter } }
        }
        RowLayout {
            spacing: 6
            Repeater {
                model: dlg.swatches
                delegate: Rectangle {
                    required property string modelData
                    width: 22; height: 22; radius: 11
                    color: modelData
                    border.width: Qt.colorEqual(dlg.markColor, modelData) ? 3 : 1
                    border.color: Qt.colorEqual(dlg.markColor, modelData) ? Theme.accent : Theme.border
                    TapHandler { onTapped: dlg.markColor = parent.color }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }
            Item { Layout.fillWidth: true }
            AppText { text: Tr.s("opacityLabel"); color: Theme.textSecondary }
            Slider { id: opacitySlider; objectName: "markOpacity"; implicitWidth: 120; from: 5; to: 100; stepSize: 5; value: 100 }
        }
        RowLayout {
            spacing: 10
            AppText { text: Tr.s("markPages"); color: Theme.textSecondary }
            SpinBox { id: fromBox; objectName: "markFrom"; from: 1; to: Math.max(1, Doc.pageCount); value: 1; editable: true }
            AppText { text: "–"; color: Theme.textSecondary }
            SpinBox { id: toBox; objectName: "markTo"; from: 0; to: Math.max(1, Doc.pageCount); value: 0; editable: true; textFromValue: function (v) { return v === 0 ? Tr.s("lastPage") : v } }
            Item { Layout.fillWidth: true }
            AppText { visible: dlg.type !== "watermark"; text: Tr.s("alignment"); color: Theme.textSecondary }
            ComboBox {
                id: alignBox
                objectName: "markAlign"
                visible: dlg.type !== "watermark"
                model: [Tr.s("alignLeft"), Tr.s("alignCenter"), Tr.s("alignRight")]
                implicitHeight: 34
                currentIndex: 1
            }
        }
        RowLayout {
            Layout.fillWidth: true
            PButton { objectName: "markRemove"; variant: "ghost"; text: Tr.s("removeMark"); enabled: Editor.count >= 0 && Object.keys(Editor.mark(dlg.type)).length > 0; onClicked: { Editor.removeMark(dlg.type); dlg.load(dlg.type) } }
            Item { Layout.fillWidth: true }
            PButton { objectName: "markClose"; variant: "ghost"; text: Tr.s("close"); onClicked: dlg.close() }
            PButton { objectName: "markApply"; variant: "primary"; text: Tr.s("apply"); onClicked: dlg.apply() }
        }
    }
}
