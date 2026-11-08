/* Included by tests.c: real Rich Edit sizing, persistence and input routing. */
static CHARFORMAT2W format_range(Note *note, LONG first, LONG last) {
    CHARRANGE selection;
    CHARFORMAT2W format;
    SendMessageW(note->edit,EM_EXGETSEL,0,(LPARAM)&selection);
    format=selection_format(note,first,last);
    SendMessageW(note->edit,EM_EXSETSEL,0,(LPARAM)&selection);
    return format;
}

static void text_size_checks(void) {
    Note *note=new_note(),*compact;
    CHARFORMAT2W format={0},before,after,large;
    CHARRANGE selection;
    WIN32_FILE_ATTRIBUTE_DATA txt_before,txt_after;
    wchar_t id[80];
    int width,height,i;
    require(note!=NULL,"create selected-text sizing fixture");
    replace_text(note,L"Small and large\r\nLast line");
    note->validating=TRUE; note->custom_ink=TRUE;
    format.cbSize=sizeof(format); format.dwMask=CFM_SIZE|CFM_COLOR|CFM_ITALIC;
    format.yHeight=220; format.crTextColor=RGB(94,60,88); format.dwEffects=CFE_ITALIC;
    SendMessageW(note->edit,EM_SETSEL,0,5);
    SendMessageW(note->edit,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&format);
    format.yHeight=360; format.crTextColor=RGB(0,91,150); format.dwEffects=0;
    SendMessageW(note->edit,EM_SETSEL,10,15);
    SendMessageW(note->edit,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&format);
    note->validating=FALSE;
    require(save_note(note) && GetFileAttributesExW(note->path,GetFileExInfoStandard,&txt_before),"save the mixed-size fixture");
    SendMessageW(note->edit,EM_EMPTYUNDOBUFFER,0,0);
    SendMessageW(note->edit,EM_SETSEL,0,5);
    width=note->width; height=note->height;
    before=format_range(note,0,5);
    heap_requests=metric_queries=graphic_creations=metadata_writes=0;
    SendMessageW(note->edit,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA/2),0);
    require(format_range(note,0,5).yHeight==before.yHeight && !note->dirty,"fractional pinch input waits for a full text step");
    SendMessageW(note->edit,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA/2),0);
    after=format_range(note,0,5);
    SendMessageW(note->edit,EM_EXGETSEL,0,(LPARAM)&selection);
    require(after.yHeight==240 && after.crTextColor==before.crTextColor && after.dwEffects==before.dwEffects &&
            selection.cpMin==0 && selection.cpMax==5,"Ctrl+wheel enlarges only selected characters, preserving style and selection");
    require(note->width==width && note->height==height && format_range(note,10,15).yHeight==360,
            "the note and unselected text keep their dimensions");
    require(note->dirty==DIRTY_RICH && !heap_requests && !metric_queries && !graphic_creations && !metadata_writes,
            "size changes defer only RTF saving, with no app allocations, font creation or metadata writes");
    SendMessageW(note->edit,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0);
    require(format_range(note,0,5).yHeight==240 && note->width==width,"ordinary scrolling never changes font or note size");
    require(SendMessageW(note->edit,EM_UNDO,0,0) && format_range(note,0,5).yHeight==220,
            "text size is an independent native undo operation");
    require(SendMessageW(note->edit,EM_REDO,0,0) && format_range(note,0,5).yHeight==240,"redo restores text size");
    SendMessageW(note->edit,EM_SETSEL,0,15);
    SendMessageW(note->window,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA),0);
    after=format_range(note,0,5); large=format_range(note,10,15);
    require(after.yHeight==280 && large.yHeight==400 && after.crTextColor==before.crTextColor &&
            (after.dwEffects&CFE_ITALIC) && !(large.dwEffects&CFE_ITALIC),"mixed selections grow each font run without flattening styles or sizes");
    SendMessageW(note->window,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,-WHEEL_DELTA),0);
    require(format_range(note,0,5).yHeight==240 && format_range(note,10,15).yHeight==360,
            "reverse input reduces every selected font run and leaves the note fixed");
    /* Partial input must not leak between text selections and the card. */
    SendMessageW(note->edit,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA/2),0);
    SendMessageW(note->edit,EM_SETSEL,10,15);
    SendMessageW(note->edit,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA/2),0);
    require(format_range(note,10,15).yHeight==360,"a new selection starts with a fresh wheel remainder");
    SendMessageW(note->edit,EM_SETSEL,0,0);
    SendMessageW(note->edit,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA/2),0);
    require(note->width==width && note->height==height,"clearing the selection never carries text deltas into note sizing");
    SendMessageW(note->edit,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA/2),0);
    require(note->width>width && note->height>height,"without a selection, the same gesture still sizes the note");
    SendMessageW(note->edit,EM_SETSEL,0,5);
    SendMessageW(note->edit,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA/2),0);
    SendMessageW(note->edit,EM_SETSEL,0,0);
    SendMessageW(note->edit,EM_SETSEL,0,5);
    SendMessageW(note->edit,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA/2),0);
    require(format_range(note,0,5).yHeight==240,"clearing and reselecting the same range starts a fresh gesture");
    SendMessageW(note->edit,EM_SETSEL,0,15);
    SendMessageW(note->edit,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,2*WHEEL_DELTA),0);
    after=format_range(note,0,5); large=format_range(note,10,15);
    require(after.yHeight>240 && large.yHeight>360,"one wheel event can contain multiple complete text steps");
    update_style(note);
    require(format_range(note,0,5).yHeight==after.yHeight,"native style refresh preserves custom point sizes");
    require(save_note(note) && GetFileAttributesExW(note->path,GetFileExInfoStandard,&txt_after) &&
            !CompareFileTime(&txt_before.ftLastWriteTime,&txt_after.ftLastWriteTime),"font-only edits never rewrite the plain-text file");
    wcscpy_s(id,80,note->id); destroy_note(note); note=open_note(id,0,TRUE);
    require(note && format_range(note,0,5).yHeight==after.yHeight && format_range(note,10,15).yHeight==large.yHeight &&
            format_range(note,0,5).crTextColor==before.crTextColor,"RTF reopens mixed sizes, colors and emphasis exactly");
    width=note->width; height=note->height;
    SendMessageW(note->edit,EM_SETSEL,0,-1);
    SendMessageW(note->edit,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,80*WHEEL_DELTA),0);
    after=format_range(note,0,5);
    require(text_fits(note) && note->width==width && note->height==height &&
            !wcscmp(note->accepted_text,L"Small and large\r\nLast line"),"select-all growth stops before clipping any line or changing the card");
    SendMessageW(note->edit,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA),0);
    require(format_range(note,0,5).yHeight==after.yHeight,"repeated overflow attempts preserve the last accepted font size");
    require(!SendMessageW(note->edit,EM_REDO,0,0) && format_range(note,0,5).yHeight==after.yHeight,
            "redo cannot reapply a size that overflows the fixed note");
    SendMessageW(note->edit,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,-WHEEL_DELTA),0);
    require(format_range(note,0,5).yHeight<after.yHeight && text_fits(note),"reverse input works immediately after reaching the fit limit");
    { Note view=*note; screenshot(&view,L"build\\text-size.bmp"); }
    destroy_note(note);

    compact=new_note(); require(compact && resize_note(compact,3),"create a multiline size fixture");
    replace_text(compact,L"a\r\nb\r\nc\r\nd\r\ne\r\nf\r\ng\r\nh\r\ni\r\nj\r\nk\r\nl");
    SendMessageW(compact->edit,EM_SETSEL,0,-1);
    for(i=0;i<5;++i) SendMessageW(compact->edit,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,-WHEEL_DELTA),0);
    require(text_fits(compact),"measure the shortened native line heights");
    height=max(scaled(compact,MIN_HEIGHT),compact->text_extent.cy+bar_height(compact)+scaled(compact,16));
    require(fit_dimensions(compact,compact->width,height) && text_fits(compact),
            "smaller fonts allow more visible lines without a fixed eleven-point line limit");
    require(!fit_dimensions(compact,compact->width,height-scaled(compact,5)),"card shrinking respects the actual last line height");
    SendMessageW(compact->edit,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,-40*WHEEL_DELTA),0);
    require(format_range(compact,0,1).yHeight==20 && save_note(compact),"native text sizing reaches its one-point lower bound safely");
    SendMessageW(compact->edit,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,-WHEEL_DELTA),0);
    require(format_range(compact,0,1).yHeight==20 && !compact->dirty,"a lower-bound no-op does not schedule a formatting save");
    destroy_note(compact);
    compact=new_note(); require(compact!=NULL,"create a scalable checklist");
    replace_text(compact,L"\x2610 Done");
    SendMessageW(compact->edit,EM_SETSEL,0,-1);
    SendMessageW(compact->edit,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,8*WHEEL_DELTA),0);
    { POINTL origin,end;
      LONG font_pixels=MulDiv(format_range(compact,0,1).yHeight,compact->dpi,1440);
      SendMessageW(compact->edit,EM_POSFROMCHAR,(WPARAM)&origin,0);
      SendMessageW(compact->edit,EM_POSFROMCHAR,(WPARAM)&end,1);
      require(end.x-origin.x>scaled(compact,16) && font_pixels>default_line_height(compact),
              "the enlarged checkbox exceeds the original fixed hit box");
      require(click_todo(compact,MAKELPARAM(end.x-2,origin.y+font_pixels-2)) && compact->accepted_text[0]==L'\x2611',
              "the enlarged checkbox is clickable at its lower-right edge");
      require(!click_todo(compact,MAKELPARAM(end.x+scaled(compact,8),origin.y+2)),
              "clicking the checklist text outside the glyph does not toggle it");
      require(SendMessageW(compact->edit,EM_UNDO,0,0) && compact->accepted_text[0]==L'\x2610',
              "an enlarged checkbox still toggles as one undo operation"); }
    destroy_note(compact);
    puts("PASS: selected-text wheel/pinch routing, fractional input, mixed font sizes, bounded growth, native undo/redo, RTF and unchanged TXT.");
}
