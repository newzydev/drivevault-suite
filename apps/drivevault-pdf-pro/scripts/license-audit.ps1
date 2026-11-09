param([Parameter(Mandatory)][string]$StageDir)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $StageDir).Path
$required = @(
    'licenses\LICENSE.txt', 'licenses\THIRD-PARTY-NOTICES.txt', 'licenses\LGPL-3.0.txt',
    'licenses\GPL-3.0.txt', 'licenses\Apache-2.0.txt', 'licenses\OFL-Inter.txt',
    'licenses\OFL-NotoSansThai.txt', 'licenses\Inno-Setup-LICENSE.txt',
    'licenses\PDFium\distribution-LICENSE.txt', 'licenses\PDFium\pdfium.txt',
    'licenses\PDFium\harfbuzz.txt', 'licenses\PDFium\libjpeg_turbo.ijg',
    'licenses\PDFium\libjpeg_turbo.md', 'licenses\PDFium\zlib.txt',
    'licenses\Qt\qtbase-6.8.3.spdx', 'licenses\Qt\qtdeclarative-6.8.3.spdx', 'licenses\Qt\qtsvg-6.8.3.spdx'
)
foreach ($relative in $required) {
    $file = Get-Item -LiteralPath (Join-Path $root $relative)
    if ($file.Length -eq 0 -or $file.PSIsContainer) { throw "Missing or empty notice: $relative" }
}
$qtModules = 'Core', 'Gui', 'LabsFolderListModel', 'Network', 'OpenGL', 'PrintSupport', 'Qml', 'QmlCore',
    'QmlMeta', 'QmlModels', 'QmlWorkerScript', 'Quick', 'QuickControls2', 'QuickControls2Basic',
    'QuickControls2BasicStyleImpl', 'QuickControls2Impl', 'QuickDialogs2', 'QuickDialogs2QuickImpl',
    'QuickDialogs2Utils', 'QuickEffects', 'QuickLayouts', 'QuickShapes', 'QuickTemplates2', 'Svg', 'Widgets'
$qtDlls = $qtModules | ForEach-Object { "Qt6$_.dll" }
$qtPlugins = 'platforms\qwindows.dll', 'platforms\qoffscreen.dll', 'platforms\qminimal.dll',
    'imageformats\qjpeg.dll', 'imageformats\qico.dll', 'imageformats\qgif.dll', 'imageformats\qsvg.dll',
    'iconengines\qsvgicon.dll', 'printsupport\windowsprintersupport.dll', 'styles\qmodernwindowsstyle.dll'
$known = 'DriveVaultPdfPro.exe', 'pdfium.dll', 'qpdf30.dll', 'product.json',
    'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'concrt140.dll',
    'docs\user-guide.en.html', 'docs\user-guide.th.html', 'docs\release-notes.txt'
$counts = @{}
foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -Force -File) {
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Unexpected linked file: $($file.FullName)" }
    $path = $file.FullName.Substring($root.Length + 1)
    $owner = if ($path -in $qtDlls -or $path -in $qtPlugins) { 'Qt' }
    elseif ($path -match '^qml\\(?:QML|Qt|QtQml|QtQuick|QtCore|QtQuick\.2)(?:\\|$)' -and
        ($file.Extension -in '.qml', '.qmltypes', '.js', '.dll' -or $file.Name -eq 'qmldir') -and
        (Test-Path -LiteralPath (Join-Path $env:DV_QT_DIR $path) -PathType Leaf) -and
        $path -notmatch 'Quick3D|VirtualKeyboard|QuickTimeline|\\tooling\\|\\debugger\\|plugind\.dll$') { 'Qt' }
    elseif ($path -eq 'pdfium.dll' -or $path -match '^licenses\\PDFium\\[^\\]+\.(txt|md|ijg)$') { 'PDFium' }
    elseif ($path -eq 'qpdf30.dll') { 'qpdf' }
    elseif ($path -in $known -or $path -in $required) { 'DriveVault / third-party notices and runtime' }
    else { throw "No approved licence owner for $path" }
    $counts[$owner] = 1 + [int]$counts[$owner]
}
Write-Host "License audit passed: $($counts.Values | Measure-Object -Sum | Select-Object -ExpandProperty Sum) files assigned."
exit 0
