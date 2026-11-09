/* Reproducible search measurements in an isolated, disposable UTF-8 archive. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "../src/search.h"

static SearchResult *measure(SearchWorker *worker, const wchar_t *root, const wchar_t *settings,
                             SearchItem *items, int count, const wchar_t *query, double *milliseconds) {
    LARGE_INTEGER frequency, start, end;
    ULONGLONG deadline = GetTickCount64() + 60000;
    SearchResult *result;
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
    if (!search_submit(worker, root, settings, query, items, count)) return NULL;
    do {
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        result = search_take(worker);
        if (result) {
            QueryPerformanceCounter(&end);
            *milliseconds = (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;
            return result;
        }
        MsgWaitForMultipleObjects(0, NULL, FALSE, 1000, QS_ALLINPUT);
    } while (GetTickCount64() < deadline);
    return NULL;
}

int wmain(void) {
    wchar_t temporary[1024], root[1024], settings[1024], path[1024];
    SearchItem *items = (SearchItem *)calloc(10000, sizeof(*items));
    SearchWorker *worker = NULL;
    HWND window = NULL;
    char body[2048];
    int sizes[] = {100, 1000, 10000}, created = 0, pass, i, run, status = 1;
    if (!items || !GetTempPathW(1024, temporary)) { free(items); return 1; }
    swprintf_s(root, 1024, L"%sJotilla-search-profile-%lu-%llu", temporary, GetCurrentProcessId(), GetTickCount64());
    swprintf_s(settings, 1024, L"%s\\notes.ini", root);
    if (!CreateDirectoryW(root, NULL)) { free(items); return 1; }
    window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
    if (window) worker = search_create(window, WM_APP + 99);
    if (!worker) goto cleanup;
    memset(body, 'x', sizeof(body)); memcpy(body, "#Release planning\r\n", 19);
    memcpy(body + 1600, "launch milestone", 16);
    puts("notes,query,first_ms,warm_mean_ms,files_read,bytes_read,results,private_mib");
    for (pass = 0; pass < 3; ++pass) {
        int count = sizes[pass];
        FILE *metadata = NULL;
        wchar_t bom = 0xfeff;
        for (i = created; i < count; ++i) {
            HANDLE file;
            DWORD written;
            swprintf_s(items[i].id, 80, L"note-2032-01-01_00-00-00-%03d-%03d", i / 1000, i % 1000);
            items[i].index = i;
            swprintf_s(path, 1024, L"%s\\%s.txt", root, items[i].id);
            file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if (file == INVALID_HANDLE_VALUE) goto cleanup;
            ++created;
            if (!WriteFile(file, body, sizeof(body), &written, NULL) || written != sizeof(body)) { CloseHandle(file); goto cleanup; }
            CloseHandle(file);
        }
        if (_wfopen_s(&metadata, settings, L"wb")) goto cleanup;
        fwrite(&bom, sizeof(bom), 1, metadata);
        for (i = 0; i < count; ++i) {
            wchar_t row[180];
            swprintf_s(row, 180, L"[%s]\r\ntags=%s\r\n", items[i].id, i % 10 ? L"personal" : L"project alpha;work");
            fwrite(row, sizeof(wchar_t), wcslen(row), metadata);
        }
        if (fclose(metadata)) goto cleanup;
        for (run = 0; run < 3; ++run) {
            const wchar_t *queries[] = {L"absent", L"#work", L"#work \"launch milestone\""};
            const char *labels[] = {"full-text", "tag-only", "tag-and-text"};
            SearchResult *result = NULL;
            PROCESS_MEMORY_COUNTERS_EX memory = {0};
            double first = 0, total = 0;
            int repeat;
            for (repeat = 0; repeat < 4; ++repeat) {
                double elapsed;
                search_result_free(result);
                result = measure(worker, root, settings, items, count, queries[run], &elapsed);
                if (!result || result->error || result->unreadable ||
                    result->count != (run ? count / 10 : 0) || (run == 1 && result->files_read)) {
                    search_result_free(result); goto cleanup;
                }
                if (!repeat) first = elapsed; else total += elapsed;
            }
            memory.cb = sizeof(memory);
            GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&memory, sizeof(memory));
            printf("%d,%s,%.2f,%.2f,%u,%llu,%d,%.2f\n", count, labels[run], first, total / 3,
                   result->files_read, result->bytes_read, result->count, memory.PrivateUsage / 1048576.0);
            fflush(stdout); search_result_free(result);
        }
    }
    status = 0;
cleanup:
    search_destroy(worker); if (window) DestroyWindow(window);
    for (i = 0; i < created; ++i) {
        swprintf_s(path, 1024, L"%s\\%s.txt", root, items[i].id); DeleteFileW(path);
    }
    DeleteFileW(settings); RemoveDirectoryW(root); free(items);
    if (status) fputs("Search profiling failed.\n", stderr);
    return status;
}
