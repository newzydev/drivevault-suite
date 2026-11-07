<#
.SYNOPSIS  Install -> launch -> uninstall -> reinstall -> launch -> uninstall, silently, per-user, into a temp folder.
.DESCRIPTION
  The launch check runs the installed executable with a minimal PATH (no Qt, no Visual Studio) to prove that
  the deployed runtime is self-contained, using the app's --screenshot option (exit code 0 + a real PNG).
  Settings kept by a silent uninstall are verified too. Registry keys and shortcuts created by the installer
  are checked and removed again by the uninstaller.
#>
param(
    [string]$Setup = "$PSScriptRoot\..\dist\DriveVault-Video-Editor-Pro-Setup-x64.exe",
    [string]$Work = "$env:TEMP\dv-installer-test"
)
$ErrorActionPreference = 'Continue'
$Setup = (Resolve-Path $Setup).Path
$suiteDir = Join-Path $Work 'Program Files\DriveVault'                       # shared parent of all DriveVault products
$installDir = Join-Path $suiteDir 'Common\DriveVault Video Editor Pro'
$productKey = 'HKCU:\Software\DriveVault\Products\VideoEditorPro'
$dataDir = Join-Path $Work 'data'
$masterKey = 'HKCU:\Software\DriveVault\MasterPro'
$masterUninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{B3A7C1D4-5E2F-4A86-9C10-7D2E8F4A6B35}_is1'
$masterExe = Join-Path $suiteDir 'DriveVaultMasterPro.exe'
$results = @()

# The test installs Master Pro itself. A real all-users Master Pro on this PC (HKLM) would make the program installer skip that step and
# many checks fail for a reason that has nothing to do with the installer, so stop with a clear message instead (use a PC / VM without DriveVault).
$realMaster = Get-ItemProperty 'HKLM:\Software\DriveVault\MasterPro' -ErrorAction SilentlyContinue
if ($realMaster -and -not $env:DV_TEST_ALLOW_REAL_MASTER) {
    Write-Host "SKIPPED: DriveVault Master Pro $($realMaster.Version) is installed for all users on this PC ($($realMaster.InstallDir))." -ForegroundColor Yellow
    Write-Host 'The installer test needs a PC (or VM) without DriveVault, or uninstall DriveVault first. Set DV_TEST_ALLOW_REAL_MASTER=1 to run it anyway (Master Pro checks will fail).' -ForegroundColor Yellow
    exit 3
}

function Check($name, $ok, $detail = '') {
    $script:results += [pscustomobject]@{ Check = $name; Result = $(if ($ok) { 'PASS' } else { 'FAIL' }); Detail = $detail }
}
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{6E1B7A52-4C3D-4F7E-9A3B-D17E5A0C2F11}_is1'

function Install([string]$lang) {
    $log = Join-Path $Work "install-$lang.log"
    $p = Start-Process $Setup -Wait -PassThru -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER',
        "/LANG=$lang", "/DIR=`"$installDir`"", "/LOG=`"$log`"", '/COMPONENTS="main,assoc"', '/TASKS=""')
    return $p.ExitCode
}
function Uninstall() {
    $unins = Get-ChildItem $installDir -Filter 'unins*.exe' | Select-Object -First 1
    if (-not $unins) { return -1 }
    $p = Start-Process $unins.FullName -Wait -PassThru -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
    Start-Sleep -Seconds 2   # the uninstaller removes its own folder from a temp copy
    return $p.ExitCode
}
function UninstallMaster([string]$dir) {
    $unins = Get-ChildItem $dir -Filter 'unins*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $unins) { return -1 }
    $p = Start-Process $unins.FullName -Wait -PassThru -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
    Start-Sleep -Seconds 2
    return $p.ExitCode
}
# Asks the installed Master Pro (minimal PATH, like a clean PC) which programs it sees.
function MasterProducts([string]$exe, [string]$label) {
    $json = Join-Path $Work "products-$label.json"
    Remove-Item $json -ErrorAction SilentlyContinue
    cmd /c "set PATH=$env:SystemRoot\System32;$env:SystemRoot&& set QT_PLUGIN_PATH=&& set QML2_IMPORT_PATH=&& `"$exe`" --data-dir `"$Work\master-data`" --lang en --list-products `"$json`"" | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $json)) { return $null }
    return (Get-Content $json -Raw | ConvertFrom-Json)
}
function Launch([string]$label) {
    $exe = Join-Path $installDir 'DriveVaultVideoEditorPro.exe'
    $png = Join-Path $Work "launch-$label.png"
    Remove-Item $png -ErrorAction SilentlyContinue
    # Minimal PATH: only Windows itself.
    $cmd = "set PATH=$env:SystemRoot\System32;$env:SystemRoot&& set QT_PLUGIN_PATH=&& set QML2_IMPORT_PATH=&& set DV_QT_DIR=&& set DV_WHISPER_DIR=&& `"$exe`" --data-dir `"$dataDir`" --lang th --screen home --screenshot `"$png`""
    cmd /c $cmd | Out-Null
    $rc = $LASTEXITCODE
    $ok = ($rc -eq 0) -and (Test-Path $png) -and ((Get-Item $png).Length -gt 20000)
    Check "launch ($label) with minimal PATH" $ok "exit=$rc png=$(if (Test-Path $png) { (Get-Item $png).Length } else { 'missing' })"
}

# Edits a tiny project (one linked video + audio clip) and renders it with the *installed* program, again with a
# minimal PATH, then inspects the result with the independent ffprobe of the FFmpeg build.
function ExportCheck([string]$label) {
    $ffDir = Join-Path $PSScriptRoot '..\..\..\.tools\ffmpeg\bin'
    $ffmpeg = Join-Path $ffDir 'ffmpeg.exe'
    $ffprobe = Join-Path $ffDir 'ffprobe.exe'
    if (-not (Test-Path $ffmpeg)) { Check "export ($label)" $false 'ffmpeg.exe of the local FFmpeg build not found'; return }
    $mediaDir = Join-Path $Work 'media'
    New-Item -ItemType Directory -Force $mediaDir | Out-Null
    $clip = Join-Path $mediaDir 'clip.mp4'
    if (-not (Test-Path $clip)) {
        & $ffmpeg -hide_banner -loglevel error -y -f lavfi -i 'color=c=red:s=640x360:r=30:d=1' -f lavfi -i 'color=c=blue:s=640x360:r=30:d=1' `
            -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=2' -filter_complex '[0:v][1:v]concat=n=2:v=1:a=0[v]' -map '[v]' -map 2:a `
            -c:v mpeg4 -q:v 3 -pix_fmt yuv420p -c:a aac $clip 2>$null
    }
    $m = $clip -replace '\\', '/'
    $json = @"
{"format":"drivevault-project","schemaVersion":2,"appVersion":"0.2.0","name":"Installer export test","createdAt":"2026-11-07T00:00:00.000Z","modifiedAt":"2026-11-07T00:00:00.000Z",
"settings":{"width":1280,"height":720,"frameRateNum":30,"frameRateDen":1,"sampleRate":48000},
"media":[{"id":"m1","path":"$m","relativePath":"media/clip.mp4","name":"clip.mp4","kind":"video","durationUs":2000000,"hasVideo":true,"hasAudio":true,"width":640,"height":360,"fpsNum":30,"fpsDen":1,"variableFrameRate":false,"audioRate":48000,"audioChannels":1,"videoCodec":"mpeg4","audioCodec":"aac","fileSize":1,"modifiedMs":0}],
"timeline":{"tracks":[{"id":"V2","name":"V2","kind":"video","muted":false,"locked":false,"visible":true},{"id":"V1","name":"V1","kind":"video","muted":false,"locked":false,"visible":true},{"id":"A1","name":"A1","kind":"audio","muted":false,"locked":false,"visible":true},{"id":"A2","name":"A2","kind":"audio","muted":false,"locked":false,"visible":true}],
"clips":[{"id":"c1","mediaId":"m1","trackId":"V1","start":0,"duration":60,"sourceIn":0,"linkId":"l1"},{"id":"c2","mediaId":"m1","trackId":"A1","start":0,"duration":60,"sourceIn":0,"linkId":"l1"}],"markers":[]}}
"@
    $proj = Join-Path $Work 'export-test.dvproj'
    [System.IO.File]::WriteAllText($proj, $json, (New-Object System.Text.UTF8Encoding $false))
    $out = Join-Path $Work "ส่งออก-$label.mp4"
    Remove-Item $out -ErrorAction SilentlyContinue
    $exe = Join-Path $installDir 'DriveVaultVideoEditorPro.exe'
    $cmd = "set PATH=$env:SystemRoot\System32;$env:SystemRoot&& `"$exe`" --data-dir `"$dataDir`" `"$proj`" --export `"$out`" --preset 720p"
    cmd /c $cmd 2>&1 | Out-Null
    $rc = $LASTEXITCODE
    $info = if (Test-Path $out) { (& $ffprobe -v error -show_entries stream=codec_name,width,height,duration -of csv=p=0 $out 2>$null) -join ';' } else { 'no file' }
    $ok = ($rc -eq 0) -and ($info -match 'h264,1280,720') -and ($info -match 'aac')
    Check "export ($label) renders a valid H.264 + AAC file" $ok "exit=$rc probe=$info"
}

if (Test-Path $Work) { Remove-Item $Work -Recurse -Force }
New-Item -ItemType Directory -Force $Work, $dataDir | Out-Null
# another (future) DriveVault product already lives in the shared suite folder: our installer / uninstaller must leave it alone
$otherProduct = Join-Path $suiteDir 'Common\DriveVaultOtherProduct'
New-Item -ItemType Directory -Force $otherProduct | Out-Null
Set-Content -Path (Join-Path $otherProduct 'keep-me.txt') -Value 'belongs to another DriveVault product'
Set-Content -Path (Join-Path $dataDir 'user-settings-marker.txt') -Value 'must survive a silent uninstall'
$projectFile = Join-Path $Work 'MyProject.dvproj'
Set-Content -Path $projectFile -Value 'user project - must never be deleted'

# ---- 1. install (English) ------------------------------------------------------------------------
$rc = Install 'english'
Check 'install (english) exit code 0' ($rc -eq 0) "exit=$rc"
Check 'main executable installed' (Test-Path (Join-Path $installDir 'DriveVaultVideoEditorPro.exe'))
Check 'Qt runtime installed (Qt6Core.dll, platforms\qwindows.dll)' ((Test-Path (Join-Path $installDir 'Qt6Core.dll')) -and (Test-Path (Join-Path $installDir 'platforms\qwindows.dll')))
Check 'licenses folder installed' (Test-Path (Join-Path $installDir 'licenses\LICENSE.txt'))
Check 'FFmpeg runtime installed (avcodec, avformat, swscale)' ((Test-Path (Join-Path $installDir 'avcodec-62.dll')) -and (Test-Path (Join-Path $installDir 'avformat-62.dll')) -and (Test-Path (Join-Path $installDir 'swscale-9.dll')))
Check 'FFmpeg license shipped' (Test-Path (Join-Path $installDir 'licenses\FFmpeg-LICENSE.txt'))
Check 'whisper.cpp engine installed (ai\whisper)' ((Test-Path (Join-Path $installDir 'ai\whisper\whisper-cli.exe')) -and (Test-Path (Join-Path $installDir 'ai\whisper\ggml-base.dll')))
Check 'whisper.cpp license shipped' (Test-Path (Join-Path $installDir 'licenses\whisper.cpp-LICENSE.txt'))
Check 'uninstaller installed' ($null -ne (Get-ChildItem $installDir -Filter 'unins*.exe' -ErrorAction SilentlyContinue))
Check 'uninstall registry entry present' (Test-Path $uninstallKey)
$reg = Get-ItemProperty $uninstallKey -ErrorAction SilentlyContinue
Check 'registry shows publisher + version' ($reg.Publisher -eq 'DriveVault' -and $reg.DisplayVersion -match '^\d+\.\d+\.\d+') "publisher=$($reg.Publisher) version=$($reg.DisplayVersion)"
Check '.dvproj association registered' ((Get-ItemProperty 'HKCU:\Software\Classes\.dvproj' -ErrorAction SilentlyContinue).'(default)' -eq 'DriveVault.Project')
$startMenu = Join-Path ([Environment]::GetFolderPath('Programs')) 'DriveVault\DriveVault Video Editor Pro.lnk'
Check 'Start Menu shortcut created (folder DriveVault)' (Test-Path $startMenu)

# ---- Phase 6: packaging contents --------------------------------------------------------------------------------
$verInstalled = (Get-Item (Join-Path $installDir 'DriveVaultVideoEditorPro.exe')).VersionInfo
Check 'executable carries version 1.4.0 and publisher' ($verInstalled.ProductVersion -eq '1.4.0' -and $verInstalled.CompanyName -eq 'DriveVault') "version=$($verInstalled.ProductVersion) company=$($verInstalled.CompanyName)"
$manifest = Get-Content (Join-Path $installDir 'product.json') -Raw -ErrorAction SilentlyContinue | ConvertFrom-Json -ErrorAction SilentlyContinue
Check 'product.json (hub manifest) is valid' ($manifest.id -eq 'drivevault.videoeditor' -and $manifest.accentColor -eq '#8B5CF6' -and $manifest.executable -eq 'DriveVaultVideoEditorPro.exe' -and $manifest.version -eq '1.4.0')
$prod = Get-ItemProperty $productKey -ErrorAction SilentlyContinue
Check 'hub registry entry (Software\DriveVault\Products\VideoEditorPro)' ($prod.InstallDir -eq $installDir -and $prod.Version -eq '1.4.0' -and $prod.AccentColor -eq '#8B5CF6' -and (Test-Path $prod.Executable)) "InstallDir=$($prod.InstallDir)"
Check 'user guide Thai + English, release notes installed' ((Test-Path (Join-Path $installDir 'docs\user-guide.th.html')) -and (Test-Path (Join-Path $installDir 'docs\user-guide.en.html')) -and (Test-Path (Join-Path $installDir 'docs\release-notes.txt')))
Check 'LGPL text, third-party notices and dependency manifest installed' ((Test-Path (Join-Path $installDir 'licenses\LGPL-3.0.txt')) -and (Test-Path (Join-Path $installDir 'licenses\THIRD-PARTY-NOTICES.txt')) -and (Test-Path (Join-Path $installDir 'licenses\dependency-manifest.json')))
Check 'Visual C++ runtime shipped app-local (program + whisper)' ((Test-Path (Join-Path $installDir 'msvcp140.dll')) -and (Test-Path (Join-Path $installDir 'vcruntime140_1.dll')) -and (Test-Path (Join-Path $installDir 'ai\whisper\vcruntime140.dll')))
Check 'no redistributable installer, no unused FFmpeg libraries' (-not (Test-Path (Join-Path $installDir 'vc_redist.x64.exe')) -and -not (Test-Path (Join-Path $installDir 'avfilter-11.dll')) -and -not (Test-Path (Join-Path $installDir 'avdevice-62.dll')))
& "$PSScriptRoot\..\..\..\shared\scripts\check-dependencies.ps1" -AppDir $installDir *> $null
Check 'clean-machine check: every import of the installed files resolves' ($LASTEXITCODE -eq 0)
$w = Join-Path $installDir 'ai\whisper\whisper-cli.exe'
cmd /c "set PATH=$env:SystemRoot\System32;$env:SystemRoot&& `"$w`" --help >nul 2>&1"
Check 'whisper-cli starts with a minimal PATH' ($LASTEXITCODE -eq 0) "exit=$LASTEXITCODE"

# ---- DriveVault Master Pro: installed by this installer, in the suite folder ------------------------------------------------------
Check 'Master Pro installed automatically in the suite folder' (Test-Path $masterExe) $masterExe
Check 'Master Pro has its own uninstaller + Apps entry' ((Test-Path $masterUninstallKey) -and (Test-Path (Join-Path $suiteDir 'unins000.exe')))
$mreg = Get-ItemProperty $masterKey -ErrorAction SilentlyContinue
Check 'Master Pro registry entry (hub)' ($mreg.InstallDir -eq $suiteDir -and $mreg.Version -match '^\d+\.\d+\.\d+' -and $mreg.AccentColor -eq '#F59E0B') "InstallDir=$($mreg.InstallDir) Version=$($mreg.Version)"
Check 'Master Pro Start Menu shortcut (folder DriveVault)' (Test-Path (Join-Path ([Environment]::GetFolderPath('Programs')) 'DriveVault\DriveVault Master Pro.lnk'))
Check 'Master Pro has an installers folder and its licenses' ((Test-Path (Join-Path $suiteDir 'installers')) -and (Test-Path (Join-Path $suiteDir 'licenses\THIRD-PARTY-NOTICES.txt')) -and (Test-Path (Join-Path $suiteDir 'licenses\LGPL-3.0.txt')))
Check 'the Video Editor folder sits inside the Master Pro folder' ($installDir -eq (Join-Path $suiteDir 'Common\DriveVault Video Editor Pro') -and $installDir.StartsWith($suiteDir + '\') -and (Test-Path (Join-Path $installDir 'DriveVaultVideoEditorPro.exe')))
$seen = MasterProducts $masterExe 'installed'
$ve = $seen | Where-Object { $_.id -eq 'drivevault.videoeditor' }
Check 'Master Pro (clean PATH) lists the Video Editor as installed' ($null -ne $ve -and $ve.installed -eq $true -and $ve.version -eq '1.4.0' -and $ve.status -eq 'installed' -and $ve.installDir -eq $installDir -and $ve.canUninstall -eq $true) "status=$($ve.status) version=$($ve.version)"
Check 'Master Pro lists the planned programs as coming soon' ((@($seen | Where-Object { $_.status -eq 'comingSoon' }).Count) -eq 2)
$mpng = Join-Path $Work 'master-window.png'
cmd /c "set PATH=$env:SystemRoot\System32;$env:SystemRoot&& set QT_QUICK_BACKEND=software&& `"$masterExe`" --data-dir `"$Work\master-data`" --lang th --screenshot `"$mpng`" --size 1120x740" | Out-Null
Check 'Master Pro window renders (clean PATH)' (($LASTEXITCODE -eq 0) -and (Test-Path $mpng) -and ((Get-Item $mpng).Length -gt 20000)) "exit=$LASTEXITCODE"
# while a program is installed, Master Pro refuses to be uninstalled (even silently)
$rcRefuse = UninstallMaster $suiteDir
Check 'Master Pro cannot be uninstalled while a DriveVault program is installed' (($rcRefuse -ne 0) -and (Test-Path $masterExe) -and (Test-Path $masterUninstallKey)) "exit=$rcRefuse"
Launch 'first-install'
ExportCheck 'first-install'

# ---- 2. uninstall ----------------------------------------------------------------------------------
$rc = Uninstall
Check 'uninstall exit code 0' ($rc -eq 0) "exit=$rc"
Check 'program files removed' (-not (Test-Path (Join-Path $installDir 'DriveVaultVideoEditorPro.exe')))
Check 'uninstall registry entry removed' (-not (Test-Path $uninstallKey))
Check '.dvproj association removed' (-not (Test-Path 'HKCU:\Software\Classes\.dvproj'))
Check 'Start Menu shortcut removed' (-not (Test-Path $startMenu))
Check 'hub registry entry removed' (-not (Test-Path $productKey))
Check 'another DriveVault product and the shared folder are untouched' ((Test-Path (Join-Path $otherProduct 'keep-me.txt')) -and (Test-Path $suiteDir))
Check 'program folder itself removed' (-not (Test-Path $installDir))
Check 'Master Pro stays after the program is uninstalled' ((Test-Path $masterExe) -and (Test-Path $masterUninstallKey) -and (Test-Path $masterKey))
$seen2 = MasterProducts $masterExe 'after-uninstall'
$ve2 = $seen2 | Where-Object { $_.id -eq 'drivevault.videoeditor' }
Check 'Master Pro now shows the Video Editor as not installed' ($null -ne $ve2 -and $ve2.installed -eq $false -and $ve2.status -eq 'notInstalled')
Check 'user settings kept by silent uninstall' (Test-Path (Join-Path $dataDir 'user-settings-marker.txt'))
Check 'user project untouched' ((Get-Content $projectFile) -eq 'user project - must never be deleted')

# ---- 3. reinstall (Thai) -----------------------------------------------------------------------------
$rc = Install 'thai'
Check 'reinstall (thai) exit code 0' ($rc -eq 0) "exit=$rc"
Check 'reinstall: executable present' (Test-Path (Join-Path $installDir 'DriveVaultVideoEditorPro.exe'))
$thaiLog = Get-Content (Join-Path $Work 'install-thai.log') -Raw -ErrorAction SilentlyContinue
Check 'reinstall: Master Pro (same version) is not installed a second time' ($thaiLog -notmatch 'MasterProSetup')
Check 'reinstall: Master Pro is still registered' (Test-Path $masterKey)
Launch 'reinstall'
ExportCheck 'reinstall'
$rc = Uninstall
Check 'final uninstall exit code 0' ($rc -eq 0) "exit=$rc"
Check 'final: program files removed' (-not (Test-Path (Join-Path $installDir 'DriveVaultVideoEditorPro.exe')))
Check 'final: registry entry removed' (-not (Test-Path $uninstallKey))
$rcMaster = UninstallMaster $suiteDir
Check 'Master Pro uninstalls once no program is left' ($rcMaster -eq 0) "exit=$rcMaster"
Check 'Master Pro files, Apps entry and registry removed' ((-not (Test-Path $masterExe)) -and (-not (Test-Path $masterUninstallKey)) -and (-not (Test-Path $masterKey)))
Check 'another DriveVault product still untouched after Master Pro is gone' (Test-Path (Join-Path $otherProduct 'keep-me.txt'))

# ---- 4. default location (no /DIR): per-user installs go to %LOCALAPPDATA%\Programs\DriveVault\Common\DriveVault Video Editor Pro ----------
# (An all-users install uses C:\Program Files\DriveVault\Common\DriveVault Video Editor Pro; that needs administrator rights and is not run here.)
$defaultSuite = Join-Path $env:LOCALAPPDATA 'Programs\DriveVault'
$defaultDir = Join-Path $defaultSuite 'Common\DriveVault Video Editor Pro'
$hadSuite = Test-Path $defaultSuite
$p = Start-Process $Setup -Wait -PassThru -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', '/LANG=english', "/LOG=`"$Work\install-default.log`"")
Check 'default-location install exit code 0' ($p.ExitCode -eq 0) "exit=$($p.ExitCode)"
Check 'default folder is ...\DriveVault\Common\DriveVault Video Editor Pro' (Test-Path (Join-Path $defaultDir 'DriveVaultVideoEditorPro.exe')) $defaultDir
Check 'default location: Master Pro was installed automatically' (Test-Path (Join-Path $defaultSuite 'DriveVaultMasterPro.exe')) $defaultSuite
$unins = Get-ChildItem $defaultDir -Filter 'unins*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($unins) { Start-Process $unins.FullName -Wait -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART'); Start-Sleep -Seconds 2 }
Check 'default-location uninstall removes the program folder' (-not (Test-Path $defaultDir))
Check 'default location: Master Pro was installed in the suite folder' (Test-Path (Join-Path $defaultSuite 'DriveVaultMasterPro.exe')) $defaultSuite
$rcM2 = UninstallMaster $defaultSuite
Check 'default location: Master Pro uninstalls' ($rcM2 -eq 0) "exit=$rcM2"
for ($i = 0; $i -lt 20 -and (Test-Path $defaultSuite); $i++) { Start-Sleep -Milliseconds 500 }     # the hub's uninstaller removes the empty folder a moment later
if (-not $hadSuite) { Check 'the shared DriveVault folder is removed when empty' (-not (Test-Path $defaultSuite)) }
Check 'hub registry key removed after the default-location uninstall' (-not (Test-Path $productKey) -and -not (Test-Path 'HKCU:\Software\DriveVault'))

$results | Format-Table -AutoSize -Wrap
$failed = @($results | Where-Object Result -eq 'FAIL').Count
Write-Host ("{0} checks, {1} failed" -f $results.Count, $failed) -ForegroundColor $(if ($failed) { 'Red' } else { 'Green' })
exit $failed
