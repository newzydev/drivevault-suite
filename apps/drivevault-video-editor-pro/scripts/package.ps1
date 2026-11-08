<#
.SYNOPSIS  Builds Release, deploys the Qt runtime into dist\stage and compiles the Inno Setup installer.
.OUTPUT    dist\DriveVault-Video-Editor-Pro-Setup-x64.exe
.EXAMPLE   .\scripts\package.ps1
#>
param(
    [switch]$SkipBuild,
    [switch]$Sign,                        # Authenticode-sign with the certificate named in DV_SIGN_PFX / DV_SIGN_THUMBPRINT (see sign.ps1)
    [string]$MasterSetup = '',            # setup of DriveVault Master Pro that travels inside this installer (default: apps\drivevault-master-pro\dist)
    [switch]$NoMaster                     # build without the hub (only for experiments: a product installer normally installs Master Pro first)
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\..\..\shared\scripts\env.ps1"
$ErrorActionPreference = 'Continue'

$dist = Join-Path $DvRoot 'dist'
$stage = Join-Path $dist 'stage'
$exe = Join-Path $DvRoot 'build\release\bin\DriveVaultVideoEditorPro.exe'

if (-not $SkipBuild) {
    & "$PSScriptRoot\build.ps1" -Config Release -Test
    if ($LASTEXITCODE) { throw "Build or tests failed - not packaging." }
}
if (-not (Test-Path $exe)) { throw "Release executable not found: $exe" }

$version = (Select-String -Path (Join-Path $DvRoot 'CMakeLists.txt') -Pattern 'VERSION\s+(\d+\.\d+\.\d+)').Matches[0].Groups[1].Value
Write-Host "Packaging version $version" -ForegroundColor Cyan

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe $stage

# Qt runtime: only the modules / plugins / QML imports the UI actually uses (scanned from src\ui).
windeployqt --release --no-translations --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw `
    --compiler-runtime --qmldir (Join-Path $DvRoot 'src\ui') --dir $stage (Join-Path $stage 'DriveVaultVideoEditorPro.exe')
if ($LASTEXITCODE) { throw "windeployqt failed" }

# The Visual C++ runtime goes next to the program (app-local): a clean PC may not have it, and windeployqt only copies the
# redistributable installer, which is never run. That installer is dropped (18 MB).
Remove-Item (Join-Path $stage 'vc_redist.x64.exe') -Force -ErrorAction SilentlyContinue
$crtDir = Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
$ompDir = Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.OpenMP' | Select-Object -First 1
if (-not $crtDir) { throw "Visual C++ redistributable DLLs not found under $env:VCToolsRedistDir" }
# only the runtime DLLs that the program, Qt and whisper really import (check-dependencies.ps1 proves the list is complete)
$crtNames = 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'
$crtDlls = @(Get-ChildItem $crtDir.FullName -Filter *.dll | Where-Object { $crtNames -contains $_.Name })
if ($ompDir) { $crtDlls += Get-ChildItem $ompDir.FullName -Filter vcomp140.dll }
Copy-Item $crtDlls.FullName $stage

# The app forces the "Basic" Quick Controls style; drop the other styles to keep the install small.
foreach ($style in 'Material', 'Universal', 'Fusion', 'Imagine', 'Windows', 'FluentWinUI3') {
    $dir = Join-Path $stage "qml\QtQuick\Controls\$style"
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    Get-ChildItem $stage -Filter "Qt6QuickControls2$style*.dll" -ErrorAction SilentlyContinue | Remove-Item -Force
}

# Qt Quick debugger plug-ins are development tools and are not shipped.
$debugPlugins = Join-Path $stage "qmltooling"
if (Test-Path $debugPlugins) { Remove-Item $debugPlugins -Recurse -Force }

# License texts shipped with the application.
$lic = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Force $lic | Out-Null
Copy-Item (Join-Path $DvRoot 'LICENSE.txt') $lic
Copy-Item (Join-Path $DvRoot 'docs\third-party-licenses.md') $lic
Copy-Item (Join-Path $DvRoot 'resources\fonts\OFL-*.txt') $lic

# FFmpeg (LGPL, shared build): the DLLs sit next to the executable, its license text goes with the others.
$ffmpegDir = if ($env:DV_FFMPEG_DIR) { $env:DV_FFMPEG_DIR } else { Join-Path $SuiteRoot '.tools\ffmpeg' }
if (-not (Test-Path (Join-Path $ffmpegDir 'bin'))) { throw "FFmpeg not found in $ffmpegDir" }
# only the libraries the program links against (avdevice / avfilter are unused and would only add license surface)
foreach ($pattern in 'avcodec-*.dll', 'avformat-*.dll', 'avutil-*.dll', 'swscale-*.dll', 'swresample-*.dll') {
    Copy-Item (Join-Path $ffmpegDir "bin\$pattern") $stage
}
Copy-Item (Join-Path $ffmpegDir 'LICENSE.txt') (Join-Path $lic 'FFmpeg-LICENSE.txt')

# whisper.cpp (speech to text, MIT): the engine ships, speech models are downloaded / imported by the user
$whisperDir = Join-Path $SuiteRoot '.tools\whisper\bin\Release'
if (Test-Path (Join-Path $whisperDir 'whisper-cli.exe')) {
    $aiStage = Join-Path $stage 'ai\whisper'
    New-Item -ItemType Directory -Force $aiStage | Out-Null
    foreach ($pattern in 'whisper-cli.exe', 'whisper.dll', 'ggml.dll', 'ggml-base.dll', 'ggml-cpu-*.dll') {
        Copy-Item (Join-Path $whisperDir $pattern) $aiStage
    }
    Copy-Item (Join-Path $DvRoot 'third_party\whisper.cpp-LICENSE.txt') (Join-Path $lic 'whisper.cpp-LICENSE.txt')
    Copy-Item $crtDlls.FullName $aiStage                      # whisper-cli is a separate program: it needs its own copy
} else {
    Write-Warning "whisper.cpp not found in $whisperDir - the installer will not contain the captions engine"
}

# llama.cpp (language model server for "Edit by prompt", MIT): the engine ships, the model (Qwen3.5 4B, Apache 2.0) is downloaded by the user
$llamaDir = Join-Path $SuiteRoot '.tools\llm\llama'
if (Test-Path (Join-Path $llamaDir 'llama-server.exe')) {
    $llamaStage = Join-Path $stage 'ai\llama'
    New-Item -ItemType Directory -Force $llamaStage | Out-Null
    foreach ($pattern in 'llama-server.exe', 'llama-server-impl.dll', 'llama.dll', 'llama-common.dll', 'mtmd.dll', 'ggml.dll', 'ggml-base.dll', 'ggml-cpu-*.dll', 'libomp.dll') {
        Copy-Item (Join-Path $llamaDir $pattern) $llamaStage
    }
    Copy-Item (Join-Path $DvRoot 'third_party\llama.cpp-LICENSE.txt') (Join-Path $lic 'llama.cpp-LICENSE.txt')
    Copy-Item (Join-Path $DvRoot 'third_party\LLVM-OpenMP-LICENSE.txt') (Join-Path $lic 'LLVM-OpenMP-LICENSE.txt')
    Copy-Item $crtDlls.FullName $llamaStage                   # llama-server is a separate program: it needs its own copy
} else {
    Write-Warning "llama.cpp not found in $llamaDir - the installer will not contain the language model engine"
}

# Licenses, notices, documentation and the product manifest for the DriveVault hub.
Copy-Item (Join-Path $DvRoot 'third_party\LGPL-3.0.txt') $lic
Copy-Item (Join-Path $DvRoot 'docs\THIRD-PARTY-NOTICES.txt') $lic
Remove-Item (Join-Path $lic 'third-party-licenses.md') -Force -ErrorAction SilentlyContinue
$docs = Join-Path $stage 'docs'
New-Item -ItemType Directory -Force $docs | Out-Null
foreach ($d in 'user-guide.en.html', 'user-guide.th.html', 'release-notes.txt') { Copy-Item (Join-Path $DvRoot "docs\$d") $docs }
$product = [ordered]@{
    manifestVersion = 1; suite = 'DriveVault'; id = 'drivevault.videoeditor'; name = 'DriveVault Video Editor Pro'
    version = $version; publisher = 'DriveVault'; executable = 'DriveVaultVideoEditorPro.exe'; accentColor = '#8B5CF6'; dataFolder = 'VideoEditorPro'
}
$product | ConvertTo-Json | Set-Content (Join-Path $stage 'product.json') -Encoding UTF8

# Release gates: the package is only built when every import resolves (clean-machine check) and the license audit passes.
$ErrorActionPreference = 'Stop'          # a crash inside a gate script must stop packaging, not be ignored
$global:LASTEXITCODE = 0
& "$SuiteRoot\shared\scripts\check-dependencies.ps1" -AppDir $stage
if ($LASTEXITCODE) { throw 'Dependency check failed - the program would not start on a clean PC.' }
$global:LASTEXITCODE = 0
& "$PSScriptRoot\license-audit.ps1" -StageDir $stage -Version $version
if ($LASTEXITCODE) { throw 'License audit failed - not packaging.' }
$ErrorActionPreference = 'Continue'

if ($Sign) {
    & "$SuiteRoot\shared\scripts\sign.ps1" -Files (Join-Path $stage 'DriveVaultVideoEditorPro.exe')
    if ($LASTEXITCODE) { throw 'Signing the executable failed.' }
}

$size = (Get-ChildItem $stage -Recurse | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("Staged {0:N1} MB in {1}" -f $size, $stage)

$isccArgs = @("/DAppVersion=$version", "/DStageDir=$stage", "/DOutputDir=$dist")
if (-not $NoMaster) {
    if (-not $MasterSetup) { $MasterSetup = Join-Path (Split-Path -Parent $DvRoot) 'drivevault-master-pro\dist\DriveVault-Master-Pro-Setup-x64.exe' }
    if (-not (Test-Path $MasterSetup)) { throw "DriveVault Master Pro setup not found: $MasterSetup (build it first with its scripts\package.ps1, or pass -NoMaster)" }
    $MasterSetup = (Resolve-Path $MasterSetup).Path
    $masterVersion = (Get-Item $MasterSetup).VersionInfo.FileVersion
    if (-not $masterVersion) { throw "Cannot read the version of $MasterSetup" }
    Write-Host "Including DriveVault Master Pro $masterVersion from $MasterSetup" -ForegroundColor Cyan
    $isccArgs += @("/DMasterSetup=$MasterSetup", "/DMasterVersion=$masterVersion")
} else {
    Write-Warning 'Building WITHOUT DriveVault Master Pro: this installer will not install the hub.'
}
iscc @isccArgs (Join-Path $DvRoot 'installer\windows\drivevault.iss')
if ($LASTEXITCODE) { throw "Inno Setup compilation failed" }

$setup = Join-Path $dist 'DriveVault-Video-Editor-Pro-Setup-x64.exe'
if ($Sign) {
    & "$SuiteRoot\shared\scripts\sign.ps1" -Files $setup
    if ($LASTEXITCODE) { throw 'Signing the installer failed.' }
}
$hash = (Get-FileHash $setup -Algorithm SHA256).Hash.ToLower()
"$hash *$(Split-Path $setup -Leaf)" | Set-Content ($setup + '.sha256') -Encoding ASCII
Get-Item $setup | Select-Object Name, @{n = 'SizeMB'; e = { [math]::Round($_.Length / 1MB, 1) } }, LastWriteTime | Format-List
Write-Host "Installer: $setup  (SHA-256 $hash)" -ForegroundColor Green
if (-not $Sign) { Write-Host 'NOTE: the installer is NOT code-signed (run with -Sign and a certificate for a release).' -ForegroundColor Yellow }
