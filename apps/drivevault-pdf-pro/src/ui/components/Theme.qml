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

    readonly property color background: "#19191c"
    readonly property color backgroundAlt: "#252529"
    readonly property color surface: "#2c2c31"
    readonly property color surfaceHover: "#38383e"
    readonly property color border: "#414148"
    readonly property color borderSubtle: "#34343a"
    readonly property color viewerBackground: "#303035"

    readonly property color text: "#efeff2"
    readonly property color textSecondary: "#b5b5bf"
    readonly property color textDisabled: "#90909a"

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
