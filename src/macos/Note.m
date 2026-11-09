#import "App.h"

NSColor *JNColor(COLORREF c) {
    return [NSColor colorWithSRGBRed:GetRValue(c)/255.0 green:GetGValue(c)/255.0 blue:GetBValue(c)/255.0 alpha:1];
}
void JNError(NSError *error) { if (error) [NSApp presentError:error]; }
void JNActivate(void) {
    if (@available(macOS 14.0, *)) [NSApp activate];
    else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        /* Compatibility with the macOS 13 deployment target. */
        [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
    }
}
NSString *JNAsk(NSString *title, NSString *value) {
    NSAlert *alert = [NSAlert new]; alert.messageText = title;
    [alert addButtonWithTitle:@"Save"]; [alert addButtonWithTitle:@"Cancel"];
    NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(0,0,360,24)]; field.stringValue = value;
    alert.accessoryView = field; alert.window.initialFirstResponder = field;
    return [alert runModal] == NSAlertFirstButtonReturn ? field.stringValue : nil;
}
static NSInteger bounded(NSString *value, NSInteger fallback, NSInteger low, NSInteger high) {
    NSInteger number = value ? value.integerValue : fallback;
    return MAX(low, MIN(high, number));
}
static NSString *paletteName(NSInteger index) {
    const wchar_t *p = PALETTE_NAMES[index];
    return [[NSString alloc] initWithBytes:p length:wcslen(p)*sizeof(*p) encoding:NSUTF32LittleEndianStringEncoding];
}

@interface JNPanel : NSPanel @end
@implementation JNPanel
- (BOOL)canBecomeKey { return YES; }
- (BOOL)canBecomeMain { return NO; }
@end
@interface JNBar : NSView
@property(nonatomic, weak) JNNote *note;
@property(nonatomic, strong) NSColor *paper;
@property(nonatomic, strong) NSColor *ink;
@property(nonatomic, copy) NSString *caption;
@end
@implementation JNBar
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirty {
    [self.paper setFill]; NSRectFill(NSIntersectionRect(dirty,self.bounds));
    [self.caption drawInRect:NSMakeRect(12,8,MAX(0,self.bounds.size.width-114),20)
        withAttributes:@{NSFontAttributeName:[NSFont boldSystemFontOfSize:12], NSForegroundColorAttributeName:self.ink}];
}
- (void)mouseDown:(NSEvent *)event {
    if (event.clickCount == 2) [self.note rename:nil];
    else [self.window performWindowDragWithEvent:event];
}
- (void)rightMouseDown:(NSEvent *)event { [NSMenu popUpContextMenu:self.note.contextMenu withEvent:event forView:self]; }
@end
@interface JNText : NSTextView
@property(nonatomic, weak) JNNote *note;
@end
@implementation JNText
- (NSMenu *)menuForEvent:(NSEvent *)event { (void)event; return self.note.contextMenu; }
- (void)scrollWheel:(NSEvent *)event {
    if (event.modifierFlags & NSEventModifierFlagCommand) [self.note resizeBy:event.scrollingDeltaY > 0 ? 1 : -1];
    else [super scrollWheel:event];
}
- (void)magnifyWithEvent:(NSEvent *)event { if (fabs(event.magnification) > 0.01) [self.note resizeBy:event.magnification > 0 ? 1 : -1]; }
- (void)paste:(id)sender { [self pasteAsPlainText:sender]; }
- (void)mouseDown:(NSEvent *)event {
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    NSUInteger index = [self characterIndexForInsertionAtPoint:point];
    if (index < self.string.length) {
        unichar c = [self.string characterAtIndex:index];
        if (c == 0x2610 || c == 0x2611) {
            NSRect glyph = [self firstRectForCharacterRange:NSMakeRange(index,1) actualRange:NULL];
            NSPoint screen = [self.window convertPointToScreen:event.locationInWindow];
            if (NSPointInRect(screen, glyph) && [self shouldChangeTextInRange:NSMakeRange(index,1) replacementString:c == 0x2610 ? @"☑" : @"☐"]) {
                [self.textStorage replaceCharactersInRange:NSMakeRange(index,1) withString:c == 0x2610 ? @"☑" : @"☐"];
                [self didChangeText]; return;
            }
        }
    }
    [super mouseDown:event];
}
@end

@implementation JNNote {
    JNBar *_bar;
    NSScrollView *_scroll;
    NSTimer *_saveTimer;
    BOOL _dirty, _contentDirty, _formatDirty, _layoutDirty, _ready, _collapsed;
    CGFloat _expandedHeight;
    NSInteger _palette, _color;
    BOOL _customInk;
}
- (instancetype)initWithRecord:(JNRecord *)record app:(JNApp *)app fresh:(BOOL)fresh error:(NSError **)error {
    NSDictionary *s = record.settings;
    CGFloat width = bounded(s[@"mac_width"] ?: s[@"width"],300,180,4096);
    CGFloat height = bounded(s[@"mac_height"] ?: s[@"height"],240,100,4096);
    NSRect frame = NSMakeRect(120 + app.notes.count*24,120 + app.notes.count*24,width,height);
    if (s[@"mac_x"] && s[@"mac_y"]) frame.origin = NSMakePoint([s[@"mac_x"] doubleValue],[s[@"mac_y"] doubleValue]);
    JNPanel *panel = [[JNPanel alloc] initWithContentRect:frame styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    if (!(self = [super initWithWindow:panel])) return nil;
    _record = record; _app = app; _expandedHeight = height;
    _palette = bounded(s[@"palette"],0,0,PALETTE_COUNT-1);
    _color = bounded(s[@"color"],0,0,PALETTE_SIZES[_palette]-1);
    _customInk = [s[@"custom_ink"] boolValue];
    panel.title = @"Jotila"; panel.level = NSFloatingWindowLevel;
    panel.hidesOnDeactivate = NO; panel.releasedWhenClosed = NO; panel.delegate = self;
    panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
    panel.minSize = NSMakeSize(180,100); panel.alphaValue = bounded(s[@"opacity"],100,60,100)/100.0;
    _bar = [[JNBar alloc] initWithFrame:NSMakeRect(0,height-32,width,32)];
    _bar.note = self; _bar.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    [panel.contentView addSubview:_bar];
    NSArray *labels = @[@"▦", @"−", @"×"];
    NSArray *actions = @[NSStringFromSelector(@selector(chooseDate:)),NSStringFromSelector(@selector(fold:)),NSStringFromSelector(@selector(close))];
    for (NSUInteger i=0; i<3; ++i) {
        NSButton *button = [NSButton buttonWithTitle:labels[i] target:self action:NSSelectorFromString(actions[i])];
        button.bordered = NO; button.frame = NSMakeRect(width-90+i*28,4,26,24);
        button.autoresizingMask = NSViewMinXMargin;
        button.toolTip = @[@"Assign a day",@"Fold or expand",@"Close and keep in archive"][i];
        [button setAccessibilityLabel:button.toolTip]; [_bar addSubview:button];
    }
    _scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0,0,width,height-32)];
    _scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable; _scroll.hasVerticalScroller = YES;
    JNText *editor = [[JNText alloc] initWithFrame:_scroll.contentView.bounds]; editor.note = self;
    editor.minSize = NSMakeSize(0,0); editor.maxSize = NSMakeSize(CGFLOAT_MAX,CGFLOAT_MAX);
    editor.verticallyResizable = YES; editor.horizontallyResizable = NO;
    editor.autoresizingMask = NSViewWidthSizable;
    editor.textContainer.containerSize = NSMakeSize(width,CGFLOAT_MAX); editor.textContainer.widthTracksTextView = YES;
    editor.textContainerInset = NSMakeSize(10,8); editor.richText = YES; editor.importsGraphics = NO;
    editor.allowsUndo = YES; editor.delegate = self;
    editor.automaticQuoteSubstitutionEnabled = NO; editor.automaticDashSubstitutionEnabled = NO;
    _editor = editor; _scroll.documentView = editor; [panel.contentView addSubview:_scroll];
    NSAttributedString *text = fresh ? [[NSAttributedString alloc] initWithString:@""] : [app.store read:record error:error];
    if (!text) return nil;
    [editor.textStorage setAttributedString:text]; if (!text.length) editor.font = [NSFont systemFontOfSize:14];
    [self applyTheme]; [editor.undoManager removeAllActions];
    NSRect visible = (NSScreen.mainScreen ?: NSScreen.screens.firstObject).visibleFrame;
    for (NSScreen *screen in NSScreen.screens) if (NSIntersectsRect(NSMakeRect(frame.origin.x,NSMaxY(frame)-32,width,32),screen.visibleFrame)) { visible = screen.visibleFrame; break; }
    frame.size.width = MIN(frame.size.width,visible.size.width); frame.size.height = MIN(frame.size.height,visible.size.height);
    frame.origin.x = MAX(NSMinX(visible),MIN(frame.origin.x,NSMaxX(visible)-frame.size.width));
    frame.origin.y = MAX(NSMinY(visible),MIN(frame.origin.y,NSMaxY(visible)-frame.size.height));
    [panel setFrame:frame display:NO]; _expandedHeight = frame.size.height;
    if ([s[@"collapsed"] boolValue]) [self fold:nil];
    _ready = YES; _layoutDirty = fresh;
    return self;
}
- (void)dealloc { [_saveTimer invalidate]; }
- (void)detach {
    _ready = NO; [_saveTimer invalidate]; _saveTimer = nil;
    [_editor.undoManager removeAllActions]; _editor.delegate = nil; self.window.delegate = nil;
    [self.window orderOut:nil];
}
- (void)applyTheme {
    NSColor *paper = JNColor(PALETTES[_palette][_color]), *ink = JNColor(palette_ink((int)_palette,(int)_color));
    self.window.backgroundColor = paper; _editor.backgroundColor = paper; _scroll.backgroundColor = paper;
    if (!_customInk) {
        [_editor.undoManager disableUndoRegistration];
        [_editor.textStorage addAttribute:NSForegroundColorAttributeName value:ink range:NSMakeRange(0,_editor.string.length)];
        [_editor.undoManager enableUndoRegistration];
    }
    _editor.insertionPointColor = ink;
    NSMutableDictionary *typing = [_editor.typingAttributes mutableCopy];
    if (!_customInk) typing[NSForegroundColorAttributeName] = ink;
    if (!typing[NSFontAttributeName]) typing[NSFontAttributeName] = [NSFont systemFontOfSize:14];
    _editor.typingAttributes = typing;
    _bar.paper = JNColor(palette_bar(PALETTES[_palette][_color]));
    _bar.ink = JNColor(palette_bar_ink((int)_palette,(int)_color));
    _bar.caption = _record.title.length ? _record.title : @"Jotila"; _bar.needsDisplay = YES;
    for (NSButton *button in _bar.subviews) button.contentTintColor = _bar.ink;
}
- (void)schedule {
    [_saveTimer invalidate];
    __weak JNNote *weakSelf = self;
    _saveTimer = [NSTimer scheduledTimerWithTimeInterval:0.4 repeats:NO block:^(NSTimer *timer) {
        (void)timer; NSError *error = nil; JNNote *note = weakSelf;
        if (note && ![note flush:&error]) { note->_bar.caption = @"Save failed — retry from menu"; note->_bar.needsDisplay = YES; JNError(error); }
    }];
}
- (void)textDidChange:(NSNotification *)notification { (void)notification; if (_ready) { _dirty = _contentDirty = _formatDirty = YES; [self schedule]; } }
- (void)windowDidResize:(NSNotification *)notification { (void)notification; [self geometryChanged]; }
- (void)windowDidMove:(NSNotification *)notification { (void)notification; [self geometryChanged]; }
- (void)geometryChanged { if (_ready) { _layoutDirty = YES; [self schedule]; } }
- (BOOL)flush:(NSError **)error {
    [_saveTimer invalidate]; _saveTimer = nil;
    if (!_dirty && !_layoutDirty) return YES;
    NSRect frame = self.window.frame;
    if (!_collapsed) _expandedHeight = frame.size.height;
    NSMutableDictionary *s = _record.settings;
    s[@"mac_x"] = @(frame.origin.x).stringValue; s[@"mac_y"] = @(frame.origin.y).stringValue;
    s[@"mac_width"] = @(frame.size.width).stringValue; s[@"mac_height"] = @(_expandedHeight).stringValue;
    s[@"open"] = @"1"; s[@"collapsed"] = _collapsed ? @"1" : @"0";
    s[@"palette"] = @(_palette).stringValue; s[@"color"] = @(_color).stringValue;
    s[@"custom_ink"] = _customInk ? @"1" : @"0";
    s[@"opacity"] = @((int)round(self.window.alphaValue*100)).stringValue;
    BOOL exists = [NSFileManager.defaultManager fileExistsAtPath:[self.app.store URLFor:_record extension:@"txt"].path];
    BOOL ok = _dirty ? [self.app.store save:_record text:_editor.textStorage writeText:_contentDirty writeRich:_formatDirty error:error] : !exists || [self.app.store saveMetadata:error];
    if (ok) { BOOL changed = _dirty; _dirty = _contentDirty = _formatDirty = _layoutDirty = NO; [self applyTheme]; if (changed) [self.app changed]; }
    return ok;
}
- (void)showAtPointer:(BOOL)pointer activate:(BOOL)activate {
    if (pointer) {
        NSPoint mouse = NSEvent.mouseLocation; NSRect f = self.window.frame;
        NSScreen *screen = NSScreen.mainScreen;
        for (NSScreen *candidate in NSScreen.screens) if (NSPointInRect(mouse,candidate.frame)) { screen = candidate; break; }
        NSRect v = screen.visibleFrame;
        f.origin = NSMakePoint(MAX(NSMinX(v),MIN(mouse.x,NSMaxX(v)-f.size.width)),MAX(NSMinY(v),MIN(mouse.y-f.size.height,NSMaxY(v)-f.size.height)));
        [self.window setFrame:f display:NO];
    }
    if (activate) { JNActivate(); [self.window makeKeyAndOrderFront:nil]; [self.window makeFirstResponder:_editor]; }
    else [self.window orderFront:nil];
}
- (BOOL)windowShouldClose:(NSWindow *)sender {
    (void)sender; NSError *error = nil;
    if (![self flush:&error]) { JNError(error); return NO; }
    _record.settings[@"open"] = @"0";
    if (![self.app.store saveMetadata:&error]) { _record.settings[@"open"] = @"1"; JNError(error); return NO; }
    [self detach]; [self.app closeNote:self]; return YES;
}
- (void)close { if ([self windowShouldClose:self.window]) [self.window close]; }
- (void)fold:(id)sender {
    (void)sender; NSRect f = self.window.frame;
    if (!_collapsed) _expandedHeight = f.size.height;
    CGFloat height = _collapsed ? _expandedHeight : 32;
    f.origin.y += f.size.height-height; f.size.height = height; _collapsed = !_collapsed;
    _scroll.hidden = _collapsed; self.window.minSize = NSMakeSize(180,_collapsed ? 32 : 100);
    [self.window setFrame:f display:YES]; [self geometryChanged];
}
- (void)rename:(id)sender {
    (void)sender; NSString *title = JNAsk(@"Note title",_record.title);
    if (!title) return;
    _record.title = [[JNPlain(title) componentsSeparatedByString:@"\n"] componentsJoinedByString:@" "];
    _dirty = _contentDirty = YES; [self applyTheme]; [self schedule];
}
- (void)tags:(id)sender {
    (void)sender; NSString *input = JNAsk(@"Tags, separated by semicolons",_record.tags);
    if (!input) return; NSError *error = nil;
    NSString *tags = JNNormalizeTags(input,&error);
    if (!tags) { JNError(error); return; }
    _record.tags = tags; _dirty = YES; [self schedule];
}
- (void)chooseDate:(id)sender {
    (void)sender; NSAlert *alert = [NSAlert new]; alert.messageText = @"Assign a day";
    [alert addButtonWithTitle:@"Save"]; [alert addButtonWithTitle:@"Remove day"]; [alert addButtonWithTitle:@"Cancel"];
    NSDatePicker *picker = [[NSDatePicker alloc] initWithFrame:NSMakeRect(0,0,300,170)];
    picker.datePickerStyle = NSClockAndCalendarDatePickerStyle; picker.datePickerElements = NSYearMonthDayDatePickerElementFlag;
    NSCalendar *calendar = [[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
    picker.calendar = calendar;
    if (_record.day) { NSDateComponents *c = [NSDateComponents new]; c.year = _record.day/10000; c.month = _record.day/100%100; c.day = _record.day%100; picker.dateValue = [calendar dateFromComponents:c]; }
    else picker.dateValue = NSDate.date;
    alert.accessoryView = picker; NSModalResponse response = [alert runModal];
    if (response == NSAlertThirdButtonReturn) return;
    NSDateComponents *c = [calendar components:NSCalendarUnitYear|NSCalendarUnitMonth|NSCalendarUnitDay fromDate:picker.dateValue];
    unsigned day = response == NSAlertSecondButtonReturn ? 0 : (unsigned)(c.year*10000+c.month*100+c.day);
    if (day && !date_valid(day)) { NSBeep(); return; }
    _record.day = day; _dirty = YES; [self schedule];
}
- (void)replaceRange:(NSRange)range with:(NSAttributedString *)replacement {
    NSAttributedString *before = [_editor.textStorage attributedSubstringFromRange:range];
    NSUInteger nextLength = replacement.length;
    [[_editor.undoManager prepareWithInvocationTarget:self] replaceRange:NSMakeRange(range.location,nextLength) with:before];
    [_editor.textStorage replaceCharactersInRange:range withAttributedString:replacement];
    [_editor setSelectedRange:NSMakeRange(range.location,nextLength)]; _dirty = _formatDirty = YES; [self schedule];
}
- (void)format:(id)sender {
    NSRange range = _editor.selectedRange; NSInteger command = [sender tag];
    if (!range.length) { NSBeep(); return; }
    NSMutableAttributedString *selection = [[_editor.textStorage attributedSubstringFromRange:range] mutableCopy];
    if (command == 3 || command == 4) {
        NSString *key = command == 3 ? NSUnderlineStyleAttributeName : NSStrikethroughStyleAttributeName;
        NSInteger value = [[selection attribute:key atIndex:0 effectiveRange:NULL] integerValue] ? 0 : NSUnderlineStyleSingle;
        [selection addAttribute:key value:@(value) range:NSMakeRange(0,selection.length)];
    } else {
        NSFontManager *manager = NSFontManager.sharedFontManager; NSFontTraitMask trait = command == 1 ? NSBoldFontMask : NSItalicFontMask;
        NSFont *first = [selection attribute:NSFontAttributeName atIndex:0 effectiveRange:NULL] ?: [NSFont systemFontOfSize:14];
        BOOL remove = ([manager traitsOfFont:first] & trait) != 0;
        [selection enumerateAttribute:NSFontAttributeName inRange:NSMakeRange(0,selection.length) options:0 usingBlock:^(NSFont *font, NSRange run, BOOL *stop) {
            (void)stop; font = font ?: first;
            [selection addAttribute:NSFontAttributeName value:remove ? [manager convertFont:font toNotHaveTrait:trait] : [manager convertFont:font toHaveTrait:trait] range:run];
        }];
    }
    [self replaceRange:range with:selection];
}
- (void)resizeBy:(CGFloat)amount {
    NSRange range = _editor.selectedRange;
    if (range.length) {
        NSMutableAttributedString *text = [[_editor.textStorage attributedSubstringFromRange:range] mutableCopy];
        [text enumerateAttribute:NSFontAttributeName inRange:NSMakeRange(0,text.length) options:0 usingBlock:^(NSFont *font, NSRange run, BOOL *stop) {
            (void)stop; font = font ?: [NSFont systemFontOfSize:14];
            [text addAttribute:NSFontAttributeName value:[NSFontManager.sharedFontManager convertFont:font toSize:MAX(6,MIN(144,font.pointSize+amount*2))] range:run];
        }];
        [self replaceRange:range with:text];
    } else {
        NSRect f = self.window.frame; CGFloat factor = amount > 0 ? 1.1 : 1/1.1;
        CGFloat width = MAX(180,MIN(4096,f.size.width*factor));
        CGFloat height = _collapsed ? 32 : MAX(100,MIN(4096,f.size.height*width/f.size.width));
        if (_collapsed) _expandedHeight = MAX(100,MIN(4096,_expandedHeight*width/f.size.width));
        f.origin.y += f.size.height-height; f.size = NSMakeSize(width,height); [self.window setFrame:f display:YES]; [self geometryChanged];
    }
}
- (BOOL)textView:(NSTextView *)text doCommandBySelector:(SEL)selector {
    if (selector != @selector(insertNewline:)) return NO;
    NSRange selection = text.selectedRange;
    NSRange line = [text.string lineRangeForRange:NSMakeRange(selection.location,0)];
    NSString *prefix = [text.string substringWithRange:NSMakeRange(line.location,selection.location-line.location)];
    BOOL shift = (NSApp.currentEvent.modifierFlags & NSEventModifierFlagShift) != 0;
    if (shift && [prefix hasPrefix:@"#"] && prefix.length > 1 && line.location == 0) {
        _record.title = [prefix substringFromIndex:1];
        [text insertText:@"" replacementRange:NSMakeRange(0,prefix.length)]; [self applyTheme]; _dirty = YES; [self schedule]; return YES;
    }
    if (shift && [prefix hasPrefix:@"*"]) { [text insertText:[@"☐ " stringByAppendingString:[prefix substringFromIndex:1]] replacementRange:NSMakeRange(line.location,prefix.length)]; return YES; }
    if ([prefix hasPrefix:@"☐ "] || [prefix hasPrefix:@"☑ "]) {
        if (prefix.length == 2) [text insertText:@"" replacementRange:NSMakeRange(line.location,2)];
        else [text insertText:@"\n☐ " replacementRange:selection];
        return YES;
    }
    return NO;
}
- (void)selectColor:(NSMenuItem *)item {
    NSInteger palette = item.tag/COLOR_COUNT, color = item.tag%COLOR_COUNT;
    NSRange range = _editor.selectedRange;
    if (range.length) {
        NSMutableAttributedString *text = [[_editor.textStorage attributedSubstringFromRange:range] mutableCopy];
        [text addAttribute:NSForegroundColorAttributeName value:JNColor(PALETTES[palette][color]) range:NSMakeRange(0,text.length)];
        _customInk = YES; [self replaceRange:range with:text];
    } else { _palette = palette; _color = color; [self applyTheme]; _dirty = _layoutDirty = YES; if (!_customInk) _formatDirty = YES; [self schedule]; }
}
- (void)favorite:(NSMenuItem *)item {
    NSInteger palette = item.tag;
    NSString *previous = self.app.store.preferences[@"favorite_palette"];
    self.app.store.preferences[@"favorite_palette"] = @(palette).stringValue;
    NSError *error = nil;
    if (![self.app.store saveMetadata:&error]) {
        if (previous) self.app.store.preferences[@"favorite_palette"] = previous;
        else [self.app.store.preferences removeObjectForKey:@"favorite_palette"];
        JNError(error); return;
    }
    if (palette >= 0) { if (_palette != palette) _color = 0; _palette = palette; _dirty = _layoutDirty = YES; if (!_customInk) _formatDirty = YES; [self applyTheme]; [self schedule]; }
}
- (void)opacity:(NSMenuItem *)item { self.window.alphaValue = item.tag/100.0; [self geometryChanged]; }
- (void)retry:(id)sender { (void)sender; NSError *error = nil; if (![self flush:&error]) JNError(error); }
- (void)exportCopy:(id)sender {
    (void)sender; NSSavePanel *panel = [NSSavePanel savePanel]; panel.nameFieldStringValue = @"Note copy.rtf";
    if ([panel runModal] != NSModalResponseOK) return;
    NSData *data = [_editor.textStorage RTFFromRange:NSMakeRange(0,_editor.string.length) documentAttributes:@{}];
    NSError *error = nil; if (!data || !JNWrite(data,panel.URL,&error)) JNError(error);
}
- (NSMenu *)contextMenu {
    NSMenu *menu = [NSMenu new];
    NSArray *names = @[@"Rename…",@"Tags…",@"Assign day…",@"Fold / expand",@"Save now",@"Export an RTF copy…"];
    SEL actions[] = {@selector(rename:),@selector(tags:),@selector(chooseDate:),@selector(fold:),@selector(retry:),@selector(exportCopy:)};
    for (NSUInteger i=0;i<names.count;++i) { NSMenuItem *item = [menu addItemWithTitle:names[i] action:actions[i] keyEquivalent:@""]; item.target = self; }
    [menu addItem:NSMenuItem.separatorItem];
    NSArray *styles = @[@"Bold",@"Italic",@"Underline",@"Strikethrough"];
    for (NSUInteger i=0;i<styles.count;++i) { NSMenuItem *item = [menu addItemWithTitle:styles[i] action:@selector(format:) keyEquivalent:@""]; item.target = self; item.tag = (NSInteger)i+1; }
    NSMenu *palettes = [NSMenu new]; NSMenuItem *root = [menu addItemWithTitle:_editor.selectedRange.length ? @"Text color" : @"Note palette" action:nil keyEquivalent:@""]; root.submenu = palettes;
    for (NSInteger p=0;p<PALETTE_COUNT;++p) {
        NSMenuItem *entry = [palettes addItemWithTitle:paletteName(p) action:nil keyEquivalent:@""]; NSMenu *colors = [NSMenu new]; entry.submenu = colors;
        for (NSInteger c=0;c<PALETTE_SIZES[p];++c) {
            NSMenuItem *item = [colors addItemWithTitle:[NSString stringWithFormat:@"Color %ld",(long)c+1] action:@selector(selectColor:) keyEquivalent:@""];
            item.target = self; item.tag = p*COLOR_COUNT+c;
            NSColor *swatch = JNColor(PALETTES[p][c]);
            item.image = [NSImage imageWithSize:NSMakeSize(16,16) flipped:NO drawingHandler:^BOOL(NSRect rect) { [swatch setFill]; NSRectFill(rect); return YES; }];
        }
        [colors addItem:NSMenuItem.separatorItem];
        NSMenuItem *star = [colors addItemWithTitle:@"Use as favorite" action:@selector(favorite:) keyEquivalent:@""]; star.tag = p; star.target = self;
    }
    NSMenuItem *clear = [palettes addItemWithTitle:@"Clear favorite" action:@selector(favorite:) keyEquivalent:@""]; clear.tag = -1; clear.target = self;
    NSMenu *opacity = [NSMenu new]; [menu addItemWithTitle:@"Opacity" action:nil keyEquivalent:@""].submenu = opacity;
    for (NSInteger percent=100;percent>=60;percent-=10) { NSMenuItem *item = [opacity addItemWithTitle:[NSString stringWithFormat:@"%ld%%",(long)percent] action:@selector(opacity:) keyEquivalent:@""]; item.target = self; item.tag = percent; }
    NSMenuItem *archive = [menu addItemWithTitle:@"Note explorer" action:@selector(browse:) keyEquivalent:@""]; archive.target = self.app;
    return menu;
}
@end
