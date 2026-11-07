/* Event-driven native color wheel. One allocation per opening, no timers,
   animation, worker, render-time allocation or floating-point hit testing. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <stdint.h>
#include <stdlib.h>
#include "palette_picker.h"
#include "colors.h"

enum { HIT_NONE = -1, HIT_PREVIOUS = -2, HIT_NEXT = -3 };
typedef struct Picker {
    HWND owner;
    void *context;
    PaletteSelection select;
    HFONT font;
    HPEN outer_pen, inner_pen;
    POINT edge[COLOR_COUNT + 1];
    int palette, selected_palette, selected_color, hover, pressed;
    int side, gap, footer, arrow, dpi;
    BOOL tracking;
} Picker;
static HWND active_picker;

/* Clockwise rays from twelve o'clock, scaled by 10000. */
static const POINT rays5[] = {{0,-10000},{9511,-3090},{5878,8090},{-5878,8090},{-9511,-3090},{0,-10000}};
static const POINT rays6[] = {{0,-10000},{8660,-5000},{8660,5000},{0,10000},{-8660,5000},{-8660,-5000},{0,-10000}};

static void geometry(Picker *picker) {
    int count = PALETTE_SIZES[picker->palette], i, center = picker->side / 2;
    const POINT *rays = count == 5 ? rays5 : rays6;
    for (i = 0; i <= count; ++i) {
        picker->edge[i].x = center + MulDiv(rays[i].x, center, 10000);
        picker->edge[i].y = center + MulDiv(rays[i].y, center, 10000);
    }
}

static int hit_test(const Picker *picker, POINT point) {
    int center = picker->side / 2, x = point.x - center, y = point.y - center, i;
    int count = PALETTE_SIZES[picker->palette];
    const POINT *rays = count == 5 ? rays5 : rays6;
    if (point.x < 0 || point.x >= picker->side || point.y < 0) return HIT_NONE;
    if (point.y >= picker->side + picker->gap && point.y < picker->side + picker->gap + picker->footer) {
        if (point.x < picker->arrow) return HIT_PREVIOUS;
        if (point.x >= picker->side - picker->arrow) return HIT_NEXT;
    }
    if ((int64_t)x*x + (int64_t)y*y > (int64_t)center*center) return HIT_NONE;
    for (i = 0; i < count; ++i)
        if ((int64_t)rays[i].x*y - (int64_t)rays[i].y*x >= 0 &&
            (int64_t)x*rays[i+1].y - (int64_t)y*rays[i+1].x >= 0) return i;
    return HIT_NONE;
}

static void wedge(HDC dc, const Picker *picker, int color) {
    Pie(dc, 0, 0, picker->side, picker->side,
        picker->edge[color].x, picker->edge[color].y,
        picker->edge[color+1].x, picker->edge[color+1].y);
}

static void paint_picker(HWND window, HDC dc) {
    Picker *picker = (Picker *)GetWindowLongPtrW(window, GWLP_USERDATA);
    RECT footer = {0, picker->side + picker->gap, picker->side, picker->side + picker->gap + picker->footer};
    RECT label = footer;
    HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ old_font = SelectObject(dc, picker->font);
    int i, count = PALETTE_SIZES[picker->palette], direction = SetArcDirection(dc, AD_CLOCKWISE);
    for (i = 0; i < count; ++i) {
        SetDCBrushColor(dc, PALETTES[picker->palette][i]); wedge(dc, picker, i);
    }
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    SelectObject(dc, GetStockObject(BLACK_PEN));
    Ellipse(dc, 0, 0, picker->side, picker->side);
    i = picker->hover >= 0 ? picker->hover :
        picker->palette == picker->selected_palette ? picker->selected_color : -1;
    if (i >= 0) {
        SelectObject(dc, picker->outer_pen); wedge(dc, picker, i);
        SelectObject(dc, picker->inner_pen); wedge(dc, picker, i);
    }
    FillRect(dc, &footer, GetSysColorBrush(COLOR_MENU));
    FrameRect(dc, &footer, GetSysColorBrush(COLOR_MENUTEXT));
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetSysColor(COLOR_MENUTEXT));
    label.left += picker->arrow; label.right -= picker->arrow;
    DrawTextW(dc, PALETTE_NAMES[picker->palette], -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    for (i = 0; i < 2; ++i) {
        RECT arrow = footer;
        int hit = i ? HIT_NEXT : HIT_PREVIOUS;
        if (i) arrow.left = arrow.right - picker->arrow; else arrow.right = picker->arrow;
        InflateRect(&arrow, -1, -1);
        if (picker->hover == hit) FillRect(dc, &arrow, GetSysColorBrush(COLOR_HIGHLIGHT));
        SetTextColor(dc, GetSysColor(picker->hover == hit ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
        DrawTextW(dc, i ? L"\x2192" : L"\x2190", 1, &arrow, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SetArcDirection(dc, direction);
    SelectObject(dc, old_font); SelectObject(dc, old_brush); SelectObject(dc, old_pen);
}

static void change_palette(HWND window, Picker *picker, int step) {
    picker->palette = (picker->palette + step + PALETTE_COUNT) % PALETTE_COUNT;
    picker->hover = HIT_NONE; picker->pressed = HIT_NONE;
    geometry(picker);
    SetWindowTextW(window, PALETTE_NAMES[picker->palette]);
    InvalidateRect(window, NULL, FALSE);
}

static void finish(HWND window, Picker *picker, int color, BOOL restore_focus) {
    HWND owner = picker->owner;
    PaletteSelection select = picker->select;
    void *context = picker->context;
    int palette = picker->palette;
    DestroyWindow(window);
    if (!IsWindow(owner)) return;
    if (color >= 0) select(context, palette, color);
    if (restore_focus && IsWindow(owner)) {
        SetForegroundWindow(owner);
        SetFocus(GetDlgItem(owner, 10));
    }
}

static LRESULT CALLBACK picker_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    Picker *picker = (Picker *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        const Picker *initial = (const Picker *)((CREATESTRUCTW *)lparam)->lpCreateParams;
        picker = (Picker *)malloc(sizeof(*picker));
        if (!picker) return FALSE;
        *picker = *initial;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)picker);
    }
    if (!picker) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_CREATE: {
        HRGN circle, footer;
        picker->font = CreateFontW(-MulDiv(10, picker->dpi, 72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
        picker->outer_pen = CreatePen(PS_SOLID, max(3, MulDiv(4,picker->dpi,96)), RGB(255,255,255));
        picker->inner_pen = CreatePen(PS_SOLID, max(1, MulDiv(2,picker->dpi,96)), RGB(0,0,0));
        if (!picker->font || !picker->outer_pen || !picker->inner_pen) return -1;
        circle = CreateEllipticRgn(0,0,picker->side,picker->side);
        footer = CreateRectRgn(0,picker->side+picker->gap,picker->side,picker->side+picker->gap+picker->footer);
        if (!circle || !footer) {
            if (circle) DeleteObject(circle);
            if (footer) DeleteObject(footer);
            return -1;
        }
        CombineRgn(circle,circle,footer,RGN_OR); DeleteObject(footer);
        if (!SetWindowRgn(window,circle,FALSE)) { DeleteObject(circle); return -1; }
        geometry(picker);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window,&paint); paint_picker(window,dc); EndPaint(window,&paint); return 0;
    }
    case WM_PRINTCLIENT: paint_picker(window,(HDC)wparam); return 0;
    case WM_MOUSEMOVE: {
        POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        int hit = hit_test(picker,point);
        if (picker->hover != hit) { picker->hover = hit; InvalidateRect(window,NULL,FALSE); }
        if (!picker->tracking) {
            TRACKMOUSEEVENT track = {sizeof(track),TME_LEAVE,window,0};
            picker->tracking = TrackMouseEvent(&track);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        picker->tracking = FALSE; picker->hover = HIT_NONE;
        InvalidateRect(window,NULL,FALSE); return 0;
    case WM_LBUTTONDOWN: {
        POINT point = {GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
        picker->pressed = hit_test(picker,point);
        if (picker->pressed != HIT_NONE) SetCapture(window);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT point = {GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
        int hit = hit_test(picker,point), pressed = picker->pressed;
        if (GetCapture() == window) ReleaseCapture();
        picker->pressed = HIT_NONE;
        if (hit == HIT_NONE || hit != pressed) return 0;
        if (hit >= 0) finish(window,picker,hit,TRUE);
        else change_palette(window,picker,hit == HIT_PREVIOUS ? -1 : 1);
        return 0;
    }
    case WM_CAPTURECHANGED: picker->pressed = HIT_NONE; return 0;
    case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTTAB | DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        if (wparam == VK_LEFT || wparam == VK_RIGHT) {
            change_palette(window,picker,wparam == VK_LEFT ? -1 : 1); return 0;
        }
        if (wparam == VK_UP || wparam == VK_DOWN || wparam == VK_TAB) {
            int count = PALETTE_SIZES[picker->palette];
            int step = wparam == VK_UP || (wparam == VK_TAB && GetKeyState(VK_SHIFT) < 0) ? -1 : 1;
            picker->hover = picker->hover < 0 ? (step < 0 ? count-1 : 0) : (picker->hover+step+count)%count;
            InvalidateRect(window,NULL,FALSE); return 0;
        }
        if (wparam == VK_RETURN || wparam == VK_SPACE) {
            int color = picker->hover >= 0 ? picker->hover :
                picker->palette == picker->selected_palette ? picker->selected_color : 0;
            finish(window,picker,color,TRUE); return 0;
        }
        if (wparam == VK_ESCAPE) { finish(window,picker,-1,TRUE); return 0; }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE) DestroyWindow(window);
        return 0;
    case WM_DPICHANGED:
    case WM_DISPLAYCHANGE:
    case WM_CLOSE: finish(window,picker,-1,FALSE); return 0;
    case WM_NCDESTROY:
        if (active_picker == window) active_picker = NULL;
        if (picker->font) DeleteObject(picker->font);
        if (picker->outer_pen) DeleteObject(picker->outer_pen);
        if (picker->inner_pen) DeleteObject(picker->inner_pen);
        SetWindowLongPtrW(window,GWLP_USERDATA,0); free(picker);
        break;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}

HWND palette_picker_show(HWND owner, POINT anchor, int palette, int color,
                         PaletteSelection select, void *context) {
    WNDCLASSW cls = {0};
    MONITORINFO monitor = {sizeof(monitor)};
    Picker initial = {0};
    HWND window;
    int x,y,height;
    if (!IsWindow(owner) || !select || palette < 0 || palette >= PALETTE_COUNT ||
        color < 0 || color >= PALETTE_SIZES[palette]) return NULL;
    if (active_picker) DestroyWindow(active_picker);
    cls.hInstance = (HINSTANCE)GetWindowLongPtrW(owner,GWLP_HINSTANCE);
    cls.hCursor = LoadCursorW(NULL,IDC_ARROW); cls.lpfnWndProc = picker_proc;
    cls.lpszClassName = PALETTE_PICKER_CLASS; cls.style = CS_DROPSHADOW;
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return NULL;
    initial.owner = owner; initial.context = context; initial.select = select;
    initial.palette = initial.selected_palette = palette; initial.selected_color = color;
    initial.hover = initial.pressed = HIT_NONE;
    initial.dpi = (int)GetDpiForWindow(owner); if (!initial.dpi) initial.dpi = 96;
    initial.side = MulDiv(240,initial.dpi,96); initial.gap = MulDiv(6,initial.dpi,96);
    initial.footer = initial.arrow = MulDiv(34,initial.dpi,96);
    height = initial.side + initial.gap + initial.footer;
    if (!GetMonitorInfoW(MonitorFromPoint(anchor,MONITOR_DEFAULTTONEAREST),&monitor)) return NULL;
    x = max(monitor.rcWork.left,min(anchor.x-initial.side/2,monitor.rcWork.right-initial.side));
    y = max(monitor.rcWork.top,min(anchor.y-initial.side/2,monitor.rcWork.bottom-height));
    window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,PALETTE_PICKER_CLASS,PALETTE_NAMES[palette],
        WS_POPUP,x,y,initial.side,height,owner,NULL,cls.hInstance,&initial);
    if (window) {
        active_picker = window;
        ShowWindow(window,SW_SHOWNORMAL); SetForegroundWindow(window); SetFocus(window);
    }
    return window;
}
