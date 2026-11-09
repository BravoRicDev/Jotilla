/* Real windows and storage, with a sampled cursor instead of physical input. */
static BOOL CALLBACK cursor_monitor_checks(HMONITOR display, HDC dc, LPRECT bounds, LPARAM data) {
    MONITORINFO monitor={sizeof(monitor)};
    RECT rect;
    int key;
    (void)dc; (void)bounds;
    require(GetMonitorInfoW(display,&monitor),"read monitor work area");
    ++*(int *)data;
    for (key=1;key<=3;++key) {
        Note *previous=notes;
        test_cursor.x=monitor.rcWork.left+40+key*10;
        test_cursor.y=monitor.rcWork.top+50+key*10;
        SendMessageW(controller,WM_HOTKEY,(WPARAM)key,0);
        require(notes && notes!=previous,"each creation hotkey creates a note");
        GetWindowRect(notes->window,&rect);
        require(rect.left==test_cursor.x && rect.top==test_cursor.y,
                "hotkey uses the exact cursor screen coordinates on each monitor");
        destroy_note(notes);
    }
    test_cursor.x=monitor.rcWork.right-1; test_cursor.y=monitor.rcWork.bottom-1;
    SendMessageW(controller,WM_HOTKEY,1,0);
    require(notes!=NULL,"create a note at the screen edge");
    GetWindowRect(notes->window,&rect);
    require(rect.left==test_cursor.x && rect.top==test_cursor.y,"even an edge hotkey retains the exact pointer origin");
    destroy_note(notes);
    return TRUE;
}

static void session_checks(void) {
    Note *first, *second, *closed;
    wchar_t first_id[80], second_id[80], closed_id[80];
    RECT first_rect, second_rect, rect, default_rect;
    MONITORINFO monitor={sizeof(monitor)};
    HANDLE lock;
    unsigned writes;
    int monitors=0, sequence, width, height;
    fake_cursor=TRUE;
    require(EnumDisplayMonitors(NULL,NULL,cursor_monitor_checks,(LPARAM)&monitors) && monitors>0,"exercise every attached monitor");
    first=new_note(); require(first!=NULL,"create default-position fixture");
    GetWindowRect(first->window,&default_rect);
    sequence=(next_color+COLOR_COUNT-1)%COLOR_COUNT;
    destroy_note(first); next_color=sequence;
    SendMessageW(controller,NEW_NOTE_MESSAGE,0,0);
    require(notes!=NULL,"create note through the tray command");
    GetWindowRect(notes->window,&rect);
    require(EqualRect(&rect,&default_rect),"tray command retains default placement regardless of cursor position");
    destroy_note(notes); fake_cursor=FALSE;

    first=new_note(); second=new_note(); closed=new_note();
    require(first && second && closed,"create session fixtures");
    wcscpy_s(first_id,80,first->id); wcscpy_s(second_id,80,second->id); wcscpy_s(closed_id,80,closed->id);
    replace_text(first,L"Folded near the lower edge"); replace_text(second,L"Pending session text");
    replace_text(closed,L"Keep archived"); SendMessageW(closed->window,WM_CLOSE,0,0);
    require(!find_note(closed_id) && !setting(closed_id,L"open",TRUE),"explicitly closed notes stay archived");
    require(GetMonitorInfoW(MonitorFromWindow(first->window,MONITOR_DEFAULTTONEAREST),&monitor),"read session monitor");
    toggle_note(first);
    SetWindowPos(first->window,NULL,monitor.rcWork.left+35,monitor.rcWork.bottom-bar_height(first)-3,0,0,
                 SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(second->window,NULL,monitor.rcWork.left+80,monitor.rcWork.top+90,0,0,
                 SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    GetWindowRect(first->window,&first_rect); GetWindowRect(second->window,&second_rect);
    width=first->width; height=first->height;
    require(SendMessageW(first->window,WM_QUERYENDSESSION,0,ENDSESSION_LOGOFF) &&
            SendMessageW(controller,WM_QUERYENDSESSION,0,ENDSESSION_LOGOFF),"logoff query saves positions and pending content");
    lock=CreateFileW(second->path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    require(lock!=INVALID_HANDLE_VALUE,"lock a session note against replacement");
    replace_text(second,L"Retry after cancellation");
    require(!SendMessageW(controller,WM_QUERYENDSESSION,0,0),"save failure rejects session termination");
    SendMessageW(controller,WM_ENDSESSION,FALSE,0);
    require(IsWindow(first->window) && IsWindow(second->window) && second->dirty,
            "cancelled shutdown retains live windows and unsaved edits");
    CloseHandle(lock);
    /* Deliver the final notification to a note before the hidden controller. */
    SendMessageW(first->window,WM_ENDSESSION,TRUE,ENDSESSION_LOGOFF);
    require(!second->dirty && !second->save_failed,"first end-session recipient saves every note");
    expect_file(second->path,"Retry after cancellation");
    writes=metadata_writes;
    SendMessageW(second->window,WM_ENDSESSION,TRUE,ENDSESSION_LOGOFF);
    SendMessageW(controller,WM_ENDSESSION,TRUE,ENDSESSION_LOGOFF);
    require(metadata_writes==writes,"multiple top-level shutdown callbacks do not repeat metadata writes or flushes");
    while (notes) destroy_note(notes);
    startup_restore=TRUE;
    require(restore_notes(),"restore the saved session");
    startup_restore=FALSE;
    first=find_note(first_id); second=find_note(second_id);
    require(first && second && !find_note(closed_id) && note_count()==2,"restart restores only the two open notes");
    GetWindowRect(first->window,&rect);
    require(EqualRect(&rect,&first_rect) && first->collapsed && first->width==width && first->height==height,
            "folded note near the lower edge restores its exact position and expanded dimensions");
    GetWindowRect(second->window,&rect);
    require(EqualRect(&rect,&second_rect),"expanded note restores its exact position and dimensions");

    destroy_note(first);
    require(write_setting(first_id,L"x",INT_MIN) && write_setting(first_id,L"y",INT_MAX),"simulate a removed monitor or corrupt coordinates");
    first=open_note(first_id,0,TRUE); require(first!=NULL,"restore off-screen note safely");
    GetWindowRect(first->window,&rect);
    require(GetMonitorInfoW(MonitorFromWindow(first->window,MONITOR_DEFAULTTONEAREST),&monitor) &&
            rect.left>=monitor.rcWork.left && rect.top>=monitor.rcWork.top &&
            rect.right<=monitor.rcWork.right && rect.top+bar_height(first)<=monitor.rcWork.bottom,
            "unavailable monitor positions leave the title bar reachable");
    SendMessageW(controller,WM_ENDSESSION,FALSE,0);
    replace_text(second,L"A later session");
    SendMessageW(controller,WM_ENDSESSION,TRUE,0);
    expect_file(second->path,"A later session");
    SendMessageW(controller,WM_ENDSESSION,FALSE,0);
    require(delete_archived_note(first_id) && delete_archived_note(second_id) && delete_archived_note(closed_id),"remove private session fixtures");
    puts("PASS: cursor/tray placement, folded and expanded restart positions, monitor recovery, shutdown cancellation and single-pass session saving.");
}
