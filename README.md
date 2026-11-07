# ![icon](assets/picoslip.ico) PicoSlip

Tiny sticky notes for Windows. **Pure C, no dependencies.**

**[Download installer](https://github.com/GarauGarau/picoslip/releases/latest/download/PicoSlip-Setup-x64.exe)** · [Portable ZIP](https://github.com/GarauGarau/picoslip/releases/latest/download/PicoSlip-portable-x64.zip)

Windows 10/11, x64. Installs for your account without administrator rights.

![Formatting, checklists and a collapsed note](docs/screenshots/notes.png)

## Use

- **Ctrl + Alt + +** creates a note. The numpad `+` works too.
- Drag the top bar to move it; double-click it to edit the title. **−** collapses it; **□** expands it; **×** closes it.
- Drag edges or corners to resize; **Ctrl + mouse wheel** scales the current proportions.
- **Ctrl + B/I/U** toggles bold/italic/underline; **Ctrl + Shift + X** toggles strikethrough.
- `*` + **Shift + Enter** starts checkboxes. **Enter** adds items; **Backspace** on an empty item ends the list. Click a box to check it and strike its text.
- First line: `#Title` + **Enter** makes a title. **Shift + Enter** keeps it as text.
- **Ctrl + T**, or **Tags** in the title-bar right-click menu, edits tags. Separate names with `;`; **Down/Up + Enter** chooses an existing tag, ranked by how many notes use it. Tags are Unicode, case-insensitive and deduplicated.
- **Ctrl + Shift + F** opens global search; **Ctrl + F** focuses the archive search bar. Plain words search both note text and tags; `#work` requires that exact tag, `#"project alpha"` selects a tag with spaces, and `"release plan"` searches a phrase. Combine terms with spaces to require all of them. Text search ignores case and accents; exact tag names retain accents.
- The archive sidebar switches between **Tags** (all tag names with note counts) and **Date** (year/month/day). Each view remembers its selection, and the selected filter combines with search text. Tags also appear without `#` in a separate card footer marked by a black vertical line; click a name to select its sidebar filter. **Ctrl + F** focuses search from anywhere in the archive.
- `-#` finds notes without tags; **Esc** or **×** clears the query. Matches include the entire saved TXT, with highlighted excerpts beyond the ordinary preview. **F5** refreshes external file edits.
- **Archive**, beside the compact search field, changes the notes folder. **Move current archive** transfers notes, formatting and tags to an empty folder. **Open or create archive** uses another folder and leaves the previous archive intact. Either choice becomes the default for future starts.
- Right-click the top bar for options:

| Option | What it does |
|---|---|
| Open notes archive | Opens the saved TXT files |
| Browse notes | Live monospaced post-it mosaic by year/month/day; click to reopen, Ctrl/Shift-select and right-click to delete |
| Opacity | 100%, 90%, 80%, 70% or 60% |
| Edit title | Change it, or leave it empty to remove it |
| Note size | Small, medium, large, extra large or square |
| New note palette | Pastel, black and white, or yellow only |
| Tags | Assign or remove tags, with suggestions from open and closed notes |

<p align="center">
  <img src="docs/screenshots/notes-demo.gif" width="49%" alt="Post-its: writing, formatting, checklists and border resizing">
  <img src="docs/screenshots/browser-demo.gif" width="49%" alt="Archive: tray minimize, archive locations, tags, search, date filters, reopening, live previews and multiple deletion">
</p>

![The three palettes](docs/screenshots/palettes.png)

Notes stay on top, with a subtle shadow and crisp **1-pixel black outlines** around the note and its title bar, also when collapsed. Archive previews keep their soft edges, without the black note border. Text never scrolls; a smaller size is refused if the text would not fit.

Minimizing the archive hides it in the **notification area beside the clock**. Double-click PicoSlip's tray icon, or right-click → **Browse notes**, to restore it with its query, filters, selection and window placement. Single-click creates a note after the Windows double-click interval; the right-click menu also offers **New note** and **Exit**.

## Your notes

Text saves automatically as UTF-8, initially in `%LOCALAPPDATA%\PicoSlip\notes`:

```text
note-2026-11-04_08-35-27-123-000.txt
```

Names sort by local creation date and time. Empty notes are not saved. Closing a nonempty note keeps its file. Open notes return on restart. Uninstalling keeps the archive.

The portable ZIP initially keeps `notes` beside the EXE. Back up the whole archive folder, including `notes.ini` and the RTF formatting companions.

Choosing another location writes its absolute path as UTF-8 in `%LOCALAPPDATA%\PicoSlip\archive.path`, or beside the portable EXE. A move copies and flushes all note files before saving this setting and removing the originals. A failed copy or setting update leaves the previous archive active; existing destination files are never overwritten. Unrelated files remain in the old folder. If a configured drive is unavailable at startup, PicoSlip reports it and keeps the saved location instead of silently creating an empty archive elsewhere. Reconnect the drive, correct `archive.path`, or remove that setting to return to the initial location.

Tags live in each note's `notes.ini` section. The first tag save upgrades this file atomically to Unicode; existing layout settings are preserved. Changing tags does not rewrite TXT or RTF files. Empty notes remain unsaved until they have a title or body.

Layout changes write one key or one complete section, preserving tags and unfamiliar keys. Reopening an unchanged open note does not rewrite its metadata; reopening a closed note updates only its open flag. Formatting-only changes, including undo/redo, update just the RTF companion. Title-only changes update TXT and any changed title flag. Body changes still save both representations. Blank-note creation keeps the next palette color in memory until a save-all operation, archive switch or application exit.

## Resources

Version **1.2.0**: about **258 KiB** for the app, **408 KiB** for the installer. Measurements with 1–10 default notes: **2.47–2.59 MiB private RAM**, **17.32–21.47 MiB working set**, **0–0.16% of one CPU core at idle** (10-second samples, Windows 11 x64; UI activity can affect short samples). Run `build.cmd profile-only` with PicoSlip closed to measure the current release in an isolated portable archive.

## Build

MSVC + Windows SDK: `build.cmd` builds the app and installer; `build.cmd test` runs the tests (`--interactive` adds real keyboard/mouse input). All application, installer, test and profiling code is **C17**, compiled with `/TC /W4 /WX /O2`. Windows provides the native UI and rich text editor; no third-party runtime is bundled. Release binaries are unsigned.

`build.cmd test-checked` runs the suite with MSVC runtime checks; `build.cmd analyze` checks all application and installer sources with the native static analyzer. Build products stay in `build/`; distributable files stay in `dist/`. PowerShell scripts only orchestrate documentation capture, packaging and installation checks.

`build.cmd storage-profile` measures blank-note creation, first save, body/style/title edits and reopening with hidden native editors. It uses isolated synthetic INI files containing 100, 1,000 and 10,000 sections, with ten samples per operation, and reports elapsed time, metadata API calls, explicit content flushes and direct content-write bytes. The synthetic metadata sections do not require corresponding TXT files. Integration checks also compare the search toolbar's pixels after shrinking, growing, maximizing, restoring and rapid resize bursts against a full repaint, with empty and Unicode queries.

`build.cmd test-only --location-only` covers archive switching and migration, Unicode paths, permanent defaults, formatting and metadata preservation, locked-file rollback, unavailable drives and native chooser cancellation.

`build.cmd test-only --search-only` runs the tag/search integration checks. `build.cmd search-profile` measures 100, 1,000 and 10,000 isolated 2 KiB notes. Search uses a single on-demand worker, streaming UTF-8 decoding and precompiled KMP terms, with no persistent content index. Exact tag filters run before text reads; changed notes are reevaluated individually. Metadata is cached for the archive session and reloaded when changed or explicitly refreshed. Queries support up to 16 terms; each note can have 24 tags of up to 63 UTF-16 code units.

Live result updates reuse their reserved storage; body-only saves reuse the tag catalog; selection snapshots query only selected/focused cards. Short-file searches avoid redundant EOF reads. A local Windows 11 x64 sample on Ryzen AI 9 HX PRO 370 measured **692 ms** for a warm full-text miss across 10,000 × 2 KiB notes (previously 747 ms), **7 ms** for a tag-only query and **112 ms** for a tag-plus-text query. Means of three warm runs; filesystem cache and machine activity affect timing. Tag-only searches read **zero TXT bytes**.

## Documentation and folders

- `src/`: application, archive, search, tags, archive locations, installer and Windows resources.
- `tests/`: integration tests and native demonstration capture, using isolated temporary archives.
- `tools/`: C profiling/icon tools and build-support scripts.
- `assets/`, `docs/screenshots/`: icon and the four published visual examples.
- `build/`, `dist/`: ignored generated files; `backups/` holds any pre-existing local recovery copies and is also excluded from Git.

Run `tools/capture.ps1` with ImageMagick installed to regenerate both PNGs and both GIFs; `-NotesOnly` or `-BrowserOnly` refreshes one walkthrough. Each uses a different local painting from `Downloads/StillTyping/Selection`; pass `-BackgroundRoot` to select another copy of that collection. No backgrounds are downloaded and the desktop wallpaper is unchanged. Temporary frames are removed after successful conversion.

Backgrounds from the Birmingham Museums Trust collection in StillTyping: **Night with her Train of Stars** (`notes.png`), **0ZbwwI6VV3E** (`palettes.png`), **February Fill Dyke** (`notes-demo.gif`), **Walton-on-the-Naze** (`browser-demo.gif`). The exact source filenames are listed in `tools/capture.ps1`.
