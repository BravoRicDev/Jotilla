#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN
FOUNDATION_EXPORT NSURL *JNProfileRoot(NSURL *support);
FOUNDATION_EXPORT NSString *JNPlain(NSString *text);
FOUNDATION_EXPORT BOOL JNWrite(NSData *data, NSURL *url, NSError * _Nullable * _Nullable error);
FOUNDATION_EXPORT NSString * _Nullable JNNormalizeTags(NSString *text, NSError * _Nullable * _Nullable error);

@interface JNRecord : NSObject
@property(nonatomic, copy) NSString *identifier;
@property(nonatomic, copy) NSString *title;
@property(nonatomic, copy) NSString *tags;
@property(nonatomic) NSUInteger day;
@property(nonatomic) NSUInteger createdDay, revision;
@property(nonatomic) BOOL persisted;
@property(nonatomic, strong) NSMutableDictionary<NSString *, NSString *> *settings;
@end

/* Main-thread metadata snapshot. Search takes an immutable copy and reads TXT
   files on its one serial queue; editor contents never enter the search cache. */
@interface JNStore : NSObject
@property(nonatomic, strong, readonly) NSURL *directory;
@property(nonatomic, strong, readonly) NSArray<JNRecord *> *records;
@property(nonatomic, strong, readonly) NSMutableDictionary<NSString *, NSString *> *preferences;
- (nullable instancetype)initWithDirectory:(NSURL *)directory error:(NSError * _Nullable * _Nullable)error;
- (BOOL)reload:(NSError * _Nullable * _Nullable)error;
- (JNRecord *)newRecord;
- (nullable NSAttributedString *)read:(JNRecord *)record error:(NSError * _Nullable * _Nullable)error;
- (BOOL)save:(JNRecord *)record text:(NSAttributedString *)text error:(NSError * _Nullable * _Nullable)error;
- (BOOL)save:(JNRecord *)record text:(NSAttributedString *)text writeText:(BOOL)writeText writeRich:(BOOL)writeRich error:(NSError * _Nullable * _Nullable)error;
- (BOOL)saveMetadata:(NSError * _Nullable * _Nullable)error;
- (BOOL)remove:(JNRecord *)record error:(NSError * _Nullable * _Nullable)error;
- (NSURL *)URLFor:(JNRecord *)record extension:(NSString *)extension;
- (nullable JNStore *)copyToDirectory:(NSURL *)directory error:(NSError * _Nullable * _Nullable)error;
- (BOOL)removeTransferredFiles:(JNStore *)destination error:(NSError * _Nullable * _Nullable)error;
@end
NS_ASSUME_NONNULL_END
