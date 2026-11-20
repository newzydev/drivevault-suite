param([Parameter(Mandatory)][string]$StageDir)

# Release gate of DriveVault Browser Pro: every staged file has an approved owner, the notices are there, FFmpeg is LGPL-only,
# no GPL / commercial-only Qt module, no debug build files, and the shipped filter lists are the unchanged files their meta files describe.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $StageDir).Path
$problems = New-Object System.Collections.Generic.List[string]

$required = @(
    'licenses\LICENSE.txt', 'licenses\THIRD-PARTY-NOTICES.txt', 'licenses\LGPL-3.0.txt', 'licenses\GPL-3.0.txt',
    'licenses\OFL-Inter.txt', 'licenses\OFL-NotoSansThai.txt', 'licenses\Chromium-LICENSE.txt', 'licenses\FFmpeg-LICENSE.txt',
    'licenses\EasyList-NOTICE.txt', 'licenses\Inno-Setup-LICENSE.txt',
    'licenses\Qt\qtbase-6.8.3.spdx', 'licenses\Qt\qtdeclarative-6.8.3.spdx', 'licenses\Qt\qtwebengine-6.8.3.spdx', 'licenses\Qt\qtwebchannel-6.8.3.spdx',
    'DriveVaultBrowserPro.exe', 'QtWebEngineProcess.exe', 'product.json', 'ffmpeg\ffmpeg.exe',
    'docs\user-guide.en.html', 'docs\user-guide.th.html', 'docs\release-notes.txt'
    'pdfium.dll', 'licenses\PDFium\distribution-LICENSE.txt'
)
foreach ($relative in $required) {
    $path = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) { $problems.Add("Missing or empty: $relative") }
}

# ---- every file has an owner -----------------------------------------------------------------------------------------------------------
$listIds = 'easylist', 'easyprivacy', 'fanboy-social', 'fanboy-annoyance'
$ffmpegDlls = '^(avcodec|avdevice|avfilter|avformat|avutil|swresample|swscale)-\d+\.dll$'
$crt = '^(msvcp140|msvcp140_1|msvcp140_2|msvcp140_atomic_wait|vcruntime140|vcruntime140_1|concrt140)\.dll$'
$qtPluginDirs = 'platforms', 'imageformats', 'iconengines', 'styles', 'tls', 'networkinformation', 'generic', 'position', 'printsupport', 'tools'
$counts = @{}
foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -Force -File) {
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { $problems.Add("Unexpected linked file: $($file.FullName)"); continue }
    $path = $file.FullName.Substring($root.Length + 1)
    $top = ($path -split '\\')[0]
    $owner = $null
    if ($path -like 'ffmpeg\*') {
        if ($path -eq 'ffmpeg\ffmpeg.exe' -or $file.Name -match $ffmpegDlls) { $owner = 'FFmpeg (LGPL-3.0)' }
        elseif ($file.Name -match $crt) { $owner = 'Microsoft Visual C++ runtime' }
    }
    elseif ($path -like 'filters\*') {
        $id = $file.Name -replace '\.(meta\.json|txt)$', ''
        if ($id -in $listIds -and ($file.Name -eq "$id.txt" -or $file.Name -eq "$id.meta.json")) { $owner = 'EasyList authors (CC BY-SA 3.0 / GPL-3.0+)' }
    }
    elseif ($path -eq 'pdfium.dll' -or $path -like 'licenses\PDFium\*') { $owner = 'PDFium and bundled dependencies' }
    elseif ($file.Name -match '^Qt6.*\.dll$') {
        if ($file.Name -like '*d.dll') { $problems.Add("Debug build file in the package: $path") }
        $owner = 'Qt'
    }
    elseif ($file.Name -eq 'QtWebEngineProcess.exe' -or $top -in 'resources', 'translations') { $owner = 'Qt WebEngine / Chromium' }
    elseif ($top -in $qtPluginDirs -and $file.Extension -eq '.dll' -and (Test-Path -LiteralPath (Join-Path $env:DV_QT_DIR "plugins\$path") -PathType Leaf)) { $owner = 'Qt' }
    elseif ($top -eq 'qml' -and (Test-Path -LiteralPath (Join-Path $env:DV_QT_DIR $path) -PathType Leaf)) { $owner = 'Qt' }
    elseif ($file.Name -match $crt -and $path -eq $file.Name) { $owner = 'Microsoft Visual C++ runtime' }
    elseif ($path -in $required -or $path -in 'DriveVaultBrowserPro.exe') { $owner = 'DriveVault / third-party notices' }
    if (-not $owner) { $problems.Add("No approved licence owner for $path"); continue }
    $counts[$owner] = 1 + [int]$counts[$owner]
}

# ---- no GPL-only / commercial-only Qt module --------------------------------------------------------------------------------------------
$gplOnly = 'Charts', 'DataVisualization', 'Quick3D', 'VirtualKeyboard', 'QuickTimeline', 'Graphs', 'Lottie', 'Coap', 'Mqtt', 'Opcua', 'HttpServer', 'Scxml'
foreach ($f in Get-ChildItem -LiteralPath $root -Recurse -File -Filter 'Qt6*.dll') {
    foreach ($m in $gplOnly) { if ($f.Name -eq "Qt6$m.dll" -or $f.Name -like "Qt6$m" + 'Quick*.dll') { $problems.Add("GPL / commercial-only Qt module in the package: $($f.Name)") } }
}

# ---- FFmpeg must be the LGPL shared build ----------------------------------------------------------------------------------------------
$ffmpegExe = Join-Path $root 'ffmpeg\ffmpeg.exe'
if (Test-Path -LiteralPath $ffmpegExe) {
    $out = & $ffmpegExe -version 2>&1 | Out-String
    $config = ($out -split "`n" | Where-Object { $_ -like 'configuration:*' } | Select-Object -First 1)
    if (-not $config) { $problems.Add('FFmpeg did not report its configuration.') }
    if ($config -match '--enable-gpl') { $problems.Add('FFmpeg was built with --enable-gpl: the whole program would have to be GPL.') }
    if ($config -match '--enable-nonfree') { $problems.Add('FFmpeg was built with --enable-nonfree: it must not be redistributed.') }
    if ($config -match '--enable-lib(x264|x265|xvid|fdk-aac|vidstab)\b') { $problems.Add('FFmpeg build enables a GPL / non-free library.') }
    if ($config -notmatch '--enable-shared') { $problems.Add('FFmpeg build is not a shared (dynamically linked) build.') }
}

# ---- the filter lists are the unchanged files the meta files describe ------------------------------------------------------------------
foreach ($id in $listIds) {
    $list = Join-Path $root "filters\$id.txt"
    $metaFile = Join-Path $root "filters\$id.meta.json"
    if (-not (Test-Path -LiteralPath $list) -or -not (Test-Path -LiteralPath $metaFile)) { $problems.Add("Filter list snapshot incomplete: $id"); continue }
    $meta = Get-Content -LiteralPath $metaFile -Raw | ConvertFrom-Json
    $digest = & (Join-Path $PSScriptRoot '..\..\..\shared\scripts\file-sha256.ps1') -Path $list
    if ($digest -cne $meta.sha256 -or (Get-Item -LiteralPath $list).Length -ne $meta.bytes) { $problems.Add("Filter list does not match its meta file: $id") }
    if (-not (Select-String -LiteralPath $list -Pattern '^! Licence: https://easylist.to/pages/licence.html' -Quiet)) { $problems.Add("Filter list has no EasyList licence line: $id") }
}

# ---- no key or secret in the package ---------------------------------------------------------------------------------------------------
foreach ($f in Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object { $_.Extension -in '.pfx', '.p12', '.key', '.pem' -or $_.Name -eq '.env' }) { $problems.Add("Secret-looking file in the package: $($f.FullName)") }

if ($problems.Count) {
    $problems | ForEach-Object { Write-Host "AUDIT: $_" -ForegroundColor Red }
    throw "License audit failed ($($problems.Count) problem(s))."
}
Write-Host "License audit passed: $(($counts.Values | Measure-Object -Sum).Sum) files assigned ($(($counts.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name): $($_.Value)" }) -join '; '))."
exit 0
