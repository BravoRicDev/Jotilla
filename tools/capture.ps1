# Optional documentation capture; ImageMagick is a documentation tool only.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    New-Item -ItemType Directory -Path build,docs/screenshots -Force | Out-Null
    $url = 'https://images.unsplash.com/photo-1578926375605-eaf7559b1458?fm=jpg&q=60&w=3000&auto=format&fit=crop&ixlib=rb-4.1.0&ixid=M3wxMjA3fDB8MHxwaG90by1yZWxhdGVkfDE1fHx8ZW58MHx8fHx8'
    if (!(Test-Path -LiteralPath build/screenshot-background.jpg)) {
        Invoke-WebRequest -Uri $url -OutFile build/screenshot-background.jpg
    }
    & magick build/screenshot-background.jpg -resize '1800x1200^' -gravity center -extent 1800x1200 BMP3:build/screenshot-background.bmp
    if ($LASTEXITCODE -ne 0) { throw 'Background conversion failed.' }
    & ./build.cmd screenshots-only
    if ($LASTEXITCODE -ne 0) { throw 'Native application capture failed.' }
    & magick build/notes.bmp -strip -define png:compression-level=9 build/readme-notes.png
    if ($LASTEXITCODE -ne 0) { throw 'Overview conversion failed.' }
    & magick build/palettes.bmp -strip -define png:compression-level=9 build/readme-palettes.png
    if ($LASTEXITCODE -ne 0) { throw 'Palette conversion failed.' }
    & magick build/gif/frame-*.bmp -append -colors 192 -unique-colors build/gif/palette.png
    if ($LASTEXITCODE -ne 0) { throw 'GIF palette conversion failed.' }
    & magick -delay 85 -loop 0 build/gif/frame-*.bmp +dither -remap build/gif/palette.png -layers Optimize build/readme-menu-demo.gif
    if ($LASTEXITCODE -ne 0) { throw 'GIF conversion failed.' }
    [IO.File]::Copy((Join-Path $root 'build/readme-notes.png'),(Join-Path $root 'docs/screenshots/notes.png'),$true)
    [IO.File]::Copy((Join-Path $root 'build/readme-palettes.png'),(Join-Path $root 'docs/screenshots/palettes.png'),$true)
    [IO.File]::Copy((Join-Path $root 'build/readme-menu-demo.gif'),(Join-Path $root 'docs/screenshots/menu-demo.gif'),$true)
} finally { Pop-Location }
