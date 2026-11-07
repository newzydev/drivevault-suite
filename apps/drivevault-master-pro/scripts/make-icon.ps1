<#
.SYNOPSIS  Draws the DriveVault Master Pro icon (amber rounded square with four tiles = the suite) as app.png + app.ico.
.EXAMPLE   .\scripts\make-icon.ps1
#>
param([string]$Accent = '#F59E0B', [string]$Out = (Join-Path (Split-Path -Parent $PSScriptRoot) 'resources\icons'))
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
$base = [System.Drawing.ColorTranslator]::FromHtml($Accent)
$dark = [System.Drawing.Color]::FromArgb(255, [int]($base.R * 0.62), [int]($base.G * 0.62), [int]($base.B * 0.62))

function New-Icon([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.Clear([System.Drawing.Color]::Transparent)
    $r = [int]($s * 0.22)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $path.AddArc(0, 0, $d, $d, 180, 90); $path.AddArc($s - $d - 1, 0, $d, $d, 270, 90)
    $path.AddArc($s - $d - 1, $s - $d - 1, $d, $d, 0, 90); $path.AddArc(0, $s - $d - 1, $d, $d, 90, 90); $path.CloseFigure()
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.Rectangle 0, 0, $s, $s), $base, $dark, 60
    $g.FillPath($brush, $path)
    $tile = [int]($s * 0.24); $gap = [int]($s * 0.07); $total = 2 * $tile + $gap; $x0 = [int](($s - $total) / 2)
    $white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(245, 255, 255, 255))
    $soft = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(150, 255, 255, 255))
    $tr = [int]($tile * 0.28)
    foreach ($i in 0..3) {
        $x = $x0 + ($i % 2) * ($tile + $gap); $y = $x0 + [int]([math]::Floor($i / 2)) * ($tile + $gap)
        $p = New-Object System.Drawing.Drawing2D.GraphicsPath; $td = [math]::Max(2, $tr * 2)
        $p.AddArc($x, $y, $td, $td, 180, 90); $p.AddArc($x + $tile - $td, $y, $td, $td, 270, 90)
        $p.AddArc($x + $tile - $td, $y + $tile - $td, $td, $td, 0, 90); $p.AddArc($x, $y + $tile - $td, $td, $td, 90, 90); $p.CloseFigure()
        $g.FillPath($(if ($i -eq 0) { $white } else { $soft }), $p)
    }
    $g.Dispose()
    return $bmp
}
$sizes = 16, 24, 32, 48, 64, 128, 256
$pngs = @{}
foreach ($s in $sizes) {
    $bmp = New-Icon $s
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngs[$s] = $ms.ToArray()
    if ($s -eq 256) { [IO.File]::WriteAllBytes((Join-Path $Out 'app.png'), $pngs[$s]) }
    $bmp.Dispose()
}
# ICO container with PNG-compressed images
$ico = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $ico
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
foreach ($s in $sizes) {
    $dim = if ($s -ge 256) { 0 } else { $s }
    $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$pngs[$s].Length); $w.Write([uint32]$offset)
    $offset += $pngs[$s].Length
}
foreach ($s in $sizes) { $w.Write($pngs[$s]) }
[IO.File]::WriteAllBytes((Join-Path $Out 'app.ico'), $ico.ToArray())
Write-Host "Wrote app.png and app.ico in $Out"
