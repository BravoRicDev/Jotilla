# <img src="assets/jotila.png" width="36" height="36" alt="Greek iota on a yellow note"> Jotila - Sticky notes, on silicon

Small, native Windows notes with autosave, an archive and a calendar. **C17**, no extra dependencies. Windows 10/11, x64.

**[Installer](https://github.com/GarauGarau/Jotila/releases/latest/download/Jotila-Setup-x64.exe)** · [Portable ZIP](https://github.com/GarauGarau/Jotila/releases/latest/download/Jotila-portable-x64.zip) · [Releases](https://github.com/GarauGarau/Jotila/releases)

![Formatting, checklists and a collapsed note](docs/screenshots/notes.png)

| Action | Shortcut |
|---|---|
| New note | **Ctrl + Alt + +** |
| Open note explorer | **Ctrl + E** |
| Search notes and tags | **Ctrl + Shift + F** |
| Turn `#Title` into a title | **Shift + Enter** |
| Start a checklist after `*` | **Shift + Enter**; **Enter** continues it |
| Bold, italic, underline | **Ctrl + B / I / U** |
| Strikethrough | **Ctrl + Shift + X** |
| Undo / redo | **Ctrl + Z / Y** |
| Resize note, or selected text | **Ctrl + + / −**, **Ctrl + wheel** or touchpad pinch |
| Edit tags | **Ctrl + T** |

Drag the bar to move; double-click its title to rename. **− / □** folds or expands; **×** closes and keeps the note in the archive. Resizing preserves visible content.

Click the note's **calendar icon** to assign, change or remove its day. The square calendar button beside **Archive** switches to the year view: circled days open a note, or a compact title list when several share the day. Type a year directly in either calendar. In day lists, **↑ / ↓**, **Page Up / Down**, **Home / End** navigate, **Enter** opens and **Esc** returns. Search also filters calendar notes.

Right-click the text for [80 color palettes](docs/palettes.md); select text first to color just that selection. **← / →** browse, click applies, **★** applies a favorite to this note and new notes, including after restart. Right-click the bar for more options.

Search by text or tags: `#work`, `"release plan"`, or `-#` for untagged notes. **F5** reloads external edits. Minimizing the explorer hides it in the tray; double-click the tray icon to return, single-click for a new note.

| Notes and shortcuts | Archive and calendar |
|---|---|
| ![Note editing walkthrough](docs/screenshots/notes-demo.gif) | ![Archive and calendar walkthrough](docs/screenshots/browser-demo.gif) |

Notes save as UTF-8 text in `%LOCALAPPDATA%\Jotila\notes`, or beside the portable EXE. Empty notes stay unsaved. **Archive** moves or switches folders; back up the whole folder, including `notes.ini` and RTF files. Updates and uninstall preserve your notes.

Build with MSVC and the Windows SDK: `build.cmd`. Checks: `build.cmd test`, `build.cmd test-checked`, `build.cmd analyze`. [Performance measurements](docs/performance.md). Package downloads: `tools/package.ps1`. Binaries are unsigned.

`tools/capture.ps1` regenerates the real-app screenshots and GIFs using ImageMagick and local artwork (`-BackgroundRoot`). Backgrounds: Birmingham Museums Trust — *Sundown, Loch Rannoch*, *February Fill Dyke* and *Walton-on-the-Naze*.
