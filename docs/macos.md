# Native macOS preview

This branch contains a local, unpublished macOS implementation. It is not a
release: it was authored on Windows, where AppKit cannot be compiled or run.
Native compilation, tests, desktop behavior, performance and package installation
still require validation on a Mac. Do not use the only copy of an archive for
the first trial.

## Build on a Mac

Target: macOS 13 or later, Intel and Apple Silicon. Install Apple's Command Line
Tools (`xcode-select --install`), then from the repository root:

```sh
make
make test
make smoke-test
make analyze
make run
```

`make` builds for the current CPU. No third-party runtime libraries, package
manager or Xcode project are needed. The build uses C17, Objective-C with ARC,
AppKit, Carbon hotkeys and ServiceManagement. Warnings are errors. `make test`
needs a logged-in graphical Mac session; it uses an isolated temporary archive.
The smoke test launches the real executable with an isolated archive and exits.
It does not register login startup or use the installed app's settings.

```sh
make package
```

This builds both architectures and produces local downloads in `dist/macos`:

- `Jotila-portable-universal.zip`
- `Jotila-Setup-universal.pkg`
- `SHA256SUMS.txt`

The default app signature is ad hoc, for development. For Developer ID signing:

```sh
make package SIGN_IDENTITY='Developer ID Application: …' \
  INSTALLER_IDENTITY='Developer ID Installer: …'
```

Packaging never uploads or publishes anything. Public distribution also needs
Apple notarization and validation on a clean Mac. The PKG installs only
`/Applications/Jotila.app`; there are no install scripts that touch notes.
Disable Start at login before removing the installed app. Moving the app to
Trash leaves its archive intact.

## Portable and installed data

Installed default: `~/Library/Application Support/Jotila/notes`. An explicitly
chosen archive is remembered in `~/Library/Application Support/Jotila/archive.path`.
An unavailable remembered folder stops startup instead of creating a replacement.

The portable bundle has its own application identity and asks for the archive
directory each launch. Select the included `Notes.jotilaarchive`, an ordinary
folder displayed as a package by Finder, or another existing folder. It writes
no archive location into the user's preferences and cannot enable Start at login.
This avoids relying on relative paths during macOS App Translocation. The OS may
still maintain its normal recent-file/security records; portable does not mean
the operating system leaves no traces.

An explicit path can also be supplied:

```sh
build/macos/arm64/Jotila.app/Contents/MacOS/Jotila \
  --portable --archive '/Volumes/USB/Notes.jotilaarchive'
```

Use `x86_64` instead of `arm64` on Intel. Keep data outside the signed `.app`.
Quit before moving the archive. Opening another archive preserves the previous
one; automatic archive migration/moving has not yet been ported.

## Architecture and persistence

`src/date.c`, `src/colors.c` and the immutable palette catalog are shared C.
The Windows side of `colors.h` retains its original types and operations; the
Mac side preserves COLORREF byte ordering without importing Windows headers.
The AppKit frontend is under `src/macos`:

- `Store`: bounded metadata decoding, archive lock, TXT/RTF and atomic replacement.
- `Note`: native editor, note window, formatting and debounced persistence.
- `Search`: bounded streaming UTF-8 search with cancellation and tag prefiltering.
- `Browser`: archive table, search queue and calendar.
- `App`: lifecycle, menus, archive switching, hotkey and login item integration.

TXT remains authoritative UTF-8. Explicit titles retain the Windows `#title`
header, CRLF output and `has_title` metadata. RTF is accepted only if its text
matches TXT after newline normalization. Metadata writes use UTF-16LE with a BOM
and preserve unknown sections and keys (comments/order are not preserved).
Legacy ANSI metadata uses CP1252 as a fallback; other legacy Windows code pages
need conversion to Unicode on Windows before import.

Each write uses a sibling temporary file, flush and rename. RTF is replaced
before TXT so an interrupted pair cannot resurrect an old body. This is not a
transaction across all archive files. A failed save keeps the note open. External
TXT changes and metadata replacement block stale writes; export an RTF copy of
unsaved edits, then use Reload and explicitly discard them if necessary. The
archive lock coordinates Mac instances; it is not a synchronization protocol
with Windows, cloud clients or other editors.

Clean saves do no work. Window geometry changes write metadata only. Formatting
changes write RTF without rewriting TXT. Search uses one cancellable serial
operation queue, 8 KiB input chunks and bounded overlap; it retains no full note
bodies. Only a visible explorer requests searches. Results include bounded text
previews. The pure tag matcher reads no TXT; the explorer separately reads
previews for displayed search results. A large-archive benchmark is still pending.

Mac geometry uses `mac_x/y/width/height` keys so it does not overwrite Windows
coordinates. Note state, palette indices, tags and calendar dates are shared.

## Preview controls and remaining validation

Global new note: **Control+Option+Command+N**. Inside the app: **Command+N** new
note, **Command+E** explorer, **Command+F** search, **Command+R** reload,
**Command+T** tags, **Command+D** calendar day. Standard Command shortcuts handle
clipboard and undo. Command+B/I/U and Command+Shift+X format selected text.
Command+plus/minus, Command+wheel and pinch resize the selection or the note.
Shift+Enter promotes a first-line `#title` or starts a `*` checklist; Enter
continues checklist items. Click a checkbox to toggle it.

Drag the bar to move; double-click to rename; minus folds, close archives.
The context menu exposes all palettes, foreground colors, favorites and opacity.
The explorer has a yearly calendar and date-filtered note list. Closing the
explorer hides it; the menu bar item brings it back. Start at login is available
only for the installed app in `/Applications`.

This preview has not reached Windows feature parity. Native Mac verification
must cover RTF round trips (fonts, colors, sizes, emphasis and Unicode), undo of
mixed editing/formatting, title promotion, input methods, clipboard, focus,
multi-monitor recovery, login, low-disk failures, archive switching, installation
and upgrade. Current differences to address before release:

- Palette selection uses native menus; the animated wheel is not ported.
- Tag suggestions/sidebar and automatic archive moving are not ported.
- The editor currently scrolls overflowing text; Windows' content-fit resize
  constraint needs a native layout implementation and tests.
- Cleared saved notes remain recoverable as empty notes rather than being
  automatically removed; new blank notes remain unsaved.
- Calendar accessibility and full keyboard navigation need a native UI pass.
- Global hotkeys, title-promotion undo and foreground-color undo need desktop
  testing; no claims of identical behavior are made yet.

## Keep the work unpublished

The `feature/macos-native` branch is local and has no upstream. GitHub visibility
is per repository: pushing it to the public origin would expose the code. A
remote backup would require a separate private repository. Do not change the
visibility of the existing public project to hide one branch.

For a private transfer without GitHub, use the local source ZIP or a Git bundle:

```sh
git bundle create Jotila-macos.bundle feature/macos-native
```

On the Mac:

```sh
git clone -b feature/macos-native Jotila-macos.bundle Jotila
```
