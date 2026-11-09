#import "Palette.h"
#import <QuartzCore/QuartzCore.h>

@interface JNPalettePanel : NSPanel @end
@implementation JNPalettePanel
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return NO; }
@end
@interface JNPaletteColor : NSAccessibilityElement
@property(nonatomic, weak) JNPalette *picker;
@property(nonatomic) NSInteger color;
@end
@implementation JNPaletteColor
- (BOOL)accessibilityPerformPress { [self.picker choose:self.color]; return YES; }
@end
@interface JNWheel : NSView
@property(nonatomic, weak) JNPalette *picker;
@property(nonatomic) NSInteger palette, selected, hover;
@end
@implementation JNWheel {
    NSArray<NSBezierPath *> *_wedges;
    NSTrackingArea *_tracking;
    NSMutableArray<CAShapeLayer *> *_outlines;
}
- (BOOL)acceptsFirstResponder { return YES; }
- (void)setPalette:(NSInteger)palette {
    _palette=palette; NSMutableArray *paths=[NSMutableArray array];
    if(!_outlines) { self.wantsLayer=YES; _outlines=[NSMutableArray array]; for(NSInteger i=0;i<COLOR_COUNT;++i) { CAShapeLayer *layer=[CAShapeLayer layer]; layer.fillColor=nil; layer.lineWidth=2; layer.opacity=0; [self.layer addSublayer:layer]; [_outlines addObject:layer]; } }
    [CATransaction begin]; [CATransaction setDisableActions:YES];
    for (NSInteger c=0;c<PALETTE_SIZES[palette];++c) {
        NSBezierPath *p=[NSBezierPath bezierPath]; [p moveToPoint:NSMakePoint(110,110)];
        [p appendBezierPathWithArcWithCenter:NSMakePoint(110,110) radius:96 startAngle:90-c*360.0/PALETTE_SIZES[palette] endAngle:90-(c+1)*360.0/PALETTE_SIZES[palette] clockwise:YES];
        [p closePath]; [paths addObject:p];
        CGMutablePathRef path=CGPathCreateMutable(); NSPoint points[3];
        for(NSInteger i=0;i<p.elementCount;++i) { switch([p elementAtIndex:i associatedPoints:points]) {
            case NSBezierPathElementMoveTo: CGPathMoveToPoint(path,NULL,points[0].x,points[0].y); break;
            case NSBezierPathElementLineTo: CGPathAddLineToPoint(path,NULL,points[0].x,points[0].y); break;
            case NSBezierPathElementCurveTo: CGPathAddCurveToPoint(path,NULL,points[0].x,points[0].y,points[1].x,points[1].y,points[2].x,points[2].y); break;
            case NSBezierPathElementClosePath: CGPathCloseSubpath(path); break;
            default: break;
        } }
        _outlines[c].path=path; CGPathRelease(path); _outlines[c].strokeColor=JNColor(palette_ink((int)palette,(int)c)).CGColor;

    }
    for(CAShapeLayer *layer in _outlines) { [layer removeAllAnimations]; layer.opacity=0; }
    [CATransaction commit]; _wedges=paths; self.needsDisplay=YES;
}
- (void)updateHighlight {
    for(NSInteger i=0;i<(NSInteger)_outlines.count;++i) {
        CAShapeLayer *layer=_outlines[i]; float opacity=i==_hover?0.86:i==_selected?0.4:0;
        if(layer.opacity==opacity) continue;
        if(!NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion) {
            CABasicAnimation *animation=[CABasicAnimation animationWithKeyPath:@"opacity"]; animation.fromValue=@(layer.presentationLayer ? layer.presentationLayer.opacity : layer.opacity); animation.toValue=@(opacity); animation.duration=0.12; [layer addAnimation:animation forKey:@"hover"];
        }
        [CATransaction begin]; [CATransaction setDisableActions:YES]; layer.opacity=opacity; [CATransaction commit];
    }
}
- (void)setHover:(NSInteger)hover { if(hover==_hover) return; _hover=hover; [self updateHighlight]; }
- (void)setSelected:(NSInteger)selected { _selected=selected; [self updateHighlight]; }
- (NSArray *)accessibilityChildren {
    NSMutableArray *elements=[NSMutableArray array];
    for(NSInteger c=0;c<PALETTE_SIZES[_palette];++c) {
        COLORREF color=PALETTES[_palette][c]; NSString *label=[NSString stringWithFormat:@"Color %ld, #%02x%02x%02x",(long)c+1,GetRValue(color),GetGValue(color),GetBValue(color)];
        JNPaletteColor *element=[JNPaletteColor accessibilityElementWithRole:NSAccessibilityButtonRole frame:[self.window convertRectToScreen:[self convertRect:_wedges[c].bounds toView:nil]] label:label parent:self]; element.picker=self.picker; element.color=c; [elements addObject:element];
    }
    return elements;
}
- (void)updateTrackingAreas {
    [super updateTrackingAreas]; if (_tracking) [self removeTrackingArea:_tracking];
    _tracking=[[NSTrackingArea alloc] initWithRect:NSZeroRect options:NSTrackingMouseMoved|NSTrackingMouseEnteredAndExited|NSTrackingActiveInKeyWindow|NSTrackingInVisibleRect owner:self userInfo:nil]; [self addTrackingArea:_tracking];
}
- (NSInteger)hit:(NSPoint)p {
    CGFloat x=p.x-110,y=p.y-110; if (x*x+y*y>96*96) return -1;
    CGFloat angle=fmod(90-atan2(y,x)*180/M_PI+360,360);
    return MIN(PALETTE_SIZES[_palette]-1,(NSInteger)(angle*PALETTE_SIZES[_palette]/360));
}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    [NSGraphicsContext saveGraphicsState];
    NSShadow *shadow=[NSShadow new]; shadow.shadowColor=[NSColor.blackColor colorWithAlphaComponent:0.22]; shadow.shadowBlurRadius=5; shadow.shadowOffset=NSMakeSize(0,-2); [shadow set];
    [NSColor.blackColor setFill]; [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(14,14,192,192)] fill];
    [NSGraphicsContext restoreGraphicsState];
    for (NSInteger c=0;c<(NSInteger)_wedges.count;++c) {
        [JNColor(PALETTES[_palette][c]) setFill]; [_wedges[c] fill];
    }
    [NSColor.blackColor setStroke]; NSBezierPath *rim=[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(14,14,192,192)]; rim.lineWidth=0.5; [rim stroke];

}
- (void)mouseMoved:(NSEvent *)event { NSInteger h=[self hit:[self convertPoint:event.locationInWindow fromView:nil]]; if(h!=_hover) { self.hover=h; } }
- (void)mouseExited:(NSEvent *)event { (void)event; self.hover=-1; }
- (void)mouseDown:(NSEvent *)event { NSInteger c=[self hit:[self convertPoint:event.locationInWindow fromView:nil]]; if(c>=0) [self.picker choose:c]; }
- (void)keyDown:(NSEvent *)event {
    switch(event.keyCode) {
        case 123: [self.picker step:-1]; return;
        case 124: [self.picker step:1]; return;
        case 125: case 126: case 48: {
            NSInteger delta=(event.keyCode==126 || (event.modifierFlags&NSEventModifierFlagShift)) ? -1 : 1, count=PALETTE_SIZES[_palette];
            self.hover=_hover<0 ? (delta<0 ? count-1 : 0) : (_hover+delta+count)%count; return;
        }
        case 36: case 76: case 49: [self.picker choose:_hover>=0?_hover:MAX(0,_selected)]; return;
        case 53: [self.picker cancel:nil]; return;
    }
    if ([event.charactersIgnoringModifiers.lowercaseString isEqual:@"f"]) [self.picker toggleFavorite:nil];
    else [super keyDown:event];
}
@end

@implementation JNPalette {
    __weak JNNote *_note;
    JNWheel *_wheel;
    NSTextField *_name, *_mode;
    NSButton *_star;
    NSInteger _order;
    NSRange _selection;
    id _outside;
}
- (instancetype)initWithNote:(JNNote *)note {
    JNPalettePanel *panel=[[JNPalettePanel alloc] initWithContentRect:NSMakeRect(0,0,276,304) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
    if (!(self=[super initWithWindow:panel])) return nil;
    _note=note; _selection=note.editor.selectedRange;
    panel.appearance=[NSAppearance appearanceNamed:NSAppearanceNameAqua]; panel.opaque=NO; panel.backgroundColor=NSColor.clearColor; panel.hasShadow=NO; panel.level=NSPopUpMenuWindowLevel; panel.delegate=self; panel.releasedWhenClosed=NO;
    _wheel=[[JNWheel alloc] initWithFrame:NSMakeRect(28,74,220,220)]; _wheel.picker=self; _wheel.hover=-1;
    [panel.contentView addSubview:_wheel];
    NSView *caption=[[NSView alloc] initWithFrame:NSMakeRect(6,2,264,78)]; caption.wantsLayer=YES; caption.layer.backgroundColor=[NSColor colorWithSRGBRed:0.96 green:0.94 blue:0.89 alpha:1].CGColor; caption.layer.cornerRadius=8; [panel.contentView addSubview:caption positioned:NSWindowBelow relativeTo:_wheel];
    _mode=[NSTextField labelWithString:_selection.length ? @"Selected text" : @"Note color"]; _mode.frame=NSMakeRect(10,53,244,19); _mode.alignment=NSTextAlignmentCenter; _mode.font=[NSFont systemFontOfSize:11]; _mode.textColor=NSColor.darkGrayColor; [caption addSubview:_mode];
    _name=[NSTextField labelWithString:@""]; _name.frame=NSMakeRect(8,32,248,21); _name.alignment=NSTextAlignmentCenter; _name.lineBreakMode=NSLineBreakByTruncatingTail; _name.font=[NSFont boldSystemFontOfSize:12]; _name.textColor=NSColor.blackColor; [caption addSubview:_name];
    NSArray *names=@[@"‹",@"☆",@"›"]; SEL actions[]={@selector(previous:),@selector(toggleFavorite:),@selector(next:)};
    for (NSInteger i=0;i<3;++i) {
        NSButton *b=[NSButton buttonWithTitle:names[i] target:self action:actions[i]]; b.frame=NSMakeRect(62+i*48,4,44,27); b.bordered=NO; b.font=[NSFont systemFontOfSize:22]; b.contentTintColor=NSColor.blackColor; [caption addSubview:b];
        [b setAccessibilityLabel:@[@"Previous palette",@"Toggle favorite palette",@"Next palette"][i]];
        if (i==1) _star=b;
    }
    NSInteger current=[note.record.settings[@"palette"] integerValue]; current=MAX(0,MIN(PALETTE_COUNT-1,current));
    _order=MAX(0,PALETTE_POPULAR_INDEX[current]); [self updatePalette]; return self;
}
- (NSInteger)palette { return POPULAR_PALETTES[_order]; }
- (void)updatePalette {
    NSInteger p=self.palette; _wheel.palette=p; _wheel.hover=-1;
    _wheel.selected=[_note.record.settings[@"palette"] integerValue]==p ? [_note.record.settings[@"color"] integerValue] : -1;
    if (_selection.length) _wheel.selected=-1;
    _name.stringValue=JNPaletteName(p); self.window.title=_name.stringValue;
    NSString *favorite=_note.app.store.preferences[@"favorite_palette"];
    _star.title=favorite && favorite.integerValue==p ? @"★" : @"☆";
    _star.toolTip=[_star.title isEqual:@"★"] ? @"Remove favorite (F)" : @"Apply to this note and new notes; save favorite (F)";
    _wheel.needsDisplay=YES;
}
- (void)previous:(id)sender { (void)sender; [self step:-1]; }
- (void)next:(id)sender { (void)sender; [self step:1]; }
- (void)step:(NSInteger)step { _order=(_order+step+POPULAR_PALETTE_COUNT)%POPULAR_PALETTE_COUNT; [self updatePalette]; }
- (void)showAtPoint:(NSPoint)point {
    NSScreen *screen=_note.window.screen ?: NSScreen.mainScreen;
    for (NSScreen *s in NSScreen.screens) if(NSPointInRect(point,s.frame)) { screen=s; break; }
    [self.window setFrame:JNConstrainFrame(NSMakeRect(point.x-138,point.y-184,276,304),screen.visibleFrame) display:NO];
    [_note.window addChildWindow:self.window ordered:NSWindowAbove];
    [self.window makeKeyAndOrderFront:nil]; [self.window makeFirstResponder:_wheel];
    if (!NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion) { self.window.alphaValue=0; [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) { context.duration=0.12; self.window.animator.alphaValue=1; } completionHandler:nil]; }
    __weak JNPalette *weakSelf=self;
    _outside=[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown|NSEventMaskRightMouseDown handler:^NSEvent *(NSEvent *e) { JNPalette *p=weakSelf; if(p && e.window!=p.window) [p close]; return e; }];
}
- (void)choose:(NSInteger)color {
    if(color<0 || color>=PALETTE_SIZES[self.palette]) return;
    _note.editor.selectedRange=_selection;
    NSMenuItem *item=[NSMenuItem new]; item.tag=self.palette*COLOR_COUNT+color; [_note selectColor:item]; [self cancel:nil];
}
- (void)toggleFavorite:(id)sender {
    (void)sender; NSString *favorite=_note.app.store.preferences[@"favorite_palette"];
    NSMenuItem *item=[NSMenuItem new]; item.tag=favorite && favorite.integerValue==self.palette ? -1 : self.palette;
    [_note favorite:item]; [self updatePalette];
}
- (void)cancel:(id)sender { (void)sender; [self close]; [_note.window makeKeyAndOrderFront:nil]; [_note.window makeFirstResponder:_note.editor]; }
- (void)close { if(_outside) { [NSEvent removeMonitor:_outside]; _outside=nil; } [self.window.parentWindow removeChildWindow:self.window]; [self.window orderOut:nil]; }
- (void)windowDidResignKey:(NSNotification *)notification { (void)notification; [self close]; }
- (void)dealloc { if(_outside) [NSEvent removeMonitor:_outside]; }
@end
