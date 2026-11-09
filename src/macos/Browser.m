#import "App.h"
#import "Search.h"
#import "Calendar.h"

@interface JNBrowser () <NSCollectionViewDataSource, NSCollectionViewDelegate, NSTableViewDataSource, NSTableViewDelegate, NSSearchFieldDelegate, NSWindowDelegate>
- (void)layoutViews;
- (void)openSelected:(id)sender;
- (void)trashSelected:(id)sender;
- (void)closeDay;
- (void)filterTag:(NSString *)tag;
@end
@interface JNBrowserContent : NSView
@property(nonatomic, weak) JNBrowser *browser;
@end
@implementation JNBrowserContent
- (void)resizeSubviewsWithOldSize:(NSSize)oldSize { [super resizeSubviewsWithOldSize:oldSize]; [self.browser layoutViews]; }
- (void)drawRect:(NSRect)dirty { [JNColor(RGB(244,238,226)) setFill]; NSRectFill(dirty); }
@end
@interface JNCard : NSView
@property(nonatomic, copy) NSDictionary *entry, *preview;
@property(nonatomic, weak) JNBrowser *browser;
@property(nonatomic) BOOL selected;
@end
@implementation JNCard
- (BOOL)isFlipped { return YES; }
- (void)setEntry:(NSDictionary *)entry { _entry=entry; self.needsDisplay=YES; self.accessibilityLabel=entry[@"title"] ?: @"Note"; self.accessibilityRole=NSAccessibilityButtonRole; }
- (void)setPreview:(NSDictionary *)preview { _preview=preview; self.needsDisplay=YES; self.accessibilityLabel=[NSString stringWithFormat:@"%@ %@",preview[@"title"] ?: @"",preview[@"body"] ?: @""]; }
- (void)setSelected:(BOOL)selected { _selected=selected; self.needsDisplay=YES; }
- (void)mouseDown:(NSEvent *)event {
    NSPoint point=[self convertPoint:event.locationInWindow fromView:nil];
    if(point.y>=self.bounds.size.height-27 && point.y<self.bounds.size.height-9) {
        CGFloat x=14;
        for(NSString *tag in [_entry[@"tags"] componentsSeparatedByString:@";"]) {
            NSString *label=[@"#" stringByAppendingString:tag]; CGFloat width=[label sizeWithAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:11]}].width;
            if(tag.length && point.x>=x && point.x<x+width) { [self.browser filterTag:tag]; return; } x+=width+6;
        }
    }
    [super mouseDown:event];
}
- (void)drawRect:(NSRect)dirty {
    (void)dirty; NSInteger p=MAX(0,MIN(PALETTE_COUNT-1,[_entry[@"palette"] integerValue])); NSInteger c=MAX(0,MIN(PALETTE_SIZES[p]-1,[_entry[@"color"] integerValue]));
    NSRect card=NSInsetRect(self.bounds,4,4),bar=NSMakeRect(card.origin.x,card.origin.y,card.size.width,28);
    [JNColor(PALETTES[p][c]) setFill]; NSRectFill(card); [JNColor(palette_bar(PALETTES[p][c])) setFill]; NSRectFill(bar);
    [(_selected ? JNColor(palette_ink((int)p,(int)c)) : NSColor.blackColor) setStroke]; NSBezierPath *border=[NSBezierPath bezierPathWithRect:NSInsetRect(card,0.5,0.5)]; border.lineWidth=_selected?2:0.5; [border stroke];
    NSMutableParagraphStyle *single=[NSMutableParagraphStyle new]; single.lineBreakMode=NSLineBreakByTruncatingTail;
    NSDictionary *titleStyle=@{NSFontAttributeName:JNFont(12,YES),NSForegroundColorAttributeName:JNColor(palette_bar_ink((int)p,(int)c)),NSParagraphStyleAttributeName:single};
    NSString *title=_preview[@"title"] ?: _entry[@"title"] ?: @"";
    [title drawInRect:NSInsetRect(bar,9,5) withAttributes:titleStyle];
    NSDictionary *bodyStyle=@{NSFontAttributeName:JNFont(12,NO),NSForegroundColorAttributeName:JNColor(palette_ink((int)p,(int)c))};
    NSString *body=_entry[@"excerpt"] ?: _preview[@"body"] ?: @"";
    NSMutableAttributedString *rich=[[NSMutableAttributedString alloc] initWithString:body attributes:bodyStyle];
    for(NSString *term in _entry[@"terms"]) {
        NSRange search=NSMakeRange(0,body.length);
        while(search.length) { NSRange hit=[body rangeOfString:term options:NSCaseInsensitiveSearch|NSDiacriticInsensitiveSearch range:search]; if(hit.location==NSNotFound || !hit.length) break;
            [rich addAttribute:NSBackgroundColorAttributeName value:[JNColor(palette_ink((int)p,(int)c)) colorWithAlphaComponent:0.18] range:hit]; search=NSMakeRange(NSMaxRange(hit),body.length-NSMaxRange(hit)); }
    }
    [rich drawWithRect:NSMakeRect(card.origin.x+10,NSMaxY(bar)+7,card.size.width-20,card.size.height-65) options:NSStringDrawingUsesLineFragmentOrigin|NSStringDrawingTruncatesLastVisibleLine];
    NSString *tags=[_entry[@"tags"] stringByReplacingOccurrencesOfString:@";" withString:@"  #"];
    if(tags.length) tags=[@"#" stringByAppendingString:tags];
    [tags drawInRect:NSMakeRect(card.origin.x+10,NSMaxY(card)-23,card.size.width-20,18) withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:11],NSForegroundColorAttributeName:JNColor(palette_ink((int)p,(int)c)),NSParagraphStyleAttributeName:single}];
}
@end
@interface JNCardItem : NSCollectionViewItem @end
@implementation JNCardItem
- (void)loadView { self.view=[[JNCard alloc] initWithFrame:NSMakeRect(0,0,230,180)]; }
- (void)setSelected:(BOOL)selected { [super setSelected:selected]; ((JNCard *)self.view).selected=selected; }
@end
@interface JNCards : NSCollectionView
@property(nonatomic, weak) JNBrowser *browser;
@end
@implementation JNCards
- (void)mouseDown:(NSEvent *)event { [super mouseDown:event]; if(!(event.modifierFlags&(NSEventModifierFlagCommand|NSEventModifierFlagShift)) && self.selectionIndexPaths.count==1) [self.browser openSelected:nil]; }
- (void)keyDown:(NSEvent *)event {
    if(event.keyCode==36 || event.keyCode==76) { [self.browser openSelected:nil]; return; }
    if(event.keyCode==51 || event.keyCode==117) { [self.browser trashSelected:nil]; return; }
    if(event.keyCode==96) { [self.browser.app reload:nil]; return; }
    [super keyDown:event];
}
- (NSMenu *)menuForEvent:(NSEvent *)event { (void)event; NSMenu *menu=[NSMenu new]; NSMenuItem *item=[menu addItemWithTitle:@"Move selected notes to Trash" action:@selector(trashSelected:) keyEquivalent:@""]; item.target=self.browser; return menu; }
@end
@interface JNDayTable : NSTableView
@property(nonatomic, weak) JNBrowser *browser;
@end
@implementation JNDayTable
- (void)keyDown:(NSEvent *)event {
    if(event.keyCode==53) { [self.browser closeDay]; return; }
    if(event.keyCode==36 || event.keyCode==76) { [self.browser openSelected:self]; return; }
    if(event.keyCode==115 || event.keyCode==119 || event.keyCode==116 || event.keyCode==121) {
        NSInteger row=event.keyCode==115?0:event.keyCode==119?self.numberOfRows-1:self.selectedRow+(event.keyCode==116?-8:8);
        if(self.numberOfRows) { row=MAX(0,MIN(self.numberOfRows-1,row)); [self selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row] byExtendingSelection:NO]; [self scrollRowToVisible:row]; } return;
    }
    [super keyDown:event];
}
@end

@implementation JNBrowser {
    NSSearchField *_search;
    JNCards *_cards;
    NSScrollView *_scroll,*_sidebarScroll,*_calendarScroll;
    NSTableView *_sidebar;
    NSSegmentedControl *_modes;
    NSButton *_archiveButton,*_calendarButton,*_openButton,*_trashButton,*_previousYear,*_nextYear,*_today;
    NSTextField *_status,*_year;
    JNCalendar *_calendar;
    NSMutableArray<NSDictionary *> *_results;
    NSArray<NSDictionary *> *_filtered,*_sidebarItems,*_dayItems;
    NSDictionary<NSString *,NSNumber *> *_recordIndices;
    NSDictionary<NSString *,JNRecord *> *_records;
    NSOperationQueue *_queue,*_previewQueue;
    NSCache<NSString *,NSDictionary *> *_previewCache;
    NSMutableSet<NSString *> *_pendingPreviews;
    NSTimer *_debounce;
    NSUInteger _generation, _cacheEpoch, _previewLoads;
    BOOL _calendarVisible,_needsRefresh,_rebuilding;
    NSString *_sidebarFilter;
    NSPopover *_dayPopover;
    JNDayTable *_dayTable;
}
- (instancetype)initWithApp:(JNApp *)app {
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(180,160,850,640) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable|NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    if(!(self=[super initWithWindow:window])) return nil;
    _app=app; window.appearance=[NSAppearance appearanceNamed:NSAppearanceNameAqua]; window.title=@"Jotilla — Notes archive"; window.minSize=NSMakeSize(660,480); window.delegate=self; window.releasedWhenClosed=NO;
    window.level=NSFloatingWindowLevel; window.collectionBehavior=NSWindowCollectionBehaviorMoveToActiveSpace|NSWindowCollectionBehaviorFullScreenAuxiliary;
    JNBrowserContent *content=[[JNBrowserContent alloc] initWithFrame:window.contentView.bounds]; content.browser=self; window.contentView=content;
    _queue=[NSOperationQueue new]; _queue.maxConcurrentOperationCount=1; _queue.qualityOfService=NSQualityOfServiceUserInitiated;
    _previewQueue=[NSOperationQueue new]; _previewQueue.maxConcurrentOperationCount=1; _previewQueue.qualityOfService=NSQualityOfServiceUtility;
    _previewCache=[NSCache new]; _previewCache.countLimit=256; _previewCache.totalCostLimit=1024*1024; _pendingPreviews=[NSMutableSet set];
    _search=[[NSSearchField alloc] initWithFrame:NSZeroRect]; _search.placeholderString=@"Search notes, #tags, \"phrases\", -#"; _search.delegate=self; _search.drawsBackground=YES; _search.backgroundColor=NSColor.whiteColor; [content addSubview:_search];
    _modes=[NSSegmentedControl segmentedControlWithLabels:@[@"Dates",@"Tags"] trackingMode:NSSegmentSwitchTrackingSelectOne target:self action:@selector(sidebarMode:)]; _modes.selectedSegment=0; [content addSubview:_modes];
    _archiveButton=[NSButton buttonWithTitle:@"Archive…" target:app action:@selector(chooseArchive:)]; [content addSubview:_archiveButton];
    _calendarButton=[NSButton buttonWithImage:[[NSImage imageWithSystemSymbolName:@"calendar" accessibilityDescription:@"Calendar"] imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithPointSize:16 weight:NSFontWeightMedium]] target:self action:@selector(toggleCalendar:)];
    _calendarButton.title=@""; _calendarButton.buttonType=NSButtonTypePushOnPushOff; _calendarButton.imagePosition=NSImageOnly;
    _calendarButton.bezelStyle=NSBezelStyleToolbar; _calendarButton.imageScaling=NSImageScaleNone;
    _calendarButton.toolTip=@"Calendar / notes"; [_calendarButton setAccessibilityLabel:_calendarButton.toolTip]; [content addSubview:_calendarButton];
    _sidebarScroll=[[NSScrollView alloc] initWithFrame:NSZeroRect]; _sidebarScroll.hasVerticalScroller=YES; _sidebarScroll.drawsBackground=YES; _sidebarScroll.backgroundColor=JNColor(RGB(235,226,209));
    _sidebar=[[NSTableView alloc] initWithFrame:NSZeroRect]; _sidebar.headerView=nil; _sidebar.rowHeight=28; _sidebar.backgroundColor=_sidebarScroll.backgroundColor; _sidebar.delegate=self; _sidebar.dataSource=self;
    NSTableColumn *column=[[NSTableColumn alloc] initWithIdentifier:@"filter"]; column.width=168; [_sidebar addTableColumn:column]; _sidebarScroll.documentView=_sidebar; [content addSubview:_sidebarScroll];
    _scroll=[[NSScrollView alloc] initWithFrame:NSZeroRect]; _scroll.hasVerticalScroller=YES; _scroll.drawsBackground=NO;
    _cards=[[JNCards alloc] initWithFrame:NSZeroRect]; _cards.browser=self; _cards.dataSource=self; _cards.delegate=self; _cards.selectable=YES; _cards.allowsMultipleSelection=YES; _cards.backgroundColors=@[JNColor(RGB(244,238,226))];
    NSCollectionViewFlowLayout *flow=[NSCollectionViewFlowLayout new]; flow.minimumInteritemSpacing=4; flow.minimumLineSpacing=4; flow.sectionInset=NSEdgeInsetsMake(0,0,8,0); _cards.collectionViewLayout=flow;
    [_cards registerClass:JNCardItem.class forItemWithIdentifier:@"note"]; _scroll.documentView=_cards; [content addSubview:_scroll];
    _calendarScroll=[[NSScrollView alloc] initWithFrame:NSZeroRect]; _calendarScroll.hasVerticalScroller=YES; _calendarScroll.hidden=YES;
    _calendar=[[JNCalendar alloc] initWithFrame:NSMakeRect(0,0,800,580)]; _calendar.year=[[[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian] component:NSCalendarUnitYear fromDate:NSDate.date];
    __weak JNBrowser *weakSelf=self; _calendar.choose=^(NSUInteger day,NSRect rect) { [weakSelf chooseDay:day rect:rect]; }; _calendarScroll.documentView=_calendar; [content addSubview:_calendarScroll];
    _year=[[NSTextField alloc] initWithFrame:NSZeroRect]; _year.stringValue=[NSString stringWithFormat:@"%ld",(long)_calendar.year]; _year.target=self; _year.action=@selector(changeYear:); _year.alignment=NSTextAlignmentCenter; [_year setAccessibilityLabel:@"Calendar year"]; [content addSubview:_year];
    _calendar.yearChanged=^(NSInteger year) { JNBrowser *b=weakSelf; if(b) b->_year.stringValue=[NSString stringWithFormat:@"%ld",(long)year]; };
    _previousYear=[NSButton buttonWithTitle:@"‹" target:self action:@selector(yearStep:)]; _previousYear.tag=-1;
    _nextYear=[NSButton buttonWithTitle:@"›" target:self action:@selector(yearStep:)]; _nextYear.tag=1;
    _today=[NSButton buttonWithTitle:@"Today" target:self action:@selector(today:)];
    for(NSView *v in @[_previousYear,_nextYear,_today]) [content addSubview:v];
    _status=[NSTextField labelWithString:@""]; _status.font=[NSFont systemFontOfSize:11]; _status.textColor=JNColor(RGB(58,52,43)); [content addSubview:_status];
    _openButton=[NSButton buttonWithTitle:@"Open" target:self action:@selector(openSelected:)]; _trashButton=[NSButton buttonWithTitle:@"Trash" target:self action:@selector(trashSelected:)]; [content addSubview:_openButton]; [content addSubview:_trashButton];
    _needsRefresh=YES; [self layoutViews];
    [window setFrame:JNConstrainFrame(window.frame,NSScreen.mainScreen.visibleFrame) display:NO]; return self;
}
- (void)layoutViews {
    NSSize size=self.window.contentView.bounds.size; CGFloat w=size.width,h=size.height,sidebar=176;
    _modes.frame=NSMakeRect(10,h-39,sidebar,28); _search.frame=NSMakeRect(_calendarVisible?10:sidebar+20,h-39,w-(_calendarVisible?150:sidebar+160),28);
    _archiveButton.frame=NSMakeRect(w-132,h-40,88,30); _calendarButton.frame=NSMakeRect(w-39,h-40,30,30);
    _sidebarScroll.frame=NSMakeRect(10,40,sidebar,h-88); _sidebar.tableColumns.firstObject.width=sidebar;
    _scroll.frame=NSMakeRect(sidebar+20,40,w-sidebar-30,h-88);
    NSCollectionViewFlowLayout *flow=(NSCollectionViewFlowLayout *)_cards.collectionViewLayout;
    CGFloat available=_scroll.contentSize.width; NSInteger columns=MAX(1,(NSInteger)(available/220)); flow.itemSize=NSMakeSize(floor((available-(columns-1)*4)/columns),182);
    _cards.frame=NSMakeRect(0,0,available,MAX(_scroll.contentSize.height,flow.collectionViewContentSize.height));
    _calendarScroll.frame=NSMakeRect(10,40,w-20,h-124); NSInteger cols=_calendarScroll.contentSize.width>=960?4:3;
    _calendar.frame=NSMakeRect(0,0,_calendarScroll.contentSize.width,MAX(_calendarScroll.contentSize.height,12/cols*128));
    _previousYear.frame=NSMakeRect(w/2-104,h-78,32,28); _year.frame=NSMakeRect(w/2-68,h-76,82,24); _nextYear.frame=NSMakeRect(w/2+18,h-78,32,28); _today.frame=NSMakeRect(w/2+58,h-78,70,28);
    _status.frame=NSMakeRect(14,12,w-200,18); _openButton.frame=NSMakeRect(w-178,6,78,28); _trashButton.frame=NSMakeRect(w-94,6,84,28);
    _sidebarScroll.hidden=_modes.hidden=_scroll.hidden=_calendarVisible;
    _calendarScroll.hidden=_year.hidden=_previousYear.hidden=_nextYear.hidden=_today.hidden=!_calendarVisible;
    _openButton.hidden=_trashButton.hidden=_calendarVisible;
}
- (void)dealloc { [_queue cancelAllOperations]; [_previewQueue cancelAllOperations]; [_debounce invalidate]; }
- (void)showWindow:(id)sender {
    [self.window deminiaturize:nil]; [super showWindow:sender]; [self.window makeKeyAndOrderFront:nil];
    if(_needsRefresh) [self refresh];
}
- (void)showNotes { if(_calendarVisible) [self toggleCalendar:nil]; }
- (BOOL)windowShouldClose:(NSWindow *)sender { [self closeDay]; [sender orderOut:nil]; [_queue cancelAllOperations]; [_previewQueue cancelAllOperations]; [_pendingPreviews removeAllObjects]; ++_generation; _needsRefresh=YES; return NO; }
- (void)windowDidMiniaturize:(NSNotification *)notification { (void)notification; [self windowShouldClose:self.window]; }
- (void)focusSearch { [self showNotes]; _sidebarFilter=nil; [self showWindow:nil]; [self.window makeFirstResponder:_search]; [_search selectText:nil]; }
- (void)invalidate { ++_cacheEpoch; [_previewQueue cancelAllOperations]; [_pendingPreviews removeAllObjects]; [_previewCache removeAllObjects]; [self refresh]; }
- (void)controlTextDidChange:(NSNotification *)notification {
    if(notification.object!=_search) return; [_debounce invalidate]; __weak JNBrowser *weakSelf=self;
    _debounce=[NSTimer scheduledTimerWithTimeInterval:0.15 repeats:NO block:^(NSTimer *timer) { (void)timer; [weakSelf refresh]; }];
}
- (void)filterTag:(NSString *)tag { _modes.selectedSegment=1; _sidebarFilter=tag; [self showNotes]; [self refresh]; }
- (void)sidebarMode:(id)sender { (void)sender; _sidebarFilter=nil; [self refresh]; }
- (void)rebuildSidebar:(NSArray *)snapshot {
    NSMutableArray *items=[NSMutableArray arrayWithObject:@{@"name":@"All notes",@"filter":@""}]; NSMutableDictionary<NSString *,NSNumber *> *counts=[NSMutableDictionary dictionary];
    if(_modes.selectedSegment==1) {
        NSUInteger untagged=0;
        for(NSDictionary *entry in snapshot) { NSString *tags=entry[@"tags"]; if(!tags.length) ++untagged; else for(NSString *tag in [tags componentsSeparatedByString:@";"]) counts[tag]=@([counts[tag] unsignedIntegerValue]+1); }
        [items addObject:@{@"name":[NSString stringWithFormat:@"Untagged (%lu)",(unsigned long)untagged],@"filter":@"-#"}];
        for(NSString *tag in [counts.allKeys sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)]) [items addObject:@{@"name":[NSString stringWithFormat:@"#%@ (%@)",tag,counts[tag]],@"filter":tag}];
    } else {
        for(NSDictionary *entry in snapshot) { NSString *month=[NSString stringWithFormat:@"%06lu",(unsigned long)([entry[@"created"] unsignedIntegerValue]/100)]; counts[month]=@([counts[month] unsignedIntegerValue]+1); }
        NSString *year=@""; NSDateFormatter *formatter=[NSDateFormatter new]; formatter.locale=NSLocale.currentLocale;
        for(NSString *month in [[counts.allKeys sortedArrayUsingSelector:@selector(compare:)] reverseObjectEnumerator]) {
            NSString *y=[month substringToIndex:4]; if(![year isEqual:y]) { year=y; [items addObject:@{@"name":y,@"filter":y}]; }
            NSInteger index=[[month substringFromIndex:4] integerValue]-1;
            if(index>=0 && index<12) [items addObject:@{@"name":[NSString stringWithFormat:@"   %@ (%@)",formatter.monthSymbols[index],counts[month]],@"filter":month}];
        }
    }
    _sidebarItems=items; _rebuilding=YES; [_sidebar reloadData]; NSUInteger selected=[items indexOfObjectPassingTest:^BOOL(NSDictionary *item,NSUInteger i,BOOL *stop) { (void)i;(void)stop;return [item[@"filter"] isEqual:self->_sidebarFilter ?: @""]; }];
    if(selected==NSNotFound) { selected=0; _sidebarFilter=nil; } [_sidebar selectRowIndexes:[NSIndexSet indexSetWithIndex:selected] byExtendingSelection:NO]; _rebuilding=NO;
}
- (NSDictionary *)entryForRecord:(JNRecord *)r {
    return @{@"id":r.identifier,@"tags":r.tags,@"day":@(r.day),@"created":@(r.createdDay),@"title":r.title,@"titled":@([r.settings[@"has_title"] boolValue]),@"palette":r.settings[@"palette"] ?: @"0",@"color":r.settings[@"color"] ?: @"0",@"cache":[NSString stringWithFormat:@"%lu:%@:%lu",(unsigned long)_cacheEpoch,r.identifier,(unsigned long)r.revision]};
}
- (void)recordChanged:(JNRecord *)record {
    if(!self.window.visible) { _needsRefresh=YES; return; }
    NSNumber *index=_recordIndices[record.identifier];
    if(!record.persisted || !index || _search.stringValue.length || _sidebarFilter.length || ![_results[index.unsignedIntegerValue][@"tags"] isEqual:record.tags]) { [self refresh]; return; }
    NSDictionary *previous=_results[index.unsignedIntegerValue], *entry=[self entryForRecord:record];
    _results[index.unsignedIntegerValue]=entry; _filtered=_results;
    if(![previous[@"day"] isEqual:entry[@"day"]]) {
        NSMutableDictionary *counts=[_calendar.counts mutableCopy]; NSNumber *old=previous[@"day"], *day=entry[@"day"];
        if(old.unsignedIntegerValue) { NSUInteger count=[counts[old] unsignedIntegerValue]; if(count<=1) [counts removeObjectForKey:old]; else counts[old]=@(count-1); }
        if(day.unsignedIntegerValue) counts[day]=@([counts[day] unsignedIntegerValue]+1); _calendar.counts=counts;
        if(_dayPopover.shown) [self closeDay];
    }
    for(JNCardItem *item in _cards.visibleItems) { JNCard *card=(JNCard *)item.view; if([card.entry[@"id"] isEqual:record.identifier]) { card.entry=entry; card.preview=[_previewCache objectForKey:entry[@"cache"]]; [self requestPreview:entry]; } }
}
- (void)refresh {
    _needsRefresh=YES; if(!self.window.visible) return; _needsRefresh=NO;
    [_debounce invalidate]; [_queue cancelAllOperations]; NSUInteger generation=++_generation;
    JNQuery *query=[[JNQuery alloc] initWithText:_search.stringValue];
    if(!query) { _filtered=@[]; [self applyResults]; _status.stringValue=@"Use at most 16 terms and close quoted phrases."; return; }
    NSMutableArray *snapshot=[NSMutableArray array]; NSMutableDictionary *records=[NSMutableDictionary dictionary];
    for(JNRecord *r in self.app.store.records) {
        records[r.identifier]=r;
        [snapshot addObject:[self entryForRecord:r]];
    }
    _records=records; [self rebuildSidebar:snapshot]; NSMutableArray *candidates=[NSMutableArray array];
    for(NSDictionary *entry in snapshot) {
        if(![query acceptsTags:entry[@"tags"]]) continue;
        if(_sidebarFilter.length && !_calendarVisible) {
            if(_modes.selectedSegment==1) { NSArray *tags=[entry[@"tags"] componentsSeparatedByString:@";"]; if([_sidebarFilter isEqual:@"-#"] ? [entry[@"tags"] length]!=0 : ![tags containsObject:_sidebarFilter]) continue; }
            else if(![[entry[@"created"] stringValue] hasPrefix:_sidebarFilter]) continue;
        }
        [candidates addObject:entry];
    }
    if(!query.requiresText) { _filtered=candidates; [self applyResults]; return; }
    _status.stringValue=@"Searching…"; NSURL *directory=self.app.store.directory; __weak JNBrowser *weakSelf=self;
    NSBlockOperation *operation=[NSBlockOperation new]; __weak NSBlockOperation *weakOperation=operation;
    [operation addExecutionBlock:^{
        NSMutableArray *hits=[NSMutableArray array]; NSUInteger unreadable=0;
        for(NSDictionary *entry in candidates) { @autoreleasepool {
            if(weakOperation.cancelled) return;
            NSError *error=nil; NSURL *url=[directory URLByAppendingPathComponent:[entry[@"id"] stringByAppendingPathExtension:@"txt"]];
            NSString *excerpt=nil;
            if([query matchesFile:url cancelled:^BOOL { return weakOperation.cancelled; } excerpt:&excerpt error:&error]) { NSMutableDictionary *hit=[entry mutableCopy]; if(excerpt) hit[@"excerpt"]=excerpt; hit[@"terms"]=query.terms; [hits addObject:hit]; } else if(error) ++unreadable;
        } }
        if(weakOperation.cancelled) return;
        dispatch_async(dispatch_get_main_queue(),^{ JNBrowser *b=weakSelf; if(!b || generation!=b->_generation) return; b->_filtered=hits; [b applyResults]; if(unreadable) b->_status.stringValue=[b->_status.stringValue stringByAppendingFormat:@" — %lu unreadable files",(unsigned long)unreadable]; });
    }]; [_queue addOperation:operation];
}
- (void)applyResults {
    NSMutableSet *selection=[NSMutableSet set]; for(NSIndexPath *index in _cards.selectionIndexPaths) if(index.item<(NSInteger)_results.count) [selection addObject:_results[index.item][@"id"]];
    _results=[_filtered mutableCopy]; NSMutableDictionary *indices=[NSMutableDictionary dictionaryWithCapacity:_results.count];
    [_results enumerateObjectsUsingBlock:^(NSDictionary *entry,NSUInteger i,BOOL *stop) { (void)stop; indices[entry[@"id"]]=@(i); }]; _recordIndices=indices;
    NSMutableDictionary *counts=[NSMutableDictionary dictionary];
    for(NSDictionary *entry in _results) if([entry[@"day"] unsignedIntegerValue]) counts[entry[@"day"]]=@([counts[entry[@"day"]] unsignedIntegerValue]+1);
    _calendar.counts=counts; [_cards reloadData]; NSMutableSet *paths=[NSMutableSet set];
    [_results enumerateObjectsUsingBlock:^(NSDictionary *entry,NSUInteger i,BOOL *stop) { (void)stop; if([selection containsObject:entry[@"id"]]) [paths addObject:[NSIndexPath indexPathForItem:i inSection:0]]; }]; _cards.selectionIndexPaths=paths;
    _status.stringValue=_results.count ? [NSString stringWithFormat:@"%lu %@",(unsigned long)_results.count,_results.count==1?@"note":@"notes"] : @"No matching notes"; [self layoutViews];
}
- (NSInteger)collectionView:(NSCollectionView *)view numberOfItemsInSection:(NSInteger)section { (void)view;(void)section;return (NSInteger)_results.count; }
- (NSCollectionViewItem *)collectionView:(NSCollectionView *)view itemForRepresentedObjectAtIndexPath:(NSIndexPath *)path {
    JNCardItem *item=[view makeItemWithIdentifier:@"note" forIndexPath:path]; JNCard *card=(JNCard *)item.view; NSDictionary *entry=_results[path.item];
    card.entry=entry; card.browser=self; card.preview=[_previewCache objectForKey:entry[@"cache"]]; [self requestPreview:entry]; return item;
}
- (void)requestPreview:(NSDictionary *)entry {
    NSString *key=entry[@"cache"]; if([_previewCache objectForKey:key] || [_pendingPreviews containsObject:key]) return;
    ++_previewLoads; [_pendingPreviews addObject:key]; NSURL *url=[self.app.store.directory URLByAppendingPathComponent:[entry[@"id"] stringByAppendingPathExtension:@"txt"]]; __weak JNBrowser *weakSelf=self;
    [_previewQueue addOperationWithBlock:^{ @autoreleasepool {
        NSFileHandle *file=[NSFileHandle fileHandleForReadingFromURL:url error:NULL]; NSData *data=[file readDataUpToLength:2048 error:NULL]; [file closeAndReturnError:NULL]; NSString *text=nil;
        for(NSUInteger trim=0;trim<4 && trim<=data.length && !text && data;++trim) text=[[NSString alloc] initWithBytes:data.bytes length:data.length-trim encoding:NSUTF8StringEncoding];
        text=JNPlain(text ?: @"Preview unavailable"); if([text hasPrefix:@"\uFEFF"]) text=[text substringFromIndex:1];
        NSString *title=entry[@"title"]; if([entry[@"titled"] boolValue] && [text hasPrefix:@"#"]) { NSRange newline=[text rangeOfString:@"\n"]; NSUInteger end=newline.location==NSNotFound?text.length:newline.location; title=[text substringWithRange:NSMakeRange(1,end-1)]; text=newline.location==NSNotFound?@"":[text substringFromIndex:end+1]; }
        NSDictionary *preview=@{@"title":title ?: @"",@"body":text};
        dispatch_async(dispatch_get_main_queue(),^{ JNBrowser *b=weakSelf; if(!b) return; [b->_pendingPreviews removeObject:key]; [b->_previewCache setObject:preview forKey:key cost:(text.length+title.length)*2];
            for(JNCardItem *item in b->_cards.visibleItems) { JNCard *card=(JNCard *)item.view; if([card.entry[@"cache"] isEqual:key]) card.preview=preview; }
            if(b->_dayPopover.shown) [b->_dayTable reloadData];
        });
    } }];
}
- (NSInteger)numberOfRowsInTableView:(NSTableView *)table { return (NSInteger)(table==_sidebar?_sidebarItems.count:_dayItems.count); }
- (NSView *)tableView:(NSTableView *)table viewForTableColumn:(NSTableColumn *)column row:(NSInteger)row {
    NSTextField *field=[table makeViewWithIdentifier:column.identifier owner:self]; if(!field) { field=[NSTextField labelWithString:@""]; field.identifier=column.identifier; field.lineBreakMode=NSLineBreakByTruncatingTail; field.font=[NSFont systemFontOfSize:12]; }
    if(table==_sidebar) { field.stringValue=_sidebarItems[(NSUInteger)row][@"name"]; field.textColor=JNColor(RGB(58,52,43)); }
    else { NSDictionary *entry=_dayItems[(NSUInteger)row],*preview=[_previewCache objectForKey:entry[@"cache"]]; NSString *title=preview[@"title"]; if(!title.length) title=[preview[@"body"] componentsSeparatedByString:@"\n"].firstObject; field.stringValue=title.length?title:@"Untitled"; [self requestPreview:entry]; }
    return field;
}
- (void)tableViewSelectionDidChange:(NSNotification *)notification { if(notification.object==_sidebar && !_rebuilding && _sidebar.selectedRow>=0) { _sidebarFilter=_sidebarItems[(NSUInteger)_sidebar.selectedRow][@"filter"]; [self refresh]; } }
- (void)toggleCalendar:(id)sender { (void)sender; [self closeDay]; _calendarVisible=!_calendarVisible; _calendarButton.state=_calendarVisible?NSControlStateValueOn:NSControlStateValueOff; [self layoutViews]; [self refresh]; }
- (void)changeYear:(id)sender { (void)sender; _calendar.year=_year.integerValue; _year.stringValue=[NSString stringWithFormat:@"%ld",(long)_calendar.year]; [self closeDay]; }
- (void)yearStep:(NSButton *)sender { _year.integerValue=_calendar.year+sender.tag; [self changeYear:nil]; }
- (void)today:(id)sender { (void)sender; NSDateComponents *c=[[[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian] components:NSCalendarUnitYear|NSCalendarUnitMonth|NSCalendarUnitDay fromDate:NSDate.date]; _calendar.year=c.year; _year.stringValue=[NSString stringWithFormat:@"%ld",(long)c.year]; _calendar.selected=(NSUInteger)(c.year*10000+c.month*100+c.day); [_calendar scrollRectToVisible:[_calendar cell:(unsigned)_calendar.selected]]; }
- (void)chooseDay:(NSUInteger)day rect:(NSRect)rect {
    [self closeDay]; NSMutableArray *items=[NSMutableArray array]; for(NSDictionary *entry in _results) if([entry[@"day"] unsignedIntegerValue]==day) [items addObject:entry];
    if(items.count==1) { [self.app openRecord:_records[items[0][@"id"]] activate:YES]; return; } if(!items.count) return; _dayItems=items;
    NSViewController *controller=[NSViewController new]; NSScrollView *scroll=[[NSScrollView alloc] initWithFrame:NSMakeRect(0,0,300,MIN(300,items.count*28+8))]; scroll.hasVerticalScroller=YES;
    _dayTable=[[JNDayTable alloc] initWithFrame:scroll.bounds]; _dayTable.browser=self; _dayTable.headerView=nil; _dayTable.rowHeight=28; _dayTable.delegate=self; _dayTable.dataSource=self; _dayTable.target=self; _dayTable.doubleAction=@selector(openSelected:);
    NSTableColumn *column=[[NSTableColumn alloc] initWithIdentifier:@"day"]; column.width=280; [_dayTable addTableColumn:column]; scroll.documentView=_dayTable; controller.view=scroll;
    _dayPopover=[NSPopover new]; _dayPopover.behavior=NSPopoverBehaviorTransient; _dayPopover.contentViewController=controller; [_dayPopover showRelativeToRect:rect ofView:_calendar preferredEdge:NSMaxYEdge];
    [_dayTable selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO]; [_dayTable.window makeFirstResponder:_dayTable];
}
- (void)closeDay { [_dayPopover close]; _dayPopover=nil; _dayItems=nil; if(_calendarVisible) [self.window makeFirstResponder:_calendar]; }
- (void)openSelected:(id)sender {
    if(sender==_dayTable) { NSInteger row=_dayTable.selectedRow; if(row>=0 && row<(NSInteger)_dayItems.count) { JNRecord *record=_records[_dayItems[(NSUInteger)row][@"id"]]; [self closeDay]; [self.app openRecord:record activate:YES]; } return; }
    for(NSIndexPath *path in _cards.selectionIndexPaths) if(path.item<(NSInteger)_results.count) [self.app openRecord:_records[_results[path.item][@"id"]] activate:YES];
}
- (void)trashSelected:(id)sender {
    (void)sender; NSMutableArray *records=[NSMutableArray array]; for(NSIndexPath *path in _cards.selectionIndexPaths) if(path.item<(NSInteger)_results.count) [records addObject:_records[_results[path.item][@"id"]]]; if(!records.count) return;
    NSAlert *alert=[NSAlert new]; alert.messageText=[NSString stringWithFormat:@"Move %lu note(s) to Trash?",(unsigned long)records.count]; [alert addButtonWithTitle:@"Cancel"]; [alert addButtonWithTitle:@"Move to Trash"]; if([alert runModal]!=NSAlertSecondButtonReturn) return;
    for(JNRecord *record in records) { NSError *error=nil; JNNote *note=self.app.notes[record.identifier]; if((note && ![note flush:&error]) || ![self.app.store remove:record error:&error]) { JNError(error); break; } [note detach]; [self.app.notes removeObjectForKey:record.identifier]; }
    [self refresh];
}
@end
