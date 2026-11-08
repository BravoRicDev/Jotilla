/* Immutable offline catalog. One source generates all constant tables; browse
   order is independent of the stable IDs stored in existing archives. */
#include "colors.h"

_Static_assert(PALETTE_COUNT <= 256 && POPULAR_PALETTE_COUNT <= 128, "Palette indices must fit their compact tables.");
#define HEX(n) RGB(((n) >> 16) & 255, ((n) >> 8) & 255, (n) & 255)
const COLORREF PALETTES[PALETTE_COUNT][COLOR_COUNT] = {
    {RGB(255,238,151), RGB(199,229,255), RGB(213,239,187), RGB(255,210,218), RGB(229,214,255), RGB(255,222,180)},
    {RGB(255,255,255), RGB(28,28,28), RGB(255,255,255), RGB(28,28,28), RGB(255,255,255), RGB(28,28,28)},
    {RGB(255,238,151), RGB(255,224,112), RGB(255,245,184), RGB(250,218,128), RGB(255,239,105), RGB(255,250,205)},
#define POPULAR_PALETTE(id,order,source,name,ink,bar,a,b,c,d,e) [id] = {HEX(a),HEX(b),HEX(c),HEX(d),HEX(e)},
#include "popular_palettes.inc"
#undef POPULAR_PALETTE
};
const wchar_t *const PALETTE_NAMES[PALETTE_COUNT] = {
    L"Pastel", L"Black and white", L"Yellow only",
#define POPULAR_PALETTE(id,order,source,name,ink,bar,a,b,c,d,e) [id] = name,
#include "popular_palettes.inc"
#undef POPULAR_PALETTE
};
const unsigned char PALETTE_SIZES[PALETTE_COUNT] = {
    6,6,6,
#define POPULAR_PALETTE(id,order,source,name,ink,bar,a,b,c,d,e) [id] = 5,
#include "popular_palettes.inc"
#undef POPULAR_PALETTE
};
/* Relative-luminance contrast is evaluated offline, independently for each
   body and title-bar color. Runtime selection is one indexed bit test. */
const unsigned char PALETTE_LIGHT_INK[PALETTE_COUNT] = {
    0,42,0,
#define POPULAR_PALETTE(id,order,source,name,ink,bar,a,b,c,d,e) [id] = ink,
#include "popular_palettes.inc"
#undef POPULAR_PALETTE
};
const unsigned char PALETTE_BAR_LIGHT_INK[PALETTE_COUNT] = {
    0,42,0,
#define POPULAR_PALETTE(id,order,source,name,ink,bar,a,b,c,d,e) [id] = bar,
#include "popular_palettes.inc"
#undef POPULAR_PALETTE
};
const unsigned char POPULAR_PALETTES[POPULAR_PALETTE_COUNT] = {
#define POPULAR_PALETTE(id,order,source,name,ink,bar,a,b,c,d,e) [order] = id,
#include "popular_palettes.inc"
#undef POPULAR_PALETTE
};
const signed char PALETTE_POPULAR_INDEX[PALETTE_COUNT] = {
    -1,-1,-1,
#define POPULAR_PALETTE(id,order,source,name,ink,bar,a,b,c,d,e) [id] = order,
#include "popular_palettes.inc"
#undef POPULAR_PALETTE
};
