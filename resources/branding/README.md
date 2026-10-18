# Application Icon

`wormprofiler.png` is the approved teal planarian artwork on a transparent
background. Qt uses it for application, window, and dialog icons.
`wormprofiler.ico` embeds the same artwork in the Windows executable at 16, 20,
24, 32, 40, 48, 64, 128, and 256 pixels, including 125%, 150%, and 200% scaling.
The icons are embedded at build time; users do not need these loose files.

After replacing the square PNG, regenerate the Windows icon from PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Update-AppIcon.ps1
```

Rebuild the application after updating either file. CMake compiles the Windows
resource only on Windows; other platforms use the Qt resource.

## Artwork Source

Created with the built-in image generation tool from the approved WormProfiler
mockup, using the [production extraction prompt](production-prompt.txt).
