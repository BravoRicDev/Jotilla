# Optional documentation capture; ImageMagick is a documentation tool only.
param([switch]$Welcome)
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
    if ($Welcome) { & ./build.cmd screenshots-only --welcome }
    else { & ./build.cmd screenshots-only }
    if ($LASTEXITCODE -ne 0) { throw 'Native application capture failed.' }
    if ($Welcome) {
        $timeline = @(Get-Content -LiteralPath build/welcome-gif/timing.txt | ForEach-Object {
            $parts = $_ -split '\|',2
            [pscustomobject]@{Delay=$parts[0];Frame=$parts[1]}
        })
        $sample = @($timeline[0].Frame,$timeline[[int]($timeline.Count/2)].Frame,$timeline[-3].Frame,$timeline[-1].Frame)
        & magick @sample -append -colors 192 -unique-colors build/welcome-gif/palette.png
        if ($LASTEXITCODE -ne 0) { throw 'Live demo palette conversion failed.' }
        $gifArguments = @('-loop','0')
        foreach ($frame in $timeline) { $gifArguments += @('-delay',$frame.Delay,$frame.Frame) }
        $gifArguments += @('+dither','-remap','build/welcome-gif/palette.png','-layers','Optimize','build/welcome-typing.gif')
        & magick @gifArguments
        if ($LASTEXITCODE -ne 0) { throw 'Live demo GIF conversion failed.' }
        [IO.File]::Copy((Join-Path $root 'build/welcome-typing.gif'),(Join-Path $root 'docs/screenshots/welcome-typing.gif'),$true)
        return
    }
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
