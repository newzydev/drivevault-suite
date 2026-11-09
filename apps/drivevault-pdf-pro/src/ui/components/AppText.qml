import QtQuick

// Text with the same line height in Thai and English (see ThaiMetrics): use this instead of Text everywhere in the UI.
Text {
    lineHeightMode: Text.FixedHeight
    lineHeight: Math.round((font.pixelSize > 0 ? font.pixelSize : 13) * ThaiMetrics.thaiLine)
    color: Theme.text
    font.pixelSize: Theme.fontMd
}
