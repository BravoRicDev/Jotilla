#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "metadata.h"

BOOL metadata_next(MetadataCursor *cursor, wchar_t **key, wchar_t **value) {
    while (cursor->next && *cursor->next) {
        wchar_t *line = cursor->next, *end = line + wcscspn(line, L"\r\n"), *equal;
        cursor->next = end;
        while (*cursor->next == L'\r' || *cursor->next == L'\n') ++cursor->next;
        *end = 0;
        while (iswspace(*line)) ++line;
        while (end > line && iswspace(end[-1])) *--end = 0;
        if (*line == L'[') {
            wchar_t *close = wcschr(line, L']');
            cursor->section = NULL;
            if (close) { *close = 0; cursor->section = line + 1; }
        } else if (cursor->section && *line != L';' && (equal = wcschr(line, L'=')) != NULL) {
            *equal = 0; *key = line; *value = equal + 1;
            while (equal > line && iswspace(equal[-1])) *--equal = 0;
            while (iswspace(**value)) ++*value;
            return TRUE;
        }
    }
    return FALSE;
}
/* Read once, preserving every section/key during the first Unicode migration.
   Existing Jotilla metadata is ASCII; legacy ANSI files use the Windows ACP. */
BOOL metadata_read(const wchar_t *path, wchar_t **result, BOOL *unicode) {
    HANDLE file;
    LARGE_INTEGER size;
    char *bytes;
    wchar_t *text;
    DWORD read;
    int count, offset = 0;
    UINT encoding = CP_ACP;
    BOOL ok;
    *result = NULL; *unicode = FALSE;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) return FALSE;
        *result = (wchar_t *)calloc(1, sizeof(wchar_t)); return *result != NULL;
    }
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 64 * 1024 * 1024) {
        CloseHandle(file); return FALSE;
    }
    bytes = (char *)malloc((size_t)size.QuadPart + 2);
    if (!bytes) { CloseHandle(file); return FALSE; }
    ok = ReadFile(file, bytes, (DWORD)size.QuadPart, &read, NULL) && read == size.QuadPart;
    CloseHandle(file);
    if (!ok) { free(bytes); return FALSE; }
    if (read >= 2 && (BYTE)bytes[0] == 0xff && (BYTE)bytes[1] == 0xfe) {
        if (read & 1) { free(bytes); return FALSE; }
        count = (int)(read / 2 - 1); *unicode = TRUE;
        text = (wchar_t *)bytes;
        memmove(text, text + 1, (size_t)count * sizeof(wchar_t)); text[count] = 0;
        *result = text; return TRUE;
    }
    if (read >= 3 && (BYTE)bytes[0] == 0xef && (BYTE)bytes[1] == 0xbb && (BYTE)bytes[2] == 0xbf) {
        offset = 3; encoding = CP_UTF8;
    }
    count = read > (DWORD)offset ? MultiByteToWideChar(encoding, MB_ERR_INVALID_CHARS,
                                                     bytes + offset, (int)read - offset, NULL, 0) : 0;
    if (!count && read > (DWORD)offset) { free(bytes); return FALSE; }
    text = (wchar_t *)calloc((size_t)count + 1, sizeof(wchar_t));
    if (text && count) MultiByteToWideChar(encoding, MB_ERR_INVALID_CHARS, bytes + offset,
                                         (int)read - offset, text, count);
    free(bytes); *result = text; return text != NULL;
}
