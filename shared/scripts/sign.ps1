<#
.SYNOPSIS  Authenticode-signs files (installer, executables, DLLs that DriveVault owns) with signtool.
.DESCRIPTION
  Real release signing needs a code-signing certificate that DriveVault buys (OV / EV, or a cloud HSM service). None is in this repository.
    - PFX file:        .\scripts\sign.ps1 -Files dist\Setup.exe -Pfx C:\keys\drivevault.pfx -Password (Read-Host -AsSecureString)
                       (or set $env:DV_SIGN_PFX / $env:DV_SIGN_PASSWORD)
    - Certificate store: -Thumbprint <sha1>
  -SelfSigned creates a throw-away TEST certificate so that the signing pipeline can be verified. A self-signed signature is NOT trusted
  by Windows and does not remove the SmartScreen warning.
.OUTPUT    Exit code 0 when every file was signed (and the signature verified for the chosen certificate).
#>
param(
    [Parameter(Mandatory)][string[]]$Files,
    [string]$Pfx = $env:DV_SIGN_PFX,
    [object]$Password = $env:DV_SIGN_PASSWORD,
    [string]$Thumbprint = $env:DV_SIGN_THUMBPRINT,
    [string]$TimestampUrl = 'http://timestamp.digicert.com',
    [switch]$NoTimestamp,
    [switch]$SelfSigned
)
$ErrorActionPreference = 'Stop'

$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -like '*\x64\signtool.exe' } | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $signtool) { throw 'signtool.exe (Windows SDK) not found.' }

$tempPfx = $null
$tempThumb = $null
if ($SelfSigned) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=DriveVault TEST certificate (not trusted)' -CertStoreLocation Cert:\CurrentUser\My `
        -NotAfter (Get-Date).AddDays(30)
    $tempThumb = $cert.Thumbprint
    $tempPfx = Join-Path $env:TEMP "dv-test-sign-$([guid]::NewGuid().ToString('N')).pfx"
    $pw = ConvertTo-SecureString 'dv-test' -AsPlainText -Force
    Export-PfxCertificate -Cert $cert -FilePath $tempPfx -Password $pw | Out-Null
    Remove-Item "Cert:\CurrentUser\My\$tempThumb" -Force            # leave nothing behind in the user's certificate store
    $Pfx = $tempPfx
    $Password = 'dv-test'
    $NoTimestamp = $true                                            # a test certificate has nothing to time-stamp
}
if (-not $Pfx -and -not $Thumbprint) {
    throw 'No certificate: pass -Pfx / -Thumbprint (or DV_SIGN_PFX / DV_SIGN_THUMBPRINT), or -SelfSigned for a pipeline test.'
}
if ($Password -is [System.Security.SecureString]) {
    $Password = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($Password))
}

$failed = 0
foreach ($file in $Files) {
    $path = (Resolve-Path $file).Path
    $args = @('sign', '/fd', 'SHA256')
    if ($Pfx) { $args += @('/f', $Pfx); if ($Password) { $args += @('/p', [string]$Password) } } else { $args += @('/sha1', $Thumbprint) }
    if (-not $NoTimestamp) { $args += @('/tr', $TimestampUrl, '/td', 'SHA256') }
    $args += $path
    & $signtool.FullName @args 2>&1 | Out-Null
    $sig = Get-AuthenticodeSignature $path
    $ok = $sig.SignerCertificate -ne $null
    Write-Host ("{0,-60} {1}  signer: {2}  status: {3}" -f (Split-Path $path -Leaf), $(if ($ok) { 'SIGNED' } else { 'NOT SIGNED' }), $sig.SignerCertificate.Subject, $sig.Status)
    if (-not $ok) { $failed++ }
}
if ($tempPfx) { Remove-Item $tempPfx -Force -ErrorAction SilentlyContinue }
if ($failed) { exit 1 }
exit 0
