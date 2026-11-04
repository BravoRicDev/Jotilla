/* Native, virtual archive view. Only visible previews are read and cached. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <wchar.h>
#include "archive.h"
#include "colors.h"

#define ARCHIVE_CLASS L"PicoSlip.Archive"
#define ARCHIVE_PATH_CAP 1024
#define DELETE_NOTES 1
#define ARCHIVE_REFRESH (WM_APP + 1)
#define ARCHIVE_PAPER RGB(244,238,226)
#define ARCHIVE_SIDEBAR RGB(235,226,209)
#define ARCHIVE_INK RGB(58,52,43)
#define ARCHIVE_SELECTION RGB(211,194,162)

static const wchar_t *MONTH_NAMES[] = {
    L"January", L"February", L"March", L"April", L"May", L"June",
    L"July", L"August", L"September", L"October", L"November", L"December"
};

typedef struct ArchiveEntry {
    wchar_t id[80];
    wchar_t *preview;
    unsigned date;
    UINT state;
    COLORREF paper, ink;
    BOOL loaded, titled;
} ArchiveEntry;

typedef struct Archive {
    HWND window, tree, list;
    HFONT font, bold;
    HBRUSH background;
    HIMAGELIST tiles;
    ArchiveEntry *entries;
    int count, first, visible, dpi;
    unsigned filter;
    const wchar_t *directory, *settings;
    ArchiveActions actions;
    BOOL rebuilding, refresh_pending;
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

static unsigned date_from_id(const wchar_t *id) {
    unsigned year, month, day;
    return swscanf_s(id + 5, L"%4u-%2u-%2u", &year, &month, &day) == 3 &&
        year >= 1601 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 && day <= 31 ?
        year * 10000 + month * 100 + day : 0;
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
        entry->date = date_from_id(entry->id);
        if (!entry->date && FileTimeToLocalFileTime(&found.ftCreationTime, &local) && FileTimeToSystemTime(&local, &time))
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
    ListView_SetItemCountEx(archive->list, archive->visible, LVSICF_NOSCROLL);
    ListView_Arrange(archive->list, LVA_DEFAULT);
    InvalidateRect(archive->list, NULL, TRUE);
}

static HTREEITEM date_node(Archive *archive, HTREEITEM parent, HTREEITEM after, const wchar_t *label, unsigned date) {
    TVINSERTSTRUCTW insert = {0};
    insert.hParent = parent; insert.hInsertAfter = after;
    insert.item.mask = TVIF_TEXT | TVIF_PARAM;
    insert.item.pszText = (wchar_t *)label; insert.item.lParam = date;
    return TreeView_InsertItem(archive->tree, &insert);
}

static unsigned node_date(HWND tree, HTREEITEM node) {
    TVITEMW item = {0};
    item.mask = TVIF_PARAM; item.hItem = node;
    TreeView_GetItem(tree, &item);
    return (unsigned)item.lParam;
}

/* Merge sorted date groups into the native tree. Unchanged nodes keep their
   handles, expansion state and scroll position; recursion is only three levels. */
static void sync_dates(Archive *archive, HTREEITEM parent, int first, int end,
                       unsigned unit, HTREEITEM *selected) {
    HTREEITEM node = TreeView_GetChild(archive->tree, parent), previous = TVI_FIRST;
    while (first < end) {
        unsigned date = archive->entries[first].date / unit * unit;
        int next = first + 1;
        wchar_t label[32];
        while (next < end && archive->entries[next].date / unit * unit == date) ++next;
        while (node && node_date(archive->tree, node) > date) {
            HTREEITEM removed = node;
            node = TreeView_GetNextSibling(archive->tree, node);
            TreeView_DeleteItem(archive->tree, removed);
        }
        if (!node || node_date(archive->tree, node) != date) {
            unsigned month = date / 100 % 100;
            swprintf_s(label, 32, unit == 10000 ? L"%04u" : L"%02u", unit == 10000 ? date / unit : date % 100);
            node = date_node(archive, parent, previous,
                unit == 100 && month >= 1 && month <= 12 ? MONTH_NAMES[month - 1] : label, date);
        }
        if (archive->filter >= date && archive->filter - date < unit) *selected = node;
        if (unit > 1) sync_dates(archive, node, first, next, unit / 100, selected);
        previous = node; node = TreeView_GetNextSibling(archive->tree, node); first = next;
    }
    while (node) {
        HTREEITEM removed = node;
        node = TreeView_GetNextSibling(archive->tree, node);
        TreeView_DeleteItem(archive->tree, removed);
    }
}

static BOOL refresh_archive(Archive *archive, BOOL reload) {
    ArchiveEntry *entries;
    int count, i, old = 0;
    HTREEITEM root = TreeView_GetRoot(archive->tree), selected;
    TVITEMW item = {0};
    wchar_t label[64];
    BOOL initial = root == NULL;
    archive->refresh_pending = FALSE;
    archive->rebuilding = TRUE;
    if (!archive->actions.save() || !scan_archive(archive, &entries, &count)) {
        archive->rebuilding = FALSE; return FALSE;
    }
    /* Keep cached previews and selected notes across additions/deletions.
       Explicit refresh also rereads files edited outside PicoSlip. */
    for (i = 0; i < count; ++i) {
        while (old < archive->count && compare_entries(&archive->entries[old], &entries[i]) < 0) ++old;
        if (old < archive->count && !compare_entries(&archive->entries[old], &entries[i])) {
            if (!reload) {
                entries[i] = archive->entries[old];
                archive->entries[old].preview = NULL;
            }
            entries[i].state = old >= archive->first && old < archive->first + archive->visible ?
                ListView_GetItemState(archive->list, old - archive->first, LVIS_SELECTED | LVIS_FOCUSED) : 0;
        }
    }
    SendMessageW(archive->tree, WM_SETREDRAW, FALSE, 0);
    free_entries(archive->entries, archive->count);
    archive->entries = entries; archive->count = count;
    swprintf_s(label, 64, L"All notes (%d)", count);
    if (!root) root = date_node(archive, TVI_ROOT, TVI_LAST, label, 0);
    else {
        item.mask = TVIF_TEXT; item.hItem = root; item.pszText = label;
        TreeView_SetItem(archive->tree, &item);
    }
    selected = root;
    sync_dates(archive, root, 0, count, 10000, &selected);
    if (initial) {
        HTREEITEM latest = TreeView_GetChild(archive->tree, root);
        TreeView_Expand(archive->tree, root, TVE_EXPAND);
        if (latest) {
            TreeView_Expand(archive->tree, latest, TVE_EXPAND);
            TreeView_Expand(archive->tree, TreeView_GetChild(archive->tree, latest), TVE_EXPAND);
        }
    }
    TreeView_SelectItem(archive->tree, selected);
    filter_archive(archive, node_date(archive->tree, selected));
    for (i = 0; i < archive->visible; ++i)
        if (entries[archive->first + i].state)
            ListView_SetItemState(archive->list, i, entries[archive->first + i].state, LVIS_SELECTED | LVIS_FOCUSED);
    archive->rebuilding = FALSE;
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
    SetDCBrushColor(dc, RGB(GetRValue(entry->paper) * 85 / 100, GetGValue(entry->paper) * 85 / 100,
                            GetBValue(entry->paper) * 85 / 100));
    FrameRect(dc, &card, brush);
    if (ListView_GetItemState(archive->list, index, LVIS_SELECTED)) {
        RECT border = card;
        SetDCBrushColor(dc, ARCHIVE_INK); FrameRect(dc, &border, brush);
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
    if (!refresh_archive(archive, FALSE) || failures)
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
        COLORREF caption = ARCHIVE_PAPER, ink = ARCHIVE_INK;
        archive->background = CreateSolidBrush(ARCHIVE_PAPER);
        if (!archive->background) return -1;
        DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
        DwmSetWindowAttribute(window, DWMWA_TEXT_COLOR, &ink, sizeof(ink));
        archive->tree = CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            0, 0, 1, 1, window, (HMENU)(INT_PTR)1, app, NULL);
        archive->list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            LVS_ICON | LVS_OWNERDATA | LVS_AUTOARRANGE | LVS_NOLABELWRAP | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS,
            0, 0, 1, 1, window, (HMENU)(INT_PTR)2, app, NULL);
        if (!archive->tree || !archive->list) return -1;
        SetWindowTheme(archive->tree, L"", L"");
        TreeView_SetBkColor(archive->tree, ARCHIVE_SIDEBAR);
        TreeView_SetTextColor(archive->tree, ARCHIVE_INK);
        TreeView_SetLineColor(archive->tree, RGB(190,177,151));
        ListView_SetBkColor(archive->list, ARCHIVE_PAPER);
        ListView_SetTextBkColor(archive->list, ARCHIVE_PAPER);
        ListView_SetTextColor(archive->list, ARCHIVE_INK);
        ListView_SetExtendedListViewStyle(archive->list, LVS_EX_DOUBLEBUFFER);
        archive_fonts(archive);
        return 0;
    }
    case WM_ERASEBKGND: {
        RECT rect;
        GetClientRect(window, &rect);
        FillRect((HDC)wparam, &rect, archive->background);
        return 1;
    }
    case WM_SIZE: archive_layout(archive); return 0;
    case ARCHIVE_REFRESH:
        if (archive->refresh_pending) refresh_archive(archive, FALSE);
        return 0;
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
        if (header->hwndFrom == archive->tree) {
            if (header->code == TVN_SELCHANGEDW && !archive->rebuilding)
                filter_archive(archive, (unsigned)((NMTREEVIEWW *)header)->itemNew.lParam);
            if (header->code == NM_CUSTOMDRAW) {
                NMTVCUSTOMDRAW *draw = (NMTVCUSTOMDRAW *)header;
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    draw->clrText = ARCHIVE_INK;
                    draw->clrTextBk = (HTREEITEM)draw->nmcd.dwItemSpec == TreeView_GetSelection(archive->tree) ?
                        ARCHIVE_SELECTION : ARCHIVE_SIDEBAR;
                    draw->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS);
                    return CDRF_NEWFONT;
                }
            }
            return 0;
        }
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
            if (key == VK_F5 && !refresh_archive(archive, TRUE))
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
        if (archive->background) DeleteObject(archive->background);
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
        if (!refresh_archive(archive, TRUE)) return NULL;
        ShowWindow(archive_window, SW_RESTORE); SetForegroundWindow(archive_window); return archive_window;
    }
    if (!window_class) {
        WNDCLASSW cls = {0};
        INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES};
        if (!InitCommonControlsEx(&controls)) return NULL;
        cls.lpfnWndProc = archive_proc; cls.hInstance = GetModuleHandleW(NULL);
        cls.hIcon = (HICON)GetClassLongPtrW(owner, GCLP_HICON);
        cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
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
    if (!refresh_archive(archive, TRUE)) { archive_close(); return NULL; }
    ShowWindow(archive_window, SW_SHOWNORMAL); SetForegroundWindow(archive_window);
    SetFocus(archive->list);
    return archive_window;
}

/* Called only after a successful save/removal, on the UI thread. Editing
   invalidates one lazy preview; membership changes share one queued refresh. */
void archive_changed(const wchar_t *id, BOOL present) {
    Archive *archive;
    ArchiveEntry key = {0}, *entry = NULL;
    int i;
    if (!archive_window) return;
    archive = (Archive *)GetWindowLongPtrW(archive_window, GWLP_USERDATA);
    wcscpy_s(key.id, 80, id); key.date = date_from_id(id);
    if (key.date && archive->count)
        entry = (ArchiveEntry *)bsearch(&key, archive->entries, (size_t)archive->count, sizeof(key), compare_entries);
    else if (!key.date) /* Legacy filenames use the file's creation date. */
        for (i = 0; i < archive->count; ++i)
            if (!wcscmp(archive->entries[i].id, id)) { entry = &archive->entries[i]; break; }
    if (entry && present) {
        i = (int)(entry - archive->entries);
        free(entry->preview); entry->preview = NULL; entry->loaded = FALSE;
        if (i >= archive->first && i < archive->first + archive->visible)
            ListView_RedrawItems(archive->list, i - archive->first, i - archive->first);
        return;
    }
    if (!entry && !present) return;
    if (!archive->rebuilding && !archive->refresh_pending)
        archive->refresh_pending = PostMessageW(archive_window, ARCHIVE_REFRESH, 0, 0);
}

void archive_close(void) {
    if (archive_window) DestroyWindow(archive_window);
}
