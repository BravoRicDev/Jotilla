/* Streaming Unicode search. Compile KMP once per term; never retain note bodies.
   Cost: O(terms * bytes examined), space independent of archive text volume. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "search.h"
#include "tags.h"

#define READ_CAP 8192
typedef struct FoldBuffer {
    wchar_t *normal, *folded;
    WORD *types;
    int normal_cap, folded_cap;
} FoldBuffer;
typedef struct SearchTerm {
    wchar_t *raw, *pattern;
    int *failure, length;
    BOOL tag, untagged;
} SearchTerm;
typedef struct Query {
    wchar_t text[SEARCH_CAP];
    SearchTerm terms[SEARCH_TERMS];
    int count;
} Query;
typedef struct SearchJob {
    wchar_t directory[1024], settings[1024], query[SEARCH_CAP];
    wchar_t tag[TAG_NAME_CAP];
    SearchItem *items;
    int count;
    LONG generation, metadata_revision;
    SearchResult *result;
} SearchJob;
struct SearchWorker {
    HANDLE thread, wake;
    CRITICAL_SECTION lock;
    volatile LONG generation, stopping;
    SearchJob *pending;
    SearchResult *completed;
    HWND window;
    UINT message;
    TagStore tags;
    wchar_t settings[1024];
    WIN32_FILE_ATTRIBUTE_DATA metadata;
    volatile LONG metadata_revision;
    LONG loaded_revision;
    BOOL tags_loaded;
};

static void fold_free(FoldBuffer *buffer) {
    free(buffer->normal); free(buffer->folded); free(buffer->types);
}

static BOOL fold_reserve(FoldBuffer *buffer, int length) {
    if (length > buffer->folded_cap) {
        wchar_t *next = (wchar_t *)realloc(buffer->folded, (size_t)length * sizeof(wchar_t));
        WORD *types;
        if (!next) return FALSE;
        buffer->folded = next;
        types = (WORD *)realloc(buffer->types, (size_t)length * sizeof(WORD));
        if (!types) return FALSE;
        buffer->types = types; buffer->folded_cap = length;
    }
    return TRUE;
}

/* Decompose accents and apply invariant Unicode casing once per input block.
   Removing combining marks also handles accents split across read boundaries. */
static int fold_text(FoldBuffer *buffer, const wchar_t *text, int length) {
    int normal, folded, i, count = 0;
    if (!length) return 0;
    for (i = 0; i < length && text[i] < 128; ++i) {}
    if (i == length) {
        if (!fold_reserve(buffer, length)) return -1;
        for (i = 0; i < length; ++i)
            buffer->folded[i] = text[i] >= L'a' && text[i] <= L'z' ? text[i] - (L'a' - L'A') : text[i];
        return length;
    }
    normal = NormalizeString(NormalizationD, text, length, NULL, 0);
    if (normal <= 0) return -1;
    if (normal > buffer->normal_cap) {
        wchar_t *next = (wchar_t *)realloc(buffer->normal, (size_t)normal * sizeof(wchar_t));
        if (!next) return -1;
        buffer->normal = next; buffer->normal_cap = normal;
    }
    normal = NormalizeString(NormalizationD, text, length, buffer->normal, buffer->normal_cap);
    if (normal <= 0) return -1;
    folded = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, buffer->normal, normal, NULL, 0, NULL, NULL, 0);
    if (!folded) return -1;
    if (!fold_reserve(buffer, folded)) return -1;
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, buffer->normal, normal,
                       buffer->folded, buffer->folded_cap, NULL, NULL, 0) ||
        !GetStringTypeW(CT_CTYPE3, buffer->folded, folded, buffer->types)) return -1;
    for (i = 0; i < folded; ++i)
        if (!(buffer->types[i] & C3_NONSPACING)) buffer->folded[count++] = buffer->folded[i];
    return count;
}

static void query_free(Query *query) {
    int i;
    for (i = 0; i < query->count; ++i) {
        free(query->terms[i].pattern); query->terms[i].pattern = NULL;
        free(query->terms[i].failure); query->terms[i].failure = NULL;
    }
}

static void build_failure(_In_reads_(length) const wchar_t *pattern,
                          _Out_writes_(length) int *failure, int length) {
    int i, prefix = 0;
    failure[0] = 0;
    for (i = 1; i < length; ++i) {
        while (prefix && pattern[i] != pattern[prefix]) prefix = failure[prefix - 1];
        if (pattern[i] == pattern[prefix]) ++prefix;
        failure[i] = prefix;
    }
}

static BOOL query_compile(Query *query, const wchar_t *text, FoldBuffer *buffer) {
    wchar_t *read, *write;
    memset(query, 0, sizeof(*query));
    wcscpy_s(query->text, SEARCH_CAP, text); read = write = query->text;
    while (*read) {
        SearchTerm *term;
        BOOL quoted = FALSE;
        int length;
        while (iswspace(*read)) ++read;
        if (!*read) break;
        if (query->count == SEARCH_TERMS) return FALSE;
        term = &query->terms[query->count++];
        if (read[0] == L'-' && read[1] == L'#' && (!read[2] || iswspace(read[2]))) {
            term->tag = term->untagged = TRUE; read += 2; continue;
        }
        term->tag = *read == L'#';
        if (term->tag) ++read;
        term->raw = write;
        if (*read == L'"') { quoted = TRUE; ++read; }
        while (*read && (quoted ? *read != L'"' : !iswspace(*read))) *write++ = *read++;
        if (*read == L'"' && quoted) ++read;
        while (iswspace(*read)) ++read;
        *write++ = 0;
        if (!*term->raw) return FALSE;
        if (term->tag) {
            wchar_t canonical[TAGS_CAP];
            if (!tags_normalize(term->raw, canonical) || !*canonical || wcschr(canonical, L';')) return FALSE;
            term->pattern = _wcsdup(canonical);
            if (!term->pattern) return FALSE;
            continue;
        }
        length = fold_text(buffer, term->raw, (int)wcslen(term->raw));
        if (length <= 0) return FALSE;
        term->length = length;
        term->pattern = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
        term->failure = (int *)malloc((size_t)length * sizeof(int));
        if (!term->pattern || !term->failure) return FALSE;
        memcpy(term->pattern, buffer->folded, (size_t)length * sizeof(wchar_t));
        build_failure(term->pattern, term->failure, length);
    }
    return TRUE;
}

static BOOL term_match(const SearchTerm *term, const wchar_t *text, int length, int *state) {
    int i, prefix = *state;
    for (i = 0; i < length; ++i) {
        while (prefix && text[i] != term->pattern[prefix]) prefix = term->failure[prefix - 1];
        if (text[i] == term->pattern[prefix]) ++prefix;
        if (prefix == term->length) { *state = 0; return TRUE; }
    }
    *state = prefix; return FALSE;
}

static BOOL cancelled(SearchWorker *worker, LONG generation) {
    return InterlockedCompareExchange(&worker->generation, 0, 0) != generation;
}

/* Store only the byte range of a hit. Visible cards later read a bounded excerpt. */
static void locate_hit(SearchHit *hit, const wchar_t *context, int length,
                       const SearchTerm *term, DWORD context_offset) {
    int matched = 0, position = FindNLSStringEx(LOCALE_NAME_INVARIANT,
        FIND_FROMSTART | NORM_IGNORECASE | NORM_IGNORENONSPACE, context, length,
        term->raw, -1, &matched, NULL, NULL, 0);
    if (position >= 0) {
        int bytes = position ? WideCharToMultiByte(CP_UTF8, 0, context, position, NULL, 0, NULL, NULL) : 0;
        hit->offset = context_offset + (DWORD)bytes;
        hit->bytes = (DWORD)WideCharToMultiByte(CP_UTF8, 0, context + position, matched, NULL, 0, NULL, NULL);
    }
}

/* -1 means an unreadable/invalid file, 0 no match/cancelled, 1 a match. */
static int match_file(SearchWorker *worker, const SearchJob *job, const Query *query,
                      const wchar_t *path, unsigned matched, FoldBuffer *buffer,
                      SearchHit *hit, SearchResult *result) {
    char bytes[READ_CAP + 4];
    wchar_t context[READ_CAP + SEARCH_CAP];
    int states[SEARCH_TERMS] = {0}, carry = 0, tail = 0;
    DWORD total = 0, tail_bytes = 0;
    unsigned all = (1u << query->count) - 1u;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) return -1;
    ++result->files_read;
    while (!cancelled(worker, job->generation)) {
        DWORD read;
        int size, offset = 0, trim, wide = 0, folded, i;
        if (!ReadFile(file, bytes + carry, READ_CAP - (DWORD)carry, &read, NULL)) { CloseHandle(file); return -1; }
        result->bytes_read += read;
        size = (int)read + carry;
        if (!size) break;
        if (!total && size >= 3 && (BYTE)bytes[0] == 0xef && (BYTE)bytes[1] == 0xbb && (BYTE)bytes[2] == 0xbf) offset = 3;
        if (total + (DWORD)size > 64 * 1024 * 1024) { CloseHandle(file); return -1; }
        for (trim = 0; trim <= 3 && trim <= size - offset; ++trim) {
            wide = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes + offset, size - offset - trim,
                                       context + tail, READ_CAP);
            if (wide || size - offset == trim) break;
        }
        if (trim > 3 || (!read && trim)) { CloseHandle(file); return -1; }
        folded = fold_text(buffer, context + tail, wide);
        if (folded < 0) { CloseHandle(file); return -1; }
        for (i = 0; i < query->count; ++i) {
            if (!(matched & (1u << i)) && term_match(&query->terms[i], buffer->folded, folded, &states[i])) {
                matched |= 1u << i;
                if (hit->offset == MAXDWORD)
                    locate_hit(hit, context, tail + wide, &query->terms[i], total + (DWORD)offset - tail_bytes);
            }
        }
        if (matched == all) { CloseHandle(file); return 1; }
        /* A short synchronous disk read reached EOF. Avoid a second read and
           the unused context conversion for the common small-note case. */
        if (size < READ_CAP) { CloseHandle(file); return trim ? -1 : 0; }
        {
            int length = tail + wide, start;
            tail = min(length, SEARCH_CAP - 1); start = length - tail;
            if (tail && context[start] >= 0xdc00 && context[start] <= 0xdfff) { ++start; --tail; }
            memmove(context, context + start, (size_t)tail * sizeof(wchar_t));
            tail_bytes = tail ? (DWORD)WideCharToMultiByte(CP_UTF8, 0, context, tail, NULL, 0, NULL, NULL) : 0;
        }
        total += (DWORD)(size - trim);
        carry = trim; memmove(bytes, bytes + size - trim, (size_t)trim);
    }
    CloseHandle(file); return 0;
}

static BOOL load_tags(SearchWorker *worker, const SearchJob *job) {
    WIN32_FILE_ATTRIBUTE_DATA attributes = {0};
    TagStore store;
    if (!GetFileAttributesExW(job->settings, GetFileExInfoStandard, &attributes) &&
        GetLastError() != ERROR_FILE_NOT_FOUND) return FALSE;
    if (worker->tags_loaded && !wcscmp(worker->settings, job->settings) &&
        worker->loaded_revision == job->metadata_revision &&
        !CompareFileTime(&worker->metadata.ftLastWriteTime, &attributes.ftLastWriteTime) &&
        worker->metadata.nFileSizeLow == attributes.nFileSizeLow && worker->metadata.nFileSizeHigh == attributes.nFileSizeHigh)
        return TRUE;
    if (!tags_store_load(NULL, job->settings, &store)) return FALSE;
    tags_store_free(&worker->tags); worker->tags = store; worker->metadata = attributes;
    worker->loaded_revision = job->metadata_revision; worker->tags_loaded = TRUE;
    wcscpy_s(worker->settings, 1024, job->settings); return TRUE;
}

static SearchResult *run_search(SearchWorker *worker, SearchJob *job) {
    SearchResult *result = job->result;
    FoldBuffer buffer = {0};
    Query query;
    int i;
    job->result = NULL;
    if (!query_compile(&query, job->query, &buffer)) {
        result->error = L"Use up to 16 words or phrases; write a tag after #."; goto done;
    }
    if (!load_tags(worker, job)) {
        result->error = L"Tags could not be read. Refresh to try again."; goto done;
    }
    if (job->count) {
        result->hits = (SearchHit *)malloc((size_t)job->count * sizeof(*result->hits));
        if (!result->hits) { result->error = L"Not enough memory to search."; goto done; }
        result->capacity = job->count;
    }
    for (i = 0; i < job->count && !cancelled(worker, job->generation); ++i) {
        const wchar_t *tags = tags_store_get(&worker->tags, job->items[i].id);
        unsigned matched = 0, all = (1u << query.count) - 1u;
        SearchHit hit = {job->items[i].index, MAXDWORD, 0};
        wchar_t path[1024];
        int term, folded, match = 0;
        BOOL excluded = FALSE;
        if (*job->tag && !tags_contains(tags, job->tag)) continue;
        for (term = 0; term < query.count; ++term) if (query.terms[term].tag) {
            if (query.terms[term].untagged ? *tags != 0 : !tags_contains(tags, query.terms[term].pattern)) {
                excluded = TRUE; break;
            }
            matched |= 1u << term;
        }
        if (excluded) continue;
        if (matched == all) { result->hits[result->count++] = hit; continue; }
        folded = fold_text(&buffer, tags, (int)wcslen(tags));
        if (folded < 0) { result->error = L"Tags could not be searched."; break; }
        for (term = 0; term < query.count; ++term) if (!(matched & (1u << term))) {
            int state = 0;
            if (term_match(&query.terms[term], buffer.folded, folded, &state)) matched |= 1u << term;
        }
        if (matched == all) match = 1;
        else if (swprintf_s(path, 1024, L"%s\\%s.txt", job->directory, job->items[i].id) >= 0)
            match = match_file(worker, job, &query, path, matched, &buffer, &hit, result);
        if (match > 0) result->hits[result->count++] = hit;
        else if (match < 0) ++result->unreadable;
    }
done:
    query_free(&query); fold_free(&buffer);
    return result;
}

static void job_free(SearchJob *job) { if (job) { search_result_free(job->result); free(job->items); free(job); } }
void search_result_free(SearchResult *result) { if (result) { free(result->hits); free(result); } }

static DWORD WINAPI search_thread(void *parameter) {
    SearchWorker *worker = (SearchWorker *)parameter;
    for (;;) {
        SearchJob *job;
        SearchResult *result;
        WaitForSingleObject(worker->wake, INFINITE);
        if (InterlockedCompareExchange(&worker->stopping, 0, 0)) break;
        EnterCriticalSection(&worker->lock);
        job = worker->pending; worker->pending = NULL;
        LeaveCriticalSection(&worker->lock);
        if (!job) continue;
        result = run_search(worker, job);
        EnterCriticalSection(&worker->lock);
        if (!cancelled(worker, job->generation)) {
            search_result_free(worker->completed); worker->completed = result; result = NULL;
            PostMessageW(worker->window, worker->message, 0, 0);
        }
        LeaveCriticalSection(&worker->lock);
        search_result_free(result); job_free(job);
    }
    return 0;
}

SearchWorker *search_create(HWND window, UINT message) {
    SearchWorker *worker = (SearchWorker *)calloc(1, sizeof(*worker));
    if (!worker) return NULL;
    worker->window = window; worker->message = message;
    InitializeCriticalSection(&worker->lock);
    worker->wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (worker->wake) worker->thread = CreateThread(NULL, 0, search_thread, worker, 0, NULL);
    if (!worker->thread) { search_destroy(worker); return NULL; }
    return worker;
}

void search_cancel(SearchWorker *worker) {
    if (!worker) return;
    EnterCriticalSection(&worker->lock);
    InterlockedIncrement(&worker->generation);
    job_free(worker->pending); worker->pending = NULL;
    search_result_free(worker->completed); worker->completed = NULL;
    LeaveCriticalSection(&worker->lock);
}

void search_reload(SearchWorker *worker) {
    if (worker) InterlockedIncrement(&worker->metadata_revision);
}

BOOL search_submit(SearchWorker *worker, const wchar_t *directory, const wchar_t *settings,
                   const wchar_t *query, const SearchItem *items, int count) {
    return search_submit_filtered(worker, directory, settings, query, L"", items, count);
}

BOOL search_submit_filtered(SearchWorker *worker, const wchar_t *directory, const wchar_t *settings,
                            const wchar_t *query, const wchar_t *tag, const SearchItem *items, int count) {
    SearchJob *job;
    if (!worker || !directory || !settings || !query || !tag || count < 0 || (count && !items) ||
        wcslen(directory) >= 1024 || wcslen(settings) >= 1024 ||
        wcslen(query) >= SEARCH_CAP || wcslen(tag) >= TAG_NAME_CAP) return FALSE;
    job = (SearchJob *)calloc(1, sizeof(*job));
    if (!job) return FALSE;
    if (wcscpy_s(job->directory, 1024, directory) || wcscpy_s(job->settings, 1024, settings) ||
        wcscpy_s(job->query, SEARCH_CAP, query) || wcscpy_s(job->tag, TAG_NAME_CAP, tag)) { free(job); return FALSE; }
    if (count) {
        job->items = (SearchItem *)malloc((size_t)count * sizeof(*items));
        if (!job->items) { free(job); return FALSE; }
        memcpy(job->items, items, (size_t)count * sizeof(*items));
    }
    job->result = (SearchResult *)calloc(1, sizeof(*job->result));
    if (!job->result) { job_free(job); return FALSE; }
    job->count = count;
    job->metadata_revision = InterlockedCompareExchange(&worker->metadata_revision, 0, 0);
    EnterCriticalSection(&worker->lock);
    job->generation = InterlockedIncrement(&worker->generation);
    job_free(worker->pending); worker->pending = job;
    search_result_free(worker->completed); worker->completed = NULL;
    LeaveCriticalSection(&worker->lock);
    SetEvent(worker->wake); return TRUE;
}

SearchResult *search_take(SearchWorker *worker) {
    SearchResult *result;
    EnterCriticalSection(&worker->lock);
    result = worker->completed; worker->completed = NULL;
    LeaveCriticalSection(&worker->lock); return result;
}

void search_destroy(SearchWorker *worker) {
    if (!worker) return;
    InterlockedExchange(&worker->stopping, TRUE); search_cancel(worker);
    if (worker->thread) { SetEvent(worker->wake); WaitForSingleObject(worker->thread, INFINITE); CloseHandle(worker->thread); }
    if (worker->wake) CloseHandle(worker->wake);
    job_free(worker->pending); search_result_free(worker->completed);
    tags_store_free(&worker->tags);
    DeleteCriticalSection(&worker->lock); free(worker);
}
