#import "App.h"
NS_ASSUME_NONNULL_BEGIN
@interface JNPalette : NSWindowController <NSWindowDelegate>
@property(nonatomic, readonly) NSInteger palette;
- (instancetype)initWithNote:(JNNote *)note;
- (void)showAtPoint:(NSPoint)point;
- (void)step:(NSInteger)step;
- (void)choose:(NSInteger)color;
- (void)toggleFavorite:(nullable id)sender;
- (void)cancel:(nullable id)sender;
@end
NS_ASSUME_NONNULL_END
