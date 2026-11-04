# Optional documentation capture; ImageMagick is a documentation tool only.
param([switch]$GifsOnly, [switch]$BrowserOnly)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$demoRoot = Join-Path ([IO.Path]::GetTempPath()) ('PicoSlip-demo-' + [guid]::NewGuid().ToString('N'))
$previousDemoRoot = $env:PICOSLIP_DEMO_ROOT
$env:PICOSLIP_DEMO_ROOT = $demoRoot
$recordings = if ($BrowserOnly) { @('browser') } else { @('notes', 'browser') }
$finished = $false

function Clear-Frames([string]$name) {
    if ($name -notin $recordings) { throw 'Unknown recording directory.' }
    $directory = Join-Path $root "build/$name-gif"
    if (!(Test-Path -LiteralPath $directory)) { return }
    $files = @(Get-ChildItem -LiteralPath $directory -Force)
    if (@($files | Where-Object { $_.PSIsContainer -or $_.Name -notmatch '^(frame-\d+\.bmp|timing\.txt|palette\.png)$' }).Count) {
        throw 'Unexpected files in the recording directory.'
    }
    foreach ($file in $files) { Remove-Item -LiteralPath $file.FullName }
    Remove-Item -LiteralPath $directory
}

Push-Location $root
try {
    New-Item -ItemType Directory -Path build,docs/screenshots -Force | Out-Null
    if (!(Test-Path -LiteralPath build/screenshot-background.jpg)) {
        Invoke-WebRequest -Uri 'https://images.unsplash.com/photo-1578926375605-eaf7559b1458?fm=jpg&q=60&w=3000&auto=format&fit=crop' -OutFile build/screenshot-background.jpg
    }
    if (!(Test-Path -LiteralPath build/browser-background.jpg)) {
        Invoke-WebRequest -Uri 'https://illustrarch.com/wp-content/uploads/2025/05/birmingham-museums-trust-8wcoY3wcbL0-unsplash.jpg' -OutFile build/browser-background.jpg
    }
    & magick build/screenshot-background.jpg -resize '1800x1200^' -gravity center -extent 1800x1200 BMP3:build/screenshot-background.bmp
    if ($LASTEXITCODE -ne 0) { throw 'Background conversion failed.' }
    & magick build/browser-background.jpg -resize '1000x1000^' -gravity center -extent 1000x1000 BMP3:build/browser-background.bmp
    if ($LASTEXITCODE -ne 0) { throw 'Browser background conversion failed.' }
    foreach ($name in $recordings) { Clear-Frames $name }
    if ($BrowserOnly) { & ./build.cmd screenshots-only --browser-only }
    elseif ($GifsOnly) { & ./build.cmd screenshots-only --gifs-only }
    else { & ./build.cmd screenshots-only }
    if ($LASTEXITCODE -ne 0) { throw 'Native application capture failed.' }
    foreach ($name in $recordings) {
        $framesDirectory = Join-Path $root "build/$name-gif"
        $timeline = @(Get-Content -LiteralPath (Join-Path $framesDirectory 'timing.txt') | ForEach-Object {
            $parts = $_ -split '\|',2
            [pscustomobject]@{Delay=$parts[0];Frame=$parts[1]}
        })
        $sample = @(0, 0.25, 0.5, 0.75, 1 | ForEach-Object { $timeline[[int]($_ * ($timeline.Count - 1))].Frame })
        $palette = Join-Path $framesDirectory 'palette.png'
        & magick @sample -append -colors 192 -unique-colors $palette
        if ($LASTEXITCODE -ne 0) { throw 'GIF palette conversion failed.' }
        $gifArguments = @('-loop','0')
        foreach ($frame in $timeline) { $gifArguments += @('-delay',$frame.Delay,$frame.Frame) }
        $gifArguments += @('+dither','-remap',$palette,'-layers','Optimize',(Join-Path $root "docs/screenshots/$name-demo.gif"))
        & magick @gifArguments
        if ($LASTEXITCODE -ne 0) { throw 'GIF conversion failed.' }
    }
    if (!$GifsOnly -and !$BrowserOnly) {
        foreach ($name in @('notes','palettes')) {
            & magick "build/$name.bmp" -strip -define png:compression-level=9 "docs/screenshots/$name.png"
            if ($LASTEXITCODE -ne 0) { throw 'Screenshot conversion failed.' }
            Remove-Item -LiteralPath "build/$name.bmp"
        }
    }
    $finished = $true
} finally {
    $env:PICOSLIP_DEMO_ROOT = $previousDemoRoot
    if (Test-Path -LiteralPath $demoRoot) {
        $files = @(Get-ChildItem -LiteralPath $demoRoot -Force)
        if (@($files | Where-Object { $_.PSIsContainer -or $_.Name -notmatch '^(notes\.ini|note-.+\.(txt|rtf)(\.tmp)?)$' }).Count) {
            throw 'Unexpected files in the private demo archive.'
        }
        foreach ($file in $files) { Remove-Item -LiteralPath $file.FullName }
        Remove-Item -LiteralPath $demoRoot
    }
    if ($finished) {
        foreach ($name in $recordings) { Clear-Frames $name }
        Remove-Item -LiteralPath build/screenshot-background.bmp,build/browser-background.bmp
    }
    Pop-Location
}
