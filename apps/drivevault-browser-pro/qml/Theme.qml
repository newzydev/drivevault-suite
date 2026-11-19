pragma Singleton
import QtQuick

// Design tokens shared with the other DriveVault programs (neutral surfaces, Inter + Noto Sans Thai); every program adds its own accent colour.
QtObject {
    readonly property color accent: "#06B6D4"            // DriveVault Browser Pro
    readonly property color privateAccent: "#A78BFA"      // the private window
    readonly property color background: "#101114"
    readonly property color backgroundAlt: "#181A20"
    readonly property color surface: "#20232B"
    readonly property color surfaceHover: "#2B2F39"
    readonly property color border: "#343844"
    readonly property color borderSubtle: "#272A33"
    readonly property color text: "#F8FAFC"
    readonly property color textSecondary: "#A1A1AA"
    readonly property color textDisabled: "#6B6F7B"
    readonly property color success: "#22C55E"
    readonly property color warning: "#F59E0B"
    readonly property color error: "#EF4444"
    readonly property color errorSoft: "#FCA5A5"
    readonly property int radius: 12
    readonly property int radiusSmall: 8

    function readableOn(c) { return (0.299 * c.r + 0.587 * c.g + 0.114 * c.b) > 0.6 ? "#14161B" : "#FFFFFF" }
}
