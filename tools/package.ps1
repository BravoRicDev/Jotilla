$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root 'dist'
$stage = Join-Path $root 'build\portable'
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $dist 'Jotila.exe') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $stage
$readme = @'
Jotila - Sticky notes, on silicon

Run Jotila.exe. Ctrl+Alt++ creates a note.
Single-click the tray icon for a new note; double-click to browse notes.
Minimizing the browser hides it beside the clock; double-click restores it.
Drag the bar to move it; use - to collapse and X to close.
Ctrl++ / Ctrl+- / Ctrl+mouse wheel or a touchpad pinch resizes the note.
With text selected, the same gesture changes only its font size.
Sizing stops before text would be clipped; Ctrl+Z/Ctrl+Y undo/redo font changes.
Right-click the bar for the archive, opacity, title, size and three original palettes.
Right-click the text for the color wheel; Left/Right browse all 80 Color-Hex Popular palettes.
Click a color to apply and close; Escape cancels.
New notes draw from the selected palette; the star saves a startup favorite.
Select text before right-clicking to change only its text color.
#Title + Shift+Enter makes a title; Enter keeps it as text.
Ctrl+E opens the note explorer; Ctrl+Z/Ctrl+Y undo/redo text and formatting.
The calendar icon assigns one day to a note; click it again to change or remove the day.
The square button beside Archive opens the calendar. Click a circled day to open its notes.
Type a year directly in either calendar. Arrow/Page/Home/End keys navigate day lists; Enter opens and Esc returns.

Notes initially save in the notes folder beside the EXE.
The Archive button beside search can move the archive to an empty folder,
or open/create another archive while leaving the previous one intact.
The chosen folder becomes the default; archive.path stores its location.
Keep portable.flag beside the EXE to retain portable mode.
Back up the entire archive, including notes.ini and RTF formatting files.

https://github.com/GarauGarau/Jotila
'@
[IO.File]::WriteAllText((Join-Path $stage 'README.txt'), $readme, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $stage 'portable.flag'), '')
$zip = Join-Path $dist 'Jotila-portable-x64.zip'
Compress-Archive -LiteralPath (Join-Path $stage 'Jotila.exe'),(Join-Path $stage 'README.txt'),(Join-Path $stage 'LICENSE'),(Join-Path $stage 'portable.flag') -DestinationPath $zip -Force
$assets = @('Jotila.exe','Jotila-Setup-x64.exe','Jotila-portable-x64.zip')
$lines = foreach ($name in $assets) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $dist $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $name"
}
[IO.File]::WriteAllLines((Join-Path $dist 'SHA256SUMS.txt'), $lines, [Text.UTF8Encoding]::new($false))
Get-Item -LiteralPath ($assets | ForEach-Object { Join-Path $dist $_ }) | Select-Object Name,Length
