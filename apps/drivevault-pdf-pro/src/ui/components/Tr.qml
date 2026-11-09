pragma Singleton
import QtQuick
import DriveVault.PdfPro.Backend

// Thai / English texts (a table is enough for the size of this program). Usage: Tr.s("key"); each entry is [English, Thai].
QtObject {
    readonly property string lang: Doc.language
    readonly property var table: ({
        "title": ["DriveVault PDF Pro", "DriveVault PDF Pro"],
        "tagline": ["Your PDF tools, on this computer", "เครื่องมือ PDF ในเครื่องของคุณ"],
        "homeText": ["Open a PDF file to read it. Your files never leave this computer.", "เปิดไฟล์ PDF เพื่ออ่าน ไฟล์ของคุณไม่ถูกส่งออกจากเครื่องนี้"],
        "openFile": ["Open PDF…", "เปิดไฟล์ PDF…"],
        "openDialogTitle": ["Open a PDF file", "เปิดไฟล์ PDF"],
        "pdfFiles": ["PDF files (*.pdf)", "ไฟล์ PDF (*.pdf)"],
        "dropHint": ["or drop a PDF file here", "หรือลากไฟล์ PDF มาวางที่นี่"],
        "dropNow": ["Drop the PDF file to open it", "ปล่อยไฟล์ PDF เพื่อเปิด"],
        "recent": ["Recent files", "ไฟล์ล่าสุด"],
        "noRecent": ["No recent files yet.", "ยังไม่มีไฟล์ล่าสุด"],
        "removeRecent": ["Remove from the list", "เอาออกจากรายการ"],
        "closeFile": ["Close file", "ปิดไฟล์"],
        "thumbnails": ["Page thumbnails", "ภาพย่อของหน้า"],
        "prevPage": ["Previous page", "หน้าก่อนหน้า"],
        "nextPage": ["Next page", "หน้าถัดไป"],
        "pageOf": ["of %1", "จาก %1"],
        "zoomIn": ["Zoom in", "ขยาย"],
        "zoomOut": ["Zoom out", "ย่อ"],
        "fitWidth": ["Fit width", "พอดีความกว้าง"],
        "fitPage": ["Fit page", "พอดีหน้า"],
        "actualSize": ["Actual size", "ขนาดจริง"],
        "passwordTitle": ["This file is protected", "ไฟล์นี้มีรหัสผ่าน"],
        "passwordText": ["Enter the password to open %1.", "ใส่รหัสผ่านเพื่อเปิด %1"],
        "password": ["Password", "รหัสผ่าน"],
        "wrongPassword": ["The password is not correct. Try again.", "รหัสผ่านไม่ถูกต้อง ลองอีกครั้ง"],
        "cancel": ["Cancel", "ยกเลิก"],
        "open": ["Open", "เปิด"],
        "errFormat": ["This is not a valid PDF file, or it is damaged.", "นี่ไม่ใช่ไฟล์ PDF ที่ถูกต้อง หรือไฟล์เสียหาย"],
        "errFile": ["The file cannot be read.", "อ่านไฟล์ไม่ได้"],
        "errTooLarge": ["The file is larger than 4 GB, which is not supported yet.", "ไฟล์ใหญ่กว่า 4 GB ซึ่งยังไม่รองรับ"],
        "errUnknown": ["The file could not be opened.", "เปิดไฟล์ไม่ได้"],
        "dismiss": ["Dismiss", "ปิดข้อความ"],
        "preview": ["Version", "เวอร์ชัน"]
    })
    function s(key) {
        const e = table[key]
        if (!e)
            return key
        return lang === "th" ? e[1] : e[0]
    }
    function f(key, arg) { return s(key).replace("%1", arg) }
    function error(code) {
        return s(code === "format" ? "errFormat" : code === "file" ? "errFile" : code === "tooLarge" ? "errTooLarge" : "errUnknown")
    }
}
