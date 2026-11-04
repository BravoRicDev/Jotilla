/* End-to-end Win32 / filesystem checks; C17, no test framework. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdlib.h>

static unsigned metadata_writes, metric_queries, graphic_creations, heap_requests;
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
    RECT rect;
    BITMAPINFO info = {0};
    BITMAPFILEHEADER header = {0};
    HDC screen, memory;
    HBITMAP bitmap;
    HGDIOBJ old;
    void *pixels;
    DWORD written, size;
    HANDLE file;
    ShowWindow(note->window, SW_SHOWNOACTIVATE);
    RedrawWindow(note->window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    pump(150);
    GetClientRect(note->window, &rect);
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = rect.right; info.bmiHeader.biHeight = -rect.bottom;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    screen = GetDC(note->window);
    memory = CreateCompatibleDC(screen);
    bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
    require(bitmap && memory, "allocate UI snapshot");
    old = SelectObject(memory, bitmap);
    require(BitBlt(memory, 0, 0, rect.right, rect.bottom, screen, 0, 0, SRCCOPY), "capture UI snapshot");
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
    SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(note->window, screen);
    ShowWindow(note->window, SW_HIDE);
}

static void press_enter(Note *note, BOOL shift) {
    BYTE previous[256], state[256];
    require(GetKeyboardState(previous), "read test thread keyboard state");
    memcpy(state, previous, sizeof(state));
    state[VK_SHIFT] = state[VK_LSHIFT] = state[VK_RSHIFT] = shift ? 0x80 : 0;
    require(SetKeyboardState(state), "set test thread Shift state");
    SendMessageW(note->edit, WM_CHAR, L'\r', 1);
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
    require(menu && GetMenuItemCount(menu)==7 && GetMenuItemID(menu,0)==CMD_ARCHIVE &&
            GetMenuItemID(menu,3)==CMD_TITLE && GetMenuItemCount(GetSubMenu(menu,2))==5 &&
            GetMenuItemCount(GetSubMenu(menu,4))==4 && GetMenuItemCount(GetSubMenu(menu,6))==3,"context menu exposes all requested choices");
    DestroyMenu(menu);
    GetCursorPos(&previous); ShowWindow(note->window,SW_SHOWNOACTIVATE);
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
    dialog_input=L"";
    require(SetTimer(NULL,0,40,fill_title_dialog)!=0,"schedule title removal");
    note_command(note,CMD_TITLE);
    require(note->title==NULL && !wcscmp(note->accepted_text,body),"dialog removes title while preserving body");
    note_command(note,CMD_PALETTE+1);
    white=new_note(); black=new_note();
    require(white && black && white->palette==1 && black->palette==1 &&
            note_color(white)==RGB(255,255,255) && ink_color(white)==RGB(0,0,0) &&
            note_color(black)==RGB(28,28,28) && ink_color(black)==RGB(255,255,255),"monochrome generation preserves contrast");
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

int main(int argc, char **argv) {
    wchar_t temporary[PATH_CAP], root[PATH_CAP], id[80], path[PATH_CAP], text[256], pattern[PATH_CAP];
    const wchar_t *sample = L"Appunti di oggi\r\nCaffè, perché, € e 😀\r\nSeconda riga.";
    const char *expected = "Appunti di oggi\r\nCaffè, perché, € e 😀\r\nSeconda riga.";
    Note *first, *second;
    RECT rect;
    HANDLE lock, find;
    WIN32_FIND_DATAW entry;
    int color, saved_height, i;
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
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    require(GetTempPathW(PATH_CAP, temporary) != 0, "temporary directory");
    swprintf_s(root, PATH_CAP, L"%sPicoSlip-test-è-%lu-%llu", temporary, GetCurrentProcessId(), GetTickCount64());
    require(make_paths(root), "create isolated Unicode test directory");
    app_icon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(IDI_APP),IMAGE_ICON,32,32,0);
    require(app_icon != NULL && register_classes(), "register native C window classes");
    controller = CreateWindowExW(WS_EX_TOOLWINDOW, APP_CLASS, L"PicoSlip test", WS_OVERLAPPED,
                                 0, 0, 0, 0, NULL, NULL, instance, NULL);
    require(controller != NULL, "create hotkey controller");
    first = new_note();
    require(first != NULL, "create first note");
    require(RegisterHotKey(controller, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT,
                           (UINT)(VkKeyScanW(L'+') & 0xff)), "register Ctrl+Alt++ with Windows");
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
    require(rect.right - rect.left == scaled(first, NOTE_WIDTH) && rect.bottom - rect.top == scaled(first, NOTE_HEIGHT),
            "post-it dimensions remain fixed even when resizing is requested");
    SendMessageW(first->window, WM_EXITSIZEMOVE, 0, 0);
    saved_height = scaled(first, NOTE_HEIGHT); color = first->color;
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
    require(first->collapsed && first->color == color && scaled(first, NOTE_HEIGHT) == saved_height && !wcscmp(text, sample),
            "restart restores collapsed state, color, height and Unicode text");
    GetWindowRect(first->window, &rect);
    require(rect.left == 110 && rect.top == 130 && rect.right - rect.left == scaled(first, NOTE_WIDTH),
            "restart restores window position and fixed width");
    expect_file(path, expected);
    puts("PASS: restart restores layout and active notes, preserves closed TXT archives.");
    title_checks();
    serialization_checks();
    options_checks();
    efficiency_checks(!(argc == 2 && !strcmp(argv[1], "--measure")));

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
