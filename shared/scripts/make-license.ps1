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
'@
        NotesTh = @'
Master Pro แสดง เปิด ติดตั้ง อัปเดต และถอนการติดตั้งโปรแกรม DriveVault เฉพาะเมื่อคุณสั่ง โดยใช้ไฟล์ติดตั้งในเครื่องของคุณ จะติดต่อ GitHub (รุ่นที่เผยแพร่ของที่เก็บ DriveVault) เมื่อคุณกด "ตรวจหาอัปเดต" และเมื่อเปิดโปรแกรมหนึ่งครั้ง
หากไม่ปิด "ตรวจหาอัปเดตเมื่อเปิดโปรแกรม" และเมื่อคุณเลือกดาวน์โหลด จะดึงไฟล์ติดตั้งนั้นไฟล์เดียวและเทียบค่า SHA-256 กับค่าที่ประกาศไว้ในรุ่นนั้นก่อนเปิด ไม่มีร้านค้าออนไลน์และไม่ดาวน์โหลดอะไรเองโดยอัตโนมัติ
โดยจะเปิดเฉพาะไฟล์ติดตั้งที่ข้อมูลเวอร์ชันระบุว่าผู้เผยแพร่คือ DriveVault ไม่ตรวจลายเซ็นดิจิทัลของไฟล์ติดตั้ง (ตัวติดตั้งอาจยังไม่ได้เซ็น)
ดังนั้นโปรดติดตั้งเฉพาะไฟล์ที่ได้จากแหล่งที่คุณเชื่อถือ การถอนการติดตั้งโปรแกรมจะไม่ลบโปรเจกต์หรือไฟล์ที่ส่งออกของคุณ
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
