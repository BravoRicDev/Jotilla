#ifndef JOTILA_PALETTE_ART_H
#define JOTILA_PALETTE_ART_H
#include <windows.h>
#include "colors.h"

enum { PICK_NONE=-1, PICK_PREVIOUS=-2, PICK_NEXT=-3, PICK_FAVORITE=-4 };
typedef struct PaletteArt PaletteArt;
typedef struct PaletteLayout {
    SIZE size;
    RECT wheel, footer, previous, next, favorite;
} PaletteLayout;

/* A transient premultiplied surface with cached coverage. Rendering a frame
   allocates nothing and performs no geometry, font or filesystem work. */
PaletteArt *palette_art_create(int dpi);
void palette_art_destroy(PaletteArt *art);
const PaletteLayout *palette_art_layout(const PaletteArt *art);
void palette_art_prepare(PaletteArt *art,int palette,int favorite,BOOL text_mode);
HDC palette_art_frame(PaletteArt *art,const BYTE weight[COLOR_COUNT],int hover,BOOL pressed);
int palette_art_hit(const PaletteArt *art,POINT point);
BOOL palette_art_contains(const PaletteArt *art,POINT point);
#endif
