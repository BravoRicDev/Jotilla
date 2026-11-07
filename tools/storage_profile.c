/* Measure production note storage against isolated synthetic metadata archives.
   Native editors stay hidden; metadata sections need no corresponding TXT. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned profile_calls, flush_calls;
static ULONGLONG output_bytes;
static BOOL counted_profile(LPCWSTR section, LPCWSTR key, LPCWSTR value, LPCWSTR path) {
    ++profile_calls; return WritePrivateProfileStringW(section, key, value, path);
}
static BOOL counted_section(LPCWSTR section, LPCWSTR values, LPCWSTR path) {
    ++profile_calls; return WritePrivateProfileSectionW(section, values, path);
}
static BOOL counted_flush(HANDLE file) { ++flush_calls; return FlushFileBuffers(file); }
static BOOL counted_write(HANDLE file, LPCVOID data, DWORD size, LPDWORD written, LPOVERLAPPED async) {
    output_bytes += size; return WriteFile(file, data, size, written, async);
}
#define WritePrivateProfileStringW counted_profile
#define WritePrivateProfileSectionW counted_section
#define FlushFileBuffers counted_flush
#define WriteFile counted_write
#include "../src/picoslip.c"
#undef WritePrivateProfileStringW
#undef WritePrivateProfileSectionW
#undef FlushFileBuffers
#undef WriteFile

static void check(BOOL ok) {
    if (!ok) { fprintf(stderr, "Probe failed, Win32=%lu\n", GetLastError()); exit(1); }
}
static void body(Note *note, const wchar_t *text) {
    SendMessageW(note->edit, EM_SETSEL, 0, -1);
    SendMessageW(note->edit, EM_REPLACESEL, TRUE, (LPARAM)text);
}
static void seed_metadata(int sections) {
    FILE *file = NULL;
    int i;
    wchar_t bom = 0xfeff;
    check(_wfopen_s(&file, state_path, L"wb") == 0);
    check(fwrite(&bom, sizeof(bom), 1, file) == 1);
    for (i = 0; i < sections; ++i) {
        wchar_t row[512];
        int length = swprintf_s(row, 512,
            L"[note-2030-01-01_00-00-00-%03d-%03d]\r\n"
            L"x=100\r\ny=100\r\ncolor=0\r\nhas_title=0\r\ncollapsed=0\r\nopen=0\r\n"
            L"palette=0\r\nsize=1\r\nopacity=100\r\nwidth=300\r\nheight=240\r\ntags=work\r\n",
            i / 1000, i % 1000);
        check(length > 0 && fwrite(row, sizeof(wchar_t), (size_t)length, file) == (size_t)length);
    }
    check(fclose(file) == 0);
}
static void clear_fixture(const wchar_t *root) {
    WIN32_FIND_DATAW found;
    wchar_t pattern[PATH_CAP], path[PATH_CAP];
    HANDLE scan;
    swprintf_s(pattern, PATH_CAP, L"%s\\*", root);
    scan = FindFirstFileW(pattern, &found);
    if (scan != INVALID_HANDLE_VALUE) {
        do {
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                swprintf_s(path, PATH_CAP, L"%s\\%s", root, found.cFileName);
                check(DeleteFileW(path));
            }
        } while (FindNextFileW(scan, &found));
        FindClose(scan);
    }
    check(RemoveDirectoryW(root));
}
int wmain(void) {
    const char *labels[] = {"new-blank", "first-save", "body-save", "format-only", "title-only", "reopen"};
    int sizes[] = {100, 1000, 10000};
    wchar_t temporary[PATH_CAP], root[PATH_CAP];
    LARGE_INTEGER frequency;
    int pass, op, repeat;
    instance = GetModuleHandleW(NULL); testing = TRUE;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    app_icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, 0);
    check(register_classes());
    check(GetTempPathW(PATH_CAP, temporary) != 0);
    QueryPerformanceFrequency(&frequency);
    puts("metadata_sections,operation,mean_ms,profile_calls_per_op,flushes_per_op,direct_write_bytes_per_op");
    for (pass = 0; pass < 3; ++pass) {
        swprintf_s(root, PATH_CAP, L"%sPicoSlip-storage-review-%lu-%llu-%d", temporary,
                   GetCurrentProcessId(), GetTickCount64(), pass);
        check(make_paths(root)); seed_metadata(sizes[pass]);
        for (op = 0; op < 6; ++op) {
            double total = 0;
            ULONGLONG bytes = 0;
            unsigned profiles = 0, flushes = 0;
            for (repeat = 0; repeat < 10; ++repeat) {
                Note *note;
                wchar_t id[80];
                LARGE_INTEGER start, end;
                note = op ? new_note() : NULL;
                if (op) check(note != NULL);
                if (op >= 2) { body(note, L"Small sample.\r\nUnicode: caff\x00e8."); check(save_note(note)); }
                if (op == 1) body(note, L"Small sample.\r\nUnicode: caff\x00e8.");
                if (op == 2) body(note, L"Changed sample.\r\nUnicode: caff\x00e8.");
                if (op == 3) {
                    SendMessageW(note->edit, EM_SETSEL, 0, 5); toggle_format(note, CFE_ITALIC);
                }
                if (op == 4) check(set_title(note, L"Title"));
                if (op == 5) { wcscpy_s(id, 80, note->id); destroy_note(note); }
                profile_calls = flush_calls = 0; output_bytes = 0;
                QueryPerformanceCounter(&start);
                if (op == 0) { note = new_note(); check(note != NULL); }
                else if (op == 5) { note = open_note(id, 0, TRUE); check(note != NULL); }
                else check(save_note(note));
                QueryPerformanceCounter(&end);
                total += (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;
                profiles += profile_calls; flushes += flush_calls; bytes += output_bytes;
                destroy_note(note);
            }
            printf("%d,%s,%.3f,%.1f,%.1f,%.1f\n", sizes[pass], labels[op], total / 10,
                   profiles / 10.0, flushes / 10.0, bytes / 10.0); fflush(stdout);
        }
        clear_fixture(root);
    }
    if (app_icon) DestroyIcon(app_icon);
    if (rich_edit) FreeLibrary(rich_edit);
    return 0;
}
