/* Shared month geometry for the date picker and the annual archive view.
   Drawing only consults memory; there is no file IO or per-day window. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include "calendar.h"

#define PAPER RGB(244,238,226)
#define INK RGB(58,52,43)
#define EDGE RGB(190,177,151)
#define SHADE RGB(235,226,209)
#define ACCENT RGB(211,194,162)
enum { YEAR_LABEL=2, YEAR_EDIT=3, TODAY=6, BACK_MONTH=7, NEXT_MONTH=8, REMOVE_DATE=9 };
static const wchar_t *months[] = {L"January",L"February",L"March",L"April",L"May",L"June",
    L"July",L"August",L"September",L"October",L"November",L"December"};
typedef struct Calendar {
    HWND window, owner, year_edit, tooltip;
    HFONT font, bold;
    HBRUSH paper;
    HDC buffer;
    HBITMAP bitmap, original_bitmap;
    HRGN damage;
    SIZE buffer_size;
    int dpi, year, month, scroll, columns, cell, month_width, month_height, wheel;
    unsigned selected, focus, hover, pressed, today, counts[366];
    UINT message;
    BOOL picker, changing, tracking;
    wchar_t tip[96];
} Calendar;
typedef struct DayList {
    HWND list, previous_focus;
    HFONT font;
    CalendarItem *items;
    int count, dpi;
    BOOL (*open)(const wchar_t *id);
} DayList;
static HWND active_picker, active_list;
static int px(const Calendar *c, int n) { return MulDiv(n, c->dpi, 96); }

unsigned calendar_today(void) {
    SYSTEMTIME time;
    GetLocalTime(&time);
    return time.wYear * 10000u + time.wMonth * 100u + time.wDay;
}

void calendar_icon(HDC dc, RECT rect, COLORREF ink, BOOL assigned) {
    int cx = (rect.left + rect.right) / 2, cy = (rect.top + rect.bottom) / 2;
    int s = max(4, (rect.bottom - rect.top) / 5);
    HGDIOBJ pen = SelectObject(dc, GetStockObject(DC_PEN));
    HGDIOBJ brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    SetDCPenColor(dc, ink);
    Rectangle(dc, cx-s, cy-s+1, cx+s+1, cy+s+1);
    MoveToEx(dc,cx-s,cy-s/2+1,NULL); LineTo(dc,cx+s,cy-s/2+1);
    MoveToEx(dc,cx-s/2,cy-s-1,NULL); LineTo(dc,cx-s/2,cy-s+3);
    MoveToEx(dc,cx+s/2,cy-s-1,NULL); LineTo(dc,cx+s/2,cy-s+3);
    if (assigned) {
        MoveToEx(dc,cx-s/2,cy+1,NULL); LineTo(dc,cx,cy+s/2+1); LineTo(dc,cx+s/2+1,cy-1);
    }
    SelectObject(dc,brush); SelectObject(dc,pen);
}

static void notify_calendar(Calendar *c, UINT code, unsigned date) {
    CalendarEvent event = {{c->window, (UINT_PTR)GetDlgCtrlID(c->window), code}, date};
    SendMessageW(c->owner, WM_NOTIFY, event.header.idFrom, (LPARAM)&event);
}

static RECT month_rect(const Calendar *c, int month) {
    int position = c->picker ? 0 : month - 1;
    RECT rect;
    rect.left = px(c,8) + position % c->columns * c->month_width;
    rect.top = px(c,46) + position / c->columns * c->month_height - c->scroll;
    rect.right = rect.left + c->month_width;
    rect.bottom = rect.top + c->month_height;
    return rect;
}

BOOL calendar_day_rect(HWND window, unsigned date, RECT *rect) {
    Calendar *c = (Calendar *)GetWindowLongPtrW(window, GWLP_USERDATA);
    int month, slot;
    RECT month_box;
    if (!c || !date_valid(date) || date / 10000 != (unsigned)c->year) return FALSE;
    month = date / 100 % 100;
    if (c->picker && month != c->month) return FALSE;
    month_box = month_rect(c, month);
    slot = date_weekday(date - date % 100 + 1) + (int)(date % 100) - 1;
    rect->left = month_box.left + (c->month_width - c->cell * 7) / 2 + slot % 7 * c->cell;
    rect->top = month_box.top + c->cell * 2 + slot / 7 * c->cell;
    rect->right = rect->left + c->cell; rect->bottom = rect->top + c->cell;
    return TRUE;
}

static void invalidate_day(Calendar *c, unsigned date) {
    RECT rect, clip;
    if (!calendar_day_rect(c->window,date,&rect)) return;
    GetClientRect(c->window,&clip); clip.top=px(c,46);
    if (c->picker) clip.bottom-=px(c,44);
    if (IntersectRect(&rect,&rect,&clip)) InvalidateRect(c->window,&rect,FALSE);
}

static void focus_day(Calendar *c, unsigned date) {
    if (date==c->focus) return;
    invalidate_day(c,c->focus); c->focus=date; invalidate_day(c,date);
}

static unsigned hit_day(Calendar *c, POINT p) {
    int month;
    if (p.y < px(c,46)) return 0;
    for (month = c->picker ? c->month : 1; month <= (c->picker ? c->month : 12); ++month) {
        RECT box = month_rect(c, month);
        int x = p.x - box.left - (c->month_width - c->cell * 7) / 2;
        int y = p.y - box.top - c->cell * 2, day;
        if (x < 0 || x >= c->cell * 7 || y < 0 || y >= c->cell * 6) continue;
        day = y / c->cell * 7 + x / c->cell - date_weekday(c->year * 10000u + month * 100u + 1) + 1;
        if (day > 0 && day <= date_days(c->year, month)) return c->year * 10000u + month * 100u + day;
    }
    return 0;
}

static void layout_calendar(Calendar *c) {
    RECT rect;
    SCROLLINFO scroll = {sizeof(scroll), SIF_RANGE | SIF_PAGE | SIF_POS};
    const struct { int id, width; } controls[] = {{YEAR_LABEL,44},{YEAR_EDIT,72},{TODAY,58}};
    int i, x, total = 0;
    GetClientRect(c->window, &rect);
    for (i = 0; i < 3; ++i) total += px(c, controls[i].width + 4);
    x = max(px(c,4), (rect.right - total) / 2);
    for (i = 0; i < 3; ++i) {
        SetWindowPos(GetDlgItem(c->window,controls[i].id),NULL,x,px(c,8),px(c,controls[i].width),px(c,28),SWP_NOZORDER|SWP_NOACTIVATE);
        x += px(c,controls[i].width+4);
    }
    c->cell = px(c, c->picker ? 30 : 21);
    c->columns = c->picker ? 1 : max(1, min(4, (rect.right - px(c,16)) / (c->cell * 7 + px(c,12))));
    c->month_width = max(1, (rect.right - px(c,16)) / c->columns);
    c->month_height = c->cell * 8 + px(c,12);
    if (c->picker) {
        SetWindowPos(GetDlgItem(c->window,BACK_MONTH),NULL,px(c,16),px(c,46),px(c,28),c->cell,SWP_NOZORDER);
        SetWindowPos(GetDlgItem(c->window,NEXT_MONTH),NULL,rect.right-px(c,44),px(c,46),px(c,28),c->cell,SWP_NOZORDER);
        SetWindowPos(GetDlgItem(c->window,REMOVE_DATE),NULL,px(c,16),rect.bottom-px(c,38),rect.right-px(c,32),px(c,28),SWP_NOZORDER);
    } else {
        scroll.nMax = ((12 + c->columns - 1) / c->columns) * c->month_height - 1;
        scroll.nPage = max(1, rect.bottom - px(c,46));
        c->scroll = max(0,min(c->scroll,scroll.nMax-(int)scroll.nPage+1));
        scroll.nPos = c->scroll; SetScrollInfo(c->window,SB_VERT,&scroll,TRUE);
    }
    InvalidateRect(c->window,NULL,FALSE);
}

static void calendar_fonts(Calendar *c) {
    HFONT old = c->font, bold = c->bold;
    int i;
    c->font = CreateFontW(-MulDiv(10,c->dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
    c->bold = CreateFontW(-MulDiv(10,c->dpi,72),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
    for (i = 1; i <= REMOVE_DATE; ++i) SendDlgItemMessageW(c->window,i,WM_SETFONT,(WPARAM)c->font,TRUE);
    if (old) DeleteObject(old);
    if (bold) DeleteObject(bold);
}

int calendar_year(HWND window) {
    Calendar *c = (Calendar *)GetWindowLongPtrW(window,GWLP_USERDATA);
    return c ? c->year : 0;
}

void calendar_set_year(HWND window, int year) {
    Calendar *c = (Calendar *)GetWindowLongPtrW(window,GWLP_USERDATA);
    wchar_t text[16];
    BOOL changed;
    unsigned focus;
    int month, day;
    if (!c) return;
    year = max(DATE_MIN_YEAR,min(DATE_MAX_YEAR,year)); changed = year != c->year;
    month=c->picker ? c->month : (int)(c->focus/100%100);
    day=min((int)(c->focus%100),date_days(year,month));
    focus=year*10000u+month*100u+day;
    c->changing = TRUE;
    swprintf_s(text,16,L"%04d",year); SetWindowTextW(c->year_edit,text); c->changing = FALSE;
    if (!changed && focus==c->focus) return;
    c->year=year; c->focus=focus;
    c->hover = 0;
    if (changed && !c->picker) { memset(c->counts,0,sizeof(c->counts)); notify_calendar(c,CALENDAR_YEAR,year * 10000u); }
    InvalidateRect(window,NULL,FALSE);
}

void calendar_counts(HWND window, const unsigned counts[366]) {
    Calendar *c = (Calendar *)GetWindowLongPtrW(window,GWLP_USERDATA);
    int month, day, ordinal=0;
    if (!c || !memcmp(c->counts,counts,sizeof(c->counts))) return;
    for (month=1;month<=12;++month)
        for (day=1;day<=date_days(c->year,month);++day,++ordinal)
            if (c->counts[ordinal]!=counts[ordinal]) invalidate_day(c,c->year*10000u+month*100u+day);
    memcpy(c->counts,counts,sizeof(c->counts));
}

static void choose_day(Calendar *c, unsigned date) {
    HWND owner = c->owner, window = c->window;
    UINT message = c->message;
    if (c->picker) {
        DestroyWindow(window);
        if (IsWindow(owner)) { SetForegroundWindow(owner); PostMessageW(owner,message,date,0); }
    } else if (date && c->counts[date_ordinal(date)]) notify_calendar(c,CALENDAR_DAY,date);
}

static void commit_year(Calendar *c) {
    wchar_t text[16];
    int year;
    GetWindowTextW(c->year_edit,text,16); year = _wtoi(text);
    calendar_set_year(c->window,year >= DATE_MIN_YEAR && year <= DATE_MAX_YEAR ? year : c->year);
}

static LRESULT CALLBACK calendar_input(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data) {
    Calendar *c = (Calendar *)data;
    (void)id;
    if (message == WM_KEYDOWN) {
        if (!c->picker && ((wparam=='F' && GetKeyState(VK_CONTROL)<0) || wparam==VK_F5))
            return SendMessageW(c->owner,message,wparam,lparam);
        if (wparam == VK_ESCAPE) { if (c->picker) DestroyWindow(c->window); else SetFocus(c->window); return 0; }
        if (window == c->year_edit && wparam == VK_RETURN) { commit_year(c); SetFocus(c->window); return 0; }
        if (wparam == VK_TAB) {
            BOOL back=GetKeyState(VK_SHIFT)<0;
            if (back && window==c->year_edit) SetFocus(c->window);
            else if (!back && window==GetDlgItem(c->window,c->picker ? REMOVE_DATE : TODAY)) {
                if (c->picker) SetFocus(c->window); else notify_calendar(c,CALENDAR_LEAVE,0);
            } else SetFocus(GetNextDlgTabItem(c->window,window,back));
            return 0;
        }
    }
    if (message == WM_CHAR && (wparam == VK_RETURN || wparam == VK_ESCAPE || wparam == VK_TAB)) return 0;
    return DefSubclassProc(window,message,wparam,lparam);
}

static void draw_calendar(Calendar *c, HDC dc) {
    RECT client, clip;
    HGDIOBJ font = SelectObject(dc,c->font), pen = SelectObject(dc,GetStockObject(DC_PEN));
    HGDIOBJ brush = SelectObject(dc,GetStockObject(DC_BRUSH));
    unsigned today = calendar_today();
    int month, saved;
    GetClientRect(c->window,&client); FillRect(dc,&client,c->paper);
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,INK);
    if (c->picker) { SetDCBrushColor(dc,EDGE); FrameRect(dc,&client,(HBRUSH)GetStockObject(DC_BRUSH)); }
    saved = SaveDC(dc); clip = client; clip.top = px(c,46);
    if (c->picker) clip.bottom -= px(c,44);
    IntersectClipRect(dc,clip.left,clip.top,clip.right,clip.bottom);
    for (month = c->picker ? c->month : 1; month <= (c->picker ? c->month : 12); ++month) {
        static const wchar_t *week[] = {L"M",L"T",L"W",L"T",L"F",L"S",L"S"};
        RECT box = month_rect(c,month), label = box;
        int day;
        if (!RectVisible(dc,&box)) continue;
        label.bottom = label.top + c->cell;
        SelectObject(dc,c->bold); SetTextColor(dc,INK);
        if (RectVisible(dc,&label)) DrawTextW(dc,months[month-1],-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(dc,c->font);
        for (day = 0; day < 7; ++day) {
            label.left = box.left + (c->month_width-c->cell*7)/2 + day*c->cell;
            label.right = label.left+c->cell; label.top = box.top+c->cell; label.bottom = label.top+c->cell;
            SetTextColor(dc,RGB(125,111,88));
            if (RectVisible(dc,&label)) DrawTextW(dc,week[day],-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        for (day = 1; day <= date_days(c->year,month); ++day) {
            unsigned date = c->year * 10000u + month * 100u + day;
            unsigned count = c->counts[date_ordinal(date)];
            wchar_t number[8];
            RECT cell, ring;
            calendar_day_rect(c->window,date,&cell);
            if (!RectVisible(dc,&cell)) continue;
            ring = cell; InflateRect(&ring,-px(c,1),-px(c,1));
            if (count || date == c->selected || date == c->hover) {
                SetDCPenColor(dc,count || date == c->selected ? INK : EDGE);
                SetDCBrushColor(dc,count || date == c->selected ? ACCENT : SHADE);
                Ellipse(dc,ring.left,ring.top,ring.right,ring.bottom);
            }
            SetTextColor(dc,INK); swprintf_s(number,8,L"%d",day);
            DrawTextW(dc,number,-1,&cell,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            if (count > 1) {
                int r = px(c,2); SetDCBrushColor(dc,INK); SetDCPenColor(dc,INK);
                Ellipse(dc,cell.right-r*2,cell.bottom-r*2,cell.right,cell.bottom);
            }
            if (date == today) {
                SetDCPenColor(dc,INK); MoveToEx(dc,cell.left+c->cell/3,cell.bottom-px(c,3),NULL);
                LineTo(dc,cell.right-c->cell/3,cell.bottom-px(c,3));
            }
            if (date == c->focus && GetFocus() == c->window) DrawFocusRect(dc,&cell);
        }
    }
    RestoreDC(dc,saved); SelectObject(dc,brush); SelectObject(dc,pen); SelectObject(dc,font);
}

/* Reuse the surface and damage region. Background and glyphs become visible
   together; ordinary hover never creates a bitmap or repaints other months. */
static BOOL prepare_buffer(Calendar *c, HDC dc, const RECT *client) {
    HBITMAP bitmap;
    if (client->right<=0 || client->bottom<=0) return FALSE;
    if (!c->buffer) c->buffer=CreateCompatibleDC(dc);
    if (!c->damage) c->damage=CreateRectRgn(0,0,0,0);
    if (!c->buffer || !c->damage) return FALSE;
    if (c->bitmap && c->buffer_size.cx==client->right && c->buffer_size.cy==client->bottom) return TRUE;
    bitmap=CreateCompatibleBitmap(dc,client->right,client->bottom);
    if (!bitmap) return FALSE;
    if (c->bitmap) { SelectObject(c->buffer,bitmap); DeleteObject(c->bitmap); }
    else c->original_bitmap=(HBITMAP)SelectObject(c->buffer,bitmap);
    c->bitmap=bitmap; c->buffer_size.cx=client->right; c->buffer_size.cy=client->bottom;
    SetRectRgn(c->damage,0,0,client->right,client->bottom);
    return TRUE;
}

static void paint_calendar(Calendar *c) {
    PAINTSTRUCT paint;
    RECT client;
    HDC dc;
    unsigned today=calendar_today();
    if (today!=c->today) {
        invalidate_day(c,c->today); c->today=today; invalidate_day(c,today);
    }
    if (c->damage) GetUpdateRgn(c->window,c->damage,FALSE);
    dc=BeginPaint(c->window,&paint); GetClientRect(c->window,&client);
    if (prepare_buffer(c,dc,&client)) {
        SelectClipRgn(c->buffer,c->damage); draw_calendar(c,c->buffer); SelectClipRgn(c->buffer,NULL);
        BitBlt(dc,paint.rcPaint.left,paint.rcPaint.top,paint.rcPaint.right-paint.rcPaint.left,
            paint.rcPaint.bottom-paint.rcPaint.top,c->buffer,paint.rcPaint.left,paint.rcPaint.top,SRCCOPY);
    } else draw_calendar(c,dc);
    EndPaint(c->window,&paint);
}

static LRESULT CALLBACK calendar_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    Calendar *c = (Calendar *)GetWindowLongPtrW(window,GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        Calendar *initial = (Calendar *)((CREATESTRUCTW *)lparam)->lpCreateParams;
        c = (Calendar *)calloc(1,sizeof(*c)); if (!c) return FALSE;
        *c = *initial; c->window = window; SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)c);
    }
    if (!c) return DefWindowProcW(window,message,wparam,lparam);
    switch (message) {
    case WM_CREATE: {
        const struct { int id; const wchar_t *label; } controls[] = {
            {YEAR_LABEL,L"Year"},{YEAR_EDIT,L"Year"},{TODAY,L"Today"},
            {BACK_MONTH,L"<"},{NEXT_MONTH,L">"},{REMOVE_DATE,L"Remove date"}
        };
        HINSTANCE app = GetModuleHandleW(NULL);
        TOOLINFOW tip = {sizeof(tip)};
        int i;
        c->dpi = (int)GetDpiForWindow(window); if (!c->dpi) c->dpi = 96;
        c->paper = CreateSolidBrush(PAPER);
        if (!c->paper) return -1;
        for (i = 0; i < (int)(sizeof(controls)/sizeof(controls[0])); ++i) {
            int id=controls[i].id;
            HWND control;
            if (!c->picker && id>TODAY) break;
            control = CreateWindowExW(id==YEAR_EDIT ? WS_EX_CLIENTEDGE : 0,id==YEAR_LABEL ? L"STATIC" : id==YEAR_EDIT ? L"EDIT" : L"BUTTON",
                controls[i].label,WS_CHILD|WS_VISIBLE|(id==YEAR_LABEL ? SS_CENTERIMAGE : WS_TABSTOP)|
                (id==YEAR_EDIT ? ES_NUMBER|ES_CENTER|ES_AUTOHSCROLL : id==YEAR_LABEL ? 0 : BS_OWNERDRAW),
                0,0,1,1,window,(HMENU)(INT_PTR)id,app,NULL);
            if (!control) return -1;
            if (id==YEAR_EDIT) { c->year_edit=control; SendMessageW(control,EM_SETLIMITTEXT,4,0); }
            if(id!=YEAR_LABEL) SetWindowSubclass(control,calendar_input,1,(DWORD_PTR)c);
        }
        c->tooltip = CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,NULL,WS_POPUP|TTS_ALWAYSTIP,
            0,0,0,0,window,NULL,app,NULL);
        if (c->tooltip) {
            tip.uFlags=TTF_SUBCLASS|TTF_IDISHWND; tip.hwnd=window; tip.uId=(UINT_PTR)window; tip.lpszText=LPSTR_TEXTCALLBACKW;
            SendMessageW(c->tooltip,TTM_ADDTOOLW,0,(LPARAM)&tip);
        }
        calendar_fonts(c); calendar_set_year(window,c->year); layout_calendar(c); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint_calendar(c); return 0;
    case WM_PRINTCLIENT: draw_calendar(c,(HDC)wparam); return 0;
    case WM_SIZE: layout_calendar(c); return 0;
    case WM_DPICHANGED_AFTERPARENT:
        c->dpi=(int)GetDpiForWindow(window); calendar_fonts(c); layout_calendar(c); return 0;
    case WM_DPICHANGED: if(c->picker) DestroyWindow(window); return 0;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *draw=(DRAWITEMSTRUCT *)lparam;
        wchar_t label[32]; RECT rect=draw->rcItem;
        HGDIOBJ font=SelectObject(draw->hDC,c->font);
        SetDCBrushColor(draw->hDC,draw->itemState&ODS_SELECTED ? ACCENT : SHADE);
        FillRect(draw->hDC,&rect,(HBRUSH)GetStockObject(DC_BRUSH));
        SetDCBrushColor(draw->hDC,EDGE); FrameRect(draw->hDC,&rect,(HBRUSH)GetStockObject(DC_BRUSH));
        GetWindowTextW(draw->hwndItem,label,32); SetTextColor(draw->hDC,INK); SetBkMode(draw->hDC,TRANSPARENT);
        DrawTextW(draw->hDC,label,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(draw->itemState&ODS_FOCUS) { InflateRect(&rect,-3,-3); DrawFocusRect(draw->hDC,&rect); }
        SelectObject(draw->hDC,font); return TRUE;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: SetTextColor((HDC)wparam,INK); SetBkColor((HDC)wparam,PAPER); return (LRESULT)c->paper;
    case WM_COMMAND: {
        int id=LOWORD(wparam);
        if(id==YEAR_EDIT) { if(HIWORD(wparam)==EN_KILLFOCUS && !c->changing) commit_year(c); return 0; }
        if(id==REMOVE_DATE) { choose_day(c,0); return 0; }
        if(id==TODAY) {
            unsigned today=calendar_today(); c->month=today/100%100; calendar_set_year(window,(int)(today/10000)); c->focus=today;
            if(!c->picker) { c->scroll=(c->month-1)/c->columns*c->month_height; layout_calendar(c); }
        }
        else if(id==BACK_MONTH || id==NEXT_MONTH) {
            int month=c->month+(id==BACK_MONTH ? -1 : 1), year=c->year;
            if(month<1) { month=12; --year; } else if(month>12) { month=1; ++year; }
            if(year>=DATE_MIN_YEAR && year<=DATE_MAX_YEAR) { c->month=month; calendar_set_year(window,year); }
        } else return 0;
        SetFocus(window); InvalidateRect(window,NULL,FALSE); return 0;
    }
    case WM_VSCROLL: {
        SCROLLINFO scroll={sizeof(scroll),SIF_ALL}; int position;
        GetScrollInfo(window,SB_VERT,&scroll); position=c->scroll;
        switch(LOWORD(wparam)) {
        case SB_LINEUP: position-=c->cell; break;
        case SB_LINEDOWN: position+=c->cell; break;
        case SB_PAGEUP: position-=(int)scroll.nPage; break;
        case SB_PAGEDOWN: position+=(int)scroll.nPage; break;
        case SB_TOP: position=0; break;
        case SB_BOTTOM: position=scroll.nMax; break;
        case SB_THUMBTRACK: position=scroll.nTrackPos; break;
        }
        c->scroll=max(0,min(position,scroll.nMax-(int)scroll.nPage+1));
        SetScrollPos(window,SB_VERT,c->scroll,TRUE); InvalidateRect(window,NULL,FALSE); return 0;
    }
    case WM_MOUSEWHEEL:
        c->wheel+=GET_WHEEL_DELTA_WPARAM(wparam);
        while(abs(c->wheel)>=WHEEL_DELTA) {
            int direction=c->wheel>0 ? -1 : 1; c->wheel+=direction*WHEEL_DELTA;
            if(c->picker) SendMessageW(window,WM_COMMAND,direction<0 ? BACK_MONTH : NEXT_MONTH,0);
            else { c->scroll=max(0,c->scroll+direction*c->cell*3); layout_calendar(c); }
        }
        return 0;
    case WM_MOUSEMOVE: {
        POINT point={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
        unsigned date=hit_day(c,point);
        if(!c->tracking) {
            TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,window,0}; c->tracking=TrackMouseEvent(&track);
        }
        if(date!=c->hover) { invalidate_day(c,c->hover); c->hover=date; invalidate_day(c,date); }
        return 0;
    }
    case WM_MOUSELEAVE: invalidate_day(c,c->hover); c->hover=0; c->tracking=FALSE; return 0;
    case WM_LBUTTONDOWN: {
        POINT point={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
        SetFocus(window); c->pressed=hit_day(c,point); if(c->pressed) SetCapture(window); return 0;
    }
    case WM_LBUTTONUP: {
        POINT point={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
        unsigned date=hit_day(c,point), pressed=c->pressed;
        c->pressed=0; if(GetCapture()==window) ReleaseCapture();
        if(date && date==pressed) { focus_day(c,date); choose_day(c,date); }
        return 0;
    }
    case WM_CAPTURECHANGED: c->pressed=0; return 0;
    case WM_SETFOCUS: case WM_KILLFOCUS: invalidate_day(c,c->focus); return 0;
    case WM_KEYDOWN:
        if(!c->picker && ((wparam=='F' && GetKeyState(VK_CONTROL)<0) || wparam==VK_F5))
            return SendMessageW(c->owner,message,wparam,lparam);
        if(wparam==VK_ESCAPE) { if(c->picker) DestroyWindow(window); else SetFocus(GetParent(window)); return 0; }
        if(wparam==VK_TAB) {
            if(GetKeyState(VK_SHIFT)>=0) SetFocus(c->year_edit);
            else if(c->picker) SetFocus(GetDlgItem(window,REMOVE_DATE));
            else notify_calendar(c,CALENDAR_LEAVE,1);
            return 0;
        }
        if(wparam==VK_RETURN || wparam==VK_SPACE) { choose_day(c,c->focus); return 0; }
        if(wparam==VK_LEFT || wparam==VK_RIGHT || wparam==VK_UP || wparam==VK_DOWN) {
            unsigned date=date_shift(c->focus,wparam==VK_LEFT ? -1 : wparam==VK_RIGHT ? 1 : wparam==VK_UP ? -7 : 7);
            RECT rect,client;
            int previous_month=c->month, previous_scroll=c->scroll;
            c->month=date/100%100;
            if((int)(date/10000)!=c->year) calendar_set_year(window,(int)(date/10000));
            focus_day(c,date);
            if(!c->picker && calendar_day_rect(window,date,&rect)) {
                GetClientRect(window,&client);
                if(rect.top<px(c,46)) c->scroll=max(0,c->scroll+rect.top-px(c,46));
                if(rect.bottom>client.bottom) c->scroll+=rect.bottom-client.bottom;
                if(c->scroll!=previous_scroll) layout_calendar(c);
            }
            if(c->picker && c->month!=previous_month) InvalidateRect(window,NULL,FALSE);
            return 0;
        }
        break;
    case WM_NOTIFY:
        if(((NMHDR *)lparam)->code==TTN_GETDISPINFOW) {
            NMTTDISPINFOW *tip=(NMTTDISPINFOW *)lparam;
            if(c->hover && c->picker) swprintf_s(c->tip,96,L"%04u-%02u-%02u",c->hover/10000,c->hover/100%100,c->hover%100);
            else if(c->hover) swprintf_s(c->tip,96,L"%04u-%02u-%02u · %u notes",c->hover/10000,c->hover/100%100,c->hover%100,c->counts[date_ordinal(c->hover)]);
            else wcscpy_s(c->tip,96,L"Type a year and press Enter");
            tip->lpszText=c->tip; return 0;
        }
        break;
    case WM_ACTIVATE: if(c->picker && LOWORD(wparam)==WA_INACTIVE) DestroyWindow(window); return 0;
    case WM_NCDESTROY:
        if(active_picker==window) active_picker=NULL;
        if(c->tooltip) DestroyWindow(c->tooltip);
        if(c->buffer) { SelectObject(c->buffer,c->original_bitmap); DeleteDC(c->buffer); }
        if(c->bitmap) DeleteObject(c->bitmap);
        if(c->damage) DeleteObject(c->damage);
        DeleteObject(c->font); DeleteObject(c->bold); DeleteObject(c->paper);
        SetWindowLongPtrW(window,GWLP_USERDATA,0); free(c); break;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}

static HWND create_calendar(HWND owner, int id, POINT anchor, unsigned date, UINT message) {
    WNDCLASSW cls={0}; Calendar initial={0}; HWND window;
    BOOL picker=message!=0;
    cls.hInstance=GetModuleHandleW(NULL); cls.hCursor=LoadCursorW(NULL,IDC_ARROW);
    cls.lpfnWndProc=calendar_proc; cls.lpszClassName=CALENDAR_CLASS;
    if(!RegisterClassW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return NULL;
    initial.owner=owner; initial.message=message; initial.picker=picker;
    initial.selected=date_valid(date)?date:0; initial.focus=initial.selected ? date : calendar_today();
    initial.year=(int)(initial.focus/10000); initial.month=initial.focus/100%100;
    window=CreateWindowExW(picker?WS_EX_TOOLWINDOW|WS_EX_TOPMOST:WS_EX_CONTROLPARENT,CALENDAR_CLASS,L"Calendar",
        (picker?WS_POPUP:WS_CHILD|WS_VSCROLL)|WS_CLIPCHILDREN|WS_TABSTOP,0,0,1,1,owner,
        picker?NULL:(HMENU)(INT_PTR)id,cls.hInstance,&initial);
    if(window && picker) {
        MONITORINFO monitor={sizeof(monitor)}; int dpi=(int)GetDpiForWindow(window), width=MulDiv(320,dpi,96), height=MulDiv(334,dpi,96);
        GetMonitorInfoW(MonitorFromPoint(anchor,MONITOR_DEFAULTTONEAREST),&monitor);
        anchor.x=max(monitor.rcWork.left,min(anchor.x,monitor.rcWork.right-width));
        anchor.y=max(monitor.rcWork.top,min(anchor.y,monitor.rcWork.bottom-height));
        active_picker=window; SetWindowPos(window,HWND_TOPMOST,anchor.x,anchor.y,width,height,SWP_SHOWWINDOW);
        SetForegroundWindow(window); SetFocus(window);
    }
    return window;
}

HWND calendar_create(HWND parent, int id) { POINT point={0}; return create_calendar(parent,id,point,0,0); }
HWND calendar_picker(HWND owner, POINT anchor, unsigned selected, UINT message) {
    if(active_picker) DestroyWindow(active_picker);
    return create_calendar(owner,0,anchor,selected,message);
}

static void open_day_item(HWND window, DayList *list, int index) {
    wchar_t id[80]; BOOL (*open)(const wchar_t *)=list->open; HWND owner=GetWindow(window,GW_OWNER);
    if(index<0 || index>=list->count) return;
    wcscpy_s(id,80,list->items[index].id); DestroyWindow(window);
    if(!open(id)) MessageBoxW(owner,L"This note could not be opened.",L"Jotilla",MB_OK|MB_ICONERROR);
}

static LRESULT CALLBACK list_input(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data) {
    HWND parent=GetParent(window);
    (void)id; (void)data;
    if(message==WM_KEYDOWN && wparam==VK_ESCAPE) {
        DayList *list=(DayList *)GetWindowLongPtrW(parent,GWLP_USERDATA);
        HWND focus=list->previous_focus, owner=GetWindow(parent,GW_OWNER);
        DestroyWindow(parent); SetForegroundWindow(owner);
        if(IsWindow(focus)) SetFocus(focus);
        return 0;
    }
    if((message==WM_KEYDOWN && wparam==VK_RETURN) || message==WM_LBUTTONUP) {
        if(message==WM_LBUTTONUP) {
            DWORD hit=(DWORD)SendMessageW(window,LB_ITEMFROMPOINT,0,lparam);
            LRESULT result=DefSubclassProc(window,message,wparam,lparam);
            if(HIWORD(hit)) return result;
        }
        SendMessageW(parent,WM_COMMAND,MAKEWPARAM(1,LBN_DBLCLK),(LPARAM)window); return 0;
    }
    return DefSubclassProc(window,message,wparam,lparam);
}

static LRESULT CALLBACK day_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    DayList *list=(DayList *)GetWindowLongPtrW(window,GWLP_USERDATA);
    if(message==WM_NCCREATE) {
        DayList *initial=(DayList *)((CREATESTRUCTW *)lparam)->lpCreateParams;
        list=(DayList *)calloc(1,sizeof(*list)); if(!list) return FALSE;
        *list=*initial; initial->items=NULL;
        SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)list);
    }
    if(!list) return DefWindowProcW(window,message,wparam,lparam);
    switch(message) {
    case WM_CREATE: {
        RECT rect; int i, header=MulDiv(28,list->dpi,96);
        GetClientRect(window,&rect);
        list->list=CreateWindowExW(0,L"LISTBOX",L"Notes for this day",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|
            LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT,0,header,rect.right,rect.bottom-header,window,(HMENU)1,GetModuleHandleW(NULL),NULL);
        if(!list->list) return -1;
        list->font=CreateFontW(-MulDiv(11,list->dpi,72),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
        SendMessageW(list->list,WM_SETFONT,(WPARAM)list->font,FALSE);
        SendMessageW(list->list,LB_INITSTORAGE,list->count,(LPARAM)(list->count*32));
        for(i=0;i<list->count;++i) if(SendMessageW(list->list,LB_ADDSTRING,0,(LPARAM)list->items[i].title)<0) return -1;
        SendMessageW(list->list,LB_SETCURSEL,0,0); SetWindowSubclass(list->list,list_input,1,0); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint; HDC dc=BeginPaint(window,&paint); RECT rect;
        wchar_t title[64]; HGDIOBJ font=SelectObject(dc,list->font);
        GetClientRect(window,&rect); rect.bottom=MulDiv(28,list->dpi,96);
        SetDCBrushColor(dc,PAPER); FillRect(dc,&rect,(HBRUSH)GetStockObject(DC_BRUSH));
        SetTextColor(dc,INK); SetBkMode(dc,TRANSPARENT); GetWindowTextW(window,title,64);
        InflateRect(&rect,-MulDiv(10,list->dpi,96),0);
        DrawTextW(dc,title,-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        SelectObject(dc,font); EndPaint(window,&paint); return 0;
    }
    case WM_MEASUREITEM: ((MEASUREITEMSTRUCT *)lparam)->itemHeight=MulDiv(32,list->dpi,96); return TRUE;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *draw=(DRAWITEMSTRUCT *)lparam;
        RECT rect=draw->rcItem; CalendarItem *item; HGDIOBJ font;
        if(draw->itemID>=(UINT)list->count) return TRUE;
        item=&list->items[draw->itemID]; SetDCBrushColor(draw->hDC,item->paper);
        FillRect(draw->hDC,&rect,(HBRUSH)GetStockObject(DC_BRUSH));
        SetTextColor(draw->hDC,item->ink); SetBkMode(draw->hDC,TRANSPARENT); font=SelectObject(draw->hDC,list->font);
        InflateRect(&rect,-MulDiv(10,list->dpi,96),0);
        DrawTextW(draw->hDC,item->title,-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        if(draw->itemState&ODS_SELECTED) {
            rect=draw->rcItem; InflateRect(&rect,-1,-1); SetDCBrushColor(draw->hDC,item->ink);
            FrameRect(draw->hDC,&rect,(HBRUSH)GetStockObject(DC_BRUSH));
            InflateRect(&rect,-1,-1); FrameRect(draw->hDC,&rect,(HBRUSH)GetStockObject(DC_BRUSH));
        }
        SelectObject(draw->hDC,font); return TRUE;
    }
    case WM_COMMAND:
        if(HIWORD(wparam)==LBN_DBLCLK) open_day_item(window,list,(int)SendMessageW(list->list,LB_GETCURSEL,0,0));
        return 0;
    case WM_ACTIVATE: if(LOWORD(wparam)==WA_INACTIVE) DestroyWindow(window); return 0;
    case WM_DPICHANGED: DestroyWindow(window); return 0;
    case WM_NCDESTROY:
        if(active_list==window) active_list=NULL;
        DeleteObject(list->font); free(list->items); free(list); SetWindowLongPtrW(window,GWLP_USERDATA,0); break;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}

void calendar_list_close(HWND owner) {
    if(active_list && GetWindow(active_list,GW_OWNER)==owner) DestroyWindow(active_list);
}

HWND calendar_list(HWND owner, POINT anchor, unsigned date, CalendarItem *items, int count, BOOL (*open)(const wchar_t *)) {
    WNDCLASSW cls={0}; MONITORINFO monitor={sizeof(monitor)}; DayList initial={0}, *list=&initial; HWND window;
    int width,height; wchar_t title[64]; RECT bounds;
    if(active_list) DestroyWindow(active_list);
    if(count<1) { free(items); return NULL; }
    cls.hInstance=GetModuleHandleW(NULL); cls.hCursor=LoadCursorW(NULL,IDC_ARROW); cls.lpfnWndProc=day_proc; cls.lpszClassName=CALENDAR_LIST_CLASS;
    if(!RegisterClassW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) { free(items); return NULL; }
    list->items=items; list->count=count; list->open=open; list->dpi=(int)GetDpiForWindow(owner);
    list->previous_focus=GetFocus();
    width=MulDiv(320,list->dpi,96); height=MulDiv(32,list->dpi,96)*min(count,8)+MulDiv(28,list->dpi,96);
    SetRect(&bounds,0,0,width,height);
    AdjustWindowRectExForDpi(&bounds,WS_POPUP|WS_BORDER,FALSE,WS_EX_TOOLWINDOW|WS_EX_TOPMOST,list->dpi);
    width=bounds.right-bounds.left; height=bounds.bottom-bounds.top;
    GetMonitorInfoW(MonitorFromPoint(anchor,MONITOR_DEFAULTTONEAREST),&monitor);
    anchor.x=max(monitor.rcWork.left,min(anchor.x,monitor.rcWork.right-width));
    anchor.y=max(monitor.rcWork.top,min(anchor.y,monitor.rcWork.bottom-height));
    swprintf_s(title,64,L"%04u-%02u-%02u · %d notes",date/10000,date/100%100,date%100,count);
    window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,CALENDAR_LIST_CLASS,title,WS_POPUP|WS_BORDER|WS_CLIPCHILDREN,
        anchor.x,anchor.y,width,height,owner,NULL,cls.hInstance,list);
    /* A failed WM_CREATE runs WM_NCDESTROY; a failed WM_NCCREATE does not. */
    free(initial.items);
    if(!window) return NULL;
    list=(DayList *)GetWindowLongPtrW(window,GWLP_USERDATA);
    active_list=window; ShowWindow(window,SW_SHOWNORMAL); SetForegroundWindow(window); SetFocus(list->list); return window;
}
