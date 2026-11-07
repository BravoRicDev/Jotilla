#ifndef IOTA_ARCHIVE_H
#define IOTA_ARCHIVE_H

#include <windows.h>

typedef struct ArchiveActions {
    BOOL (*save)(void);
    BOOL (*open)(const wchar_t *id);
    BOOL (*remove)(const wchar_t *id);
    void (*location)(HWND owner);
} ArchiveActions;

HWND archive_show(HWND owner, const wchar_t *directory, const wchar_t *settings, const ArchiveActions *actions);
void archive_changed(const wchar_t *id, BOOL present, BOOL tags_changed);
void archive_close(void);
void archive_search(void);

#endif
