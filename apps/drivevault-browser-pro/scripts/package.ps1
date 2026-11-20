<#
.SYNOPSIS  Builds Release (+ tests), deploys Qt WebEngine, FFmpeg, the filter list snapshot and notices into dist\stage, runs the release gates and compiles the installer.
.OUTPUT    dist\DriveVault-Browser-Pro-Setup-x64.exe  (+ .sha256)
.EXAMPLE   .\scripts\package.ps1            (add -Sign with DV_SIGN_PFX / DV_SIGN_THUMBPRINT set for a release; -SkipBuild packages the existing Release build)
#>
param([switch]$SkipBuild, [switch]$Sign)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\..\..\shared\scripts\env.ps1"
$sharedScripts = Join-Path $SuiteRoot 'shared\scripts'
$dist = Join-Path $DvRoot 'dist'
$stage = Join-Path $dist 'stage'
$buildBin = Join-Path $DvRoot 'build\release\bin'
$exe = Join-Path $buildBin 'DriveVaultBrowserPro.exe'
$version = (Select-String -LiteralPath (Join-Path $DvRoot 'CMakeLists.txt') -Pattern '^\s+VERSION\s+(\d+\.\d+\.\d+)').Matches[0].Groups[1].Value
# DriveVault Master Pro is not inside this installer: the installer checks GitHub for the newest release and downloads it (shared\installer\master-hub-code.iss)

if (-not $SkipBuild) {
    & "$PSScriptRoot\build.ps1" -Config Release -Test
    if ($LASTEXITCODE) { throw 'Build or tests failed - not packaging.' }
}
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $exe)) { throw "Release executable not found: $exe" }
if ((Get-Item -LiteralPath $exe).VersionInfo.ProductVersion -ne $version) { throw "Build Browser Pro $version before packaging (the exe says $((Get-Item -LiteralPath $exe).VersionInfo.ProductVersion))." }
Write-Host "Packaging DriveVault Browser Pro $version" -ForegroundColor Cyan

$toolsRoot = if ($env:DV_TOOLS_DIR) { $env:DV_TOOLS_DIR } else { Join-Path $SuiteRoot '.tools' }
$qtVersion = & (Join-Path $env:DV_QT_DIR 'bin\qmake.exe') -query QT_VERSION
if ($LASTEXITCODE -or $qtVersion -ne '6.8.3') { throw 'Qt version does not match the audited build (6.8.3).' }
$ffmpegDir = if ($env:DV_FFMPEG_DIR) { $env:DV_FFMPEG_DIR } else { Join-Path $toolsRoot 'ffmpeg' }
$pdfiumDir = if ($env:DV_PDFIUM_DIR) { $env:DV_PDFIUM_DIR } else { Join-Path $SuiteRoot '.tools\pdfium' }
if (-not (Select-String -LiteralPath (Join-Path $pdfiumDir 'VERSION') -Pattern '^BUILD=8138$') -or
    (Get-Content -LiteralPath (Join-Path $pdfiumDir 'args.gn') -Raw) -notmatch 'pdf_enable_v8 = false' -or
    (Get-Content -LiteralPath (Join-Path $pdfiumDir 'args.gn') -Raw) -notmatch 'pdf_enable_xfa = false') {
    throw 'PDFium build does not match the audited build and configuration.'
}
$listsDir = if ($env:DV_FILTERS_DIR) { $env:DV_FILTERS_DIR } else { Join-Path $toolsRoot 'browser-test-data\filters' }

New-Item -ItemType Directory -Path $dist -Force | Out-Null
$expectedDist = Join-Path (Resolve-Path -LiteralPath $DvRoot).Path 'dist'
$distItem = Get-Item -LiteralPath $dist
if ($distItem.FullName -ne $expectedDist -or ($distItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe dist directory.' }
if (Test-Path -LiteralPath $stage) {
    $old = Get-Item -LiteralPath $stage
    if ($old.FullName -ne (Join-Path $expectedDist 'stage') -or ($old.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe stage directory.' }
    Remove-Item -LiteralPath $old.FullName -Recurse -Force
}
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $exe -Destination $stage
$pdfiumBinary = Join-Path $buildBin 'pdfium.dll'
if ((Get-FileHash -LiteralPath $pdfiumBinary).Hash -ne (Get-FileHash -LiteralPath (Join-Path $pdfiumDir 'bin\pdfium.dll')).Hash) {
    throw 'The build uses a different PDFium DLL.'
}
Copy-Item -LiteralPath $pdfiumBinary -Destination $stage

# ---- Qt + Qt WebEngine (Chromium) ----
# The "tls" plug-in stays (downloads of the lists and videos use https); the program forces the Basic Quick Controls style.
& windeployqt --release --no-translations --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw --no-compiler-runtime `
    --qmldir (Join-Path $DvRoot 'qml') --dir $stage (Join-Path $stage 'DriveVaultBrowserPro.exe')
if ($LASTEXITCODE) { throw 'windeployqt failed.' }
foreach ($style in 'Material', 'Universal', 'Fusion', 'Imagine', 'Windows', 'FluentWinUI3') {
    $d = Join-Path $stage "qml\QtQuick\Controls\$style"
    if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force }
    Get-ChildItem -LiteralPath $stage -File -Filter "Qt6QuickControls2$style*.dll" | Remove-Item -Force
}
Remove-Item -LiteralPath (Join-Path $stage 'qmltooling') -Recurse -Force -ErrorAction SilentlyContinue
# Qt WebEngine pulls in Qt Positioning: its serial-port (NMEA) source needs Qt6SerialPort, which is not shipped; the Windows location source stays
Remove-Item -LiteralPath (Join-Path $stage 'position\qtposition_nmea.dll') -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $stage 'vc_redist.x64.exe') -Force -ErrorAction SilentlyContinue
foreach ($required in 'platforms\qwindows.dll', 'tls\qschannelbackend.dll', 'QtWebEngineProcess.exe', 'Qt6WebEngineCore.dll', 'resources\icudtl.dat') {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $required) -PathType Leaf)) { throw "Missing runtime file: $required" }
}

# ---- Visual C++ runtime, app-local (a clean PC may not have it) ----
$crt = Get-ChildItem -LiteralPath (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
if (-not $crt) { throw 'The app-local Visual C++ runtime is missing.' }
$crtNames = 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'concrt140.dll'
foreach ($dll in $crtNames) { Copy-Item -LiteralPath (Join-Path $crt.FullName $dll) -Destination $stage }

# ---- FFmpeg (LGPL shared build): joins the pieces of an HLS / DASH stream; a separate program in its own folder ----
if (-not (Test-Path -LiteralPath (Join-Path $ffmpegDir 'bin\ffmpeg.exe'))) { throw "FFmpeg not found in $ffmpegDir" }
$ffStage = Join-Path $stage 'ffmpeg'
New-Item -ItemType Directory -Path $ffStage | Out-Null
Copy-Item -LiteralPath (Join-Path $ffmpegDir 'bin\ffmpeg.exe') -Destination $ffStage
foreach ($pattern in 'avcodec-*.dll', 'avdevice-*.dll', 'avfilter-*.dll', 'avformat-*.dll', 'avutil-*.dll', 'swresample-*.dll', 'swscale-*.dll') {
    $found = @(Get-ChildItem -LiteralPath (Join-Path $ffmpegDir 'bin') -File -Filter $pattern)
    if ($found.Count -ne 1) { throw "Expected one $pattern in the FFmpeg folder." }
    Copy-Item -LiteralPath $found[0].FullName -Destination $ffStage
}
foreach ($dll in $crtNames) { Copy-Item -LiteralPath (Join-Path $crt.FullName $dll) -Destination $ffStage }      # a separate program looks in its own folder

# ---- filter list snapshot (unchanged files + their meta files) ----
$filtersStage = Join-Path $stage 'filters'
New-Item -ItemType Directory -Path $filtersStage | Out-Null
foreach ($id in 'easylist', 'easyprivacy', 'fanboy-social', 'fanboy-annoyance') {
    foreach ($name in "$id.txt", "$id.meta.json") {
        $source = Join-Path $listsDir $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Filter list snapshot missing: $source (press 'Update filter lists' in a development build, or set DV_FILTERS_DIR)" }
        Copy-Item -LiteralPath $source -Destination $filtersStage
    }
}

# ---- licences, notices, docs, manifest ----
$lic = Join-Path $stage 'licenses'
$qtLic = Join-Path $lic 'Qt'
$docs = Join-Path $stage 'docs'
New-Item -ItemType Directory -Path $lic, $qtLic, $docs -Force | Out-Null
$pdfiumLic = Join-Path $lic 'PDFium'
New-Item -ItemType Directory -Path $pdfiumLic -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $pdfiumDir 'LICENSE') -Destination (Join-Path $pdfiumLic 'distribution-LICENSE.txt')
Copy-Item -Path (Join-Path $pdfiumDir 'licenses\*') -Destination $pdfiumLic -Recurse
Copy-Item -LiteralPath (Join-Path $DvRoot 'LICENSE.txt') -Destination $lic
Copy-Item -LiteralPath (Join-Path $DvRoot 'docs\THIRD-PARTY-NOTICES.txt') -Destination $lic
Copy-Item -Path (Join-Path $DvRoot 'resources\fonts\OFL-*.txt') -Destination $lic
Copy-Item -Path (Join-Path $DvRoot 'third_party\*.txt') -Destination $lic
Copy-Item -LiteralPath (Join-Path $SuiteRoot 'shared\legal\GPL-3.0.txt') -Destination $lic
Copy-Item -LiteralPath (Join-Path $ffmpegDir 'LICENSE.txt') -Destination (Join-Path $lic 'FFmpeg-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $toolsRoot 'InnoSetup\license.txt') -Destination (Join-Path $lic 'Inno-Setup-LICENSE.txt')
foreach ($module in 'qtbase', 'qtdeclarative', 'qtwebengine', 'qtwebchannel') {
    Copy-Item -LiteralPath (Join-Path $env:DV_QT_DIR "sbom\$module-6.8.3.spdx") -Destination $qtLic
}
foreach ($doc in 'user-guide.en.html', 'user-guide.th.html', 'release-notes.txt') {
    Copy-Item -LiteralPath (Join-Path $DvRoot "docs\$doc") -Destination $docs
}
[ordered]@{
    manifestVersion = 1; suite = 'DriveVault'; id = 'drivevault.browser'; name = 'DriveVault Browser Pro'
    version = $version; publisher = 'DriveVault'; executable = 'DriveVaultBrowserPro.exe'; accentColor = '#06B6D4'; dataFolder = 'BrowserPro'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'product.json') -Encoding UTF8

# ---- gates: clean-machine imports, licence audit ----
$global:LASTEXITCODE = 0
& (Join-Path $sharedScripts 'check-dependencies.ps1') -AppDir $stage
if ($LASTEXITCODE) { throw 'Dependency check failed - the program would not start on a clean PC.' }
& "$PSScriptRoot\license-audit.ps1" -StageDir $stage
if ($LASTEXITCODE) { throw 'License audit failed.' }

if ($Sign) {
    & (Join-Path $sharedScripts 'sign.ps1') -Files (Join-Path $stage 'DriveVaultBrowserPro.exe')
    if ($LASTEXITCODE) { throw 'Signing the executable failed.' }
}
Write-Host ("Staged {0:N1} MB in {1}" -f ((Get-ChildItem -LiteralPath $stage -Recurse -File | Measure-Object Length -Sum).Sum / 1MB), $stage)

$art = Join-Path $dist 'wizard'
& (Join-Path $sharedScripts 'make-brand-assets.ps1') -Accent '#06B6D4' -Title 'Browser Pro' -Logo (Join-Path $DvRoot 'resources\icons\app.png') -Out $art
$inno = @("/DAppVersion=$version", "/DStageDir=$stage", "/DOutputDir=$dist", "/DArtworkDir=$art", (Join-Path $DvRoot 'installer\windows\browser.iss'))
& iscc @inno
if ($LASTEXITCODE) { throw 'Inno Setup compilation failed.' }
$setup = Join-Path $dist 'DriveVault-Browser-Pro-Setup-x64.exe'
if ($Sign) {
    & (Join-Path $sharedScripts 'sign.ps1') -Files $setup
    if ($LASTEXITCODE) { throw 'Signing the installer failed.' }
}
$info = (Get-Item -LiteralPath $setup).VersionInfo
if (([string]$info.ProductName).Trim() -ne 'DriveVault Browser Pro' -or
    ([string]$info.CompanyName).Trim() -ne 'DriveVault' -or ([string]$info.ProductVersion).Trim() -ne $version) {
    throw 'Installer version resource does not match the release manifest.'
}
$hash = & (Join-Path $sharedScripts 'file-sha256.ps1') -Path $setup
"$hash *$(Split-Path -Leaf $setup)" | Set-Content -LiteralPath "$setup.sha256" -Encoding ASCII
Write-Host ("Installer: {0} ({1:N1} MiB, SHA-256 {2})" -f $setup, ((Get-Item -LiteralPath $setup).Length / 1MB), $hash) -ForegroundColor Green
if (-not $Sign) { Write-Warning 'The installer is unsigned.' }
