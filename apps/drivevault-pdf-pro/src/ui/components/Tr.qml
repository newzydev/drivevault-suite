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
        "preview": ["Version", "เวอร์ชัน"],
        // side panel
        "outline": ["Bookmarks", "สารบัญ"],
        "comments": ["Comments and markup", "ความเห็นและหมายเหตุ"],
        "noOutline": ["This file has no bookmarks.", "ไฟล์นี้ไม่มีสารบัญ"],
        "noComments": ["This file has no comments or markup.", "ไฟล์นี้ไม่มีความเห็นหรือหมายเหตุ"],
        "loading": ["Loading…", "กำลังโหลด…"],
        "pageN": ["page %1", "หน้า %1"],
        // search
        "find": ["Find", "ค้นหา"],
        "findPlaceholder": ["Find in this document", "ค้นหาในเอกสารนี้"],
        "matchCase": ["Match case", "ตรงตามตัวพิมพ์เล็ก/ใหญ่"],
        "wholeWord": ["Whole word", "ทั้งคำ"],
        "prevHit": ["Previous match", "ผลก่อนหน้า"],
        "nextHit": ["Next match", "ผลถัดไป"],
        "closeSearch": ["Close search", "ปิดการค้นหา"],
        "searching": ["Searching…", "กำลังค้นหา…"],
        "noMatches": ["No matches", "ไม่พบ"],
        "hitsOf": ["%1 of %2", "%1 จาก %2"],
        // view
        "viewContinuous": ["One page, continuous", "หน้าเดียวต่อเนื่อง"],
        "viewTwoPage": ["Two pages, continuous", "สองหน้าต่อเนื่อง"],
        "rotateRight": ["Rotate view clockwise", "หมุนมุมมองตามเข็มนาฬิกา"],
        "rotateLeft": ["Rotate view counter clockwise", "หมุนมุมมองทวนเข็มนาฬิกา"],
        "darkPages": ["Dark pages (the view only, the file is not changed)", "หน้าสีเข้ม (เฉพาะการแสดงผล ไม่แก้ไฟล์)"],
        "fullScreen": ["Full screen (F11)", "เต็มจอ (F11)"],
        "copy": ["Copy", "คัดลอก"],
        "selectAll": ["Select all", "เลือกทั้งหมด"],
        "print": ["Print", "พิมพ์"],
        // links
        "openLinkTitle": ["Open this link?", "เปิดลิงก์นี้ไหม?"],
        "openLinkText": ["The document wants to open this web address in your browser:", "เอกสารต้องการเปิดที่อยู่เว็บนี้ในเบราว์เซอร์ของคุณ:"],
        "openLink": ["Open link", "เปิดลิงก์"],
        "linkRefused": ["This kind of link is not opened.", "ไม่เปิดลิงก์ชนิดนี้"],
        // printing
        "printTitle": ["Print", "พิมพ์"],
        "printer": ["Printer", "เครื่องพิมพ์"],
        "noPrinters": ["No printer", "ไม่มีเครื่องพิมพ์"],
        "noPrintersText": ["No printer is installed on this computer.", "ไม่พบเครื่องพิมพ์ในเครื่องนี้"],
        "pagesToPrint": ["Pages", "หน้าที่จะพิมพ์"],
        "allPages": ["All pages", "ทุกหน้า"],
        "currentPageN": ["Current page (%1)", "หน้าปัจจุบัน (%1)"],
        "pageRange": ["Pages", "ช่วงหน้า"],
        "rangeHint": ["for example 1-3, 5, 8-", "เช่น 1-3, 5, 8-"],
        "copies": ["Copies", "จำนวนชุด"],
        "colour": ["Colour", "สี"],
        "fitPaper": ["Fit to paper", "พอดีกระดาษ"],
        "badRange": ["This page range is not valid for this file (it has %1 pages).", "ช่วงหน้าไม่ถูกต้อง (ไฟล์นี้มี %1 หน้า)"],
        "printBusy": ["Another print job is still running.", "ยังมีงานพิมพ์ที่ทำงานอยู่"],
        "printing": ["Printing…", "กำลังพิมพ์…"],
        "printDone": ["Printing finished.", "พิมพ์เสร็จแล้ว"],
        "printFailed": ["Printing failed.", "พิมพ์ไม่สำเร็จ"],
        "printCancelled": ["Printing was cancelled.", "ยกเลิกการพิมพ์แล้ว"]
    })
    function s(key) {
        const e = table[key]
        if (!e)
            return key
        return lang === "th" ? e[1] : e[0]
    }
    function f(key, arg) { return s(key).replace("%1", arg) }
    function f2(key, a, b) { return s(key).replace("%1", a).replace("%2", b) }
    function error(code) {
        return s(code === "format" ? "errFormat" : code === "file" ? "errFile" : code === "tooLarge" ? "errTooLarge" : "errUnknown")
    }
}
