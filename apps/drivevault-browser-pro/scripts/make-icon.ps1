<#
.SYNOPSIS  Draws the DriveVault Browser Pro icon (cyan rounded square with a white globe) as app.png + app.ico.
.EXAMPLE   .\scripts\make-icon.ps1
#>
param([string]$Accent = '#06B6D4', [string]$Out = (Join-Path (Split-Path -Parent $PSScriptRoot) 'resources\icons'))
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
$base = [System.Drawing.ColorTranslator]::FromHtml($Accent)
$dark = [System.Drawing.Color]::FromArgb(255, [int]($base.R * 0.55), [int]($base.G * 0.55), [int]($base.B * 0.55))

function New-Icon([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.Clear([System.Drawing.Color]::Transparent)
    $r = [int]($s * 0.22); $d = $r * 2
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc(0, 0, $d, $d, 180, 90); $path.AddArc($s - $d - 1, 0, $d, $d, 270, 90)
    $path.AddArc($s - $d - 1, $s - $d - 1, $d, $d, 0, 90); $path.AddArc(0, $s - $d - 1, $d, $d, 90, 90); $path.CloseFigure()
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.Rectangle 0, 0, $s, $s), $base, $dark, 60
    $g.FillPath($brush, $path)
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(245, 255, 255, 255)), ([single][math]::Max(1.2, $s * 0.055))
    $m = $s * 0.24; $w = $s - 2 * $m
    $g.DrawEllipse($pen, [single]$m, [single]$m, [single]$w, [single]$w)                                   # globe
    $g.DrawEllipse($pen, [single]($s * 0.40), [single]$m, [single]($s * 0.20), [single]$w)                 # meridian
    $g.DrawLine($pen, [single]$m, [single]($s / 2), [single]($s - $m), [single]($s / 2))                   # equator
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
    if ($s -eq 256) { [System.IO.File]::WriteAllBytes((Join-Path $Out 'app.png'), $pngs[$s]) }
    $bmp.Dispose()
}
# ICO container with PNG-compressed images
$ico = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ico
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
foreach ($s in $sizes) {
    $dim = if ($s -ge 256) { 0 } else { $s }
    $bw.Write([byte]$dim); $bw.Write([byte]$dim); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32); $bw.Write([uint32]$pngs[$s].Length); $bw.Write([uint32]$offset)
    $offset += $pngs[$s].Length
}
foreach ($s in $sizes) { $bw.Write($pngs[$s]) }
$bw.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $Out 'app.ico'), $ico.ToArray())
Write-Host "Icon written to $Out" -ForegroundColor Green
