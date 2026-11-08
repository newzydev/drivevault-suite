<#
.SYNOPSIS  Third-party license audit of the staged application folder (what the installer will contain).
.DESCRIPTION
  * every file must belong to a known owner (Qt, FFmpeg, whisper.cpp, Microsoft runtime, DriveVault) - an unknown file fails the audit
  * the FFmpeg build must be LGPL (no --enable-gpl, no --enable-nonfree)
  * no Qt module that is GPL / commercial only, no debug runtimes
  * the license texts and notices that the licenses require must be in the licenses folder
  * writes dependency-manifest.json (owner, license, size, SHA-256 of every file) next to the notices
.EXAMPLE   .\scripts\license-audit.ps1 -Stage .\dist\stage
.OUTPUT    Exit code 0 = passed, 1 = failed (reasons listed).
#>
param(
    [Parameter(Mandatory)][string]$StageDir,
    [string]$Version = '',
    [string]$FfmpegExe = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$StageDir = (Resolve-Path $StageDir).Path
if (-not $FfmpegExe) { $FfmpegExe = Join-Path (Split-Path -Parent (Split-Path -Parent $root)) '.tools\ffmpeg\bin\ffmpeg.exe' }
$problems = New-Object System.Collections.Generic.List[string]

# ---- 1. owner of every file ------------------------------------------------------------------------------------------
function Get-Owner([string]$rel) {
    $name = [IO.Path]::GetFileName($rel).ToLower()
    $r = $rel.ToLower()
    if ($r -eq 'drivevaultvideoeditorpro.exe' -or $r -eq 'product.json') { return @('DriveVault', 'Proprietary') }
    if ($r -like 'licenses\*' -or $r -like 'docs\*') { return @('Documentation', 'n/a') }
    if ($r -like 'ai\whisper\*') {
        if ($name -match '^(msvcp|vcruntime|vcomp|concrt)') { return @('Microsoft Visual C++ runtime', 'Microsoft redistributable terms') }
        if ($name -eq 'whisper-cli.exe' -or $name -eq 'whisper.dll' -or $name -like 'ggml*.dll') { return @('whisper.cpp / ggml', 'MIT') }
        return $null
    }
    if ($r -like 'ai\models\*') {
        if ($name -eq 'ggml-base.bin') { return @('Whisper base speech model (OpenAI, ggml format by whisper.cpp)', 'MIT') }
        return $null
    }
    if ($name -match '^(msvcp|vcruntime|vcomp|concrt)\d*(_\d+)?\.dll$') { return @('Microsoft Visual C++ runtime', 'Microsoft redistributable terms') }
    if ($name -match '^(avcodec|avformat|avutil|swscale|swresample)-\d+\.dll$') { return @('FFmpeg', 'LGPL-3.0') }
    if ($name -like 'qt6*.dll' -or $r -like 'platforms\*' -or $r -like 'imageformats\*' -or $r -like 'iconengines\*' -or $r -like 'generic\*' -or
        $r -like 'networkinformation\*' -or $r -like 'tls\*' -or $r -like 'qml\*' -or $r -like 'styles\*') { return @('Qt', 'LGPL-3.0') }
    return $null
}

$files = Get-ChildItem $StageDir -Recurse -File
$entries = New-Object System.Collections.Generic.List[object]
foreach ($f in $files) {
    $rel = $f.FullName.Substring($StageDir.Length + 1)
    $owner = Get-Owner $rel
    if (-not $owner) { $problems.Add("Unknown file (no owner / license): $rel"); continue }
    $entries.Add([pscustomobject][ordered]@{
        path = $rel.Replace('\', '/'); size = $f.Length; owner = $owner[0]; license = $owner[1]
        sha256 = (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower()
    })
}

# ---- 2. FFmpeg must be LGPL -------------------------------------------------------------------------------------------
$ffmpegConfig = ''
if (Test-Path $FfmpegExe) {
    $out = & $FfmpegExe -version 2>&1 | Out-String
    $ffmpegConfig = ($out -split "`n" | Where-Object { $_ -like 'configuration:*' } | Select-Object -First 1)
    if ($ffmpegConfig -match '--enable-gpl') { $problems.Add('FFmpeg was built with --enable-gpl: the whole program would have to be GPL.') }
    if ($ffmpegConfig -match '--enable-nonfree') { $problems.Add('FFmpeg was built with --enable-nonfree: it must not be redistributed.') }
    if ($ffmpegConfig -notmatch '--enable-version3' -and $ffmpegConfig -notmatch '--enable-shared') { $problems.Add('FFmpeg build is not a shared (dynamically linked) build.') }
    if ($ffmpegConfig -match '--enable-lib(x264|x265|xvid|fdk-aac|vidstab)\b') { $problems.Add('FFmpeg build enables a GPL / non-free library.') }
} else {
    $problems.Add("FFmpeg executable not found for the configuration check: $FfmpegExe")
}

# ---- 3. no GPL-only Qt modules, no debug runtimes ---------------------------------------------------------------------
$gplOnlyQt = 'Charts', 'DataVisualization', 'Quick3D', 'VirtualKeyboard', 'QuickTimeline', 'Graphs', 'Lottie', 'Coap', 'Mqtt', 'Opcua', 'HttpServer', 'Scxml'
foreach ($f in $files) {
    foreach ($m in $gplOnlyQt) { if ($f.Name -like "Qt6$m*.dll") { $problems.Add("Qt module that is GPL / commercial only: $($f.Name)") } }
    if ($f.Name -match '^(msvcp|vcruntime|ucrtbase)\d*d\.dll$|^Qt6.*d\.dll$|\.pdb$') { $problems.Add("Debug build file in the package: $($f.Name)") }
}
if (-not ($files | Where-Object { $_.Name -like 'msvcp140.dll' -and $_.DirectoryName -eq $StageDir })) { $problems.Add('The Visual C++ runtime (msvcp140.dll) is not shipped next to the program.') }

# ---- 4. license texts and notices that must accompany the binaries ---------------------------------------------------------
$required = 'LICENSE.txt', 'LGPL-3.0.txt', 'FFmpeg-LICENSE.txt', 'whisper.cpp-LICENSE.txt', 'whisper-model-LICENSE.txt', 'OFL-Inter.txt', 'OFL-NotoSansThai.txt', 'THIRD-PARTY-NOTICES.txt'
foreach ($n in $required) {
    if (-not (Test-Path (Join-Path $StageDir "licenses\$n"))) { $problems.Add("Missing in licenses\: $n") }
}
foreach ($n in 'user-guide.en.html', 'user-guide.th.html', 'release-notes.txt') {
    if (-not (Test-Path (Join-Path $StageDir "docs\$n"))) { $problems.Add("Missing in docs\: $n") }
}

# ---- manifest ----------------------------------------------------------------------------------------------------------------
$components = $entries | Group-Object { "$($_.owner)|$($_.license)" } | ForEach-Object {
    $p = $_.Name -split '\|'
    [ordered]@{ component = $p[0]; license = $p[1]; files = $_.Count }
}
$manifest = [ordered]@{
    product = 'DriveVault Video Editor Pro'; version = $Version
    generated = (Get-Date).ToString('s'); ffmpegConfiguration = $ffmpegConfig
    components = @($components); files = $entries.ToArray()
    result = $(if ($problems.Count) { 'FAILED' } else { 'PASSED' })
}
$manifestPath = Join-Path $root 'dist\dependency-manifest.json'
New-Item -ItemType Directory -Force (Split-Path $manifestPath) | Out-Null
$manifest | ConvertTo-Json -Depth 6 | Set-Content $manifestPath -Encoding UTF8
$licDir = Join-Path $StageDir 'licenses'
if (Test-Path $licDir) { Copy-Item $manifestPath (Join-Path $licDir 'dependency-manifest.json') -Force }

Write-Host ("Audited {0} files; components: {1}" -f $entries.Count, (($components | ForEach-Object { "$($_.component) ($($_.files))" }) -join ', '))
if ($problems.Count -eq 0) {
    Write-Host 'LICENSE AUDIT PASSED' -ForegroundColor Green
    exit 0
}
foreach ($p in $problems) { Write-Host "AUDIT: $p" -ForegroundColor Red }
Write-Host 'LICENSE AUDIT FAILED' -ForegroundColor Red
exit 1
