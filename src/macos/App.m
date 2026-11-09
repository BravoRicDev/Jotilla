#import "App.h"
#import <Carbon/Carbon.h>
#import <ServiceManagement/ServiceManagement.h>

static OSStatus hotkey(EventHandlerCallRef handler, EventRef event, void *context) {
    (void)handler; (void)event;
    JNApp *app = (__bridge JNApp *)context;
    [app newNote:@"pointer"]; return noErr;
}
@implementation JNApp {
    NSStatusItem *_status;
    JNBrowser *_browser;
    EventHotKeyRef _hotkey;
    EventHandlerRef _handler;
    BOOL _launched, _quitting;
    NSURL *_configuration;
}
- (instancetype)init { if ((self=[super init])) _notes=[NSMutableDictionary dictionary]; return self; }
- (NSURL *)pickDirectory {
    NSOpenPanel *panel=[NSOpenPanel openPanel]; panel.canChooseDirectories=YES; panel.canChooseFiles=NO;
    panel.canCreateDirectories=YES; panel.treatsFilePackagesAsDirectories=YES;
    panel.message=self.portable ? @"Choose the portable notes folder. Notes and settings stay in this folder." : @"Open or create a notes archive. The current archive is retained.";
    return [panel runModal]==NSModalResponseOK ? panel.URL : nil;
}
- (void)applicationWillFinishLaunching:(NSNotification *)notification {
    (void)notification;
    self.portable=self.portable || [[NSBundle.mainBundle objectForInfoDictionaryKey:@"JotilaPortable"] boolValue];
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification; NSError *error=nil;
    NSAppleEventDescriptor *launch = [NSAppleEventManager.sharedAppleEventManager.currentAppleEvent paramDescriptorForKeyword:keyAEPropData];
    self.startup = self.startup || launch.enumCodeValue == keyAELaunchedAsLogInItem;
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    NSURL *directory=self.initialDirectory;
    if (!self.portable) {
        NSURL *support=[NSFileManager.defaultManager URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:&error];
        NSURL *root=[support URLByAppendingPathComponent:@"Jotila" isDirectory:YES];
        if (!support || ![NSFileManager.defaultManager createDirectoryAtURL:root withIntermediateDirectories:YES attributes:nil error:&error]) { JNError(error); [NSApp terminate:nil]; return; }
        _configuration=[root URLByAppendingPathComponent:@"archive.path"];
        if (!directory) {
            NSString *saved=[NSString stringWithContentsOfURL:_configuration encoding:NSUTF8StringEncoding error:NULL];
            saved=[saved stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
            if (saved.length) {
                directory=[NSURL fileURLWithPath:saved isDirectory:YES];
                BOOL isDirectory=NO;
                if (![NSFileManager.defaultManager fileExistsAtPath:directory.path isDirectory:&isDirectory] || !isDirectory) {
                    JNError([NSError errorWithDomain:@"Jotila" code:1 userInfo:@{NSLocalizedDescriptionKey:@"The configured archive is unavailable. Reconnect its drive. Its location has not been changed."}]); [NSApp terminate:nil]; return;
                }
            } else directory=[root URLByAppendingPathComponent:@"notes" isDirectory:YES];
        }
    }
    if (!directory) directory=[self pickDirectory];
    if (!directory) { [NSApp terminate:nil]; return; }
    self.store=[[JNStore alloc] initWithDirectory:directory error:&error];
    if (!self.store) { JNError(error); [NSApp terminate:nil]; return; }
    [self installMenu];
    EventTypeSpec spec={kEventClassKeyboard,kEventHotKeyPressed};
    OSStatus status=InstallEventHandler(GetApplicationEventTarget(),hotkey,1,&spec,(__bridge void *)self,&_handler);
    if (status==noErr) status=RegisterEventHotKey(kVK_ANSI_N,controlKey|optionKey|cmdKey,(EventHotKeyID){0x4a6f746c,1},GetApplicationEventTarget(),0,&_hotkey);
    if (status!=noErr) NSLog(@"Jotila: global shortcut unavailable (%d); use the menu bar item.",(int)status);
    /* Restoring an existing session never activates the app or creates a note. */
    [self restore]; _launched=YES;
    if (!self.store.records.count && !self.startup) [self newNote:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(screenChanged:) name:NSApplicationDidChangeScreenParametersNotification object:nil];
}
- (void)restore {
    for (JNRecord *record in self.store.records)
        if (!record.settings[@"open"] || [record.settings[@"open"] boolValue]) [self openRecord:record activate:NO];
}
- (void)installMenu {
    NSMenu *main=[NSMenu new], *appMenu=[NSMenu new], *file=[NSMenu new], *edit=[NSMenu new], *format=[NSMenu new];
    NSArray *menus=@[appMenu,file,edit,format]; NSArray *titles=@[@"Jotila",@"File",@"Edit",@"Format"];
    for (NSUInteger i=0;i<menus.count;++i) [main addItemWithTitle:titles[i] action:nil keyEquivalent:@""].submenu=menus[i];
    [appMenu addItemWithTitle:@"About Jotila" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    NSMenuItem *login=[appMenu addItemWithTitle:@"Start at login" action:@selector(toggleLogin:) keyEquivalent:@""]; login.target=self;
    login.state=SMAppService.mainAppService.status==SMAppServiceStatusEnabled ? NSControlStateValueOn : NSControlStateValueOff;
    [appMenu addItem:NSMenuItem.separatorItem]; [appMenu addItemWithTitle:@"Quit Jotila" action:@selector(terminate:) keyEquivalent:@"q"];
    NSArray *commands=@[@"New note",@"Note explorer",@"Search notes",@"Open archive…",@"Reload external edits"];
    NSArray *keys=@[@"n",@"e",@"f",@"o",@"r"];
    SEL actions[]={@selector(newNote:),@selector(browse:),@selector(search:),@selector(chooseArchive:),@selector(reload:)};
    for (NSUInteger i=0;i<commands.count;++i) { NSMenuItem *item=[file addItemWithTitle:commands[i] action:actions[i] keyEquivalent:keys[i]]; item.target=self; }
    NSArray *editNames=@[@"Undo",@"Redo",@"Cut",@"Copy",@"Paste",@"Select all"];
    NSArray *editKeys=@[@"z",@"Z",@"x",@"c",@"v",@"a"];
    SEL editActions[]={@selector(undo:),@selector(redo:),@selector(cut:),@selector(copy:),@selector(paste:),@selector(selectAll:)};
    for (NSUInteger i=0;i<editNames.count;++i) [edit addItemWithTitle:editNames[i] action:editActions[i] keyEquivalent:editKeys[i]];
    NSArray *styleNames=@[@"Bold",@"Italic",@"Underline",@"Strikethrough",@"Increase size",@"Decrease size",@"Tags…",@"Assign day…",@"Rename…",@"Fold / expand"];
    NSArray *styleKeys=@[@"b",@"i",@"u",@"X",@"+",@"-",@"t",@"d",@"",@""];
    for (NSUInteger i=0;i<styleNames.count;++i) { NSMenuItem *item=[format addItemWithTitle:styleNames[i] action:@selector(noteCommand:) keyEquivalent:styleKeys[i]]; item.target=self; item.tag=(NSInteger)i+1; }
    NSApp.mainMenu=main;
    _status=[NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength];
    _status.button.title=@"ι"; _status.button.toolTip=@"Jotila"; [_status.button setAccessibilityLabel:@"Jotila notes"];
    NSMenu *tray=[file copy]; [tray addItem:NSMenuItem.separatorItem]; [tray addItemWithTitle:@"Quit Jotila" action:@selector(terminate:) keyEquivalent:@"q"]; _status.menu=tray;
}
- (BOOL)validateMenuItem:(NSMenuItem *)item {
    if (item.action==@selector(toggleLogin:)) return !self.portable && [NSBundle.mainBundle.bundlePath hasPrefix:@"/Applications/"];
    if (item.action==@selector(noteCommand:)) return [NSApp.keyWindow.windowController isKindOfClass:JNNote.class];
    return YES;
}
- (void)toggleLogin:(NSMenuItem *)item {
    NSError *error=nil; SMAppService *service=SMAppService.mainAppService;
    BOOL ok=service.status==SMAppServiceStatusEnabled ? [service unregisterAndReturnError:&error] : [service registerAndReturnError:&error];
    if (!ok) JNError(error);
    item.state=service.status==SMAppServiceStatusEnabled ? NSControlStateValueOn : NSControlStateValueOff;
    if (service.status==SMAppServiceStatusRequiresApproval) [SMAppService openSystemSettingsLoginItems];
}
- (void)noteCommand:(NSMenuItem *)item {
    JNNote *note=(JNNote *)NSApp.keyWindow.windowController;
    if (![note isKindOfClass:JNNote.class]) return;
    if (item.tag<=4) [note format:item];
    else if (item.tag<=6) [note resizeBy:item.tag==5 ? 1 : -1];
    else if (item.tag==7) [note tags:nil];
    else if (item.tag==8) [note chooseDate:nil];
    else if (item.tag==9) [note rename:nil];
    else [note fold:nil];
}
- (void)newNote:(id)sender {
    if (!self.store) return;
    JNRecord *record=[self.store newRecord];
    NSString *favorite=self.store.preferences[@"favorite_palette"];
    NSInteger palette=favorite ? favorite.integerValue : 0;
    if (palette<0 || palette>=PALETTE_COUNT) palette=0;
    record.settings[@"palette"]=@(palette).stringValue;
    record.settings[@"color"]=@(self.notes.count%PALETTE_SIZES[palette]).stringValue;
    NSError *error=nil; JNNote *note=[[JNNote alloc] initWithRecord:record app:self fresh:YES error:&error];
    if (!note) { JNError(error); return; }
    self.notes[record.identifier]=note;
    [note showAtPointer:[sender isKindOfClass:NSString.class] activate:YES];
}
- (void)openRecord:(JNRecord *)record activate:(BOOL)activate {
    if (!record) return;
    JNNote *note=self.notes[record.identifier];
    if (!note) {
        NSError *error=nil; note=[[JNNote alloc] initWithRecord:record app:self fresh:NO error:&error];
        if (!note) { JNError(error); return; }
        self.notes[record.identifier]=note;
        if (activate) { record.settings[@"open"]=@"1"; if (![self.store saveMetadata:&error]) JNError(error); }
    }
    [note showAtPointer:NO activate:activate];
}
- (void)closeNote:(JNNote *)note {
    /* Retain the controller until the window's close callback has returned. */
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self.notes[note.record.identifier] == note) [self.notes removeObjectForKey:note.record.identifier];
        [self changed];
    });
}
- (void)changed { [_browser refresh]; }
- (void)browse:(id)sender {
    (void)sender; NSError *error=nil; if (![self flush:&error]) { JNError(error); return; }
    if (!_browser) _browser=[[JNBrowser alloc] initWithApp:self];
    JNActivate(); [_browser showWindow:nil]; [_browser.window makeKeyAndOrderFront:nil];
}
- (void)search:(id)sender { [self browse:sender]; [_browser focusSearch]; }
- (BOOL)flush:(NSError **)error {
    for (JNNote *note in self.notes.allValues) if (![note flush:error]) return NO;
    return YES;
}
- (BOOL)switchDirectory:(NSURL *)directory {
    if ([directory.URLByResolvingSymlinksInPath.URLByStandardizingPath isEqual:self.store.directory]) return YES;
    NSError *error=nil;
    if (![self flush:&error]) { JNError(error); return NO; }
    JNStore *next=[[JNStore alloc] initWithDirectory:directory error:&error];
    if (!next) { JNError(error); return NO; }
    if (_configuration && !JNWrite([next.directory.path dataUsingEncoding:NSUTF8StringEncoding],_configuration,&error)) { JNError(error); return NO; }
    for (JNNote *note in self.notes.allValues) [note detach];
    [self.notes removeAllObjects]; self.store=next; [self restore]; [self changed]; return YES;
}
- (void)chooseArchive:(id)sender { (void)sender; NSURL *directory=[self pickDirectory]; if (directory) [self switchDirectory:directory]; }
- (void)reload:(id)sender {
    (void)sender; NSError *error=nil;
    if (![self flush:&error]) {
        NSAlert *alert=[NSAlert new]; alert.messageText=@"Reload and discard unsaved edits?";
        alert.informativeText=[error.localizedDescription stringByAppendingString:@"\nCancel to export an RTF copy from the note menu first."];
        [alert addButtonWithTitle:@"Cancel"]; [alert addButtonWithTitle:@"Discard and reload"];
        if ([alert runModal]!=NSAlertSecondButtonReturn) return;
    }
    /* Close only after the new snapshot has been read successfully. */
    if (![self.store reload:&error]) { JNError(error); return; }
    for (JNNote *note in self.notes.allValues) [note detach];
    [self.notes removeAllObjects]; [self restore]; [self changed];
}
- (void)screenChanged:(NSNotification *)notification {
    (void)notification;
    for (JNNote *note in self.notes.allValues) {
        NSRect frame=note.window.frame; BOOL reachable=NO;
        for (NSScreen *screen in NSScreen.screens) if (NSIntersectsRect(NSMakeRect(frame.origin.x,NSMaxY(frame)-32,frame.size.width,32),screen.visibleFrame)) { reachable=YES; break; }
        if (!reachable && NSScreen.mainScreen) { NSRect v=NSScreen.mainScreen.visibleFrame; [note.window setFrameTopLeftPoint:NSMakePoint(v.origin.x+20,NSMaxY(v)-20)]; }
    }
}
- (BOOL)applicationShouldHandleReopen:(NSApplication *)app hasVisibleWindows:(BOOL)visible { (void)app; (void)visible; [self browse:nil]; return NO; }
- (void)application:(NSApplication *)application openURLs:(NSArray<NSURL *> *)urls {
    (void)application; if (!urls.count) return;
    if (!_launched) self.initialDirectory=urls.firstObject;
    else [self switchDirectory:urls.firstObject];
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    (void)sender; NSError *error=nil;
    if (![self flush:&error]) { JNError(error); return NSTerminateCancel; }
    _quitting=YES; return NSTerminateNow;
}
- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification; (void)_quitting;
    if (_hotkey) UnregisterEventHotKey(_hotkey);
    if (_handler) RemoveEventHandler(_handler);
    [NSNotificationCenter.defaultCenter removeObserver:self];
}
@end
