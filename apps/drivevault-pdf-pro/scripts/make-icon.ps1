<#
.SYNOPSIS  Draws the DriveVault PDF Pro icon (red rounded square with a white page and a folded corner) as app.png + app.ico.
.EXAMPLE   .\scripts\make-icon.ps1
#>
param([string]$Accent = '#EF4444', [string]$Out = (Join-Path (Split-Path -Parent $PSScriptRoot) 'resources\icons'))
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
$base = [System.Drawing.ColorTranslator]::FromHtml($Accent)
$dark = [System.Drawing.Color]::FromArgb(255, [int]($base.R * 0.62), [int]($base.G * 0.62), [int]($base.B * 0.62))

function New-RoundPath([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $p.AddArc($x, $y, $d, $d, 180, 90); $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90); $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90); $p.CloseFigure()
    return $p
}

function New-Icon([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.TextRenderingHint = 'AntiAliasGridFit'; $g.Clear([System.Drawing.Color]::Transparent)
    $bg = New-RoundPath 0 0 ($s - 1) ($s - 1) ($s * 0.22)
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.Rectangle 0, 0, $s, $s), $base, $dark, 60
    $g.FillPath($brush, $bg)
    # the page: white, folded top right corner
    $px = $s * 0.26; $py = $s * 0.16; $pw = $s * 0.48; $ph = $s * 0.68; $fold = $s * 0.16
    $page = New-Object System.Drawing.Drawing2D.GraphicsPath
    $page.AddPolygon(@(
        (New-Object System.Drawing.PointF $px, $py),
        (New-Object System.Drawing.PointF ($px + $pw - $fold), $py),
        (New-Object System.Drawing.PointF ($px + $pw), ($py + $fold)),
        (New-Object System.Drawing.PointF ($px + $pw), ($py + $ph)),
        (New-Object System.Drawing.PointF $px, ($py + $ph))))
    $g.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(248, 255, 255, 255))), $page)
    $corner = New-Object System.Drawing.Drawing2D.GraphicsPath
    $corner.AddPolygon(@(
        (New-Object System.Drawing.PointF ($px + $pw - $fold), $py),
        (New-Object System.Drawing.PointF ($px + $pw), ($py + $fold)),
        (New-Object System.Drawing.PointF ($px + $pw - $fold), ($py + $fold))))
    $g.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(150, $dark.R, $dark.G, $dark.B))), $corner)
    if ($s -ge 32) {
        $font = New-Object System.Drawing.Font 'Segoe UI', ([float]($s * 0.17)), ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
        $fmt = New-Object System.Drawing.StringFormat; $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
        $rect = New-Object System.Drawing.RectangleF $px, ($py + $ph * 0.38), $pw, ($ph * 0.4)
        $g.DrawString('PDF', $font, (New-Object System.Drawing.SolidBrush $base), $rect, $fmt)
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
