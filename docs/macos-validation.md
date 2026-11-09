# macOS feature coverage and validation

Validated locally on 2026-11-09: Apple Silicon, macOS 15.3.1, Apple Command Line
Tools with the macOS 15.5 SDK, deployment target macOS 13. This report distinguishes
implemented behavior and automated evidence from environment-dependent release
checks. It does not certify identical behavior on untested Macs.

## Feature coverage

The reference is `src/jotila.c`, `src/archive.c`, `src/calendar.c`,
`src/palette_picker.c`, the bundled palette catalog and the Windows documentation.
The Windows implementation is unchanged by this work.

| Area | Native implementation | Verification |
| --- | --- | --- |
| Notes | Bordered cards, black bar divider, calendar/fold/close controls, monospace editing, palette contrast, opacity, drag and double-click rename | Real AppKit rendering, focus and dialog tests |
| Lifecycle | New note, close into archive, reopen, session state, blank note suppression, cleared notes removed with Trash recovery | Title-only and body-only saves, whitespace-only titles/bodies, legacy empty TXT/BOM/title headers, UTF-8 chunk boundaries, clear/recreate and close/reopen |
| Text | Bold, italic, underline, strikethrough, insertion styles, plain paste, Unicode and RTF | Attribute undo/redo, native pasteboard round trip, RTF/TXT tests |
| Titles and lists | Atomic title-promotion undo; checklist creation, continuation, exit, Backspace, checkbox and paragraph strikethrough | Native editor commands and undo assertions |
| Sizing | Five presets, keyboard zoom, accumulated wheel/pinch input, selection font sizing, collapsed-note dimensions, content-fit checks | Native layout, rejected overflow, minimum size and hidden-selection tests |
| Palettes | All 80 Popular palettes / 400 wedges, three legacy defaults, foreground/background modes, favorites, session palettes and shuffled color bags | Catalog checks, every wedge hit tested, keyboard navigation, favorite/foreground persistence |
| Palette presentation | Cached vector paths, contrasting selection rim, shadow, 120 ms transitions, Reduce Motion, owner-bound popup, accessible colors | Native screenshots, popup navigation and accessibility checks |
| Explorer | Virtualized colored cards, visible-only previews, creation-date/tag sidebar, clickable tags, search excerpts/highlighting, multi-selection, Trash | First-open/reopen regression, card visibility, search and deletion tests |
| Tags | Native token field, normalization, bounded suggestions from current archive | Dialog acceptance/cancellation and normalization tests |
| Calendar | Annual layout, circles/counts, typed year, native month/day picker, same-day title list, keyboard and accessibility | Leap-year layout, assignment, list navigation, accessibility and resizing tests |
| Archive | Open/create, move to an empty separate destination, verified copies, atomic remembered path, independent portable identity | Migration, locking, nested-path rejection and byte comparison tests |
| Persistence | Windows-compatible title headers, CRLF TXT, UTF-16LE INI, RTF matching, unknown keys, stale-write rejection | Storage/conflict tests and malformed UTF-8 regression |
| Native integration | Template vector menu-bar icon, native calendar symbol, menu bar single/double/right click, Command shortcuts, Carbon global new-note shortcuts, ServiceManagement login item, Spaces and display recovery | Actual icon rendering and calendar toggle, menu dispatch and synthetic off-screen geometry tests; physical/session checks below |
| Packaging | ARM64 and x86_64 builds, universal ZIP and PKG, ad hoc signatures and checksums | Local cross-compilation, signature verification and package creation |

Native controls and system font rasterization follow macOS. The note/card/wheel
visual model and palette values are shared with the Windows design; screenshots
are evidence of the Mac rendering, not a claim of pixel identity between OSes.

## Reproduce

```sh
make
make test
make ui-test
make smoke-test
make analyze
make profile
make package
```

`make test` passes **292,446 checks**, including the full Gregorian cycle. Most
of that count is date arithmetic; it is not a count of distinct UI scenarios.
`make ui-test` passes **1,086 checks** covering actual AppKit windows, menus,
dialogs, editing, rendering, calendar cells and palette wedges. `make profile` adds nine large-archive
assertions to the UI suite. The test sources enumerate the individual assertions.

AddressSanitizer and UndefinedBehaviorSanitizer were also run against both suites:

```sh
make OUT=build/macos/checked \
  COMMON='-arch arm64 -mmacosx-version-min=13.0 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror -Isrc' \
  test ui-test
```

The UI suite creates isolated archives and uses a private pasteboard. It captures
its own windows in `build/macos/ui-snapshots` (with AppKit view rendering as a
fallback). The current preview archive is not used as test data.

For a manual trial, quit any running Jotila instance, then use:

```sh
make preview
```

This uses the repository's ignored `notes/macOS-preview.jotilaarchive`. `make run`
uses the normal installed-app data location instead. Do not confuse the two
archives when checking whether previously created notes are present.

## Performance and ownership

A representative run with 10,000 generated notes measured:

| Operation | Observed |
| --- | ---: |
| Load archive snapshot, including empty-file detection | 311.9 ms |
| Initial cards, including an intentional 250 ms event pump | 291.3 ms |
| Initial preview loads | 6 |
| Tag-only filter, 2,000 matches | 60.9 ms |
| Full-text search, 10 matches across 10,000 files | 420.9 ms |
| Save a body edit with 10,000 records | 1.7 ms |

These are local workload measurements, not latency guarantees or cold-disk
benchmarks. The fixture has small notes and runs on the local filesystem.

The body-edit assertion verifies that `notes.ini` is not replaced and the
explorer's search generation does not change. Archive loading skips zero-byte
files using file metadata and checks other TXT files in 256-byte chunks, stopping
at the first non-whitespace character. This detects legacy blank notes without
loading complete bodies or RTF. Invalid, unreadable and oversized files remain
visible so opening them can report the problem. Original blank files are retained.
Search/preview operations use immutable snapshots. Existing cards are updated by ID;
query/filter/catalog changes rebuild the filtered snapshot. Preview keys include
an archive epoch and record revision, preventing stale cross-archive callbacks.
Preview memory is bounded to 256 entries / 1 MiB, reads to 2 KiB per requested
card, and search chunks to 8 KiB with bounded overlap and excerpts. Search and
preview queues are serial and cancellable. Idle notes have no repeating save
timer; idle palettes have no animation timer. Calendar hover repaints affected
cells and uses cached labels and constant-time hit testing.

The filesystem layer retains atomic replacements and checks outside changes
before overwriting. Archive transfer verifies copies before changing ownership;
source cleanup compares bytes again and retains changed originals. The system
Trash is used for explicit deletion and cleared saved notes.

## Environment-dependent checks

The following require a separate session or hardware and are not certified by
this run:

- Physical display disconnection/reconnection, mixed display scale factors,
  full-screen Spaces and Mission Control interactions. Synthetic off-screen and
  negative-coordinate constraints are covered.
- Every hardware keyboard layout, physical pinch gesture and input method.
  Native Command dispatch, text commands and private-pasteboard behavior are
  covered; global shortcut registration is exercised at application startup.
- A real sign-out/login with the app installed under `/Applications`, including
  the macOS background-item approval flow. The portable preview intentionally
  cannot register itself for login.
- Execution on an Intel Mac and older supported macOS releases. The Intel binary
  is cross-compiled here, not executed on Intel hardware.
- Clean-machine PKG installation, upgrade/uninstall, disk-full/unplugged-volume
  faults, Developer ID signing and Apple notarization. Local package construction
  and ad hoc signature verification do not establish those properties.
