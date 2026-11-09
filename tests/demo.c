/* Actual application screenshots and native menu interactions, on a photo backdrop. */
#define main integration_main
#include "tests.c"
#undef main
static HWND backdrop;
static HBITMAP photo;
static BITMAP photo_info;
static HFONT caption_font;
static const wchar_t *caption;
static BOOL show_cursor;
static BOOL show_click;
static POINT click_position;
static int frame_number, menu_phase, menu_submenu, menu_choice, menu_retries;
static HMENU recording_menu;
static const wchar_t *frame_directory;
static int frame_delay=85, menu_offset=200;
static FILE *frame_timing;
static BOOL recorded_palettes[PALETTE_COUNT];
static const wchar_t *WELCOME_BODY=L"Welcome my son\r\nWelcome to the machine\r\nWhere have you been?";
static const wchar_t *WELCOME_ARCHIVE_ID=L"note-2025-06-18_09-30-00-000-001";

static BOOL recording_unobstructed(const RECT *bounds) {
    HWND window;
    DWORD current=GetCurrentProcessId();
    for (window=GetTopWindow(NULL); window && window!=backdrop; window=GetWindow(window,GW_HWNDNEXT)) {
        DWORD process=0,cloaked=0;
        RECT rect,overlap;
        if (!IsWindowVisible(window) || IsIconic(window)) continue;
        GetWindowThreadProcessId(window,&process);
        if (process==current) continue;
        if (SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) && cloaked) continue;
        if (GetWindowRect(window,&rect) && IntersectRect(&overlap,bounds,&rect)) {
            wchar_t name[80];
            GetClassNameW(window,name,80);
            fprintf(stderr,"Recording obstructed by %ls at frame %d.\n",name,frame_number);
            return FALSE;
        }
    }
    return window==backdrop;
}

static LRESULT CALLBACK backdrop_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if (message==WM_PAINT) {
        PAINTSTRUCT paint;
        RECT rect;
        HDC dc=BeginPaint(window,&paint);
        GetClientRect(window,&rect);
        HDC source=CreateCompatibleDC(dc);
        HGDIOBJ old=SelectObject(source,photo);
        BitBlt(dc,0,0,rect.right,rect.bottom,source,
               (photo_info.bmWidth-rect.right)/2,(photo_info.bmHeight-rect.bottom)/2,SRCCOPY);
        SelectObject(source,old); DeleteDC(source);
        if (caption) {
            RECT footer={40,904,960,964},shadow=footer,border=footer;
            HBRUSH brush=(HBRUSH)GetStockObject(DC_BRUSH);
            HGDIOBJ font=SelectObject(dc,caption_font);
            OffsetRect(&shadow,6,6);
            SetDCBrushColor(dc,RGB(55,48,38)); FillRect(dc,&shadow,brush);
            SetDCBrushColor(dc,RGB(244,238,226));
            FillRect(dc,&footer,brush);
            SetDCBrushColor(dc,RGB(0,0,0)); FrameRect(dc,&border,brush);
            InflateRect(&border,-1,-1); FrameRect(dc,&border,brush);
            SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(58,52,43));
            DrawTextW(dc,caption,-1,&footer,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            SelectObject(dc,font);
        }
        EndPaint(window,&paint); return 0;
    }
    if (message==WM_ERASEBKGND) return 1;
    return DefWindowProcW(window,message,wparam,lparam);
}

static void capture(const wchar_t *path) {
    RECT rect;
    HDC screen,memory;
    HBITMAP bitmap;
    HGDIOBJ old;
    BITMAPINFO info={0};
    BITMAPFILEHEADER header={0};
    DWORD size,written;
    HANDLE file;
    void *pixels;
    pump(100);
    GetWindowRect(backdrop,&rect);
    if (frame_timing) require(recording_unobstructed(&rect),"do not record windows outside the demo");
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=rect.right-rect.left; info.bmiHeader.biHeight=rect.top-rect.bottom;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    screen=GetDC(NULL); memory=CreateCompatibleDC(screen);
    bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,NULL,0);
    require(bitmap && memory,"allocate demonstration capture");
    old=SelectObject(memory,bitmap);
    require(BitBlt(memory,0,0,rect.right-rect.left,rect.bottom-rect.top,screen,rect.left,rect.top,SRCCOPY|CAPTUREBLT),"capture actual demonstration windows");
    if (show_cursor) {
        CURSORINFO cursor={sizeof(cursor)};
        ICONINFO icon;
        if (GetCursorInfo(&cursor) && (cursor.flags&CURSOR_SHOWING) && GetIconInfo(cursor.hCursor,&icon)) {
            /* Monochrome XOR cursors do not survive bitmap encoding reliably.
               The actual RichEdit caret already marks the insertion point. */
            if (icon.hbmColor && cursor.hCursor != LoadCursorW(NULL, IDC_IBEAM))
                DrawIconEx(memory,cursor.ptScreenPos.x-rect.left-(int)icon.xHotspot,
                           cursor.ptScreenPos.y-rect.top-(int)icon.yHotspot,cursor.hCursor,0,0,0,NULL,DI_NORMAL);
            if (icon.hbmColor) DeleteObject(icon.hbmColor);
            if (icon.hbmMask) DeleteObject(icon.hbmMask);
        }
    }
    if (show_click) {
        int x=click_position.x-rect.left,y=click_position.y-rect.top;
        HPEN halo=CreatePen(PS_SOLID,4,RGB(244,238,226));
        HGDIOBJ previous_pen=SelectObject(memory,halo),previous_brush=SelectObject(memory,GetStockObject(NULL_BRUSH));
        Ellipse(memory,x-11,y-11,x+12,y+12);
        SelectObject(memory,GetStockObject(DC_PEN)); SetDCPenColor(memory,RGB(58,52,43));
        Ellipse(memory,x-11,y-11,x+12,y+12);
        SelectObject(memory,previous_pen); SelectObject(memory,previous_brush); DeleteObject(halo);
    }
    size=(DWORD)((rect.right-rect.left)*(rect.bottom-rect.top)*4);
    header.bfType=0x4d42; header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER); header.bfSize=header.bfOffBits+size;
    file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
    require(file!=INVALID_HANDLE_VALUE,"create demonstration screenshot");
    require(WriteFile(file,&header,sizeof(header),&written,NULL) &&
            WriteFile(file,&info.bmiHeader,sizeof(BITMAPINFOHEADER),&written,NULL) &&
            WriteFile(file,pixels,size,&written,NULL),"write demonstration screenshot");
    CloseHandle(file); SelectObject(memory,old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(NULL,screen);
}

static Note *demo_note(int palette,int color,int size,int x,int y,const wchar_t *title,const wchar_t *body,int opacity) {
    Note *note;
    RECT rect;
    current_palette=palette; next_color=color; shuffled_palette=FALSE; colors_left=0;
    note=new_note(); require(note!=NULL,"create demonstration note");
    require(resize_note(note,size) && set_title(note,title),"configure demonstration note");
    replace_text(note,body); require(!wcscmp(note->accepted_text,body),"demonstration text fits");
    note->opacity=opacity; require(apply_opacity(note),"demonstration transparency");
    GetWindowRect(backdrop,&rect);
    SetWindowPos(note->window,HWND_TOPMOST,rect.left+x,rect.top+y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
    ShowWindow(note->window,SW_SHOWNORMAL); SetForegroundWindow(note->window); UpdateWindow(note->window);
    return note;
}

static void clear_scene(void) {
    archive_close();
    while (notes) destroy_note(notes);
    RedrawWindow(backdrop,NULL,NULL,RDW_INVALIDATE|RDW_UPDATENOW);
}

static void clear_demo_archive(void) {
    wchar_t pattern[PATH_CAP],path[PATH_CAP];
    WIN32_FIND_DATAW entry;
    HANDLE search;
    WritePrivateProfileStringW(NULL,NULL,NULL,state_path);
    swprintf_s(pattern,PATH_CAP,L"%s\\*",directory);
    search=FindFirstFileW(pattern,&entry);
    if (search==INVALID_HANDLE_VALUE) return;
    do {
        if (entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;
        swprintf_s(path,PATH_CAP,L"%s\\%s",directory,entry.cFileName);
        require(DeleteFileW(path),"remove isolated demonstration data");
    } while (FindNextFileW(search,&entry));
    FindClose(search);
}

static void format_overview_note(Note *note) {
    CHARFORMAT2W format={0};
    CHARRANGE paragraph;
    LONG start=0;
    const DWORD effects[]={CFM_BOLD,CFM_ITALIC,CFM_UNDERLINE};
    int i;
    format.cbSize=sizeof(format);
    format.dwMask=CFM_BOLD|CFM_ITALIC|CFM_UNDERLINE|CFM_STRIKEOUT;
    note->validating=TRUE;
    SendMessageW(note->edit,EM_SETSEL,0,-1);
    SendMessageW(note->edit,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&format);
    for (i=0;i<3;++i) {
        paragraph_at(note->edit,start,&paragraph);
        SendMessageW(note->edit,EM_EXSETSEL,0,(LPARAM)&paragraph);
        format.dwMask=format.dwEffects=effects[i];
        SendMessageW(note->edit,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&format);
        start=paragraph.cpMax+1;
    }
    paragraph_at(note->edit,start,&paragraph); strike_todo(note,&paragraph,TRUE);
    SendMessageW(note->edit,EM_SETSEL,(WPARAM)-1,-1);
    note->validating=FALSE;
    require(text_fits(note),"formatting and both checklist states fit in the overview");
    mark_dirty(note, DIRTY_RICH);
}

static void populate_demo_archive(void) {
    static const struct { const wchar_t *id; const char *text; } fixtures[]={
        {L"note-2026-11-03_09-00-00-000-001","#Small wins\r\nReview the draft.\r\nShip one useful thing."},
        {L"note-2026-09-22_10-00-00-000-001","#Autumn ideas\r\nA quieter morning.\r\nA fresh notebook."},
        {L"note-2026-04-12_11-00-00-000-001","#Desk notes\r\nSketch the next idea.\r\nKeep the first version small."},
        {L"note-2026-04-08_08-00-00-000-001","#Spring plans\r\nBook the museum.\r\nFind a good cafe."},
        {L"note-2026-01-17_12-00-00-000-001","#New year\r\nMake room for good ideas."},
        {L"note-2025-12-19_16-00-00-000-001","#Winter break\r\nPack a book.\r\nBring the camera."},
        {L"note-2025-10-05_14-00-00-000-001","#Reading list\r\nOne chapter at a time."},
        {L"note-2025-06-18_09-30-00-000-001","#Welcome to the machine\r\nWelcome my son\r\nWelcome to the machine\r\nWhere have you been?"},
        {L"note-2025-06-03_07-00-00-000-001","#Early summer\r\nA walk before breakfast.\r\nCoffee with a friend."},
        {L"note-2025-02-07_18-00-00-000-001","#First ideas\r\nStart with a blank page."}
    };
    RECT bounds;
    size_t i;
    GetWindowRect(backdrop,&bounds);
    for (i=0;i<sizeof(fixtures)/sizeof(fixtures[0]);++i) {
        archive_fixture(fixtures[i].id,fixtures[i].text);
        require(write_setting(fixtures[i].id,L"color",(int)i%COLOR_COUNT) &&
                write_setting(fixtures[i].id,L"size",4) &&
                write_setting(fixtures[i].id,L"x",bounds.left+315) &&
                write_setting(fixtures[i].id,L"y",bounds.top+490),"configure isolated dated previews");
        require(tags_write(state_path, fixtures[i].id, i % 2 ? L"ideas;personal" : L"plans;work"),
                "tag the demonstration archive");
    }
}

static void capture_frame(void) {
    wchar_t path[80],label[80];
    Note *note;
    HWND popup=thread_window(PALETTE_PICKER_CLASS,NULL);
    int i;
    for(note=notes;note;note=note->next)
        if(IsWindowVisible(note->window)) recorded_palettes[note->palette]=TRUE;
    if(popup) {
        GetWindowTextW(popup,label,80);
        for(i=0;i<POPULAR_PALETTE_COUNT;++i)
            if(!wcscmp(label,PALETTE_NAMES[POPULAR_PALETTES[i]])) recorded_palettes[POPULAR_PALETTES[i]]=TRUE;
    }
    swprintf_s(path,80,L"%s\\frame-%03d.bmp",frame_directory,frame_number++);
    capture(path);
    if (frame_timing) fprintf(frame_timing,"%d|%ls\n",frame_delay,path);
}

static void set_caption(const wchar_t *text) {
    caption=text;
    InvalidateRect(backdrop,NULL,FALSE); UpdateWindow(backdrop);
}

static void set_background(const wchar_t *path) {
    HBITMAP next = (HBITMAP)LoadImageW(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    require(next && GetObjectW(next, sizeof(photo_info), &photo_info), "load the local artwork for this capture");
    if (photo) DeleteObject(photo);
    photo = next;
    if (backdrop) RedrawWindow(backdrop, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
}

static void export_gif_colors(void) {
    const COLORREF ui[]={RGB(244,238,226),RGB(58,52,43),RGB(41,40,32),RGB(100,94,80),
        RGB(178,138,39),RGB(116,88,21),RGB(255,255,255),RGB(0,0,0),
        GetSysColor(COLOR_HIGHLIGHT),GetSysColor(COLOR_HIGHLIGHTTEXT)};
    FILE *file;
    wchar_t path[80];
    int p,c;
    size_t i,count=sizeof(ui)/sizeof(ui[0]);
    for(p=0;p<PALETTE_COUNT;++p) if(recorded_palettes[p]) count+=PALETTE_SIZES[p];
    swprintf_s(path,80,L"%s\\colors.ppm",frame_directory);
    require(_wfopen_s(&file,path,L"w")==0,"export only the application palettes shown in this recording");
    fprintf(file,"P3\n%zu 1\n255\n",count);
    for(p=0;p<PALETTE_COUNT;++p) if(recorded_palettes[p]) for(c=0;c<PALETTE_SIZES[p];++c) {
        COLORREF color=PALETTES[p][c];
        fprintf(file,"%u %u %u\n",GetRValue(color),GetGValue(color),GetBValue(color));
    }
    for(i=0;i<sizeof(ui)/sizeof(ui[0]);++i)
        fprintf(file,"%u %u %u\n",GetRValue(ui[i]),GetGValue(ui[i]),GetBValue(ui[i]));
    require(!ferror(file) && fclose(file)==0,"finish the recording's GIF color catalog");
}

static void begin_recording(const wchar_t *folder) {
    wchar_t timeline[80];
    clear_scene(); clear_demo_archive();
    SetWindowPos(backdrop,NULL,20,20,1000,1000,SWP_NOZORDER|SWP_NOACTIVATE);
    frame_directory=folder; frame_number=0; frame_delay=80; menu_offset=160; show_cursor=TRUE;
    memset(recorded_palettes,0,sizeof(recorded_palettes));
    require(CreateDirectoryW(folder,NULL) || GetLastError()==ERROR_ALREADY_EXISTS,"create square recording directory");
    swprintf_s(timeline,80,L"%s\\timing.txt",folder);
    require(_wfopen_s(&frame_timing,timeline,L"w")==0,"create square recording timeline");
}

static void end_recording(void) {
    fclose(frame_timing); frame_timing=NULL;
    export_gif_colors();
    show_cursor=FALSE; caption=NULL;
}

static INPUT pointer_input(int x,int y) {
    INPUT input={0};
    input.type=INPUT_MOUSE;
    input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
    input.mi.dx=MulDiv(x-GetSystemMetrics(SM_XVIRTUALSCREEN),65535,GetSystemMetrics(SM_CXVIRTUALSCREEN)-1);
    input.mi.dy=MulDiv(y-GetSystemMetrics(SM_YVIRTUALSCREEN),65535,GetSystemMetrics(SM_CYVIRTUALSCREEN)-1);
    return input;
}

static void move_pointer(int x,int y) {
    INPUT input=pointer_input(x,y);
    require(SendInput(1,&input,sizeof(INPUT))==1,"move the actual pointer");
}

static void click_pointer(void) {
    INPUT clicks[2]={{0},{0}};
    clicks[0].type=clicks[1].type=INPUT_MOUSE;
    clicks[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN; clicks[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    require(SendInput(2,clicks,sizeof(INPUT))==2,"click the actual application");
}

static void position_demo_archive(void) {
    RECT bounds;
    HWND window=thread_window(L"Jotilla.Archive",NULL);
    require(window!=NULL,"locate this process's archive window");
    GetWindowRect(backdrop,&bounds);
    SetWindowPos(window,HWND_TOPMOST,bounds.left+50,bounds.top+70,900,750,0);
    SetForegroundWindow(window); UpdateWindow(window);
}

typedef struct EdgeDrag { POINT from,to; } EdgeDrag;
static volatile LONG edge_drag_active;
static UINT_PTR edge_frame_timer;

static DWORD WINAPI drag_edge_input(void *argument) {
    const EdgeDrag *drag=(const EdgeDrag *)argument;
    INPUT input=pointer_input(drag->from.x,drag->from.y);
    BOOL ok=SendInput(1,&input,sizeof(input))==1;
    int i;
    Sleep(60);
    input.type=INPUT_MOUSE; input.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
    if (ok) ok=SendInput(1,&input,sizeof(input))==1;
    Sleep(80);
    for (i=1;ok && i<=12;++i) {
        input=pointer_input(drag->from.x+MulDiv(drag->to.x-drag->from.x,i,12),
                            drag->from.y+MulDiv(drag->to.y-drag->from.y,i,12));
        ok=SendInput(1,&input,sizeof(input))==1;
        Sleep(55);
    }
    memset(&input,0,sizeof(input)); input.type=INPUT_MOUSE; input.mi.dwFlags=MOUSEEVENTF_LEFTUP;
    if (SendInput(1,&input,sizeof(input))!=1) ok=FALSE;
    InterlockedExchange(&edge_drag_active,0);
    return ok ? 0 : 1;
}

static void CALLBACK capture_edge_frame(HWND window,UINT message,UINT_PTR timer,DWORD time) {
    (void)window; (void)message; (void)time;
    KillTimer(NULL,timer); edge_frame_timer=0;
    capture_frame();
    if (InterlockedCompareExchange(&edge_drag_active,0,0))
        edge_frame_timer=SetTimer(NULL,0,90,capture_edge_frame);
}

static void record_edge_resize(Note *note,BOOL right_edge,int distance) {
    RECT before,after;
    EdgeDrag drag;
    HANDLE thread;
    DWORD result;
    require(save_note(note),"save before the border resize demonstration");
    SetForegroundWindow(note->window); GetWindowRect(note->window,&before);
    drag.from.x=right_edge ? before.right-1 : (before.left+before.right)/2;
    drag.from.y=right_edge ? (before.top+before.bottom)/2 : before.bottom-1;
    drag.to=drag.from;
    if (right_edge) drag.to.x+=distance;
    else drag.to.y+=distance;
    move_pointer(drag.from.x,drag.from.y); frame_delay=65; capture_frame();
    frame_delay=14;
    InterlockedExchange(&edge_drag_active,1);
    edge_frame_timer=SetTimer(NULL,0,90,capture_edge_frame);
    thread=CreateThread(NULL,0,drag_edge_input,&drag,0,NULL);
    require(edge_frame_timer && thread,"record native edge resizing while the message loop runs");
    pump(1150);
    require(WaitForSingleObject(thread,1000)==WAIT_OBJECT_0 && GetExitCodeThread(thread,&result) && result==0,
            "complete the real border drag");
    CloseHandle(thread);
    if (edge_frame_timer) { KillTimer(NULL,edge_frame_timer); edge_frame_timer=0; }
    GetWindowRect(note->window,&after);
    require((right_edge && after.right-before.right==distance) || (!right_edge && after.bottom-before.bottom==distance),
            "dragging the border changes the real post-it dimensions");
    frame_delay=80; capture_frame();
}

static void select_demo_date(HWND tree,unsigned date,BOOL expand) {
    HTREEITEM item=find_date(tree,TreeView_GetRoot(tree),date);
    RECT rect;
    POINT point;
    require(item!=NULL,"find a dated archive group");
    TreeView_EnsureVisible(tree,item);
    require(TreeView_GetItemRect(tree,item,&rect,TRUE),"locate the date label");
    point.x=(rect.left+rect.right)/2; point.y=(rect.top+rect.bottom)/2;
    ClientToScreen(tree,&point); move_pointer(point.x,point.y); frame_delay=45; capture_frame();
    TreeView_SelectItem(tree,item); pump(120);
    require(TreeView_GetSelection(tree)==item,"the native tree selects the requested date");
    if (expand && !(TreeView_GetItemState(tree,item,TVIS_EXPANDED)&TVIS_EXPANDED)) {
        TVHITTESTINFO hit={0};
        int offset,first=0,last=0,indent=TreeView_GetIndent(tree);
        require(TreeView_GetItemRect(tree,item,&rect,TRUE),"refresh the selected row after native scrolling");
        hit.pt.y=(rect.top+rect.bottom)/2;
        for (offset=1;offset<=indent;++offset) {
            hit.pt.x=rect.left-offset;
            if (TreeView_HitTest(tree,&hit)==item && (hit.flags&TVHT_ONITEMBUTTON)) {
                if(!first) first=offset;
                last=offset;
            }
        }
        require(first!=0,"locate the native tree expansion button");
        point.x=rect.left-(first+last)/2; point.y=hit.pt.y;
        ClientToScreen(tree,&point); move_pointer(point.x,point.y); pump(50); TreeView_Expand(tree,item,TVE_EXPAND); pump(120);
        require(TreeView_GetItemState(tree,item,TVIS_EXPANDED)&TVIS_EXPANDED,"expand the year or month with its native button");
    }
    frame_delay=90; capture_frame();
}

static void CALLBACK animate_menu(HWND window,UINT message,UINT_PTR timer,DWORD time) {
    RECT rect;
    INPUT click[3]={{0},{0},{0}};
    (void)window; (void)message; (void)time;
    KillTimer(NULL,timer);
    capture_frame();
    if (menu_submenu<0) {
        require(GetMenuItemRect(NULL,recording_menu,(UINT)menu_choice,&rect),"locate the browser menu command");
        move_pointer((rect.left+rect.right)/2,(rect.top+rect.bottom)/2);
        if (!menu_phase++) { require(SetTimer(NULL,0,250,animate_menu)!=0,"highlight the archive menu option"); return; }
        click_pointer(); return;
    }
    if (menu_phase==0) {
        require(GetMenuItemRect(NULL,recording_menu,(UINT)menu_submenu,&rect),"locate real menu item");
        move_pointer((rect.left+rect.right)/2,(rect.top+rect.bottom)/2);
        menu_phase=1; require(SetTimer(NULL,0,600,animate_menu)!=0,"wait for native submenu");
    } else if (menu_phase==1) {
        if (!GetMenuItemRect(NULL,GetSubMenu(recording_menu,menu_submenu),(UINT)menu_choice,&rect)) {
            require(++menu_retries<=10 && GetMenuItemRect(NULL,recording_menu,(UINT)menu_submenu,&rect),
                    "wait for the native submenu to open");
            move_pointer((rect.left+rect.right)/2,(rect.top+rect.bottom)/2);
            require(SetTimer(NULL,0,200,animate_menu)!=0,"wait for delayed native submenu layout"); return;
        }
        move_pointer((rect.left+rect.right)/2,(rect.top+rect.bottom)/2);
        menu_phase=2; require(SetTimer(NULL,0,180,animate_menu)!=0,"highlight native menu choice");
    } else {
        require(GetMenuItemRect(NULL,GetSubMenu(recording_menu,menu_submenu),(UINT)menu_choice,&rect),"locate the selected item before clicking");
        click[0]=pointer_input((rect.left+rect.right)/2,(rect.top+rect.bottom)/2);
        click[1].type=click[2].type=INPUT_MOUSE;
        click[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN; click[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;
        require(SendInput(3,click,sizeof(INPUT))==3,"click native menu option");
    }
}

static void record_menu(Note *note,int submenu,int choice,UINT expected) {
    RECT rect;
    UINT selected;
    INPUT activate[3]={{0},{0},{0}};
    GetWindowRect(note->window,&rect);
    activate[0]=pointer_input(rect.left+bar_height(note)+scaled(note,12),rect.top+bar_height(note)/2);
    activate[1].type=activate[2].type=INPUT_MOUSE;
    activate[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN; activate[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    require(SendInput(3,activate,sizeof(INPUT))==3,"activate the actual note before opening its menu");
    pump(100);
    require(GetForegroundWindow()==note->window,"note owns foreground input for menu recording");
    move_pointer(rect.left+menu_offset,rect.top+bar_height(note)/2);
    capture_frame();
    recording_menu=build_note_menu(note); require(recording_menu!=NULL,"build actual application menu");
    menu_submenu=submenu; menu_choice=choice; menu_phase=menu_retries=0;
    require(SetTimer(NULL,0,120,animate_menu)!=0,"schedule menu interaction");
    SetForegroundWindow(note->window);
    selected=(UINT)TrackPopupMenu(recording_menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,rect.left+menu_offset,rect.top+bar_height(note)/2,0,note->window,NULL);
    DestroyMenu(recording_menu);
    if (selected!=expected) fprintf(stderr,"Menu selected %u, expected %u.\n",selected,expected);
    require(selected==expected,"native menu click selects the intended command");
    note_command(note,selected);
    if (selected==CMD_BROWSE) position_demo_archive();
    pump(220); capture_frame();
}

static void type_live(Note *note,const wchar_t *text) {
    frame_delay=6;
    SetForegroundWindow(note->window); SetFocus(note->edit);
    while (*text) {
        if (*text==L'\r') press_enter(note,FALSE);
        else SendMessageW(note->edit,WM_CHAR,*text,1);
        ++text; capture_frame();
    }
}

static Note *create_live_note(int x,int y) {
    Note *note=new_note();
    RECT rect,edit;
    require(note!=NULL,"create a live demonstration note");
    GetWindowRect(backdrop,&rect);
    SetWindowPos(note->window,HWND_TOPMOST,rect.left+x,rect.top+y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
    ShowWindow(note->window,SW_SHOWNORMAL);
    SetForegroundWindow(note->window); SetFocus(note->edit);
    GetWindowRect(note->edit,&edit);
    /* A real click dismisses shell panels that SetForegroundWindow alone
       may leave over the recording, even while WM_CHAR edits our note. */
    move_pointer(edit.left+12,edit.top+12); click_pointer(); pump(100);
    require(GetForegroundWindow()==note->window && GetFocus()==note->edit,"activate the note before recording any typing");
    capture_frame();
    return note;
}

static void record_postits(void) {
    Note *note;
    POINTL checkbox;
    POINT point;
    CHARRANGE paragraph;
    FINDTEXTEXW todo={{0,-1},L"\x2610 ",{0,0}};
    RECT bounds;
    begin_recording(L"build\\notes-gif");
    set_background(L"build\\notes-gif-background.bmp");
    current_palette=0; next_color=0;
    set_caption(L"Create a note: Ctrl + Alt + +");
    note=create_live_note(230,200);
    set_caption(L"#Title + Shift + Enter");
    type_live(note,L"#Welcome to the machine");
    press_enter(note,TRUE); frame_delay=90; capture_frame();
    require(note->title && !wcscmp(note->title,L"Welcome to the machine"),"live title entry works");
    set_caption(L"Write freely; notes save automatically");
    type_live(note,L"Welcome my son\rWelcome to the machine\rWhere have you been?");
    require(!wcscmp(note->accepted_text,WELCOME_BODY),"post-it demo starts with the original square-demo text");
    set_caption(L"Right-click the bar: choose a size");
    frame_delay=65; record_menu(note,5,4,CMD_SIZE+4);
    set_caption(L"Drag the right or bottom edge to resize");
    record_edge_resize(note,TRUE,110);
    record_edge_resize(note,FALSE,70);
    set_caption(L"Ctrl + plus / minus resizes the post-it");
    sizing_shortcut(note,VK_ADD,L'+'); frame_delay=110; capture_frame();
    sizing_shortcut(note,VK_SUBTRACT,L'-'); capture_frame();
    set_caption(L"Select text: Ctrl + plus changes its size");
    SendMessageW(note->edit,EM_SETSEL,0,7); sizing_shortcut(note,VK_ADD,L'+'); capture_frame();
    set_caption(L"Ctrl + Z undoes text and formatting");
    shortcut(note,'Z',FALSE); capture_frame();
    SendMessageW(note->edit,EM_SETSEL,(WPARAM)-1,-1);
    set_caption(L"Ctrl + B / I / U: format selected text");
    paragraph_at(note->edit,0,&paragraph);
    SendMessageW(note->edit,EM_EXSETSEL,0,(LPARAM)&paragraph); shortcut(note,'B',FALSE);
    frame_delay=90; capture_frame(); shortcut(note,'I',FALSE);
    SendMessageW(note->edit,EM_SETSEL,(WPARAM)-1,-1);
    frame_delay=90; capture_frame();
    paragraph_at(note->edit,15,&paragraph);
    SendMessageW(note->edit,EM_EXSETSEL,0,(LPARAM)&paragraph); shortcut(note,'U',FALSE);
    SendMessageW(note->edit,EM_SETSEL,(WPARAM)-1,-1); capture_frame();
    set_caption(L"* + Shift + Enter starts a checklist");
    press_enter(note,FALSE); type_live(note,L"*");
    press_enter(note,TRUE); frame_delay=90; capture_frame();
    type_live(note,L"Draft the outline");
    set_caption(L"Enter adds the next checkbox");
    press_enter(note,FALSE); type_live(note,L"Finish one thing");
    require(SendMessageW(note->edit,EM_FINDTEXTEXW,FR_DOWN,(LPARAM)&todo)>=0,"find the first checklist item");
    paragraph_at(note->edit,todo.chrgText.cpMin,&paragraph);
    require(todo_marker(note->edit,&paragraph)==L'\x2610',"locate the new unchecked item");
    set_caption(L"Click a checkbox to complete its task");
    SendMessageW(note->edit,EM_POSFROMCHAR,(WPARAM)&checkbox,paragraph.cpMin);
    point.x=checkbox.x+2; point.y=checkbox.y+default_line_height(note)/2;
    ClientToScreen(note->edit,&point); move_pointer(point.x,point.y); frame_delay=90; capture_frame();
    click_pointer(); pump(150);
    require(todo_marker(note->edit,&paragraph)==L'\x2611',"the actual click completes the demonstration task");
    frame_delay=140; capture_frame();
    set_caption(L"Collapse a note; expand it when needed");
    GetWindowRect(note->window,&bounds);
    point.x=bounds.right-bar_height(note)*3/2; point.y=bounds.top+bar_height(note)/2;
    move_pointer(point.x,point.y); click_pointer(); pump(100); frame_delay=90; capture_frame();
    require(note->collapsed,"the real collapse button folds the note");
    pump(GetDoubleClickTime()+20);
    click_pointer(); pump(100); frame_delay=160; capture_frame();
    require(!note->collapsed,"the real expand button restores the note");
    end_recording();
    puts("Recorded square post-it demo: typing, title, edge resizing, formatting, checklist and collapse.");
}

static void send_key(WORD key,BOOL down) {
    INPUT input={0};
    input.type=INPUT_KEYBOARD; input.ki.wVk=key;
    input.ki.dwFlags=down ? 0 : KEYEVENTF_KEYUP;
    require(SendInput(1,&input,sizeof(input))==1,"deliver demonstration key");
}

static HWND open_demo_palette(Note *note) {
    RECT edit;
    POINT corner;
    HWND popup;
    INPUT click[2]={{0},{0}};
    GetWindowRect(note->edit,&edit);
    corner.x=edit.right-scaled(note,10); corner.y=edit.bottom-scaled(note,10);
    SetForegroundWindow(note->window); SetFocus(note->edit);
    move_pointer(corner.x,corner.y); pump(80);
    require(WindowFromPoint(corner)==note->edit,"open the wheel inside the note's lower-right corner");
    click[0].type=click[1].type=INPUT_MOUSE;
    click[0].mi.dwFlags=MOUSEEVENTF_RIGHTDOWN; click[1].mi.dwFlags=MOUSEEVENTF_RIGHTUP;
    require(SendInput(1,click,sizeof(INPUT))==1,"press the right mouse button in the note editor");
    click_position=corner; show_click=TRUE; frame_delay=80; capture_frame(); show_click=FALSE;
    require(SendInput(1,click+1,sizeof(INPUT))==1,"release the right mouse button to open the wheel");
    pump(180); popup=thread_window(PALETTE_PICKER_CLASS,NULL);
    require(popup!=NULL,"the corner right-click opens the native color wheel");
    frame_delay=100; capture_frame();
    return popup;
}

static void choose_demo_color(HWND popup,int palette,int color,int steps) {
    PaletteArt *art=palette_art_create((int)GetDpiForWindow(popup));
    PaletteLayout layout;
    RECT arrow;
    POINT point;
    wchar_t label[80];
    double angle=(-90+(color+.5)*360/PALETTE_SIZES[palette])*3.141592653589793/180;
    double radius;
    int i;
    require(art!=NULL,"read the native wheel geometry at the recording DPI");
    layout=*palette_art_layout(art); palette_art_destroy(art);
    arrow=steps<0?layout.previous:layout.next;
    point.x=(arrow.left+arrow.right)/2; point.y=(arrow.top+arrow.bottom)/2;
    ClientToScreen(popup,&point); move_pointer(point.x,point.y);
    for(i=0;i<abs(steps);++i) { click_pointer(); pump(160); frame_delay=40; capture_frame(); }
    GetWindowTextW(popup,label,80);
    require(!wcscmp(label,PALETTE_NAMES[palette]),"record navigation to the intended palette");
    radius=(layout.wheel.right-layout.wheel.left)*.5;
    point.x=(LONG)(layout.wheel.left+radius+cos(angle)*radius*.6);
    point.y=(LONG)(layout.wheel.top+radius+sin(angle)*radius*.6);
    ClientToScreen(popup,&point); move_pointer(point.x,point.y);
    pump(180); frame_delay=100; capture_frame();
    click_pointer(); pump(180);
    require(!IsWindow(popup),"choosing a visible wedge applies the color and closes the wheel");
}

static void record_favorite_palette(Note *note) {
    HWND popup=open_demo_palette(note);
    PaletteArt *art=palette_art_create((int)GetDpiForWindow(popup));
    PaletteLayout layout;
    POINT point;
    RECT bounds;
    Note *next;
    require(art!=NULL,"read favorite button geometry");
    layout=*palette_art_layout(art); palette_art_destroy(art);
    set_caption(L"Star a palette: the current note changes immediately");
    point.x=(layout.next.left+layout.next.right)/2; point.y=(layout.next.top+layout.next.bottom)/2;
    ClientToScreen(popup,&point); move_pointer(point.x,point.y); click_pointer(); pump(160);
    point.x=(layout.favorite.left+layout.favorite.right)/2; point.y=(layout.favorite.top+layout.favorite.bottom)/2;
    ClientToScreen(popup,&point); move_pointer(point.x,point.y); frame_delay=100; capture_frame();
    click_pointer(); pump(180);
    require(IsWindow(popup) && favorite_palette==11 && current_palette==11 && note->palette==11 && note->color==0,
            "the recorded star click applies Beach immediately to the current note");
    frame_delay=160; capture_frame();
    send_key(VK_ESCAPE,TRUE); send_key(VK_ESCAPE,FALSE); pump(100);
    require(!IsWindow(popup),"dismiss the favorite picker");
    next=new_note(); require(next && next->palette==11,"the next recorded note uses the favorite without a restart");
    GetWindowRect(backdrop,&bounds);
    SetWindowPos(next->window,HWND_TOPMOST,bounds.left+560,bounds.top+240,0,0,SWP_NOSIZE);
    ShowWindow(next->window,SW_SHOWNORMAL); SetForegroundWindow(next->window); pump(120);
    set_caption(L"The next note uses the favorite too; no restart needed");
    frame_delay=210; capture_frame(); destroy_note(next);
}

static void record_note_colors(Note *note,HWND browser) {
    RECT bounds,edit;
    HWND popup;
    POINT from,to;
    POINTL first={0},last={0};
    CHARRANGE selection;
    CHARFORMAT2W format={0};
    INPUT drag={0};
    LONG line,start,end;
    int i;
    ShowWindow(browser,SW_HIDE);
    GetWindowRect(backdrop,&bounds);
    SetWindowPos(note->window,HWND_TOPMOST,bounds.left+160,bounds.top+170,0,0,SWP_NOSIZE);
    pump(250); /* Let the archive's native hide animation finish before recording. */
    SendMessageW(note->edit,EM_SETSEL,(WPARAM)-1,-1);
    set_caption(L"Right-click near the lower-right corner: note color");
    popup=open_demo_palette(note);
    set_caption(L"Browse palettes, then click a color for the note");
    choose_demo_color(popup,3,3,1);
    require(note->palette==3 && note->color==3,"the demonstration applies the green note background");
    frame_delay=150; capture_frame();

    set_caption(L"Select a line to change only its text color");
    line=(LONG)SendMessageW(note->edit,EM_GETLINECOUNT,0,0)-1;
    start=(LONG)SendMessageW(note->edit,EM_LINEINDEX,line,0);
    end=start+(LONG)SendMessageW(note->edit,EM_LINELENGTH,start,0);
    require(start>=0 && end>start,"locate the live-update line for foreground formatting");
    SendMessageW(note->edit,EM_POSFROMCHAR,(WPARAM)&first,start);
    SendMessageW(note->edit,EM_POSFROMCHAR,(WPARAM)&last,end);
    from.x=first.x; from.y=first.y+default_line_height(note)/2;
    to.x=last.x; to.y=last.y+default_line_height(note)/2;
    ClientToScreen(note->edit,&from); ClientToScreen(note->edit,&to);
    move_pointer(from.x,from.y); pump(60);
    drag.type=INPUT_MOUSE; drag.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
    require(SendInput(1,&drag,sizeof(drag))==1,"start selecting the actual text");
    pump(50);
    for(i=1;i<=4;++i) {
        move_pointer(from.x+MulDiv(to.x-from.x,i,4),to.y);
        frame_delay=12; capture_frame();
    }
    drag.mi.dwFlags=MOUSEEVENTF_LEFTUP;
    require(SendInput(1,&drag,sizeof(drag))==1,"finish the real text-selection drag");
    pump(100); SendMessageW(note->edit,EM_EXGETSEL,0,(LPARAM)&selection);
    if(selection.cpMin!=start || selection.cpMax<end || selection.cpMax>end+1)
        fprintf(stderr,"Selection %ld..%ld, expected %ld..%ld; drag %ld,%ld to %ld,%ld.\n",
                selection.cpMin,selection.cpMax,start,end,from.x,from.y,to.x,to.y);
    /* Rich Edit may include the final paragraph mark when dragging to its end. */
    require(selection.cpMin==start && selection.cpMax>=end && selection.cpMax<=end+1,
            "the recorded drag selects the last line, including its optional paragraph mark");
    frame_delay=100; capture_frame();
    set_caption(L"Right-click in the same corner: selected text");
    popup=open_demo_palette(note);
    require(note->palette_selection.cpMin==selection.cpMin && note->palette_selection.cpMax==selection.cpMax,
            "the lower-right right-click keeps the text selection");
    set_caption(L"Choose a text color; the note keeps its background");
    choose_demo_color(popup,POPULAR_PALETTES[0],1,-1);
    format.cbSize=sizeof(format);
    SendMessageW(note->edit,EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&format);
    require(note->custom_ink && note->palette==3 && note->color==3 && format.crTextColor==PALETTES[POPULAR_PALETTES[0]][1],
            "only the selected line changes to the chosen foreground");
    GetWindowRect(note->edit,&edit);
    move_pointer(edit.right-scaled(note,10),edit.bottom-scaled(note,10)); click_pointer(); pump(150);
    set_caption(L"A little color, exactly where you want it");
    frame_delay=210; capture_frame();
    record_favorite_palette(note);
    ShowWindow(browser,SW_SHOWNOACTIVATE);
}

static int delete_phase;
static UINT_PTR delete_timer;
static void CALLBACK animate_delete(HWND window,UINT message,UINT_PTR timer,DWORD time) {
    HWND dialog,menu;
    RECT button;
    (void)window; (void)message; (void)time;
    KillTimer(NULL,timer); delete_timer=0;
    if (delete_phase==0) {
        menu=thread_window(L"#32768",NULL);
        if (menu) {
            GetWindowRect(menu,&button);
            move_pointer((button.left+button.right)/2,(button.top+button.bottom)/2);
            frame_delay=100; capture_frame(); ++delete_phase;
        }
    } else if (delete_phase==1) {
        ++delete_phase; click_pointer();
    } else if ((dialog=thread_window(L"#32770",L"Jotilla"))!=NULL) {
        RECT bounds,rect;
        require(GetWindow(dialog,GW_OWNER)==thread_window(L"Jotilla.Archive",NULL),"only delete private demo notes");
        GetWindowRect(backdrop,&bounds); GetWindowRect(dialog,&rect);
        SetWindowPos(dialog,HWND_TOPMOST,bounds.left+(1000-(rect.right-rect.left))/2,
                     bounds.top+(900-(rect.bottom-rect.top))/2,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
        GetWindowRect(GetDlgItem(dialog,IDYES),&button);
        move_pointer((button.left+button.right)/2,(button.top+button.bottom)/2);
        frame_delay=100; capture_frame(); click_pointer(); ++delete_phase; return;
    }
    delete_timer=SetTimer(NULL,0,150,animate_delete);
    require(delete_timer!=0,"continue the native delete-menu demonstration");
}

static BOOL archive_choices_recorded;
static void CALLBACK record_archive_choices(HWND window, UINT message, UINT_PTR timer, DWORD tick) {
    HWND dialog = thread_window(L"#32770", L"Archive");
    RECT bounds, rect;
    (void)window; (void)message; (void)tick;
    if (!dialog) return;
    KillTimer(NULL, timer);
    require(GetWindow(dialog, GW_OWNER) == thread_window(L"Jotilla.Archive", NULL),
            "record only the private archive chooser");
    GetWindowRect(backdrop, &bounds); GetWindowRect(dialog, &rect);
    SetWindowPos(dialog, HWND_TOPMOST, bounds.left + (1000 - (rect.right - rect.left)) / 2,
                 bounds.top + (900 - (rect.bottom - rect.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    frame_delay = 260; capture_frame(); archive_choices_recorded = TRUE;
    SendMessageW(dialog, TDM_CLICK_BUTTON, IDCANCEL, 0);
}

static void record_day_hover(HWND calendar,unsigned day) {
    set_caption(L"Move the pointer across the days"); frame_delay=45;
    for(unsigned date=day-3;date<day;++date) {
        RECT cell; POINT point;
        calendar_day_rect(calendar,date,&cell);
        point.x=(cell.left+cell.right)/2; point.y=(cell.top+cell.bottom)/2;
        ClientToScreen(calendar,&point); move_pointer(point.x,point.y); capture_frame();
    }
}

static void record_calendar(Note *note, HWND browser) {
    RECT bounds,cell; POINT point; HWND picker,calendar,list;
    Note *other;
    const unsigned day=20281114;
    GetWindowRect(backdrop,&bounds);
    SetWindowPos(note->window,HWND_TOPMOST,bounds.left+230,bounds.top+160,0,0,SWP_NOSIZE);
    SetForegroundWindow(note->window);
    set_caption(L"Calendar icon: assign one day to a note");
    GetWindowRect(note->window,&cell); point.x=cell.left+bar_height(note)/2; point.y=cell.top+bar_height(note)/2;
    move_pointer(point.x,point.y); frame_delay=100; capture_frame(); click_pointer(); pump(120);
    picker=thread_window(CALENDAR_CLASS,L"Calendar"); require(picker!=NULL,"the real note icon opens the date picker");
    capture_frame();
    set_caption(L"Type the year directly, then press Enter");
    GetWindowRect(GetDlgItem(picker,3),&cell); move_pointer((cell.left+cell.right)/2,(cell.top+cell.bottom)/2);
    SetFocus(GetDlgItem(picker,3)); SendDlgItemMessageW(picker,3,EM_SETSEL,0,-1); frame_delay=120; capture_frame();
    SetWindowTextW(GetDlgItem(picker,3),L"2028"); SendMessageW(GetDlgItem(picker,3),WM_KEYDOWN,VK_RETURN,0);
    /* Keep November visible independently of the recording's current month. */
    {
        int month=calendar_today()/100%100;
        while(month<11) { SendMessageW(picker,WM_COMMAND,8,0); ++month; }
        while(month>11) { SendMessageW(picker,WM_COMMAND,7,0); --month; }
    }
    require(calendar_day_rect(picker,day,&cell),"the picker shows November in the chosen year");
    record_day_hover(picker,day);
    frame_delay=120;
    set_caption(L"Choose a day; the same post-it stays in the archive");
    point.x=(cell.left+cell.right)/2; point.y=(cell.top+cell.bottom)/2; ClientToScreen(picker,&point);
    move_pointer(point.x,point.y); capture_frame(); capture(L"build\\calendar-picker-review.bmp");
    click_pointer(); pump(150); require(note->calendar_date==day,"the visible day assigns the demonstrated note"); capture_frame();
    other=open_note(L"note-2025-06-03_07-00-00-000-001",0,TRUE);
    require(other && set_note_date(other,day),"prepare another post-it on the same day");
    SendMessageW(other->window,WM_CLOSE,0,0); SendMessageW(note->window,WM_CLOSE,0,0);
    SetWindowPos(browser,HWND_TOPMOST,bounds.left+30,bounds.top+25,940,850,0); SetForegroundWindow(browser);
    set_caption(L"The calendar button opens a year of notes");
    GetWindowRect(GetDlgItem(browser,10),&cell); move_pointer((cell.left+cell.right)/2,(cell.top+cell.bottom)/2);
    click_pointer(); pump(150); calendar=GetDlgItem(browser,11); calendar_set_year(calendar,2028);
    SendMessageW(calendar,WM_VSCROLL,SB_BOTTOM,0);
    /* Focus navigation also brings the selected month into the viewport. */
    SetFocus(calendar);
    frame_delay=150; capture_frame();
    GetWindowRect(GetDlgItem(calendar,3),&cell); move_pointer((cell.left+cell.right)/2,(cell.top+cell.bottom)/2);
    set_caption(L"Enter any year to browse its calendar");
    SetWindowTextW(GetDlgItem(calendar,3),L"2035"); SendMessageW(GetDlgItem(calendar,3),WM_KEYDOWN,VK_RETURN,0); capture_frame();
    SetWindowTextW(GetDlgItem(calendar,3),L"2028"); SendMessageW(GetDlgItem(calendar,3),WM_KEYDOWN,VK_RETURN,0); pump(100);
    SendMessageW(calendar,WM_VSCROLL,SB_BOTTOM,0);
    require(calendar_day_rect(calendar,day,&cell),"locate the assigned day in the annual view");
    record_day_hover(calendar,day);
    frame_delay=150;
    set_caption(L"Circled days contain notes; a dot means several");
    point.x=(cell.left+cell.right)/2; point.y=(cell.top+cell.bottom)/2; ClientToScreen(calendar,&point);
    move_pointer(point.x,point.y); capture_frame(); capture(L"build\\calendar-year-review.bmp");
    click_pointer(); pump(150); list=thread_window(CALENDAR_LIST_CLASS,NULL);
    require(list!=NULL,"the real day click opens its compact title list");
    set_caption(L"Choose a title when several notes share a day");
    frame_delay=180; capture_frame(); capture(L"build\\calendar-list-review.bmp");
    set_caption(L"Up / Down selects a title; Enter opens it");
    SendMessageW(GetDlgItem(list,1),WM_KEYDOWN,VK_DOWN,0); capture_frame();
    SendMessageW(GetDlgItem(list,1),WM_KEYDOWN,VK_RETURN,0); pump(120); capture_frame();
    other=find_note(L"note-2025-06-03_07-00-00-000-001");
    if(other) SendMessageW(other->window,WM_CLOSE,0,0);
    other=find_note(WELCOME_ARCHIVE_ID);
    if(other) SendMessageW(other->window,WM_CLOSE,0,0);
    SendMessageW(browser,WM_COMMAND,10,0); position_demo_archive();
}

static void record_archive_browser(void) {
    HWND window,tree,list;
    RECT tile={0},bounds;
    POINT point;
    Note *note,*reopened;
    int i;
    INPUT right_click[2]={{0},{0}};
    wchar_t preview[128],path[PATH_CAP];
    begin_recording(L"build\\browser-gif");
    set_background(L"build\\browser-background.bmp");
    set_caption(L"Ctrl + E opens the note explorer");
    note=demo_note(0,0,4,230,200,L"Welcome to the machine",WELCOME_BODY,100);
    populate_demo_archive();
    frame_delay=75;
    shortcut(note,'E',FALSE); position_demo_archive(); capture_frame();
    SendMessageW(note->window,WM_CLOSE,0,0);
    window=thread_window(L"Jotilla.Archive",NULL); tree=GetDlgItem(window,1); list=GetDlgItem(window,2);
    require(tree && list,"record the actual date tree and virtual mosaic");
    set_caption(L"All notes in a mosaic, grouped by date");
    frame_delay=150; capture_frame();
    set_caption(L"Minimize to the tray beside the clock");
    SendMessageW(window, WM_SYSCOMMAND, SC_MINIMIZE, 0); pump(150);
    require(!IsWindowVisible(window), "the real archive hides when minimized");
    frame_delay=120; capture_frame();
    set_caption(L"Double-click Jotilla's tray icon to return");
    SendMessageW(controller, TRAY_MESSAGE, 1, WM_LBUTTONDBLCLK);
    SendMessageW(controller, TRAY_MESSAGE, 1, WM_LBUTTONUP); pump(150);
    require(IsWindowVisible(window), "tray activation restores the existing archive");
    frame_delay=120; capture_frame();
    set_caption(L"Archive: move your notes or choose another folder");
    GetWindowRect(GetDlgItem(window, 9), &tile);
    move_pointer((tile.left + tile.right) / 2, (tile.top + tile.bottom) / 2);
    frame_delay=100; capture_frame();
    require(SetTimer(NULL, 0, 120, record_archive_choices) != 0, "record the two archive location choices");
    SendMessageW(GetDlgItem(window, 9), BM_CLICK, 0, 0);
    require(archive_choices_recorded, "the native archive chooser is included in the walkthrough");
    set_caption(L"Tags show how many notes share a subject");
    SendMessageW(GetDlgItem(window, 7), BM_CLICK, 0, 0); pump(150);
    frame_delay=150; capture_frame();
    set_caption(L"Search text and tags together");
    SetWindowTextW(GetDlgItem(window, 3), L"#work"); wait_search_ui(window);
    require(ListView_GetItemCount(list)==5, "the demonstration search shows five work notes");
    frame_delay=160; capture_frame();
    SetWindowTextW(GetDlgItem(window, 3), L""); wait_search_ui(window);
    SendMessageW(GetDlgItem(window, 8), BM_CLICK, 0, 0); pump(150);
    set_caption(L"Choose a year, a month, then a day");
    select_demo_date(tree,20260000,TRUE);
    select_demo_date(tree,20260400,TRUE);
    select_demo_date(tree,20260408,FALSE);
    select_demo_date(tree,20250000,TRUE);
    select_demo_date(tree,20250600,TRUE);
    select_demo_date(tree,20250618,FALSE);
    set_caption(L"Click a preview to reopen its post-it");
    /* Leave a band of the painting around both windows during the live edit. */
    GetWindowRect(backdrop,&bounds);
    TreeView_Expand(tree,find_date(tree,TreeView_GetRoot(tree),20260000),TVE_COLLAPSE);
    SetWindowPos(window,HWND_TOPMOST,bounds.left+50,bounds.top+50,900,400,0);
    TreeView_EnsureVisible(tree,TreeView_GetSelection(tree));
    require(ListView_GetItemCount(list)==1 && ListView_GetItemRect(list,0,&tile,LVIR_ICON),"locate the archived welcome note");
    point.x=tile.left+35; point.y=tile.top+45;
    ClientToScreen(list,&point); move_pointer(point.x,point.y); frame_delay=65; capture_frame();
    click_pointer(); pump(150);
    reopened=find_note(WELCOME_ARCHIVE_ID);
    require(reopened && !wcscmp(reopened->accepted_text,WELCOME_BODY),"clicking the archive restores the exact square-demo text");
    frame_delay=140; capture_frame();
    set_caption(L"Edit the note: its preview updates automatically");
    SendMessageW(reopened->edit,EM_SETSEL,(WPARAM)-1,-1);
    type_live(reopened,L"\rA live update."); pump(650);
    require(!reopened->dirty,"the live edit autosaves before the browser preview capture");
    frame_delay=180; capture_frame();
    record_note_colors(reopened,window);
    record_calendar(reopened,window);
    position_demo_archive();
    set_caption(L"Ctrl-click to select more than one note");
    SetForegroundWindow(window); select_demo_date(tree,20250600,TRUE);
    require(ListView_GetItemCount(list)==2,"June contains exactly two private demo notes");
    send_key(VK_CONTROL,TRUE);
    for (i=0;i<2;++i) {
        ListView_GetItemRect(list,i,&tile,LVIR_ICON);
        point.x=tile.left+35; point.y=tile.top+45; ClientToScreen(list,&point);
        move_pointer(point.x,point.y); click_pointer(); pump(100); frame_delay=90; capture_frame();
    }
    send_key(VK_CONTROL,FALSE);
    require(ListView_GetSelectedCount(list)==2,"real Ctrl-click selects both archive previews");
    set_caption(L"Right-click to delete the selected notes");
    delete_phase=0; delete_timer=SetTimer(NULL,0,150,animate_delete);
    require(delete_timer!=0,"record deletion through the native context menu");
    right_click[0].type=right_click[1].type=INPUT_MOUSE;
    right_click[0].mi.dwFlags=MOUSEEVENTF_RIGHTDOWN; right_click[1].mi.dwFlags=MOUSEEVENTF_RIGHTUP;
    require(SendInput(2,right_click,sizeof(INPUT))==2,"open the real archive context menu");
    pump(1800);
    if (delete_timer) { KillTimer(NULL,delete_timer); delete_timer=0; }
    if (delete_phase!=3) fprintf(stderr,"Delete recording stopped at phase %d.\n",delete_phase);
    require(delete_phase==3 && !find_date(tree,TreeView_GetRoot(tree),20250600),"deleting the demo selection also removes its empty month");
    swprintf_s(path,PATH_CAP,L"%s\\%s.txt",directory,WELCOME_ARCHIVE_ID);
    require(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES,"the demo deletion removes the selected archive file");
    ListView_GetItemText(list,0,0,preview,128);
    require(!wcscmp(preview,L"Winter break"),"other archived dates remain intact");
    frame_delay=160; capture_frame();
    end_recording();
    puts("Recorded square browser demo: archive navigation, live updates, note/text palettes from the lower-right corner and multi-delete.");
}

int main(int argc,char **argv) {
    wchar_t temporary[PATH_CAP],root[PATH_CAP],resolved[PATH_CAP];
    WNDCLASSW cls={0};
    Note *collapsed,*formatted,*colored;
    DWORD root_length;
    POINT previous_cursor;
    BOOL overview_only=argc==2 && !strcmp(argv[1],"--overview-only");
    BOOL stills_only=argc==2 && !strcmp(argv[1],"--stills-only");
    BOOL browser_only=argc==2 && !strcmp(argv[1],"--browser-only");
    BOOL notes_only=argc==2 && !strcmp(argv[1],"--notes-only");
    BOOL gifs_only=browser_only || notes_only || (argc==2 && !strcmp(argv[1],"--gifs-only"));
    instance=GetModuleHandleW(NULL); testing=TRUE;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    require(GetTempPathW(PATH_CAP,temporary)!=0,"locate private demonstration folder");
    root_length=GetEnvironmentVariableW(L"JOTILLA_DEMO_ROOT",root,PATH_CAP);
    if (!root_length) swprintf_s(root,PATH_CAP,L"%sJotilla-demo-%lu-%llu",temporary,GetCurrentProcessId(),GetTickCount64());
    require(root_length<PATH_CAP,"demonstration path length is valid");
    root_length=GetFullPathNameW(root,PATH_CAP,resolved,NULL);
    require(root_length>0 && root_length<PATH_CAP &&
            !_wcsnicmp(resolved,temporary,wcslen(temporary)) &&
            !wcsncmp(resolved+wcslen(temporary),L"Jotilla-demo-",wcslen(L"Jotilla-demo-")),"demo data stays in a private temporary directory");
    wcscpy_s(root,PATH_CAP,resolved);
    require(make_paths(root),"create private demonstration archive");
    app_icon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(IDI_APP),IMAGE_ICON,32,32,0);
    set_background(L"build\\notes-background.bmp");
    caption_font=CreateFontW(-24,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,L"Consolas");
    require(caption_font!=NULL,"create the recording caption font");
    require(register_classes(),"register demonstration application");
    controller=CreateWindowExW(WS_EX_TOOLWINDOW,APP_CLASS,L"Jotilla documentation",WS_OVERLAPPED,
                                0,0,0,0,NULL,NULL,instance,NULL);
    require(controller!=NULL,"create the isolated archive owner");
    cls.hInstance=instance; cls.lpfnWndProc=backdrop_proc; cls.lpszClassName=L"Jotilla.DemoBackdrop";
    cls.style=CS_HREDRAW|CS_VREDRAW;
    require(RegisterClassW(&cls)!=0,"register private screenshot backdrop");
    backdrop=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,L"Jotilla demo",WS_POPUP|WS_VISIBLE,40,40,1040,640,NULL,NULL,instance,NULL);
    require(backdrop!=NULL,"show private screenshot backdrop");
    GetCursorPos(&previous_cursor);
    if (gifs_only) goto animations;
    formatted=demo_note(0,0,1,55,50,L"Today",L"Bold focus\r\nItalic idea\r\nUnderlined plan\r\n\x2611 Draft the outline\r\n\x2610 Finish one thing",100);
    format_overview_note(formatted);
    demo_note(0,1,1,545,100,L"Project",L"Review the mockup\r\nSend the first draft\r\nFinish before Friday.",100);
    collapsed=demo_note(0,3,1,70,470,L"Later",L"Ideas for another day.",100);
    toggle_note(collapsed);
    capture(L"build\\notes.bmp"); clear_scene();
    if (overview_only) goto cleanup;
    set_background(L"build\\palettes-background.bmp");
    demo_note(0,1,0,35,35,L"Pastel",L"Pick up flowers\r\nDinner at 7:30",100);
    demo_note(1,0,0,370,35,L"White",L"Read chapter 4\r\nEmail Alex",100);
    demo_note(2,2,0,705,35,L"Soft yellow",L"Train at 8:15\r\nPack charger",100);
    colored=demo_note(0,3,0,35,350,L"Faded Rose",L"Book the museum\r\nFind a good cafe",100);
    require(recolor_note(colored,4,1),"show original Faded Rose colors");
    colored=demo_note(1,1,0,370,350,L"Soft Black",L"Sunday walk\r\nBring the camera",100);
    require(recolor_note(colored,6,0),"show readable dark palette ink");
    colored=demo_note(0,0,0,705,350,L"Your colors",L"Choose a shade\r\nKeep the idea",100);
    require(recolor_note(colored,3,0),"show custom note color");
    capture(L"build\\palettes.bmp"); clear_scene();
    if (stills_only) goto cleanup;
animations:
    if (!browser_only) record_postits();
    if (!notes_only) record_archive_browser();
cleanup:
    SetCursorPos(previous_cursor.x,previous_cursor.y);
    clear_scene();
    DestroyWindow(controller);
    DestroyWindow(backdrop); if (app_icon) DestroyIcon(app_icon);
    DeleteObject(photo); DeleteObject(caption_font);
    clear_demo_archive();
    require(RemoveDirectoryW(root),"remove private demo folder");
    puts(overview_only ? "Captured the notes overview." :
         stills_only ? "Captured the notes and color examples." :
         browser_only ? "Captured the square browser walkthrough." :
         notes_only ? "Captured the square post-it walkthrough." : "Captured the two square walkthroughs.");
    return 0;
}
