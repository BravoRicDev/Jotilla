# Optional documentation capture; ImageMagick is a documentation tool only.
param([switch]$Welcome)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$demoRoot = Join-Path ([IO.Path]::GetTempPath()) ('PicoSlip-demo-' + [guid]::NewGuid().ToString('N'))
$previousDemoRoot = $env:PICOSLIP_DEMO_ROOT
$env:PICOSLIP_DEMO_ROOT = $demoRoot
Push-Location $root
try {
    New-Item -ItemType Directory -Path build,docs/screenshots -Force | Out-Null
    $url = 'https://images.unsplash.com/photo-1578926375605-eaf7559b1458?fm=jpg&q=60&w=3000&auto=format&fit=crop&ixlib=rb-4.1.0&ixid=M3wxMjA3fDB8MHxwaG90by1yZWxhdGVkfDE1fHx8ZW58MHx8fHx8'
    if (!(Test-Path -LiteralPath build/screenshot-background.jpg)) {
        Invoke-WebRequest -Uri $url -OutFile build/screenshot-background.jpg
    }
    & magick build/screenshot-background.jpg -resize '1800x1200^' -gravity center -extent 1800x1200 BMP3:build/screenshot-background.bmp
    if ($LASTEXITCODE -ne 0) { throw 'Background conversion failed.' }
    $framesDirectory = Join-Path $root $(if ($Welcome) { 'build/welcome-gif' } else { 'build/gif' })
    $resolvedFrames = [IO.Path]::GetFullPath($framesDirectory)
    if (!$resolvedFrames.StartsWith(([IO.Path]::GetFullPath((Join-Path $root 'build')) + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Frame directory is outside the build directory.'
    }
    if (Test-Path -LiteralPath $resolvedFrames) { Remove-Item -LiteralPath $resolvedFrames -Recurse -Force }
    if ($Welcome) { & ./build.cmd screenshots-only --welcome }
    else { & ./build.cmd screenshots-only }
    if ($LASTEXITCODE -ne 0) { throw 'Native application capture failed.' }
    $timeline = @(Get-Content -LiteralPath (Join-Path $framesDirectory 'timing.txt') | ForEach-Object {
        $parts = $_ -split '\|',2
        [pscustomobject]@{Delay=$parts[0];Frame=$parts[1]}
    })
    $sample = @(0, 0.25, 0.5, 0.75, 1 | ForEach-Object { $timeline[[int](($_ * ($timeline.Count - 1)))].Frame })
    $palette = Join-Path $framesDirectory 'palette.png'
    & magick @sample -append -colors 192 -unique-colors $palette
    if ($LASTEXITCODE -ne 0) { throw 'GIF palette conversion failed.' }
    $gifArguments = @('-loop','0')
    foreach ($frame in $timeline) { $gifArguments += @('-delay',$frame.Delay,$frame.Frame) }
    $outputGif = if ($Welcome) { 'welcome-typing.gif' } else { 'menu-demo.gif' }
    $gifArguments += @('+dither','-remap',$palette,'-layers','Optimize',(Join-Path $root "docs/screenshots/$outputGif"))
    & magick @gifArguments
    if ($LASTEXITCODE -ne 0) { throw 'GIF conversion failed.' }
    if ($Welcome) { return }
    & magick build/notes.bmp -strip -define png:compression-level=9 build/readme-notes.png
    if ($LASTEXITCODE -ne 0) { throw 'Overview conversion failed.' }
    & magick build/palettes.bmp -strip -define png:compression-level=9 build/readme-palettes.png
    if ($LASTEXITCODE -ne 0) { throw 'Palette conversion failed.' }
    [IO.File]::Copy((Join-Path $root 'build/readme-notes.png'),(Join-Path $root 'docs/screenshots/notes.png'),$true)
    [IO.File]::Copy((Join-Path $root 'build/readme-palettes.png'),(Join-Path $root 'docs/screenshots/palettes.png'),$true)
} finally {
    $env:PICOSLIP_DEMO_ROOT = $previousDemoRoot
    $resolvedDemo = [IO.Path]::GetFullPath($demoRoot)
    $temporary = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if (!$resolvedDemo.StartsWith($temporary, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedDemo) -notlike 'PicoSlip-demo-*') { throw 'Unsafe demo cleanup path.' }
    if (Test-Path -LiteralPath $resolvedDemo) { Remove-Item -LiteralPath $resolvedDemo -Recurse -Force }
    Pop-Location
}
