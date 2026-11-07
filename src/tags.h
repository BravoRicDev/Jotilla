#ifndef IOTA_TAGS_H
#define IOTA_TAGS_H
#include <windows.h>

#define TAG_NAME_CAP 64
#define TAGS_CAP 2048
#define TAGS_MAX 24

typedef struct TagRecord {
    wchar_t id[80];
    wchar_t *tags;
} TagRecord;
typedef struct TagStore {
    TagRecord *items;
    size_t count;
} TagStore;
typedef struct TagCount {
    wchar_t name[TAG_NAME_CAP];
    unsigned uses;
} TagCount;
typedef struct TagCatalog {
    TagCount *items;
    size_t count;
} TagCatalog;

/* Canonical, sorted, unique names separated by ';'. Empty sets allocate nothing. */
BOOL tags_normalize(const wchar_t *input, wchar_t output[TAGS_CAP]);
BOOL tags_read(const wchar_t *settings, const wchar_t *id, wchar_t **result);
BOOL tags_write(const wchar_t *settings, const wchar_t *id, const wchar_t *tags);
BOOL tags_contains(const wchar_t *tags, const wchar_t *name);
BOOL tags_next(const wchar_t **cursor, wchar_t name[TAG_NAME_CAP]);
/* NULL directory skips existence checks when the caller already has a file snapshot. */
BOOL tags_store_load(const wchar_t *directory, const wchar_t *settings, TagStore *store);
const wchar_t *tags_store_get(const TagStore *store, const wchar_t *id);
void tags_store_free(TagStore *store);
BOOL tags_catalog_build(const TagStore *store, TagCatalog *catalog);
void tags_catalog_free(TagCatalog *catalog);
int tags_suggest(const TagCatalog *catalog, const wchar_t *prefix, const wchar_t *assigned,
                 const TagCount **results, int capacity);
#endif
