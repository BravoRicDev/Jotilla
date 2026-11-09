#import "../../src/macos/App.h"
#import "../../src/macos/Search.h"
#include <stdio.h>
#include <sys/stat.h>

static unsigned checks;
static void require(BOOL condition, const char *message) {
    ++checks;
    if (!condition) { fprintf(stderr,"FAIL: %s\n",message); exit(1); }
}
static NSAttributedString *rich(NSString *text) {
    return [[NSAttributedString alloc] initWithString:text attributes:@{
        NSFontAttributeName:[NSFont boldSystemFontOfSize:18],NSForegroundColorAttributeName:NSColor.blueColor,
        NSUnderlineStyleAttributeName:@(NSUnderlineStyleSingle)}];
}
static BOOL matches(JNQuery *query, NSURL *file) {
    NSError *error=nil;
    BOOL result=[query matchesFile:file cancelled:^BOOL { return NO; } error:&error];
    require(!error,"streaming search has no I/O or encoding failure"); return result;
}
static void emptyArchives(void) {
    NSURL *root=[[NSURL fileURLWithPath:NSTemporaryDirectory()] URLByAppendingPathComponent:NSUUID.UUID.UUIDString];
    NSError *error=nil; require([NSFileManager.defaultManager createDirectoryAtURL:root withIntermediateDirectories:YES attributes:nil error:&error],"create legacy blank-note fixture");
    NSString *padding=[@"" stringByPaddingToLength:255 withString:@" " startingAtIndex:0];
    NSDictionary *files=@{@"empty":@"",@"spaces":@" \t\r\n\u00a0\u2003",@"bom":@"\uFEFF  \n",@"header":@"\uFEFF# \t\r\n",@"title":@"#Only title\r\n",@"body":@"Only text",@"later":[padding stringByAppendingString:@"🌿"],@"long-spaces":[padding stringByAppendingString:@"\u2003\u00a0\n"]};
    for(NSString *name in files) require(JNWrite([files[name] dataUsingEncoding:NSUTF8StringEncoding],[root URLByAppendingPathComponent:[name stringByAppendingPathExtension:@"txt"]],&error),"write legacy note fixture");
    require(JNWrite([@"[header]\nhas_title=1\n[title]\nhas_title=1\n" dataUsingEncoding:NSUTF8StringEncoding],[root URLByAppendingPathComponent:@"notes.ini"],&error),"legacy title flags");
    const unsigned char invalid[]={0xf0,0x9f}; require(JNWrite([NSData dataWithBytes:invalid length:sizeof(invalid)],[root URLByAppendingPathComponent:@"invalid.txt"],&error),"invalid note remains discoverable");
    JNStore *store=[[JNStore alloc] initWithDirectory:root error:&error]; require(store!=nil,"load legacy archive with blank files");
    NSMutableSet *ids=[NSMutableSet set]; for(JNRecord *record in store.records) [ids addObject:record.identifier];
    require([ids isEqual:[NSSet setWithArray:@[@"title",@"body",@"later",@"invalid"]]],"reload excludes blank files and empty title headers; retains title-only, body-only, split UTF-8 and invalid files");
    require([NSFileManager.defaultManager fileExistsAtPath:[root URLByAppendingPathComponent:@"empty.txt"].path],"loading does not delete legacy blank files");
    require([store reload:&error] && store.records.count==4,"blank notes stay excluded after another reload");
    require([NSFileManager.defaultManager removeItemAtURL:root error:&error],"remove legacy blank-note fixture");
}
int main(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        emptyArchives();
        NSURL *root=[[NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES] URLByAppendingPathComponent:[@"Jotila-tests-" stringByAppendingString:NSUUID.UUID.UUIDString] isDirectory:YES];
        NSError *error=nil;
        JNStore *store=[[JNStore alloc] initWithDirectory:root error:&error];
        require(store!=nil,"create isolated archive");
        require([[JNStore alloc] initWithDirectory:root error:&error]==nil,"second writer cannot acquire archive");
        error=nil;
        JNRecord *note=[store newRecord];
        require([store save:note text:rich(@"   \n") error:&error],"blank fresh note remains ephemeral");
        require(store.records.count==0,"blank note stays out of explorer");
        require(![NSFileManager.defaultManager fileExistsAtPath:[store URLFor:note extension:@"txt"].path],"blank note has no TXT");
        note.title=@" \t\u00a0";
        require([store save:note text:rich(@"\u2003\n") error:&error] && store.records.count==0,"whitespace title and body stay unsaved");
        note.title=@"Caffè 🌿"; note.tags=JNNormalizeTags(@"#Work; CAFÉ; cafe\u0301; work",&error); note.day=20280229;
        require([note.tags isEqual:@"café;work"],"tags are normalized, sorted and deduplicated");
        require(JNNormalizeTags(@"bad\"tag",&error)==nil,"invalid tag rejected"); error=nil;
        note.settings[@"unknown_future_key"]=@"preserved"; note.settings[@"open"]=@"1";
        NSAttributedString *text=rich(@"First line\n☐ task\nGrüße 🌿\n");
        require([store save:note text:text error:&error],"save rich note");
        NSString *plain=[NSString stringWithContentsOfURL:[store URLFor:note extension:@"txt"] encoding:NSUTF8StringEncoding error:&error];
        require([plain hasPrefix:@"#Caffè 🌿\r\nFirst line\r\n"],"Windows title and CRLF encoding");
        NSData *metadata=[NSData dataWithContentsOfURL:[root URLByAppendingPathComponent:@"notes.ini"]];
        const unsigned char *bytes=metadata.bytes;
        require(metadata.length>=2 && bytes[0]==255 && bytes[1]==254,"INI has Windows UTF-16LE BOM");
        require([store reload:&error],"reload metadata snapshot"); note=store.records.firstObject;
        NSAttributedString *loaded=[store read:note error:&error];
        require([loaded.string isEqual:text.string],"RTF and TXT round trip");
        require([note.title isEqual:@"Caffè 🌿"] && note.day==20280229,"title and leap day survive reload");
        require([note.settings[@"unknown_future_key"] isEqual:@"preserved"],"unknown metadata survives");
        require([[loaded attribute:NSUnderlineStyleAttributeName atIndex:0 effectiveRange:NULL] integerValue]==NSUnderlineStyleSingle,"rich formatting survives");
        struct stat before, after;
        require(!stat([store URLFor:note extension:@"txt"].fileSystemRepresentation,&before),"stat TXT before formatting save");
        require([store save:note text:rich(loaded.string) writeText:NO writeRich:YES error:&error],"format-only save");
        require(!stat([store URLFor:note extension:@"txt"].fileSystemRepresentation,&after) && before.st_ino==after.st_ino && before.st_mtimespec.tv_sec==after.st_mtimespec.tv_sec && before.st_mtimespec.tv_nsec==after.st_mtimespec.tv_nsec,"format-only save leaves TXT untouched");
        require(JNWrite([@"external replacement" dataUsingEncoding:NSUTF8StringEncoding],[store URLFor:note extension:@"txt"],&error),"simulate external TXT replacement");
        require(![store save:note text:text error:&error],"stale editor cannot overwrite external TXT"); error=nil;
        loaded=[store read:note error:&error];
        require([loaded.string isEqual:@"external replacement"],"stale RTF never resurrects old text");
        note.title=@"";
        require([store save:note text:rich(@"safe replacement") error:&error],"save after explicit reload");
        NSURL *ini=[root URLByAppendingPathComponent:@"notes.ini"];
        require(JNWrite([@"[app]\r\nexternal=1\r\n" dataUsingEncoding:NSUTF8StringEncoding],ini,&error),"simulate external metadata");
        require(![store saveMetadata:&error],"metadata conflict does not overwrite outside changes"); error=nil;
        require([store reload:&error],"recover by reloading external metadata");
        require([store.preferences[@"external"] isEqual:@"1"],"external key remains intact");

        NSURL *search=[root URLByAppendingPathComponent:@"search.txt"];
        NSMutableString *large=[NSMutableString string];
        for (NSUInteger i=0;i<8191;++i) [large appendString:@"x"];
        [large appendString:@"🌿 CAFÉ release plan\nfin"];
        require(JNWrite([large dataUsingEncoding:NSUTF8StringEncoding],search,&error),"create UTF-8 boundary fixture");
        JNQuery *query=[[JNQuery alloc] initWithText:@"#work café \"release plan\" 🌿"];
        require([query acceptsTags:@"personal;work"] && ![query acceptsTags:@"personal"],"tag prefilter");
        require(matches(query,search),"accent folding, phrase and split UTF-8 scalar");
        NSMutableString *boundary=[NSMutableString string];
        for (NSUInteger i=0;i<8188;++i) [boundary appendString:@"x"];
        [boundary appendString:@"release plan"];
        require(JNWrite([boundary dataUsingEncoding:NSUTF8StringEncoding],search,&error),"phrase boundary fixture");
        require(matches([[JNQuery alloc] initWithText:@"\"release plan\""],search),"phrase crosses stream boundary");
        require(!matches([[JNQuery alloc] initWithText:@"missing"],search),"missing term");
        require(![[JNQuery alloc] initWithText:@"\"unclosed"],"unclosed phrase rejected");
        JNQuery *tagOnly=[[JNQuery alloc] initWithText:@"-#"];
        require([tagOnly acceptsTags:@""] && ![tagOnly acceptsTags:@"work"],"untagged filter");
        require(matches(tagOnly,[root URLByAppendingPathComponent:@"absent.txt"]),"tag-only query reads no TXT");
        require(![query matchesFile:search cancelled:^BOOL { return YES; } error:&error],"search cancellation");
        NSURL *bad=[root URLByAppendingPathComponent:@"invalid.txt"];
        unsigned char invalid[]={0xf0,0x9f};
        require(JNWrite([NSData dataWithBytes:invalid length:sizeof(invalid)],bad,&error),"invalid UTF-8 fixture");
        require(![query matchesFile:bad cancelled:^BOOL { return NO; } error:&error] && error!=nil,"truncated UTF-8 reports failure");
        require([error.domain isEqual:NSCocoaErrorDomain] && error.code==NSFileReadInapplicableStringEncodingError &&
            [error.userInfo[NSFilePathErrorKey] isEqual:bad.path],"encoding error survives the search autorelease pool"); error=nil;

        NSString *excerpt=nil;
        require([[[JNQuery alloc] initWithText:@"release"] terms].count==1,"parsed search terms available for highlighting");
        require([[[JNQuery alloc] initWithText:@"release"] matchesFile:search cancelled:^BOOL{return NO;} excerpt:&excerpt error:&error] && [excerpt containsString:@"release plan"],"bounded search excerpt includes match across chunk boundary");
        require(excerpt.length<400,"search excerpt stays bounded");
        NSURL *moved=[root.URLByDeletingLastPathComponent URLByAppendingPathComponent:[@"Jotila-moved-" stringByAppendingString:NSUUID.UUID.UUIDString]];
        JNStore *copy=[store copyToDirectory:moved error:&error]; require(copy!=nil,"copy archive into locked empty folder");
        require([NSFileManager.defaultManager contentsEqualAtPath:[root URLByAppendingPathComponent:@"notes.ini"].path andPath:[moved URLByAppendingPathComponent:@"notes.ini"].path],"migration preserves metadata bytes");
        require([NSFileManager.defaultManager contentsEqualAtPath:[root URLByAppendingPathComponent:@"search.txt"].path andPath:[moved URLByAppendingPathComponent:@"search.txt"].path],"migration preserves note bytes");
        require([store copyToDirectory:[root URLByAppendingPathComponent:@"child"] error:&error]==nil,"migration rejects nested destination"); error=nil;
        require([store copyToDirectory:root.URLByDeletingLastPathComponent error:&error]==nil,"migration rejects parent destination"); error=nil;
        require([store copyToDirectory:moved error:&error]==nil,"migration rejects destination in use"); error=nil;
        require([store removeTransferredFiles:copy error:&error],"verified originals are removed after switching");
        require(![NSFileManager.defaultManager fileExistsAtPath:search.path] && [NSFileManager.defaultManager fileExistsAtPath:[moved URLByAppendingPathComponent:@"search.txt"].path],"migration leaves authoritative files at destination");
        store=copy;
        JNRecord *erasable=[store newRecord]; require([store save:erasable text:rich(@"temporary") error:&error],"create note for clear and undo");
        require([store save:erasable text:rich(@"") error:&error],"clearing saved note removes it from archive");
        require(![store.records containsObject:erasable],"cleared note absent from explorer snapshot");
        require([store save:erasable text:rich(@"restored by undo") error:&error],"undo after clear can save same identifier again");
        require([[store read:erasable error:&error].string isEqual:@"restored by undo"],"restored note round trip");
        require([NSFileManager.defaultManager removeItemAtURL:moved error:&error],"remove migration test directory");
        store=[[JNStore alloc] initWithDirectory:root error:&error]; require(store!=nil,"original directory reusable after migration");

        unsigned date=20000101;
        for (unsigned i=0;i<146097;++i) { require(date_valid(date),"Gregorian cycle valid"); require(date_weekday(date)>=0 && date_weekday(date)<7,"weekday range"); date=date_shift(date,1); }
        require(date==24000101,"Gregorian 400-year cycle");
        for (int p=0;p<PALETTE_COUNT;++p) { require(PALETTE_SIZES[p]>=1 && PALETTE_SIZES[p]<=COLOR_COUNT,"palette bounds"); require(PALETTE_NAMES[p]!=NULL,"palette names"); }
        require(GetRValue(PALETTES[0][0])==255 && GetGValue(PALETTES[0][0])==238 && GetBValue(PALETTES[0][0])==151,"shared RGB byte order");

        JNApp *app=[JNApp new]; app.store=store;
        JNRecord *fresh=[store newRecord];
        JNNote *window=[[JNNote alloc] initWithRecord:fresh app:app fresh:YES error:&error];
        require(window!=nil && window.window.canBecomeKeyWindow,"native note accepts keyboard focus");
        require(window.window.level==NSFloatingWindowLevel,"note floats");
        CGFloat height=window.window.frame.size.height; [window fold:nil];
        require(window.window.frame.size.height==32,"folded title bar height"); [window fold:nil];
        require(fabs(window.window.frame.size.height-height)<1,"expand restores height");
        require([window flush:&error],"empty note layout does not create archive content");
        [window.window orderOut:nil]; window=nil; app=nil; store=nil;
        require([NSFileManager.defaultManager removeItemAtURL:root error:&error],"remove isolated test directory");
        printf("PASS: %u checks (storage, conflicts, RTF, search, Gregorian dates, palettes, native note).\n",checks);
    }
    return 0;
}
