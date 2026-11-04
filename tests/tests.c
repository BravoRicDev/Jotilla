/* End-to-end Win32 / filesystem checks; C17, no test framework. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdlib.h>
#include <commctrl.h>

static unsigned metadata_writes, metric_queries, graphic_creations, heap_requests;
/* Physical input checks are opt-in so ordinary runs do not type on the user's desktop. */
static BOOL interactive_tests;
static BOOL trace_profile(LPCWSTR section, LPCWSTR key, LPCWSTR value, LPCWSTR path) {
    ++metadata_writes;
    return WritePrivateProfileStringW(section, key, value, path);
}
static BOOL trace_metrics(HDC dc, LPTEXTMETRICW metrics) {
    ++metric_queries; return GetTextMetricsW(dc, metrics);
}
static HPEN trace_pen(int style, int width, COLORREF color) {
    ++graphic_creations; return CreatePen(style, width, color);
}
static HBRUSH trace_brush(COLORREF color) {
    ++graphic_creations; return CreateSolidBrush(color);
}
static void *trace_malloc(size_t size) { ++heap_requests; return malloc(size); }
static void *trace_calloc(size_t count, size_t size) { ++heap_requests; return calloc(count, size); }
static void *trace_realloc(void *memory, size_t size) { ++heap_requests; return realloc(memory, size); }
#define WritePrivateProfileStringW trace_profile
#define GetTextMetricsW trace_metrics
#define CreatePen trace_pen
#define CreateSolidBrush trace_brush
#define malloc trace_malloc
#define calloc trace_calloc
#define realloc trace_realloc
#include "../src/picoslip.c"
#undef WritePrivateProfileStringW
#undef GetTextMetricsW
#undef CreatePen
#undef CreateSolidBrush
#undef malloc
#undef calloc
#undef realloc

static void require(BOOL condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s (Windows error %lu)\n", message, GetLastError());
        exit(1);
    }
}

static int note_count(void) {
    int count = 0;
    Note *note;
    for (note = notes; note; note = note->next) ++count;
    return count;
}

static void pump(DWORD milliseconds) {
    ULONGLONG end = GetTickCount64() + milliseconds;
    MSG event;
    do {
        while (PeekMessageW(&event, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&event); DispatchMessageW(&event);
        }
        Sleep(5);
    } while (GetTickCount64() < end);
}

static void replace_text(Note *note, const wchar_t *text) {
    SendMessageW(note->edit, EM_SETSEL, 0, -1);
    SendMessageW(note->edit, EM_REPLACESEL, TRUE, (LPARAM)text);
}

static void expect_file(const wchar_t *path, const char *expected) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, 0, NULL);
    char buffer[4096];
    DWORD read;
    size_t length = strlen(expected);
    require(file != INVALID_HANDLE_VALUE, "open saved TXT");
    require(ReadFile(file, buffer, sizeof(buffer), &read, NULL), "read saved TXT");
    CloseHandle(file);
    require(read == length && !memcmp(buffer, expected, length), "exact UTF-8 TXT content");
}

static void click_button(Note *note, int number) {
    RECT rect;
    int bar = bar_height(note);
    LPARAM point;
    GetClientRect(note->window, &rect);
    point = MAKELPARAM(rect.right - (number == 1 ? bar * 3 / 2 : bar / 2), bar / 2);
    SendMessageW(note->window, WM_LBUTTONDOWN, MK_LBUTTON, point);
    SendMessageW(note->window, WM_LBUTTONUP, 0, point);
}

static void screenshot(Note *note, const wchar_t *path) {
    RECT rect, bounds;
    BITMAPINFO info = {0};
    BITMAPFILEHEADER header = {0};
    HDC screen, memory;
    HBITMAP bitmap;
    HGDIOBJ old;
    void *pixels;
    DWORD written, size;
    HANDLE file;
    ShowWindow(note->window, SW_SHOWNOACTIVATE);
    SetWindowPos(note->window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    RedrawWindow(note->window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    pump(150);
    GetWindowRect(note->window, &bounds);
    SetRect(&rect, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top);
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = rect.right; info.bmiHeader.biHeight = -rect.bottom;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    screen = GetDC(NULL);
    memory = CreateCompatibleDC(screen);
    bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
    require(bitmap && memory, "allocate UI snapshot");
    old = SelectObject(memory, bitmap);
    require(BitBlt(memory, 0, 0, rect.right, rect.bottom, screen, bounds.left, bounds.top, SRCCOPY), "capture UI snapshot");
    size = (DWORD)(rect.right * rect.bottom * 4);
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + size;
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    require(file != INVALID_HANDLE_VALUE, "create UI snapshot");
    require(WriteFile(file, &header, sizeof(header), &written, NULL) &&
            WriteFile(file, &info.bmiHeader, sizeof(BITMAPINFOHEADER), &written, NULL) &&
            WriteFile(file, pixels, size, &written, NULL), "write UI snapshot");
    CloseHandle(file);
    SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(NULL, screen);
    ShowWindow(note->window, SW_HIDE);
}

static void press_enter(Note *note, BOOL shift) {
    BYTE previous[256], state[256];
    require(GetKeyboardState(previous), "read test thread keyboard state");
    memcpy(state, previous, sizeof(state));
    state[VK_SHIFT] = state[VK_LSHIFT] = state[VK_RSHIFT] = shift ? 0x80 : 0;
    require(SetKeyboardState(state), "set test thread Shift state");
    SendMessageW(note->edit, WM_KEYDOWN, VK_RETURN, 1);
    SendMessageW(note->edit, WM_CHAR, L'\r', 1);
    SendMessageW(note->edit, WM_KEYUP, VK_RETURN, 1);
    require(SetKeyboardState(previous), "restore test thread keyboard state");
}

static void title_checks(void) {
    Note *titled = new_note(), *literal = new_note();
    wchar_t titled_id[80], literal_id[80], text[256];
    LOGFONTW font;
    SIZE narrow, wide;
    HDC dc;
    HGDIOBJ old;
    require(titled && literal, "create title test notes");
    require(!titled->title && !literal->title, "new notes have no default title");
    require(GetObjectW(titled->font, sizeof(font), &font) != 0 && font.lfWeight == FW_BOLD,
            "font uses bold weight");
    dc = GetDC(titled->edit); old = SelectObject(dc, titled->font);
    GetTextExtentPoint32W(dc, L"iiii", 4, &narrow);
    GetTextExtentPoint32W(dc, L"WWWW", 4, &wide);
    require(narrow.cx == wide.cx, "actual selected font is monospaced");
    SendMessageW(titled->window, WM_CTLCOLOREDIT, (WPARAM)dc, (LPARAM)titled->edit);
    require(GetTextColor(dc) == RGB(0, 0, 0), "body text is pure black");
    SelectObject(dc, old); ReleaseDC(titled->edit, dc);

    replace_text(titled, L"#Spesa è urgente");
    press_enter(titled, FALSE);
    require(titled->title && !wcscmp(titled->title, L"Spesa è urgente"), "Enter promotes #first line without hash");
    require(GetWindowTextLengthW(titled->edit) == 0, "promoted title is removed from body without blank line");
    replace_text(titled, L"Caffè e pane");
    pump(650);
    require(!titled->dirty, "title and body autosave");
    expect_file(titled->path, "#Spesa è urgente\r\nCaffè e pane");
    screenshot(titled, L"build\\titled-expanded.bmp");
    click_button(titled, 1);
    screenshot(titled, L"build\\titled-collapsed.bmp");

    replace_text(literal, L"#appunto");
    press_enter(literal, TRUE);
    GetWindowTextW(literal->edit, text, 256);
    require(!literal->title && !wcscmp(text, L"#appunto\r\n"), "Shift+Enter keeps hashtag line in body");
    SendMessageW(literal->edit, EM_REPLACESEL, TRUE, (LPARAM)L"Resta nel testo");
    press_enter(literal, FALSE);
    require(!literal->title, "Enter on later lines cannot promote initial literal hashtag");
    SendMessageW(literal->edit, EM_REPLACESEL, TRUE, (LPARAM)L"Anche qui.");
    require(save_note(literal), "save literal hashtag note");
    expect_file(literal->path, "#appunto\r\nResta nel testo\r\nAnche qui.");
    wcscpy_s(titled_id, 80, titled->id); wcscpy_s(literal_id, 80, literal->id);
    destroy_note(titled); destroy_note(literal);
    titled = open_note(titled_id, 0, TRUE); literal = open_note(literal_id, 0, TRUE);
    require(titled && literal && titled->collapsed, "reopen title notes and collapsed state");
    require(titled->title && !wcscmp(titled->title, L"Spesa è urgente"), "Unicode title survives restart");
    GetWindowTextW(titled->edit, text, 256);
    require(!wcscmp(text, L"Caffè e pane"), "title header is excluded from reopened body");
    GetWindowTextW(literal->edit, text, 256);
    require(!literal->title && !wcscmp(text, L"#appunto\r\nResta nel testo\r\nAnche qui."),
            "literal Shift+Enter note remains literal after restart");
    replace_text(literal, L"#");
    press_enter(literal, FALSE);
    require(!literal->title, "a bare hash does not create an empty title");
    SendMessageW(titled->window, WM_CLOSE, 0, 0);
    SendMessageW(literal->window, WM_CLOSE, 0, 0);
    puts("PASS: bold monospaced black font, Enter title, Shift+Enter literal, body separation and restart persistence.");
}

static void efficiency_checks(BOOL enforce) {
    Note *note = new_note();
    wchar_t body[64];
    unsigned clean_writes, typing_allocations, typing_metrics, repaint_objects;
    int i;
    require(note != NULL, "create efficiency check note");
    metadata_writes = 0;
    for (i = 0; i < 20; ++i) require(save_note(note), "save an unchanged note");
    clean_writes = metadata_writes;
    heap_requests = metric_queries = 0;
    for (i = 0; i < 50; ++i) SendMessageW(note->edit, WM_CHAR, L'a', 1);
    typing_allocations = heap_requests; typing_metrics = metric_queries;
    GetWindowTextW(note->edit, body, 64);
    require(wcslen(body) == 50 && text_fits(note), "all fifty typed characters remain visible");
    require(save_note(note), "save typed input");
    ShowWindow(note->window, SW_SHOWNOACTIVATE); pump(30);
    graphic_creations = 0;
    for (i = 0; i < 100; ++i)
        RedrawWindow(note->window, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    repaint_objects = graphic_creations;
    ShowWindow(note->window, SW_HIDE);
    if (enforce) {
        require(clean_writes == 0, "unchanged saves do not rewrite metadata");
        require(typing_allocations <= 1 && typing_metrics == 0, "typing reuses text buffer and font metrics");
        require(repaint_objects == 0, "repaints reuse graphic resources");
    }
    printf("MEASURE: 20 clean saves: %u metadata calls; 50 typed characters: %u allocations, %u font queries; "
           "100 repaints: %u graphic creations.\n", clean_writes, typing_allocations, typing_metrics, repaint_objects);
    SendMessageW(note->window, WM_CLOSE, 0, 0);
}

static void serialization_checks(void) {
    Note *note = new_note();
    wchar_t body[128];
    require(note != NULL, "create serialization check note");
    replace_text(note, L"#  Titolo 😀  ");
    press_enter(note, FALSE);
    require(note->title && !wcscmp(note->title, L"Titolo 😀"), "trimmed Unicode title");
    require(save_note(note), "save a title with an empty body");
    expect_file(note->path, "#Titolo 😀\r\n");
    replace_text(note, L"#Nuovo\r\nCorpo già presente: 漢字");
    SendMessageW(note->edit, EM_SETSEL, 6, 6);
    press_enter(note, FALSE);
    GetWindowTextW(note->edit, body, 128);
    require(!wcscmp(note->title, L"Nuovo") && !wcscmp(body, L"Corpo già presente: 漢字"),
            "promoting an existing first line preserves following body");
    require(save_note(note), "save replacement title and remaining body");
    expect_file(note->path, "#Nuovo\r\nCorpo già presente: 漢字");
    replace_text(note, L"\xd800");
    require(!save_note(note), "incomplete Unicode surrogate cannot replace a valid saved file");
    expect_file(note->path, "#Nuovo\r\nCorpo già presente: 漢字");
    replace_text(note, L"😀");
    require(save_note(note), "complete Unicode pair can be saved");
    expect_file(note->path, "#Nuovo\r\n😀");
    SendMessageW(note->window, WM_CLOSE, 0, 0);
    puts("PASS: empty title body, Unicode serialization, in-place title promotion and invalid UTF-16 protection.");
}

static Note *dialog_note;
static const wchar_t *dialog_input;
static BOOL native_menu_seen;
static DWORD WINAPI double_click_title(void *unused) {
    INPUT clicks[2]={{0},{0}};
    (void)unused;
    clicks[0].type=clicks[1].type=INPUT_MOUSE;
    clicks[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN; clicks[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    if (SendInput(1,clicks,sizeof(INPUT))!=1) return 1;
    Sleep(30);
    if (SendInput(1,clicks+1,sizeof(INPUT))!=1) return 1;
    /* Keep input timing independent of the native caption move loop. */
    Sleep(80);
    if (SendInput(1,clicks,sizeof(INPUT))!=1) return 1;
    Sleep(30);
    return SendInput(1,clicks+1,sizeof(INPUT))==1 ? 0 : 1;
}
static void CALLBACK inspect_native_menu(HWND window,UINT message,UINT_PTR timer,DWORD time) {
    GUITHREADINFO info={sizeof(info)};
    (void)window; (void)message; (void)time;
    if (GetGUIThreadInfo(GetCurrentThreadId(),&info) && (info.flags&GUI_POPUPMENUMODE)) {
        native_menu_seen=TRUE; KillTimer(NULL,timer); EndMenu();
    }
}
static void CALLBACK fill_title_dialog(HWND window,UINT message,UINT_PTR timer,DWORD time) {
    HWND dialog=FindWindowW(L"#32770",L"Edit title");
    (void)window; (void)message; (void)time;
    if (dialog && GetWindow(dialog,GW_OWNER)==dialog_note->window) {
        KillTimer(NULL,timer);
        SetDlgItemTextW(dialog,IDC_TITLE,dialog_input);
        SendMessageW(dialog,WM_COMMAND,IDOK,0);
    }
}

static void options_checks(void) {
    Note *note=new_note(), *white, *black, *yellow;
    wchar_t id[80], body[1024], restored[1024];
    HMENU menu;
    BYTE alpha;
    COLORREF key;
    DWORD flags;
    CHARFORMAT2W ink = {0};
    unsigned year,month,day,hour,minute,second,millisecond,suffix;
    int i;
    POINT previous;
    RECT bounds;
    INPUT right_click[2]={{0},{0}};
    UINT_PTR menu_timer;
    require(note!=NULL,"create options note");
    require(swscanf_s(note->id,L"note-%4u-%2u-%2u_%2u-%2u-%2u-%3u-%u",&year,&month,&day,&hour,&minute,&second,&millisecond,&suffix)==8,
            "archive names expose sortable creation date and time");
    menu=build_note_menu(note);
    require(menu && GetMenuItemCount(menu)==8 && GetMenuItemID(menu,0)==CMD_ARCHIVE && GetMenuItemID(menu,1)==CMD_BROWSE &&
            GetMenuItemID(menu,4)==CMD_TITLE && GetMenuItemCount(GetSubMenu(menu,3))==5 &&
            GetMenuItemCount(GetSubMenu(menu,5))==5 && GetMenuItemCount(GetSubMenu(menu,7))==3,"context menu exposes all requested choices");
    DestroyMenu(menu);
    replace_text(note,L"Saved size preset");
    require(resize_note(note,4),"square preset is available");
    GetWindowRect(note->window,&bounds);
    require(bounds.right-bounds.left==bounds.bottom-bounds.top,"square note has equal width and height");
    wcscpy_s(id,80,note->id);
    require(save_note(note),"save square preset");
    destroy_note(note); note=open_note(id,0,TRUE);
    require(note && note->size==4 && note_width(note)==expanded_height(note),"square shape survives reopening");
    require(resize_note(note,1),"return to default size");
    if (interactive_tests) {
        GetCursorPos(&previous); ShowWindow(note->window,SW_SHOWNORMAL);
        SetWindowPos(note->window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
        SetForegroundWindow(note->window); pump(80);
        GetWindowRect(note->window,&bounds);
        SetCursorPos(bounds.left+scaled(note,45),bounds.top+bar_height(note)/2);
        menu_timer=SetTimer(NULL,0,100,inspect_native_menu);
        require(menu_timer!=0,"inspect real right-click menu");
        right_click[0].type=right_click[1].type=INPUT_MOUSE;
        right_click[0].mi.dwFlags=MOUSEEVENTF_RIGHTDOWN; right_click[1].mi.dwFlags=MOUSEEVENTF_RIGHTUP;
        require(SendInput(2,right_click,sizeof(INPUT))==2,"right-click the actual note bar");
        pump(500); KillTimer(NULL,menu_timer);
        SetCursorPos(previous.x,previous.y); ShowWindow(note->window,SW_HIDE);
        require(native_menu_seen,"right-clicking the bar opens the actual native popup menu");
    }
    for (i=0;i<5;++i) {
        note_command(note,CMD_OPACITY+i);
        require(note->opacity==OPACITIES[i],"opacity preset selected");
        if (i) require(GetLayeredWindowAttributes(note->window,&key,&alpha,&flags) &&
                       alpha==(BYTE)MulDiv(255,OPACITIES[i],100),"native window uses requested alpha");
    }
    note_command(note,CMD_OPACITY);
    require(!(GetWindowLongPtrW(note->window,GWL_EXSTYLE)&WS_EX_LAYERED),"opaque preset releases layered rendering");
    require(resize_note(note,3),"increase note size");
    body[0]=0;
    for (i=0;i<12;++i) wcscat_s(body,1024,L"A visible line of text\r\n");
    replace_text(note,body);
    require(!wcscmp(note->accepted_text,body),"larger note accepts more content");
    require(!resize_note(note,0) && note->size==3,"shrinking refuses to hide or truncate existing text");
    require(!wcscmp(note->accepted_text,body),"refused shrink preserves text");
    dialog_note=note; dialog_input=L"Edited title è";
    require(SetTimer(NULL,0,40,fill_title_dialog)!=0,"schedule native title dialog input");
    note_command(note,CMD_TITLE);
    require(note->title && !wcscmp(note->title,dialog_input),"title can be edited through actual dialog");
    dialog_input=L"Double-click title";
    if (interactive_tests) {
        ShowWindow(note->window,SW_SHOWNORMAL);
        SetWindowPos(note->window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
        SetForegroundWindow(note->window); pump(80);
        GetWindowRect(note->window,&bounds); GetCursorPos(&previous);
        SetCursorPos(bounds.left+scaled(note,45),bounds.top+bar_height(note)/2);
        {
            HANDLE input_thread;
            DWORD exit_code;
            UINT_PTR timer=SetTimer(NULL,0,100,fill_title_dialog);
            input_thread=CreateThread(NULL,0,double_click_title,NULL,0,NULL);
            require(timer && input_thread,"schedule a real double click while the native message loop runs");
            pump(500); KillTimer(NULL,timer);
            require(WaitForSingleObject(input_thread,1000)==WAIT_OBJECT_0 &&
                    GetExitCodeThread(input_thread,&exit_code) && exit_code==0,"deliver both physical title clicks");
            CloseHandle(input_thread);
        }
        SetCursorPos(previous.x,previous.y); ShowWindow(note->window,SW_HIDE);
    } else {
        require(SetTimer(NULL,0,40,fill_title_dialog)!=0,"schedule double-click title dialog input");
        SendMessageW(note->window,WM_NCLBUTTONDBLCLK,HTCAPTION,0);
    }
    require(note->title && !wcscmp(note->title,dialog_input) && !note->collapsed,
            "double-click opens the same title editor without collapsing the note");
    dialog_input=L"";
    require(SetTimer(NULL,0,40,fill_title_dialog)!=0,"schedule title removal");
    note_command(note,CMD_TITLE);
    require(note->title==NULL && !wcscmp(note->accepted_text,body),"dialog removes title while preserving body");
    note_command(note,CMD_PALETTE+1);
    white=new_note(); black=new_note();
    require(white && black && white->palette==1 && black->palette==1 &&
            note_color(white)==RGB(255,255,255) && ink_color(white)==RGB(0,0,0) &&
            note_color(black)==RGB(28,28,28) && ink_color(black)==RGB(255,255,255),"monochrome generation preserves contrast");
    ink.cbSize = sizeof(ink);
    SendMessageW(black->edit, EM_GETCHARFORMAT, SCF_DEFAULT, (LPARAM)&ink);
    require(ink.crTextColor == RGB(255,255,255), "rich text on dark notes uses white ink");
    note_command(note,CMD_PALETTE+2);
    yellow=new_note();
    require(yellow && yellow->palette==2 && note_color(yellow)==PALETTES[2][0],"yellow-only generation");
    require(note->palette==0,"palette preference does not recolor existing notes");
    note_command(note,CMD_OPACITY+2);
    wcscpy_s(id,80,note->id);
    require(save_note(note),"save all note options");
    destroy_note(note); note=open_note(id,0,TRUE);
    require(note && note->size==3 && note->opacity==80 && note->palette==0 && !note->title,"size, alpha, palette and deleted title persist");
    GetWindowTextW(note->edit,restored,1024);
    require(!wcscmp(restored,body),"options round trip preserves text");
    SendMessageW(note->window,WM_CLOSE,0,0); SendMessageW(white->window,WM_CLOSE,0,0);
    SendMessageW(black->window,WM_CLOSE,0,0); SendMessageW(yellow->window,WM_CLOSE,0,0);
    current_palette=0; next_color=0;
    puts("PASS: archive names, menu structure, native opacity, safe sizing, title dialog, palettes and option persistence.");
}

static void shortcut(Note *note, UINT key, BOOL shift) {
    BYTE previous[256], state[256];
    require(GetKeyboardState(previous), "read shortcut keyboard state");
    memset(state, 0, sizeof(state));
    state[VK_CONTROL] = state[VK_LCONTROL] = 0x80;
    state[VK_SHIFT] = state[VK_LSHIFT] = shift ? 0x80 : 0;
    require(SetKeyboardState(state), "set shortcut modifiers");
    SendMessageW(note->edit, WM_KEYDOWN, key, 1);
    SendMessageW(note->edit, WM_CHAR, key - 'A' + 1, 1);
    require(SetKeyboardState(previous), "restore shortcut keyboard state");
}

static DWORD effects_at(Note *note, LONG position) {
    CHARRANGE selection;
    CHARFORMAT2W format = {0};
    format.cbSize = sizeof(format);
    SendMessageW(note->edit, EM_EXGETSEL, 0, (LPARAM)&selection);
    SendMessageW(note->edit, EM_SETSEL, position, position + 1);
    SendMessageW(note->edit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&format);
    SendMessageW(note->edit, EM_EXSETSEL, 0, (LPARAM)&selection);
    return format.dwEffects;
}

static void click_checkbox(Note *note, LONG position) {
    POINTL point;
    LPARAM click;
    SendMessageW(note->edit, EM_POSFROMCHAR, (WPARAM)&point, position);
    click = MAKELPARAM(point.x + 2, point.y + note->line_height / 2);
    SendMessageW(note->edit, WM_LBUTTONDOWN, MK_LBUTTON, click);
    SendMessageW(note->edit, WM_LBUTTONUP, 0, click);
}

static void type_input(const wchar_t *text) {
    INPUT input[2] = {{0}, {0}};
    input[0].type = input[1].type = INPUT_KEYBOARD;
    input[0].ki.dwFlags = KEYEVENTF_UNICODE;
    input[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    while (*text) {
        input[0].ki.wScan = input[1].ki.wScan = *text++;
        require(SendInput(2, input, sizeof(INPUT)) == 2, "type actual keyboard text");
    }
    pump(80);
}

static void key_input(WORD key, BOOL shift) {
    INPUT input[4] = {{0}, {0}, {0}, {0}};
    int i;
    for (i = 0; i < 4; ++i) input[i].type = INPUT_KEYBOARD;
    input[0].ki.wVk = input[3].ki.wVk = VK_LSHIFT;
    input[1].ki.wVk = input[2].ki.wVk = key;
    input[2].ki.dwFlags = input[3].ki.dwFlags = KEYEVENTF_KEYUP;
    require(SendInput(shift ? 4 : 2, input + (shift ? 0 : 1), sizeof(INPUT)) == (shift ? 4u : 2u),
            "press actual keyboard keys");
    pump(80);
}

static void keyboard_checks(void) {
    Note *note = new_note();
    INPUT click[2] = {{0}, {0}};
    POINT previous, checkbox;
    POINTL position;
    require(note != NULL, "create real keyboard input note");
    ShowWindow(note->window, SW_SHOWNORMAL);
    SetForegroundWindow(note->window); SetFocus(note->edit); pump(100);
    type_input(L"*");
    require(!wcscmp(note->accepted_text, L"*"), "real keyboard writes the checklist trigger");
    key_input(VK_RETURN, TRUE);
    require(!wcscmp(note->accepted_text, L"\x2610 "), "real Shift+Enter starts a todo");
    type_input(L"Pane"); key_input(VK_RETURN, FALSE);
    require(!wcscmp(note->accepted_text, L"\x2610 Pane\r\n\x2610 "), "real Enter adds exactly one todo");
    type_input(L"Latte"); key_input(VK_RETURN, FALSE); key_input(VK_BACK, FALSE);
    type_input(L"Fine"); key_input(VK_BACK, FALSE);
    require(!wcscmp(note->accepted_text, L"\x2610 Pane\r\n\x2610 Latte\r\nFin"),
            "real Backspace exits an empty todo and deletes one ordinary character");
    key_input(VK_RETURN, FALSE);
    require(!wcscmp(note->accepted_text, L"\x2610 Pane\r\n\x2610 Latte\r\nFin\r\n"),
            "real Enter outside the todo inserts only one line break");
    SendMessageW(note->edit, EM_POSFROMCHAR, (WPARAM)&position, 0);
    checkbox.x = position.x + 2; checkbox.y = position.y + note->line_height / 2;
    ClientToScreen(note->edit, &checkbox); GetCursorPos(&previous); SetCursorPos(checkbox.x, checkbox.y);
    click[0].type = click[1].type = INPUT_MOUSE;
    click[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN; click[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    require(SendInput(2, click, sizeof(INPUT)) == 2, "click a real checkbox");
    pump(80); SetCursorPos(previous.x, previous.y);
    require(note->accepted_text[0] == L'\x2611' && (effects_at(note, 2) & CFE_STRIKEOUT),
            "real mouse click checks and strikes an item");
    screenshot(note, L"build\\keyboard-active.bmp");
    SendMessageW(note->window, WM_CLOSE, 0, 0);
    puts("PASS: real keyboard checklist creation, continuation, Backspace, plain Enter and mouse completion.");
}

static void editing_checks(void) {
    Note *note = new_note();
    wchar_t id[80], overflow[4096];
    const DWORD all = CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE | CFE_STRIKEOUT;
    int i;
    CHARFORMAT2W actual = {0};
    require(note != NULL, "create rich editing note");
    actual.cbSize = sizeof(actual);
    SendMessageW(note->edit, EM_GETCHARFORMAT, SCF_DEFAULT, (LPARAM)&actual);
    require(!wcscmp(actual.szFaceName, L"Consolas") && actual.crTextColor == ink_color(note),
            "native rich editor uses the intended font and palette ink");
    replace_text(note, L"Testo è 😀");
    SendMessageW(note->edit, EM_SETSEL, 0, 5);
    shortcut(note, 'B', FALSE);
    require(!(effects_at(note, 1) & CFE_BOLD), "Ctrl+B toggles existing bold off");
    shortcut(note, 'B', FALSE); shortcut(note, 'I', FALSE);
    shortcut(note, 'U', FALSE); shortcut(note, 'X', TRUE);
    require((effects_at(note, 1) & all) == all && !(effects_at(note, 6) & (all & ~CFE_BOLD)),
            "all shortcuts format only the selection without inserting control characters");
    require(!wcscmp(note->accepted_text, L"Testo è 😀"), "formatting preserves Unicode text");
    for (i = 0; i < 4095; ++i) overflow[i] = L'W';
    overflow[4095] = 0;
    replace_text(note, overflow);
    require(!wcscmp(note->accepted_text, L"Testo è 😀") && (effects_at(note, 1) & all) == all,
            "overflow rollback preserves both content and character formatting");
    require(save_note(note), "save rich character styles");
    wcscpy_s(id, 80, note->id); destroy_note(note); note = open_note(id, 0, TRUE);
    require(note && (effects_at(note, 1) & all) == all && !(effects_at(note, 6) & CFE_ITALIC),
            "selected formatting survives reopening");
    SendMessageW(note->edit, EM_SETSEL, 0, 5); shortcut(note, 'I', FALSE);
    require(save_note(note), "save a format-only edit");
    shortcut(note, 'Z', FALSE);
    require(note->dirty && (effects_at(note, 1) & CFE_ITALIC), "undoing a format-only edit schedules autosave");
    SendMessageW(note->window, WM_CLOSE, 0, 0);

    note = new_note();
    replace_text(note, L"*"); press_enter(note, TRUE);
    require(!wcscmp(note->accepted_text, L"\x2610 "), "asterisk and Shift+Enter start a checklist in place");
    SendMessageW(note->edit, EM_REPLACESEL, TRUE, (LPARAM)L"Primo"); press_enter(note, FALSE);
    SendMessageW(note->edit, EM_REPLACESEL, TRUE, (LPARAM)L"Secondo"); press_enter(note, FALSE);
    require(!wcscmp(note->accepted_text, L"\x2610 Primo\r\n\x2610 Secondo\r\n\x2610 "),
            "Enter continues the checklist");
    SendMessageW(note->edit, WM_KEYDOWN, VK_BACK, 1);
    SendMessageW(note->edit, WM_CHAR, L'\b', 1);
    SendMessageW(note->edit, WM_KEYUP, VK_BACK, 1);
    SendMessageW(note->edit, EM_REPLACESEL, TRUE, (LPARAM)L"Normale");
    require(!wcscmp(note->accepted_text, L"\x2610 Primo\r\n\x2610 Secondo\r\nNormale"),
            "Backspace removes the final empty item and resumes ordinary text");
    click_checkbox(note, 0);
    require(note->accepted_text[0] == L'\x2611' && (effects_at(note, 2) & CFE_STRIKEOUT) &&
            !(effects_at(note, 10) & CFE_STRIKEOUT), "checking strikes only that item's text");
    SendMessageW(note->edit, WM_UNDO, 0, 0);
    require(note->accepted_text[0] == L'\x2610' && !(effects_at(note, 2) & CFE_STRIKEOUT),
            "one undo restores both checkbox and text style");
    SendMessageW(note->edit, EM_REDO, 0, 0);
    require(note->accepted_text[0] == L'\x2611' && (effects_at(note, 2) & CFE_STRIKEOUT),
            "redo restores the completed item");
    click_checkbox(note, 8);
    require((effects_at(note, 10) & CFE_STRIKEOUT), "checkbox on a later paragraph is clickable");
    click_checkbox(note, 8);
    require(!(effects_at(note, 10) & CFE_STRIKEOUT), "unchecking restores ordinary text");
    require(save_note(note), "save checklist");
    expect_file(note->path, "☑ Primo\r\n☐ Secondo\r\nNormale");
    wcscpy_s(id, 80, note->id); destroy_note(note); note = open_note(id, 0, TRUE);
    require(note && note->accepted_text[0] == L'\x2611' && (effects_at(note, 2) & CFE_STRIKEOUT),
            "checkbox state and strikethrough survive reopening");
    SendMessageW(note->edit, EM_SETSEL, 7, 7); press_enter(note, FALSE);
    SendMessageW(note->edit, EM_REPLACESEL, TRUE, (LPARAM)L"Nuovo");
    require(!(effects_at(note, 10) & CFE_STRIKEOUT), "new item after a completed item is not struck");
    press_enter(note, FALSE); press_enter(note, FALSE);
    SendMessageW(note->edit, EM_REPLACESEL, TRUE, (LPARAM)L"Libero");
    require(wcsstr(note->accepted_text, L"\r\nLibero\r\n") != NULL,
            "Enter on an empty item also exits the checklist");
    require(!(effects_at(note, 10) & CFE_STRIKEOUT), "unchecked text stays ordinary after continuing the list");
    screenshot(note, L"build\\checklist.bmp");
    SendMessageW(note->window, WM_CLOSE, 0, 0);
    puts("PASS: formatting shortcuts, styled overflow rollback, checklists, checkbox undo/redo and rich persistence.");
}

static POINT drag_target;
static int drag_phase;
static void CALLBACK resize_drag(HWND window, UINT message, UINT_PTR timer, DWORD time) {
    INPUT release = {0};
    (void)window; (void)message; (void)time;
    if (!drag_phase++) { SetCursorPos(drag_target.x, drag_target.y); return; }
    KillTimer(NULL, timer);
    release.type = INPUT_MOUSE; release.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &release, sizeof(release));
}

static void resizing_checks(void) {
    Note *note = new_note();
    RECT bounds, proposed;
    wchar_t id[80];
    int width, height, i;
    POINT previous;
    INPUT down = {0};
    const int hits[] = {HTTOPLEFT, HTTOPRIGHT, HTBOTTOMLEFT, HTBOTTOMRIGHT};
    BOOL nonclient_rendering = TRUE;
    require(note != NULL, "create custom sizing note");
    replace_text(note,L"Resize me");
    require(SUCCEEDED(DwmGetWindowAttribute(note->window, DWMWA_NCRENDERING_ENABLED,
                                          &nonclient_rendering, sizeof(nonclient_rendering))) && !nonclient_rendering,
            "native resize handles do not enable the standard gray window frame");
    SetWindowPos(note->window, NULL, 120, 140, scaled(note, 370), scaled(note, 210), SWP_NOZORDER | SWP_NOACTIVATE);
    width = note->width; height = note->height;
    GetWindowRect(note->window, &bounds);
    for (i = 0; i < 4; ++i)
        require(SendMessageW(note->window, WM_NCHITTEST, 0,
                            MAKELPARAM((i & 1) ? bounds.right - 1 : bounds.left + 1,
                                       (i & 2) ? bounds.bottom - 1 : bounds.top + 1)) == hits[i],
                "all four corners expose native resize handles");
    if (interactive_tests) {
        GetCursorPos(&previous); ShowWindow(note->window, SW_SHOWNORMAL);
        SetForegroundWindow(note->window); pump(80);
        SetCursorPos(bounds.right - 1, bounds.bottom - 1);
        drag_target.x = bounds.right + 39; drag_target.y = bounds.bottom + 29; drag_phase = 0;
        require(SetTimer(NULL, 0, 100, resize_drag) != 0, "schedule a real native resize drag");
        down.type = INPUT_MOUSE; down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        require(SendInput(1, &down, sizeof(down)) == 1, "press the real bottom-right corner");
        pump(450); SetCursorPos(previous.x, previous.y); ShowWindow(note->window, SW_HIDE);
        require(note->width > width && note->height > height, "a real corner drag freely resizes the native window");
        width = note->width; height = note->height;
        GetWindowRect(note->window, &bounds);
    }
    SendMessageW(note->edit, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA / 2), 0);
    require(note->width == width && note->height == height, "partial wheel steps accumulate");
    SendMessageW(note->edit, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA / 2), 0);
    require(note->width > width && note->height > height &&
            abs(note->height - MulDiv(height, note->width, width)) <= 1,
            "Ctrl+wheel on the editor scales the current aspect ratio");
    SendMessageW(note->window, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, -WHEEL_DELTA), 0);
    require(abs(note->width - width) <= 1 && abs(note->height - height) <= 1,
            "reverse wheel scales the note down");
    proposed = bounds; proposed.right += scaled(note, 50); proposed.bottom += scaled(note, 70);
    require(SendMessageW(note->window, WM_SIZING, WMSZ_BOTTOMRIGHT, (LPARAM)&proposed) != 0, "native sizing gesture");
    SetWindowPos(note->window, NULL, proposed.left, proposed.top, proposed.right - proposed.left,
                 proposed.bottom - proposed.top, SWP_NOZORDER | SWP_NOACTIVATE);
    width = note->width; height = note->height;
    toggle_note(note);
    SendMessageW(note->window, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA), 0);
    require(note->height > height && note_height(note) == bar_height(note),
            "a collapsed note scales its stored expanded dimensions");
    toggle_note(note);
    width = note->width; height = note->height;
    wcscpy_s(id, 80, note->id); require(save_note(note), "save arbitrary dimensions");
    destroy_note(note); note = open_note(id, 0, TRUE);
    require(note && note->width == width && note->height == height && note->size == -1,
            "arbitrary dimensions and aspect ratio survive reopening");
    SendMessageW(note->window, WM_CLOSE, 0, 0);
    puts("PASS: corner resize handles, proportional Ctrl+wheel, partial wheel input, collapsed resizing and persistence.");
}

static HTREEITEM find_date(HWND tree, HTREEITEM item, unsigned date) {
    while (item) {
        TVITEMW value={0};
        HTREEITEM child;
        value.hItem=item; value.mask=TVIF_PARAM;
        TreeView_GetItem(tree,&value);
        if ((unsigned)value.lParam==date) return item;
        child=find_date(tree,TreeView_GetChild(tree,item),date);
        if (child) return child;
        item=TreeView_GetNextSibling(tree,item);
    }
    return NULL;
}

static void archive_fixture(const wchar_t *id,const char *text) {
    wchar_t path[PATH_CAP];
    HANDLE file;
    DWORD written, length=(DWORD)strlen(text);
    swprintf_s(path,PATH_CAP,L"%s\\%s.txt",directory,id);
    file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    require(file!=INVALID_HANDLE_VALUE,"create dated archive fixture");
    require(WriteFile(file,text,length,&written,NULL) && written==length,"write archive preview fixture");
    CloseHandle(file);
    require(write_setting(id,L"open",FALSE) && write_setting(id,L"has_title",TRUE),"archive fixture metadata");
}

static void CALLBACK confirm_archive_delete(HWND window,UINT message,UINT_PTR timer,DWORD time) {
    HWND dialog=FindWindowW(L"#32770",L"PicoSlip");
    (void)window; (void)message; (void)time;
    if (dialog && GetWindow(dialog,GW_OWNER)==FindWindowW(L"PicoSlip.Archive",NULL)) {
        KillTimer(NULL,timer); SendMessageW(dialog,WM_COMMAND,IDYES,0);
    }
}

static BOOL archive_menu_seen;
static void CALLBACK choose_archive_delete(HWND window,UINT message,UINT_PTR timer,DWORD time) {
    GUITHREADINFO info={sizeof(info)};
    INPUT keys[4]={{0},{0},{0},{0}};
    int i;
    (void)window; (void)message; (void)time;
    if (!GetGUIThreadInfo(GetCurrentThreadId(),&info) || !(info.flags&GUI_POPUPMENUMODE)) return;
    KillTimer(NULL,timer); archive_menu_seen=TRUE;
    for (i=0;i<4;++i) { keys[i].type=INPUT_KEYBOARD; keys[i].ki.wVk=i<2?VK_DOWN:VK_RETURN; }
    keys[1].ki.dwFlags=keys[3].ki.dwFlags=KEYEVENTF_KEYUP;
    SendInput(4,keys,sizeof(INPUT));
}

static void archive_checks(void) {
    const wchar_t *first=L"note-2024-01-03_08-00-00-000-001", *second=L"note-2024-02-04_09-00-00-000-001";
    const wchar_t *third=L"note-2025-03-05_10-00-00-000-001";
    HWND window,tree,list;
    HTREEITEM year,month,day;
    NMITEMACTIVATE click={0};
    wchar_t preview[256],path[PATH_CAP],id[80];
    Note *note;
    HANDLE lock;
    int before;
    RECT row;
    archive_fixture(first,"#Spesa è urgente\r\nPane, caffè e latte");
    archive_fixture(second,"#Viaggio\r\nPartenza domani");
    archive_fixture(third,"#Progetto\r\nUna nota di un altro anno");
    browse_notes();
    window=FindWindowW(L"PicoSlip.Archive",NULL);
    require(window!=NULL,"archive opens through the note menu command");
    tree=GetDlgItem(window,1); list=GetDlgItem(window,2);
    require(tree && list,"archive consists of a native date tree and a virtual preview list");
    year=find_date(tree,TreeView_GetRoot(tree),20240000);
    month=find_date(tree,TreeView_GetRoot(tree),20240100);
    day=find_date(tree,TreeView_GetRoot(tree),20240103);
    require(year && month && day && TreeView_GetParent(tree,day)==month && TreeView_GetParent(tree,month)==year,
            "archive tree groups notes by creation year, month and day");
    {
        TVITEMW label={0};
        label.mask=TVIF_TEXT; label.hItem=month; label.pszText=preview; label.cchTextMax=256;
        require(TreeView_GetItem(tree,&label) && !wcscmp(preview,L"January"),
                "archive month names stay English independently of the Windows locale");
    }
    TreeView_SelectItem(tree,day); pump(40);
    require(ListView_GetItemCount(list)==1,"selecting a day filters the virtual list");
    ListView_GetItemText(list,0,0,preview,256);
    require(!wcscmp(preview,L"Spesa è urgente"),"bounded lazy previews preserve UTF-8 accents");
    ListView_GetItemRect(list,0,&row,LVIR_BOUNDS);
    require(row.bottom-row.top>=MulDiv(110,(int)GetDpiForWindow(window),96),"previews have post-it sized rows");
    before=note_count();
    click.hdr.hwndFrom=list; click.hdr.code=NM_CLICK; click.iItem=0;
    SendMessageW(window,WM_NOTIFY,2,(LPARAM)&click);
    note=find_note(first);
    require(note && note_count()==before+1 && note->title && !wcscmp(note->title,L"Spesa è urgente"),
            "clicking a preview restores the archived post-it");
    toggle_note(note);
    SendMessageW(window,WM_NOTIFY,2,(LPARAM)&click);
    require(find_note(first)==note && !note->collapsed && note_count()==before+1,
            "opening an existing note expands it without creating duplicates");
    replace_text(note,L"Pane, caffè e latte"); require(save_note(note),"create formatting companion for deletion check");
    swprintf_s(path,PATH_CAP,L"%s\\%s.rtf",directory,first);
    lock=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    require(lock!=INVALID_HANDLE_VALUE && !delete_archived_note(first),"locked formatting blocks deletion of the whole note");
    swprintf_s(path,PATH_CAP,L"%s\\%s.txt",directory,first);
    require(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES && find_note(first)==note,
            "a refused deletion preserves the body and open post-it");
    CloseHandle(lock);
    swprintf_s(path,PATH_CAP,L"%s\\%s.rtf",directory,first);
    require(SetFileAttributesW(path,FILE_ATTRIBUTE_READONLY) && !delete_archived_note(first),
            "read-only formatting refuses a partially completed deletion");
    require(SetFileAttributesW(path,FILE_ATTRIBUTE_NORMAL),"restore test companion permissions");
    swprintf_s(path,PATH_CAP,L"%s\\%s.txt",directory,first);
    require(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES && find_note(first)==note,
            "a failed second disposition rolls back deletion of the TXT");
    TreeView_SelectItem(tree,year);
    require(ListView_GetItemCount(list)==2,"year filter includes both months");
    {
        RECT other;
        LOGFONTW font;
        require((HICON)GetClassLongPtrW(window,GCLP_HICON)==app_icon,"archive title bar uses the PicoSlip icon");
        require(GetObjectW((HFONT)SendMessageW(tree,WM_GETFONT,0,0),sizeof(font),&font) &&
                !wcscmp(font.lfFaceName,L"Consolas"),"archive uses a monospaced font");
        ListView_GetItemRect(list,0,&row,LVIR_ICON); ListView_GetItemRect(list,1,&other,LVIR_ICON);
        require(row.top==other.top && row.right<=other.left,"post-it previews form a native mosaic");
    }
    {
        Note view={0};
        view.window=window;
        screenshot(&view,L"build\\archive.bmp");
        ShowWindow(window,SW_SHOWNORMAL);
    }
    ListView_SetItemState(list,-1,LVIS_SELECTED,LVIS_SELECTED);
    if (interactive_tests) {
        INPUT clicks[2]={{0},{0}};
        POINT previous,point;
        UINT_PTR choose_timer=SetTimer(NULL,0,60,choose_archive_delete);
        UINT_PTR confirm_timer=SetTimer(NULL,0,60,confirm_archive_delete);
        require(choose_timer && confirm_timer,"schedule deletion of isolated test fixtures through the real menu");
        SetForegroundWindow(window); pump(80);
        ListView_GetItemRect(list,0,&row,LVIR_BOUNDS);
        point.x=row.left+40; point.y=(row.top+row.bottom)/2;
        ClientToScreen(list,&point); GetCursorPos(&previous); SetCursorPos(point.x,point.y);
        clicks[0].type=clicks[1].type=INPUT_MOUSE;
        clicks[0].mi.dwFlags=MOUSEEVENTF_RIGHTDOWN; clicks[1].mi.dwFlags=MOUSEEVENTF_RIGHTUP;
        require(SendInput(2,clicks,sizeof(INPUT))==2,"right-click the selected archive previews");
        pump(500); KillTimer(NULL,choose_timer); KillTimer(NULL,confirm_timer);
        SetCursorPos(previous.x,previous.y);
        require(archive_menu_seen,"native right-click exposes archive deletion");
    } else {
        NMLVKEYDOWN key={0};
        key.hdr.hwndFrom=list; key.hdr.code=LVN_KEYDOWN; key.wVKey=VK_DELETE;
        require(SetTimer(NULL,0,60,confirm_archive_delete)!=0,"confirm deletion of isolated test fixtures");
        SendMessageW(window,WM_NOTIFY,2,(LPARAM)&key);
    }
    require(!find_note(first) && GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES,"multi-delete removes an open note and its TXT");
    swprintf_s(path,PATH_CAP,L"%s\\%s.rtf",directory,first);
    require(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES,"multi-delete removes the RTF companion too");
    swprintf_s(path,PATH_CAP,L"%s\\%s.txt",directory,second);
    require(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES && setting(first,L"has_title",-1)==-1,
            "multi-delete removes the second note and obsolete metadata");
    swprintf_s(path,PATH_CAP,L"%s\\%s.txt",directory,third);
    require(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES,"multi-delete preserves unselected dates");
    require(delete_archived_note(third),"delete a single archived note without an RTF");
    wcscpy_s(id,80,notes->id);
    browse_notes();
    require(FindWindowW(L"PicoSlip.Archive",NULL)==window && !wcscmp(notes->id,id),"archive refresh reuses its window without changing notes");
    archive_close();
    {
        wchar_t empty[PATH_CAP];
        const ArchiveActions actions={save_all,open_archived_note,delete_archived_note};
        swprintf_s(empty,PATH_CAP,L"%s\\empty",directory);
        require(CreateDirectoryW(empty,NULL),"create an empty archive fixture");
        window=archive_show(controller,empty,state_path,&actions);
        require(window && ListView_GetItemCount(GetDlgItem(window,2))==0,"empty archives open normally");
        archive_close(); require(RemoveDirectoryW(empty),"remove empty archive fixture");
    }
    puts("PASS: archive tree, virtual Unicode previews, reopening, duplicate prevention, locked-file safety and multi-delete.");
}

static void empty_note_checks(void) {
    Note *note=new_note(), *other=new_note();
    wchar_t path[PATH_CAP],id[80];
    require(note && other && wcscmp(note->id,other->id),"unsaved blank notes still receive distinct identifiers");
    require(save_note(note) && !note->persisted && GetFileAttributesW(note->path)==INVALID_FILE_ATTRIBUTES &&
            setting(note->id,L"open",-1)==-1,"blank notes create neither a text file nor note metadata");
    replace_text(note,L" \t\r\n");
    require(save_note(note) && GetFileAttributesW(note->path)==INVALID_FILE_ATTRIBUTES,
            "whitespace-only notes are not archived");
    require(set_title(note,L"Title only") && save_note(note) && note->persisted,"a title alone counts as content");
    expect_file(note->path,"#Title only\r\n \t\r\n");
    replace_text(note,L""); require(set_title(note,L"") && save_note(note),"clear all content from a saved note");
    require(!note->persisted && IsWindow(note->window) && GetFileAttributesW(note->path)==INVALID_FILE_ATTRIBUTES,
            "clearing a saved note removes the archive file but keeps the blank post-it usable");
    swprintf_s(path,PATH_CAP,L"%s\\%s.rtf",directory,note->id);
    require(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES && setting(note->id,L"open",-1)==-1,
            "clearing also removes formatting and metadata");
    replace_text(note,L"Reusable"); require(save_note(note) && note->persisted,"a cleared post-it can be saved again");
    wcscpy_s(id,80,other->id); wcscpy_s(path,PATH_CAP,other->path);
    SendMessageW(other->window,WM_CLOSE,0,0);
    require(!find_note(id) && GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES,"closing a blank note leaves no archive entry");
    require(delete_archived_note(note->id),"remove the isolated empty-note test fixture");
    puts("PASS: blank and whitespace notes stay unsaved, title-only notes persist, cleared notes remove TXT/RTF/metadata.");
}

int main(int argc, char **argv) {
    wchar_t temporary[PATH_CAP], root[PATH_CAP], id[80], path[PATH_CAP], text[256], pattern[PATH_CAP];
    const wchar_t *sample = L"Appunti di oggi\r\nCaffè, perché, € e 😀\r\nSeconda riga.";
    const char *expected = "Appunti di oggi\r\nCaffè, perché, € e 😀\r\nSeconda riga.";
    Note *first, *second;
    RECT rect;
    HANDLE lock, find;
    WIN32_FIND_DATAW entry;
    int color, saved_width, saved_height, i;
    wchar_t overflow[5000];
    HWND old_window;
    if (argc == 2 && !strcmp(argv[1], "--stop-app")) {
        HWND running = FindWindowW(APP_CLASS, NULL);
        DWORD process_id;
        DWORD_PTR reply;
        HANDLE process;
        if (!running) return 0;
        GetWindowThreadProcessId(running, &process_id);
        process = OpenProcess(SYNCHRONIZE, FALSE, process_id);
        require(process != NULL, "observe running application");
        require(SendMessageTimeoutW(running, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 5000, &reply) != 0,
                "save and gracefully close running application");
        require(WaitForSingleObject(process, 5000) == WAIT_OBJECT_0, "running application saved and exited");
        CloseHandle(process);
        return 0;
    }
    instance = GetModuleHandleW(NULL); testing = TRUE;
    interactive_tests=argc==2 && !strcmp(argv[1],"--interactive");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    require(GetTempPathW(PATH_CAP, temporary) != 0, "temporary directory");
    swprintf_s(root, PATH_CAP, L"%sPicoSlip-test-è-%lu-%llu", temporary, GetCurrentProcessId(), GetTickCount64());
    require(make_paths(root), "create isolated Unicode test directory");
    app_icon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(IDI_APP),IMAGE_ICON,32,32,0);
    require(app_icon != NULL && register_classes(), "register native C window classes");
    controller = CreateWindowExW(WS_EX_TOOLWINDOW, APP_CLASS, L"PicoSlip test", WS_OVERLAPPED,
                                 0, 0, 0, 0, NULL, NULL, instance, NULL);
    require(controller != NULL, "create hotkey controller");
    if (argc==2 && !strcmp(argv[1],"--archive-only")) {
        require(new_note()!=NULL,"create archive test owner");
        archive_checks();
        goto cleanup;
    }
    first = new_note();
    require(first != NULL, "create first note");
    require(RegisterHotKey(controller, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT,
                           (UINT)(VkKeyScanW(L'+') & 0xff)) ||
            (GetLastError() == ERROR_HOTKEY_ALREADY_REGISTERED &&
             RegisterHotKey(controller, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F24)),
            "register creation hotkey without interrupting a running app");
    SendMessageW(controller, WM_HOTKEY, 1, 0);
    second = notes;
    require(note_count() == 2 && second != first && second->color != first->color,
            "hotkey creates independent note with next color");
    require(wcscmp(first->path, second->path) != 0, "unique TXT files for rapid note creation");
    replace_text(first, sample);
    require(first->dirty, "typing schedules autosave");
    pump(650);
    require(!first->dirty && !first->save_failed, "real autosave timer completes");
    require(!(GetWindowLongPtrW(first->edit, GWL_STYLE) & WS_VSCROLL), "short notes have no redundant scrollbar");
    expect_file(first->path, expected);
    require(GetFileAttributesW(first->path) != INVALID_FILE_ATTRIBUTES, "TXT saved in notes folder");
    for (i = 0; i < 4999; ++i) overflow[i] = L'W';
    overflow[4999] = 0;
    replace_text(first, overflow);
    require(text_fits(first) && GetWindowTextLengthW(first->edit) < 4999,
            "oversized pasted text cannot exceed visible capacity");
    require(!(GetWindowLongPtrW(first->edit, GWL_STYLE) & (WS_VSCROLL | WS_HSCROLL | ES_AUTOVSCROLL | ES_AUTOHSCROLL)),
            "no scrollbar or hidden scrolling content");
    replace_text(first, sample);
    require(save_note(first), "save after overflow check");
    puts("PASS: hotkey dispatch, different colors, unique files, timed UTF-8 autosave with emoji.");

    lock = CreateFileW(first->path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    require(lock != INVALID_HANDLE_VALUE, "lock target against replacement");
    replace_text(first, L"Testo nuovo");
    require(!save_note(first) && first->dirty && first->save_failed, "failed save keeps unsaved text");
    expect_file(first->path, expected);
    old_window = first->window;
    SendMessageW(old_window, WM_CLOSE, 0, 0);
    require(IsWindow(old_window), "failed save prevents closing and losing text");
    CloseHandle(lock);
    pump(1150);
    expect_file(first->path, "Testo nuovo");
    require(!first->dirty && !first->save_failed, "failed autosave retries after file becomes writable");
    replace_text(first, sample);
    require(save_note(first), "restore sample text");
    puts("PASS: failed atomic write preserves previous file, blocks close and retries successfully.");

    SetWindowPos(first->window, NULL, 110, 130, 340, 270, SWP_NOZORDER | SWP_NOACTIVATE);
    GetWindowRect(first->window, &rect);
    require(rect.right - rect.left == 340 && rect.bottom - rect.top == 270,
            "post-it accepts arbitrary dimensions");
    SendMessageW(first->window, WM_EXITSIZEMOVE, 0, 0);
    saved_width = note_width(first); saved_height = expanded_height(first); color = first->color;
    screenshot(first, L"build\\expanded.bmp");
    click_button(first, 1);
    GetWindowRect(first->window, &rect);
    require(first->collapsed && rect.bottom - rect.top == bar_height(first), "collapse leaves only title bar");
    require(!(GetWindowLongPtrW(first->edit, GWL_STYLE) & WS_VISIBLE), "collapsed editor is hidden");
    screenshot(first, L"build\\collapsed.bmp");
    click_button(first, 1);
    GetWindowRect(first->window, &rect);
    require(!first->collapsed && rect.bottom - rect.top == saved_height, "expand restores original height");
    GetWindowTextW(first->edit, text, 256);
    require(!wcscmp(text, sample), "collapse and expand retain complete text");
    click_button(first, 1);
    wcscpy_s(id, 80, first->id); wcscpy_s(path, PATH_CAP, first->path);

    replace_text(second, L"Chiusa, ma conservata.");
    wcscpy_s(text, 256, second->id);
    ShowWindow(second->window, SW_SHOWNORMAL);
    SetFocus(second->edit);
    click_button(second, 2);
    require(note_count() == 1 && setting(text, L"open", TRUE) == FALSE,
            "close saves TXT and archives even a focused note");
    puts("PASS: title-bar buttons, collapsed height, restored size, preserved text and close semantics.");

    require(save_all(), "save before application exit");
    while (notes) destroy_note(notes);
    require(restore_notes(), "restore notes after restart");
    first = notes;
    require(note_count() == 1 && !wcscmp(first->id, id), "restart restores only notes left open");
    GetWindowTextW(first->edit, text, 256);
    require(first->collapsed && first->color == color && expanded_height(first) == saved_height && !wcscmp(text, sample),
            "restart restores collapsed state, color, height and Unicode text");
    GetWindowRect(first->window, &rect);
    require(rect.left == 110 && rect.top == 130 && rect.right - rect.left == saved_width,
            "restart restores window position and custom width");
    expect_file(path, expected);
    puts("PASS: restart restores layout and active notes, preserves closed TXT archives.");
    title_checks();
    serialization_checks();
    options_checks();
    if (interactive_tests) keyboard_checks();
    editing_checks();
    resizing_checks();
    archive_checks();
    empty_note_checks();
    efficiency_checks(!(argc == 2 && !strcmp(argv[1], "--measure")));

cleanup:
    archive_close();
    while (notes) destroy_note(notes);
    DestroyWindow(controller);
    if (app_icon) DestroyIcon(app_icon);
    swprintf_s(pattern, PATH_CAP, L"%s\\*", root);
    find = FindFirstFileW(pattern, &entry);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                swprintf_s(path, PATH_CAP, L"%s\\%s", root, entry.cFileName);
                require(DeleteFileW(path), "remove isolated test output");
            }
        } while (FindNextFileW(find, &entry));
        FindClose(find);
    }
    require(RemoveDirectoryW(root), "remove isolated test directory");
    puts("All integration checks passed. UI snapshots: build/expanded.bmp and build/collapsed.bmp");
    return 0;
}
