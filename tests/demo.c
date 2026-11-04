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
    while (notes) destroy_note(notes);
    RedrawWindow(backdrop,NULL,NULL,RDW_INVALIDATE|RDW_UPDATENOW);
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

static void CALLBACK animate_menu(HWND window,UINT message,UINT_PTR timer,DWORD time) {
    RECT rect;
    INPUT click[3]={{0},{0},{0}};
    (void)window; (void)message; (void)time;
    KillTimer(NULL,timer);
    capture_frame();
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
    note_command(note,selected); pump(220); capture_frame();
}

static void CALLBACK animate_title(HWND window,UINT message,UINT_PTR timer,DWORD time) {
    HWND dialog=FindWindowW(L"#32770",L"Edit title");
    RECT rect;
    (void)window; (void)message; (void)time;
    if (!dialog) return;
    KillTimer(NULL,timer); capture_frame();
    if (!menu_phase) {
        SetDlgItemTextW(dialog,IDC_TITLE,L"Weekend");
        SendDlgItemMessageW(dialog,IDC_TITLE,EM_SETSEL,7,7);
        GetWindowRect(GetDlgItem(dialog,IDOK),&rect);
        SetCursorPos((rect.left+rect.right)/2,(rect.top+rect.bottom)/2);
        menu_phase=1; require(SetTimer(NULL,0,180,animate_title)!=0,"show edited title");
    } else SendMessageW(dialog,WM_COMMAND,IDOK,0);
}

static void record_walkthrough(void) {
    Note *note,*white,*black;
    RECT rect;
    POINT previous;
    clear_scene();
    SetWindowPos(backdrop,NULL,0,0,860,620,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    CreateDirectoryW(L"build\\gif",NULL);
    GetCursorPos(&previous); show_cursor=TRUE;
    note=demo_note(0,0,1,50,50,L"Today",L"Plan the week\r\nBuy coffee\r\nFinish one thing",100);
    record_menu(note,3,2,CMD_OPACITY+2);
    record_menu(note,5,2,CMD_SIZE+2);
    menu_phase=0;
    require(SetTimer(NULL,0,100,animate_title)!=0,"open title editor demonstration");
    note_command(note,CMD_TITLE); capture_frame();
    record_menu(note,7,1,CMD_PALETTE+1);
    white=new_note(); require(white && white->palette==1,"generate white note using selected palette");
    resize_note(white,0); set_title(white,L"White"); replace_text(white,L"Train tickets\r\nPack charger");
    GetWindowRect(backdrop,&rect);
    SetWindowPos(white->window,HWND_TOPMOST,rect.left+430,rect.top+300,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
    ShowWindow(white->window,SW_SHOWNOACTIVATE); capture_frame();
    black=new_note(); require(black && ink_color(black)==RGB(255,255,255),"generate contrasting black note");
    resize_note(black,0); set_title(black,L"Black"); replace_text(black,L"Read chapter 4\r\nEmail Alex");
    SetWindowPos(black->window,HWND_TOPMOST,rect.left+500,rect.top+350,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
    ShowWindow(black->window,SW_SHOWNOACTIVATE); capture_frame();
    toggle_note(note); capture_frame(); capture_frame();
    SetCursorPos(previous.x,previous.y); show_cursor=FALSE;
    puts("Recorded native menu clicks, opacity, size, title editing, palette generation and collapse.");
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
    const wchar_t *body=L"Welcome my son\r\nWelcome to the machine\r\nWhere have you been?";
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
    require(!wcscmp(note->accepted_text,body),"live typing exactly matches the requested three lines");
    frame_delay=180; capture_frame();
    frame_delay=85; menu_offset=160;
    record_menu(note,5,4,CMD_SIZE+4);
    require(note_width(note)==expanded_height(note),"first note becomes square through its menu");
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
    wchar_t temporary[PATH_CAP],root[PATH_CAP],pattern[PATH_CAP],path[PATH_CAP];
    WNDCLASSW cls={0};
    WIN32_FIND_DATAW entry;
    HANDLE found;
    Note *collapsed;
    instance=GetModuleHandleW(NULL); testing=TRUE;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    require(GetTempPathW(PATH_CAP,temporary)!=0,"locate private demonstration folder");
    swprintf_s(root,PATH_CAP,L"%sPicoSlip-demo-%lu",temporary,GetCurrentProcessId());
    require(make_paths(root),"create private demonstration archive");
    app_icon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(IDI_APP),IMAGE_ICON,32,32,0);
    photo=(HBITMAP)LoadImageW(NULL,L"build\\screenshot-background.bmp",IMAGE_BITMAP,0,0,LR_LOADFROMFILE|LR_CREATEDIBSECTION);
    require(photo && GetObjectW(photo,sizeof(photo_info),&photo_info),"load requested screenshot photo");
    require(register_classes(),"register demonstration application");
    cls.hInstance=instance; cls.lpfnWndProc=backdrop_proc; cls.lpszClassName=L"PicoSlip.DemoBackdrop";
    cls.style=CS_HREDRAW|CS_VREDRAW;
    require(RegisterClassW(&cls)!=0,"register private screenshot backdrop");
    backdrop=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,L"PicoSlip demo",WS_POPUP|WS_VISIBLE,40,40,1040,640,NULL,NULL,instance,NULL);
    require(backdrop!=NULL,"show private screenshot backdrop");
    if (argc==2 && !strcmp(argv[1],"--welcome")) { record_welcome(); goto finished; }
    demo_note(0,0,1,55,50,L"Today",L"Buy coffee\r\nCall Maya\r\nWrite one small thing.",100);
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
    DestroyWindow(backdrop); if (app_icon) DestroyIcon(app_icon);
    DeleteObject(photo);
    swprintf_s(pattern,PATH_CAP,L"%s\\*",root);
    found=FindFirstFileW(pattern,&entry);
    if (found!=INVALID_HANDLE_VALUE) {
        do { if (!(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
            swprintf_s(path,PATH_CAP,L"%s\\%s",root,entry.cFileName); require(DeleteFileW(path),"remove private demo data");
        } } while (FindNextFileW(found,&entry));
        FindClose(found);
    }
    require(RemoveDirectoryW(root),"remove private demo folder");
    puts(argc==2 && !strcmp(argv[1],"--welcome") ? "Captured the live-typing welcome demo." :
         "Captured overview, palettes and the native menu walkthrough.");
    return 0;
}
