import QtQuick
import QtQuick.Controls

// Phase 0 skeleton: the program does not connect to anything yet. The window says so (a status label must match reality).
ApplicationWindow {
    id: root
    visible: true
    width: 760
    height: 460
    minimumWidth: 560
    minimumHeight: 360
    title: "DriveVault Remote Desktop Pro"
    color: "#0F1115"

    Column {
        anchors.centerIn: parent
        spacing: 14
        width: Math.min(parent.width - 64, 560)

        Rectangle {
            width: 64; height: 64; radius: 16; color: "#06B6D4"
            anchors.horizontalCenter: parent.horizontalCenter
            Text { anchors.centerIn: parent; text: "R"; color: "white"; font.pixelSize: 34; font.bold: true }
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: "DriveVault Remote Desktop Pro"
            color: "#F3F4F6"; font.pixelSize: 24; font.bold: true
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: "Not implemented yet - ยังไม่พร้อมใช้งาน"
            color: "#06B6D4"; font.pixelSize: 16
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: "This is the phase 0 skeleton (measurements only). It does not capture the screen, open a port or connect to anything.\n" +
                  "นี่คือโครงเริ่มต้นเฟส 0 (ใช้วัดผลเท่านั้น) ยังไม่จับภาพหน้าจอ ไม่เปิดพอร์ต และไม่เชื่อมต่ออะไร"
            color: "#9CA3AF"; font.pixelSize: 13
        }
    }
}
