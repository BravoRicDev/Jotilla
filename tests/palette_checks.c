/* Included by tests.c: exercise the real editor, popup, persistence and GDI
   lifetime in its isolated archive. No physical mouse/keyboard injection. */
#include <math.h>

static double color_luminance(COLORREF color) {
    double channel[3]={GetRValue(color)/255.0,GetGValue(color)/255.0,GetBValue(color)/255.0};
    int i;
    for (i=0;i<3;++i) channel[i]=channel[i]<=.04045 ? channel[i]/12.92 : pow((channel[i]+.055)/1.055,2.4);
    return .2126*channel[0]+.7152*channel[1]+.0722*channel[2];
}

static void palette_preview(HWND list,COLORREF paper,BOOL highlighted) {
    RECT client,card;
    int dpi=(int)GetDpiForWindow(list),x,y,black=0,gold=0;
    HDC dc=GetDC(list),memory=CreateCompatibleDC(dc);
    HBITMAP bitmap;
    HGDIOBJ old;
    GetClientRect(list,&client);
    bitmap=CreateCompatibleBitmap(dc,client.right,client.bottom);
    require(memory && bitmap,"allocate native archive color rendering check");
    old=SelectObject(memory,bitmap);
    require(ListView_GetItemRect(list,0,&card,LVIR_ICON) && PrintWindow(list,memory,PW_CLIENTONLY),
            "render the real archive tile");
    require(GetPixel(memory,(card.left+card.right)/2,card.bottom-MulDiv(25,dpi,96))==paper,
            "archive preview follows the saved note color without refresh");
    if (highlighted) {
        for (y=card.top+MulDiv(38,dpi,96);y<card.top+MulDiv(58,dpi,96);++y)
            for (x=card.left+MulDiv(14,dpi,96);x<card.left+MulDiv(50,dpi,96);++x) {
                COLORREF pixel=GetPixel(memory,x,y);
                if (pixel==RGB(0,0,0)) ++black;
                if (pixel==RGB(228,196,109)) ++gold;
            }
        require(black>0 && gold>0,"search matches on dark notes use black text on the gold highlight");
    }
    SelectObject(memory,old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(list,dc);
}

static void palette_archive_checks(Note *note) {
    HWND window,list;
    browse_notes(); window=thread_window(L"Jotila.Archive",NULL);
    require(window!=NULL,"open archive for custom colors"); list=GetDlgItem(window,2);
    SetWindowTextW(GetDlgItem(window,3),L"Keep"); wait_search_ui(window);
    require(ListView_GetItemCount(list)==1,"find the unique palette fixture");
    ListView_SetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    palette_preview(list,note_color(note),TRUE);
    require(recolor_note(note,4,1),"recolor while the archive is open"); wait_search_ui(window);
    palette_preview(list,note_color(note),FALSE);
    require(ListView_GetItemState(list,0,LVIS_SELECTED|LVIS_FOCUSED)==(LVIS_SELECTED|LVIS_FOCUSED),
            "recolor preserves archive selection");
    archive_close();
}

static void palette_checks(void) {
    Note *note, view={0};
    HWND popup;
    POINT anchor={400,360};
    RECT bounds;
    HRGN region;
    CHARRANGE selected={0,4}, after;
    CHARFORMAT2W format={0};
    WIN32_FILE_ATTRIBUTE_DATA before_file, after_file;
    wchar_t id[80], label[80];
    int p,c,preference,sequence,side,footer_y;
    DWORD gdi,user;
    for (p=0;p<PALETTE_COUNT;++p) for (c=0;c<PALETTE_SIZES[p];++c) {
        double ink=color_luminance(palette_ink(p,c));
        double body=color_luminance(PALETTES[p][c]),bar=color_luminance(palette_bar(PALETTES[p][c]));
        require((max(ink,body)+.05)/(min(ink,body)+.05)>=4.5 &&
                (max(ink,bar)+.05)/(min(ink,bar)+.05)>=4.5,"every note and title color has readable ink");
    }
    current_palette=0; note=new_note(); require(note!=NULL,"create color-picker note");
    replace_text(note,L"Keep my text");
    SendMessageW(note->edit,EM_EXSETSEL,0,(LPARAM)&selected); toggle_format(note,CFM_ITALIC);
    SendMessageW(note->edit,EM_SETSEL,(WPARAM)-1,-1);
    SendMessageW(note->edit,EM_REPLACESEL,TRUE,(LPARAM)L"!");
    require(save_note(note),"save formatted color fixture");
    SendMessageW(note->edit,EM_EXSETSEL,0,(LPARAM)&selected);
    preference=current_palette; sequence=next_color;
    require(GetFileAttributesExW(note->path,GetFileExInfoStandard,&before_file),"snapshot TXT before color-only edits");
    metadata_writes=0;
    SendMessageW(note->edit,WM_CONTEXTMENU,(WPARAM)note->edit,MAKELPARAM(anchor.x,anchor.y));
    popup=FindWindowW(PALETTE_PICKER_CLASS,NULL); require(popup!=NULL,"editor right-click opens the color wheel");
    SendMessageW(popup,WM_KEYDOWN,VK_LEFT,0);
    GetWindowTextW(popup,label,80); require(!wcscmp(label,PALETTE_NAMES[PALETTE_COUNT-1]),"left arrow wraps palettes");
    SendMessageW(popup,WM_KEYDOWN,VK_RIGHT,0);
    GetWindowTextW(popup,label,80); require(!wcscmp(label,PALETTE_NAMES[0]),"right arrow wraps palettes");
    SendMessageW(popup,WM_KEYDOWN,VK_ESCAPE,0);
    require(!IsWindow(popup) && note->palette==0 && metadata_writes==0,"browsing and Escape never save or recolor");
    require(recolor_note(note,6,0),"choose a readable dark color");
    SendMessageW(note->edit,EM_EXGETSEL,0,(LPARAM)&after);
    require(after.cpMin==selected.cpMin && after.cpMax==selected.cpMax,"recolor keeps text selection");
    format.cbSize=sizeof(format); SendMessageW(note->edit,EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&format);
    require((format.dwEffects&CFE_ITALIC) && format.crTextColor==RGB(255,255,255),"dark color preserves emphasis and uses white ink");
    require(GetFileAttributesExW(note->path,GetFileExInfoStandard,&after_file) &&
            !CompareFileTime(&before_file.ftLastWriteTime,&after_file.ftLastWriteTime),"color-only edits do not rewrite TXT");
    SendMessageW(note->edit,EM_UNDO,0,0);
    require(!wcscmp(note->accepted_text,L"Keep my text"),"theme changes preserve the text undo history");
    SendMessageW(note->edit,EM_GETCHARFORMAT,SCF_DEFAULT,(LPARAM)&format);
    require(format.crTextColor==RGB(255,255,255),"undo keeps the selected note's ink");
    require(save_note(note),"save color and undo result"); wcscpy_s(id,80,note->id);
    destroy_note(note); note=open_note(id,0,TRUE);
    require(note && note->palette==6 && note->color==0 && !wcscmp(note->accepted_text,L"Keep my text"),
            "custom palette and text survive reopening");
    SendMessageW(note->edit,EM_EXSETSEL,0,(LPARAM)&selected);
    SendMessageW(note->edit,EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&format);
    require((format.dwEffects&CFE_ITALIC) && format.crTextColor==RGB(255,255,255),"RTF emphasis and theme ink survive reopening");
    palette_archive_checks(note);
    for (p=0;p<PALETTE_COUNT;++p) for (c=0;c<PALETTE_SIZES[p];++c) {
        double a=(-90.0+(c+.5)*360.0/PALETTE_SIZES[p])*3.141592653589793/180;
        LPARAM point;
        popup=palette_picker_show(note->window,anchor,p,0,select_note_color,note);
        require(popup!=NULL,"open each complete palette"); GetClientRect(popup,&bounds);
        side=bounds.right;
        point=MAKELPARAM((int)(side*.5+cos(a)*side*.3),(int)(side*.5+sin(a)*side*.3));
        SendMessageW(popup,WM_MOUSEMOVE,0,point);
        SendMessageW(popup,WM_LBUTTONDOWN,MK_LBUTTON,point); SendMessageW(popup,WM_LBUTTONUP,0,point);
        require(!IsWindow(popup) && note->palette==p && note->color==c,"each visible wedge selects its own color and closes");
    }
    require(current_palette==preference && next_color==sequence,"manual note colors never change new-note defaults or sequence");
    popup=palette_picker_show(note->window,anchor,3,0,select_note_color,note);
    GetClientRect(popup,&bounds); side=bounds.right; footer_y=bounds.bottom-MulDiv(17,note->dpi,96);
    region=CreateRectRgn(0,0,0,0); require(region!=NULL,"create popup region probe");
    require(GetWindowRgn(popup,region)!=ERROR && !PtInRegion(region,0,0) &&
            PtInRegion(region,side/2,side/2) && PtInRegion(region,side/2,footer_y),"popup consists of full circle and separate footer");
    DeleteObject(region);
    SendMessageW(popup,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(side-10,footer_y));
    SendMessageW(popup,WM_LBUTTONUP,0,MAKELPARAM(side-10,footer_y));
    GetWindowTextW(popup,label,80); require(!wcscmp(label,PALETTE_NAMES[4]),"footer arrow navigates without selecting");
    SendMessageW(popup,WM_KEYDOWN,VK_DOWN,0); SendMessageW(popup,WM_KEYDOWN,VK_RETURN,0);
    require(!IsWindow(popup) && note->palette==4 && note->color==0,"keyboard can select and commit a wedge");
    popup=palette_picker_show(note->window,anchor,3,0,select_note_color,note);
    view.window=popup; screenshot(&view,L"build\\palette-picker.bmp");
    if (IsWindow(popup)) SendMessageW(popup,WM_ACTIVATE,WA_INACTIVE,0);
    require(!IsWindow(popup) && note->palette==4,"outside activation cancels without recoloring");
    SetForegroundWindow(note->window); SetFocus(note->edit); pump(30);
    gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS); user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    for (c=0;c<32;++c) {
        popup=palette_picker_show(note->window,anchor,3,0,select_note_color,note);
        require(popup!=NULL,"repeat popup lifecycle"); SendMessageW(popup,WM_KEYDOWN,VK_ESCAPE,0);
    }
    pump(30);
    require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdi &&
            GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<=user,"repeated pickers release all GDI and window objects");
    popup=palette_picker_show(note->window,anchor,3,0,select_note_color,note);
    destroy_note(note); require(!IsWindow(popup),"destroying the note closes its picker");
    puts("PASS: palette contrast, full wedge hit targets, arrow/keyboard navigation, cancellation, immediate selection, undo/RTF persistence and popup resource lifetime.");
}

static void write_palette_fixture(const wchar_t *path, const char *text) {
    DWORD written, length=(DWORD)strlen(text);
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    require(file!=INVALID_HANDLE_VALUE,"create private palette fixture");
    require(WriteFile(file,text,length,&written,NULL) && written==length,"write private palette fixture");
    require(CloseHandle(file),"close private palette fixture");
}

static void branding_location_checks(void) {
    const wchar_t *brands[]={L"PicoSlip",L"IOTA"};
    int brand;
    wchar_t temporary[PATH_CAP],base[PATH_CAP],legacy[PATH_CAP],modern[PATH_CAP],old_notes[PATH_CAP];
    wchar_t config[PATH_CAP],fallback[PATH_CAP],loaded[PATH_CAP],old_config[PATH_CAP];
    GetTempPathW(PATH_CAP,temporary);
    for (brand=0;brand<2;++brand) {
    swprintf_s(base,PATH_CAP,L"%sJotila-upgrade-%lu-%llu",temporary,GetCurrentProcessId(),GetTickCount64());
    require(CreateDirectoryW(base,NULL),"create private profile fixture");
    require(location_user_paths(base,config,fallback) && location_load(config,fallback,loaded) &&
            !wcscmp(fallback,loaded),"fresh profiles use the Jotila notes folder");
    swprintf_s(legacy,PATH_CAP,L"%s\\%s",base,brands[brand]); swprintf_s(modern,PATH_CAP,L"%s\\Jotila",base);
    swprintf_s(old_notes,PATH_CAP,L"%s\\notes",legacy); swprintf_s(old_config,PATH_CAP,L"%s\\archive.path",legacy);
    require(CreateDirectoryW(legacy,NULL) && CreateDirectoryW(old_notes,NULL),"create legacy archive fixture");
    require(location_user_paths(base,config,fallback) && location_load(config,fallback,loaded) &&
            !wcscmp(loaded,old_notes) && GetFileAttributesW(old_notes)!=INVALID_FILE_ATTRIBUTES,
            "upgrade records the original archive without moving or deleting it");
    require(DeleteFileW(config),"clear only private upgrade preference");
    write_palette_fixture(old_config,"Z:\\Jotila-unavailable-drive-test");
    require(!location_user_paths(base,config,fallback) && GetFileAttributesW(config)==INVALID_FILE_ATTRIBUTES,
            "an unavailable legacy configured archive is never silently replaced");
    require(DeleteFileW(old_config),"remove unavailable private configuration");
    write_palette_fixture(old_config,"relative-path");
    require(CreateDirectoryW(fallback,NULL) && location_user_paths(base,config,fallback),
            "an established Jotila archive takes priority over legacy configuration");
    remove_location_fixture(fallback); remove_location_fixture(old_notes);
    remove_location_fixture(legacy); remove_location_fixture(modern); remove_location_fixture(base);
    }
    puts("PASS: fresh Jotila paths, automatic legacy archive adoption, explicit new-profile priority and unavailable-drive preservation.");
}
