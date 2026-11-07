<#
.SYNOPSIS  Clean-machine check: every DLL that a file of the deployed application imports must be shipped in the folder or be
           a Windows inbox DLL. The Visual C++ runtime is NOT accepted from System32 (a clean PC may not have it).
.EXAMPLE   .\scripts\check-dependencies.ps1 -AppDir .\dist\stage
.OUTPUT    Exit code 0 = closed, 1 = something is missing (listed).
#>
param([Parameter(Mandatory)][string]$AppDir)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\env.ps1"
$ErrorActionPreference = 'Continue'
$AppDir = (Resolve-Path $AppDir).Path
$sys = Join-Path $env:SystemRoot 'System32'
# Not part of Windows: must be next to the application.
$mustShip = '^(msvcp|vcruntime|vcomp|concrt|vccorlib|mfc|atl)\d*'

$files = Get-ChildItem $AppDir -Recurse -File | Where-Object { $_.Extension -in '.dll', '.exe' -and $_.Name -ne 'vc_redist.x64.exe' }
$shipped = @{}
foreach ($f in $files) { $shipped[$f.Name.ToLower()] = $true }

$missing = @{}
foreach ($f in $files) {
    $out = & dumpbin /nologo /dependents $f.FullName 2>&1
    $in = $false
    foreach ($line in $out) {
        if ($line -match 'following dependencies') { $in = $true; continue }
        if ($line -match '^\s*Summary') { break }
        if (-not $in -or $line -notmatch '^\s+(\S+\.dll)\s*$') { continue }
        $dep = $Matches[1].ToLower()
        if ($dep -like 'api-ms-win-*' -or $dep -like 'ext-ms-win-*') { continue }
        # where this file may find it: its own folder, the application folder (for the main program and its plug-ins)
        $local = $shipped.ContainsKey($dep)
        if ($f.DirectoryName -like '*\ai\whisper') {
            $local = Test-Path (Join-Path $f.DirectoryName $dep)        # a separate program: only its own folder counts
        }
        if ($local) { continue }
        if ($dep -notmatch $mustShip -and (Test-Path (Join-Path $sys $dep))) { continue }
        $rel = $f.FullName.Substring($AppDir.Length + 1)
        if (-not $missing.ContainsKey($dep)) { $missing[$dep] = @() }
        $missing[$dep] += $rel
    }
}
Write-Host ("Checked {0} files in {1}" -f $files.Count, $AppDir)
if ($missing.Count -eq 0) {
    Write-Host "OK: every import is shipped or a Windows inbox DLL." -ForegroundColor Green
    exit 0
}
foreach ($k in $missing.Keys | Sort-Object) {
    Write-Host ("MISSING {0}  (needed by {1})" -f $k, (($missing[$k] | Select-Object -First 3) -join ', ')) -ForegroundColor Red
}
exit 1
