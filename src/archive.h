#ifndef JOTILA_ARCHIVE_H
#define JOTILA_ARCHIVE_H

#include <windows.h>

typedef struct ArchiveActions {
    BOOL (*save)(void);
    BOOL (*open)(const wchar_t *id);
    BOOL (*remove)(const wchar_t *id);
    void (*location)(HWND owner);
} ArchiveActions;

HWND archive_show(HWND owner, const wchar_t *directory, const wchar_t *settings, const ArchiveActions *actions);
enum { ARCHIVE_TAGS_CHANGED = 1, ARCHIVE_DATE_CHANGED = 2 };
void archive_changed(const wchar_t *id, BOOL present, unsigned changes);
void archive_close(void);
void archive_search(void);
void archive_notes(void);

#endif
