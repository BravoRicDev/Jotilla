/* Actual application screenshots and native menu interactions, on a photo backdrop. */
#define main integration_main
#include "tests.c"
#undef main
static HWND backdrop;
static HBITMAP photo;
static BITMAP photo_info;
static BOOL show_cursor;
static int frame_number, menu_phase, menu_submenu, menu_choice;
static HMENU recording_menu;
static const wchar_t *frame_directory=L"build\\gif";
static int frame_delay=85, menu_offset=200;
static FILE *frame_timing;
static const wchar_t *WELCOME_BODY=L"Welcome my son\r\nWelcome to the machine\r\nWhere have you been?";
static const wchar_t *WELCOME_ARCHIVE_ID=L"note-2025-06-18_09-30-00-000-001";

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
            DrawIconEx(memory,cursor.ptScreenPos.x-rect.left-(int)icon.xHotspot,
                       cursor.ptScreenPos.y-rect.top-(int)icon.yHotspot,cursor.hCursor,0,0,0,NULL,DI_NORMAL);
            if (icon.hbmColor) DeleteObject(icon.hbmColor);
            if (icon.hbmMask) DeleteObject(icon.hbmMask);
        }
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
    current_palette=palette; next_color=color;
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
    mark_dirty(note);
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
                write_setting(fixtures[i].id,L"x",bounds.left+760) &&
                write_setting(fixtures[i].id,L"y",bounds.top+320),"configure isolated dated previews");
    }
}

static void capture_frame(void) {
    wchar_t path[80];
    swprintf_s(path,80,L"%s\\frame-%03d.bmp",frame_directory,frame_number++);
    capture(path);
    if (frame_timing) fprintf(frame_timing,"%d|%ls\n",frame_delay,path);
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
    HWND window=thread_window(L"PicoSlip.Archive",NULL);
    require(window!=NULL,"locate this process's archive window");
    GetWindowRect(backdrop,&bounds);
    SetWindowPos(window,HWND_TOPMOST,bounds.left+35,bounds.top+45,1000,680,0);
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
    click_pointer(); pump(120);
    require(TreeView_GetSelection(tree)==item,"the real click selects the requested date");
    if (expand && !(TreeView_GetItemState(tree,item,TVIS_EXPANDED)&TVIS_EXPANDED)) {
        TVHITTESTINFO hit={0};
        int offset,indent=TreeView_GetIndent(tree);
        hit.pt.y=(rect.top+rect.bottom)/2;
        for (offset=1;offset<=indent;++offset) {
            hit.pt.x=rect.left-offset;
            if (TreeView_HitTest(tree,&hit)==item && (hit.flags&TVHT_ONITEMBUTTON)) break;
        }
        require(offset<=indent,"locate the native tree expansion button");
        point=hit.pt; ClientToScreen(tree,&point); move_pointer(point.x,point.y); click_pointer(); pump(120);
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
        require(GetMenuItemRect(NULL,GetSubMenu(recording_menu,menu_submenu),(UINT)menu_choice,&rect),"locate real submenu choice");
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
    activate[0]=pointer_input(rect.left+scaled(note,12),rect.top+bar_height(note)/2);
    activate[1].type=activate[2].type=INPUT_MOUSE;
    activate[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN; activate[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    require(SendInput(3,activate,sizeof(INPUT))==3,"activate the actual note before opening its menu");
    pump(100);
    require(GetForegroundWindow()==note->window,"note owns foreground input for menu recording");
    move_pointer(rect.left+menu_offset,rect.top+bar_height(note)/2);
    capture_frame();
    recording_menu=build_note_menu(note); require(recording_menu!=NULL,"build actual application menu");
    menu_submenu=submenu; menu_choice=choice; menu_phase=0;
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

static void record_archive_browser(Note *note) {
    HWND window,tree,list;
    RECT tile={0};
    POINT point;
    Note *reopened;
    populate_demo_archive();
    frame_delay=75;
    record_menu(note,-1,1,CMD_BROWSE);
    window=thread_window(L"PicoSlip.Archive",NULL); tree=GetDlgItem(window,1); list=GetDlgItem(window,2);
    require(tree && list,"record the actual date tree and virtual mosaic");
    select_demo_date(tree,20260000,TRUE);
    select_demo_date(tree,20260400,TRUE);
    select_demo_date(tree,20260408,FALSE);
    select_demo_date(tree,20250000,TRUE);
    select_demo_date(tree,20250600,TRUE);
    select_demo_date(tree,20250618,FALSE);
    require(ListView_GetItemCount(list)==1 && ListView_GetItemRect(list,0,&tile,LVIR_ICON),"locate the archived welcome note");
    point.x=tile.left+35; point.y=tile.top+45;
    ClientToScreen(list,&point); move_pointer(point.x,point.y); frame_delay=65; capture_frame();
    click_pointer(); pump(150);
    reopened=find_note(WELCOME_ARCHIVE_ID);
    require(reopened && !wcscmp(reopened->accepted_text,WELCOME_BODY),"clicking the archive restores the exact square-demo text");
    frame_delay=160; capture_frame();
}

static void record_walkthrough(void) {
    Note *note;
    POINT previous;
    clear_scene(); clear_demo_archive();
    SetWindowPos(backdrop,NULL,0,0,1160,780,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    CreateDirectoryW(L"build\\gif",NULL);
    require(_wfopen_s(&frame_timing,L"build\\gif\\timing.txt",L"w")==0,"create walkthrough frame timeline");
    GetCursorPos(&previous); show_cursor=TRUE; frame_delay=90;
    note=demo_note(0,0,1,70,65,L"Welcome to the machine",WELCOME_BODY,100);
    capture_frame();
    record_menu(note,5,4,CMD_SIZE+4);
    record_edge_resize(note,TRUE,110);
    record_edge_resize(note,FALSE,70);
    frame_delay=75; record_menu(note,3,2,CMD_OPACITY+2);
    record_archive_browser(note);
    fclose(frame_timing); frame_timing=NULL;
    SetCursorPos(previous.x,previous.y); show_cursor=FALSE;
    puts("Recorded square preset, real edge resizing, opacity and archived notes across 2025/2026.");
}

static void type_live(Note *note,const wchar_t *text) {
    frame_delay=10;
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
    GetWindowRect(note->edit,&edit); SetCursorPos(edit.left+12,edit.top+12);
    capture_frame();
    return note;
}

static void record_welcome(void) {
    Note *note,*second,*third;
    POINT previous;
    RECT rect;
    clear_scene();
    frame_directory=L"build\\welcome-gif";
    CreateDirectoryW(frame_directory,NULL);
    require(_wfopen_s(&frame_timing,L"build\\welcome-gif\\timing.txt",L"w")==0,"create live-typing frame timeline");
    SetWindowPos(backdrop,NULL,0,0,1160,780,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    GetCursorPos(&previous); show_cursor=TRUE; frame_delay=80;
    GetWindowRect(backdrop,&rect); SetCursorPos(rect.left+170,rect.top+150);
    capture_frame();
    current_palette=0; next_color=0;
    note=create_live_note(120,120);
    type_live(note,L"#Welcome to the machine");
    frame_delay=90; capture_frame();
    press_enter(note,FALSE); capture_frame();
    require(note->title && !wcscmp(note->title,L"Welcome to the machine"),"typed hash title is promoted by Enter");
    type_live(note,L"Welcome my son\rWelcome to the machine\rWhere have you been?");
    require(!wcscmp(note->accepted_text,WELCOME_BODY),"live typing exactly matches the requested three lines");
    frame_delay=180; capture_frame();
    frame_delay=85; menu_offset=160;
    record_menu(note,5,4,CMD_SIZE+4);
    require(note->width==note->height,"first note becomes square through its menu");
    menu_offset=60;
    second=create_live_note(560,200);
    record_menu(second,5,4,CMD_SIZE+4);
    third=create_live_note(640,300);
    record_menu(third,5,4,CMD_SIZE+4);
    require(second->size==4 && third->size==4,"both newly created notes use the real square preset");
    require(save_note(note),"save the exact demonstration text");
    move_pointer(rect.left+1100,rect.top+710); pump(220);
    frame_delay=250; capture_frame();
    fclose(frame_timing); frame_timing=NULL;
    SetCursorPos(previous.x,previous.y); show_cursor=FALSE;
    puts("Recorded live #title entry, exact requested text, native menus and new square notes.");
}

int main(int argc,char **argv) {
    wchar_t temporary[PATH_CAP],root[PATH_CAP],resolved[PATH_CAP];
    WNDCLASSW cls={0};
    Note *collapsed,*formatted;
    DWORD root_length;
    instance=GetModuleHandleW(NULL); testing=TRUE;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    require(GetTempPathW(PATH_CAP,temporary)!=0,"locate private demonstration folder");
    root_length=GetEnvironmentVariableW(L"PICOSLIP_DEMO_ROOT",root,PATH_CAP);
    if (!root_length) swprintf_s(root,PATH_CAP,L"%sPicoSlip-demo-%lu-%llu",temporary,GetCurrentProcessId(),GetTickCount64());
    require(root_length<PATH_CAP,"demonstration path length is valid");
    root_length=GetFullPathNameW(root,PATH_CAP,resolved,NULL);
    require(root_length>0 && root_length<PATH_CAP &&
            !_wcsnicmp(resolved,temporary,wcslen(temporary)) &&
            !wcsncmp(resolved+wcslen(temporary),L"PicoSlip-demo-",14),"demo data stays in a private temporary directory");
    wcscpy_s(root,PATH_CAP,resolved);
    require(make_paths(root),"create private demonstration archive");
    app_icon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(IDI_APP),IMAGE_ICON,32,32,0);
    photo=(HBITMAP)LoadImageW(NULL,L"build\\screenshot-background.bmp",IMAGE_BITMAP,0,0,LR_LOADFROMFILE|LR_CREATEDIBSECTION);
    require(photo && GetObjectW(photo,sizeof(photo_info),&photo_info),"load requested screenshot photo");
    require(register_classes(),"register demonstration application");
    controller=CreateWindowExW(WS_EX_TOOLWINDOW,APP_CLASS,L"PicoSlip documentation",WS_OVERLAPPED,
                                0,0,0,0,NULL,NULL,instance,NULL);
    require(controller!=NULL,"create the isolated archive owner");
    cls.hInstance=instance; cls.lpfnWndProc=backdrop_proc; cls.lpszClassName=L"PicoSlip.DemoBackdrop";
    cls.style=CS_HREDRAW|CS_VREDRAW;
    require(RegisterClassW(&cls)!=0,"register private screenshot backdrop");
    backdrop=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,L"PicoSlip demo",WS_POPUP|WS_VISIBLE,40,40,1040,640,NULL,NULL,instance,NULL);
    require(backdrop!=NULL,"show private screenshot backdrop");
    if (argc==2 && !strcmp(argv[1],"--welcome")) { record_welcome(); goto finished; }
    formatted=demo_note(0,0,1,55,50,L"Today",L"Bold focus\r\nItalic idea\r\nUnderlined plan\r\n\x2611 Draft the outline\r\n\x2610 Finish one thing",100);
    format_overview_note(formatted);
    demo_note(0,1,1,545,100,L"Project",L"Review the mockup\r\nSend the first draft\r\nFinish before Friday.",100);
    collapsed=demo_note(0,3,1,70,470,L"Later",L"Ideas for another day.",100);
    toggle_note(collapsed);
    capture(L"build\\notes.bmp"); clear_scene();
    demo_note(0,1,0,35,35,L"Pastel",L"Pick up flowers\r\nDinner at 7:30",100);
    demo_note(1,0,0,370,35,L"White",L"Read chapter 4\r\nEmail Alex",100);
    demo_note(2,2,0,705,35,L"Soft yellow",L"Train at 8:15\r\nPack charger",100);
    demo_note(0,3,0,35,350,L"More pastel",L"Book the museum\r\nFind a good cafe",100);
    demo_note(1,1,0,370,350,L"Black",L"Sunday walk\r\nBring the camera",100);
    demo_note(2,1,0,705,350,L"Golden yellow",L"Water the plants\r\nCall home",100);
    capture(L"build\\palettes.bmp"); clear_scene();
    record_walkthrough();
finished:
    clear_scene();
    DestroyWindow(controller);
    DestroyWindow(backdrop); if (app_icon) DestroyIcon(app_icon);
    DeleteObject(photo);
    clear_demo_archive();
    require(RemoveDirectoryW(root),"remove private demo folder");
    puts(argc==2 && !strcmp(argv[1],"--welcome") ? "Captured the live-typing welcome demo." :
         "Captured overview, palettes and the native menu walkthrough.");
    return 0;
}
