#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "resource.h"

#define APP_CLASS APP_CONTROLLER
#define NOTE_CLASS APP_NOTE
#define PATH_CAP 1024
#define TEXT_LIMIT (16 * 1024 * 1024)
#define SAVE_TIMER 1
#define TRAY_MESSAGE (WM_APP + 1)
#define NEW_NOTE_MESSAGE (WM_APP + 2)
#define EXIT_COMMAND 1
#define NOTE_WIDTH 300
#define NOTE_HEIGHT 240
#define CMD_ARCHIVE 100
#define CMD_TITLE 101
#define CMD_OPACITY 200
#define CMD_SIZE 300
#define CMD_PALETTE 400

typedef struct Note Note;
struct Note {
    Note *next;
    HWND window, edit;
    WNDPROC edit_proc;
    HBRUSH brush;
    HFONT font;
    HPEN pen;
    wchar_t *accepted_text, *title;
    size_t text_capacity;
    wchar_t id[80], path[PATH_CAP];
    int color, palette, size, opacity, dpi, line_height, max_lines, hover, pressed;
    int saved_layout[9];
    DWORD selection_start, selection_end;
    BOOL collapsed, ready, dirty, save_failed, validating, layout_saved;
};
#define COLOR_COUNT 6

static const COLORREF PALETTES[3][6] = {
    {RGB(255,238,151), RGB(199,229,255), RGB(213,239,187), RGB(255,210,218), RGB(229,214,255), RGB(255,222,180)},
    {RGB(255,255,255), RGB(28,28,28), RGB(255,255,255), RGB(28,28,28), RGB(255,255,255), RGB(28,28,28)},
    {RGB(255,238,151), RGB(255,224,112), RGB(255,245,184), RGB(250,218,128), RGB(255,239,105), RGB(255,250,205)}
};
static const SIZE NOTE_SIZES[] = {{240,180}, {NOTE_WIDTH,NOTE_HEIGHT}, {420,340}, {540,440}, {300,300}};
static const wchar_t *SIZE_NAMES[] = {L"Small", L"Medium", L"Large", L"Extra large", L"Square"};
#define SIZE_COUNT ((int)(sizeof(NOTE_SIZES)/sizeof(NOTE_SIZES[0])))
static const wchar_t *PALETTE_NAMES[] = {L"Pastel", L"Black and white", L"Yellow only"};
static const int OPACITIES[] = {100,90,80,70,60};
static HINSTANCE instance;
static HWND controller;
static HICON app_icon;
static Note *notes;
static wchar_t directory[PATH_CAP], state_path[PATH_CAP];
static UINT taskbar_created;
static int next_color;
static int current_palette;
static BOOL testing;

static int scaled(const Note *note, int value) { return MulDiv(value, note->dpi, 96); }
static int bar_height(const Note *note) { return scaled(note, 32); }
static int note_width(const Note *note) { return scaled(note, NOTE_SIZES[note->size].cx); }
static int expanded_height(const Note *note) { return scaled(note, NOTE_SIZES[note->size].cy); }
static int note_height(const Note *note) { return note->collapsed ? bar_height(note) : expanded_height(note); }
static COLORREF note_color(const Note *note) { return PALETTES[note->palette][note->color]; }
static COLORREF ink_color(const Note *note) {
    return note->palette == 1 && (note->color & 1) ? RGB(255,255,255) : RGB(0,0,0);
}

static void redraw_bar(Note *note) {
    RECT rect = {0, 0, note_width(note), bar_height(note)};
    InvalidateRect(note->window, &rect, FALSE);
}

static void mark_dirty(Note *note) {
    note->dirty = TRUE;
    SetTimer(note->window, SAVE_TIMER, 400, NULL);
}

static void report_error(HWND owner, const wchar_t *message) {
    if (!testing) MessageBoxW(owner, message, L"PicoSlip", MB_OK | MB_ICONERROR);
}

static BOOL make_paths(const wchar_t *root) {
    if (swprintf_s(directory, PATH_CAP, L"%s", root) < 0 ||
        swprintf_s(state_path, PATH_CAP, L"%s\\notes.ini", root) < 0) return FALSE;
    if (!CreateDirectoryW(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return FALSE;
    return (GetFileAttributesW(directory) & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static BOOL write_setting(const wchar_t *section, const wchar_t *key, int value) {
    wchar_t number[32];
    swprintf_s(number, 32, L"%d", value);
    return WritePrivateProfileStringW(section, key, number, state_path);
}

static int setting(const wchar_t *section, const wchar_t *key, int fallback) {
    return (int)GetPrivateProfileIntW(section, key, fallback, state_path);
}

static BOOL save_layout(Note *note, BOOL open) {
    static const wchar_t *keys[] = {L"x", L"y", L"color", L"has_title", L"collapsed", L"open", L"palette", L"size", L"opacity"};
    RECT rect;
    int values[9], i;
    if (!GetWindowRect(note->window, &rect)) return FALSE;
    values[0] = rect.left; values[1] = rect.top; values[2] = note->color;
    values[3] = note->title != NULL; values[4] = note->collapsed; values[5] = open;
    values[6] = note->palette; values[7] = note->size; values[8] = note->opacity;
    for (i = 0; i < 9; ++i)
        if ((!note->layout_saved || values[i] != note->saved_layout[i]) &&
            !write_setting(note->id, keys[i], values[i])) return FALSE;
    memcpy(note->saved_layout, values, sizeof(values));
    note->layout_saved = TRUE;
    return TRUE;
}

/* Replace a complete UTF-8 snapshot on the same volume. A failed write never
   truncates the last saved note. The temporary file stays beside its .txt. */
static BOOL save_text(Note *note) {
    int bytes = 0, converted;
    size_t body_length, title_length, capacity;
    wchar_t temporary[PATH_CAP];
    char *utf8;
    HANDLE file;
    DWORD written;
    BOOL ok;
    if (!note->dirty) return TRUE;
    body_length = wcslen(note->accepted_text);
    title_length = note->title ? wcslen(note->title) : 0;
    /* At most three UTF-8 bytes per UTF-16 code unit, plus # and CRLF.
       Convert each part once, directly from the accepted editor snapshot. */
    capacity = (body_length + title_length) * 3 + 3;
    utf8 = (char *)malloc(capacity);
    if (!utf8) return FALSE;
    if (note->title) {
        utf8[bytes++] = '#';
        converted = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, note->title, (int)title_length,
                                        utf8 + bytes, (int)capacity - bytes, NULL, NULL);
        if (title_length && !converted) { free(utf8); return FALSE; }
        bytes += converted;
        utf8[bytes++] = '\r'; utf8[bytes++] = '\n';
    }
    converted = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, note->accepted_text, (int)body_length,
                                    utf8 + bytes, (int)capacity - bytes, NULL, NULL);
    if (body_length && !converted) { free(utf8); return FALSE; }
    bytes += converted;
    if (swprintf_s(temporary, PATH_CAP, L"%s.tmp", note->path) < 0) { free(utf8); return FALSE; }
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) { free(utf8); return FALSE; }
    ok = WriteFile(file, utf8, (DWORD)bytes, &written, NULL) && written == (DWORD)bytes;
    free(utf8);
    if (ok) ok = FlushFileBuffers(file);
    if (!CloseHandle(file)) ok = FALSE;
    if (ok) ok = MoveFileExW(temporary, note->path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (ok) note->dirty = FALSE;
    return ok;
}

static BOOL save_note(Note *note) {
    BOOL previous_failure = note->save_failed;
    BOOL ok = save_text(note);
    if (ok) ok = save_layout(note, TRUE);
    note->save_failed = !ok;
    if (ok) KillTimer(note->window, SAVE_TIMER);
    else SetTimer(note->window, SAVE_TIMER, 1000, NULL);
    if (previous_failure != note->save_failed) redraw_bar(note);
    return ok;
}

static void destroy_note(Note *note) {
    /* Destroying the focused EDIT sends WM_KILLFOCUS. Do not let that save
       mark an explicitly closed (archived) note as open again. */
    note->ready = FALSE;
    DestroyWindow(note->window);
}

static BOOL load_text(Note *note) {
    HANDLE file;
    LARGE_INTEGER size;
    DWORD read;
    char *data;
    wchar_t *text;
    int offset = 0, count;
    BOOL ok;
    file = CreateFileW(note->path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    if (!GetFileSizeEx(file, &size) || size.QuadPart > TEXT_LIMIT * 4LL || size.QuadPart < 0) {
        CloseHandle(file); return FALSE;
    }
    data = (char *)malloc((size_t)size.QuadPart + 1);
    if (!data) { CloseHandle(file); return FALSE; }
    ok = ReadFile(file, data, (DWORD)size.QuadPart, &read, NULL) && read == (DWORD)size.QuadPart;
    CloseHandle(file);
    if (!ok) { free(data); return FALSE; }
    if (read >= 3 && (unsigned char)data[0] == 0xef && (unsigned char)data[1] == 0xbb &&
        (unsigned char)data[2] == 0xbf) offset = 3;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data + offset, (int)read - offset, NULL, 0);
    if (((int)read > offset && !count) || count > TEXT_LIMIT) { free(data); return FALSE; }
    text = (wchar_t *)calloc((size_t)count + 1, sizeof(wchar_t));
    if (!text) { free(data); return FALSE; }
    if (count) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data + offset, (int)read - offset, text, count);
    free(data);
    /* This flag records an explicit Enter gesture. A literal #first line
       entered with Shift+Enter must never become a title just by reopening. */
    if (setting(note->id, L"has_title", FALSE) && text[0] == L'#') {
        size_t end = wcscspn(text, L"\r\n"), body = end;
        if (end > 1) {
            note->title = (wchar_t *)calloc(end, sizeof(wchar_t));
            if (!note->title) { free(text); return FALSE; }
            wmemcpy(note->title, text + 1, end - 1);
            if (text[body] == L'\r') ++body;
            if (text[body] == L'\n') ++body;
            memmove(text, text + body, (wcslen(text + body) + 1) * sizeof(wchar_t));
        }
    }
    ok = SetWindowTextW(note->edit, text);
    if (ok) {
        note->accepted_text = text;
        note->text_capacity = (size_t)count + 1;
    } else free(text);
    return ok;
}

static void update_style(Note *note) {
    HFONT old_font = note->font;
    HPEN old_pen = note->pen;
    TEXTMETRICW metrics;
    HDC dc;
    HGDIOBJ old;
    note->dpi = (int)GetDpiForWindow(note->window);
    if (!note->dpi) note->dpi = 96;
    note->font = CreateFontW(-MulDiv(11, note->dpi, 72), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    SendMessageW(note->edit, WM_SETFONT, (WPARAM)note->font, TRUE);
    dc = GetDC(note->edit); old = SelectObject(dc, note->font);
    GetTextMetricsW(dc, &metrics);
    note->line_height = metrics.tmHeight;
    SelectObject(dc, old); ReleaseDC(note->edit, dc);
    note->pen = CreatePen(PS_SOLID, max(1, scaled(note, 1)), ink_color(note));
    if (old_font) DeleteObject(old_font);
    if (old_pen) DeleteObject(old_pen);
}

static BOOL text_fits(Note *note) {
    return (int)SendMessageW(note->edit, EM_GETLINECOUNT, 0, 0) <= note->max_lines;
}

static BOOL remember_text(Note *note) {
    int length = GetWindowTextLengthW(note->edit);
    if ((size_t)length + 1 > note->text_capacity) {
        size_t capacity = max((size_t)length + 1, max(note->text_capacity * 2, 128));
        wchar_t *copy = (wchar_t *)realloc(note->accepted_text, capacity * sizeof(wchar_t));
        if (!copy) return FALSE;
        note->accepted_text = copy;
        note->text_capacity = capacity;
    }
    GetWindowTextW(note->edit, note->accepted_text, length + 1);
    return TRUE;
}

static BOOL copy_title(const wchar_t *text,size_t end,wchar_t **result) {
    size_t start=0;
    *result=NULL;
    while (start<end && (text[start]==L' ' || text[start]==L'\t')) ++start;
    while (end>start && (text[end-1]==L' ' || text[end-1]==L'\t')) --end;
    if (end>start) {
        *result=(wchar_t *)calloc(end-start+1,sizeof(wchar_t));
        if (!*result) return FALSE;
        wmemcpy(*result,text+start,end-start);
    }
    return TRUE;
}

static BOOL promote_title(Note *note) {
    const wchar_t *text = note->accepted_text;
    wchar_t *title;
    size_t end, after;
    DWORD selection_start, selection_end;
    SendMessageW(note->edit, EM_GETSEL, (WPARAM)&selection_start, (LPARAM)&selection_end);
    if (selection_start != selection_end || text[0] != L'#') return FALSE;
    end = wcscspn(text, L"\r\n");
    if (selection_start != end) return FALSE;
    if (!copy_title(text+1,end-1,&title) || !title) return FALSE;
    after = end;
    if (text[after] == L'\r') ++after;
    if (text[after] == L'\n') ++after;
    note->validating = TRUE;
    if (!SetWindowTextW(note->edit, text + after)) {
        note->validating = FALSE; free(title); return FALSE;
    }
    note->validating = FALSE;
    free(note->title); note->title = title;
    memmove(note->accepted_text, text + after, (wcslen(text + after) + 1) * sizeof(wchar_t));
    SendMessageW(note->edit, EM_SETSEL, 0, 0);
    mark_dirty(note);
    redraw_bar(note);
    return TRUE;
}

static void layout(Note *note) {
    RECT rect;
    int padding = scaled(note, 10), bar = bar_height(note);
    /* A collapsed card keeps the same editor dimensions and text capacity. */
    MoveWindow(note->edit, padding, bar + scaled(note, 6), note_width(note) - padding * 2,
               expanded_height(note) - bar - scaled(note, 16), TRUE);
    SendMessageW(note->edit, EM_GETRECT, 0, (LPARAM)&rect);
    note->max_lines = note->line_height > 0 ? (rect.bottom - rect.top) / note->line_height : 0;
    ShowWindow(note->edit, note->collapsed ? SW_HIDE : SW_SHOW);
    InvalidateRect(note->window, NULL, FALSE);
}

static void toggle_note(Note *note) {
    RECT rect;
    GetWindowRect(note->window, &rect);
    note->collapsed = !note->collapsed;
    SetWindowPos(note->window, NULL, 0, 0, rect.right - rect.left,
                 note_height(note),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    save_note(note);
    if (!note->collapsed && !testing) SetFocus(note->edit);
}

static int button_at(Note *note, LPARAM position) {
    RECT rect;
    int x = GET_X_LPARAM(position), y = GET_Y_LPARAM(position), bar = bar_height(note);
    GetClientRect(note->window, &rect);
    if (y < 0 || y >= bar || x < rect.right - bar * 2 || x >= rect.right) return 0;
    return x >= rect.right - bar ? 2 : 1;
}

static void paint_note(Note *note) {
    PAINTSTRUCT ps;
    RECT rect, bar_rect, button;
    HDC dc = BeginPaint(note->window, &ps);
    COLORREF color = note_color(note);
    HBRUSH dc_brush = (HBRUSH)GetStockObject(DC_BRUSH);
    HGDIOBJ old_pen = SelectObject(dc, note->pen);
    int bar = bar_height(note), i;
    GetClientRect(note->window, &rect);
    FillRect(dc, &rect, note->brush);
    bar_rect = rect; bar_rect.bottom = bar;
    SetDCBrushColor(dc, note->save_failed ? RGB(235, 161, 151) :
        RGB(GetRValue(color) * 94 / 100, GetGValue(color) * 94 / 100, GetBValue(color) * 94 / 100));
    FillRect(dc, &bar_rect, dc_brush);
    if (note->title) {
        RECT title_rect = {scaled(note, 10), 0, rect.right - bar * 2 - scaled(note, 6), bar};
        HGDIOBJ old_font = SelectObject(dc, note->font);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, ink_color(note));
        DrawTextW(dc, note->title, -1, &title_rect,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, old_font);
    }
    for (i = 1; i <= 2; ++i) {
        int cx = rect.right - (i == 1 ? bar * 3 / 2 : bar / 2), cy = bar / 2, s = scaled(note, 4);
        if (note->hover == i) {
            SetDCBrushColor(dc, i == 2 ? RGB(235, 161, 151) : color);
            button.left = cx - bar / 2; button.right = button.left + bar; button.top = 0; button.bottom = bar;
            FillRect(dc, &button, dc_brush);
        }
        if (i == 2) {
            MoveToEx(dc, cx - s, cy - s, NULL); LineTo(dc, cx + s + 1, cy + s + 1);
            MoveToEx(dc, cx + s, cy - s, NULL); LineTo(dc, cx - s - 1, cy + s + 1);
        } else if (note->collapsed) {
            HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, cx - s - 1, cy - s, cx + s + 2, cy + s + 1);
            SelectObject(dc, old_brush);
        } else {
            MoveToEx(dc, cx - s - 1, cy, NULL); LineTo(dc, cx + s + 2, cy);
        }
    }
    SelectObject(dc, old_pen);
    EndPaint(note->window, &ps);
}

static LRESULT CALLBACK edit_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    Note *note = (Note *)GetWindowLongPtrW(GetParent(window), GWLP_USERDATA);
    if (note->ready && !note->validating && (message == WM_CHAR || message == WM_KEYDOWN ||
        message == WM_PASTE || message == WM_CUT || message == WM_CLEAR || message == WM_UNDO ||
        message == EM_REPLACESEL || message == WM_SETTEXT))
        SendMessageW(window, EM_GETSEL, (WPARAM)&note->selection_start, (LPARAM)&note->selection_end);
    if (message == WM_KEYDOWN && wparam == 'A' && GetKeyState(VK_CONTROL) < 0) {
        SendMessageW(window, EM_SETSEL, 0, -1); return 0;
    }
    if (message == WM_CHAR && wparam == L'\r' && note->ready && GetKeyState(VK_SHIFT) >= 0 &&
        promote_title(note)) return 0;
    if (message == WM_KILLFOCUS && note->ready) save_note(note);
    return CallWindowProcW(note->edit_proc, window, message, wparam, lparam);
}

static BOOL set_title(Note *note, const wchar_t *text) {
    wchar_t *title;
    if (!copy_title(text,wcscspn(text,L"\r\n"),&title)) return FALSE;
    free(note->title); note->title = title;
    mark_dirty(note); redraw_bar(note);
    return TRUE;
}

static INT_PTR CALLBACK title_dialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    Note *note = (Note *)GetWindowLongPtrW(dialog, DWLP_USER);
    if (message == WM_INITDIALOG) {
        RECT owner, rect;
        MONITORINFO monitor = {sizeof(monitor)};
        int x,y;
        note = (Note *)lparam;
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        SetDlgItemTextW(dialog, IDC_TITLE, note->title ? note->title : L"");
        SendDlgItemMessageW(dialog, IDC_TITLE, EM_SETLIMITTEXT, TEXT_LIMIT, 0);
        SendDlgItemMessageW(dialog, IDC_TITLE, EM_SETSEL, 0, -1);
        GetWindowRect(note->window, &owner); GetWindowRect(dialog, &rect);
        GetMonitorInfoW(MonitorFromWindow(note->window,MONITOR_DEFAULTTONEAREST),&monitor);
        x=owner.left+(note_width(note)-(rect.right-rect.left))/2;
        y=owner.top+bar_height(note);
        x=max(monitor.rcWork.left,min(x,monitor.rcWork.right-(rect.right-rect.left)));
        y=max(monitor.rcWork.top,min(y,monitor.rcWork.bottom-(rect.bottom-rect.top)));
        SetWindowPos(dialog, HWND_TOPMOST, x,y,0,0,SWP_NOSIZE);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wparam) == IDOK) {
        HWND edit = GetDlgItem(dialog, IDC_TITLE);
        int length = GetWindowTextLengthW(edit);
        wchar_t *text = (wchar_t *)calloc((size_t)length + 1, sizeof(wchar_t));
        if (!text) return TRUE;
        GetWindowTextW(edit, text, length + 1);
        if (set_title(note, text)) { save_note(note); EndDialog(dialog, IDOK); }
        free(text); return TRUE;
    }
    if ((message == WM_COMMAND && LOWORD(wparam) == IDCANCEL) || message == WM_CLOSE) {
        EndDialog(dialog, IDCANCEL); return TRUE;
    }
    return FALSE;
}

static BOOL apply_opacity(Note *note) {
    LONG_PTR style = GetWindowLongPtrW(note->window, GWL_EXSTYLE);
    if (note->opacity < 100) {
        if (!(style & WS_EX_LAYERED)) SetWindowLongPtrW(note->window, GWL_EXSTYLE, style | WS_EX_LAYERED);
        return SetLayeredWindowAttributes(note->window, 0, (BYTE)MulDiv(255,note->opacity,100), LWA_ALPHA);
    }
    if (style & WS_EX_LAYERED) {
        SetWindowLongPtrW(note->window, GWL_EXSTYLE, style & ~WS_EX_LAYERED);
        RedrawWindow(note->window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    return TRUE;
}

static BOOL resize_note(Note *note, int size) {
    int previous = note->size;
    note->size = size;
    SetWindowPos(note->window, NULL, 0, 0, note_width(note), note_height(note), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (!text_fits(note)) {
        note->size = previous;
        SetWindowPos(note->window, NULL, 0, 0, note_width(note), note_height(note), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        report_error(note->window, L"The text does not fit this size. Choose a larger size or shorten the note first.");
        return FALSE;
    }
    save_note(note); return TRUE;
}

static void note_command(Note *note, UINT command) {
    if (command == CMD_ARCHIVE) {
        if ((INT_PTR)ShellExecuteW(note->window, L"open", directory, NULL, NULL, SW_SHOWNORMAL) <= 32)
            report_error(note->window, L"The notes archive could not be opened.");
    } else if (command == CMD_TITLE) {
        DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_TITLE), note->window, title_dialog, (LPARAM)note);
    } else if (command >= CMD_OPACITY && command < CMD_OPACITY + 5) {
        int previous = note->opacity;
        note->opacity = OPACITIES[command - CMD_OPACITY];
        if (!apply_opacity(note)) { note->opacity = previous; apply_opacity(note); }
        save_note(note);
    } else if (command >= CMD_SIZE && command < CMD_SIZE + SIZE_COUNT) {
        resize_note(note, (int)command - CMD_SIZE);
    } else if (command >= CMD_PALETTE && command < CMD_PALETTE + 3) {
        int palette = (int)command - CMD_PALETTE;
        if (write_setting(L"app", L"palette", palette) && write_setting(L"app", L"next_color", 0)) {
            current_palette = palette; next_color = 0;
        } else report_error(note->window, L"The palette preference could not be saved.");
    }
}

static HMENU build_note_menu(const Note *note) {
    HMENU menu = CreatePopupMenu(), opacity = CreatePopupMenu(), size = CreatePopupMenu(), palette = CreatePopupMenu();
    wchar_t label[64];
    int i;
    if (!menu || !opacity || !size || !palette) {
        if (menu) DestroyMenu(menu);
        if (opacity) DestroyMenu(opacity);
        if (size) DestroyMenu(size);
        if (palette) DestroyMenu(palette);
        return NULL;
    }
    AppendMenuW(menu, MF_STRING, CMD_ARCHIVE, L"Open notes archive");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    for (i = 0; i < 5; ++i) {
        swprintf_s(label, 64, i ? L"%d%% opacity" : L"%d%% (opaque)", OPACITIES[i]);
        AppendMenuW(opacity, MF_STRING | (note->opacity == OPACITIES[i] ? MF_CHECKED : 0), CMD_OPACITY + i, label);
    }
    for (i = 0; i < SIZE_COUNT; ++i)
        AppendMenuW(size, MF_STRING | (note->size == i ? MF_CHECKED : 0), CMD_SIZE + i, SIZE_NAMES[i]);
    for (i = 0; i < 3; ++i)
        AppendMenuW(palette, MF_STRING | (current_palette == i ? MF_CHECKED : 0), CMD_PALETTE + i, PALETTE_NAMES[i]);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)opacity, L"Opacity");
    AppendMenuW(menu, MF_STRING, CMD_TITLE, L"Edit title...");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)size, L"Note size");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)palette, L"New note palette");
    return menu;
}

static void show_note_menu(Note *note, POINT point) {
    HMENU menu = build_note_menu(note);
    UINT command;
    if (!menu) return;
    SetForegroundWindow(note->window);
    command = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, note->window, NULL);
    DestroyMenu(menu);
    if (command) note_command(note, command);
    PostMessageW(note->window, WM_NULL, 0, 0);
}

static LRESULT CALLBACK note_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    Note *note = (Note *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        note = (Note *)((CREATESTRUCTW *)lparam)->lpCreateParams;
        note->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)note);
    }
    if (!note) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_NCCALCSIZE: return 0;
    case WM_CREATE:
        note->brush = CreateSolidBrush(note_color(note));
        note->edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_WANTRETURN,
                                    0, 0, 1, 1, window, (HMENU)(INT_PTR)10, instance, NULL);
        if (!note->edit || !note->brush) return -1;
        note->edit_proc = (WNDPROC)SetWindowLongPtrW(note->edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
        SendMessageW(note->edit, EM_SETLIMITTEXT, TEXT_LIMIT, 0);
        SendMessageW(note->edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
        update_style(note);
        return 0;
    case WM_NCHITTEST: {
        POINT p = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        RECT rect;
        ScreenToClient(window, &p); GetClientRect(window, &rect);
        if (p.y < bar_height(note) && p.x >= rect.right - bar_height(note) * 2) return HTCLIENT;
        return p.y < bar_height(note) ? HTCAPTION : HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *)lparam;
        info->ptMinTrackSize.x = info->ptMaxTrackSize.x = note_width(note);
        info->ptMinTrackSize.y = info->ptMaxTrackSize.y = note_height(note);
        return 0;
    }
    case WM_WINDOWPOSCHANGING: {
        WINDOWPOS *position = (WINDOWPOS *)lparam;
        if (!(position->flags & SWP_NOSIZE)) {
            position->cx = note_width(note);
            position->cy = note_height(note);
        }
        return 0;
    }
    case WM_SIZE:
        if (note->edit) layout(note);
        return 0;
    case WM_EXITSIZEMOVE:
        if (note->ready) save_note(note);
        return 0;
    case WM_DPICHANGED: {
        RECT *rect = (RECT *)lparam;
        update_style(note);
        SetWindowPos(window, NULL, rect->left, rect->top, note_width(note),
                     note_height(note),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout(note);
        if (note->ready) save_note(note);
        return 0;
    }
    case WM_COMMAND:
        if (!lparam) { note_command(note, LOWORD(wparam)); return 0; }
        if (HIWORD(wparam) == EN_CHANGE && note->ready && !note->validating) {
            if (!text_fits(note) || !remember_text(note)) {
                note->validating = TRUE;
                SetWindowTextW(note->edit, note->accepted_text);
                SendMessageW(note->edit, EM_SETSEL, note->selection_start, note->selection_end);
                note->validating = FALSE;
                return 0;
            }
            mark_dirty(note);
        }
        return 0;
    case WM_TIMER:
        if (wparam == SAVE_TIMER) save_note(note);
        return 0;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wparam, ink_color(note)); SetBkColor((HDC)wparam, note_color(note));
        return (LRESULT)note->brush;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint_note(note); return 0;
    case WM_MOUSEMOVE: {
        int hover = button_at(note, lparam);
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window, 0};
        TrackMouseEvent(&track);
        if (hover != note->hover) { note->hover = hover; redraw_bar(note); }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (note->hover) { note->hover = 0; redraw_bar(note); }
        return 0;
    case WM_LBUTTONDOWN:
        note->pressed = button_at(note, lparam);
        if (note->pressed) SetCapture(window);
        return 0;
    case WM_LBUTTONUP: {
        int pressed = note->pressed;
        note->pressed = 0;
        if (GetCapture() == window) ReleaseCapture();
        if (pressed && pressed == button_at(note, lparam)) {
            if (pressed == 1) toggle_note(note);
            else SendMessageW(window, WM_CLOSE, 0, 0);
        }
        return 0;
    }
    case WM_CAPTURECHANGED: note->pressed = 0; return 0;
    case WM_NCRBUTTONUP:
        if (wparam == HTCAPTION) {
            POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            show_note_menu(note, point); return 0;
        }
        break;
    case WM_CONTEXTMENU: {
        POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}, client = point;
        if (point.x == -1 && point.y == -1) {
            point.x = scaled(note, 10); point.y = bar_height(note);
            ClientToScreen(window, &point); show_note_menu(note, point); return 0;
        }
        ScreenToClient(window, &client);
        if (client.y >= 0 && client.y < bar_height(note)) show_note_menu(note, point);
        return 0;
    }
    case WM_NCLBUTTONDBLCLK:
        if (wparam == HTCAPTION) { toggle_note(note); return 0; }
        break;
    case WM_SETFOCUS:
        if (!note->collapsed) SetFocus(note->edit);
        return 0;
    case WM_CLOSE:
        if (!save_note(note) || !save_layout(note, FALSE)) {
            report_error(window, L"This note could not be saved. Check that the notes folder is writable.\nThe note will stay open.");
            return 0;
        }
        destroy_note(note);
        return 0;
    case WM_QUERYENDSESSION: return save_note(note);
    case WM_ENDSESSION:
        if (wparam) save_note(note);
        return 0;
    case WM_NCDESTROY: {
        Note **link = &notes;
        while (*link && *link != note) link = &(*link)->next;
        if (*link) *link = note->next;
        if (note->brush) DeleteObject(note->brush);
        if (note->font) DeleteObject(note->font);
        if (note->pen) DeleteObject(note->pen);
        free(note->accepted_text);
        free(note->title);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        free(note);
        break;
    }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static Note *open_note(const wchar_t *id, int color, BOOL restore) {
    Note *note = (Note *)calloc(1, sizeof(Note));
    RECT rect;
    MONITORINFO monitor = {sizeof(monitor)};
    int x, y, width, height;
    HWND window;
    if (!note) return NULL;
    if (wcscpy_s(note->id, 80, id) || swprintf_s(note->path, PATH_CAP, L"%s\\%s.txt", directory, id) < 0) {
        free(note); return NULL;
    }
    note->dpi = (int)GetDpiForSystem();
    note->color = restore ? setting(id, L"color", color) : color;
    if (note->color < 0 || note->color >= COLOR_COUNT) note->color = 0;
    note->palette = restore ? setting(id, L"palette", 0) : current_palette;
    if (note->palette < 0 || note->palette > 2) note->palette = 0;
    note->size = restore ? setting(id, L"size", 1) : 1;
    if (note->size < 0 || note->size >= SIZE_COUNT) note->size = 1;
    note->opacity = restore ? setting(id, L"opacity", 100) : 100;
    if (note->opacity < 60 || note->opacity > 100) note->opacity = 100;
    width = note_width(note);
    height = expanded_height(note);
    x = restore ? setting(id, L"x", 100) : 100 + next_color % 7 * 28;
    y = restore ? setting(id, L"y", 100) : 100 + next_color % 7 * 28;
    /* Clamp stored coordinates before arithmetic and keep the whole title bar
       reachable when a monitor is unplugged or the screen layout changes. */
    x = max(-100000, min(x, 100000)); y = max(-100000, min(y, 100000));
    SetRect(&rect, x, y, x + width, y + height);
    GetMonitorInfoW(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &monitor);
    width = min(width, monitor.rcWork.right - monitor.rcWork.left);
    height = min(height, monitor.rcWork.bottom - monitor.rcWork.top);
    x = max(monitor.rcWork.left, min(x, monitor.rcWork.right - width));
    y = max(monitor.rcWork.top, min(y, monitor.rcWork.bottom - height));
    note->collapsed = restore && setting(id, L"collapsed", FALSE);
    note->next = notes; notes = note;
    window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, NOTE_CLASS, L"PicoSlip",
                             WS_POPUP | WS_CLIPCHILDREN,
                             x, y, width, note->collapsed ? bar_height(note) : height,
                             NULL, NULL, instance, note);
    /* A failed WM_CREATE destroys the window and releases Note in WM_NCDESTROY. */
    if (!window) {
        if (notes == note) { notes = note->next; free(note); }
        return NULL;
    }
    if (!load_text(note)) { destroy_note(note); return NULL; }
    SetWindowPos(window, NULL, 0, 0, note_width(note), note_height(note),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    layout(note);
    if (!text_fits(note) || !remember_text(note)) { destroy_note(note); return NULL; }
    note->ready = TRUE;
    if (!apply_opacity(note)) { destroy_note(note); return NULL; }
    if (!save_layout(note, TRUE)) { destroy_note(note); return NULL; }
    if (!testing) {
        ShowWindow(window, SW_SHOWNORMAL);
        SetForegroundWindow(window);
        if (!note->collapsed) SetFocus(note->edit);
    }
    return note;
}

static Note *new_note(void) {
    SYSTEMTIME now;
    wchar_t id[80], path[PATH_CAP];
    HANDLE file;
    Note *note;
    int suffix, color = next_color;
    GetLocalTime(&now);
    for (suffix = 0; suffix < 10000; ++suffix) {
        swprintf_s(id, 80, L"note-%04u-%02u-%02u_%02u-%02u-%02u-%03u-%03d", now.wYear, now.wMonth,
                   now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, suffix);
        if (swprintf_s(path, PATH_CAP, L"%s\\%s.txt", directory, id) < 0) return NULL;
        file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); break; }
        if (GetLastError() != ERROR_FILE_EXISTS) return NULL;
    }
    if (suffix == 10000) return NULL;
    note = open_note(id, color, FALSE);
    if (note) {
        next_color = (color + 1) % COLOR_COUNT;
        write_setting(L"app", L"next_color", next_color);
    }
    return note;
}

static BOOL restore_notes(void) {
    WIN32_FIND_DATAW found;
    HANDLE search;
    wchar_t pattern[PATH_CAP];
    BOOL ok = TRUE;
    if (swprintf_s(pattern, PATH_CAP, L"%s\\*.txt", directory) < 0) return FALSE;
    search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    do {
        wchar_t id[80];
        size_t length = wcslen(found.cFileName);
        if (wcsncmp(found.cFileName, L"note-", 5) && wcsncmp(found.cFileName, L"nota-", 5)) continue;
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || length < 5 || length - 4 >= 80) continue;
        wcsncpy_s(id, 80, found.cFileName, length - 4);
        if (setting(id, L"open", TRUE) && !open_note(id, 0, TRUE)) ok = FALSE;
    } while (FindNextFileW(search, &found));
    if (GetLastError() != ERROR_NO_MORE_FILES) ok = FALSE;
    FindClose(search);
    return ok;
}

static void tray(BOOL add) {
    NOTIFYICONDATAW data = {0};
    data.cbSize = sizeof(data); data.hWnd = controller; data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = TRAY_MESSAGE; data.hIcon = app_icon ? app_icon : LoadIconW(NULL, IDI_APPLICATION);
    wcscpy_s(data.szTip, 128, L"PicoSlip - Ctrl+Alt++");
    Shell_NotifyIconW(add ? NIM_ADD : NIM_DELETE, &data);
}

static BOOL save_all(void) {
    Note *note;
    BOOL ok = TRUE;
    for (note = notes; note; note = note->next) if (!save_note(note)) ok = FALSE;
    return ok;
}

static LRESULT CALLBACK controller_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (taskbar_created && message == taskbar_created) { tray(TRUE); return 0; }
    switch (message) {
    case WM_HOTKEY:
    case NEW_NOTE_MESSAGE:
        if (!new_note()) report_error(NULL, L"A new note could not be created. Check the notes folder.");
        return 0;
    case TRAY_MESSAGE:
        if (lparam == WM_LBUTTONUP) PostMessageW(window, NEW_NOTE_MESSAGE, 0, 0);
        if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
            POINT point;
            HMENU menu = CreatePopupMenu();
            GetCursorPos(&point);
            AppendMenuW(menu, MF_STRING, EXIT_COMMAND, L"Exit");
            SetForegroundWindow(window);
            if (TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, NULL) == EXIT_COMMAND)
                PostMessageW(window, WM_CLOSE, 0, 0);
            PostMessageW(window, WM_NULL, 0, 0);
            DestroyMenu(menu);
        }
        return 0;
    case WM_CLOSE:
        if (!save_all()) {
            report_error(NULL, L"Some notes could not be saved. PicoSlip will stay open."); return 0;
        }
        while (notes) destroy_note(notes);
        DestroyWindow(window);
        return 0;
    case WM_QUERYENDSESSION: return save_all();
    case WM_ENDSESSION:
        if (wparam) save_all();
        return 0;
    case WM_DESTROY:
        tray(FALSE);
        UnregisterHotKey(window, 1); UnregisterHotKey(window, 2); UnregisterHotKey(window, 3);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static BOOL register_classes(void) {
    WNDCLASSW cls = {0};
    cls.hInstance = instance; cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
    cls.hIcon = app_icon; cls.lpszClassName = NOTE_CLASS;
    cls.style = CS_DBLCLKS | CS_DROPSHADOW; cls.lpfnWndProc = note_proc;
    if (!RegisterClassW(&cls)) return FALSE;
    cls.style = 0; cls.lpszClassName = APP_CLASS; cls.lpfnWndProc = controller_proc;
    return RegisterClassW(&cls) != 0;
}

int WINAPI wWinMain(HINSTANCE app, HINSTANCE previous, PWSTR command, int show) {
    HANDLE mutex;
    wchar_t root[PATH_CAP], *slash;
    DWORD length;
    MSG message;
    int result;
    BOOL hotkey;
    SHORT plus;
    UINT key, modifiers;
    (void)previous; (void)command; (void)show;
    instance = app;
    mutex = CreateMutexW(NULL, FALSE, L"Local\\PicoSlip.SingleInstance");
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(APP_CLASS, NULL);
        if (existing) PostMessageW(existing, NEW_NOTE_MESSAGE, 0, 0);
        CloseHandle(mutex); return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    length = GetModuleFileNameW(NULL, root, PATH_CAP);
    if (!length || length >= PATH_CAP - 7) { CloseHandle(mutex); return 1; }
    slash = wcsrchr(root, L'\\');
    if (!slash) { CloseHandle(mutex); return 1; }
    wcscpy_s(slash + 1, PATH_CAP - (size_t)(slash + 1 - root), L"portable.flag");
    if (GetFileAttributesW(root) != INVALID_FILE_ATTRIBUTES) {
        wcscpy_s(slash + 1, PATH_CAP - (size_t)(slash + 1 - root), L"notes");
    } else {
        wchar_t local[PATH_CAP];
        if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, local)) ||
            swprintf_s(root, PATH_CAP, L"%s\\PicoSlip", local) < 0 ||
            (!CreateDirectoryW(root, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) ||
            wcscat_s(root, PATH_CAP, L"\\notes")) { CloseHandle(mutex); return 1; }
    }
    if (!make_paths(root)) {
        report_error(NULL, L"The notes folder could not be created. Check that the archive location is writable.");
        CloseHandle(mutex); return 1;
    }
    next_color = setting(L"app", L"next_color", 0);
    if (next_color < 0 || next_color >= COLOR_COUNT) next_color = 0;
    current_palette = setting(L"app", L"palette", 0);
    if (current_palette < 0 || current_palette > 2) current_palette = 0;
    app_icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    if (!register_classes()) { CloseHandle(mutex); return 1; }
    controller = CreateWindowExW(WS_EX_TOOLWINDOW, APP_CLASS, L"PicoSlip", WS_OVERLAPPED,
                                 0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (!controller) { CloseHandle(mutex); return 1; }
    plus = VkKeyScanW(L'+');
    key = plus == -1 ? VK_OEM_PLUS : (UINT)(plus & 0xff);
    modifiers = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
    hotkey = RegisterHotKey(controller, 1, modifiers, key);
    if (RegisterHotKey(controller, 2, modifiers, VK_ADD)) hotkey = TRUE;
    if (RegisterHotKey(controller, 3, modifiers | MOD_SHIFT, key)) hotkey = TRUE;
    if (!hotkey) {
        report_error(NULL, L"Ctrl+Alt++ is already in use by another application.");
        DestroyWindow(controller); CloseHandle(mutex); return 1;
    }
    taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    tray(TRUE);
    if (!restore_notes())
        report_error(NULL, L"Some notes could not be opened. Their original TXT files are still in the archive.");
    if (!notes && !new_note()) report_error(NULL, L"The first note could not be created.");
    while ((result = GetMessageW(&message, NULL, 0, 0)) > 0) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    if (app_icon) DestroyIcon(app_icon);
    CloseHandle(mutex);
    return result < 0 ? 1 : 0;
}
