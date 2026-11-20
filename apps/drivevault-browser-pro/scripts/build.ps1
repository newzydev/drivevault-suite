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
# Keep the Desktop OAuth material out of source control and compiler command lines.
$oauthPath = if ($env:DV_GOOGLE_OAUTH_FILE) { $env:DV_GOOGLE_OAUTH_FILE } else {
    Join-Path (Split-Path -Parent $SuiteRoot) 'GOOGLE_LOGIN_CLIENT_ID.txt'
}
$oauthHeader = Join-Path $build 'generated/GoogleDesktopOAuth.h'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $oauthHeader) | Out-Null
$oauth = @{}
if (Test-Path -LiteralPath $oauthPath -PathType Leaf) {
    foreach ($line in Get-Content -LiteralPath $oauthPath) {
        if ($line -match '^\s*(GOOGLE_LOGIN_CLIENT_(?:ID|SECRET))\s*=\s*(.+?)\s*$') {
            $oauth[$Matches[1]] = $Matches[2].Trim().Trim([char]34, [char]39)
        }
    }
    if ($oauth['GOOGLE_LOGIN_CLIENT_ID'] -notmatch '^[A-Za-z0-9_-]+\.apps\.googleusercontent\.com$' -or
        $oauth['GOOGLE_LOGIN_CLIENT_SECRET'] -notmatch '^[A-Za-z0-9_-]+$') {
        throw 'Invalid Google Desktop OAuth credential file.'
    }
} elseif ($env:DV_GOOGLE_OAUTH_FILE) {
    throw 'The configured Google Desktop OAuth file does not exist.'
}
$key = $iv = $ciphertext = ''
if ($oauth.Count -gt 0) {
    $aes = [Security.Cryptography.Aes]::Create()
    try {
        $aes.KeySize = 256
        $aes.GenerateKey()
        $aes.GenerateIV()
        $plain = [Text.Encoding]::UTF8.GetBytes(($oauth | ConvertTo-Json -Compress))
        try {
            $encryptor = $aes.CreateEncryptor()
            try {
                $ciphertext = [Convert]::ToBase64String($encryptor.TransformFinalBlock($plain, 0, $plain.Length))
            } finally { $encryptor.Dispose() }
        } finally { [Array]::Clear($plain, 0, $plain.Length) }
        $key = [Convert]::ToBase64String($aes.Key)
        $iv = [Convert]::ToBase64String($aes.IV)
    } finally { $aes.Dispose() }
}
@"
#pragma once
namespace dv::br::desktopOAuth {
inline constexpr char key[] = "$key";
inline constexpr char iv[] = "$iv";
inline constexpr char ciphertext[] = "$ciphertext";
}
"@ | Set-Content -LiteralPath $oauthHeader -Encoding ASCII
$pdfiumPath = if ($env:DV_PDFIUM_DIR) { $env:DV_PDFIUM_DIR } else { Join-Path $SuiteRoot '.tools\pdfium' }
cmake -S $DvRoot -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_PREFIX_PATH=$($env:DV_QT_DIR)" "-DDB_PDFIUM_DIR=$pdfiumPath"
if ($LASTEXITCODE) { throw "CMake configure failed" }
cmake --build $build
if ($LASTEXITCODE) { throw "Build failed" }
if ($Test) {
    $env:DV_NO_AUDIO = '1'
    ctest --test-dir $build --output-on-failure
    if ($LASTEXITCODE) { throw "Tests failed" }
}
Write-Host "Build OK: $build\bin\DriveVaultBrowserPro.exe" -ForegroundColor Green
