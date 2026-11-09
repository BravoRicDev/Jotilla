#import "App.h"
#import "Search.h"

@interface JNCalendar : NSView
@property(nonatomic) NSInteger year;
@property(nonatomic) NSUInteger selected;
@property(nonatomic, copy) NSDictionary<NSNumber *, NSNumber *> *counts;
@property(nonatomic, copy) void (^choose)(NSUInteger);
@end
@implementation JNCalendar
- (BOOL)isFlipped { return YES; }
- (NSRect)cell:(unsigned)day {
    unsigned month=day/100%100, number=day%100;
    CGFloat width=self.bounds.size.width/3, height=self.bounds.size.height/4;
    int offset=date_weekday((unsigned)(_year*10000+month*100+1));
    NSUInteger index=(NSUInteger)offset+number-1;
    return NSMakeRect((month-1)%3*width+10+index%7*(width-20)/7,
                      (month-1)/3*height+42+index/7*(height-50)/6,(width-20)/7,(height-50)/6);
}
- (void)drawRect:(NSRect)dirty {
    [NSColor.windowBackgroundColor setFill]; NSRectFill(NSIntersectionRect(dirty,self.bounds));
    NSDictionary *label=@{NSFontAttributeName:[NSFont systemFontOfSize:11],NSForegroundColorAttributeName:NSColor.labelColor};
    NSDateFormatter *formatter=[NSDateFormatter new]; formatter.locale=NSLocale.currentLocale;
    CGFloat width=self.bounds.size.width/3,height=self.bounds.size.height/4;
    for (unsigned month=1;month<=12;++month) {
        [formatter.monthSymbols[month-1] drawAtPoint:NSMakePoint((month-1)%3*width+12,(month-1)/3*height+6) withAttributes:@{NSFontAttributeName:[NSFont boldSystemFontOfSize:13],NSForegroundColorAttributeName:NSColor.labelColor}];
        for (NSUInteger d=0;d<7;++d) [@[@"M",@"T",@"W",@"T",@"F",@"S",@"S"][d] drawAtPoint:NSMakePoint((month-1)%3*width+16+d*(width-20)/7,(month-1)/3*height+26) withAttributes:label];
        for (int day=1;day<=date_days((int)_year,(int)month);++day) {
            unsigned date=(unsigned)(_year*10000+month*100+(unsigned)day); NSRect rect=[self cell:date];
            if (!NSIntersectsRect(rect,dirty)) continue;
            if (date==_selected) { [NSColor.selectedContentBackgroundColor setFill]; [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(rect,1,1) xRadius:8 yRadius:8] fill]; }
            else if ([_counts[@(date)] unsignedIntegerValue]) { [NSColor.controlAccentColor setStroke]; [[NSBezierPath bezierPathWithOvalInRect:NSInsetRect(rect,2,2)] stroke]; }
            [[NSString stringWithFormat:@"%d",day] drawAtPoint:NSMakePoint(rect.origin.x+5,rect.origin.y+3) withAttributes:label];
        }
    }
}
- (void)mouseDown:(NSEvent *)event {
    NSPoint point=[self convertPoint:event.locationInWindow fromView:nil];
    for (unsigned month=1;month<=12;++month) for (int day=1;day<=date_days((int)_year,(int)month);++day) {
        unsigned date=(unsigned)(_year*10000+month*100+(unsigned)day);
        if (NSPointInRect(point,[self cell:date])) { self.selected=self.selected==date ? 0 : date; self.needsDisplay=YES; if (self.choose) self.choose(self.selected); return; }
    }
}
@end

@interface JNBrowser () <NSTableViewDataSource, NSTableViewDelegate, NSSearchFieldDelegate, NSWindowDelegate>
@end
@implementation JNBrowser {
    NSSearchField *_search;
    NSTableView *_table;
    NSScrollView *_scroll;
    NSTextField *_status, *_year;
    JNCalendar *_calendar;
    NSArray<NSDictionary *> *_results;
    NSArray<NSDictionary *> *_filtered;
    NSDictionary<NSString *,JNRecord *> *_records;
    NSOperationQueue *_queue;
    NSTimer *_debounce;
    NSUInteger _generation, _selectedDay;
    BOOL _calendarVisible, _needsRefresh;
}
- (instancetype)initWithApp:(JNApp *)app {
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(200,180,850,680)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable|NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    if (!(self=[super initWithWindow:window])) return nil;
    _app=app; window.title=@"Jotila — Note explorer"; window.minSize=NSMakeSize(650,500); window.delegate=self;
    _queue=[NSOperationQueue new]; _queue.maxConcurrentOperationCount=1; _queue.qualityOfService=NSQualityOfServiceUserInitiated;
    _search=[[NSSearchField alloc] initWithFrame:NSMakeRect(14,638,430,28)]; _search.placeholderString=@"Search notes, #tags, \"phrases\", -#";
    _search.delegate=self; _search.autoresizingMask=NSViewWidthSizable|NSViewMinYMargin; [window.contentView addSubview:_search];
    NSArray *labels=@[@"New",@"Archive…",@"Calendar"];
    SEL actions[]={@selector(newNote:),@selector(chooseArchive:),@selector(toggleCalendar:)};
    for (NSUInteger i=0;i<3;++i) {
        NSButton *button=[NSButton buttonWithTitle:labels[i] target:i==2 ? self : app action:actions[i]];
        button.frame=NSMakeRect(454+i*125,638,118,28); button.autoresizingMask=NSViewMinXMargin|NSViewMinYMargin; [window.contentView addSubview:button];
    }
    _scroll=[[NSScrollView alloc] initWithFrame:NSMakeRect(14,42,822,584)]; _scroll.hasVerticalScroller=YES; _scroll.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    _table=[[NSTableView alloc] initWithFrame:_scroll.bounds]; _table.delegate=self; _table.dataSource=self; _table.target=self; _table.doubleAction=@selector(openSelected:); _table.rowHeight=28;
    for (NSString *name in @[@"Note",@"Tags",@"Day"]) { NSTableColumn *column=[[NSTableColumn alloc] initWithIdentifier:name]; column.title=name; column.width=[name isEqual:@"Note"] ? 440 : 170; [_table addTableColumn:column]; }
    _scroll.documentView=_table; [window.contentView addSubview:_scroll];
    _calendar=[[JNCalendar alloc] initWithFrame:NSMakeRect(14,220,822,366)]; _calendar.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable; _calendar.hidden=YES;
    NSCalendar *civil=[[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian]; _calendar.year=[civil component:NSCalendarUnitYear fromDate:NSDate.date];
    __weak JNBrowser *weakSelf=self;
    _calendar.choose=^(NSUInteger day) { JNBrowser *browser=weakSelf; if (!browser) return; browser->_selectedDay=day; [browser applyDay]; if (day && browser->_results.count==1) { [browser->_table selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO]; [browser openSelected:nil]; } };
    [window.contentView addSubview:_calendar];
    _year=[[NSTextField alloc] initWithFrame:NSMakeRect(14,596,90,26)]; _year.integerValue=_calendar.year; _year.target=self; _year.action=@selector(changeYear:); _year.autoresizingMask=NSViewMinYMargin; _year.hidden=YES; [window.contentView addSubview:_year];
    _status=[NSTextField labelWithString:@""]; _status.frame=NSMakeRect(14,12,580,22); _status.autoresizingMask=NSViewWidthSizable; [window.contentView addSubview:_status];
    NSButton *open=[NSButton buttonWithTitle:@"Open" target:self action:@selector(openSelected:)]; open.frame=NSMakeRect(640,8,90,28); open.autoresizingMask=NSViewMinXMargin; [window.contentView addSubview:open];
    NSButton *remove=[NSButton buttonWithTitle:@"Trash" target:self action:@selector(trashSelected:)]; remove.frame=NSMakeRect(736,8,100,28); remove.autoresizingMask=NSViewMinXMargin; [window.contentView addSubview:remove];
    _needsRefresh=YES; return self;
}
- (void)dealloc { [_queue cancelAllOperations]; [_debounce invalidate]; }
- (void)showWindow:(id)sender { [super showWindow:sender]; if (_needsRefresh) [self refresh]; }
- (BOOL)windowShouldClose:(NSWindow *)sender { [sender orderOut:nil]; [_queue cancelAllOperations]; ++_generation; _needsRefresh=YES; return NO; }
- (void)windowDidMiniaturize:(NSNotification *)notification { (void)notification; [self.window orderOut:nil]; }
- (void)focusSearch { [self showWindow:nil]; [self.window makeFirstResponder:_search]; }
- (void)controlTextDidChange:(NSNotification *)notification {
    if (notification.object!=_search) return;
    [_debounce invalidate]; __weak JNBrowser *weakSelf=self;
    _debounce=[NSTimer scheduledTimerWithTimeInterval:0.18 repeats:NO block:^(NSTimer *timer) { (void)timer; [weakSelf refresh]; }];
}
- (void)refresh {
    _needsRefresh=YES;
    if (!self.window.visible) return;
    _needsRefresh=NO; [_queue cancelAllOperations]; NSUInteger generation=++_generation;
    JNQuery *query=[[JNQuery alloc] initWithText:_search.stringValue];
    if (!query) { _status.stringValue=@"Use at most 16 search terms and close quoted phrases."; return; }
    NSMutableArray *snapshot=[NSMutableArray array]; NSMutableDictionary *records=[NSMutableDictionary dictionary];
    for (JNRecord *record in self.app.store.records) {
        records[record.identifier]=record;
        [snapshot addObject:@{@"id":record.identifier,@"tags":record.tags,@"day":@(record.day),@"titled":@([record.settings[@"has_title"] boolValue]),@"title":record.title}];
    }
    _records=records;
    NSURL *directory=self.app.store.directory; _status.stringValue=@"Searching…";
    __weak JNBrowser *weakSelf=self;
    NSBlockOperation *operation=[NSBlockOperation new]; __weak NSBlockOperation *weakOperation=operation;
    [operation addExecutionBlock:^{
        NSMutableArray *hits=[NSMutableArray array]; NSUInteger unreadable=0;
        for (NSDictionary *item in snapshot) { @autoreleasepool {
            if (weakOperation.cancelled) return;
            if (![query acceptsTags:item[@"tags"]]) continue;
            NSURL *url=[directory URLByAppendingPathComponent:[item[@"id"] stringByAppendingPathExtension:@"txt"]];
            NSError *error=nil;
            if (![query matchesFile:url cancelled:^BOOL { return weakOperation.cancelled; } error:&error]) { if (error) ++unreadable; continue; }
            /* One bounded preview, never the full note body. */
            NSFileHandle *file=[NSFileHandle fileHandleForReadingFromURL:url error:&error];
            if (!file) { ++unreadable; continue; }
            NSData *data=[file readDataUpToLength:1024 error:&error]; [file closeAndReturnError:NULL];
            if (!data) { ++unreadable; continue; }
            NSString *preview=nil;
            for (NSUInteger trim=0;trim<4 && trim<=data.length && !preview;++trim)
                preview=[[NSString alloc] initWithBytes:data.bytes length:data.length-trim encoding:NSUTF8StringEncoding];
            NSString *title=item[@"title"];
            if (!title.length) title=[[JNPlain(preview ?: @"") componentsSeparatedByString:@"\n"] firstObject];
            if ([item[@"titled"] boolValue] && [title hasPrefix:@"#"]) title=[title substringFromIndex:1];
            NSMutableDictionary *hit=[item mutableCopy]; hit[@"title"]=title.length ? title : @"Untitled"; [hits addObject:hit];
        } }
        if (weakOperation.cancelled) return;
        dispatch_async(dispatch_get_main_queue(), ^{
            JNBrowser *browser=weakSelf; if (!browser || generation!=browser->_generation) return;
            browser->_filtered=hits; [browser applyDay];
            if (unreadable) browser->_status.stringValue=[browser->_status.stringValue stringByAppendingFormat:@" — %lu unreadable files",(unsigned long)unreadable];
        });
    }];
    [_queue addOperation:operation];
}
- (void)applyDay {
    NSMutableArray *rows=[NSMutableArray array]; NSMutableDictionary *counts=[NSMutableDictionary dictionary];
    for (NSDictionary *item in _filtered) { NSNumber *day=item[@"day"]; if (day.unsignedIntegerValue) counts[day]=@([counts[day] unsignedIntegerValue]+1); if (!_selectedDay || day.unsignedIntegerValue==_selectedDay) [rows addObject:item]; }
    _results=rows; _calendar.counts=counts; _calendar.needsDisplay=YES; [_table reloadData];
    _status.stringValue=[NSString stringWithFormat:@"%lu notes%@",(unsigned long)rows.count,_selectedDay ? @" on the selected day" : @""];
}
- (void)toggleCalendar:(id)sender {
    (void)sender; _calendarVisible=!_calendarVisible; _calendar.hidden=_year.hidden=!_calendarVisible;
    CGFloat height=self.window.contentView.bounds.size.height;
    _scroll.frame=NSMakeRect(14,42,self.window.contentView.bounds.size.width-28,_calendarVisible ? 164 : height-96);
    _scroll.autoresizingMask=NSViewWidthSizable|(_calendarVisible ? 0 : NSViewHeightSizable);
    if (!_calendarVisible) { _selectedDay=0; _calendar.selected=0; [self applyDay]; }
}
- (void)changeYear:(id)sender { (void)sender; _calendar.year=MAX(DATE_MIN_YEAR,MIN(DATE_MAX_YEAR,_year.integerValue)); _year.integerValue=_calendar.year; _calendar.needsDisplay=YES; }
- (NSInteger)numberOfRowsInTableView:(NSTableView *)table { (void)table; return (NSInteger)_results.count; }
- (NSView *)tableView:(NSTableView *)table viewForTableColumn:(NSTableColumn *)column row:(NSInteger)row {
    NSTextField *field=[table makeViewWithIdentifier:column.identifier owner:self];
    if (!field) { field=[NSTextField labelWithString:@""]; field.identifier=column.identifier; field.lineBreakMode=NSLineBreakByTruncatingTail; }
    NSDictionary *item=_results[(NSUInteger)row];
    if ([column.identifier isEqual:@"Note"]) field.stringValue=item[@"title"];
    else if ([column.identifier isEqual:@"Tags"]) field.stringValue=item[@"tags"];
    else { NSUInteger day=[item[@"day"] unsignedIntegerValue]; field.stringValue=day ? [NSString stringWithFormat:@"%04lu-%02lu-%02lu",(unsigned long)(day/10000),(unsigned long)(day/100%100),(unsigned long)(day%100)] : @""; }
    return field;
}
- (void)openSelected:(id)sender { (void)sender; NSInteger row=_table.selectedRow; if (row>=0 && (NSUInteger)row<_results.count) [self.app openRecord:_records[_results[(NSUInteger)row][@"id"]] activate:YES]; }
- (void)trashSelected:(id)sender {
    (void)sender; NSInteger row=_table.selectedRow; if (row<0 || (NSUInteger)row>=_results.count) return;
    JNRecord *record=_records[_results[(NSUInteger)row][@"id"]];
    NSAlert *alert=[NSAlert new]; alert.messageText=@"Move this note to Trash?"; [alert addButtonWithTitle:@"Move to Trash"]; [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal]!=NSAlertFirstButtonReturn) return;
    NSError *error=nil; JNNote *note=self.app.notes[record.identifier];
    if (note && ![note flush:&error]) { JNError(error); return; }
    if (![self.app.store remove:record error:&error]) { JNError(error); return; }
    [note detach]; [self.app.notes removeObjectForKey:record.identifier]; [self refresh];
}
@end
