#import "Search.h"
#import "Store.h"

static NSString *fold(NSString *text) {
    return [text stringByFoldingWithOptions:NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch
                                   locale:[NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"]];
}
@implementation JNQuery {
    NSArray<NSString *> *_terms, *_tags;
    BOOL _untagged;
    NSUInteger _overlap;
}
- (instancetype)initWithText:(NSString *)text {
    if (!(self = [super init]) || text.length > 512) return nil;
    NSMutableArray *terms = [NSMutableArray array], *tags = [NSMutableArray array];
    NSCharacterSet *space = NSCharacterSet.whitespaceAndNewlineCharacterSet;
    NSUInteger i=0;
    while (i<text.length) {
        while (i<text.length && [space characterIsMember:[text characterAtIndex:i]]) ++i;
        if (i==text.length) break;
        NSMutableString *term = [NSMutableString string]; BOOL quote = NO;
        while (i<text.length) {
            unichar c = [text characterAtIndex:i];
            if (c == '"') { quote = !quote; ++i; continue; }
            if (!quote && [space characterIsMember:c]) break;
            [term appendFormat:@"%C",c]; ++i;
        }
        if (quote) return nil;
        if ([term isEqual:@"-#"]) _untagged = YES;
        else if ([term hasPrefix:@"#"] && term.length>1) [tags addObject:fold([term substringFromIndex:1])];
        else if (term.length) { NSString *normal = fold(term); [terms addObject:normal]; _overlap = MAX(_overlap,normal.length); }
        if (terms.count+tags.count+(_untagged ? 1u : 0u)>16) return nil;
    }
    _terms = terms; _tags = tags; return self;
}
- (BOOL)acceptsTags:(NSString *)tags {
    if (_untagged && tags.length) return NO;
    if (!_tags.count) return YES;
    NSArray *names = [fold(tags) componentsSeparatedByString:@";"];
    for (NSString *tag in _tags) if (![names containsObject:tag]) return NO;
    return YES;
}
- (BOOL)matchesFile:(NSURL *)url cancelled:(BOOL (^)(void))cancelled error:(NSError **)error {
    if (!_terms.count) return YES;
    NSInputStream *stream = [NSInputStream inputStreamWithURL:url]; [stream open];
    unsigned found=0, all=(1u<<(unsigned)_terms.count)-1;
    unsigned char bytes[8192+4]; NSUInteger pending=0; NSString *tail=@"";
    while (!cancelled()) {
        NSInteger read = [stream read:bytes+pending maxLength:8192];
        if (read<0) { if (error) *error=stream.streamError; break; }
        NSUInteger length=pending+(NSUInteger)read;
        if (!length) break;
        NSUInteger usable=length;
        if (read>0) {
            NSUInteger lead=length-1;
            while (lead && (bytes[lead]&0xc0)==0x80) --lead;
            unsigned char c=bytes[lead];
            NSUInteger width=c<0x80 ? 1 : c>=0xc2 && c<=0xdf ? 2 : c<=0xef && c>=0xe0 ? 3 : c>=0xf0 && c<=0xf4 ? 4 : 1;
            if (length-lead<width) usable=lead;
        }
        if (read>0 && usable && bytes[usable-1]=='\r') --usable;
        @autoreleasepool {
            NSString *chunk=[[NSString alloc] initWithBytes:bytes length:usable encoding:NSUTF8StringEncoding];
            if (!chunk) { if (error) *error=[NSError errorWithDomain:NSCocoaErrorDomain code:NSFileReadInapplicableStringEncodingError userInfo:@{NSFilePathErrorKey:url.path}]; [stream close]; return NO; }
            NSString *window=[tail stringByAppendingString:fold(JNPlain(chunk))];
            for (NSUInteger t=0;t<_terms.count;++t)
                if (!(found&(1u<<t)) && [window rangeOfString:_terms[t] options:NSLiteralSearch].location!=NSNotFound) found|=1u<<t;
            NSUInteger start=window.length>_overlap ? window.length-_overlap : 0;
            if (start && CFStringIsSurrogateLowCharacter([window characterAtIndex:start])) --start;
            tail=[window substringFromIndex:start];
        }
        if (found==all) { [stream close]; return YES; }
        pending=length-usable; if (pending) memmove(bytes,bytes+usable,pending);
        if (!read) break;
    }
    [stream close]; return NO;
}
@end
