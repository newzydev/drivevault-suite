<#
.SYNOPSIS  Configure, build and (optionally) test DriveVault Browser Pro.
.EXAMPLE   .\scripts\build.ps1 -Test
#>
param([ValidateSet('Release', 'Debug')] [string]$Config = 'Release', [switch]$Clean, [switch]$Test)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\..\..\shared\scripts\env.ps1"
$ErrorActionPreference = 'Continue'

$build = Join-Path $DvRoot "build\$($Config.ToLower())"
if ($Clean -and (Test-Path $build)) { Remove-Item $build -Recurse -Force }
cmake -S $DvRoot -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_PREFIX_PATH=$($env:DV_QT_DIR)"
if ($LASTEXITCODE) { throw "CMake configure failed" }
cmake --build $build
if ($LASTEXITCODE) { throw "Build failed" }
if ($Test) {
    $env:DV_NO_AUDIO = '1'
    ctest --test-dir $build --output-on-failure
    if ($LASTEXITCODE) { throw "Tests failed" }
}
Write-Host "Build OK: $build\bin\DriveVaultBrowserPro.exe" -ForegroundColor Green
