#import "App.h"
NS_ASSUME_NONNULL_BEGIN
@interface JNCalendar : NSView
@property(nonatomic) NSInteger year;
@property(nonatomic) NSUInteger selected;
@property(nonatomic, copy) NSDictionary<NSNumber *, NSNumber *> *counts;
@property(nonatomic, copy, nullable) void (^yearChanged)(NSInteger);
@property(nonatomic, copy, nullable) void (^choose)(NSUInteger, NSRect);
- (NSRect)cell:(unsigned)day;
- (void)moveSelection:(NSInteger)days;
@end
NS_ASSUME_NONNULL_END
