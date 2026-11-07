<#
.SYNOPSIS  Verifies the signing pipeline (sign.ps1) on a COPY of the installer with a throw-away self-signed TEST certificate.
.DESCRIPTION
  This proves that the files can be signed and that the signature is read back; it does not make anything trusted. A real release is signed with
  a certificate that DriveVault buys: .\scripts\package.ps1 -Sign (with DV_SIGN_PFX / DV_SIGN_PASSWORD or DV_SIGN_THUMBPRINT set).
#>
param([string]$Setup = "$PSScriptRoot\..\dist\DriveVault-Video-Editor-Pro-Setup-x64.exe")
$ErrorActionPreference = 'Continue'
$Setup = (Resolve-Path $Setup).Path
$work = Join-Path $env:TEMP 'dv-sign-test'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force $work | Out-Null
$copy = Join-Path $work 'Setup-copy.exe'
Copy-Item $Setup $copy
$before = (Get-FileHash $copy -Algorithm SHA256).Hash
$unsignedStatus = (Get-AuthenticodeSignature $copy).Status

& "$PSScriptRoot\..\..\..\shared\scripts\sign.ps1" -Files $copy -SelfSigned
$rc = $LASTEXITCODE
$sig = Get-AuthenticodeSignature $copy
$after = (Get-FileHash $copy -Algorithm SHA256).Hash

$results = @(
    [pscustomobject]@{ Check = 'the original installer is unsigned'; Result = $(if ($unsignedStatus -eq 'NotSigned') { 'PASS' } else { 'FAIL' }) }
    [pscustomobject]@{ Check = 'sign.ps1 -SelfSigned exits 0'; Result = $(if ($rc -eq 0) { 'PASS' } else { 'FAIL' }) }
    [pscustomobject]@{ Check = 'the copy now carries a signature from the test certificate'; Result = $(if ($sig.SignerCertificate -and $sig.SignerCertificate.Subject -like '*DriveVault TEST*') { 'PASS' } else { 'FAIL' }) }
    [pscustomobject]@{ Check = 'the signed file differs from the original'; Result = $(if ($before -ne $after) { 'PASS' } else { 'FAIL' }) }
    [pscustomobject]@{ Check = 'a test certificate is NOT reported as trusted (as expected)'; Result = $(if ($sig.Status -ne 'Valid') { 'PASS' } else { 'FAIL' }) }
    [pscustomobject]@{ Check = 'the real installer in dist\ was not modified'; Result = $(if ((Get-AuthenticodeSignature $Setup).Status -eq 'NotSigned') { 'PASS' } else { 'FAIL' }) }
    [pscustomobject]@{ Check = 'the test certificate is not left in the user certificate store'; Result = $(if (-not (Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -like '*DriveVault TEST*' })) { 'PASS' } else { 'FAIL' }) }
)
$results | Format-Table -AutoSize
Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
$failed = @($results | Where-Object Result -eq 'FAIL').Count
Write-Host ("{0} checks, {1} failed (signature status of the test file: {2})" -f $results.Count, $failed, $sig.Status) -ForegroundColor $(if ($failed) { 'Red' } else { 'Green' })
exit $failed
