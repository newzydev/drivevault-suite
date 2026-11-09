<#
.SYNOPSIS  Configure, build and (optionally) test DriveVault PDF Pro.
.EXAMPLE   .\scripts\build.ps1 -Test
.EXAMPLE   .\scripts\build.ps1 -Config Debug -Clean
#>
param(
    [ValidateSet('Release', 'Debug')] [string]$Config = 'Release',
    [switch]$Clean,
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\..\..\shared\scripts\env.ps1"
$ErrorActionPreference = 'Continue'   # native tools write progress to stderr; exit codes are checked explicitly

$build = Join-Path $DvRoot "build\$($Config.ToLower())"
if ($Clean -and (Test-Path $build)) { Remove-Item $build -Recurse -Force }

cmake -S $DvRoot -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_PREFIX_PATH=$($env:DV_QT_DIR)"
if ($LASTEXITCODE) { throw "CMake configure failed" }

cmake --build $build
if ($LASTEXITCODE) { throw "Build failed" }

if ($Test) {
    ctest --test-dir $build --output-on-failure
    if ($LASTEXITCODE) { throw "Tests failed" }
}
Write-Host "Build OK: $build\bin\DriveVaultPdfPro.exe" -ForegroundColor Green
