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
        "heroTitle": ["Everything you create, in one place", "ทุกโปรแกรมของคุณ ในที่เดียว"],
        "searchPlaceholder": ["Search programs…", "ค้นหาโปรแกรม…"],
        "cat_all": ["All", "ทั้งหมด"],
        "cat_multimedia": ["Multimedia", "โปรแกรมมัลติมีเดีย"],
        "cat_office": ["Office", "โปรแกรมออฟฟิศ"],
        "cat_other": ["Other", "โปรแกรมอื่นๆ"],
        "categoryLabel": ["Category", "หมวดหมู่"],
        "noResults": ["No program matches your search.", "ไม่พบโปรแกรมที่ตรงกับคำค้นหา"],
        "installedCount": ["%1 installed", "ติดตั้งแล้ว %1"],
        "sec_multimedia": ["Multimedia programs", "โปรแกรมมัลติมีเดีย"],
        "sec_office": ["Office programs", "โปรแกรมออฟฟิศ"],
        "sec_other": ["Other programs", "โปรแกรมอื่นๆ"],
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
        "openInstallers": ["Open install folder", "เปิดโฟลเดอร์ติดตั้ง"],
        "refresh": ["Refresh", "รีเฟรช"],
        "about": ["About", "เกี่ยวกับ"],
        "busy": ["Working…", "กำลังทำงาน…"],
        "close": ["Close", "ปิด"],
        "minimize": ["Minimize", "ย่อหน้าต่าง"],
        "maximize": ["Maximize", "ขยายหน้าต่าง"],
        "restore": ["Restore", "คืนขนาดหน้าต่าง"],
        "notices": ["Third-party notices", "ประกาศของบุคคลที่สาม"],
        "aboutText": ["DriveVault Master Pro is the hub of the DriveVault suite. It lists every DriveVault program on this computer and lets you open, install, update and uninstall them from one place. It is installed automatically together with your first DriveVault program.", "DriveVault Master Pro คือศูนย์รวมโปรแกรมในชุด DriveVault แสดงรายการโปรแกรม DriveVault ทั้งหมดในเครื่องนี้ และให้คุณเปิด ติดตั้ง อัปเดต และถอนการติดตั้งได้จากที่เดียว โดยจะถูกติดตั้งให้อัตโนมัติพร้อมโปรแกรม DriveVault ตัวแรกของคุณ"],
        "honest": ["Programs are installed from setup files that are already on this computer. Master Pro goes online only to look for updates: when you press \"Check for updates\" and, unless you switch it off in the \"...\" menu, once when it opens. It downloads something only when you press an update button, and checks the file (SHA-256) before it runs. There is no online store.", "โปรแกรมถูกติดตั้งจากไฟล์ติดตั้งที่มีอยู่ในเครื่องนี้ Master Pro จะเชื่อมต่ออินเทอร์เน็ตเฉพาะตอนตรวจหาอัปเดต คือเมื่อคุณกดปุ่ม \"ตรวจหาอัปเดต\" และตอนเปิดโปรแกรมหนึ่งครั้ง (ปิดได้ในเมนู \"...\") จะดาวน์โหลดก็ต่อเมื่อคุณกดปุ่มอัปเดตเท่านั้น และตรวจไฟล์ (SHA-256) ก่อนเปิดทุกครั้ง ไม่มีร้านค้าออนไลน์"],
        "suiteFolder": ["Programs folder", "โฟลเดอร์โปรแกรม"],
        "language": ["Language", "ภาษา"],
        "confirmUninstall": ["Uninstall %1?", "ถอนการติดตั้ง %1 หรือไม่?"],
        "confirmText": ["The program's own uninstaller opens. Your projects are never deleted.", "ตัวถอนของโปรแกรมจะเปิดขึ้น โปรเจกต์ของคุณจะไม่ถูกลบ"],
        "cancel": ["Cancel", "ยกเลิก"],
        "setupFiles": ["Setup files (*.exe)", "ไฟล์ติดตั้ง (*.exe)"],
        "checkUpdates": ["Check for updates", "ตรวจหาอัปเดต"],
        "autoCheck": ["Check for updates on start", "ตรวจหาอัปเดตเมื่อเปิดโปรแกรม"],
        "selfUpdate": ["Master Pro %1 is available", "มี Master Pro รุ่นใหม่ %1"],
        "selfUpdateBtn": ["Update Master Pro", "อัปเดต Master Pro"],
        "checking": ["Checking…", "กำลังตรวจ…"],
        "onlineUpdate": ["Download version %1 and update", "ดาวน์โหลดเวอร์ชัน %1 และอัปเดต"],
        "onlineInstall": ["Download version %1 and install", "ดาวน์โหลดเวอร์ชัน %1 และติดตั้ง"],
        "onlineHint": ["Newest online: version %1", "รุ่นล่าสุดออนไลน์: เวอร์ชัน %1"]
    })
    function s(key) {
        const e = table[key]
        return e ? (lang === "th" ? e[1] : e[0]) : key
    }
}
