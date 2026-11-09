#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface JNQuery : NSObject
- (nullable instancetype)initWithText:(NSString *)text;
- (BOOL)acceptsTags:(NSString *)tags;
- (BOOL)matchesFile:(NSURL *)url cancelled:(BOOL (^)(void))cancelled error:(NSError * _Nullable * _Nullable)error;
@end
NS_ASSUME_NONNULL_END
