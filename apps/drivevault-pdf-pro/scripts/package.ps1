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
$toolsRoot = if ($env:DV_TOOLS_DIR) { $env:DV_TOOLS_DIR } else { Join-Path $SuiteRoot '.tools' }
$pdfium = if ($env:DV_PDFIUM_DIR) { $env:DV_PDFIUM_DIR } else { Join-Path $toolsRoot 'pdfium' }
$qpdfDir = if ($env:DV_QPDF_DIR) { $env:DV_QPDF_DIR } else { Join-Path $toolsRoot 'qpdf' }
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
$docs = Join-Path $stage 'docs'
New-Item -ItemType Directory -Path $lic, $pdfiumLic, $qtLic, $docs -Force | Out-Null
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
