/* Included by tests.c: exercise the real editor, popup, persistence and GDI
   lifetime in its isolated archive. No physical mouse/keyboard injection. */
#include <math.h>
#include "../src/palette_art.h"

static double color_luminance(COLORREF color) {
    double channel[3]={GetRValue(color)/255.0,GetGValue(color)/255.0,GetBValue(color)/255.0};
    int i;
    for (i=0;i<3;++i) channel[i]=channel[i]<=.04045 ? channel[i]/12.92 : pow((channel[i]+.055)/1.055,2.4);
    return .2126*channel[0]+.7152*channel[1]+.0722*channel[2];
}

static HWND test_picker(Note *note,POINT anchor,int palette,int color) {
    PalettePickerOptions options={palette,color,favorite_palette,FALSE,select_note_color,save_favorite,note};
    return palette_picker_show(note->window,anchor,&options);
}

static BOOL CALLBACK palette_monitor_check(HMONITOR monitor,HDC dc,LPRECT rect,LPARAM data) {
    MONITORINFO info={sizeof(info)};
    Note *note=new_note();
    POINT anchor={40,60};
    HWND popup;
    int palette;
    (void)dc; (void)rect; (void)data;
    require(note && GetMonitorInfoW(monitor,&info),"create a palette fixture on each connected monitor");
    replace_text(note,L"Monitor palette check");
    SetWindowPos(note->window,NULL,info.rcWork.left+32,info.rcWork.top+32,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    ClientToScreen(note->edit,&anchor);
    printf("Checking palette on monitor (%ld,%ld), note DPI %u.\n",info.rcMonitor.left,info.rcMonitor.top,GetDpiForWindow(note->window));
    fflush(stdout);
    SendMessageW(note->edit,EM_SETSEL,0,0);
    SendMessageW(note->edit,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(40,60));
    SendMessageW(note->edit,WM_RBUTTONUP,0,MAKELPARAM(40,60));
    popup=thread_window(PALETTE_PICKER_CLASS,NULL);
    require(popup && GetDpiForWindow(popup)==GetDpiForWindow(note->window) &&
            MonitorFromWindow(popup,MONITOR_DEFAULTTONEAREST)==monitor,
            "right-click opens a live palette at the note's monitor and DPI");
    SendMessageW(popup,WM_KEYDOWN,VK_DOWN,0); SendMessageW(popup,WM_KEYDOWN,VK_RETURN,0);
    require(!IsWindow(popup),"background selection closes the monitor fixture's popup");
    palette=note->palette;
    SendMessageW(note->edit,EM_SETSEL,0,7);
    SendMessageW(note->edit,WM_CONTEXTMENU,(WPARAM)note->edit,MAKELPARAM(anchor.x,anchor.y));
    popup=thread_window(PALETTE_PICKER_CLASS,NULL);
    require(popup && GetDpiForWindow(popup)==GetDpiForWindow(note->window),
            "selected-text mode also opens at the correct monitor DPI");
    SendMessageW(popup,WM_KEYDOWN,VK_DOWN,0); SendMessageW(popup,WM_KEYDOWN,VK_RETURN,0);
    require(!IsWindow(popup) && note->palette==palette && note->custom_ink,
            "selected-text color leaves the background unchanged on every monitor");
    destroy_note(note);
    return TRUE;
}

static void palette_monitor_checks(void) {
    require(EnumDisplayMonitors(NULL,NULL,palette_monitor_check,0),"check palettes on every connected monitor");
    load_preferences();
    puts("PASS: right-click background and text palettes across all connected monitor scales.");
}

static UINT picker_interrupt, picker_trigger;
static BOOL picker_interrupted;
static LRESULT CALLBACK interrupt_picker(int code,WPARAM wparam,LPARAM lparam) {
    if(code>=0 && !picker_interrupted) {
        const CWPSTRUCT *event=(const CWPSTRUCT *)lparam;
        wchar_t name[64];
        if(event->message==picker_trigger && GetClassNameW(event->hwnd,name,64) &&
            !wcscmp(name,PALETTE_PICKER_CLASS) &&
            ((picker_trigger==WM_SHOWWINDOW && event->wParam) ||
             (picker_trigger==WM_WINDOWPOSCHANGING && ((WINDOWPOS *)event->lParam)->cx>0))) {
            RECT rect;
            picker_interrupted=TRUE; GetWindowRect(event->hwnd,&rect);
            SendMessageW(event->hwnd,picker_interrupt,0,(LPARAM)&rect);
        }
    }
    return CallNextHookEx(NULL,code,wparam,lparam);
}

static void palette_interruption_checks(void) {
    static const UINT messages[]={WM_DPICHANGED,WM_DISPLAYCHANGE,WM_CLOSE,WM_ACTIVATE};
    Note *note=new_note();
    POINT anchor={400,360};
    int i;
    require(note!=NULL,"create isolated palette interruption owner");
    for(i=0;i<4;++i) {
        HHOOK hook;
        HWND popup;
        picker_interrupt=messages[i]; picker_interrupted=FALSE;
        picker_trigger=i==3?WM_SHOWWINDOW:WM_WINDOWPOSCHANGING;
        hook=SetWindowsHookExW(WH_CALLWNDPROC,interrupt_picker,NULL,GetCurrentThreadId());
        require(hook!=NULL,"observe only this test thread's popup messages");
        popup=test_picker(note,anchor,3,0);
        require(UnhookWindowsHookEx(hook),"remove the private popup hook");
        require(picker_interrupted && popup==NULL && !thread_window(PALETTE_PICKER_CLASS,NULL),
                "synchronous placement or activation cancellation never returns a destroyed picker");
        popup=test_picker(note,anchor,3,0);
        require(popup!=NULL,"a new palette opens normally after interrupted creation");
        SendMessageW(popup,WM_KEYDOWN,VK_ESCAPE,0);
    }
    destroy_note(note);
    puts("PASS: synchronous DPI/display/close/activation cancellation during popup creation and safe reopening.");
}

static void browse_picker(HWND popup,int target) {
    wchar_t name[80];
    int i,order=-1,step;
    GetWindowTextW(popup,name,80);
    for(i=0;i<POPULAR_PALETTE_COUNT;++i)
        if(!wcscmp(name,PALETTE_NAMES[POPULAR_PALETTES[i]])) { order=i; break; }
    require(order>=0 && PALETTE_POPULAR_INDEX[target]>=0,"navigate between catalog palettes only");
    step=(PALETTE_POPULAR_INDEX[target]-order+POPULAR_PALETTE_COUNT)%POPULAR_PALETTE_COUNT;
    if(step>POPULAR_PALETTE_COUNT/2) step-=POPULAR_PALETTE_COUNT;
    for(i=0;i<abs(step);++i) SendMessageW(popup,WM_KEYDOWN,step<0?VK_LEFT:VK_RIGHT,0);
}

static void palette_catalog_checks(void) {
    BOOL seen[PALETTE_COUNT]={0};
    int i,p,c;
    require(POPULAR_PALETTE_COUNT==80 && PALETTE_COUNT==83,"include all 80 Popular palettes alongside the three legacy defaults");
    for(i=0;i<DEFAULT_PALETTE_COUNT;++i)
        require(PALETTE_POPULAR_INDEX[i]==-1 && PALETTE_SIZES[i]==6,"the original palettes are excluded from wheel navigation");
    for(i=0;i<POPULAR_PALETTE_COUNT;++i) {
        p=POPULAR_PALETTES[i];
        require(p>=DEFAULT_PALETTE_COUNT && p<PALETTE_COUNT && !seen[p] &&
            PALETTE_POPULAR_INDEX[p]==i && PALETTE_SIZES[p]==5,"catalog navigation visits every five-color palette exactly once");
        seen[p]=TRUE;
    }
    require(!wcscmp(PALETTE_NAMES[POPULAR_PALETTES[0]],L"Cappuccino") &&
        !wcscmp(PALETTE_NAMES[POPULAR_PALETTES[79]],L"PwC Corp"),"browse order matches the complete source collection");
    require(PALETTES[3][0]==RGB(255,179,186) && PALETTES[4][1]==RGB(162,121,143) &&
        PALETTES[5][3]==RGB(94,60,88) && PALETTES[6][4]==RGB(14,17,17) &&
        PALETTES[7][0]==RGB(210,212,220) && PALETTES[8][3]==RGB(254,255,163) &&
        PALETTES[9][4]==RGB(255,255,227),"all seven pre-existing palette IDs keep their original RGB values");
    for(p=0;p<PALETTE_COUNT;++p) for(c=0;c<PALETTE_SIZES[p];++c) {
        double body=color_luminance(PALETTES[p][c]),bar=color_luminance(palette_bar(PALETTES[p][c]));
        double ink=color_luminance(palette_ink(p,c)),title=color_luminance(palette_bar_ink(p,c));
        require((max(ink,body)+.05)/(min(ink,body)+.05)>=4.5 &&
            (max(title,bar)+.05)/(min(title,bar)+.05)>=4.5,"every body and title color has independently readable ink");
        require((palette_ink(p,c)==RGB(255,255,255))==(1.05/(body+.05)>(body+.05)/.05) &&
            (palette_bar_ink(p,c)==RGB(255,255,255))==(1.05/(bar+.05)>(bar+.05)/.05),
            "each precomputed ink bit chooses the stronger black-or-white contrast");
    }
    puts("PASS: all 80 Popular palettes, original RGB ordering, stable legacy IDs, exclusive wheel navigation and per-color body/title contrast.");
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

static void palette_art_checks(void) {
    static const int dpis[]={96,120,144,192};
    int d,p,c;
    for(d=0;d<4;++d) {
        PaletteArt *art=palette_art_create(dpis[d]);
        const PaletteLayout *l;
        BYTE weights[COLOR_COUNT]={0};
        DWORD gdi;
        require(art!=NULL,"allocate the native wheel at common monitor scales");
        l=palette_art_layout(art);
        for(p=0;p<4;p+=3) {
            HDC dc;
            DIBSECTION bitmap={0};
            DWORD *pixels;
            int x,y,partial=0;
            double radius=(l->wheel.right-l->wheel.left)*.5;
            palette_art_prepare(art,p,-1,FALSE); dc=palette_art_frame(art,weights,PICK_NONE,FALSE);
            require(GetObjectW(GetCurrentObject(dc,OBJ_BITMAP),sizeof(bitmap),&bitmap)==sizeof(bitmap),"inspect actual premultiplied wheel pixels");
            pixels=(DWORD *)bitmap.dsBm.bmBits;
            for(y=0;y<l->size.cy;++y) for(x=0;x<l->size.cx;++x) {
                DWORD pixel=pixels[(size_t)y*l->size.cx+x],a=pixel>>24;
                double dx=x+.5-(l->wheel.left+radius),dy=y+.5-(l->wheel.top+radius),distance=sqrt(dx*dx+dy*dy);
                require(((pixel>>16)&255)<=a && ((pixel>>8)&255)<=a && (pixel&255)<=a,
                        "every pixel satisfies premultiplied-alpha composition");
                if(distance<radius-1) require(a==255,"the entire disk is filled without seam pinholes");
                if(distance>radius-.5 && distance<radius+.5 && a>0 && a<255) ++partial;
            }
            require(partial>100,"circle edges carry fractional pixel coverage at every tested DPI");
            {
                int edge=l->wheel.right-1,middle=(l->wheel.top+l->wheel.bottom)/2;
                COLORREF rim=GetPixel(dc,edge,middle),inside=GetPixel(dc,edge-MulDiv(3,dpis[d],96),middle);
                DWORD shadow=pixels[(size_t)middle*l->size.cx+l->wheel.right+MulDiv(1,dpis[d],96)];
                require(GetRValue(rim)<GetRValue(inside)*.6 && GetGValue(rim)<GetGValue(inside)*.6 &&
                        GetBValue(rim)<GetBValue(inside)*.6,"the half-DIP black rim remains distinct at every DPI");
                require((shadow>>24)>0 && (shadow>>24)<=24 && !(shadow&0xffffff),"the circle has a subtle black shadow outside its rim");
                palette_art_prepare(art,p,-1,TRUE); palette_art_frame(art,weights,PICK_NONE,FALSE);
                require(GetPixel(dc,edge,middle)==rim &&
                        pixels[(size_t)middle*l->size.cx+l->wheel.right+MulDiv(1,dpis[d],96)]==shadow,
                        "text and note color modes share the same rim and shadow");
            }
            for(c=0;c<PALETTE_SIZES[p];++c) {
                double angle=(-90+(c+.5)*360/PALETTE_SIZES[p])*3.141592653589793/180;
                POINT point={(LONG)(l->wheel.left+radius+cos(angle)*radius*.6),
                             (LONG)(l->wheel.top+radius+sin(angle)*radius*.6)};
                require(palette_art_hit(art,point)==c && GetPixel(dc,point.x,point.y)==PALETTES[p][c],
                        "five- and six-wedge hit testing matches the rendered color at every DPI");
            }
        }
        gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        for(c=0;c<60;++c) { weights[0]=(BYTE)(c*4); palette_art_frame(art,weights,PICK_NONE,FALSE); }
        require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==gdi,"animation frames reuse their surface and drawing objects");
        palette_art_destroy(art);
    }
    puts("PASS: opaque wedge interiors, premultiplied antialiased edges, exact colors and hit testing at 100/125/150/200% DPI.");
}

static LRESULT CALLBACK count_picker_timers(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR id,DWORD_PTR data) {
    (void)id;
    if(message==WM_TIMER) ++*(unsigned *)data;
    return DefSubclassProc(window,message,wparam,lparam);
}

static BOOL close_favorite_callback(void *context,int palette) {
    (void)palette;
    DestroyWindow(*(HWND *)context); return TRUE;
}

static void palette_preference_checks(void) {
    Note *note,*other;
    wchar_t id[80];
    POINT anchor={430,380};
    HWND popup;
    PaletteArt *art;
    PaletteLayout layout;
    PalettePickerOptions options={0};
    unsigned timer_count=0,seen=0;
    int i,base=setting(L"app",L"palette",0),sequence=next_color,favorite=PALETTE_COUNT-1;
    HANDLE lock;
    require(write_setting(L"app",L"palette",2) && write_setting(L"app",L"next_color",0) &&
            write_setting(L"app",L"favorite_palette",-1),"set isolated original palette preferences");
    load_preferences();
    for(i=0;i<COLOR_COUNT;++i) {
        note=new_note();
        require(note && note->palette==2 && note->color==i,"untouched title-bar defaults retain their original sequence");
        destroy_note(note);
    }
    note=new_note(); require(note!=NULL,"create preference owner");
    select_note_color(note,4,1);
    require(current_palette==4 && shuffled_palette,"a committed background sets this session's palette");
    for(i=0;i<20;++i) {
        Note *next=new_note();
        require(next && next->palette==4 && !(seen&(1u<<next->color)),"new notes draw every palette slot once per cycle");
        seen|=1u<<next->color;
        if(i%5==4) { require(seen==31,"a complete cycle uses all five colors"); seen=0; }
        destroy_note(next);
    }
    require(setting(L"app",L"palette",-1)==2 && setting(L"app",L"favorite_palette",-2)==-1,
            "session-only selection leaves startup preferences unchanged");
    replace_text(note,L"Favorite palette"); require(save_note(note),"persist the favorite owner");
    wcscpy_s(id,80,note->id);
    other=new_note(); require(other!=NULL,"keep another note open in the previous palette");
    art=palette_art_create(note->dpi); require(art!=NULL,"read star layout at owner DPI");
    layout=*palette_art_layout(art); palette_art_destroy(art);
    popup=test_picker(note,anchor,note->palette,note->color);
    browse_picker(popup,favorite);
    require(SetWindowSubclass(popup,count_picker_timers,1,(DWORD_PTR)&timer_count),"observe popup animation lifetime");
    pump(220); timer_count=0; pump(180);
    require(timer_count==0,"a settled popup has no animation timer");
    SendMessageW(popup,WM_MOUSEMOVE,0,MAKELPARAM(layout.size.cx/2+40,layout.size.cx/2-30));
    pump(180); timer_count=0; pump(180);
    require(timer_count==0,"hover animation also stops after its transition");
    SendMessageW(popup,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM((layout.favorite.left+layout.favorite.right)/2,
        (layout.favorite.top+layout.favorite.bottom)/2));
    SendMessageW(popup,WM_LBUTTONUP,0,MAKELPARAM((layout.favorite.left+layout.favorite.right)/2,
        (layout.favorite.top+layout.favorite.bottom)/2));
    require(IsWindow(popup) && favorite_palette==favorite && current_palette==favorite && note->palette==favorite &&
            note->color==0 && setting(L"app",L"favorite_palette",-1)==favorite,
            "star immediately applies and saves the favorite without closing the picker");
    require(setting(note->id,L"palette",-1)==favorite && setting(note->id,L"color",-1)==0 && other->palette==4,
            "the current note's color is durable while other open notes keep their palette");
    for(i=0;i<10;++i) {
        Note *next=new_note();
        require(next && next->palette==favorite && !(seen&(1u<<next->color)),
                "new notes immediately draw from the favorite without reloading preferences");
        seen|=1u<<next->color;
        if(i%5==4) { require(seen==31,"changing favorites resets the previous palette's color bag"); seen=0; }
        destroy_note(next);
    }
    { Note view={0}; view.window=popup; screenshot(&view,L"build\\palette-favorite.bmp"); }
    if(IsWindow(popup)) SendMessageW(popup,WM_KEYDOWN,VK_ESCAPE,0);
    destroy_note(note); note=open_note(id,0,TRUE);
    require(note && note->palette==favorite && note->color==0,"the recolored note survives closing and reopening");
    load_preferences();
    require(current_palette==favorite && favorite_palette==favorite && shuffled_palette,"startup uses the saved favorite");
    { Note *next=new_note(); require(next && next->palette==favorite,"favorite governs actual new notes after reload"); destroy_note(next); }
    select_note_color(note,favorite,3);
    popup=test_picker(note,anchor,note->palette,note->color);
    SendMessageW(popup,WM_KEYDOWN,'F',0);
    require(favorite_palette==-1 && current_palette==favorite && note->color==3,
            "clearing a favorite keeps the current note and session colors");
    i=colors_left;
    SendMessageW(popup,WM_KEYDOWN,'F',0);
    require(favorite_palette==favorite && note->color==3 && colors_left==i,
            "starring the current palette preserves its chosen color and remaining shuffle");
    SendMessageW(popup,WM_KEYDOWN,VK_RETURN,0);
    require(!IsWindow(popup) && note->color==3,"Enter after starring retains the selected wedge");
    lock=CreateFileW(state_path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    require(lock!=INVALID_HANDLE_VALUE && !save_favorite(note,6) && favorite_palette==favorite &&
            current_palette==favorite && note->palette==favorite && note->color==3 && colors_left==i,
            "a failed favorite save leaves the active preference intact"); CloseHandle(lock);
    popup=test_picker(note,anchor,note->palette,note->color); browse_picker(popup,6);
    SendMessageW(popup,WM_KEYDOWN,'F',0);
    require(favorite_palette==6 && current_palette==6 && note->palette==6 && note->color==0,
            "F applies a different favorite immediately, just like clicking its star");
    { Note *next=new_note(); require(next && next->palette==6,"keyboard favorite governs the very next note"); destroy_note(next); }
    SendMessageW(popup,WM_KEYDOWN,VK_ESCAPE,0);
    popup=test_picker(note,anchor,note->palette,note->color); SendMessageW(popup,WM_KEYDOWN,'F',0);
    require(favorite_palette==-1 && setting(L"app",L"favorite_palette",0)==-1,"F removes the displayed favorite");
    SendMessageW(popup,WM_KEYDOWN,VK_ESCAPE,0); load_preferences();
    require(current_palette==2 && !shuffled_palette,"removing the favorite restores the original startup preference");
    options.palette=3; options.color=0; options.favorite=-1; options.select=select_note_color;
    options.favorite_changed=close_favorite_callback; options.context=&popup;
    popup=palette_picker_show(note->window,anchor,&options); require(popup!=NULL,"open callback lifetime fixture");
    SendMessageW(popup,WM_KEYDOWN,'F',0);
    require(!IsWindow(popup),"favorite callbacks may close their own popup safely");
    note_command(note,CMD_PALETTE+1);
    { HMENU menu=build_note_menu(note),palettes=NULL;
      for(i=0;i<GetMenuItemCount(menu);++i) {
          HMENU submenu=GetSubMenu(menu,i);
          if(submenu && GetMenuItemID(submenu,0)==CMD_PALETTE) { palettes=submenu; break; }
      }
      require(menu && palettes && GetMenuItemCount(palettes)==DEFAULT_PALETTE_COUNT,
              "title-bar palette menu still contains only the three original palettes"); DestroyMenu(menu); }
    require(current_palette==1 && !shuffled_palette,"title-bar choices restore the original drawing sequence");
    use_session_palette(1);
    for(i=0;i<4;++i) {
        Note *first=new_note(),*second=new_note();
        require(first && second && note_color(first)!=note_color(second),
                "a manually chosen monochrome palette draws distinct colors despite legacy repeated slots");
        destroy_note(first); destroy_note(second);
    }
    destroy_note(note); destroy_note(other);
    require(write_setting(L"app",L"palette",base) && write_setting(L"app",L"next_color",sequence),"restore test preference baseline");
    load_preferences();
    puts("PASS: immediate mouse/keyboard favorites, current and next note colors, persistence, shuffle cycles, removal, save failure and popup lifetime.");
}

static CHARFORMAT2W selection_format(Note *note,LONG start,LONG end) {
    CHARFORMAT2W format={0};
    format.cbSize=sizeof(format);
    SendMessageW(note->edit,EM_SETSEL,start,end);
    SendMessageW(note->edit,EM_GETCHARFORMAT,SCF_SELECTION,(LPARAM)&format);
    return format;
}

static void palette_text_checks(void) {
    Note *note;
    HWND popup;
    CHARRANGE range;
    CHARFORMAT2W format;
    WIN32_FILE_ATTRIBUTE_DATA before,after;
    wchar_t id[80];
    int session,background,color;
    note=new_note(); require(note!=NULL,"create foreground formatting fixture");
    require(recolor_note(note,0,0),"use a readable light note");
    replace_text(note,L"Blue and plain");
    SendMessageW(note->edit,EM_SETSEL,0,4); toggle_format(note,CFM_ITALIC);
    require(save_note(note) && GetFileAttributesExW(note->path,GetFileExInfoStandard,&before),"save text before foreground-only changes");
    session=current_palette; background=note->palette; color=note->color;
    SendMessageW(note->edit,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(180,80));
    SendMessageW(note->edit,EM_EXGETSEL,0,(LPARAM)&range);
    require(range.cpMin==0 && range.cpMax==4,"right-down outside the range never collapses selected text");
    SendMessageW(note->edit,WM_RBUTTONUP,0,MAKELPARAM(180,80));
    popup=thread_window(PALETTE_PICKER_CLASS,NULL); require(popup!=NULL,"right-up with a selection opens foreground mode");
    browse_picker(popup,6);
    SendMessageW(popup,WM_KEYDOWN,VK_UP,0); SendMessageW(popup,WM_KEYDOWN,VK_RETURN,0);
    require(!IsWindow(popup) && note->custom_ink && note->palette==background && note->color==color && current_palette==session,
            "text color closes immediately without changing background or session palette");
    SendMessageW(note->edit,EM_EXGETSEL,0,(LPARAM)&range);
    require(range.cpMin==0 && range.cpMax==4,"text formatting preserves the selected range");
    format=selection_format(note,0,4);
    require(format.crTextColor==PALETTES[6][4] && (format.dwEffects&(CFE_BOLD|CFE_ITALIC))==(CFE_BOLD|CFE_ITALIC),
            "selected text gets the exact color while preserving emphasis");
    format=selection_format(note,5,14); require(format.crTextColor==RGB(0,0,0),"unselected text keeps its foreground");
    require(GetFileAttributesExW(note->path,GetFileExInfoStandard,&after) &&
            !CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime),"foreground formatting never rewrites the TXT body");
    SendMessageW(note->edit,EM_UNDO,0,0); format=selection_format(note,0,4);
    require(format.crTextColor==RGB(0,0,0) && !wcscmp(note->accepted_text,L"Blue and plain"),"foreground color is a normal, isolated undo operation");
    SendMessageW(note->edit,EM_REDO,0,0); format=selection_format(note,0,4);
    require(format.crTextColor==PALETTES[6][4],"redo restores the selected foreground color");
    require(recolor_note(note,6,0),"change background across the automatic-ink boundary");
    format=selection_format(note,0,4); require(format.crTextColor==PALETTES[6][4],"manual colors survive a background change");
    update_style(note); format=selection_format(note,0,4);
    require(format.crTextColor==PALETTES[6][4],"font and DPI style updates preserve manual colors");
    require(save_note(note),"persist mixed foreground colors"); wcscpy_s(id,80,note->id); destroy_note(note);
    note=open_note(id,0,TRUE); require(note && note->custom_ink,"restore manual-color ownership with the note");
    format=selection_format(note,0,4);
    require(format.crTextColor==PALETTES[6][4] && (format.dwEffects&CFE_ITALIC),"RTF restores color and emphasis exactly");
    SendMessageW(note->edit,WM_CONTEXTMENU,(WPARAM)note->edit,MAKELPARAM(-1,-1));
    popup=thread_window(PALETTE_PICKER_CLASS,NULL); require(popup!=NULL,"open favorite with manually colored text selected");
    browse_picker(popup,3); SendMessageW(popup,WM_KEYDOWN,'F',0);
    SendMessageW(note->edit,EM_EXGETSEL,0,(LPARAM)&range);
    require(IsWindow(popup) && favorite_palette==3 && current_palette==3 && note->palette==3 && note->color==0 &&
            range.cpMin==0 && range.cpMax==4,"text-mode favorite changes the background and preserves the selection");
    format=selection_format(note,0,4);
    require(format.crTextColor==PALETTES[6][4] && (format.dwEffects&CFE_ITALIC),
            "starring a background preserves manual foreground color and emphasis");
    format=selection_format(note,5,14);
    require(format.crTextColor==RGB(0,0,0),"starring also preserves the unselected foreground");
    SendMessageW(popup,WM_KEYDOWN,'F',0); SendMessageW(popup,WM_KEYDOWN,VK_ESCAPE,0);
    require(favorite_palette==-1 && note->palette==3,"removing a text-mode favorite leaves the new background in place");
    require(save_note(note),"persist the starred note's mixed formatting"); destroy_note(note);
    note=open_note(id,0,TRUE); require(note && note->palette==3 && note->custom_ink,"reopen the starred background and manual colors");
    format=selection_format(note,0,4);
    require(format.crTextColor==PALETTES[6][4] && (format.dwEffects&CFE_ITALIC),
            "favorite changes preserve mixed foregrounds after reopening");
    SendMessageW(note->edit,EM_SETSEL,0,-1);
    SendMessageW(note->edit,WM_CONTEXTMENU,(WPARAM)note->edit,MAKELPARAM(-1,-1));
    popup=thread_window(PALETTE_PICKER_CLASS,NULL); require(popup!=NULL,"keyboard context request supports select-all");
    browse_picker(popup,POPULAR_PALETTES[9]); /* Facebook ends with exact white. */
    SendMessageW(popup,WM_KEYDOWN,VK_UP,0); SendMessageW(popup,WM_KEYDOWN,VK_RETURN,0);
    format=selection_format(note,0,-1);
    require((format.dwMask&CFM_COLOR) && format.crTextColor==RGB(255,255,255),"select-all applies one foreground color to the complete text");
    require(save_note(note),"save full foreground selection"); destroy_note(note);
    puts("PASS: right-click selection preservation, partial/all-text foregrounds, independent undo/redo, emphasis, RTF persistence and unchanged TXT writes.");
}

static void palette_checks(void) {
    Note *note, view={0};
    HWND popup;
    POINT anchor={400,360};
    RECT bounds;
    PaletteArt *art;
    PaletteLayout layout;
    CHARRANGE selected={0,4}, after;
    CHARFORMAT2W format={0};
    WIN32_FILE_ATTRIBUTE_DATA before_file, after_file;
    wchar_t id[80], label[80];
    int p,c,sequence,side,footer_y,neighbor=POPULAR_PALETTES[2];
    DWORD gdi,user;
    palette_catalog_checks(); palette_monitor_checks(); palette_interruption_checks();
    current_palette=0; shuffled_palette=FALSE;
    note=new_note(); require(note!=NULL,"create color-picker note");
    art=palette_art_create(note->dpi); require(art!=NULL,"create native coverage renderer");
    layout=*palette_art_layout(art); palette_art_destroy(art);
    replace_text(note,L"Keep my text");
    SendMessageW(note->edit,EM_EXSETSEL,0,(LPARAM)&selected); toggle_format(note,CFM_ITALIC);
    SendMessageW(note->edit,EM_SETSEL,(WPARAM)-1,-1);
    SendMessageW(note->edit,EM_REPLACESEL,TRUE,(LPARAM)L"!");
    require(save_note(note),"save formatted color fixture");
    SendMessageW(note->edit,EM_EXSETSEL,0,(LPARAM)&selected);
    sequence=next_color;
    require(GetFileAttributesExW(note->path,GetFileExInfoStandard,&before_file),"snapshot TXT before color-only edits");
    metadata_writes=0;
    SendMessageW(note->edit,WM_CONTEXTMENU,(WPARAM)note->edit,MAKELPARAM(anchor.x,anchor.y));
    popup=thread_window(PALETTE_PICKER_CLASS,NULL); require(popup!=NULL,"editor right-click opens the color wheel");
    for(p=0;p<POPULAR_PALETTE_COUNT;++p) {
        GetWindowTextW(popup,label,80);
        require(!wcscmp(label,PALETTE_NAMES[POPULAR_PALETTES[p]]),"arrows expose all 80 palettes in source order, without standard palettes");
        SendMessageW(popup,WM_KEYDOWN,VK_RIGHT,0);
    }
    SendMessageW(popup,WM_KEYDOWN,VK_LEFT,0);
    GetWindowTextW(popup,label,80); require(!wcscmp(label,PALETTE_NAMES[POPULAR_PALETTES[POPULAR_PALETTE_COUNT-1]]),"left arrow wraps palettes");
    SendMessageW(popup,WM_KEYDOWN,VK_RIGHT,0);
    GetWindowTextW(popup,label,80); require(!wcscmp(label,PALETTE_NAMES[POPULAR_PALETTES[0]]),"right arrow wraps palettes");
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
    {
        HANDLE lock=CreateFileW(state_path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
        require(lock!=INVALID_HANDLE_VALUE && !recolor_note(note,3,1),"a locked archive reports an unsaved background change");
        CloseHandle(lock);
        require(recolor_note(note,3,1) && setting(note->id,L"palette",-1)==3 && setting(note->id,L"color",-1)==1,
                "choosing the same color retries an earlier failed save");
    }
    for (p=DEFAULT_PALETTE_COUNT;p<PALETTE_COUNT;++p) for (c=0;c<PALETTE_SIZES[p];++c) {
        double a=(-90.0+(c+.5)*360.0/PALETTE_SIZES[p])*3.141592653589793/180;
        LPARAM point;
        LOGPEN pen;
        popup=test_picker(note,anchor,p,0);
        require(popup!=NULL,"open each complete palette"); GetClientRect(popup,&bounds);
        side=bounds.right;
        point=MAKELPARAM((int)(side*.5+cos(a)*side*.3),(int)(side*.5+sin(a)*side*.3));
        SendMessageW(popup,WM_MOUSEMOVE,0,point);
        SendMessageW(popup,WM_LBUTTONDOWN,MK_LBUTTON,point); SendMessageW(popup,WM_LBUTTONUP,0,point);
        require(!IsWindow(popup) && note->palette==p && note->color==c && current_palette==p,
                "each visible wedge selects its own color, sets the session palette and closes");
        SendMessageW(note->edit,EM_GETCHARFORMAT,SCF_DEFAULT,(LPARAM)&format);
        require(format.crTextColor==palette_ink(p,c) && GetObjectW(note->pen,sizeof(pen),&pen)==sizeof(pen) &&
                pen.lopnColor==palette_bar_ink(p,c),"actual editor defaults and title controls use their own contrast choice");
    }
    destroy_note(note); note=open_note(id,0,TRUE);
    require(note && note->palette==PALETTE_COUNT-1 && note->color==4,"new palette IDs survive saving and reopening");
    require(shuffled_palette && next_color==sequence,"background selection enables palette drawing without consuming a color");
    popup=test_picker(note,anchor,3,0);
    GetWindowRect(popup,&bounds);
    require((GetWindowLongPtrW(popup,GWL_EXSTYLE)&WS_EX_LAYERED) &&
        SendMessageW(popup,WM_NCHITTEST,0,MAKELPARAM(bounds.left,bounds.top))==HTTRANSPARENT &&
        SendMessageW(popup,WM_NCHITTEST,0,MAKELPARAM(bounds.left+layout.size.cx/2,bounds.top+layout.size.cx/2))==HTCLIENT,
        "alpha popup keeps the full circle clickable and transparent corners pass through");
    side=(layout.next.left+layout.next.right)/2; footer_y=(layout.next.top+layout.next.bottom)/2;
    SendMessageW(popup,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(side,footer_y));
    SendMessageW(popup,WM_LBUTTONUP,0,MAKELPARAM(side,footer_y));
    GetWindowTextW(popup,label,80); require(!wcscmp(label,PALETTE_NAMES[neighbor]),"footer arrow navigates without selecting");
    SendMessageW(popup,WM_KEYDOWN,VK_DOWN,0); SendMessageW(popup,WM_KEYDOWN,VK_RETURN,0);
    require(!IsWindow(popup) && note->palette==neighbor && note->color==0,"keyboard can select and commit a wedge");
    popup=test_picker(note,anchor,3,0);
    view.window=popup; screenshot(&view,L"build\\palette-picker.bmp");
    if (IsWindow(popup)) SendMessageW(popup,WM_ACTIVATE,WA_INACTIVE,0);
    require(!IsWindow(popup) && note->palette==neighbor,"outside activation cancels without recoloring");
    SetForegroundWindow(note->window); SetFocus(note->edit); pump(30);
    gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS); user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    for (c=0;c<32;++c) {
        popup=test_picker(note,anchor,3,0);
        require(popup!=NULL,"repeat popup lifecycle"); SendMessageW(popup,WM_KEYDOWN,VK_ESCAPE,0);
    }
    pump(30);
    require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdi &&
            GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<=user,"repeated pickers release all GDI and window objects");
    popup=test_picker(note,anchor,3,0);
    destroy_note(note); require(!IsWindow(popup),"destroying the note closes its picker");
    load_preferences();
    palette_art_checks(); palette_preference_checks(); palette_text_checks();
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
