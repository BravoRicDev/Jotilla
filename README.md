# ![icon](assets/picoslip.ico) PicoSlip

PicoSlip is a small Windows app for jotting down notes on your desktop. Notes are saved and archived automatically, so you can find, reopen and update them later. Sticky notes, on silicon. :)

**[Download installer](https://github.com/GarauGarau/picoslip/releases/latest/download/PicoSlip-Setup-x64.exe)** · [Portable ZIP](https://github.com/GarauGarau/picoslip/releases/latest/download/PicoSlip-portable-x64.zip)

Windows 10/11, x64. No administrator rights or extra dependencies required.

![Formatting, checklists and a collapsed note](docs/screenshots/notes.png)

## Everyday use

- **Ctrl + Alt + +** creates a note; the numpad `+` works too.
- Drag the title bar to move a note, double-click to rename it, or use **− / □** to collapse and expand it. **×** closes the note and keeps it in the archive.
- Drag an edge or corner to resize, or use **Ctrl + mouse wheel**. Notes stay on top and prevent resizing below what the text needs.
- Right-click the title bar for the archive, tags, opacity, preset sizes and palettes for new notes.

| Action | Shortcut |
|---|---|
| Bold, italic, underline | **Ctrl + B / I / U** |
| Strikethrough | **Ctrl + Shift + X** |
| Start a checklist | Type `*`, then **Shift + Enter** |
| Add or finish checklist items | **Enter** adds an item; **Backspace** on an empty item ends the list |
| Turn the first line into a title | Type `#Title`, then **Enter** |
| Edit tags | **Ctrl + T**; separate tags with `;` |
| Search all notes | **Ctrl + Shift + F** |

Click a checkbox to mark a task complete. In the archive, search by text or tags, filter by date, and click a note to reopen it. Use `#work` for an exact tag, `"release plan"` for a phrase, or `-#` for untagged notes. **Ctrl + F** focuses search; **F5** refreshes external edits.

Minimizing the archive sends it to the notification area. Double-click the tray icon to restore it; a single click creates a note.

<p align="center">
  <img src="docs/screenshots/notes-demo.gif" width="49%" alt="Writing, formatting, checklists and resizing">
  <img src="docs/screenshots/browser-demo.gif" width="49%" alt="Searching, filtering and reopening archived notes">
</p>

![Pastel, black and white, and yellow palettes](docs/screenshots/palettes.png)

## Your notes

Notes save automatically as UTF-8 text files in `%LOCALAPPDATA%\PicoSlip\notes`, or in `notes` beside the portable EXE. Empty notes are not saved. Open notes return on restart, and uninstalling leaves your archive intact.

Use **Archive** in the archive window to move your notes or open another archive. Back up the whole folder, including `notes.ini` and the RTF files that preserve formatting.

## Development

Written in **C17** using the native Windows UI and rich text editor. Build with MSVC and the Windows SDK:

```bat
build.cmd                 rem Build the app and installer
build.cmd test            rem Run integration tests
build.cmd test-checked    rem Run tests with runtime checks
build.cmd analyze         rem Run static analysis
```

Sources live in `src/`, tests in `tests/`, and supporting tools in `tools/`. Generated files go to `build/` and `dist/`. Release binaries are unsigned.

With ImageMagick installed, `tools/capture.ps1` regenerates the screenshots and demos using local artwork; `-OverviewOnly` refreshes just the first screenshot. Pass `-BackgroundRoot` to choose the artwork folder. Backgrounds are from the Birmingham Museums Trust collection.
