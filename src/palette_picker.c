/* Owner-bound native popup. Its cached alpha surface is redrawn only on input
   or during a short transition; settling always removes the animation timer. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdlib.h>
#include <string.h>
#include "palette_picker.h"
#include "palette_art.h"

enum { ANIMATION_TIMER=1, TRANSITION_MS=120 };
typedef struct Picker {
    PalettePickerOptions options;
    PaletteArt *art;
    HWND owner, tooltip;
    int palette, hover, pressed;
    BYTE weight[COLOR_COUNT], from[COLOR_COUNT], target[COLOR_COUNT], alpha, from_alpha;
    ULONGLONG started;
    BOOL tracking, animate;
} Picker;
static HWND active_picker;

static void render(HWND window, Picker *p) {
    BLENDFUNCTION blend={AC_SRC_OVER,0,p->alpha,AC_SRC_ALPHA};
    POINT source={0,0};
    const PaletteLayout *layout=palette_art_layout(p->art);
    SIZE size=layout->size;
    HDC dc=palette_art_frame(p->art,p->weight,p->hover,p->pressed==p->hover && p->pressed!=PICK_NONE);
    UpdateLayeredWindow(window,NULL,NULL,&size,dc,&source,0,&blend,ULW_ALPHA);
}

static void transition(HWND window, Picker *p) {
    int i;
    memcpy(p->from,p->weight,sizeof(p->from)); p->from_alpha=p->alpha;
    for(i=0;i<COLOR_COUNT;++i)
        p->target[i]=(BYTE)(p->hover==i ? 220 :
            p->palette==p->options.palette && i==p->options.color ? 100 : 0);
    p->started=GetTickCount64();
    if(!p->animate || (p->alpha==255 && !memcmp(p->from,p->target,sizeof(p->from))) ||
        !SetTimer(window,ANIMATION_TIMER,16,NULL)) {
        memcpy(p->weight,p->target,sizeof(p->weight)); p->alpha=255;
        KillTimer(window,ANIMATION_TIMER);
    }
    render(window,p);
}

static void tooltip_text(HWND window, Picker *p, UINT message) {
    TOOLINFOW tool={sizeof(tool)};
    tool.uFlags=TTF_SUBCLASS; tool.hwnd=window; tool.uId=1;
    tool.rect=palette_art_layout(p->art)->favorite;
    tool.lpszText=p->palette==p->options.favorite ? L"Remove startup favorite (F)" : L"Use this palette when Jotila starts (F)";
    SendMessageW(p->tooltip,message,0,(LPARAM)&tool);
}

static void change_palette(HWND window, Picker *p, int step) {
    p->palette=(p->palette+step+PALETTE_COUNT)%PALETTE_COUNT;
    p->hover=p->pressed=PICK_NONE;
    memset(p->weight,0,sizeof(p->weight));
    palette_art_prepare(p->art,p->palette,p->options.favorite,p->options.text_mode);
    SetWindowTextW(window,PALETTE_NAMES[p->palette]);
    tooltip_text(window,p,TTM_UPDATETIPTEXTW);
    transition(window,p);
}

static void favorite(HWND window, Picker *p) {
    int value=p->palette==p->options.favorite ? -1 : p->palette;
    if(!p->options.favorite_changed) return;
    /* A failed save may display a dialog, deactivating and destroying us. */
    if(!p->options.favorite_changed(p->options.context,value) || !IsWindow(window) ||
        (Picker *)GetWindowLongPtrW(window,GWLP_USERDATA)!=p) return;
    p->options.favorite=value;
    palette_art_prepare(p->art,p->palette,value,p->options.text_mode);
    tooltip_text(window,p,TTM_UPDATETIPTEXTW);
    render(window,p);
}

static void finish(HWND window, Picker *p, int color, BOOL restore_focus) {
    HWND owner=p->owner;
    PaletteSelection select=p->options.select;
    void *context=p->options.context;
    int palette=p->palette;
    DestroyWindow(window);
    if(!IsWindow(owner)) return;
    if(color>=0) select(context,palette,color);
    if(restore_focus && IsWindow(owner)) {
        SetForegroundWindow(owner); SetFocus(GetDlgItem(owner,10));
    }
}

static LRESULT CALLBACK picker_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    Picker *p=(Picker *)GetWindowLongPtrW(window,GWLP_USERDATA);
    if(message==WM_NCCREATE) {
        const Picker *initial=(const Picker *)((CREATESTRUCTW *)lparam)->lpCreateParams;
        p=(Picker *)malloc(sizeof(*p));
        if(!p) return FALSE;
        *p=*initial; SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)p);
    }
    if(!p) return DefWindowProcW(window,message,wparam,lparam);
    switch(message) {
    case WM_CREATE: {
        int dpi=(int)GetDpiForWindow(p->owner);
        p->art=palette_art_create(dpi?dpi:96);
        if(!p->art) return -1;
        palette_art_prepare(p->art,p->palette,p->options.favorite,p->options.text_mode);
        p->animate=TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&p->animate,0);
        p->tooltip=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE,TOOLTIPS_CLASSW,NULL,
            WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,window,NULL,GetModuleHandleW(NULL),NULL);
        if(p->tooltip) tooltip_text(window,p,TTM_ADDTOOLW);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        BeginPaint(window,&paint); EndPaint(window,&paint); return 0;
    }
    case WM_PRINTCLIENT: {
        const PaletteLayout *l=palette_art_layout(p->art);
        HDC dc=palette_art_frame(p->art,p->weight,p->hover,FALSE);
        BitBlt((HDC)wparam,0,0,l->size.cx,l->size.cy,dc,0,0,SRCCOPY); return 0;
    }
    case WM_TIMER:
        if(wparam==ANIMATION_TIMER) {
            ULONGLONG elapsed=GetTickCount64()-p->started;
            unsigned t=(unsigned)min(elapsed,TRANSITION_MS)*256/TRANSITION_MS;
            unsigned ease=256-(256-t)*(256-t)*(256-t)/65536;
            int i;
            for(i=0;i<COLOR_COUNT;++i)
                p->weight[i]=(BYTE)((p->from[i]*(256-ease)+p->target[i]*ease+128)/256);
            p->alpha=(BYTE)((p->from_alpha*(256-ease)+255*ease+128)/256);
            if(elapsed>=TRANSITION_MS) KillTimer(window,ANIMATION_TIMER);
            render(window,p); return 0;
        }
        break;
    case WM_NCHITTEST: {
        POINT point={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
        ScreenToClient(window,&point);
        return palette_art_contains(p->art,point)?HTCLIENT:HTTRANSPARENT;
    }
    case WM_MOUSEMOVE: {
        POINT point={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
        int hit=palette_art_hit(p->art,point);
        if(p->hover!=hit) { p->hover=hit; transition(window,p); }
        if(!p->tracking) {
            TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,window,0};
            p->tracking=TrackMouseEvent(&track);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        p->tracking=FALSE; p->hover=PICK_NONE; transition(window,p); return 0;
    case WM_LBUTTONDOWN: {
        POINT point={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
        p->pressed=palette_art_hit(p->art,point);
        if(p->pressed!=PICK_NONE) SetCapture(window);
        render(window,p); return 0;
    }
    case WM_LBUTTONUP: {
        POINT point={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
        int hit=palette_art_hit(p->art,point),pressed=p->pressed;
        if(GetCapture()==window) ReleaseCapture();
        p->pressed=PICK_NONE;
        if(hit==PICK_NONE || hit!=pressed) { render(window,p); return 0; }
        if(hit>=0) finish(window,p,hit,TRUE);
        else if(hit==PICK_FAVORITE) favorite(window,p);
        else change_palette(window,p,hit==PICK_PREVIOUS?-1:1);
        return 0;
    }
    case WM_CAPTURECHANGED: p->pressed=PICK_NONE; render(window,p); return 0;
    case WM_GETDLGCODE: return DLGC_WANTARROWS|DLGC_WANTTAB|DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        if(wparam==VK_LEFT || wparam==VK_RIGHT) { change_palette(window,p,wparam==VK_LEFT?-1:1); return 0; }
        if(wparam==VK_UP || wparam==VK_DOWN || wparam==VK_TAB) {
            int count=PALETTE_SIZES[p->palette];
            int step=wparam==VK_UP || (wparam==VK_TAB && GetKeyState(VK_SHIFT)<0)?-1:1;
            p->hover=p->hover<0?(step<0?count-1:0):(p->hover+step+count)%count;
            transition(window,p); return 0;
        }
        if(wparam==VK_RETURN || wparam==VK_SPACE) {
            int color=p->hover>=0?p->hover:
                p->palette==p->options.palette && p->options.color>=0?p->options.color:0;
            finish(window,p,color,TRUE); return 0;
        }
        if(wparam=='F') { favorite(window,p); return 0; }
        if(wparam==VK_ESCAPE) { finish(window,p,-1,TRUE); return 0; }
        break;
    case WM_ACTIVATE:
        if(LOWORD(wparam)==WA_INACTIVE) DestroyWindow(window);
        return 0;
    case WM_DPICHANGED:
    case WM_DISPLAYCHANGE:
    case WM_CLOSE: finish(window,p,-1,FALSE); return 0;
    case WM_NCDESTROY:
        KillTimer(window,ANIMATION_TIMER);
        if(active_picker==window) active_picker=NULL;
        if(p->tooltip) DestroyWindow(p->tooltip);
        palette_art_destroy(p->art);
        SetWindowLongPtrW(window,GWLP_USERDATA,0); free(p);
        break;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}

HWND palette_picker_show(HWND owner, POINT anchor, const PalettePickerOptions *options) {
    WNDCLASSW cls={0};
    MONITORINFO monitor={sizeof(monitor)};
    Picker initial={0},*p;
    const PaletteLayout *layout;
    HWND window;
    int x,y;
    if(!IsWindow(owner) || !options || !options->select || options->palette<0 || options->palette>=PALETTE_COUNT ||
        options->color<-1 || options->color>=PALETTE_SIZES[options->palette] ||
        options->favorite<-1 || options->favorite>=PALETTE_COUNT) return NULL;
    if(active_picker) DestroyWindow(active_picker);
    if(!GetMonitorInfoW(MonitorFromPoint(anchor,MONITOR_DEFAULTTONEAREST),&monitor)) return NULL;
    cls.hInstance=(HINSTANCE)GetWindowLongPtrW(owner,GWLP_HINSTANCE);
    cls.hCursor=LoadCursorW(NULL,IDC_ARROW); cls.lpfnWndProc=picker_proc; cls.lpszClassName=PALETTE_PICKER_CLASS;
    if(!RegisterClassW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return NULL;
    initial.owner=owner; initial.options=*options; initial.palette=options->palette;
    initial.hover=initial.pressed=PICK_NONE;
    window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_LAYERED,PALETTE_PICKER_CLASS,
        PALETTE_NAMES[options->palette],WS_POPUP,0,0,0,0,owner,NULL,cls.hInstance,&initial);
    if(!window) return NULL;
    p=(Picker *)GetWindowLongPtrW(window,GWLP_USERDATA); layout=palette_art_layout(p->art);
    x=max(monitor.rcWork.left,min(anchor.x-layout->size.cx/2,monitor.rcWork.right-layout->size.cx));
    y=max(monitor.rcWork.top,min(anchor.y-(layout->wheel.top+layout->wheel.bottom)/2,monitor.rcWork.bottom-layout->size.cy));
    SetWindowPos(window,NULL,x,y,layout->size.cx,layout->size.cy,SWP_NOACTIVATE|SWP_NOZORDER);
    active_picker=window; transition(window,p);
    ShowWindow(window,SW_SHOWNORMAL); SetForegroundWindow(window); SetFocus(window);
    return window;
}
