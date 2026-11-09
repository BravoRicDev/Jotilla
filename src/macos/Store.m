#import "Store.h"
#include "../date.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static NSError *failure(NSString *message) {
    return [NSError errorWithDomain:@"Jotila" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];
}
static BOOL fail(NSError **error, NSString *message) {
    if (error) *error = failure(message);
    return NO;
}
static NSString *version(NSURL *url) {
    struct stat info;
    if (lstat(url.fileSystemRepresentation,&info)) return @"missing";
    return [NSString stringWithFormat:@"%llu:%llu:%lld:%ld:%lld:%ld",(unsigned long long)info.st_ino,
        (unsigned long long)info.st_size,(long long)info.st_mtimespec.tv_sec,info.st_mtimespec.tv_nsec,
        (long long)info.st_ctimespec.tv_sec,info.st_ctimespec.tv_nsec];
}
NSString *JNPlain(NSString *text) {
    return [[text stringByReplacingOccurrencesOfString:@"\r\n" withString:@"\n"]
            stringByReplacingOccurrencesOfString:@"\r" withString:@"\n"];
}
BOOL JNWrite(NSData *data, NSURL *url, NSError **error) {
    NSString *pattern = [url.path stringByAppendingString:@".XXXXXX"];
    char *temporary = strdup(pattern.fileSystemRepresentation);
    if (!temporary) return fail(error, @"Not enough memory to save the archive.");
    int fd = mkstemp(temporary), saved = 0;
    if (fd < 0) saved = errno;
    const unsigned char *bytes = data.bytes;
    size_t left = data.length;
    while (!saved && left) {
        ssize_t count = write(fd, bytes, left);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { saved = count < 0 ? errno : EIO; break; }
        bytes += count; left -= (size_t)count;
    }
    if (!saved && fsync(fd)) saved = errno;
    if (fd >= 0 && close(fd) && !saved) saved = errno;
    if (!saved && rename(temporary, url.fileSystemRepresentation)) saved = errno;
    if (saved && fd >= 0) unlink(temporary);
    free(temporary);
    if (!saved) {
        int parent = open(url.URLByDeletingLastPathComponent.fileSystemRepresentation, O_RDONLY);
        if (parent >= 0) { (void)fsync(parent); close(parent); }
        return YES;
    }
    if (error) *error = [NSError errorWithDomain:NSPOSIXErrorDomain code:saved userInfo:@{NSFilePathErrorKey:url.path}];
    return NO;
}
NSString *JNNormalizeTags(NSString *text, NSError **error) {
    NSMutableSet<NSString *> *names = [NSMutableSet set];
    NSCharacterSet *space = NSCharacterSet.whitespaceAndNewlineCharacterSet;
    NSCharacterSet *invalid = [NSCharacterSet characterSetWithCharactersInString:@"\"#\r\n\t"];
    for (NSString *part in [text componentsSeparatedByString:@";"]) {
        NSString *name = [part stringByTrimmingCharactersInSet:space];
        if ([name hasPrefix:@"#"]) name = [name substringFromIndex:1];
        name = [[name precomposedStringWithCanonicalMapping] lowercaseStringWithLocale:
                [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"]];
        if (!name.length) continue;
        if (name.length >= 64 || [name rangeOfCharacterFromSet:invalid].location != NSNotFound ||
            [name rangeOfCharacterFromSet:NSCharacterSet.controlCharacterSet].location != NSNotFound) {
            fail(error, @"Tags must be shorter than 64 characters and cannot contain quotes, # or control characters.");
            return nil;
        }
        [names addObject:name];
    }
    if (names.count > 24) { fail(error, @"A note can have at most 24 tags."); return nil; }
    return [[[names allObjects] sortedArrayUsingSelector:@selector(compare:)] componentsJoinedByString:@";"];
}

@implementation JNRecord
- (instancetype)init {
    if ((self = [super init])) {
        _identifier = NSUUID.UUID.UUIDString.lowercaseString;
        _title = @""; _tags = @""; _settings = [NSMutableDictionary dictionary];
    }
    return self;
}
@end

@implementation JNStore {
    NSMutableArray<JNRecord *> *_items;
    NSMutableDictionary<NSString *, NSMutableDictionary<NSString *, NSString *> *> *_sections;
    NSData *_metadataSnapshot;
    NSString *_metadataVersion;
    NSMutableDictionary<NSString *, NSString *> *_versions;
    int _lock;
}
- (instancetype)initWithDirectory:(NSURL *)directory error:(NSError **)error {
    if (!(self = [super init])) return nil;
    _lock = -1;
    _directory = directory.URLByStandardizingPath.URLByResolvingSymlinksInPath;
    if (![NSFileManager.defaultManager createDirectoryAtURL:_directory withIntermediateDirectories:YES attributes:nil error:error]) return nil;
    NSURL *lockURL = [_directory URLByAppendingPathComponent:@".jotila.lock"];
    _lock = open(lockURL.fileSystemRepresentation, O_CREAT | O_RDWR, 0600);
    if (_lock < 0 || flock(_lock, LOCK_EX | LOCK_NB)) {
        fail(error, @"This archive is unavailable or already open in another Jotila process."); return nil;
    }
    if (![self reload:error]) return nil;
    return self;
}
- (void)dealloc { if (_lock >= 0) close(_lock); }
- (NSArray<JNRecord *> *)records { return [_items copy]; }
- (NSMutableDictionary *)preferences { return _sections[@"app"]; }
- (NSURL *)URLFor:(JNRecord *)record extension:(NSString *)extension {
    return [_directory URLByAppendingPathComponent:[record.identifier stringByAppendingPathExtension:extension]];
}
- (BOOL)reload:(NSError **)error {
    NSURL *url = [_directory URLByAppendingPathComponent:@"notes.ini"];
    NSString *metadataVersion = version(url);
    NSData *data = [NSData dataWithContentsOfURL:url options:0 error:error];
    if (!data && [NSFileManager.defaultManager fileExistsAtPath:url.path]) return NO;
    if (error) *error = nil;
    if (data.length > 64 * 1024 * 1024) return fail(error, @"The metadata file is too large.");
    NSString *text = @"";
    if (data.length) {
        const unsigned char *p = data.bytes;
        BOOL utf16 = data.length >= 2 && p[0] == 255 && p[1] == 254;
        text = [[NSString alloc] initWithData:data encoding:utf16 ? NSUTF16LittleEndianStringEncoding : NSUTF8StringEncoding];
        /* Legacy non-Unicode Windows INI files commonly use CP1252. */
        if (!text && !utf16) text = [[NSString alloc] initWithData:data encoding:NSWindowsCP1252StringEncoding];
        if (!text) return fail(error, @"The metadata encoding is invalid.");
        if ([text hasPrefix:@"\uFEFF"]) text = [text substringFromIndex:1];
    }
    NSMutableDictionary *sections = [NSMutableDictionary dictionaryWithObject:[NSMutableDictionary dictionary] forKey:@"app"];
    __block NSMutableDictionary *section = nil;
    NSCharacterSet *space = NSCharacterSet.whitespaceCharacterSet;
    [JNPlain(text) enumerateLinesUsingBlock:^(NSString *line, BOOL *stop) {
        (void)stop;
        line = [line stringByTrimmingCharactersInSet:space];
        if ([line hasPrefix:@"["]) {
            NSRange end = [line rangeOfString:@"]"];
            section = nil;
            if (end.location != NSNotFound && end.location > 1) {
                NSString *name = [[line substringWithRange:NSMakeRange(1, end.location - 1)] lowercaseString];
                section = sections[name];
                if (!section) sections[name] = section = [NSMutableDictionary dictionary];
            }
        } else if (section && ![line hasPrefix:@";"]) {
            NSRange equal = [line rangeOfString:@"="];
            if (equal.location != NSNotFound) {
                NSString *key = [[[line substringToIndex:equal.location] stringByTrimmingCharactersInSet:space] lowercaseString];
                section[key] = [[line substringFromIndex:NSMaxRange(equal)] stringByTrimmingCharactersInSet:space];
            }
        }
    }];
    NSArray<NSURL *> *files = [NSFileManager.defaultManager contentsOfDirectoryAtURL:_directory
        includingPropertiesForKeys:@[NSURLIsRegularFileKey, NSURLIsSymbolicLinkKey] options:NSDirectoryEnumerationSkipsHiddenFiles error:error];
    if (!files) return NO;
    NSMutableArray *items = [NSMutableArray array];
    for (NSURL *file in files) {
        if (![file.pathExtension.lowercaseString isEqual:@"txt"]) continue;
        NSNumber *regular, *symlink;
        if (![file getResourceValue:&regular forKey:NSURLIsRegularFileKey error:error] ||
            ![file getResourceValue:&symlink forKey:NSURLIsSymbolicLinkKey error:error]) return NO;
        if (!regular.boolValue || symlink.boolValue) continue;
        NSString *identifier = file.lastPathComponent.stringByDeletingPathExtension;
        if ([identifier rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"[]\r\n"]].location != NSNotFound) continue;
        JNRecord *record = [JNRecord new]; record.identifier = identifier;
        record.settings = sections[identifier.lowercaseString] ?: [NSMutableDictionary dictionary];
        sections[identifier.lowercaseString] = record.settings;
        record.tags = record.settings[@"tags"] ?: @"";
        unsigned day = (unsigned)[record.settings[@"calendar_date"] integerValue];
        record.day = date_valid(day) ? day : 0;
        [items addObject:record];
    }
    [items sortUsingComparator:^NSComparisonResult(JNRecord *a, JNRecord *b) { return [a.identifier compare:b.identifier]; }];
    if (![metadataVersion isEqual:version(url)]) return fail(error, @"Metadata changed while it was being read. Try reloading again.");
    _sections = sections; _items = items; _metadataSnapshot = data; _metadataVersion = metadataVersion;
    _versions = [NSMutableDictionary dictionary];
    return YES;
}
- (JNRecord *)newRecord {
    return [JNRecord new];
}
- (NSAttributedString *)read:(JNRecord *)record error:(NSError **)error {
    NSURL *url = [self URLFor:record extension:@"txt"];
    NSString *textVersion = version(url);
    NSNumber *size;
    if (![url getResourceValue:&size forKey:NSURLFileSizeKey error:error]) return nil;
    if (size.unsignedLongLongValue > 64 * 1024 * 1024) { fail(error, @"The note is too large."); return nil; }
    NSString *text = [NSString stringWithContentsOfURL:url encoding:NSUTF8StringEncoding error:error];
    if (!text) return nil;
    text = JNPlain(text);
    if (![textVersion isEqual:version(url)]) { fail(error, @"The note changed while it was being read. Try reopening it."); return nil; }
    _versions[record.identifier] = textVersion;
    if ([text hasPrefix:@"\uFEFF"]) text = [text substringFromIndex:1];
    record.title = @"";
    if ([record.settings[@"has_title"] boolValue] && [text hasPrefix:@"#"]) {
        NSRange newline = [text rangeOfString:@"\n"];
        NSUInteger end = newline.location == NSNotFound ? text.length : newline.location;
        if (end > 1) { record.title = [text substringWithRange:NSMakeRange(1, end - 1)]; text = newline.location == NSNotFound ? @"" : [text substringFromIndex:end + 1]; }
    }
    NSURL *richURL = [self URLFor:record extension:@"rtf"];
    NSNumber *richSize;
    if ([richURL getResourceValue:&richSize forKey:NSURLFileSizeKey error:NULL] && richSize.unsignedLongLongValue <= 256 * 1024 * 1024) {
        NSData *richData = [NSData dataWithContentsOfURL:richURL];
        NSAttributedString *rich = richData ? [[NSAttributedString alloc] initWithRTF:richData documentAttributes:NULL] : nil;
        if (rich && [JNPlain(rich.string) isEqual:text]) {
            NSMutableAttributedString *normalized = [rich mutableCopy];
            /* Normalize in reverse so attribute ranges remain valid. */
            for (NSInteger i = (NSInteger)normalized.length - 1; i >= 0; --i)
                if ([normalized.string characterAtIndex:(NSUInteger)i] == '\r') {
                    BOOL pair = (NSUInteger)i + 1 < normalized.length && [normalized.string characterAtIndex:(NSUInteger)i + 1] == '\n';
                    [normalized replaceCharactersInRange:NSMakeRange((NSUInteger)i, 1) withString:pair ? @"" : @"\n"];
                }
            return normalized;
        }
    }
    return [[NSAttributedString alloc] initWithString:text attributes:@{NSFontAttributeName:[NSFont systemFontOfSize:14]}];
}
- (BOOL)saveMetadata:(NSError **)error {
    NSURL *url = [_directory URLByAppendingPathComponent:@"notes.ini"];
    if (![version(url) isEqual:_metadataVersion])
        return fail(error, @"Archive metadata changed outside Jotila. Reload the archive before editing further.");
    NSMutableString *ini = [NSMutableString stringWithString:@"\uFEFF"];
    for (NSString *name in [[_sections allKeys] sortedArrayUsingSelector:@selector(compare:)]) {
        NSDictionary *section = _sections[name];
        if (!section.count) continue;
        [ini appendFormat:@"[%@]\r\n", name];
        for (NSString *key in [[section allKeys] sortedArrayUsingSelector:@selector(compare:)])
            [ini appendFormat:@"%@=%@\r\n", key, section[key]];
        [ini appendString:@"\r\n"];
    }
    NSData *data = [ini dataUsingEncoding:NSUTF16LittleEndianStringEncoding];
    if ([data isEqual:_metadataSnapshot]) return YES;
    if (!JNWrite(data, url, error)) return NO;
    _metadataSnapshot = data; _metadataVersion = version(url); return YES;
}
- (BOOL)save:(JNRecord *)record text:(NSAttributedString *)text error:(NSError **)error {
    return [self save:record text:text writeText:YES writeRich:YES error:error];
}
- (BOOL)save:(JNRecord *)record text:(NSAttributedString *)text writeText:(BOOL)writeText writeRich:(BOOL)writeRich error:(NSError **)error {
    NSURL *url = [self URLFor:record extension:@"txt"];
    NSString *expected = _versions[record.identifier] ?: @"missing";
    if (![version(url) isEqual:expected]) return fail(error, @"This note changed outside Jotila. Export your edit as a copy, then reload the archive.");
    if (![version([_directory URLByAppendingPathComponent:@"notes.ini"]) isEqual:_metadataVersion])
        return fail(error, @"Archive metadata changed outside Jotila. Export your edit as a copy, then reload the archive.");
    NSString *body = JNPlain(text.string);
    if (!record.title.length && ![body stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet].length) {
        /* A blank new window remains ephemeral. Existing notes can be cleared
           without silently deleting their archive files. */
        if (![NSFileManager.defaultManager fileExistsAtPath:[self URLFor:record extension:@"txt"].path]) return YES;
    }
    BOOL fresh = [expected isEqual:@"missing"];
    writeText = writeText || fresh; writeRich = writeRich || fresh;
    NSData *utf8 = nil, *rich = nil;
    if (writeText) {
        NSString *plain = record.title.length ? [NSString stringWithFormat:@"#%@\n%@", record.title, body] : body;
        utf8 = [[plain stringByReplacingOccurrencesOfString:@"\n" withString:@"\r\n"] dataUsingEncoding:NSUTF8StringEncoding];
        if (!utf8) return fail(error, @"The text could not be encoded.");
    }
    if (writeRich) {
        rich = [text RTFFromRange:NSMakeRange(0, text.length) documentAttributes:@{}];
        if (!rich) return fail(error, @"The formatting could not be encoded.");
    }
    /* RTF precedes TXT. On any interrupted pair, read verifies that RTF agrees
       with authoritative TXT before applying formatting. */
    if ((writeRich && !JNWrite(rich, [self URLFor:record extension:@"rtf"], error)) ||
        (writeText && !JNWrite(utf8, url, error))) return NO;
    _versions[record.identifier] = version(url);
    if (![_items containsObject:record]) [_items addObject:record];
    _sections[record.identifier.lowercaseString] = record.settings;
    record.settings[@"has_title"] = record.title.length ? @"1" : @"0";
    record.settings[@"tags"] = record.tags;
    record.settings[@"calendar_date"] = [NSString stringWithFormat:@"%08lu", (unsigned long)record.day];
    return [self saveMetadata:error];
}
- (BOOL)remove:(JNRecord *)record error:(NSError **)error {
    /* User deletion goes to Trash and remains recoverable. */
    for (NSString *ext in @[@"txt", @"rtf"]) {
        NSURL *url = [self URLFor:record extension:ext];
        if ([NSFileManager.defaultManager fileExistsAtPath:url.path] &&
            ![NSFileManager.defaultManager trashItemAtURL:url resultingItemURL:NULL error:error]) return NO;
    }
    [_items removeObject:record]; [_sections removeObjectForKey:record.identifier.lowercaseString];
    return [self saveMetadata:error];
}
@end
