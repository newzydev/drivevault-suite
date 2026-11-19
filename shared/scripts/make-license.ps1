<#
.SYNOPSIS  Generates the licence texts of every app from the shared templates (shared\legal\EULA.en.txt / EULA.th.txt).
.DESCRIPTION
  Writes, for each app:  apps\<app>\LICENSE.txt (English + Thai, shipped in the installer's licenses folder)
                         apps\<app>\installer\windows\license-en.txt and license-th.txt (shown on the installer's licence page; UTF-8 with BOM for Thai)
  Product names and the product-specific section (6) are defined below. Edit the templates, then run this script and commit the results.
.EXAMPLE   .\shared\scripts\make-license.ps1
#>
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$utf8bom = New-Object System.Text.UTF8Encoding $true

$products = @(
    @{
        Folder = 'drivevault-pdf-pro'; Name = 'DriveVault PDF Pro'
        NotesEn = @'
PDF processing runs locally. Review redacted, sanitized and repaired copies before sharing or relying on them. Some PDFs require image-only redaction, which removes searchable text and interactive structures from the copy.
Document cleanup removes selected document structures; page content, pixels and hidden layers can still contain sensitive data. Some readers may ignore permission restrictions. Unlock documents only when you have permission and the correct password.
'@
        NotesTh = @'
การประมวลผล PDF ทำในเครื่อง โปรดตรวจสำเนาที่ปิดบัง ล้างข้อมูล หรือซ่อมแล้วก่อนส่งต่อหรือใช้งาน PDF บางไฟล์ต้องใช้โหมดสำเนาแบบภาพ ซึ่งทำให้สำเนาไม่มีข้อความค้นหาได้และโครงสร้างโต้ตอบ
การล้างข้อมูลนำโครงสร้างเอกสารที่ระบุออก เนื้อหาหน้า พิกเซล และเลเยอร์ที่ซ่อนยังอาจมีข้อมูลส่วนตัว โปรแกรมอ่านบางตัวอาจไม่ทำตามข้อจำกัดสิทธิ์ โปรดปลดล็อกเฉพาะไฟล์ที่คุณมีสิทธิ์และมีรหัสที่ถูกต้อง
'@
    },
    @{
        Folder = 'drivevault-video-editor-pro'; Name = 'DriveVault Video Editor Pro'
        NotesEn = @'
AI tools (optional, offline): automatic captions, silence detection, cut suggestions, noise reduction, object tracking and the assistant run on your computer. Their results can be
wrong or incomplete (for example recognised words, detected pauses or tracked positions): check them before you rely on them. Speech models are not part of the Software; you download
or import them yourself and they have their own licences (the Whisper models are MIT licensed). The assistant only understands a fixed set of requests and only calls the Software's own
functions.
Export: the Software checks every exported file automatically, but you are responsible for checking the final result before you publish or deliver it.
'@
        NotesTh = @'
เครื่องมือ AI (ไม่บังคับ ทำงานออฟไลน์): ซับอัตโนมัติ ตรวจจับความเงียบ แนะนำจุดตัด ลดเสียงรบกวน ติดตามวัตถุ และผู้ช่วย ทำงานบนเครื่องของคุณ ผลลัพธ์อาจผิดพลาดหรือไม่ครบถ้วน
(เช่น คำที่จดจำได้ ช่วงเงียบที่ตรวจพบ หรือตำแหน่งที่ติดตาม) โปรดตรวจสอบก่อนนำไปใช้ โมเดลเสียงไม่ได้รวมอยู่ในซอฟต์แวร์ คุณดาวน์โหลดหรือนำเข้าเอง และมีสัญญาอนุญาตของตัวเอง
(โมเดล Whisper ใช้สัญญาอนุญาต MIT) ผู้ช่วยเข้าใจเฉพาะชุดคำขอที่กำหนดไว้ และเรียกใช้เฉพาะฟังก์ชันของซอฟต์แวร์เองเท่านั้น
การส่งออก: ซอฟต์แวร์ตรวจไฟล์ที่ส่งออกให้อัตโนมัติ แต่คุณต้องรับผิดชอบตรวจผลลัพธ์สุดท้ายก่อนเผยแพร่หรือส่งมอบ
'@
    },
    @{
        Folder = 'drivevault-master-pro'; Name = 'DriveVault Master Pro'
        NotesEn = @'
Master Pro lists, opens, installs, updates and uninstalls DriveVault programs only when you ask it to. It uses setup files on your computer; it contacts GitHub (the releases of the DriveVault repository) when you press "Check for updates"
and, unless you switch "Check for updates on start" off, once when it opens; only when you then choose to download, it fetches that one setup file and compares its SHA-256 checksum with the one published in the release before it runs.
It has no online store and never downloads anything by itself. It starts only setup files whose version information names DriveVault as the publisher. It does not verify digital signatures of setup files (installers may be unsigned),
so install only setup files that you obtained from a source you trust. Uninstalling a program never deletes your projects or exported files.
Online AI models (optional): the AI pages can use a model on your own computer (nothing leaves your computer) or, only when you choose one in the model menu, a free online model run by NVIDIA (build.nvidia.com).
When you choose an online model, each message you send, the earlier messages of that conversation and - on the Agentic AI page - the text of the files, folders and command results that the AI reads for the job are sent over the internet to NVIDIA,
which runs the model. NVIDIA may process and keep them under its own terms and privacy policy, which apply to that service and not to this agreement. Do not use an online model with content that you are not allowed to share.
The service is reached with an access key that is built into the Software and shared by all users; it may be limited, changed or withdrawn at any time, and the Software may stop offering online models without notice.
When "Web" is switched on, the AI may also send search words to DuckDuckGo and request web pages from other websites. AI answers can be wrong or incomplete: check them before you rely on them.
The Agentic AI page can change files and run commands on your computer with your permissions when you allow it: review what it does before you allow it.
'@
        NotesTh = @'
Master Pro แสดง เปิด ติดตั้ง อัปเดต และถอนการติดตั้งโปรแกรม DriveVault เฉพาะเมื่อคุณสั่ง โดยใช้ไฟล์ติดตั้งในเครื่องของคุณ จะติดต่อ GitHub (รุ่นที่เผยแพร่ของที่เก็บ DriveVault) เมื่อคุณกด "ตรวจหาอัปเดต" และเมื่อเปิดโปรแกรมหนึ่งครั้ง
หากไม่ปิด "ตรวจหาอัปเดตเมื่อเปิดโปรแกรม" และเมื่อคุณเลือกดาวน์โหลด จะดึงไฟล์ติดตั้งนั้นไฟล์เดียวและเทียบค่า SHA-256 กับค่าที่ประกาศไว้ในรุ่นนั้นก่อนเปิด ไม่มีร้านค้าออนไลน์และไม่ดาวน์โหลดอะไรเองโดยอัตโนมัติ
โดยจะเปิดเฉพาะไฟล์ติดตั้งที่ข้อมูลเวอร์ชันระบุว่าผู้เผยแพร่คือ DriveVault ไม่ตรวจลายเซ็นดิจิทัลของไฟล์ติดตั้ง (ตัวติดตั้งอาจยังไม่ได้เซ็น)
ดังนั้นโปรดติดตั้งเฉพาะไฟล์ที่ได้จากแหล่งที่คุณเชื่อถือ การถอนการติดตั้งโปรแกรมจะไม่ลบโปรเจกต์หรือไฟล์ที่ส่งออกของคุณ
โมเดล AI ออนไลน์ (ไม่บังคับ): หน้า AI ใช้โมเดลบนเครื่องของคุณเองได้ (ไม่มีอะไรออกจากเครื่อง) หรือ เฉพาะเมื่อคุณเลือกในเมนูโมเดล ใช้โมเดลออนไลน์ฟรีที่ NVIDIA เป็นผู้รัน (build.nvidia.com)
เมื่อคุณเลือกโมเดลออนไลน์ ข้อความที่คุณส่งแต่ละข้อความ ข้อความก่อนหน้าของบทสนทนานั้น และ (ในหน้าเอเจนติกเอไอ) ข้อความในไฟล์ โฟลเดอร์ และผลคำสั่งที่ AI อ่านเพื่อทำงานนั้น จะถูกส่งทางอินเทอร์เน็ตไปที่ NVIDIA ซึ่งเป็นผู้รันโมเดล
NVIDIA อาจประมวลผลและเก็บข้อมูลนั้นตามข้อกำหนดและนโยบายความเป็นส่วนตัวของ NVIDIA เอง ซึ่งใช้กับบริการนั้นและไม่ใช่ข้อตกลงนี้ โปรดอย่าใช้โมเดลออนไลน์กับเนื้อหาที่คุณไม่มีสิทธิ์เผยแพร่
บริการนี้ใช้กุญแจเข้าถึง (access key) ที่ฝังอยู่ในซอฟต์แวร์และใช้ร่วมกันทุกคน อาจถูกจำกัด เปลี่ยน หรือยกเลิกเมื่อใดก็ได้ และซอฟต์แวร์อาจเลิกมีโมเดลออนไลน์โดยไม่แจ้งล่วงหน้า
เมื่อเปิด "เว็บ" AI อาจส่งคำค้นไปที่ DuckDuckGo และขอหน้าเว็บจากเว็บไซต์อื่นด้วย คำตอบของ AI อาจผิดพลาดหรือไม่ครบถ้วน โปรดตรวจสอบก่อนนำไปใช้
หน้าเอเจนติกเอไอแก้ไฟล์และรันคำสั่งบนเครื่องของคุณด้วยสิทธิ์ของคุณได้เมื่อคุณอนุญาต โปรดตรวจสิ่งที่มันจะทำก่อนอนุญาต
'@
    },
    @{
        Folder = 'drivevault-browser-pro'; Name = 'DriveVault Browser Pro'
        NotesEn = @'
Browser Pro opens the websites you ask for and talks to the Internet only for that, and when you press "Update filter lists" (it then downloads the four ad-blocking lists from easylist.to over https) or when you save a video (it then downloads that video from the website that shows it).
It keeps your history, bookmarks, cookies, cache and the permissions you allow on this computer only; a private window keeps nothing on the disk. It does not sign in to any account, does not sync and sends no usage data to DriveVault.
The ad blocker, the tracker blocker and the protection against ad-blocker detection work from public filter lists and fixed rules; they can break a website or miss an advertisement, and a website may still show a message about ad blockers. Turn the shield off for a site that does not work.
Saving a video: only save what you have the right to keep. The Software refuses streams that are copy protected (DRM) and does not offer saving from YouTube and from subscription or music streaming services, whose terms forbid it. The websites you visit keep their own terms and copyrights.
The browser engine is Chromium (through Qt WebEngine) built without the H.264 and AAC codecs, so some videos do not play. Web pages can be harmful: keep Windows up to date and do not enter passwords on websites you do not trust. This version has no extensions.
'@
        NotesTh = @'
Browser Pro เปิดเว็บไซต์ที่คุณสั่ง และติดต่ออินเทอร์เน็ตเฉพาะเรื่องนั้น เมื่อคุณกด "อัปเดตลิสต์ตัวกรอง" (จะดาวน์โหลดลิสต์บล็อกโฆษณา 4 รายการจาก easylist.to ผ่าน https) หรือเมื่อคุณบันทึกวิดีโอ (จะดาวน์โหลดวิดีโอนั้นจากเว็บไซต์ที่แสดงอยู่)
ประวัติ บุ๊กมาร์ก คุกกี้ แคช และสิทธิ์ที่คุณอนุญาตถูกเก็บในเครื่องของคุณเท่านั้น หน้าต่างส่วนตัวไม่เก็บอะไรลงดิสก์ โปรแกรมไม่ล็อกอินบัญชีใด ไม่ซิงก์ และไม่ส่งข้อมูลการใช้งานให้ DriveVault
ตัวบล็อกโฆษณา ตัวบล็อกตัวติดตาม และการป้องกันการตรวจจับตัวบล็อกโฆษณา ทำงานจากลิสต์ตัวกรองสาธารณะและกฎที่กำหนดไว้ อาจทำให้เว็บไซต์เสียหรือพลาดโฆษณาบางตัว และเว็บไซต์อาจยังแสดงข้อความเรื่องตัวบล็อกโฆษณาได้ ปิดโล่สำหรับเว็บไซต์ที่ใช้ไม่ได้
การบันทึกวิดีโอ: บันทึกเฉพาะสิ่งที่คุณมีสิทธิ์เก็บ ซอฟต์แวร์ปฏิเสธสตรีมที่มีการป้องกันการคัดลอก (DRM) และไม่เสนอการบันทึกจาก YouTube และบริการสตรีมแบบสมัครสมาชิกหรือเพลง ซึ่งข้อกำหนดของเขาห้ามไว้ เว็บไซต์ที่คุณเข้ามีข้อกำหนดและลิขสิทธิ์ของตัวเอง
เอนจินเบราว์เซอร์คือ Chromium (ผ่าน Qt WebEngine) ที่สร้างโดยไม่มีตัวถอดรหัส H.264 และ AAC วิดีโอบางรายการจึงเล่นไม่ได้ หน้าเว็บอาจเป็นอันตราย โปรดอัปเดต Windows อยู่เสมอ และอย่ากรอกรหัสผ่านในเว็บไซต์ที่ไม่น่าเชื่อถือ รุ่นนี้ยังไม่มีส่วนขยาย
'@
    }
)

$en = Get-Content (Join-Path $root 'shared\legal\EULA.en.txt') -Raw -Encoding UTF8
$th = Get-Content (Join-Path $root 'shared\legal\EULA.th.txt') -Raw -Encoding UTF8
foreach ($p in $products) {
    $app = Join-Path $root "apps\$($p.Folder)"
    if (-not (Test-Path $app)) { Write-Warning "missing $app"; continue }
    $textEn = $en.Replace('{{PRODUCT}}', $p.Name).Replace('{{NOTES}}', $p.NotesEn.Trim()).TrimEnd() + "`r`n"
    $textTh = $th.Replace('{{PRODUCT}}', $p.Name).Replace('{{NOTES}}', $p.NotesTh.Trim()).TrimEnd() + "`r`n"
    $textEn = ($textEn -replace "`r?`n", "`r`n"); $textTh = ($textTh -replace "`r?`n", "`r`n")
    $both = $textEn + "`r`n" + ('=' * 100) + "`r`n`r`n" + $textTh
    [IO.File]::WriteAllText((Join-Path $app 'LICENSE.txt'), $both, $utf8bom)
    $inst = Join-Path $app 'installer\windows'
    New-Item -ItemType Directory -Force $inst | Out-Null
    [IO.File]::WriteAllText((Join-Path $inst 'license-en.txt'), $textEn, $utf8bom)
    [IO.File]::WriteAllText((Join-Path $inst 'license-th.txt'), $textTh, $utf8bom)
    Write-Host "wrote licence texts for $($p.Name)"
}
