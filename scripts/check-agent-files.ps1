<#
.SYNOPSIS  Checks that every folder with a CLAUDE.md also has an AGENTS.md with exactly the same content (and vice versa).
.EXAMPLE   .\scripts\check-agent-files.ps1          (add -Fix to copy CLAUDE.md over AGENTS.md)
.OUTPUT    Exit code 0 = in sync, 1 = differences (listed).
#>
param([switch]$Fix)
$root = Split-Path -Parent $PSScriptRoot
$names = 'CLAUDE.md', 'AGENTS.md'
$dirs = Get-ChildItem $root -Recurse -File -Force -Include $names | Where-Object { $_.FullName -notmatch '\\(\.tools|build|dist|\.git)\\' } |
    ForEach-Object { $_.DirectoryName } | Sort-Object -Unique
$bad = 0
foreach ($d in $dirs) {
    $c = Join-Path $d 'CLAUDE.md'; $a = Join-Path $d 'AGENTS.md'
    $rel = $d.Substring($root.Length).TrimStart('\'); if (-not $rel) { $rel = '.' }
    if (-not (Test-Path $c) -or -not (Test-Path $a)) {
        if ($Fix -and (Test-Path $c)) { Copy-Item $c $a -Force; Write-Host "fixed (created AGENTS.md): $rel" -ForegroundColor Yellow; continue }
        Write-Host "MISSING pair in $rel" -ForegroundColor Red; $bad++; continue
    }
    if ((Get-FileHash $c).Hash -ne (Get-FileHash $a).Hash) {
        if ($Fix) { Copy-Item $c $a -Force; Write-Host "fixed (AGENTS.md := CLAUDE.md): $rel" -ForegroundColor Yellow }
        else { Write-Host "DIFFERENT: $rel" -ForegroundColor Red; $bad++ }
    }
}
Write-Host ("{0} folder(s) checked, {1} problem(s)" -f @($dirs).Count, $bad) -ForegroundColor $(if ($bad) { 'Red' } else { 'Green' })
exit $(if ($bad) { 1 } else { 0 })
