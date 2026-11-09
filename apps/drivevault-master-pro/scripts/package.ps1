<#
.SYNOPSIS  Builds Release, deploys Qt and the Visual C++ runtime into dist\stage and compiles the installer.
.OUTPUT    dist\DriveVault-Master-Pro-Setup-x64.exe  (+ .sha256)
.EXAMPLE   .\scripts\package.ps1            (add -Sign with DV_SIGN_PFX / DV_SIGN_THUMBPRINT set for a release)
#>
param([switch]$SkipBuild, [switch]$Sign)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\..\..\shared\scripts\env.ps1"
$ErrorActionPreference = 'Continue'

$dist = Join-Path $DvRoot 'dist'
$stage = Join-Path $dist 'stage'
$exe = Join-Path $DvRoot 'build\release\bin\DriveVaultMasterPro.exe'
$mainScripts = Join-Path $SuiteRoot 'shared\scripts'

if (-not $SkipBuild) {
    & "$PSScriptRoot\build.ps1" -Config Release -Test
    if ($LASTEXITCODE) { throw "Build or tests failed - not packaging." }
}
if (-not (Test-Path $exe)) { throw "Release executable not found: $exe" }
$version = (Select-String -Path (Join-Path $DvRoot 'CMakeLists.txt') -Pattern 'VERSION\s+(\d+\.\d+\.\d+)').Matches[0].Groups[1].Value
Write-Host "Packaging DriveVault Master Pro $version" -ForegroundColor Cyan

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe $stage

windeployqt --release --no-translations --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw `
    --qmldir (Join-Path $DvRoot 'qml') --dir $stage (Join-Path $stage 'DriveVaultMasterPro.exe')
if ($LASTEXITCODE) { throw "windeployqt failed" }

foreach ($style in 'Material', 'Universal', 'Fusion', 'Imagine', 'Windows', 'FluentWinUI3') {
    $d = Join-Path $stage "qml\QtQuick\Controls\$style"
    if (Test-Path $d) { Remove-Item $d -Recurse -Force }
    Get-ChildItem $stage -Filter "Qt6QuickControls2$style*.dll" -ErrorAction SilentlyContinue | Remove-Item -Force
}
Remove-Item (Join-Path $stage 'qmltooling') -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $stage 'vc_redist.x64.exe') -Force -ErrorAction SilentlyContinue

# Visual C++ runtime app-local (a clean PC may not have it)
$crtDir = Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
if (-not $crtDir) { throw "Visual C++ redistributable DLLs not found" }
$crtNames = 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'
Copy-Item (Get-ChildItem $crtDir.FullName -Filter *.dll | Where-Object { $crtNames -contains $_.Name }).FullName $stage

# licenses and notices
$lic = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Force $lic | Out-Null
Copy-Item (Join-Path $DvRoot 'LICENSE.txt') $lic
Copy-Item (Join-Path $DvRoot 'docs\LGPL-3.0.txt') $lic
Copy-Item (Join-Path $SuiteRoot 'shared\legal\GPL-3.0.txt') $lic
Copy-Item (Join-Path $DvRoot 'docs\THIRD-PARTY-NOTICES.txt') $lic
Copy-Item (Join-Path $DvRoot 'resources\fonts\OFL-*.txt') $lic

# gates: clean-machine imports, GPL-only Qt modules, license files
$ErrorActionPreference = 'Stop'
$global:LASTEXITCODE = 0
& (Join-Path $mainScripts 'check-dependencies.ps1') -AppDir $stage
if ($LASTEXITCODE) { throw 'Dependency check failed - the program would not start on a clean PC.' }
$gplOnly = 'Charts', 'DataVisualization', 'Quick3D', 'VirtualKeyboard', 'QuickTimeline', 'Graphs', 'Lottie', 'Coap', 'Mqtt', 'Opcua', 'HttpServer', 'Scxml'
foreach ($f in Get-ChildItem $stage -Recurse -File) {
    foreach ($m in $gplOnly) { if ($f.Name -like "Qt6$m*.dll") { throw "GPL / commercial-only Qt module in the package: $($f.Name)" } }
    if ($f.Name -match 'd\.dll$' -and $f.Name -like 'Qt6*') { throw "Debug build file in the package: $($f.Name)" }
}
foreach ($n in 'LICENSE.txt', 'LGPL-3.0.txt', 'GPL-3.0.txt', 'THIRD-PARTY-NOTICES.txt', 'OFL-Inter.txt', 'OFL-NotoSansThai.txt') {
    if (-not (Test-Path (Join-Path $lic $n))) { throw "Missing license file: $n" }
}
if ($Sign) {
    & (Join-Path $mainScripts 'sign.ps1') -Files (Join-Path $stage 'DriveVaultMasterPro.exe')
    if ($LASTEXITCODE) { throw 'Signing the executable failed.' }
}
$ErrorActionPreference = 'Continue'

Write-Host ("Staged {0:N1} MB in {1}" -f ((Get-ChildItem $stage -Recurse | Measure-Object Length -Sum).Sum / 1MB), $stage)
iscc "/DAppVersion=$version" "/DStageDir=$stage" "/DOutputDir=$dist" (Join-Path $DvRoot 'installer\windows\master.iss')
if ($LASTEXITCODE) { throw "Inno Setup compilation failed" }

$setup = Join-Path $dist 'DriveVault-Master-Pro-Setup-x64.exe'
if ($Sign) {
    & (Join-Path $mainScripts 'sign.ps1') -Files $setup
    if ($LASTEXITCODE) { throw 'Signing the installer failed.' }
}
$ErrorActionPreference = 'Stop'
$hash = & (Join-Path $mainScripts 'file-sha256.ps1') -Path $setup
"$hash *$(Split-Path $setup -Leaf)" | Set-Content ($setup + '.sha256') -Encoding ASCII
Write-Host "Installer: $setup  (SHA-256 $hash)" -ForegroundColor Green
if (-not $Sign) { Write-Host 'NOTE: the installer is NOT code-signed.' -ForegroundColor Yellow }
