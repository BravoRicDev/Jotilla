# Performance review: 1.7.0 → 1.7.1

Measured locally on Windows x64, 2026-11-08, using the same MSVC C17 `/O2 /MT` configuration. Baseline: `7baeb4b`. All 12 production C modules were reviewed, including persistence, search, archive migration, palettes and Setup. Runtime changes are confined to calendar rendering, date arithmetic, archive notifications, tag catalog construction and mouse tracking.

The calendar previously erased its visible background and redrew every month for each hovered day. It now composes damaged cells in a reusable GDI bitmap and presents them together. Mouse tracking registers once per entry; unchanged day counts do not repaint. Ordinary note browsing creates no calendar window until requested.

| 1,000 pointer moves between two adjacent January days | 1.7.0 | 1.7.1 | Reduction |
|---|---:|---:|---:|
| Annual view: text drawing calls | 462,000 | 1,999 | 99.57% |
| Month picker: text drawing calls | 39,000 | 1,999 | 94.87% |
| Annual view: invalidated pixels | 538,850,000 | 1,351,324 | 99.75% |
| Month picker: invalidated pixels | 167,200,000 | 2,886,556 | 98.27% |
| Mouse tracking registrations, either view | 1,000 | 1 | 99.9% |

Workload: year 2028, 125% DPI, annual window 850 × 650 physical pixels; picker 400 × 418. `UpdateWindow` and `GdiFlush` complete every move. One observed run took 21,839 → 179 ms annually and 1,855 → 180 ms in the picker; these are workload timings, not general application speedups. Hover creates **zero bitmaps** after warmup and retains **zero additional GDI objects**. The buffer costs approximately **2.06 MiB** for this annual view and **0.64 MiB** for this picker, allocated on painting and reused until resize/close.

| Catalog: 10,000 notes × 24 assignments | Before, mean ms | After, mean ms |
|---|---:|---:|
| 24 shared names | 48.90 | 30.33 |
| 240,000 distinct names, ordered | 47.18 | 23.62 |
| 240,000 distinct names, reverse order | 54.21 | 23.99 |
| 240,000 distinct names, shuffled | 129.34 | 108.98 |

Five alternating runs per version, five catalog builds per run. Sorting borrowed spans copies each distinct name only once; monotone inputs bypass sorting. With 24 shared names, the largest requested working vector falls from **33 MiB to 4 MiB (−87.88%)**. With all names distinct it falls from 33 MiB to 30.21 MiB. These are allocation sizes, not total process RAM or the allocator's transient peak.

A successful date-only update now rereads metadata **0 times instead of 1**, retaining previews and search results. New notes and F5 still refresh the authoritative snapshot. Gregorian ordinals use a constant-time lookup rather than summing preceding months.

Search and storage workloads retain their I/O counts. At 10,000 notes: tag-only search reads zero TXT files; full-text search reads 10,000 files / 20,480,000 bytes; tag-plus-text search reads 1,000 / 2,048,000. Clean saves still make zero metadata calls, 50 typed characters allocate zero buffers, and 100 note repaints create zero graphics objects. Atomic replacement and file flushes remain intact.

The EXE grows from **326,656 to 330,240 bytes (+3,584; 1.10%)**. Its nine directly imported Windows DLLs are unchanged; Rich Edit remains a system component and the CRT is statically linked. There are no third-party runtime libraries or additional timers/workers.

Validation includes the full integration suite, checked runtime, MSVC static analysis, **146,097 consecutive Gregorian dates**, exact catalogs with 24,000 distinct names, and **1,000 pixel comparisons** across 100/125/150/200% DPI. Rendering checks cover hover/leave, focus, counts, scrolling, resize, year changes, midnight and bitmap-allocation recovery; 30 open/close cycles retain no GDI objects. Calendar checks also cover day-list arrows/Page/Home/End/Enter/Escape, search filtering, reassignment and persistence.

Reproduce with `build.cmd calendar-profile`, `tags-profile`, `search-profile`, `storage-profile`; validate with `test`, `test-checked`, `analyze`. Timings vary with disk cache, scheduling and machine.
