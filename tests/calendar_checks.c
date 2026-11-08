/* Included by tests.c. Exercise the production windows and real archive files. */
static int immediate_day_count;
static void click_calendar_day(HWND calendar, unsigned date) {
    RECT rect;
    SetForegroundWindow(GetAncestor(calendar,GA_ROOT));
    SetFocus(calendar);
    require(calendar_day_rect(calendar,date,&rect),"locate a real calendar day");
    SendMessageW(calendar,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM((rect.left+rect.right)/2,(rect.top+rect.bottom)/2));
    SendMessageW(calendar,WM_LBUTTONUP,0,MAKELPARAM((rect.left+rect.right)/2,(rect.top+rect.bottom)/2));
    {
        HWND list=thread_window(CALENDAR_LIST_CLASS,NULL);
        immediate_day_count=list?(int)SendDlgItemMessageW(list,1,LB_GETCOUNT,0,0):0;
    }
    pump(40);
}

static void calendar_checks(void) {
    Note *first=new_note(), *second=new_note();
    HWND browser,calendar,picker,list;
    wchar_t first_id[80],second_id[80],label[256];
    HANDLE lock;
    int count,year;
    require(date_weekday(16010101)==0 && date_weekday(20000101)==5 && date_weekday(20261108)==6,
        "civil weekdays use Monday as the first column");
    require(date_valid(20000229) && !date_valid(21000229) && !date_valid(20260229) && !date_valid(20260431) &&
        !date_valid(0) && !date_valid(100000101),"Gregorian leap centuries, short months and bounded years");
    require(date_parse(L"20280229")==20280229 && !date_parse(L"20280229x") && !date_parse(L"20") &&
        date_ordinal(20241231)==365 && date_ordinal(20231231)==364,"strict stored dates and leap-year ordinals");
    for(year=DATE_MIN_YEAR;year<DATE_MAX_YEAR;++year) {
        unsigned end=year*10000u+1231, next=(year+1)*10000u+101;
        require(date_shift(end,1)==next && date_shift(next,-1)==end,"every supported year boundary is reversible");
    }
    {
        unsigned date=20000101;
        int ordinal=0, weekday=5, days=0;
        while (date<24000101) {
            require(date_ordinal(date)==ordinal && date_weekday(date)==weekday,
                "every day of a complete Gregorian cycle has the expected ordinal and weekday");
            ++days; weekday=(weekday+1)%7; date=date_shift(date,1);
            ordinal=date%10000==101 ? 0 : ordinal+1;
        }
        require(days==146097,"Gregorian cycle covers 146097 consecutive days");
    }
    require(date_shift(16010101,-7)==16010101 && date_shift(99991231,7)==99991231,"navigation clamps at supported endpoints");
    require(first && second && !first->calendar_date,"older and new notes start unassigned");
    replace_text(first,L"Calendar first body"); require(set_title(first,L"Leap meeting"),"name the first calendar note");
    replace_text(second,L"First line fallback\r\nOther content");
    wcscpy_s(first_id,80,first->id); wcscpy_s(second_id,80,second->id);
    require(set_note_date(first,20280229) && set_note_date(second,20280229),"two notes may share one calendar day");
    require(!set_note_date(first,20270229) && first->calendar_date==20280229,"invalid assignment preserves the old day");
    lock=CreateFileW(state_path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    require(lock!=INVALID_HANDLE_VALUE && !set_note_date(first,20300101) && first->calendar_date==20280229,
        "failed metadata writes do not report an unsaved calendar date");
    CloseHandle(lock);
    destroy_note(first); first=open_note(first_id,0,TRUE);
    require(first && first->calendar_date==20280229 && !wcscmp(first->id,first_id),"assigned day survives reopening without changing identity");
    require(set_tags(first,L"calendar;test") && save_note(first),"Unicode-capable tag writes preserve the scheduled day");
    browser=browse_notes(); require(browser!=NULL,"open archive for calendar integration");
    require(!GetDlgItem(browser,11),"ordinary note browsing creates no calendar windows");
    SendMessageW(browser,WM_COMMAND,10,0); calendar=GetDlgItem(browser,11);
    require(IsWindowVisible(calendar) && !IsWindowVisible(GetDlgItem(browser,2)),"calendar replaces the note browser in the same window");
    require(!GetDlgItem(calendar,1) && !GetDlgItem(calendar,4) && !GetDlgItem(calendar,5),"annual view has no year step buttons");
    SetWindowTextW(GetDlgItem(calendar,3),L"2048"); SendMessageW(GetDlgItem(calendar,3),WM_KEYDOWN,VK_RETURN,0);
    require(calendar_year(calendar)==2048,"annual year entry reaches distant future dates");
    SetWindowTextW(GetDlgItem(calendar,3),L"1904"); SendMessageW(GetDlgItem(calendar,3),WM_KEYDOWN,VK_RETURN,0);
    require(calendar_year(calendar)==1904,"annual year entry reaches earlier centuries");
    SetWindowTextW(GetDlgItem(calendar,3),L"2028"); SendMessageW(GetDlgItem(calendar,3),WM_KEYDOWN,VK_RETURN,0);
    require(calendar_year(calendar)==2028,"annual view accepts a directly entered year");
    count=note_count(); click_calendar_day(calendar,20280228);
    require(note_count()==count && !thread_window(CALENDAR_LIST_CLASS,NULL),"an empty day opens nothing");
    click_calendar_day(calendar,20280229); list=thread_window(CALENDAR_LIST_CLASS,NULL);
    require(list && SendDlgItemMessageW(list,1,LB_GETCOUNT,0,0)==2,"multiple notes open a compact native list");
    SendDlgItemMessageW(list,1,LB_GETTEXT,0,(LPARAM)label);
    require(!wcscmp(label,L"First line fallback") || !wcscmp(label,L"Leap meeting"),"day rows show titles or the first nonempty line");
    SendMessageW(GetDlgItem(list,1),WM_KEYDOWN,VK_RETURN,0); pump(40);
    require(!IsWindow(list) && note_count()==count,"opening an already open note never duplicates it");
    require(set_note_date(second,20300101),"moving a note updates both day memberships");
    SendMessageW(first->window,WM_CLOSE,0,0); pump(40); count=note_count();
    click_calendar_day(calendar,20280229); first=find_note(first_id);
    require(first && note_count()==count+1 && !thread_window(CALENDAR_LIST_CLASS,NULL),"one archived note opens directly from its assigned day");
    show_note_calendar(first); picker=thread_window(CALENDAR_CLASS,L"Calendar");
    require(picker && picker!=calendar && calendar_year(picker)==2028,"note picker starts on the assigned month and year");
    require(!GetDlgItem(picker,1) && !GetDlgItem(picker,4) && !GetDlgItem(picker,5),"note picker has no year step buttons");
    SetWindowTextW(GetDlgItem(picker,3),L"1904"); SendMessageW(GetDlgItem(picker,3),WM_KEYDOWN,VK_RETURN,0);
    require(calendar_year(picker)==1904,"note picker accepts a distant past year");
    SetWindowTextW(GetDlgItem(picker,3),L"2032"); SendMessageW(GetDlgItem(picker,3),WM_KEYDOWN,VK_RETURN,0);
    click_calendar_day(picker,20320229);
    require(!IsWindow(picker) && first->calendar_date==20320229,"picker selection commits a day in another leap year");
    show_note_calendar(first); picker=thread_window(CALENDAR_CLASS,L"Calendar");
    SendMessageW(picker,WM_COMMAND,9,0); pump(40);
    require(!first->calendar_date && setting(first_id,L"calendar_date",0)==0,"remove date clears only the association");
    require(set_note_date(first,20280229) && set_note_date(second,20280229),"restore shared day for filtering and deletion");
    calendar_set_year(calendar,2028);
    SetWindowTextW(GetDlgItem(browser,3),L"Calendar first"); pump(350);
    click_calendar_day(calendar,20280229);
    require(!thread_window(CALENDAR_LIST_CLASS,NULL),"calendar search restricts day membership to matching notes");
    SetWindowTextW(GetDlgItem(browser,3),L""); pump(200);
    {
        wchar_t id[80]; int i, selected;
        for(i=0;i<10;++i) {
            swprintf_s(id,80,L"note-1901-06-01_00-00-00-000-%03d",50+i);
            archive_fixture(id,"#Keyboard entry\r\nA note for scrolling checks.");
            require(write_setting(id,L"calendar_date",20280229),"assign a keyboard list fixture");
        }
        SendMessageW(browser,WM_KEYDOWN,VK_F5,0); click_calendar_day(calendar,20280229);
        list=thread_window(CALENDAR_LIST_CLASS,NULL);
        if(!list || SendDlgItemMessageW(list,1,LB_GETCOUNT,0,0)!=12) {
            GetWindowTextW(browser,label,256);
            fwprintf(stderr,L"Day list diagnostic: immediate=%d, after=%lld, year=%d, title=%s, archive=%s\n",
                immediate_day_count,list?(long long)SendDlgItemMessageW(list,1,LB_GETCOUNT,0,0):0,
                calendar_year(calendar),label,directory);
        }
        require(list && SendDlgItemMessageW(list,1,LB_GETCOUNT,0,0)==12,"large days use a scrollable compact list");
        SendDlgItemMessageW(list,1,WM_KEYDOWN,VK_DOWN,0);
        require(SendDlgItemMessageW(list,1,LB_GETCURSEL,0,0)==1,"Down selects the next title without opening it");
        SendDlgItemMessageW(list,1,WM_KEYDOWN,VK_UP,0);
        require(SendDlgItemMessageW(list,1,LB_GETCURSEL,0,0)==0,"Up returns to the preceding title");
        SendDlgItemMessageW(list,1,WM_KEYDOWN,VK_NEXT,0);
        selected=(int)SendDlgItemMessageW(list,1,LB_GETCURSEL,0,0);
        require(selected>1 && selected<12,"Page Down navigates a page of titles");
        SendDlgItemMessageW(list,1,WM_KEYDOWN,VK_HOME,0);
        require(SendDlgItemMessageW(list,1,LB_GETCURSEL,0,0)==0,"Home selects the first title");
        SendDlgItemMessageW(list,1,WM_KEYDOWN,VK_END,0);
        require(SendDlgItemMessageW(list,1,LB_GETCURSEL,0,0)==11 && SendDlgItemMessageW(list,1,LB_GETTOPINDEX,0,0)>0,
            "End selects and scrolls the last title into view");
        SendDlgItemMessageW(list,1,WM_KEYDOWN,VK_RETURN,0);
        require(!IsWindow(list) && find_note(L"note-1901-06-01_00-00-00-000-050"),"Enter opens exactly the keyboard-selected note");
        click_calendar_day(calendar,20280229); list=thread_window(CALENDAR_LIST_CLASS,NULL);
        SendDlgItemMessageW(list,1,WM_KEYDOWN,VK_ESCAPE,0);
        require(!IsWindow(list) && GetFocus()==calendar,"Escape closes the day list and returns keyboard focus to the calendar");
        for(i=0;i<10;++i) {
            swprintf_s(id,80,L"note-1901-06-01_00-00-00-000-%03d",50+i);
            require(delete_archived_note(id),"remove keyboard list fixture");
        }
        pump(80);
    }
    click_calendar_day(calendar,20280229); list=thread_window(CALENDAR_LIST_CLASS,NULL);
    require(list!=NULL,"clearing search restores the complete day");
    require(delete_archived_note(second_id),"delete a scheduled note while the compact list is open"); pump(60);
    require(!IsWindow(list),"archive changes close stale day rows before rebuilding references");
    shortcut(first,'E',FALSE);
    require(IsWindowVisible(GetDlgItem(browser,2)) && !IsWindowVisible(calendar),"Ctrl+E returns directly to the note explorer");
    archive_close();
    require(delete_archived_note(first_id),"remove isolated calendar fixture");
    puts("PASS: civil dates, durable assignment, direct year entry, keyboard scrolling and opening, filtering, reopening and deletion.");
}

static void sizing_shortcut(Note *note, UINT key, wchar_t character) {
    BYTE previous[256],state[256]={0};
    GetKeyboardState(previous); state[VK_CONTROL]=state[VK_LCONTROL]=0x80; SetKeyboardState(state);
    SendMessageW(note->edit,WM_KEYDOWN,key,1);
    if(character) SendMessageW(note->edit,WM_CHAR,character,1);
    SetKeyboardState(previous);
}

static void shortcut_checks(void) {
    Note *note=new_note(); int width; LONG size;
    PARAFORMAT2 paragraph={0};
    require(note!=NULL,"create shortcut fixture"); replace_text(note,L"Alpha beta");
    SendMessageW(note->edit,EM_EMPTYUNDOBUFFER,0,0);
    SendMessageW(note->edit,EM_REPLACESEL,TRUE,(LPARAM)L"!");
    shortcut(note,'Z',FALSE); require(!wcscmp(note->accepted_text,L"Alpha beta"),"Ctrl+Z undoes text exactly once");
    shortcut(note,'Y',FALSE); require(!wcscmp(note->accepted_text,L"Alpha beta!"),"Ctrl+Y restores undone text");
    width=note->width; sizing_shortcut(note,VK_OEM_PLUS,L'+');
    require(note->width>width && !wcscmp(note->accepted_text,L"Alpha beta!"),"Ctrl+plus grows the note without inserting a character");
    width=note->width; sizing_shortcut(note,VK_SUBTRACT,L'-'); require(note->width<width,"numpad Ctrl+minus shrinks the note");
    SendMessageW(note->edit,EM_SETSEL,0,5); width=note->width; size=format_range(note,0,5).yHeight;
    sizing_shortcut(note,VK_ADD,L'+');
    require(note->width==width && format_range(note,0,5).yHeight>size && format_range(note,6,10).yHeight==size,
        "selected text alone grows with Ctrl+plus");
    shortcut(note,'Z',FALSE); require(format_range(note,0,5).yHeight==size,"Ctrl+Z restores the selected font size");
    sizing_shortcut(note,VK_OEM_MINUS,31); require(format_range(note,0,5).yHeight<size && note->width==width,
        "Ctrl+minus shrinks only the selection and consumes its control character");
    shortcut(note,'E',FALSE); paragraph.cbSize=sizeof(paragraph);
    SendMessageW(note->edit,EM_GETPARAFORMAT,0,(LPARAM)&paragraph);
    require(paragraph.wAlignment==PFA_LEFT && thread_window(L"Jotila.Archive",NULL),"Ctrl+E opens the explorer without centering paragraphs");
    archive_close(); require(delete_archived_note(note->id),"remove shortcut fixture");
    puts("PASS: Ctrl+E, Ctrl+Z/Y and keyboard note/selection sizing with no duplicate input.");
}
