/* Native C rasterizer. Analytic pixel coverage keeps the circle, seams and
   focus outline smooth at any DPI; coverage is cached until the wedge count changes. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "palette_art.h"

typedef struct Coverage { BYTE wedge,neighbor,blend,alpha,stroke,rim; } Coverage;
struct PaletteArt {
    PaletteLayout layout;
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ old_bitmap;
    HFONT font,small;
    DWORD *pixels,*base,colors[COLOR_COUNT],inks[COLOR_COUNT];
    Coverage *coverage;
    int dpi,side,count;
};
static const POINT rays5[]={{0,-10000},{9511,-3090},{5878,8090},{-5878,8090},{-9511,-3090},{0,-10000}};
static const POINT rays6[]={{0,-10000},{8660,-5000},{8660,5000},{0,10000},{-8660,5000},{-8660,-5000},{0,-10000}};
static int scale(const PaletteArt *a,int n) { return MulDiv(n,a->dpi,96); }
static BYTE coverage(float n) { return (BYTE)(n<=0?0:n>=1?255:n*255+.5f); }
static DWORD rgb(COLORREF c) { return (DWORD)GetRValue(c)<<16 | (DWORD)GetGValue(c)<<8 | GetBValue(c); }
static DWORD mix(DWORD a,DWORD b,unsigned t) {
    unsigned r=(((a>>16)&255)*(255-t)+((b>>16)&255)*t+127)/255;
    unsigned g=(((a>>8)&255)*(255-t)+((b>>8)&255)*t+127)/255;
    unsigned bl=((a&255)*(255-t)+(b&255)*t+127)/255;
    return r<<16|g<<8|bl;
}
static DWORD over(DWORD background,DWORD color,unsigned alpha) {
    return (alpha+((background>>24)*(255-alpha)+127)/255)<<24 | mix(background,color,alpha);
}
static int sector(int count,int x,int y) {
    const POINT *ray=count==5?rays5:rays6;
    int i;
    for(i=0;i<count;++i)
        if((int64_t)ray[i].x*y-(int64_t)ray[i].y*x>=0 &&
           (int64_t)x*ray[i+1].y-(int64_t)y*ray[i+1].x>=0) return i;
    return 0;
}
static float rounded_distance(float x,float y,const RECT *r,float radius) {
    float dx=fabsf(x-(r->left+r->right)*.5f)-(r->right-r->left)*.5f+radius;
    float dy=fabsf(y-(r->top+r->bottom)*.5f)-(r->bottom-r->top)*.5f+radius;
    float ax=max(dx,0),ay=max(dy,0);
    return sqrtf(ax*ax+ay*ay)+min(max(dx,dy),0)-radius;
}
static void geometry(PaletteArt *a,int count) {
    const POINT *rays=count==5?rays5:rays6;
    float radius=a->side*.5f,stroke=1.8f*a->dpi/96,border=.5f*a->dpi/96;
    int x,y;
    a->count=count;
    for(y=0;y<a->side;++y) for(x=0;x<a->side;++x) {
        Coverage *pixel=a->coverage+(size_t)y*a->side+x;
        float dx=x+.5f-radius,dy=y+.5f-radius;
        float radial=radius-sqrtf(dx*dx+dy*dy);
        int w=sector(count,(int)(dx*256),(int)(dy*256));
        float left=(rays[w].x*dy-rays[w].y*dx)/10000;
        float right=(dx*rays[w+1].y-dy*rays[w+1].x)/10000;
        float edge=min(left,right);
        pixel->wedge=(BYTE)w;
        pixel->neighbor=(BYTE)((w+(left<right?count-1:1))%count);
        pixel->blend=coverage(.5f-edge);
        pixel->alpha=coverage(radial+.5f);
        pixel->stroke=coverage(stroke-min(edge,radial));
        /* The black rim occupies exactly the outer half-DIP of coverage,
           including fractional edge pixels, and survives hover highlighting. */
        pixel->rim=pixel->alpha ? (BYTE)((pixel->alpha-coverage(radial-border+.5f))*255/pixel->alpha) : 0;
    }
}
static float segment_distance(float x,float y,float ax,float ay,float bx,float by) {
    float dx=bx-ax,dy=by-ay,t=((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy);
    t=max(0,min(1,t)); dx=x-ax-t*dx; dy=y-ay-t*dy;
    return sqrtf(dx*dx+dy*dy);
}
static void line(PaletteArt *a,float ax,float ay,float bx,float by,float width,DWORD ink) {
    int x,y,left=max(0,(int)(min(ax,bx)-width-1)),right=min(a->layout.size.cx,(int)(max(ax,bx)+width+2));
    int top=max(0,(int)(min(ay,by)-width-1)),bottom=min(a->layout.size.cy,(int)(max(ay,by)+width+2));
    for(y=top;y<bottom;++y) for(x=left;x<right;++x) {
        DWORD *pixel=a->pixels+(size_t)y*a->layout.size.cx+x;
        *pixel=over(*pixel,ink,coverage(width*.5f+.5f-segment_distance(x+.5f,y+.5f,ax,ay,bx,by)));
    }
}
static void arrow(PaletteArt *a,const RECT *rect,int direction) {
    float x=(rect->left+rect->right)*.5f,y=(rect->top+rect->bottom)*.5f;
    float d=4.0f*a->dpi/96,t=1.2f*a->dpi/96;
    line(a,x-d,y,x+d,y,t,0x292820);
    line(a,x+direction*d,y,x,y-d,t,0x292820);
    line(a,x+direction*d,y,x,y+d,t,0x292820);
}
static void star(PaletteArt *a,BOOL filled) {
    static const POINT points[10]={{0,-1000},{235,-324},{951,-309},{380,124},{588,809},
                                  {0,400},{-588,809},{-380,124},{-951,-309},{-235,-324}};
    const RECT *r=&a->layout.favorite;
    float cx=(r->left+r->right)*.5f,cy=(r->top+r->bottom)*.5f,radius=7.5f*a->dpi/96;
    int x,y,i,j;
    for(y=r->top;y<r->bottom;++y) for(x=r->left;x<r->right;++x) {
        float px=(x+.5f-cx)/radius,py=(y+.5f-cy)/radius,distance=100;
        BOOL inside=FALSE;
        DWORD *pixel=a->pixels+(size_t)y*a->layout.size.cx+x;
        for(i=0,j=9;i<10;j=i++) {
            float ax=points[i].x/1000.0f,ay=points[i].y/1000.0f;
            float bx=points[j].x/1000.0f,by=points[j].y/1000.0f;
            float d=segment_distance(px,py,ax,ay,bx,by)*radius;
            if(d<distance) distance=d;
            if((ay>py)!=(by>py) && px<(bx-ax)*(py-ay)/(by-ay)+ax) inside=!inside;
        }
        if(filled) *pixel=over(*pixel,0xb28a27,coverage(.5f+(inside?distance:-distance)));
        *pixel=over(*pixel,filled?0x745815:0x49463c,coverage(.5f+.6f*a->dpi/96-distance));
    }
}

PaletteArt *palette_art_create(int dpi) {
    PaletteArt *a;
    PaletteLayout layout={0};
    BITMAPINFO info={0};
    int pad,side,top,button;
    size_t pixels,shapes;
    if(dpi<48 || dpi>768) return NULL;
    pad=MulDiv(10,dpi,96); side=MulDiv(240,dpi,96); button=MulDiv(30,dpi,96);
    SetRect(&layout.wheel,pad,pad,pad+side,pad+side);
    top=pad+side+MulDiv(12,dpi,96);
    SetRect(&layout.footer,pad,top,pad+side,top+MulDiv(58,dpi,96));
    SetRect(&layout.previous,pad,top,pad+button,top+MulDiv(34,dpi,96));
    layout.next=layout.previous; OffsetRect(&layout.next,side-button,0);
    layout.favorite=layout.next; OffsetRect(&layout.favorite,-button,0);
    layout.size.cx=side+pad*2; layout.size.cy=layout.footer.bottom+pad;
    pixels=(size_t)layout.size.cx*layout.size.cy; shapes=(size_t)side*side;
    a=(PaletteArt *)calloc(1,sizeof(*a)+pixels*sizeof(DWORD)+shapes*sizeof(Coverage));
    if(!a) return NULL;
    a->layout=layout; a->side=side; a->dpi=dpi;
    a->base=(DWORD *)(a+1); a->coverage=(Coverage *)(a->base+pixels);
    a->dc=CreateCompatibleDC(NULL);
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=layout.size.cx;
    info.bmiHeader.biHeight=-layout.size.cy; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    a->bitmap=CreateDIBSection(NULL,&info,DIB_RGB_COLORS,(void **)&a->pixels,NULL,0);
    a->font=CreateFontW(-MulDiv(10,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FIXED_PITCH,L"Consolas");
    a->small=CreateFontW(-MulDiv(8,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FIXED_PITCH,L"Consolas");
    if(!a->dc || !a->bitmap || !a->font || !a->small) { palette_art_destroy(a); return NULL; }
    a->old_bitmap=SelectObject(a->dc,a->bitmap);
    return a;
}
void palette_art_destroy(PaletteArt *a) {
    if(!a) return;
    if(a->old_bitmap) SelectObject(a->dc,a->old_bitmap);
    if(a->bitmap) DeleteObject(a->bitmap);
    if(a->font) DeleteObject(a->font);
    if(a->small) DeleteObject(a->small);
    if(a->dc) DeleteDC(a->dc);
    free(a);
}
const PaletteLayout *palette_art_layout(const PaletteArt *a) { return &a->layout; }

void palette_art_prepare(PaletteArt *a,int palette,int favorite,BOOL text_mode) {
    const PaletteLayout *l=&a->layout;
    float cx=(l->wheel.left+l->wheel.right)*.5f,cy=(l->wheel.top+l->wheel.bottom)*.5f;
    float radius=a->side*.5f,soft=6.0f*a->dpi/96,corner=5.0f*a->dpi/96;
    RECT label={l->previous.right,l->previous.top,l->favorite.left,l->previous.bottom},measured;
    RECT mode={l->footer.left,l->previous.bottom,l->footer.right,l->footer.bottom-scale(a,4)};
    HGDIOBJ old;
    int x,y,i;
    if(a->count!=PALETTE_SIZES[palette]) geometry(a,PALETTE_SIZES[palette]);
    for(i=0;i<a->count;++i) { a->colors[i]=rgb(PALETTES[palette][i]); a->inks[i]=rgb(palette_ink(palette,i)); }
    for(y=0;y<l->size.cy;++y) for(x=0;x<l->size.cx;++x) {
        float dx=x+.5f-cx,dy=y+.5f-cy-scale(a,2);
        float circle=sqrtf(dx*dx+dy*dy)-radius;
        float footer=rounded_distance(x+.5f,y+.5f,&l->footer,corner);
        float shadow=min(circle,footer-scale(a,2)),t=max(0,min(1,1-shadow/soft));
        DWORD pixel=(DWORD)(24*t*t)<<24;
        if(footer<.5f) {
            DWORD fill=mix(0x292820,0xf4eee2,coverage(-footer-.6f));
            pixel=over(pixel,fill,coverage(.5f-footer));
        }
        a->pixels[(size_t)y*l->size.cx+x]=pixel;
    }
    old=SelectObject(a->dc,a->font);
    SetBkMode(a->dc,TRANSPARENT); SetTextColor(a->dc,RGB(41,40,32));
    measured=label;
    DrawTextW(a->dc,PALETTE_NAMES[palette],-1,&measured,DT_CENTER|DT_WORDBREAK|DT_CALCRECT|DT_NOPREFIX);
    label.top+=max(0,((label.bottom-label.top)-(measured.bottom-measured.top))/2);
    DrawTextW(a->dc,PALETTE_NAMES[palette],-1,&label,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
    SelectObject(a->dc,a->small); SetTextColor(a->dc,RGB(100,94,80));
    DrawTextW(a->dc,text_mode?L"Selected text":L"Note color",-1,&mode,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    SelectObject(a->dc,old); GdiFlush();
    /* GDI text clears alpha; its entire clipped drawing area is opaque. */
    for(y=l->footer.top+scale(a,3);y<l->footer.bottom-scale(a,3);++y)
        for(x=l->footer.left+scale(a,5);x<l->footer.right-scale(a,5);++x)
            a->pixels[(size_t)y*l->size.cx+x]|=0xff000000;
    arrow(a,&l->previous,-1); arrow(a,&l->next,1); star(a,palette==favorite);
    memcpy(a->base,a->pixels,(size_t)l->size.cx*l->size.cy*sizeof(DWORD));
}
HDC palette_art_frame(PaletteArt *a,const BYTE weight[COLOR_COUNT],int hover,BOOL pressed) {
    const PaletteLayout *l=&a->layout;
    int x,y;
    memcpy(a->pixels,a->base,(size_t)l->size.cx*l->size.cy*sizeof(DWORD));
    for(y=0;y<a->side;++y) for(x=0;x<a->side;++x) {
        const Coverage *p=a->coverage+(size_t)y*a->side+x;
        DWORD color,neighbor,*pixel;
        unsigned w=p->wedge,n=p->neighbor;
        if(!p->alpha) continue;
        color=mix(a->colors[w],a->inks[w],(unsigned)weight[w]*p->stroke/255);
        neighbor=mix(a->colors[n],a->inks[n],(unsigned)weight[n]*p->stroke/255);
        color=mix(color,neighbor,p->blend);
        if(p->rim) color=mix(color,0,p->rim);
        pixel=a->pixels+(size_t)(y+l->wheel.top)*l->size.cx+x+l->wheel.left;
        *pixel=over(*pixel,color,p->alpha);
    }
    if(hover<=PICK_PREVIOUS) {
        const RECT *r=hover==PICK_PREVIOUS?&l->previous:hover==PICK_NEXT?&l->next:&l->favorite;
        for(y=r->top+scale(a,4);y<r->bottom-scale(a,4);++y)
            for(x=r->left+scale(a,3);x<r->right-scale(a,3);++x) {
                DWORD *pixel=a->pixels+(size_t)y*l->size.cx+x;
                *pixel=0xff000000|mix(*pixel,0x292820,pressed?36:17);
            }
    }
    return a->dc;
}
BOOL palette_art_contains(const PaletteArt *a,POINT point) {
    const PaletteLayout *l=&a->layout;
    int x=point.x-l->wheel.left-a->side/2,y=point.y-l->wheel.top-a->side/2;
    if((int64_t)x*x+(int64_t)y*y<=(int64_t)a->side*a->side/4) return TRUE;
    return PtInRect(&l->footer,point);
}
int palette_art_hit(const PaletteArt *a,POINT point) {
    const PaletteLayout *l=&a->layout;
    int x=point.x-l->wheel.left-a->side/2,y=point.y-l->wheel.top-a->side/2;
    if(PtInRect(&l->previous,point)) return PICK_PREVIOUS;
    if(PtInRect(&l->next,point)) return PICK_NEXT;
    if(PtInRect(&l->favorite,point)) return PICK_FAVORITE;
    if((int64_t)x*x+(int64_t)y*y>(int64_t)a->side*a->side/4) return PICK_NONE;
    return sector(a->count,x,y);
}
