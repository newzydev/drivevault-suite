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
# Qt WebEngine (the browser tab) pulls in Qt Positioning: its serial-port (NMEA) source needs Qt6SerialPort, which is not shipped; the Windows location source stays
Remove-Item (Join-Path $stage 'position\qtposition_nmea.dll') -Force -ErrorAction SilentlyContinue
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
Copy-Item (Join-Path $DvRoot 'third_party\*.txt') $lic

# llama.cpp (MIT, Vulkan build b12170): the engine of the local AI models. llama-server.exe runs a model as a separate program in its own folder (ai\llama).
# The same audited files as DriveVault PDF Pro (keep the two lists identical). No model is shipped: the user downloads one in the AI settings.
$llamaDir = Join-Path $SuiteRoot '.tools\llm\llama-vulkan'
$llamaFiles = [ordered]@{
    'llama-server.exe' = 'fe352ae80869c9e0f57153c5fe78a0daf01cd9107280f564b164a09307867c29'
    'llama-server-impl.dll' = '0d4a4fc6321b2c558d3210704a3817bf0727a50166123b157cd30213833f7661'
    'llama.dll' = 'cd32952a8f363bf651e67b24b23c7f808a52da7cb5ea84efec1567a39527207b'
    'llama-common.dll' = '4329ccd7de848efc06af30be778c278ae07c9f7be65543b3b3e5d176591a05a8'
    'ggml.dll' = '2b56a3a514d5d545d34def24ec49711e86eb7de5bd32f0c412171081e0c12fd0'
    'ggml-base.dll' = 'a9cb6445722f52a222413e019dbcdb59f8cff82fc11ef209dfc497fd86adaabd'
    'ggml-vulkan.dll' = '1d6315a88aa3c51b15bb0440b3cf3ebc4f9662c262d3eeccc67e9f7d61b1dc39'
    'libomp.dll' = 'a12116ba72d1d6820407cf30be23da04ce79d6bb8a71a5ee71759c5a1faa6f1c'
    'mtmd.dll' = 'd08e8a5083665ed328a59ee0def6285bb5ffdd683abc5d9a7877991af6921d77'
    'ggml-cpu-alderlake.dll' = '87936bc87a65dab6792086e5bcb2559d56a8366f7f91f56abb2e372e38a4d70f'
    'ggml-cpu-cannonlake.dll' = 'dd3cffe293555f31724da51b64bac5c3b2f7d97e274c8f7e86e38d06f179daf6'
    'ggml-cpu-cascadelake.dll' = 'eb154c970c75637dff88fe7d133a88ce95fdd7b025679d7521e6f3f7b97fe1c8'
    'ggml-cpu-cooperlake.dll' = 'a1bb905c401a4f80f80cbde1bc275a6f2a4fa7795278d70f99f83be15788c95f'
    'ggml-cpu-haswell.dll' = '8be60592cca4ecefa498fee715cac3943d1735c42148be431a82695a8d497ee2'
    'ggml-cpu-icelake.dll' = 'c7e965a28eab2e9ac939dc62cc331da2e2a053ad190f3928c72ae8a794797fae'
    'ggml-cpu-ivybridge.dll' = 'edccfb0a57a68ee516ced5fa5924baf473cb21cca80eb23808e64d2ec803a566'
    'ggml-cpu-piledriver.dll' = 'e80dc16e81a584797c6a917fd581f68c056afc0653775b944dd97bf434c4c06b'
    'ggml-cpu-sandybridge.dll' = '8c07d2a396be16b506566bd92f53f04368d895b6e8dfdfc23ead025fe3354177'
    'ggml-cpu-sapphirerapids.dll' = 'c87aa88e5ff1581d1c49530b41030d7fbd7fbf2ae7b01bb6949ba014a06296b3'
    'ggml-cpu-skylakex.dll' = '9d249c6dda908fa6ec98dedca4276302f6b166df3dded54a9a31a79d79e7fc16'
    'ggml-cpu-sse42.dll' = '3ec623e099c35712e89fd7c9aafe556334be30b7d98985a19ee566abe70606c8'
    'ggml-cpu-x64.dll' = '2cca197feef476036115f33d2ff1dc40de4d711f478ce3defb5ad6fea6e0ac68'
    'ggml-cpu-zen4.dll' = '40e6f283963b928d61809ea4ef7d166fc84183d50ce7252ee61f7f288605267b'
}
$llamaStage = Join-Path $stage 'ai\llama'
New-Item -ItemType Directory -Force $llamaStage | Out-Null
foreach ($name in $llamaFiles.Keys) {
    $source = Join-Path $llamaDir $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Put the llama.cpp b12170 Vulkan build in .tools\llm\llama-vulkan first: $name" }
    $digest = & (Join-Path $mainScripts 'file-sha256.ps1') -Path $source
    if ($digest -cne $llamaFiles[$name]) { throw "llama.cpp file does not match the audited build b12170: $name" }
    Copy-Item -LiteralPath $source -Destination $llamaStage
}
foreach ($dll in 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'concrt140.dll') {
    Copy-Item -LiteralPath (Join-Path $crtDir.FullName $dll) -Destination $llamaStage      # a separate program only looks in its own folder
}

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
foreach ($n in 'LICENSE.txt', 'LGPL-3.0.txt', 'GPL-3.0.txt', 'THIRD-PARTY-NOTICES.txt', 'OFL-Inter.txt', 'OFL-NotoSansThai.txt',
               'llama.cpp-LICENSE.txt', 'LLVM-OpenMP-LICENSE.txt', 'Apache-2.0.txt', 'Qwen3.5-NOTICE.txt', 'Chromium-LICENSE.txt') {
    if (-not (Test-Path (Join-Path $lic $n))) { throw "Missing license file: $n" }
}
if (-not (Test-Path (Join-Path $stage 'ai\llama\llama-server.exe'))) { throw 'The AI engine (ai\llama\llama-server.exe) is not in the package.' }
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
