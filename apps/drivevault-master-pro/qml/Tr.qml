pragma Singleton
import QtQuick
import DriveVault.Master.Backend

// Thai / English texts (the hub is small, so a table is enough). Usage: Tr.s("key")
QtObject {
    readonly property string lang: Hub.language
    readonly property var table: ({
        "title": ["DriveVault Master Pro", "DriveVault Master Pro"],
        "subtitle": ["All your DriveVault programs in one place", "โปรแกรม DriveVault ทั้งหมดของคุณในที่เดียว"],
        "programs": ["Programs", "โปรแกรม"],
        "installed": ["Installed", "ติดตั้งแล้ว"],
        "notInstalled": ["Not installed", "ยังไม่ได้ติดตั้ง"],
        "comingSoon": ["Coming soon", "เร็วๆ นี้"],
        "broken": ["Needs repair", "ต้องซ่อมแซม"],
        "update": ["Update available", "มีรุ่นใหม่"],
        "version": ["Version", "เวอร์ชัน"],
        "launch": ["Open", "เปิด"],
        "install": ["Install", "ติดตั้ง"],
        "updateBtn": ["Update", "อัปเดต"],
        "uninstall": ["Uninstall", "ถอนการติดตั้ง"],
        "folder": ["Folder", "โฟลเดอร์"],
        "noInstaller": ["No setup file found. Put it in the installers folder or choose it yourself.", "ไม่พบไฟล์ติดตั้ง วางไว้ในโฟลเดอร์ติดตั้งหรือเลือกไฟล์เอง"],
        "installerFound": ["Setup file found: version %1", "พบไฟล์ติดตั้ง: เวอร์ชัน %1"],
        "browse": ["Choose setup file…", "เลือกไฟล์ติดตั้ง…"],
        "installers": ["Installer folders", "โฟลเดอร์ติดตั้ง"],
        "addFolder": ["Add folder…", "เพิ่มโฟลเดอร์…"],
        "openInstallers": ["Open installers folder", "เปิดโฟลเดอร์ติดตั้ง"],
        "refresh": ["Refresh", "รีเฟรช"],
        "about": ["About", "เกี่ยวกับ"],
        "busy": ["Working…", "กำลังทำงาน…"],
        "close": ["Close", "ปิด"],
        "notices": ["Third-party notices", "ประกาศของบุคคลที่สาม"],
        "aboutText": ["DriveVault Master Pro installs, opens and updates the DriveVault programs. It is installed automatically with the first program.", "DriveVault Master Pro ติดตั้ง เปิด และอัปเดตโปรแกรม DriveVault และถูกติดตั้งอัตโนมัติพร้อมโปรแกรมแรก"],
        "honest": ["Only setup files on this computer are offered - there is no online store yet.", "แสดงเฉพาะไฟล์ติดตั้งที่อยู่ในเครื่องนี้ - ยังไม่มีร้านค้าออนไลน์"],
        "suiteFolder": ["Suite folder", "โฟลเดอร์ชุดโปรแกรม"],
        "language": ["Language", "ภาษา"],
        "confirmUninstall": ["Uninstall %1?", "ถอนการติดตั้ง %1 หรือไม่?"],
        "confirmText": ["The program's own uninstaller opens. Your projects are never deleted.", "ตัวถอนของโปรแกรมจะเปิดขึ้น โปรเจกต์ของคุณจะไม่ถูกลบ"],
        "cancel": ["Cancel", "ยกเลิก"],
        "setupFiles": ["Setup files (*.exe)", "ไฟล์ติดตั้ง (*.exe)"]
    })
    function s(key) {
        const e = table[key]
        return e ? (lang === "th" ? e[1] : e[0]) : key
    }
}
