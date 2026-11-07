#ifndef IOTA_SEARCH_H
#define IOTA_SEARCH_H
#include <windows.h>

#define SEARCH_CAP 512
#define SEARCH_TERMS 16
typedef struct SearchItem { wchar_t id[80]; int index; } SearchItem;
typedef struct SearchHit { int index; DWORD offset, bytes; } SearchHit;
typedef struct SearchResult {
    SearchHit *hits;
    int count, capacity, unreadable;
    unsigned files_read;
    ULONGLONG bytes_read;
    const wchar_t *error;
} SearchResult;
typedef struct SearchWorker SearchWorker;

/* A single sleeping worker owns each immutable job and its reusable buffers. */
SearchWorker *search_create(HWND window, UINT message);
BOOL search_submit(SearchWorker *worker, const wchar_t *directory, const wchar_t *settings,
                   const wchar_t *query, const SearchItem *items, int count);
BOOL search_submit_filtered(SearchWorker *worker, const wchar_t *directory, const wchar_t *settings,
                            const wchar_t *query, const wchar_t *tag, const SearchItem *items, int count);
void search_cancel(SearchWorker *worker);
void search_reload(SearchWorker *worker);
SearchResult *search_take(SearchWorker *worker);
void search_result_free(SearchResult *result);
void search_destroy(SearchWorker *worker);
#endif
