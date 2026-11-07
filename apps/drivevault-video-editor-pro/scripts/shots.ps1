<#
.SYNOPSIS  Renders the main screens to PNG files (diagnostic; uses the app's --screenshot option).
.EXAMPLE   .\scripts\shots.ps1 -OutDir .\build\shots -Project .\some.dvproj
#>
param(
    [string]$OutDir = "$PSScriptRoot\..\build\shots",
    [string]$Project = '',
    [string]$Exe = "$PSScriptRoot\..\build\release\bin\DriveVaultVideoEditorPro.exe"
)
. "$PSScriptRoot\..\..\..\shared\scripts\env.ps1"
$ErrorActionPreference = 'Continue'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$data = Join-Path $OutDir '_data'
$specs = @(
    @{ n = 'home';        a = @('--screen', 'home') },
    @{ n = 'newproject';  a = @('--screen', 'newproject') },
    @{ n = 'settings';    a = @('--screen', 'settings', '--page', 'general') },
    @{ n = 'settings-appearance'; a = @('--screen', 'settings', '--page', 'appearance') },
    @{ n = 'settings-ai'; a = @('--screen', 'settings', '--page', 'ai') },
    @{ n = 'settings-about'; a = @('--screen', 'settings', '--page', 'about') }
)
if ($Project) {
    foreach ($ws in 'edit', 'media', 'export') {
        $specs += @{ n = "editor-$ws"; a = @('--screen', 'editor', '--workspace', $ws, $Project) }
    }
}
foreach ($lang in 'en', 'th') {
    foreach ($s in $specs) {
        $file = Join-Path $OutDir "$($s.n)-$lang.png"
        & $Exe --data-dir $data --lang $lang --screenshot $file @($s.a) 2>&1 | Out-Null
        Write-Host ("{0,-28} exit={1}" -f "$($s.n)-$lang", $LASTEXITCODE)
    }
}
