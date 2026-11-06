#ifndef PICOSLIP_LOCATION_H
#define PICOSLIP_LOCATION_H
#include <windows.h>

#define LOCATION_CAP 1024

/* archive.path is independent of per-archive notes.ini. Missing configuration
   uses the legacy default; an unavailable configured archive is never replaced. */
BOOL location_load(const wchar_t *config, const wchar_t *fallback, wchar_t path[LOCATION_CAP]);
/* Copies and flushes a move before committing the default; never overwrites a
   destination file. Failure preserves the current archive and configuration. */
BOOL location_change(const wchar_t *config, const wchar_t *source, const wchar_t *target,
                     BOOL move, wchar_t selected[LOCATION_CAP], BOOL *copies_left);
HRESULT location_pick(HWND owner, const wchar_t *current, BOOL move, wchar_t path[LOCATION_CAP]);
#endif
