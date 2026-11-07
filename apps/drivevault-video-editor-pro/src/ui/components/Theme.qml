pragma Singleton
import QtQuick
import DriveVault.Backend

// Design tokens for the DriveVault design system (see docs/ui-design-system.md).
QtObject {
    id: theme

    readonly property bool dark: AppSettings.values.theme !== "light"
    readonly property color accent: AppSettings.values.accentColor
    readonly property bool animated: AppSettings.values.animations

    // ---- Surfaces ----------------------------------------------------------------------------
    readonly property color background: dark ? "#101114" : "#ECEEF3"
    readonly property color backgroundAlt: dark ? "#181A20" : "#F6F7FA"
    readonly property color surface: dark ? "#20232B" : "#FFFFFF"
    readonly property color surfaceElevated: dark ? "#292D36" : "#FFFFFF"
    readonly property color surfaceHover: dark ? "#2F333E" : "#EEF0F5"
    readonly property color border: dark ? "#343844" : "#D3D7E0"
    readonly property color borderSubtle: dark ? "#272A33" : "#E1E4EB"
    readonly property color overlay: dark ? "#B3000000" : "#8C1A1C24"

    // ---- Text --------------------------------------------------------------------------------
    readonly property color text: dark ? "#F8FAFC" : "#14161B"
    readonly property color textSecondary: dark ? "#A1A1AA" : "#565B6B"
    readonly property color textDisabled: dark ? "#6B6F7B" : "#8E93A3"

    // ---- Accent ------------------------------------------------------------------------------
    readonly property color accentHover: dark ? Qt.lighter(accent, 1.22) : Qt.darker(accent, 1.12)
    readonly property color accentPressed: dark ? Qt.darker(accent, 1.15) : Qt.darker(accent, 1.3)
    readonly property color accentSubtle: Qt.alpha(accent, dark ? 0.18 : 0.14)
    readonly property color textOnAccent: luminance(accent) > 0.55 ? "#14161B" : "#FFFFFF"
    // Text-on-surface variant of the accent that keeps enough contrast in both themes.
    readonly property color accentText: dark ? Qt.lighter(accent, 1.3) : Qt.darker(accent, 1.35)

    // ---- Status ------------------------------------------------------------------------------
    readonly property color success: "#22C55E"
    readonly property color warning: "#F59E0B"
    readonly property color error: "#EF4444"
    readonly property color info: "#3B82F6"

    // ---- Timeline / media colours --------------------------------------------------------------
    readonly property color videoTrack: dark ? "#2A3350" : "#C9D4F5"
    readonly property color audioTrack: dark ? "#1F3A33" : "#BFE6D8"
    readonly property color subtitleTrack: dark ? "#3A3220" : "#F3E4B8"
    readonly property color clipTitle: dark ? "#7A4FA0" : "#C4A3E6"
    readonly property color clipSubtitle: dark ? "#9A7A22" : "#E6C766"
    readonly property color monitor: "#000000"
    readonly property color clipVideo: dark ? "#3A4F8F" : "#8FA6E8"
    readonly property color clipAudio: dark ? "#2C6B58" : "#7BC9AE"
    readonly property color clipText: "#FFFFFF"

    // ---- Geometry ----------------------------------------------------------------------------
    readonly property int radiusSmall: 6
    readonly property int radius: 10
    readonly property int radiusLarge: 16
    readonly property int controlHeight: 34
    readonly property int controlHeightSmall: 28
    readonly property int spacing: 8
    readonly property int spacingLarge: 16
    readonly property int panelGap: 1

    // ---- Typography --------------------------------------------------------------------------
    // Thai glyphs have a smaller x-height than Latin ones at the same size, so Thai gets +1 px.
    readonly property int fontOffset: AppSettings.values.fontSizeOffset + (Localization.currentLanguage === "th-TH" ? 1 : 0)
    readonly property int fontXs: 11 + fontOffset
    readonly property int fontSm: 12 + fontOffset
    readonly property int fontMd: 13 + fontOffset
    readonly property int fontLg: 15 + fontOffset
    readonly property int fontXl: 18 + fontOffset
    readonly property int fontXxl: 24 + fontOffset
    readonly property int fontHero: 36 + fontOffset

    // ---- Motion ------------------------------------------------------------------------------
    readonly property int durationFast: animated ? 110 : 0
    readonly property int durationNormal: animated ? 190 : 0
    readonly property int durationSlow: animated ? 320 : 0

    readonly property var accentPresets: [
        { name: "violet", color: "#8B5CF6" },
        { name: "blue",   color: "#3B82F6" },
        { name: "teal",   color: "#14B8A6" },
        { name: "green",  color: "#22C55E" },
        { name: "amber",  color: "#F59E0B" },
        { name: "rose",   color: "#F43F5E" }
    ]

    function luminance(c) {
        // Relative luminance (WCAG) used to choose readable text on the accent colour.
        function lin(v) { return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) }
        return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b)
    }
}
