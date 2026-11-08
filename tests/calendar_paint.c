/* Pixel equivalence: incremental updates must match a complete render. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
static unsigned test_today=20280110;
static BOOL fail_bitmap;
static void test_clock(LPSYSTEMTIME time) {
    GetLocalTime(time); time->wYear=(WORD)(test_today/10000);
    time->wMonth=(WORD)(test_today/100%100); time->wDay=(WORD)(test_today%100);
}
static HBITMAP test_bitmap(HDC dc,int width,int height) {
    return fail_bitmap ? NULL : CreateCompatibleBitmap(dc,width,height);
}
#define GetLocalTime test_clock
#define CreateCompatibleBitmap test_bitmap
#include "../src/calendar.c"
#undef GetLocalTime
#undef CreateCompatibleBitmap

static unsigned comparisons;
static void require(BOOL condition, const char *message) {
    if (!condition) { fprintf(stderr,"FAIL: %s (%lu)\n",message,GetLastError()); exit(1); }
}

static void equivalent(HWND window) {
    Calendar *c=(Calendar *)GetWindowLongPtrW(window,GWLP_USERDATA);
    BITMAPINFO info={0};
    HDC reference=CreateCompatibleDC(NULL), actual=CreateCompatibleDC(NULL);
    HBITMAP expected_bitmap, actual_bitmap;
    HGDIOBJ old_expected,old_actual;
    DWORD *expected,*pixels;
    RECT client;
    size_t i,total;
    UpdateWindow(window); GetClientRect(window,&client);
    require(c->bitmap!=NULL,"paint owns a reusable buffer");
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=client.right;
    info.bmiHeader.biHeight=-client.bottom; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    expected_bitmap=CreateDIBSection(NULL,&info,DIB_RGB_COLORS,(void **)&expected,NULL,0);
    actual_bitmap=CreateDIBSection(NULL,&info,DIB_RGB_COLORS,(void **)&pixels,NULL,0);
    require(reference && actual && expected_bitmap && actual_bitmap,"allocate comparison surfaces");
    old_expected=SelectObject(reference,expected_bitmap); old_actual=SelectObject(actual,actual_bitmap);
    SendMessageW(window,WM_PRINTCLIENT,(WPARAM)reference,PRF_CLIENT);
    require(BitBlt(actual,0,0,client.right,client.bottom,c->buffer,0,0,SRCCOPY),"copy current buffered pixels");
    GdiFlush(); total=(size_t)client.right*client.bottom;
    for(i=0;i<total;++i) if((expected[i]&0xffffff)!=(pixels[i]&0xffffff)) {
        fprintf(stderr,"pixel %zu (%zu,%zu), dpi %d, scroll %d, hover %u, focus %u: %06lx != %06lx\n",
            i,i%(size_t)client.right,i/(size_t)client.right,c->dpi,c->scroll,c->hover,c->focus,
            expected[i]&0xffffff,pixels[i]&0xffffff);
        require(FALSE,"incremental pixels equal a full render");
    }
    ++comparisons;
    SelectObject(reference,old_expected); SelectObject(actual,old_actual);
    DeleteObject(expected_bitmap); DeleteObject(actual_bitmap); DeleteDC(reference); DeleteDC(actual);
}

static void hover(HWND window, unsigned date) {
    RECT rect;
    require(calendar_day_rect(window,date,&rect),"locate hover cell");
    SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM((rect.left+rect.right)/2,(rect.top+rect.bottom)/2));
    equivalent(window);
}

static void exercise(HWND window, BOOL picker) {
    const int dpis[]={96,120,144,192};
    Calendar *c=(Calendar *)GetWindowLongPtrW(window,GWLP_USERDATA);
    unsigned counts[366]={0};
    int dpi,width,i;
    calendar_set_year(window,2028);
    for(dpi=0;dpi<4;++dpi) {
        c->dpi=dpis[dpi]; calendar_fonts(c);
        for(width=0;width<(picker?1:4);++width) {
            c->focus=20280101; c->month=1; c->scroll=0; calendar_set_year(window,2028);
            SetWindowPos(window,NULL,0,0,picker?px(c,320):px(c,180+width*165),
                picker?px(c,334):px(c,660),SWP_NOZORDER|SWP_NOACTIVATE);
            layout_calendar(c); equivalent(window);
            hover(window,20280110); hover(window,20280111); hover(window,20280128);
            require(!GetUpdateRect(window,NULL,FALSE),"paint consumes damage");
            {
                RECT cell;
                calendar_day_rect(window,20280128,&cell);
                SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(cell.left+1,cell.top+1));
                require(!GetUpdateRect(window,NULL,FALSE),"same-cell mouse motion leaves no damage");
                equivalent(window);
            }
            test_today=20280111; invalidate_day(c,20280128); equivalent(window);
            test_today=20280110; invalidate_day(c,20280128); equivalent(window);
            SendMessageW(window,WM_MOUSELEAVE,0,0); equivalent(window);
            counts[9]=1; counts[27]=2; calendar_counts(window,counts); equivalent(window);
            counts[9]=0; counts[27]=1; calendar_counts(window,counts); equivalent(window);
            calendar_counts(window,counts);
            require(!GetUpdateRect(window,NULL,FALSE),"unchanged counts require no repaint");
            SetFocus(window); equivalent(window);
            for(i=0;i<36;++i) { SendMessageW(window,WM_KEYDOWN,VK_RIGHT,0); equivalent(window); }
            if(picker) { c->month=1; calendar_set_year(window,2028); equivalent(window); }
            else {
                SendMessageW(window,WM_VSCROLL,SB_BOTTOM,0); equivalent(window);
                SendMessageW(window,WM_VSCROLL,SB_TOP,0); equivalent(window);
                SetFocus(c->year_edit); equivalent(window);
            }
        }
    }
    calendar_set_year(window,2100); equivalent(window);
    calendar_set_year(window,2000); equivalent(window);
    c->month=2; c->focus=20000229; calendar_set_year(window,2100);
    require(c->focus==21000228,"changing year clamps February 29 to a valid day");
    calendar_set_year(window,2100);
    require(c->focus==21000228,"committing an unchanged year preserves keyboard focus");
    equivalent(window);
    fail_bitmap=TRUE;
    SetWindowPos(window,NULL,0,0,picker?620:740,picker?700:810,SWP_NOZORDER|SWP_NOACTIVATE); UpdateWindow(window);
    fail_bitmap=FALSE; invalidate_day(c,c->focus); equivalent(window);
}

int wmain(void) {
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_STANDARD_CLASSES};
    HWND owner,calendar;
    POINT anchor={20,20};
    DWORD initial,final;
    int i;
    InitCommonControlsEx(&controls);
    owner=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Calendar rendering checks",WS_POPUP|WS_CLIPCHILDREN,
        0,0,1500,1400,NULL,NULL,GetModuleHandleW(NULL),NULL);
    require(owner!=NULL,"create isolated owner"); ShowWindow(owner,SW_SHOWNOACTIVATE);
    calendar=calendar_create(owner,11); require(calendar!=NULL,"create annual view");
    ShowWindow(calendar,SW_SHOW); exercise(calendar,FALSE); DestroyWindow(calendar);
    calendar=calendar_picker(owner,anchor,20280110,WM_APP+1); require(calendar!=NULL,"create month picker");
    exercise(calendar,TRUE); DestroyWindow(calendar);
    initial=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(i=0;i<30;++i) {
        calendar=calendar_create(owner,11); require(calendar!=NULL,"recreate calendar");
        SetWindowPos(calendar,NULL,0,0,600,500,SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        UpdateWindow(calendar); DestroyWindow(calendar);
    }
    final=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    require(final==initial,"opening and closing calendars leaks no GDI objects");
    DestroyWindow(owner);
    printf("PASS: %u pixel comparisons, 4 DPIs, 4 annual widths, hover, focus, counts, scrolling, keyboard, resizing and 30 resource lifecycles.\n",comparisons);
    return 0;
}
