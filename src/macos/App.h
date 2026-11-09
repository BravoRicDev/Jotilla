#import "Store.h"
#include "../colors.h"
#include "../date.h"

NS_ASSUME_NONNULL_BEGIN
@class JNApp, JNBrowser;
FOUNDATION_EXPORT NSColor *JNColor(COLORREF color);
FOUNDATION_EXPORT void JNError(NSError * _Nullable error);
FOUNDATION_EXPORT void JNActivate(void);
FOUNDATION_EXPORT NSString * _Nullable JNAsk(NSString *title, NSString *value);

@interface JNNote : NSWindowController <NSWindowDelegate, NSTextViewDelegate>
@property(nonatomic, strong, readonly) JNRecord *record;
@property(nonatomic, strong, readonly) NSTextView *editor;
@property(nonatomic, weak) JNApp *app;
- (nullable instancetype)initWithRecord:(JNRecord *)record app:(JNApp *)app fresh:(BOOL)fresh error:(NSError * _Nullable * _Nullable)error;
- (BOOL)flush:(NSError * _Nullable * _Nullable)error;
- (void)showAtPointer:(BOOL)pointer activate:(BOOL)activate;
- (void)fold:(nullable id)sender;
- (void)tags:(nullable id)sender;
- (void)chooseDate:(nullable id)sender;
- (void)rename:(nullable id)sender;
- (void)format:(id)sender;
- (void)resizeBy:(CGFloat)amount;
- (void)detach;
- (NSMenu *)contextMenu;
@end

@interface JNBrowser : NSWindowController
@property(nonatomic, weak) JNApp *app;
- (instancetype)initWithApp:(JNApp *)app;
- (void)refresh;
- (void)focusSearch;
@end

@interface JNApp : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) JNStore *store;
@property(nonatomic, strong, readonly) NSMutableDictionary<NSString *, JNNote *> *notes;
@property(nonatomic) BOOL portable;
@property(nonatomic) BOOL startup;
@property(nonatomic, strong, nullable) NSURL *initialDirectory;
- (void)newNote:(nullable id)sender;
- (void)browse:(nullable id)sender;
- (void)search:(nullable id)sender;
- (void)chooseArchive:(nullable id)sender;
- (void)reload:(nullable id)sender;
- (BOOL)flush:(NSError * _Nullable * _Nullable)error;
- (void)openRecord:(JNRecord *)record activate:(BOOL)activate;
- (void)changed;
- (void)closeNote:(JNNote *)note;
@end
NS_ASSUME_NONNULL_END
