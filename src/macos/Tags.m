#import "Tags.h"
@interface JNTagEditor : NSObject <NSTokenFieldDelegate>
@property(nonatomic, copy) NSArray<NSString *> *catalog;
@end
@implementation JNTagEditor
- (NSArray *)tokenField:(NSTokenField *)field completionsForSubstring:(NSString *)substring indexOfToken:(NSInteger)index indexOfSelectedItem:(NSInteger *)selected {
    (void)field; (void)index; *selected=0;
    if([substring hasPrefix:@"#"]) substring=[substring substringFromIndex:1];
    NSMutableArray *matches=[NSMutableArray array];
    for(NSString *tag in self.catalog) {
        if([tag rangeOfString:substring options:NSAnchoredSearch|NSCaseInsensitiveSearch|NSDiacriticInsensitiveSearch].location!=NSNotFound) [matches addObject:tag];
        if(matches.count==50) break;
    }
    return matches;
}
@end
NSString *JNEditTags(JNRecord *record,JNStore *store) {
    __attribute__((objc_precise_lifetime)) JNTagEditor *editor=[JNTagEditor new];
    NSMutableSet *catalog=[NSMutableSet set]; for(JNRecord *r in store.records) if(r.tags.length) [catalog addObjectsFromArray:[r.tags componentsSeparatedByString:@";"]];
    editor.catalog=[catalog.allObjects sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)];
    NSAlert *alert=[NSAlert new]; alert.messageText=@"Tags"; alert.informativeText=@"Separate tags with semicolons. Start typing to reuse an existing tag.";
    [alert addButtonWithTitle:@"Save"]; [alert addButtonWithTitle:@"Cancel"];
    NSTokenField *field=[[NSTokenField alloc] initWithFrame:NSMakeRect(0,0,420,30)]; field.delegate=editor; field.tokenizingCharacterSet=[NSCharacterSet characterSetWithCharactersInString:@";\n"]; field.completionDelay=0.15; field.objectValue=record.tags.length?[record.tags componentsSeparatedByString:@";"]:@[];
    [field setAccessibilityLabel:@"Note tags"]; alert.accessoryView=field; alert.window.initialFirstResponder=field;
    NSModalResponse response=[alert runModal]; [alert.window makeFirstResponder:nil]; field.delegate=nil;
    if(response!=NSAlertFirstButtonReturn) return nil;
    id value=field.objectValue;
    return [value isKindOfClass:NSArray.class] ? [value componentsJoinedByString:@";"] : field.stringValue;
}
