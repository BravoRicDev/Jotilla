/* Small metadata sets; no persistent dictionary or index. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "tags.h"
#include "metadata.h"

static int name_compare(const void *a, const void *b) {
    return CompareStringOrdinal((const wchar_t *)a, -1, (const wchar_t *)b, -1, TRUE) - CSTR_EQUAL;
}

BOOL tags_next(const wchar_t **cursor, wchar_t name[TAG_NAME_CAP]) {
    const wchar_t *start = *cursor;
    size_t length;
    if (!start || !*start) return FALSE;
    length = wcscspn(start, L";");
    wcsncpy_s(name, TAG_NAME_CAP, start, min(length, TAG_NAME_CAP - 1));
    *cursor = start + length + (start[length] == L';');
    return TRUE;
}

BOOL tags_normalize(const wchar_t *input, wchar_t output[TAGS_CAP]) {
    wchar_t names[TAGS_MAX][TAG_NAME_CAP], raw[TAG_NAME_CAP], normal[TAG_NAME_CAP];
    int count = 0, i, length;
    size_t used = 0;
    output[0] = 0;
    while (*input) {
        const wchar_t *end = input + wcscspn(input, L";");
        const wchar_t *next = end + (*end == L';');
        while (input < end && iswspace(*input)) ++input;
        if (input < end && *input == L'#') ++input;
        while (end > input && iswspace(end[-1])) --end;
        length = (int)(end - input);
        if (length) {
            BOOL ascii = TRUE;
            if (length >= TAG_NAME_CAP) return FALSE;
            for (i = 0; i < length; ++i) {
                if (input[i] < L' ' || input[i] == L'"' || input[i] == L'#') return FALSE;
                if (input[i] >= 128) ascii = FALSE;
            }
            if (ascii) {
                for (i = 0; i < length; ++i) normal[i] = input[i] >= L'A' && input[i] <= L'Z' ? input[i] + (L'a' - L'A') : input[i];
                normal[length] = 0;
            } else {
                wcsncpy_s(raw, TAG_NAME_CAP, input, (size_t)length);
                if (NormalizeString(NormalizationC, raw, -1, normal, TAG_NAME_CAP) <= 0 ||
                    !LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, normal, -1,
                                   raw, TAG_NAME_CAP, NULL, NULL, 0)) return FALSE;
                wcscpy_s(normal, TAG_NAME_CAP, raw);
            }
            for (i = 0; i < count && name_compare(names[i], normal); ++i) {}
            if (i < count) { input = next; continue; }
            if (count == TAGS_MAX) return FALSE;
            wcscpy_s(names[count], TAG_NAME_CAP, normal);
            ++count;
        }
        input = next;
    }
    qsort(names, (size_t)count, sizeof(names[0]), name_compare);
    for (i = 0; i < count; ++i) {
        size_t size = wcslen(names[i]);
        if (used + size + 2 > TAGS_CAP) return FALSE;
        if (used) output[used++] = L';';
        memcpy(output + used, names[i], size * sizeof(wchar_t)); used += size;
    }
    output[used] = 0;
    return TRUE;
}

BOOL tags_contains(const wchar_t *tags, const wchar_t *name) {
    wchar_t item[TAG_NAME_CAP];
    while (tags_next(&tags, item)) if (!name_compare(item, name)) return TRUE;
    return FALSE;
}

static BOOL unicode_ini(const wchar_t *path) {
    wchar_t *text, temporary[1024], bom = 0xfeff;
    BOOL unicode, ok;
    DWORD size, written;
    HANDLE file;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        wchar_t header = 0;
        ok = ReadFile(file, &header, sizeof(header), &written, NULL);
        CloseHandle(file);
        if (!ok) return FALSE;
        if (written == sizeof(header) && header == 0xfeff) return TRUE;
    }
    if (!metadata_read(path, &text, &unicode)) return FALSE;
    if (unicode) { free(text); return TRUE; }
    if (swprintf_s(temporary, 1024, L"%s.unicode.tmp", path) < 0) { free(text); return FALSE; }
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) { free(text); return FALSE; }
    size = (DWORD)(wcslen(text) * sizeof(wchar_t));
    ok = WriteFile(file, &bom, sizeof(bom), &written, NULL) && written == sizeof(bom) &&
         WriteFile(file, text, size, &written, NULL) && written == size && FlushFileBuffers(file);
    free(text);
    if (!CloseHandle(file)) ok = FALSE;
    if (ok) ok = MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(temporary);
    return ok;
}

BOOL tags_read(const wchar_t *settings, const wchar_t *id, wchar_t **result) {
    wchar_t raw[TAGS_CAP + 1], normal[TAGS_CAP];
    DWORD length = GetPrivateProfileStringW(id, L"tags", L"", raw, TAGS_CAP + 1, settings);
    *result = NULL;
    if (length >= TAGS_CAP || !tags_normalize(raw, normal)) return FALSE;
    if (*normal) { *result = _wcsdup(normal); if (!*result) return FALSE; }
    return TRUE;
}

BOOL tags_write(const wchar_t *settings, const wchar_t *id, const wchar_t *tags) {
    if (tags && *tags && !unicode_ini(settings)) return FALSE;
    return WritePrivateProfileStringW(id, L"tags", tags && *tags ? tags : NULL, settings);
}

static int record_compare(const void *a, const void *b) {
    return wcscmp(((const TagRecord *)a)->id, ((const TagRecord *)b)->id);
}

void tags_store_free(TagStore *store) {
    size_t i;
    for (i = 0; i < store->count; ++i) { free(store->items[i].tags); store->items[i].tags = NULL; }
    free(store->items); memset(store, 0, sizeof(*store));
}

BOOL tags_store_load(const wchar_t *directory, const wchar_t *settings, TagStore *store) {
    wchar_t *text, *key, *value;
    MetadataCursor cursor = {0};
    size_t capacity = 0;
    BOOL unicode, ok = TRUE;
    memset(store, 0, sizeof(*store));
    if (!metadata_read(settings, &text, &unicode)) return FALSE;
    cursor.next = text;
    while (metadata_next(&cursor,&key,&value)) {
        const wchar_t *id = cursor.section;
        if (id && !_wcsicmp(key,L"tags") && wcslen(id)<80 &&
            (!wcsncmp(id,L"note-",5) || !wcsncmp(id,L"nota-",5))) {
            wchar_t normal[TAGS_CAP], path[1024];
            WIN32_FILE_ATTRIBUTE_DATA attributes;
            if (!tags_normalize(value, normal)) { ok = FALSE; break; }
            if (*normal && (!directory || (swprintf_s(path, 1024, L"%s\\%s.txt", directory, id) >= 0 &&
                GetFileAttributesExW(path, GetFileExInfoStandard, &attributes) &&
                !(attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                (attributes.nFileSizeHigh || attributes.nFileSizeLow)))) {
                TagRecord *record;
                if (store->count == capacity) {
                    size_t size = capacity ? capacity * 2 : 32;
                    TagRecord *items = (TagRecord *)realloc(store->items, size * sizeof(*items));
                    if (!items) { ok = FALSE; break; }
                    store->items = items; capacity = size;
                }
                record = &store->items[store->count];
                wcscpy_s(record->id, 80, id); record->tags = _wcsdup(normal);
                if (!record->tags) { ok = FALSE; break; }
                ++store->count;
            }
        }
    }
    free(text);
    if (!ok) { tags_store_free(store); return FALSE; }
    if (store->count > 1) qsort(store->items, store->count, sizeof(*store->items), record_compare);
    return TRUE;
}

const wchar_t *tags_store_get(const TagStore *store, const wchar_t *id) {
    TagRecord key = {0}, *record;
    if (!store->count) return L"";
    wcscpy_s(key.id, 80, id);
    record = (TagRecord *)bsearch(&key, store->items, store->count, sizeof(key), record_compare);
    return record ? record->tags : L"";
}

BOOL tags_catalog_build(const TagStore *store, TagCatalog *catalog) {
    size_t i, count = 0, capacity = 0;
    wchar_t name[TAG_NAME_CAP];
    memset(catalog, 0, sizeof(*catalog));
    for (i = 0; i < store->count; ++i) {
        const wchar_t *cursor = store->items[i].tags;
        while (tags_next(&cursor, name)) {
            if (count == capacity) {
                size_t size = capacity ? capacity * 2 : 32;
                TagCount *items = (TagCount *)realloc(catalog->items, size * sizeof(*items));
                if (!items) { tags_catalog_free(catalog); return FALSE; }
                catalog->items = items; capacity = size;
            }
            wcscpy_s(catalog->items[count].name, TAG_NAME_CAP, name);
            catalog->items[count++].uses = 1;
        }
    }
    if (count > 1) qsort(catalog->items, count, sizeof(*catalog->items), name_compare);
    for (i = 0; i < count; ++i) {
        if (catalog->count && !name_compare(catalog->items[catalog->count - 1].name, catalog->items[i].name))
            ++catalog->items[catalog->count - 1].uses;
        else catalog->items[catalog->count++] = catalog->items[i];
    }
    /* The catalog outlives this build: retain distinct names, not one slot
       per tag assignment across the entire archive. Shrinking is optional. */
    if (catalog->count && catalog->count < capacity) {
        TagCount *items = (TagCount *)realloc(catalog->items, catalog->count * sizeof(*items));
        if (items) catalog->items = items;
    }
    return TRUE;
}

void tags_catalog_free(TagCatalog *catalog) {
    free(catalog->items); memset(catalog, 0, sizeof(*catalog));
}

int tags_suggest(const TagCatalog *catalog, const wchar_t *prefix, const wchar_t *assigned,
                 const TagCount **results, int capacity) {
    size_t low = 0, high = catalog->count, length = wcslen(prefix), i;
    int count = 0;
    /* Locate the prefix interval, then retain only the best K without sorting it. */
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (name_compare(catalog->items[middle].name, prefix) < 0) low = middle + 1;
        else high = middle;
    }
    for (i = low; i < catalog->count; ++i) {
        const TagCount *item = &catalog->items[i];
        int position = 0, j;
        if (length && (wcslen(item->name) < length ||
            CompareStringOrdinal(item->name, (int)length, prefix, (int)length, TRUE) != CSTR_EQUAL)) break;
        if (tags_contains(assigned, item->name)) continue;
        while (position < count && (results[position]->uses > item->uses ||
               (results[position]->uses == item->uses && name_compare(results[position]->name, item->name) < 0))) ++position;
        if (position >= capacity) continue;
        if (count < capacity) ++count;
        for (j = count - 1; j > position; --j) results[j] = results[j - 1];
        results[position] = item;
    }
    return count;
}
