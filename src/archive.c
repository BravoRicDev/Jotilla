/* Native, virtual archive view. Only visible previews are read and cached. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <wchar.h>
#include "archive.h"
#include "colors.h"

#define ARCHIVE_CLASS L"PicoSlip.Archive"
#define ARCHIVE_PATH_CAP 1024
#define DELETE_NOTES 1

typedef struct ArchiveEntry {
    wchar_t id[80];
    wchar_t *preview;
    unsigned date;
    COLORREF paper, ink;
    BOOL loaded, titled;
} ArchiveEntry;

typedef struct Archive {
    HWND window, tree, list;
    HFONT font, bold;
    HIMAGELIST tiles;
    ArchiveEntry *entries;
    int count, first, visible, dpi;
    unsigned filter;
    const wchar_t *directory, *settings;
    ArchiveActions actions;
    BOOL rebuilding;
} Archive;

static HWND archive_window;

static int archive_scaled(const Archive *archive, int value) { return MulDiv(value, archive->dpi, 96); }

static void free_entries(ArchiveEntry *entries, int count) {
    int i;
    for (i = 0; i < count; ++i) free(entries[i].preview);
    free(entries);
}

static int compare_entries(const void *left, const void *right) {
    const ArchiveEntry *a = (const ArchiveEntry *)left, *b = (const ArchiveEntry *)right;
    if (a->date != b->date) return a->date < b->date ? 1 : -1;
    return wcscmp(b->id, a->id);
}

static BOOL scan_archive(Archive *archive, ArchiveEntry **result, int *total) {
    wchar_t pattern[ARCHIVE_PATH_CAP];
    WIN32_FIND_DATAW found;
    HANDLE search;
    ArchiveEntry *entries = NULL;
    int count = 0, capacity = 0;
    DWORD error;
    *result = NULL; *total = 0;
    if (swprintf_s(pattern, ARCHIVE_PATH_CAP, L"%s\\*.txt", archive->directory) < 0) return FALSE;
    search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    do {
        size_t length = wcslen(found.cFileName);
        SYSTEMTIME time;
        FILETIME local;
        unsigned year, month, day;
        ArchiveEntry *entry;
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (!found.nFileSizeHigh && !found.nFileSizeLow) ||
            length < 6 || length - 4 >= 80 ||
            (wcsncmp(found.cFileName, L"note-", 5) && wcsncmp(found.cFileName, L"nota-", 5))) continue;
        if (count == capacity) {
            int next_capacity;
            ArchiveEntry *next;
            if (capacity > INT_MAX / 2) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); break; }
            next_capacity = capacity ? capacity * 2 : 64;
            next = (ArchiveEntry *)realloc(entries, (size_t)next_capacity * sizeof(*entries));
            if (!next) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); break; }
            entries = next; capacity = next_capacity;
        }
        entry = &entries[count];
        memset(entry, 0, sizeof(*entry));
        wcsncpy_s(entry->id, 80, found.cFileName, length - 4);
        if (swscanf_s(entry->id + 5, L"%4u-%2u-%2u", &year, &month, &day) == 3 &&
            year >= 1601 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 && day <= 31)
            entry->date = year * 10000 + month * 100 + day;
        else if (FileTimeToLocalFileTime(&found.ftCreationTime, &local) && FileTimeToSystemTime(&local, &time))
            entry->date = time.wYear * 10000u + time.wMonth * 100u + time.wDay;
        ++count;
    } while (FindNextFileW(search, &found));
    error = GetLastError(); FindClose(search);
    if (error != ERROR_NO_MORE_FILES) { free_entries(entries, count); return FALSE; }
    if (count > 1) qsort(entries, (size_t)count, sizeof(*entries), compare_entries);
    *result = entries; *total = count;
    return TRUE;
}

/* Date groups are contiguous in the sorted vector: filtering needs no
   secondary index, allocation, or full scan. */
static int date_bound(const Archive *archive, unsigned date) {
    int low = 0, high = archive->count;
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (archive->entries[middle].date > date) low = middle + 1;
        else high = middle;
    }
    return low;
}

static void filter_archive(Archive *archive, unsigned date) {
    unsigned last = date;
    archive->filter = date;
    if (!date) { archive->first = 0; archive->visible = archive->count; }
    else {
        if (!(date % 10000)) last += 1231;
        else if (!(date % 100)) last += 31;
        archive->first = date_bound(archive, last);
        archive->visible = date_bound(archive, date - 1) - archive->first;
    }
    ListView_SetItemState(archive->list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemCountEx(archive->list, archive->visible, 0);
    ListView_Arrange(archive->list, LVA_DEFAULT);
    InvalidateRect(archive->list, NULL, TRUE);
}

static HTREEITEM date_node(Archive *archive, HTREEITEM parent, const wchar_t *label, unsigned date) {
    TVINSERTSTRUCTW insert = {0};
    insert.hParent = parent; insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_PARAM;
    insert.item.pszText = (wchar_t *)label; insert.item.lParam = date;
    return TreeView_InsertItem(archive->tree, &insert);
}

static BOOL refresh_archive(Archive *archive) {
    ArchiveEntry *entries;
    int count, i;
    unsigned year = UINT_MAX, month = UINT_MAX, day = UINT_MAX, filter = archive->filter;
    HTREEITEM root, year_node = NULL, month_node = NULL, selected;
    wchar_t label[64];
    if (!archive->actions.save() || !scan_archive(archive, &entries, &count)) return FALSE;
    archive->rebuilding = TRUE;
    SendMessageW(archive->tree, WM_SETREDRAW, FALSE, 0);
    ListView_SetItemCountEx(archive->list, 0, 0);
    free_entries(archive->entries, archive->count);
    archive->entries = entries; archive->count = count;
    TreeView_DeleteAllItems(archive->tree);
    swprintf_s(label, 64, L"All notes (%d)", count);
    root = date_node(archive, TVI_ROOT, label, 0); selected = root;
    for (i = 0; i < count; ++i) {
        unsigned date = entries[i].date, y = date / 10000, m = date / 100;
        HTREEITEM node;
        if (y != year) {
            year = y; month = day = UINT_MAX;
            swprintf_s(label, 64, L"%04u", year);
            year_node = date_node(archive, root, label, year * 10000);
            if (filter == year * 10000) selected = year_node;
        }
        if (m != month) {
            SYSTEMTIME time = {0};
            month = m; day = UINT_MAX;
            time.wYear = (WORD)year; time.wMonth = (WORD)(month % 100); time.wDay = 1;
            if (!GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &time, L"MMMM", label, 64, NULL))
                swprintf_s(label, 64, L"%02u", month % 100);
            month_node = date_node(archive, year_node, label, month * 100);
            if (filter == month * 100) selected = month_node;
        }
        if (date != day) {
            day = date; swprintf_s(label, 64, L"%02u", day % 100);
            node = date_node(archive, month_node, label, date);
            if (filter == date) selected = node;
        }
    }
    TreeView_Expand(archive->tree, root, TVE_EXPAND);
    if (count) {
        HTREEITEM latest_year = TreeView_GetChild(archive->tree, root);
        TreeView_Expand(archive->tree, latest_year, TVE_EXPAND);
        TreeView_Expand(archive->tree, TreeView_GetChild(archive->tree, latest_year), TVE_EXPAND);
    }
    TreeView_SelectItem(archive->tree, selected);
    TreeView_EnsureVisible(archive->tree, selected);
    archive->rebuilding = FALSE;
    filter_archive(archive, selected == root ? 0 : filter);
    SendMessageW(archive->tree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(archive->tree, NULL, TRUE);
    return TRUE;
}

static void load_preview(Archive *archive, ArchiveEntry *entry) {
    wchar_t path[ARCHIVE_PATH_CAP];
    char bytes[1024];
    DWORD read;
    HANDLE file;
    int count = 0, length, offset = 0, trim;
    int color = (int)GetPrivateProfileIntW(entry->id, L"color", 0, archive->settings);
    int palette = (int)GetPrivateProfileIntW(entry->id, L"palette", 0, archive->settings);
    if (color < 0 || color >= COLOR_COUNT) color = 0;
    if (palette < 0 || palette >= 3) palette = 0;
    entry->paper = PALETTES[palette][color];
    entry->ink = palette == 1 && (color & 1) ? RGB(255,255,255) : RGB(0,0,0);
    entry->titled = GetPrivateProfileIntW(entry->id, L"has_title", FALSE, archive->settings) != 0;
    entry->loaded = TRUE;
    if (swprintf_s(path, ARCHIVE_PATH_CAP, L"%s\\%s.txt", archive->directory, entry->id) < 0) return;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    if (!ReadFile(file, bytes, sizeof(bytes), &read, NULL)) { CloseHandle(file); return; }
    CloseHandle(file);
    if (read >= 3 && (BYTE)bytes[0] == 0xef && (BYTE)bytes[1] == 0xbb && (BYTE)bytes[2] == 0xbf) offset = 3;
    length = (int)read - offset;
    /* A bounded read may stop in the middle of a UTF-8 code point. */
    for (trim = 0; trim <= 3 && length >= trim; ++trim) {
        count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes + offset, length - trim, NULL, 0);
        if (count || length == trim) { length -= trim; break; }
    }
    if (!count && read > (DWORD)offset) return;
    entry->preview = (wchar_t *)calloc((size_t)count + 1, sizeof(wchar_t));
    if (entry->preview && count) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes + offset, length, entry->preview, count);
}

static LRESULT draw_preview(Archive *archive, NMLVCUSTOMDRAW *draw) {
    int index = (int)draw->nmcd.dwItemSpec;
    ArchiveEntry *entry;
    RECT card, bar, title, body;
    HDC dc = draw->nmcd.hdc;
    HGDIOBJ previous;
    HBRUSH brush = (HBRUSH)GetStockObject(DC_BRUSH);
    const wchar_t *text, *after;
    if (index < 0 || index >= archive->visible) return CDRF_SKIPDEFAULT;
    entry = &archive->entries[archive->first + index];
    if (!entry->loaded) load_preview(archive, entry);
    ListView_GetItemRect(archive->list, index, &card, LVIR_ICON);
    InflateRect(&card, -archive_scaled(archive, 4), -archive_scaled(archive, 4));
    SetDCBrushColor(dc, entry->paper); FillRect(dc, &card, brush);
    bar = card; bar.bottom = bar.top + archive_scaled(archive, 28);
    SetDCBrushColor(dc, RGB(GetRValue(entry->paper) * 94 / 100, GetGValue(entry->paper) * 94 / 100,
                            GetBValue(entry->paper) * 94 / 100));
    FillRect(dc, &bar, brush);
    if (ListView_GetItemState(archive->list, index, LVIS_SELECTED)) {
        RECT border = card;
        SetDCBrushColor(dc, GetSysColor(COLOR_HIGHLIGHT)); FrameRect(dc, &border, brush);
        InflateRect(&border, -1, -1); FrameRect(dc, &border, brush);
    }
    title = bar; InflateRect(&title, -archive_scaled(archive, 10), 0);
    body = card; InflateRect(&body, -archive_scaled(archive, 10), -archive_scaled(archive, 8));
    body.top = bar.bottom + archive_scaled(archive, 6);
    text = entry->preview ? entry->preview : L"Preview unavailable";
    after = text;
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, entry->ink);
    previous = SelectObject(dc, archive->bold);
    if (entry->titled && *text == L'#') {
        size_t heading = wcscspn(++text, L"\r\n"); after = text + heading;
        if (*after == L'\r') ++after;
        if (*after == L'\n') ++after;
        DrawTextW(dc, text, (int)heading, &title, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    SelectObject(dc, archive->font);
    DrawTextW(dc, after, -1, &body, DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, previous);
    return CDRF_SKIPDEFAULT;
}

static void archive_layout(Archive *archive) {
    RECT rect;
    int gap = archive_scaled(archive, 8), tree_width = archive_scaled(archive, 174);
    GetClientRect(archive->window, &rect);
    MoveWindow(archive->tree, gap, gap, tree_width, max(1, rect.bottom - gap * 2), TRUE);
    MoveWindow(archive->list, tree_width + gap * 2, gap,
               max(1, rect.right - tree_width - gap * 3), max(1, rect.bottom - gap * 2), TRUE);
    ListView_Arrange(archive->list, LVA_DEFAULT);
}

static void archive_fonts(Archive *archive) {
    HFONT old_font = archive->font, old_bold = archive->bold;
    archive->dpi = (int)GetDpiForWindow(archive->window);
    if (!archive->dpi) archive->dpi = 96;
    archive->font = CreateFontW(-MulDiv(10, archive->dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    archive->bold = CreateFontW(-MulDiv(11, archive->dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    SendMessageW(archive->tree, WM_SETFONT, (WPARAM)archive->font, TRUE);
    SendMessageW(archive->list, WM_SETFONT, (WPARAM)archive->font, TRUE);
    /* One shared image defines native tile geometry and hit testing. Content
       is drawn directly; there is no bitmap or child editor per post-it. */
    if (!archive->tiles) {
        archive->tiles = ImageList_Create(archive_scaled(archive, 220), archive_scaled(archive, 170), ILC_COLOR32, 1, 1);
        ListView_SetImageList(archive->list, archive->tiles, LVSIL_NORMAL);
    } else ImageList_SetIconSize(archive->tiles, archive_scaled(archive, 220), archive_scaled(archive, 170));
    ImageList_SetImageCount(archive->tiles, 1);
    ListView_SetIconSpacing(archive->list, archive_scaled(archive, 236), archive_scaled(archive, 196));
    if (old_font) DeleteObject(old_font);
    if (old_bold) DeleteObject(old_bold);
}

static void open_archive_item(Archive *archive, int index) {
    if (index >= 0 && index < archive->visible && !archive->actions.open(archive->entries[archive->first + index].id))
        MessageBoxW(archive->window, L"This note could not be opened.", L"PicoSlip", MB_OK | MB_ICONERROR);
}

static void delete_archive_selection(Archive *archive) {
    int index = -1, count = (int)ListView_GetSelectedCount(archive->list), failures = 0;
    wchar_t message[128];
    if (!count) return;
    swprintf_s(message, 128, L"Delete %d selected note%s from the archive?", count, count == 1 ? L"" : L"s");
    if (MessageBoxW(archive->window, message, L"PicoSlip", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES) return;
    while ((index = ListView_GetNextItem(archive->list, index, LVNI_SELECTED)) >= 0)
        if (!archive->actions.remove(archive->entries[archive->first + index].id)) ++failures;
    if (!refresh_archive(archive) || failures)
        MessageBoxW(archive->window, L"Some notes could not be deleted. Check that their files are writable.",
                    L"PicoSlip", MB_OK | MB_ICONERROR);
}

static void archive_context_menu(Archive *archive, POINT point) {
    HMENU menu;
    UINT command;
    if (!ListView_GetSelectedCount(archive->list)) return;
    menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, DELETE_NOTES, L"Delete from archive");
    command = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, archive->window, NULL);
    DestroyMenu(menu);
    if (command == DELETE_NOTES) delete_archive_selection(archive);
}

static LRESULT CALLBACK archive_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    Archive *archive = (Archive *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        archive = (Archive *)((CREATESTRUCTW *)lparam)->lpCreateParams;
        archive->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)archive);
    }
    if (!archive) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_CREATE: {
        HINSTANCE app = GetModuleHandleW(NULL);
        archive->tree = CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            0, 0, 1, 1, window, (HMENU)(INT_PTR)1, app, NULL);
        archive->list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            LVS_ICON | LVS_OWNERDATA | LVS_AUTOARRANGE | LVS_NOLABELWRAP | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS,
            0, 0, 1, 1, window, (HMENU)(INT_PTR)2, app, NULL);
        if (!archive->tree || !archive->list) return -1;
        ListView_SetExtendedListViewStyle(archive->list, LVS_EX_DOUBLEBUFFER);
        archive_fonts(archive);
        return 0;
    }
    case WM_SIZE: archive_layout(archive); return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *)lparam;
        info->ptMinTrackSize.x = archive_scaled(archive, 520);
        info->ptMinTrackSize.y = archive_scaled(archive, 320);
        return 0;
    }
    case WM_DPICHANGED: {
        RECT *rect = (RECT *)lparam;
        archive_fonts(archive);
        SetWindowPos(window, NULL, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR *header = (NMHDR *)lparam;
        if (header->hwndFrom == archive->tree && header->code == TVN_SELCHANGEDW && !archive->rebuilding)
            filter_archive(archive, (unsigned)((NMTREEVIEWW *)header)->itemNew.lParam);
        if (header->hwndFrom != archive->list) return 0;
        switch (header->code) {
        case NM_CUSTOMDRAW: {
            NMLVCUSTOMDRAW *draw = (NMLVCUSTOMDRAW *)header;
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) return draw_preview(archive, draw);
            break;
        }
        case LVN_GETDISPINFOW: {
            NMLVDISPINFOW *info = (NMLVDISPINFOW *)header;
            if (info->item.mask & LVIF_IMAGE) info->item.iImage = 0;
            if ((info->item.mask & LVIF_TEXT) && info->item.iItem >= 0 && info->item.iItem < archive->visible) {
                ArchiveEntry *entry = &archive->entries[archive->first + info->item.iItem];
                const wchar_t *text;
                size_t length;
                if (!entry->loaded) load_preview(archive, entry);
                text = entry->preview ? entry->preview : L"Preview unavailable";
                if (entry->titled && *text == L'#') ++text;
                length = wcscspn(text, L"\r\n");
                if (info->item.cchTextMax > 0)
                    wcsncpy_s(info->item.pszText, (size_t)info->item.cchTextMax, text,
                              min(length, (size_t)info->item.cchTextMax - 1));
            }
            return 0;
        }
        case LVN_GETEMPTYMARKUP:
            wcscpy_s(((NMLVEMPTYMARKUP *)header)->szMarkup, L_MAX_URL_LENGTH, L"No notes"); return TRUE;
        case NM_CLICK:
            if (GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_SHIFT) >= 0)
                open_archive_item(archive, ((NMITEMACTIVATE *)header)->iItem);
            return 0;
        case NM_RCLICK: {
            POINT point;
            GetCursorPos(&point); archive_context_menu(archive, point); return 0;
        }
        case LVN_KEYDOWN: {
            WORD key = ((NMLVKEYDOWN *)header)->wVKey;
            if (key == VK_RETURN) open_archive_item(archive, ListView_GetNextItem(archive->list, -1, LVNI_FOCUSED));
            if (key == VK_DELETE) delete_archive_selection(archive);
            if (key == VK_F5 && !refresh_archive(archive))
                MessageBoxW(window, L"The archive could not be refreshed.", L"PicoSlip", MB_OK | MB_ICONERROR);
            if (key == 'A' && GetKeyState(VK_CONTROL) < 0)
                ListView_SetItemState(archive->list, -1, LVIS_SELECTED, LVIS_SELECTED);
            return 0;
        }
        }
        return 0;
    }
    case WM_CONTEXTMENU:
        if ((HWND)wparam == archive->list && GET_X_LPARAM(lparam) == -1 && GET_Y_LPARAM(lparam) == -1) {
            RECT rect;
            int selected = ListView_GetNextItem(archive->list, -1, LVNI_SELECTED);
            if (selected >= 0 && ListView_GetItemRect(archive->list, selected, &rect, LVIR_BOUNDS)) {
                POINT point = {rect.left + archive_scaled(archive, 16), rect.top + archive_scaled(archive, 16)};
                ClientToScreen(archive->list, &point); archive_context_menu(archive, point);
            }
            return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_NCDESTROY:
        if (archive->tiles) ImageList_Destroy(archive->tiles);
        if (archive->font) DeleteObject(archive->font);
        if (archive->bold) DeleteObject(archive->bold);
        free_entries(archive->entries, archive->count);
        archive_window = NULL;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0); free(archive);
        return DefWindowProcW(window, message, wparam, lparam);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

HWND archive_show(HWND owner, const wchar_t *directory, const wchar_t *settings, const ArchiveActions *actions) {
    static ATOM window_class;
    Archive *archive;
    MONITORINFO monitor = {sizeof(monitor)};
    int dpi, width, height, x, y;
    if (archive_window) {
        archive = (Archive *)GetWindowLongPtrW(archive_window, GWLP_USERDATA);
        if (!refresh_archive(archive)) return NULL;
        ShowWindow(archive_window, SW_RESTORE); SetForegroundWindow(archive_window); return archive_window;
    }
    if (!window_class) {
        WNDCLASSW cls = {0};
        INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES};
        if (!InitCommonControlsEx(&controls)) return NULL;
        cls.lpfnWndProc = archive_proc; cls.hInstance = GetModuleHandleW(NULL);
        cls.hIcon = (HICON)GetClassLongPtrW(owner, GCLP_HICON);
        cls.hCursor = LoadCursorW(NULL, IDC_ARROW); cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        cls.lpszClassName = ARCHIVE_CLASS;
        window_class = RegisterClassW(&cls);
        if (!window_class) return NULL;
    }
    archive = (Archive *)calloc(1, sizeof(*archive));
    if (!archive) return NULL;
    archive->directory = directory; archive->settings = settings; archive->actions = *actions;
    dpi = (int)GetDpiForWindow(owner); if (!dpi) dpi = 96;
    archive->dpi = dpi;
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor);
    width = min(MulDiv(800, dpi, 96), monitor.rcWork.right - monitor.rcWork.left);
    height = min(MulDiv(580, dpi, 96), monitor.rcWork.bottom - monitor.rcWork.top);
    x = monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2;
    y = monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2;
    archive_window = CreateWindowExW(WS_EX_TOPMOST, ARCHIVE_CLASS, L"Notes archive",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, width, height, owner, NULL, GetModuleHandleW(NULL), archive);
    /* WM_NCDESTROY owns the allocation once WM_NCCREATE succeeds. */
    if (!archive_window) return NULL;
    if (!refresh_archive(archive)) { archive_close(); return NULL; }
    ShowWindow(archive_window, SW_SHOWNORMAL); SetForegroundWindow(archive_window);
    SetFocus(archive->list);
    return archive_window;
}

void archive_close(void) {
    if (archive_window) DestroyWindow(archive_window);
}
