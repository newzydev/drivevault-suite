param([Parameter(Mandatory)][string]$Path)

$ErrorActionPreference = 'Stop'
$resolved = (Resolve-Path -LiteralPath $Path).Path
$algorithm = [Security.Cryptography.SHA256]::Create()
try {
    $stream = [IO.File]::OpenRead($resolved)
    try {
        ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    } finally {
        $stream.Dispose()
    }
} finally {
    $algorithm.Dispose()
}
