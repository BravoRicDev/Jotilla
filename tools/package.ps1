$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root 'dist'
$stage = Join-Path $root 'build\portable'
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $dist 'PicoSlip.exe') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $stage
$readme = @'
PicoSlip - Tiny Sticky Notes for Windows

Run PicoSlip.exe. Ctrl+Alt++ creates a note.
Single-click the tray icon for a new note; double-click to browse notes.
Drag the bar to move it; use - to collapse and X to close.
Right-click the bar for the archive, opacity, title, size and palette.
#Title + Enter makes a title; Shift+Enter keeps it as text.

Notes save automatically in the notes folder beside the EXE.
Keep portable.flag beside the EXE to retain portable mode.
Back up the whole notes folder, including notes.ini.

https://github.com/GarauGarau/picoslip
'@
[IO.File]::WriteAllText((Join-Path $stage 'README.txt'), $readme, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $stage 'portable.flag'), '')
$zip = Join-Path $dist 'PicoSlip-portable-x64.zip'
Compress-Archive -LiteralPath (Join-Path $stage 'PicoSlip.exe'),(Join-Path $stage 'README.txt'),(Join-Path $stage 'LICENSE'),(Join-Path $stage 'portable.flag') -DestinationPath $zip -Force
$assets = @('PicoSlip.exe','PicoSlip-Setup-x64.exe','PicoSlip-portable-x64.zip')
$lines = foreach ($name in $assets) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $dist $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $name"
}
[IO.File]::WriteAllLines((Join-Path $dist 'SHA256SUMS.txt'), $lines, [Text.UTF8Encoding]::new($false))
Get-Item -LiteralPath ($assets | ForEach-Object { Join-Path $dist $_ }) | Select-Object Name,Length
