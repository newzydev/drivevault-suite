<#
.SYNOPSIS  Builds, tests and packages the whole suite: Master Pro first (its setup travels inside the Video Editor setup), then the Video Editor,
           then the installer test that exercises both installers.
.EXAMPLE   .\scripts\build-all.ps1
           .\scripts\build-all.ps1 -SkipBuild      (package the existing release builds)
           .\scripts\build-all.ps1 -Sign           (needs DV_SIGN_PFX / DV_SIGN_THUMBPRINT)
#>
param([switch]$SkipBuild, [switch]$Sign)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$flags = @()
if ($SkipBuild) { $flags += '-SkipBuild' }
if ($Sign) { $flags += '-Sign' }

# Every step runs in its own PowerShell process, like a developer starting it by hand (clean environment, no PATH growth between steps).
function Step([string]$title, [string]$script, [string[]]$arguments) {
    Write-Host "`n=== $title ===" -ForegroundColor Cyan
    & powershell -NoProfile -ExecutionPolicy Bypass -File $script @arguments
    if ($LASTEXITCODE) { throw "$title failed (exit code $LASTEXITCODE)" }
}

Step 'DriveVault Master Pro: build, test, package' "$root\apps\drivevault-master-pro\scripts\package.ps1" $flags
Step 'DriveVault Video Editor Pro: build, test, package' "$root\apps\drivevault-video-editor-pro\scripts\package.ps1" $flags
Step 'Installer test (Video Editor + the Master Pro it installs)' "$root\apps\drivevault-video-editor-pro\scripts\test-installer.ps1" @()

Write-Host "`nDone:" -ForegroundColor Green
Get-ChildItem "$root\apps\*\dist\*Setup*.exe" | ForEach-Object { Write-Host ("  {0}  ({1:N1} MB)" -f $_.FullName, ($_.Length / 1MB)) }
