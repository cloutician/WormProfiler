param(
    [string]$Source = (Join-Path $PSScriptRoot '../resources/branding/wormprofiler.png')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$destination = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../resources/branding')).Path
$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$frames = [System.Collections.Generic.List[byte[]]]::new()
$image = [System.Drawing.Image]::FromFile($sourcePath)
try {
    if ($image.Width -ne $image.Height) {
        throw 'The application icon source must be square.'
    }
    foreach ($size in $sizes) {
        $bitmap = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $stream = [System.IO.MemoryStream]::new()
        try {
            $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.DrawImage($image, [System.Drawing.Rectangle]::new(0, 0, $size, $size))
            $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
            $frames.Add($stream.ToArray())
        } finally {
            $stream.Dispose()
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
} finally {
    $image.Dispose()
}

# ICO directory entries point to individual full-colour, alpha-preserving PNG frames.
$output = [System.IO.MemoryStream]::new()
$writer = [System.IO.BinaryWriter]::new($output)
try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($index = 0; $index -lt $sizes.Count; ++$index) {
        $dimension = if ($sizes[$index] -eq 256) { 0 } else { $sizes[$index] }
        $writer.Write([byte]$dimension)
        $writer.Write([byte]$dimension)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$frames[$index].Length)
        $writer.Write([uint32]$offset)
        $offset += $frames[$index].Length
    }
    foreach ($frame in $frames) { $writer.Write($frame) }
    $writer.Flush()
    $iconPath = Join-Path $destination 'wormprofiler.ico'
    [System.IO.File]::WriteAllBytes($iconPath, $output.ToArray())
    Write-Output "Created $iconPath ($($sizes -join ', ') px)"
} finally {
    $writer.Dispose()
    $output.Dispose()
}
