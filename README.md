# <img src="assets/jotila.png" width="36" height="36" alt="Greek iota on a yellow note"> Jotila - Sticky notes, on silicon

Small, native notes with autosave, an archive and a calendar. No extra dependencies.

**[Visit the Jotila website](https://garaugarau.github.io/Jotila/)**

## Download

| System | Installer | Portable · no installation |
|---|---|---|
| Windows 10/11 · x64 | [Setup (.exe)](https://github.com/GarauGarau/Jotila/releases/download/v1.8.2/Jotila-Setup-x64.exe) | [Portable ZIP](https://github.com/GarauGarau/Jotila/releases/download/v1.8.2/Jotila-portable-x64.zip) |
| macOS 13+ · Apple Silicon / Intel · preview | [Setup (.pkg)](https://github.com/GarauGarau/Jotila/releases/download/macos-v0.1.2/Jotila-Setup-universal.pkg) | [Portable ZIP](https://github.com/GarauGarau/Jotila/releases/download/macos-v0.1.2/Jotila-portable-universal.zip) |

Run the installer, or extract the portable ZIP and launch Jotila. For the Mac portable app, select the included `Notes.jotilaarchive` folder when prompted.

[Release notes and checksums](https://github.com/GarauGarau/Jotila/releases). The macOS preview is not notarized.

![Formatting, checklists and a collapsed note](docs/screenshots/notes.png)

| Action | Windows | macOS |
|---|---|---|
| New note (global) | **Ctrl + Alt + +** | **Ctrl + Option + Cmd + N** |
| Open note explorer | **Ctrl + E** | **Cmd + E** |
| Search notes and tags | **Ctrl + Shift + F** | **Cmd + F** |
| Turn `#Title` into a title | **Shift + Enter** | **Shift + Enter** |
| Start a checklist after `*` | **Shift + Enter**; **Enter** continues it | **Shift + Enter**; **Enter** continues it |
| Bold, italic, underline | **Ctrl + B / I / U** | **Cmd + B / I / U** |
| Strikethrough | **Ctrl + Shift + X** | **Cmd + Shift + X** |
| Undo / redo | **Ctrl + Z / Y** | **Cmd + Z / Cmd + Shift + Z** |
| Resize note, or selected text | **Ctrl + + / −**, **Ctrl + wheel** or pinch | **Cmd + + / −**, **Cmd + wheel** or pinch |
| Edit tags | **Ctrl + T** | **Cmd + T** |

Drag the bar to move; double-click its title to rename. **− / □** folds or expands; **×** closes and keeps the note in the archive. Resizing preserves visible content.

Open notes return to their saved positions, sizes and folded state, with their title bars kept reachable if a monitor is disconnected.

Click the note's **calendar icon** to assign, change or remove its day. The square calendar button beside **Archive** switches to the year view: circled days open a note, or a compact title list when several share the day. Type a year directly in either calendar. In day lists, **↑ / ↓**, **Page Up / Down**, **Home / End** navigate, **Enter** opens and **Esc** returns. Search also filters calendar notes.

Right-click the text for [80 color palettes](docs/palettes.md); select text first to color just that selection. **← / →** browse, click applies, **★** applies a favorite to this note and new notes, including after restart. Right-click the bar for more options.

Search by text or tags: `#work`, `"release plan"`, or `-#` for untagged notes. **F5** reloads external edits. Double-click the tray or menu bar icon to open the explorer, single-click for a new note.

| Notes and shortcuts | Archive and calendar |
|---|---|
| ![Note editing walkthrough](docs/screenshots/notes-demo.gif) | ![Archive and calendar walkthrough](docs/screenshots/browser-demo.gif) |

Notes save as UTF-8 text in the archive folder. Empty notes stay unsaved. **Archive** moves or switches folders; back up the whole folder, including `notes.ini` and RTF files. Updates and uninstall preserve your notes.

## Source and builds

| Platform | Branch | Build, installation and data |
|---|---|---|
| Windows | [`windows`](https://github.com/GarauGarau/Jotila/tree/windows) · default | [Windows guide](docs/windows.md) |
| macOS | [`macos`](https://github.com/GarauGarau/Jotila/tree/macos) | [macOS guide](https://github.com/GarauGarau/Jotila/blob/macos/docs/macos.md) · [Feature coverage and validation](https://github.com/GarauGarau/Jotila/blob/macos/docs/macos-validation.md) |

The website lives on its own [`gh-pages` branch](https://github.com/GarauGarau/Jotila/tree/gh-pages), with [source and publishing instructions](https://github.com/GarauGarau/Jotila/blob/gh-pages/README.md).

[Performance measurements](docs/performance.md).

`tools/capture.ps1` regenerates the real-app screenshots and GIFs using ImageMagick and local artwork (`-BackgroundRoot`). Backgrounds: Birmingham Museums Trust — *Sundown, Loch Rannoch*, *February Fill Dyke* and *Walton-on-the-Naze*.
