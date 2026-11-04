#ifndef PICOSLIP_ARCHIVE_H
#define PICOSLIP_ARCHIVE_H

#include <windows.h>

typedef struct ArchiveActions {
    BOOL (*save)(void);
    BOOL (*open)(const wchar_t *id);
    BOOL (*remove)(const wchar_t *id);
} ArchiveActions;

HWND archive_show(HWND owner, const wchar_t *directory, const wchar_t *settings, const ArchiveActions *actions);
void archive_close(void);

#endif
