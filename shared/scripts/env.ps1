# Dot-source this file to get a shell with the MSVC x64 toolchain, CMake, Ninja and Qt on PATH.
#   . "$PSScriptRoot\..\..\..\shared\scripts\env.ps1"        (from apps\<app>\scripts)
# Sets $SuiteRoot (the repository root) and $DvRoot (the root of the app whose script dot-sourced this file; the suite root when a shared script did).
# The suite's .tools\ folder (portable CMake/Ninja/Qt/Inno Setup/FFmpeg/whisper) is used when present;
# otherwise tools found on PATH are used. Override Qt with $env:DV_QT_DIR, the tools folder with $env:DV_TOOLS_DIR.

$ErrorActionPreference = 'Stop'
$script:SuiteRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$caller = (Get-PSCallStack | Select-Object -Skip 1 -First 1).ScriptName
$callerDir = if ($caller) { Split-Path -Parent $caller } else { $null }
$script:DvRoot = if ($callerDir -and (Split-Path -Leaf $callerDir) -eq 'scripts' -and (Split-Path -Parent $callerDir) -ne $SuiteRoot -and
                     (Split-Path -Leaf (Split-Path -Parent (Split-Path -Parent $callerDir))) -eq 'apps') { Split-Path -Parent $callerDir } else { $SuiteRoot }
$tools = if ($env:DV_TOOLS_DIR) { $env:DV_TOOLS_DIR } else { Join-Path $SuiteRoot '.tools' }

function Find-VcVars {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found - install Visual Studio Build Tools 2022 (C++ workload)." }
    $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $install) { throw "No Visual Studio installation with the C++ x64 tools was found." }
    $vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }
    return $vcvars
}

# PATH entries are added once: this file is sourced again by the scripts it calls, and a PATH that keeps growing breaks child processes.
function Add-ToPath([string]$entry) {
    if (-not (";$env:PATH;").ToLower().Contains(";$($entry.ToLower());")) { $env:PATH = "$entry;$env:PATH" }
}

if (-not $env:VSCMD_VER) {                      # the MSVC environment is imported only once per process
    $vcvars = Find-VcVars
    Add-ToPath "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
    $ErrorActionPreference = "Continue"
    cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
    }
    $ErrorActionPreference = "Stop"
}

foreach ($dir in @("$tools\cmake\bin", "$tools\ninja", "$tools\InnoSetup")) {
    if (Test-Path $dir) { Add-ToPath $dir }
}

if (-not $env:DV_QT_DIR) {
    $candidate = Get-ChildItem "$tools\Qt" -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if ($candidate) { $env:DV_QT_DIR = Join-Path $candidate.FullName 'msvc2022_64' }
}
if (-not $env:DV_QT_DIR -or -not (Test-Path $env:DV_QT_DIR)) {
    throw "Qt 6.8+ (MSVC 2022 x64) not found. Set `$env:DV_QT_DIR or see README.md (Prerequisites)."
}
Add-ToPath "$($env:DV_QT_DIR)\bin"
Write-Host "Toolchain ready. Qt: $($env:DV_QT_DIR)" -ForegroundColor Green
