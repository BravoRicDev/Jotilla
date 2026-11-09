#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface JNQuery : NSObject
@property(nonatomic, readonly) BOOL requiresText;
@property(nonatomic, readonly) NSArray<NSString *> *terms;
- (nullable instancetype)initWithText:(NSString *)text;
- (BOOL)acceptsTags:(NSString *)tags;
- (BOOL)matchesFile:(NSURL *)url cancelled:(BOOL (^)(void))cancelled error:(NSError * _Nullable * _Nullable)error;
- (BOOL)matchesFile:(NSURL *)url cancelled:(BOOL (^)(void))cancelled excerpt:(NSString * _Nullable * _Nullable)excerpt error:(NSError * _Nullable * _Nullable)error;
@end
NS_ASSUME_NONNULL_END
