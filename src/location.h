#ifndef IOTA_LOCATION_H
#define IOTA_LOCATION_H
#include <windows.h>

#define LOCATION_CAP 1024

/* Fresh installs use IOTA; upgrades retain the existing archive in place and
   atomically remember its path. Explicit IOTA settings always take priority. */
BOOL location_user_paths(const wchar_t *local, wchar_t config[LOCATION_CAP], wchar_t fallback[LOCATION_CAP]);

/* archive.path is independent of per-archive notes.ini. Missing configuration
   uses the legacy default; an unavailable configured archive is never replaced. */
BOOL location_load(const wchar_t *config, const wchar_t *fallback, wchar_t path[LOCATION_CAP]);
/* Copies and flushes a move before committing the default; never overwrites a
   destination file. Failure preserves the current archive and configuration. */
BOOL location_change(const wchar_t *config, const wchar_t *source, const wchar_t *target,
                     BOOL move, wchar_t selected[LOCATION_CAP], BOOL *copies_left);
HRESULT location_pick(HWND owner, const wchar_t *current, BOOL move, wchar_t path[LOCATION_CAP]);
#endif
