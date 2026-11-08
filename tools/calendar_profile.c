/* Real Win32 hover workload; no archive, input injection or user data. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned text_calls, tracking_calls, bitmap_calls;
static int measured_text(HDC dc, LPCWSTR text, int length, LPRECT rect, UINT format) {
    ++text_calls; return DrawTextW(dc,text,length,rect,format);
}
static BOOL measured_tracking(LPTRACKMOUSEEVENT event) {
    ++tracking_calls; return TrackMouseEvent(event);
}
static HBITMAP measured_bitmap(HDC dc, int width, int height) {
    ++bitmap_calls; return CreateCompatibleBitmap(dc,width,height);
}
#define DrawTextW measured_text
#define TrackMouseEvent measured_tracking
#define CreateCompatibleBitmap measured_bitmap
#include "../src/calendar.c"
#undef DrawTextW
#undef TrackMouseEvent
#undef CreateCompatibleBitmap

static unsigned long long dirty_area(HWND window, HRGN region) {
    union { RGNDATA header; BYTE bytes[4096]; } data;
    unsigned long long pixels=0;
    DWORD i;
    if(GetUpdateRgn(window,region,FALSE)==ERROR || !GetRegionData(region,sizeof(data),&data.header)) return 0;
    for(i=0;i<data.header.rdh.nCount;++i) {
        const RECT *r=(const RECT *)data.header.Buffer+i;
        pixels+=(unsigned long long)(r->right-r->left)*(r->bottom-r->top);
    }
    return pixels;
}

static void profile(HWND window, const char *name, BOOL moving) {
    RECT first,second;
    HRGN dirty=CreateRectRgn(0,0,0,0);
    LARGE_INTEGER frequency,start,end;
    unsigned long long pixels=0;
    unsigned gdi_before,gdi_after;
    int i;
    calendar_set_year(window,2028);
    calendar_day_rect(window,20280110,&first); calendar_day_rect(window,20280111,&second);
    SendMessageW(window,WM_MOUSELEAVE,0,0); UpdateWindow(window); GdiFlush();
    text_calls=tracking_calls=bitmap_calls=0;
    gdi_before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
    for(i=0;i<1000;++i) {
        RECT *r=moving && (i&1) ? &second : &first;
        SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM((r->left+r->right)/2,(r->top+r->bottom)/2));
        pixels+=dirty_area(window,dirty); UpdateWindow(window); GdiFlush();
    }
    QueryPerformanceCounter(&end);
    gdi_after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    printf("%s,%s,1000,%.3f,%llu,%u,%u,%u,%d\n",name,moving?"adjacent_days":"same_day",
        (end.QuadPart-start.QuadPart)*1000.0/frequency.QuadPart,pixels,text_calls,tracking_calls,bitmap_calls,
        (int)gdi_after-(int)gdi_before);
    DeleteObject(dirty);
}

int wmain(void) {
    HWND owner,annual,picker;
    POINT anchor={30,30};
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    owner=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Jotila calendar measurement",WS_POPUP|WS_CLIPCHILDREN,
        20,20,850,650,NULL,NULL,GetModuleHandleW(NULL),NULL);
    if(!owner) return 1;
    annual=calendar_create(owner,11);
    if(!annual) { DestroyWindow(owner); return 1; }
    SetWindowPos(annual,NULL,0,0,850,650,SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    ShowWindow(owner,SW_SHOWNOACTIVATE); UpdateWindow(owner); UpdateWindow(annual);
    puts("calendar,workload,moves,elapsed_ms,invalidated_pixels,text_calls,tracking_calls,bitmap_allocations,gdi_delta");
    profile(annual,"annual",TRUE); profile(annual,"annual",FALSE);
    ShowWindow(owner,SW_HIDE);
    picker=calendar_picker(owner,anchor,20280110,WM_APP+1);
    if(!picker) { DestroyWindow(owner); return 1; }
    profile(picker,"picker",TRUE); profile(picker,"picker",FALSE);
    DestroyWindow(picker); DestroyWindow(owner);
    return 0;
}
