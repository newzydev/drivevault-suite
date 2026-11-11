pragma Singleton
import QtQuick
import DriveVault.PdfPro.Backend

// Design tokens: the same neutral dark surfaces as the other DriveVault programs; the accent is the PDF Pro red (#EF4444, see docs/suite-integration.md).
QtObject {
    readonly property bool light: Doc.lightTheme
    readonly property color accent: light ? "#b91c32" : "#EF4444"
    readonly property color accentHover: light ? "#a2182b" : Qt.lighter(accent, 1.18)
    readonly property color accentPressed: Qt.darker(accent, 1.2)
    readonly property color accentSubtle: Qt.alpha(accent, 0.16)
    readonly property color accentText: light ? "#a2172c" : Qt.lighter(accent, 1.3)
    readonly property color textOnAccent: "#FFFFFF"

    readonly property color background: light ? "#f7f7fa" : "#19191c"
    readonly property color backgroundAlt: light ? "#ffffff" : "#252529"
    readonly property color surface: light ? "#f0eef3" : "#2c2c31"
    readonly property color surfaceHover: light ? "#e7e3ec" : "#38383e"
    readonly property color border: light ? "#d4cedb" : "#414148"
    readonly property color borderSubtle: light ? "#e4dfe8" : "#34343a"
    readonly property color viewerBackground: light ? "#e7e4ec" : "#303035"
    readonly property color titleBar: light ? "#ffffff" : "#0d0e11"
    readonly property color toolCard: light ? "#ffffff" : "#232328"
    readonly property color editBar: light ? "#ffffff" : "#14161b"

    readonly property color text: light ? "#25212c" : "#efeff2"
    readonly property color textSecondary: light ? "#5c5366" : "#b5b5bf"
    readonly property color textDisabled: light ? "#766e80" : "#90909a"

    readonly property color success: light ? "#166534" : "#22C55E"
    readonly property color warning: light ? "#92400e" : "#F59E0B"
    readonly property color error: light ? "#b91c32" : "#EF4444"
    readonly property color errorSoft: light ? "#a2172c" : "#F87171"

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
