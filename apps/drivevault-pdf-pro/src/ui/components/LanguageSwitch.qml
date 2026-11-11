import QtQuick
import DriveVault.PdfPro.Backend

PButton {
    objectName: "languageToggle"
    compact: true
    implicitWidth: 48
    text: Tr.s(Doc.language === "th" ? "languageThaiShort" : "languageEnglishShort")
    tip: Tr.s(Doc.language === "th" ? "switchToEnglish" : "switchToThai")
    onClicked: Doc.language = Doc.language === "th" ? "en" : "th"
}
