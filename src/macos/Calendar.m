#import "Calendar.h"
@interface JNAccessibleDay : NSAccessibilityElement
@property(nonatomic, weak) JNCalendar *calendar;
@property(nonatomic) unsigned day;
@end
@implementation JNAccessibleDay
- (BOOL)accessibilityPerformPress { self.calendar.selected=self.day; if(self.calendar.choose) self.calendar.choose(self.day,[self.calendar cell:self.day]); return YES; }
@end
@implementation JNCalendar {
    NSArray<NSString *> *_months, *_weekdays, *_dayLabels;
    NSDictionary *_label, *_heading, *_todayStyle;
    NSTrackingArea *_tracking;
    unsigned _hover, _today;
}
- (instancetype)initWithFrame:(NSRect)frame {
    if(!(self=[super initWithFrame:frame])) return nil;
    NSDateFormatter *f=[NSDateFormatter new]; f.locale=NSLocale.currentLocale; f.calendar=[[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
    _months=f.monthSymbols; NSArray *days=f.veryShortStandaloneWeekdaySymbols;
    _weekdays=@[days[1],days[2],days[3],days[4],days[5],days[6],days[0]];
    _label=@{NSFontAttributeName:[NSFont systemFontOfSize:11],NSForegroundColorAttributeName:JNColor(RGB(58,52,43))};
    _heading=@{NSFontAttributeName:[NSFont boldSystemFontOfSize:13],NSForegroundColorAttributeName:JNColor(RGB(58,52,43))};
    _todayStyle=@{NSFontAttributeName:[NSFont boldSystemFontOfSize:11],NSForegroundColorAttributeName:JNColor(RGB(58,52,43))};
    NSMutableArray *numbers=[NSMutableArray array]; for(NSInteger day=1;day<=31;++day) [numbers addObject:[NSString stringWithFormat:@"%ld",(long)day]]; _dayLabels=numbers;
    [self refreshToday]; [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(refreshToday) name:NSCalendarDayChangedNotification object:nil];
    self.accessibilityLabel=@"Year calendar"; self.accessibilityRole=NSAccessibilityGroupRole; return self;
}
- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; }
- (void)refreshToday {
    NSDateComponents *now=[[[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian] components:NSCalendarUnitYear|NSCalendarUnitMonth|NSCalendarUnitDay fromDate:NSDate.date];
    unsigned today=(unsigned)(now.year*10000+now.month*100+now.day); if(today==_today) return;
    if(_today && _today/10000==_year) [self setNeedsDisplayInRect:[self cell:_today]]; _today=today; if(today/10000==_year) [self setNeedsDisplayInRect:[self cell:today]];
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (NSArray *)accessibilityChildren {
    NSMutableArray *elements=[NSMutableArray array];
    for(unsigned month=1;month<=12;++month) for(int day=1;day<=date_days((int)_year,(int)month);++day) {
        unsigned date=(unsigned)(_year*10000+month*100+day); NSString *label=[NSString stringWithFormat:@"%d %@ %ld, %lu notes",day,_months[month-1],(long)_year,(unsigned long)[_counts[@(date)] unsignedIntegerValue]];
        JNAccessibleDay *element=[JNAccessibleDay accessibilityElementWithRole:NSAccessibilityButtonRole frame:[self.window convertRectToScreen:[self convertRect:[self cell:date] toView:nil]] label:label parent:self]; element.calendar=self; element.day=date; [elements addObject:element];
    }
    return elements;
}
- (void)setYear:(NSInteger)year { year=MAX(DATE_MIN_YEAR,MIN(DATE_MAX_YEAR,year)); if(_year==year) return; _year=year; _hover=0; _selected=0; self.needsDisplay=YES; if(_yearChanged) _yearChanged(year); }
- (void)setCounts:(NSDictionary *)counts {
    if([_counts isEqual:counts]) return;
    NSMutableSet *changed=[NSMutableSet setWithArray:_counts.allKeys ?: @[]]; [changed addObjectsFromArray:counts.allKeys];
    for(NSNumber *date in changed) if(![_counts[date] isEqual:counts[date]] && date.unsignedIntValue/10000==_year) [self setNeedsDisplayInRect:[self cell:date.unsignedIntValue]];
    _counts=[counts copy];
}
- (void)setSelected:(NSUInteger)selected { if(_selected==selected) return; if(_selected) [self setNeedsDisplayInRect:[self cell:(unsigned)_selected]]; _selected=selected; if(selected) [self setNeedsDisplayInRect:[self cell:(unsigned)selected]]; }
- (NSInteger)columns { return self.bounds.size.width>=960 ? 4 : 3; }
- (NSRect)cell:(unsigned)day {
    unsigned month=day/100%100, number=day%100;
    if(month<1 || month>12 || !number) return NSZeroRect;
    NSInteger columns=[self columns]; CGFloat width=self.bounds.size.width/columns, height=self.bounds.size.height/(12/columns);
    NSInteger index=date_weekday((unsigned)(_year*10000+month*100+1))+number-1;
    return NSMakeRect((month-1)%columns*width+10+index%7*(width-20)/7,(month-1)/columns*height+42+index/7*(height-50)/6,(width-20)/7,(height-50)/6);
}
- (void)drawRect:(NSRect)dirty {
    [JNColor(RGB(244,238,226)) setFill]; NSRectFill(dirty);
    NSInteger columns=[self columns]; CGFloat width=self.bounds.size.width/columns,height=self.bounds.size.height/(12/columns);

    for(unsigned month=1;month<=12;++month) {
        NSRect area=NSMakeRect((month-1)%columns*width,(month-1)/columns*height,width,height);
        if(!NSIntersectsRect(area,dirty)) continue;
        [_months[month-1] drawAtPoint:NSMakePoint(area.origin.x+12,area.origin.y+6) withAttributes:_heading];
        for(NSInteger d=0;d<7;++d) [_weekdays[d] drawAtPoint:NSMakePoint(area.origin.x+16+d*(width-20)/7,area.origin.y+26) withAttributes:_label];
        for(int day=1;day<=date_days((int)_year,(int)month);++day) {
            unsigned date=(unsigned)(_year*10000+month*100+day); NSRect rect=[self cell:date]; if(!NSIntersectsRect(rect,dirty)) continue;
            if(date==_selected || date==_hover) { [JNColor(date==_selected ? RGB(211,194,162) : RGB(235,226,209)) setFill]; [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(rect,1,1) xRadius:4 yRadius:4] fill]; }
            if([_counts[@(date)] unsignedIntegerValue]) { [JNColor(RGB(58,52,43)) setStroke]; [[NSBezierPath bezierPathWithOvalInRect:NSInsetRect(rect,2,1)] stroke]; }
            NSString *label=_dayLabels[(NSUInteger)day-1]; NSSize size=[label sizeWithAttributes:date==_today?_todayStyle:_label];
            [label drawAtPoint:NSMakePoint(NSMidX(rect)-size.width/2,NSMidY(rect)-size.height/2) withAttributes:date==_today?_todayStyle:_label];
        }
    }
}
- (unsigned)dayAtPoint:(NSPoint)point {
    NSInteger cols=[self columns]; CGFloat w=self.bounds.size.width/cols,h=self.bounds.size.height/(12/cols);
    NSInteger col=(NSInteger)(point.x/w), row=(NSInteger)(point.y/h), month=row*cols+col+1;
    if(col<0 || col>=cols || month<1 || month>12) return 0;
    CGFloat x=point.x-col*w-10,y=point.y-row*h-42;
    if(x<0 || x>=w-20 || y<0 || y>=h-50) return 0;
    NSInteger day=(NSInteger)(y/((h-50)/6))*7+(NSInteger)(x/((w-20)/7))-date_weekday((unsigned)(_year*10000+month*100+1))+1;
    return day>=1 && day<=date_days((int)_year,(int)month) ? (unsigned)(_year*10000+month*100+day) : 0;
}
- (void)updateTrackingAreas { [super updateTrackingAreas]; if(_tracking) [self removeTrackingArea:_tracking]; _tracking=[[NSTrackingArea alloc] initWithRect:NSZeroRect options:NSTrackingInVisibleRect|NSTrackingMouseMoved|NSTrackingMouseEnteredAndExited|NSTrackingActiveInActiveApp owner:self userInfo:nil]; [self addTrackingArea:_tracking]; }
- (void)mouseMoved:(NSEvent *)event { unsigned day=[self dayAtPoint:[self convertPoint:event.locationInWindow fromView:nil]]; if(day==_hover) return; if(_hover) [self setNeedsDisplayInRect:[self cell:_hover]]; _hover=day; if(day) [self setNeedsDisplayInRect:[self cell:day]]; }
- (void)mouseExited:(NSEvent *)event { (void)event; if(_hover) [self setNeedsDisplayInRect:[self cell:_hover]]; _hover=0; }
- (void)mouseDown:(NSEvent *)event { [self.window makeFirstResponder:self]; unsigned day=[self dayAtPoint:[self convertPoint:event.locationInWindow fromView:nil]]; if(day) { self.selected=day; if(_choose) _choose(day,[self cell:day]); } }
- (void)moveSelection:(NSInteger)days {
    unsigned date=date_shift((unsigned)(_selected ?: _year*10000+101),(int)days); if(!date_valid(date)) return;
    if(date/10000!=_year) self.year=date/10000; self.selected=date; [self scrollRectToVisible:[self cell:date]];
}
- (void)keyDown:(NSEvent *)event {
    switch(event.keyCode) {
        case 123:[self moveSelection:-1];return; case 124:[self moveSelection:1];return;
        case 125:[self moveSelection:7];return; case 126:[self moveSelection:-7];return;
        case 116:self.year--;return; case 121:self.year++;return;
        case 115:self.selected=(NSUInteger)(_year*10000+101);return;
        case 119:self.selected=(NSUInteger)(_year*10000+1231);return;
        case 36:case 76:if(_selected && _choose) _choose(_selected,[self cell:(unsigned)_selected]);return;
        case 53:self.selected=0;return;
    } [super keyDown:event];
}
@end
