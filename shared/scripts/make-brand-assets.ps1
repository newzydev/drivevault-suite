<#
.SYNOPSIS  Draws the installer artwork (wizard side picture and small header picture) in a product's accent colour.
.EXAMPLE   .\scripts\make-brand-assets.ps1                       # Video Editor Pro, violet
           .\scripts\make-brand-assets.ps1 -Accent "#3B82F6" -Title "Image Editor" -Out C:\somewhere
#>
param(
    [string]$Accent = '#8B5CF6',
    [string]$Title = 'Video Editor Pro',
    [string]$Out = (Join-Path (Split-Path -Parent $PSScriptRoot) 'installer\windows'),
    [string]$Logo = (Join-Path (Split-Path -Parent $PSScriptRoot) 'resources\icons\app.png')
)
Add-Type -AssemblyName System.Drawing
$base = [System.Drawing.ColorTranslator]::FromHtml($Accent)
$dark = [System.Drawing.Color]::FromArgb(255, [int]($base.R * 0.28), [int]($base.G * 0.28), [int]($base.B * 0.28))
$logoImg = if (Test-Path $Logo) { [System.Drawing.Image]::FromFile($Logo) } else { $null }
New-Item -ItemType Directory -Force $Out | Out-Null

function New-Picture($w, $h, $logoSize, [bool]$withText, $file) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.TextRenderingHint = 'AntiAliasGridFit'; $g.InterpolationMode = 'HighQualityBicubic'
    $rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, $base, $dark, 70
    $g.FillRectangle($brush, $rect)
    if ($logoImg) {
        $x = [int](($w - $logoSize) / 2)
        $y = if ($withText) { 56 } else { [int](($h - $logoSize) / 2) }
        $g.DrawImage($logoImg, $x, $y, $logoSize, $logoSize)
    }
    if ($withText) {
        $sf = New-Object System.Drawing.StringFormat; $sf.Alignment = 'Center'
        $white = [System.Drawing.Brushes]::White
        $f1 = New-Object System.Drawing.Font 'Segoe UI Semibold', 19, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
        $f2 = New-Object System.Drawing.Font 'Segoe UI', 14, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
        $f3 = New-Object System.Drawing.Font 'Segoe UI', 11, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
        $g.DrawString('DriveVault', $f1, $white, (New-Object System.Drawing.RectangleF 0, 172, $w, 30), $sf)
        $g.DrawString($Title, $f2, $white, (New-Object System.Drawing.RectangleF 4, 200, ($w - 8), 44), $sf)
        $g.DrawString('Professional Power.', $f3, $white, (New-Object System.Drawing.RectangleF 0, 268, $w, 18), $sf)
        $g.DrawString('Beautiful Simplicity.', $f3, $white, (New-Object System.Drawing.RectangleF 0, 284, $w, 18), $sf)
    }
    $g.Dispose()
    $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Bmp)
    $bmp.Dispose()
}
New-Picture 164 314 80 $true (Join-Path $Out 'wizard-large.bmp')
New-Picture 55 55 40 $false (Join-Path $Out 'wizard-small.bmp')
Write-Host "Wrote wizard-large.bmp and wizard-small.bmp in $Out (accent $Accent)"
