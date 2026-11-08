# Optional documentation capture; ImageMagick is a documentation tool only.
param(
    [switch]$OverviewOnly,
    [switch]$StillsOnly,
    [switch]$GifsOnly,
    [switch]$BrowserOnly,
    [switch]$NotesOnly,
    [string]$BackgroundRoot = (Join-Path $env:USERPROFILE 'Downloads/StillTyping/Selection')
)
$ErrorActionPreference = 'Stop'
if ($BrowserOnly -and $NotesOnly) { throw 'Choose one walkthrough, or omit both switches to capture both.' }
if (($OverviewOnly -or $StillsOnly) -and ($GifsOnly -or $BrowserOnly -or $NotesOnly)) { throw 'Still captures cannot be combined with walkthrough switches.' }
$root = Split-Path -Parent $PSScriptRoot
$demoRoot = Join-Path ([IO.Path]::GetTempPath()) ('Jotila-demo-' + [guid]::NewGuid().ToString('N'))
$previousDemoRoot = $env:JOTILA_DEMO_ROOT
$env:JOTILA_DEMO_ROOT = $demoRoot
$recordings = if ($OverviewOnly -or $StillsOnly) { @() } elseif ($BrowserOnly) { @('browser') } elseif ($NotesOnly) { @('notes') } else { @('notes', 'browser') }
$finished = $false
$backgrounds = [ordered]@{
    'notes-background' = 'birmingham-museums-trust-oTfV3cBAhSo-unsplash.jpg'
    'palettes-background' = 'birmingham-museums-trust-0ZbwwI6VV3E-unsplash.jpg'
    'notes-gif-background' = '1914P308  February Fill Dyke.jpg'
    'browser-background' = '1915P97 Walton-on-the-Naze.jpg'
}
if ($OverviewOnly) { $backgrounds = [ordered]@{ 'notes-background' = $backgrounds['notes-background'] } }
elseif ($StillsOnly) { $backgrounds = [ordered]@{ 'notes-background' = $backgrounds['notes-background']; 'palettes-background' = $backgrounds['palettes-background'] } }

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
    foreach ($entry in $backgrounds.GetEnumerator()) {
        $source = Join-Path $BackgroundRoot $entry.Value
        if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing local background: $source" }
    }
    foreach ($entry in $backgrounds.GetEnumerator()) {
        $size = if ($entry.Key -in @('notes-background', 'palettes-background')) { '1040x640' } else { '1000x1000' }
        & magick (Join-Path $BackgroundRoot $entry.Value) -auto-orient -resize "$size^" -gravity center -extent $size "BMP3:build/$($entry.Key).bmp"
        if ($LASTEXITCODE -ne 0) { throw 'Background conversion failed.' }
    }
    foreach ($name in $recordings) { Clear-Frames $name }
    if ($OverviewOnly) { & ./build.cmd screenshots-only --overview-only }
    elseif ($StillsOnly) { & ./build.cmd screenshots-only --stills-only }
    elseif ($BrowserOnly) { & ./build.cmd screenshots-only --browser-only }
    elseif ($NotesOnly) { & ./build.cmd screenshots-only --notes-only }
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
    if ($OverviewOnly -or (!$GifsOnly -and !$BrowserOnly -and !$NotesOnly)) {
        $screenshots = if ($OverviewOnly) { @('notes') } else { @('notes','palettes') }
        foreach ($name in $screenshots) {
            & magick "build/$name.bmp" -strip -define png:compression-level=9 "docs/screenshots/$name.png"
            if ($LASTEXITCODE -ne 0) { throw 'Screenshot conversion failed.' }
            Remove-Item -LiteralPath "build/$name.bmp"
        }
    }
    $finished = $true
} finally {
    $env:JOTILA_DEMO_ROOT = $previousDemoRoot
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
        foreach ($name in $backgrounds.Keys) { Remove-Item -LiteralPath "build/$name.bmp" }
    }
    Pop-Location
}
