/* Archive location and one-shot migration. Pure C; no index or resident worker. */
#define COBJMACROS
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "location.h"

typedef struct TransferFile {
    wchar_t name[96];
    HANDLE source, target;
} TransferFile;

static BOOL full_path(const wchar_t *input, wchar_t output[LOCATION_CAP]) {
    DWORD length = GetFullPathNameW(input, LOCATION_CAP, output, NULL);
    if (!length || length >= LOCATION_CAP - 100) { SetLastError(ERROR_FILENAME_EXCED_RANGE); return FALSE; }
    while (length > 3 && output[length - 1] == L'\\') output[--length] = 0;
    return TRUE;
}

BOOL location_load(const wchar_t *config, const wchar_t *fallback, wchar_t path[LOCATION_CAP]) {
    char bytes[LOCATION_CAP * 4];
    wchar_t decoded[LOCATION_CAP];
    DWORD read, attributes;
    int count;
    HANDLE file = CreateFileW(config, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND && full_path(fallback, path);
    if (!ReadFile(file, bytes, sizeof(bytes), &read, NULL)) { CloseHandle(file); return FALSE; }
    CloseHandle(file);
    if (read == sizeof(bytes)) { SetLastError(ERROR_INVALID_DATA); return FALSE; }
    while (read && (bytes[read - 1] == '\r' || bytes[read - 1] == '\n')) --read;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, (int)read, decoded, LOCATION_CAP - 1);
    if (!count) { SetLastError(ERROR_INVALID_DATA); return FALSE; }
    decoded[count] = 0;
    if (count < 3 || wcslen(decoded) != (size_t)count ||
        !((decoded[0] == L'\\' && decoded[1] == L'\\') ||
          (decoded[1] == L':' && (decoded[2] == L'\\' || decoded[2] == L'/'))) ||
        !full_path(decoded, path)) { SetLastError(ERROR_INVALID_DATA); return FALSE; }
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return FALSE;
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) { SetLastError(ERROR_DIRECTORY); return FALSE; }
    return TRUE;
}

static BOOL save_location(const wchar_t *config, const wchar_t *path) {
    char utf8[LOCATION_CAP * 4];
    wchar_t temporary[LOCATION_CAP];
    DWORD written, error;
    int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, (int)wcslen(path),
                                    utf8, sizeof(utf8), NULL, NULL);
    HANDLE file;
    BOOL ok;
    if (!length || swprintf_s(temporary, LOCATION_CAP, L"%s.tmp", config) < 0) return FALSE;
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    ok = WriteFile(file, utf8, (DWORD)length, &written, NULL) && written == (DWORD)length && FlushFileBuffers(file);
    error = GetLastError();
    if (!CloseHandle(file)) { ok = FALSE; error = GetLastError(); }
    if (ok) { ok = MoveFileExW(temporary, config, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH); error = GetLastError(); }
    if (!ok) { DeleteFileW(temporary); SetLastError(error); }
    return ok;
}

BOOL location_user_paths(const wchar_t *local, wchar_t config[LOCATION_CAP], wchar_t fallback[LOCATION_CAP]) {
    const wchar_t *previous[] = {L"Jotila", L"IOTA", L"PicoSlip"};
    wchar_t root[LOCATION_CAP], legacy_config[LOCATION_CAP], legacy_notes[LOCATION_CAP], selected[LOCATION_CAP];
    DWORD attributes;
    int i;
    if (swprintf_s(root,LOCATION_CAP,L"%s\\Jotilla",local) < 0 ||
        swprintf_s(config,LOCATION_CAP,L"%s\\archive.path",root) < 0 ||
        swprintf_s(fallback,LOCATION_CAP,L"%s\\notes",root) < 0) return FALSE;
    if (!CreateDirectoryW(root,NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return FALSE;
    attributes = GetFileAttributesW(config);
    if (attributes != INVALID_FILE_ATTRIBUTES) return TRUE;
    if (GetLastError() != ERROR_FILE_NOT_FOUND) return FALSE;
    attributes = GetFileAttributesW(fallback);
    if (attributes != INVALID_FILE_ATTRIBUTES) return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (GetLastError() != ERROR_FILE_NOT_FOUND) return FALSE;
    for (i=0;i<(int)(sizeof(previous)/sizeof(previous[0]));++i) {
        if (swprintf_s(legacy_config,LOCATION_CAP,L"%s\\%s\\archive.path",local,previous[i]) < 0 ||
            swprintf_s(legacy_notes,LOCATION_CAP,L"%s\\%s\\notes",local,previous[i]) < 0) return FALSE;
        attributes = GetFileAttributesW(legacy_config);
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            if (!location_load(legacy_config,legacy_notes,selected)) return FALSE;
            return save_location(config,selected);
        }
        if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) return FALSE;
        attributes = GetFileAttributesW(legacy_notes);
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) { SetLastError(ERROR_DIRECTORY); return FALSE; }
            return save_location(config,legacy_notes);
        }
        if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) return FALSE;
    }
    return TRUE;
}

static BOOL managed_file(const wchar_t *name) {
    const wchar_t *extension;
    size_t length = wcslen(name);
    if (!_wcsicmp(name, L"notes.ini")) return TRUE;
    if (length < 9 || length >= 84 || (_wcsnicmp(name, L"note-", 5) && _wcsnicmp(name, L"nota-", 5))) return FALSE;
    extension = wcsrchr(name, L'.');
    return extension && (!_wcsicmp(extension, L".txt") || !_wcsicmp(extension, L".rtf"));
}

static BOOL empty_directory(const wchar_t *path) {
    wchar_t pattern[LOCATION_CAP];
    WIN32_FIND_DATAW entry;
    HANDLE scan;
    DWORD error;
    swprintf_s(pattern, LOCATION_CAP, L"%s\\*", path);
    scan = FindFirstFileW(pattern, &entry);
    if (scan == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    do {
        if (wcscmp(entry.cFileName, L".") && wcscmp(entry.cFileName, L"..")) {
            FindClose(scan); SetLastError(ERROR_DIR_NOT_EMPTY); return FALSE;
        }
    } while (FindNextFileW(scan, &entry));
    error = GetLastError(); FindClose(scan); SetLastError(error);
    return error == ERROR_NO_MORE_FILES;
}

static HANDLE directory_handle(const wchar_t *path, wchar_t canonical[LOCATION_CAP]) {
    HANDLE handle = CreateFileW(path, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    DWORD length;
    if (handle == INVALID_HANDLE_VALUE) return handle;
    length = GetFinalPathNameByHandleW(handle, canonical, LOCATION_CAP, FILE_NAME_NORMALIZED);
    if (!length || length >= LOCATION_CAP - 100) {
        DWORD error = length ? ERROR_FILENAME_EXCED_RANGE : GetLastError();
        CloseHandle(handle); SetLastError(error); return INVALID_HANDLE_VALUE;
    }
    while (length && canonical[length - 1] == L'\\') canonical[--length] = 0;
    return handle;
}

static BOOL contains_path(const wchar_t *parent, const wchar_t *child) {
    size_t length = wcslen(parent);
    return !_wcsnicmp(parent, child, length) && child[length] == L'\\';
}

/* Keep the originals locked until the default has committed. One reusable
   buffer copies every file, retaining creation dates for legacy filenames. */
static BOOL copy_contents(HANDLE source, HANDLE target, BYTE *buffer) {
    DWORD read, written;
    FILETIME created, accessed, modified;
    for (;;) {
        if (!ReadFile(source, buffer, 65536, &read, NULL)) return FALSE;
        if (!read) break;
        if (!WriteFile(target, buffer, read, &written, NULL) || written != read) return FALSE;
    }
    return GetFileTime(source, &created, &accessed, &modified) &&
           SetFileTime(target, &created, &accessed, &modified) && FlushFileBuffers(target);
}

BOOL location_change(const wchar_t *config, const wchar_t *source, const wchar_t *target,
                     BOOL move, wchar_t selected[LOCATION_CAP], BOOL *copies_left) {
    wchar_t source_key[LOCATION_CAP], target_key[LOCATION_CAP], path[LOCATION_CAP];
    wchar_t probe[LOCATION_CAP];
    HANDLE source_dir = INVALID_HANDLE_VALUE, target_dir = INVALID_HANDLE_VALUE, scan = INVALID_HANDLE_VALUE;
    TransferFile *files = NULL;
    BYTE *buffer = NULL;
    size_t count = 0, capacity = 0, i;
    DWORD error = ERROR_SUCCESS;
    BOOL committed = FALSE;
    FILE_DISPOSITION_INFO discard = {TRUE};
    *copies_left = FALSE;
    if (!full_path(target, selected)) return FALSE;
    if (!CreateDirectoryW(selected, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return FALSE;
    source_dir = directory_handle(source, source_key);
    if (source_dir == INVALID_HANDLE_VALUE) goto done;
    target_dir = directory_handle(selected, target_key);
    if (target_dir == INVALID_HANDLE_VALUE) goto done;
    if (!_wcsicmp(source_key, target_key)) { committed = save_location(config, selected); goto done; }
    if (move && (contains_path(source_key, target_key) || contains_path(target_key, source_key))) {
        SetLastError(ERROR_INVALID_PARAMETER); goto done;
    }
    /* Opening an archive must also support subsequent autosaves. The uniquely
       named probe is removed by Windows even if the process is interrupted. */
    swprintf_s(probe, LOCATION_CAP, L"%s\\.iota-write-%lu-%llu.tmp", selected, GetCurrentProcessId(), GetTickCount64());
    {
        HANDLE test = CreateFileW(probe, GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW,
                                  FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
        if (test == INVALID_HANDLE_VALUE) goto done;
        CloseHandle(test);
    }
    if (!move) {
        HANDLE metadata;
        swprintf_s(path, LOCATION_CAP, L"%s\\notes.ini", selected);
        metadata = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (metadata == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND) goto done;
        if (metadata != INVALID_HANDLE_VALUE) CloseHandle(metadata);
    }
    if (move) {
        WIN32_FIND_DATAW entry;
        if (!empty_directory(selected)) goto done;
        swprintf_s(path, LOCATION_CAP, L"%s\\*", source);
        scan = FindFirstFileW(path, &entry);
        if (scan == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND) goto done;
        if (scan != INVALID_HANDLE_VALUE) {
            do {
                TransferFile *file;
                if (!managed_file(entry.cFileName)) continue;
                if (entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_READONLY)) {
                    SetLastError(ERROR_ACCESS_DENIED); goto done;
                }
                if (count == capacity) {
                    size_t next = capacity ? capacity * 2 : 32;
                    TransferFile *grown = (TransferFile *)realloc(files, next * sizeof(*files));
                    if (!grown) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); goto done; }
                    files = grown; capacity = next;
                }
                file = &files[count++];
                file->source = file->target = INVALID_HANDLE_VALUE;
                wcscpy_s(file->name, 96, entry.cFileName);
                swprintf_s(path, LOCATION_CAP, L"%s\\%s", source, file->name);
                file->source = CreateFileW(path, GENERIC_READ | DELETE, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                           FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
                if (file->source == INVALID_HANDLE_VALUE) goto done;
                {
                    BY_HANDLE_FILE_INFORMATION info;
                    if (!GetFileInformationByHandle(file->source, &info)) goto done;
                    if (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_READONLY)) {
                        SetLastError(ERROR_ACCESS_DENIED); goto done;
                    }
                }
            } while (FindNextFileW(scan, &entry));
            if (GetLastError() != ERROR_NO_MORE_FILES) goto done;
            FindClose(scan); scan = INVALID_HANDLE_VALUE;
        }
        if (count) {
            buffer = (BYTE *)malloc(65536);
            if (!buffer) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); goto done; }
        }
        for (i = 0; i < count; ++i) {
            swprintf_s(path, LOCATION_CAP, L"%s\\%s", selected, files[i].name);
            files[i].target = CreateFileW(path, GENERIC_WRITE | DELETE, FILE_SHARE_READ, NULL, CREATE_NEW,
                                          FILE_ATTRIBUTE_NORMAL, NULL);
            if (files[i].target == INVALID_HANDLE_VALUE || !copy_contents(files[i].source, files[i].target, buffer)) goto done;
        }
    }
    committed = save_location(config, selected);
done:
    error = GetLastError();
    if (scan != INVALID_HANDLE_VALUE) FindClose(scan);
    for (i = 0; i < count; ++i) {
        if (files[i].target != INVALID_HANDLE_VALUE) {
            if (!committed) SetFileInformationByHandle(files[i].target, FileDispositionInfo, &discard, sizeof(discard));
            CloseHandle(files[i].target); files[i].target = INVALID_HANDLE_VALUE;
        }
        if (files[i].source != INVALID_HANDLE_VALUE) {
            if (committed && !SetFileInformationByHandle(files[i].source, FileDispositionInfo, &discard, sizeof(discard)))
                *copies_left = TRUE;
            CloseHandle(files[i].source); files[i].source = INVALID_HANDLE_VALUE;
        }
    }
    free(buffer); free(files);
    if (source_dir != INVALID_HANDLE_VALUE) CloseHandle(source_dir);
    if (target_dir != INVALID_HANDLE_VALUE) CloseHandle(target_dir);
    /* Keep the old directory itself: it may contain unrelated user files. */
    SetLastError(error);
    return committed;
}

HRESULT location_pick(HWND owner, const wchar_t *current, BOOL move, wchar_t path[LOCATION_CAP]) {
    IFileDialog *dialog = NULL;
    IShellItem *folder = NULL, *selection = NULL;
    PWSTR name = NULL;
    HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    HRESULT result;
    if (FAILED(initialized)) return initialized;
    result = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileDialog, (void **)&dialog);
    if (SUCCEEDED(result)) result = IFileDialog_SetOptions(dialog,
        FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT);
    if (SUCCEEDED(result)) result = IFileDialog_SetTitle(dialog, move ? L"Move current archive to an empty folder" : L"Open or create archive");
    if (SUCCEEDED(result)) result = IFileDialog_SetOkButtonLabel(dialog, move ? L"Move archive here" : L"Use this archive");
    if (SUCCEEDED(result)) {
        if (SUCCEEDED(SHCreateItemFromParsingName(current, NULL, &IID_IShellItem, (void **)&folder)))
            IFileDialog_SetFolder(dialog, folder);
        result = IFileDialog_Show(dialog, owner);
    }
    if (SUCCEEDED(result)) result = IFileDialog_GetResult(dialog, &selection);
    if (SUCCEEDED(result)) result = IShellItem_GetDisplayName(selection, SIGDN_FILESYSPATH, &name);
    if (SUCCEEDED(result) && (!name || wcslen(name) >= LOCATION_CAP - 100)) result = HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    if (SUCCEEDED(result)) wcscpy_s(path, LOCATION_CAP, name);
    CoTaskMemFree(name);
    if (selection) IShellItem_Release(selection);
    if (folder) IShellItem_Release(folder);
    if (dialog) IFileDialog_Release(dialog);
    CoUninitialize();
    return result;
}
