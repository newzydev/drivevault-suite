pragma Singleton
import QtQuick

// The line height of a Thai line, as a multiple of the font size. Thai glyphs come from a font with taller line metrics than Latin text, so Thai
// lines are taller than English ones; AppText gives every line the Thai height, so both languages lay out the same.
// (Not named TextMetrics: QtQuick already has a type with that name and it would win.)
Item {
    readonly property real thaiLine: probe.implicitHeight / 100

    Text {
        id: probe
        visible: false
        text: "ก"
        font.pixelSize: 100
    }
}
