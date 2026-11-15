param([switch]$SkipBuild, [switch]$Sign, [string]$MasterSetup)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\..\..\shared\scripts\env.ps1"
$sharedScripts = Join-Path $SuiteRoot 'shared\scripts'
$dist = Join-Path $DvRoot 'dist'
$stage = Join-Path $dist 'stage'
$buildBin = Join-Path $DvRoot 'build\release\bin'
$exe = Join-Path $buildBin 'DriveVaultPdfPro.exe'
function Read-Version([string]$project) {
    (Select-String -LiteralPath (Join-Path $project 'CMakeLists.txt') -Pattern '^\s+VERSION\s+(\d+\.\d+\.\d+)').Matches[0].Groups[1].Value
}
$version = Read-Version $DvRoot
$hubRoot = Join-Path $SuiteRoot 'apps\drivevault-master-pro'
$hubVersion = Read-Version $hubRoot
if (-not $MasterSetup) { $MasterSetup = Join-Path $hubRoot 'dist\DriveVault-Master-Pro-Setup-x64.exe' }
$MasterSetup = (Resolve-Path -LiteralPath $MasterSetup).Path
$hubInfo = (Get-Item -LiteralPath $MasterSetup).VersionInfo
if (([string]$hubInfo.ProductName).Trim() -ne 'DriveVault Master Pro' -or
    ([string]$hubInfo.CompanyName).Trim() -ne 'DriveVault' -or ([string]$hubInfo.ProductVersion).Trim() -ne $hubVersion) {
    throw "Build Master Pro $hubVersion first. The bundled setup has an unexpected version or publisher."
}
$hubHash = Get-Content -LiteralPath "$MasterSetup.sha256" -Raw
$computedHubHash = & (Join-Path $sharedScripts 'file-sha256.ps1') -Path $MasterSetup
if ($hubHash.Trim() -cne "$computedHubHash *$(Split-Path -Leaf $MasterSetup)") { throw 'Master Pro setup checksum does not match.' }
if ($Sign -and (Get-AuthenticodeSignature -LiteralPath $MasterSetup).Status -ne 'Valid') { throw 'Build and sign Master Pro first.' }

if (-not $SkipBuild) {
    & "$PSScriptRoot\build.ps1" -Config Release -Test
    if ($LASTEXITCODE) { throw 'Build or tests failed.' }
}
$ErrorActionPreference = 'Stop'
if ((Get-Item -LiteralPath $exe).VersionInfo.ProductVersion -ne $version) { throw "Build PDF Pro $version before packaging." }
if (-not (Select-String -LiteralPath (Join-Path $buildBin '..\CMakeCache.txt') -Pattern '^DP_ONNX_AVAILABLE:INTERNAL=ON$')) {
    throw 'Build PDF Pro with the Thai OCR engine before packaging.'
}
$toolsRoot = if ($env:DV_TOOLS_DIR) { $env:DV_TOOLS_DIR } else { Join-Path $SuiteRoot '.tools' }
$pdfium = if ($env:DV_PDFIUM_DIR) { $env:DV_PDFIUM_DIR } else { Join-Path $toolsRoot 'pdfium' }
$qpdfDir = if ($env:DV_QPDF_DIR) { $env:DV_QPDF_DIR } else { Join-Path $toolsRoot 'qpdf' }
$onnxDir = Join-Path $toolsRoot 'onnxruntime-win-x64-1.24.4'
$ocrModels = Join-Path $toolsRoot 'openthai-ocr'
if (-not (Select-String -LiteralPath (Join-Path $pdfium 'VERSION') -Pattern '^BUILD=8138$') -or
    (Get-Content -LiteralPath (Join-Path $pdfium 'args.gn') -Raw) -notmatch 'pdf_enable_v8 = false' -or
    (Get-Content -LiteralPath (Join-Path $pdfium 'args.gn') -Raw) -notmatch 'pdf_enable_xfa = false') {
    throw 'PDFium build does not match the audited build and configuration.'
}
$qpdfVersion = & (Join-Path $qpdfDir 'bin\qpdf.exe') --version
if ($LASTEXITCODE -or $qpdfVersion[0] -ne 'qpdf version 12.4.2') { throw 'qpdf version does not match the audited build.' }
$qtVersion = & (Join-Path $env:DV_QT_DIR 'bin\qmake.exe') -query QT_VERSION
if ($LASTEXITCODE -or $qtVersion -ne '6.8.3') { throw 'Qt version does not match the audited build.' }

New-Item -ItemType Directory -Path $dist -Force | Out-Null
$expectedDist = Join-Path (Resolve-Path -LiteralPath $DvRoot).Path 'dist'
$distItem = Get-Item -LiteralPath $dist
if ($distItem.FullName -ne $expectedDist -or ($distItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe dist directory.' }
function Remove-StagedDirectory([string]$relative) {
    $path = Join-Path $expectedDist $relative
    if (Test-Path -LiteralPath $path) {
        $item = Get-Item -LiteralPath $path
        if ($item.FullName -ne $path -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Unsafe directory: $path" }
        Remove-Item -LiteralPath $item.FullName -Recurse -Force
    }
}
Remove-StagedDirectory 'stage'
New-Item -ItemType Directory -Path $stage | Out-Null
foreach ($binary in 'DriveVaultPdfPro.exe', 'pdfium.dll', 'qpdf30.dll') {
    Copy-Item -LiteralPath (Join-Path $buildBin $binary) -Destination $stage
}
foreach ($engine in @(@{ Root = $pdfium; Name = 'pdfium.dll' }, @{ Root = $qpdfDir; Name = 'qpdf30.dll' })) {
    $deployed = & (Join-Path $sharedScripts 'file-sha256.ps1') -Path (Join-Path $stage $engine.Name)
    $audited = & (Join-Path $sharedScripts 'file-sha256.ps1') -Path (Join-Path $engine.Root "bin\$($engine.Name)")
    if ($deployed -cne $audited) { throw "The build's $($engine.Name) does not match the audited toolchain." }
}
$ocrFiles = [ordered]@{
    'onnxruntime.dll' = 'b95efb2113b603bbbf3f191061c5516a871ed546893c820e4f3b7b6c358dbf2a'
    'models\openthai-ocr-detection.onnx' = '193bab7a04fca699a6c82e6abb5b81bdb28177f0abd4062552b04908dafb19f8'
    'models\openthai-ocr-recognition.onnx' = '82ec89533346faadcb30df96703dad2dd7521de65709e60d83fd9745b8968124'
    'models\openthai-ocr-dict.txt' = '259ef2636cec0eeae225ae9fd9a63fa9305285650c22d7ec34fe8a82b24c2f1a'
}
New-Item -ItemType Directory -Path (Join-Path $stage 'models') | Out-Null
foreach ($relative in $ocrFiles.Keys) {
    $payload = Join-Path $buildBin $relative
    if (-not (Test-Path -LiteralPath $payload -PathType Leaf)) { throw "Build with the Thai OCR prerequisites first: $relative" }
    $digest = & (Join-Path $sharedScripts 'file-sha256.ps1') -Path $payload
    if ($digest -cne $ocrFiles[$relative]) { throw "Thai OCR payload does not match the audited version: $relative" }
    Copy-Item -LiteralPath $payload -Destination (Join-Path $stage $relative)
}

$deploy = @('--release', '--no-translations', '--no-system-d3d-compiler', '--no-system-dxc-compiler', '--no-opengl-sw',
    '--no-compiler-runtime', '--skip-plugin-types', 'generic,networkinformation,tls', '--include-plugins', 'qjpeg,qoffscreen',
    '--qmldir', (Join-Path $DvRoot 'src\ui'), '--dir', $stage, (Join-Path $stage 'DriveVaultPdfPro.exe'))
& windeployqt @deploy
if ($LASTEXITCODE) { throw 'windeployqt failed.' }
foreach ($style in 'Material', 'Universal', 'Fusion', 'Imagine', 'Windows', 'FluentWinUI3') {
    Remove-StagedDirectory "stage\qml\QtQuick\Controls\$style"
    Get-ChildItem -LiteralPath $stage -File -Filter "Qt6QuickControls2$style*.dll" | ForEach-Object {
        Remove-Item -LiteralPath $_.FullName -Force
    }
}
Remove-StagedDirectory 'stage\qmltooling'
Get-ChildItem -LiteralPath (Join-Path $stage 'imageformats') -File | Where-Object Name -NotIn @('qjpeg.dll', 'qico.dll', 'qgif.dll', 'qsvg.dll') | ForEach-Object {
    Remove-Item -LiteralPath $_.FullName -Force
}
foreach ($required in 'imageformats\qjpeg.dll', 'platforms\qwindows.dll', 'platforms\qoffscreen.dll', 'Qt6PrintSupport.dll') {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $required) -PathType Leaf)) { throw "Missing runtime file: $required" }
}

$crt = Get-ChildItem -LiteralPath (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
if (-not $crt) { throw 'The app-local Visual C++ runtime is missing.' }
foreach ($dll in 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'concrt140.dll') {
    Copy-Item -LiteralPath (Join-Path $crt.FullName $dll) -Destination $stage
}

$lic = Join-Path $stage 'licenses'
$pdfiumLic = Join-Path $lic 'PDFium'
$qtLic = Join-Path $lic 'Qt'
$ocrLic = Join-Path $lic 'OCR'
$docs = Join-Path $stage 'docs'
New-Item -ItemType Directory -Path $lic, $pdfiumLic, $qtLic, $ocrLic, $docs -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $DvRoot 'LICENSE.txt') -Destination $lic
Copy-Item -LiteralPath (Join-Path $DvRoot 'docs\THIRD-PARTY-NOTICES.txt') -Destination $lic
Copy-Item -Path (Join-Path $DvRoot 'resources\fonts\OFL-*.txt') -Destination $lic
Copy-Item -Path (Join-Path $DvRoot 'third_party\*.txt') -Destination $lic
Copy-Item -LiteralPath (Join-Path $SuiteRoot 'shared\legal\GPL-3.0.txt') -Destination $lic
foreach ($module in 'qtbase', 'qtdeclarative', 'qtsvg') {
    Copy-Item -LiteralPath (Join-Path $env:DV_QT_DIR "sbom\$module-6.8.3.spdx") -Destination $qtLic
}
Copy-Item -LiteralPath (Join-Path $pdfium 'LICENSE') -Destination (Join-Path $pdfiumLic 'distribution-LICENSE.txt')
Copy-Item -Path (Join-Path $pdfium 'licenses\*') -Destination $pdfiumLic
Copy-Item -LiteralPath (Join-Path $toolsRoot 'InnoSetup\license.txt') -Destination (Join-Path $lic 'Inno-Setup-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $onnxDir 'LICENSE') -Destination (Join-Path $ocrLic 'ONNX-Runtime-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $onnxDir 'ThirdPartyNotices.txt') -Destination (Join-Path $ocrLic 'ONNX-Runtime-ThirdPartyNotices.txt')
# OpenThai 2.0 OCR Text (Apache-2.0, built on PaddlePaddle PP-OCRv6-tiny): the model's own NOTICE goes with the Apache-2.0 text (licenses\Apache-2.0.txt)
Copy-Item -LiteralPath (Join-Path $ocrModels 'NOTICE') -Destination (Join-Path $ocrLic 'OpenThai-OCR-NOTICE.txt')
# llama.cpp (MIT, Vulkan build b12170): the engine of the AI assistant. llama-server.exe runs the language model as a separate program in its own folder (ai\llama).
# The model itself (Qwen3.5-4B, 2.7 GB, Apache-2.0) is NOT shipped: the setup downloads it when the user agrees, or the user presses a button in the program.
$llamaDir = Join-Path $toolsRoot 'llm\llama-vulkan'
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
New-Item -ItemType Directory -Path $llamaStage -Force | Out-Null
foreach ($name in $llamaFiles.Keys) {
    $source = Join-Path $llamaDir $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Put the llama.cpp b12170 Vulkan build in .tools\llm\llama-vulkan first: $name" }
    $digest = & (Join-Path $sharedScripts 'file-sha256.ps1') -Path $source
    if ($digest -cne $llamaFiles[$name]) { throw "llama.cpp file does not match the audited build b12170: $name" }
    Copy-Item -LiteralPath $source -Destination $llamaStage
}
foreach ($dll in 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'concrt140.dll') {
    Copy-Item -LiteralPath (Join-Path $crt.FullName $dll) -Destination $llamaStage      # a separate program only looks in its own folder
}

foreach ($doc in 'user-guide.en.html', 'user-guide.th.html', 'release-notes.txt') {
    Copy-Item -LiteralPath (Join-Path $DvRoot "docs\$doc") -Destination $docs
}
[ordered]@{
    manifestVersion = 1; suite = 'DriveVault'; id = 'drivevault.pdf'; name = 'DriveVault PDF Pro'
    version = $version; publisher = 'DriveVault'; executable = 'DriveVaultPdfPro.exe'; accentColor = '#EF4444'; dataFolder = 'PdfPro'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'product.json') -Encoding UTF8

$global:LASTEXITCODE = 0
& (Join-Path $sharedScripts 'check-dependencies.ps1') -AppDir $stage
if ($LASTEXITCODE) { throw 'Dependency check failed.' }
& "$PSScriptRoot\license-audit.ps1" -StageDir $stage
if ($LASTEXITCODE) { throw 'License audit failed.' }

if ($Sign) {
    & (Join-Path $sharedScripts 'sign.ps1') -Files (Join-Path $stage 'DriveVaultPdfPro.exe')
    if ($LASTEXITCODE) { throw 'Signing the executable failed.' }
}
$art = Join-Path $dist 'wizard'
& (Join-Path $sharedScripts 'make-brand-assets.ps1') -Accent '#EF4444' -Title 'PDF Pro' -Logo (Join-Path $DvRoot 'resources\icons\app.png') -Out $art
$inno = @("/DAppVersion=$version", "/DStageDir=$stage", "/DOutputDir=$dist", "/DArtworkDir=$art",
    "/DMasterSetup=$MasterSetup", "/DMasterVersion=$hubVersion", (Join-Path $DvRoot 'installer\windows\pdf.iss'))
& iscc @inno
if ($LASTEXITCODE) { throw 'Inno Setup compilation failed.' }
$setup = Join-Path $dist 'DriveVault-PDF-Pro-Setup-x64.exe'
if ($Sign) {
    & (Join-Path $sharedScripts 'sign.ps1') -Files $setup
    if ($LASTEXITCODE) { throw 'Signing the installer failed.' }
}
$info = (Get-Item -LiteralPath $setup).VersionInfo
if (([string]$info.ProductName).Trim() -ne 'DriveVault PDF Pro' -or
    ([string]$info.CompanyName).Trim() -ne 'DriveVault' -or ([string]$info.ProductVersion).Trim() -ne $version) {
    throw 'Installer version resource does not match the release manifest.'
}
$hash = & (Join-Path $sharedScripts 'file-sha256.ps1') -Path $setup
"$hash *$(Split-Path -Leaf $setup)" | Set-Content -LiteralPath "$setup.sha256" -Encoding ASCII
Write-Host ("Installer: {0} ({1:N1} MiB)" -f $setup, ((Get-Item -LiteralPath $setup).Length / 1MB))
if (-not $Sign) { Write-Warning 'The installer is unsigned.' }
