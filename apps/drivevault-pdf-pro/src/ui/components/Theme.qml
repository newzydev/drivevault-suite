pragma Singleton
import QtQuick

// Design tokens: the same neutral dark surfaces as the other DriveVault programs; the accent is the PDF Pro red (#EF4444, see docs/suite-integration.md).
QtObject {
    readonly property color accent: "#EF4444"
    readonly property color accentHover: Qt.lighter(accent, 1.18)
    readonly property color accentPressed: Qt.darker(accent, 1.2)
    readonly property color accentSubtle: Qt.alpha(accent, 0.16)
    readonly property color accentText: Qt.lighter(accent, 1.3)
    readonly property color textOnAccent: "#FFFFFF"

    readonly property color background: "#101114"
    readonly property color backgroundAlt: "#181A20"
    readonly property color surface: "#20232B"
    readonly property color surfaceHover: "#2F333E"
    readonly property color border: "#343844"
    readonly property color borderSubtle: "#272A33"
    readonly property color viewerBackground: "#2A2D35"

    readonly property color text: "#F8FAFC"
    readonly property color textSecondary: "#A1A1AA"
    readonly property color textDisabled: "#6B6F7B"

    readonly property color success: "#22C55E"
    readonly property color warning: "#F59E0B"
    readonly property color error: "#EF4444"
    readonly property color errorSoft: "#F87171"

    readonly property int radiusSmall: 6
    readonly property int radius: 10
    readonly property int radiusLarge: 16

    // Thai glyphs have a smaller x-height than Latin ones, so both languages use the Thai size and look the same.
    readonly property int fontSm: 13
    readonly property int fontMd: 14
    readonly property int fontLg: 16
    readonly property int fontXl: 19
    readonly property int fontHero: 34
}
