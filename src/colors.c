/* Immutable, offline palette catalog. Source links and selection criteria:
   docs/palettes.md. No color conversion, allocation or network work at paint time. */
#include "colors.h"

#define HEX(n) RGB(((n) >> 16) & 255, ((n) >> 8) & 255, (n) & 255)
const COLORREF PALETTES[PALETTE_COUNT][COLOR_COUNT] = {
    {RGB(255,238,151), RGB(199,229,255), RGB(213,239,187), RGB(255,210,218), RGB(229,214,255), RGB(255,222,180)},
    {RGB(255,255,255), RGB(28,28,28), RGB(255,255,255), RGB(28,28,28), RGB(255,255,255), RGB(28,28,28)},
    {RGB(255,238,151), RGB(255,224,112), RGB(255,245,184), RGB(250,218,128), RGB(255,239,105), RGB(255,250,205)},
    {HEX(0xffb3ba), HEX(0xffdfba), HEX(0xffffba), HEX(0xbaffc9), HEX(0xbae1ff)},
    {HEX(0xdfdfde), HEX(0xa2798f), HEX(0xd7c6cf), HEX(0x8caba8), HEX(0xebdada)},
    {HEX(0x2e4045), HEX(0x83adb5), HEX(0xc7bbc9), HEX(0x5e3c58), HEX(0xbfb5b2)},
    {HEX(0x414a4c), HEX(0x3b444b), HEX(0x353839), HEX(0x232b2b), HEX(0x0e1111)},
    {HEX(0xd2d4dc), HEX(0xafafaf), HEX(0xf8f8fa), HEX(0xe5e6eb), HEX(0xc0c2ce)},
    {HEX(0xffd4e5), HEX(0xd4ffea), HEX(0xeecbff), HEX(0xfeffa3), HEX(0xdbdcff)},
    {HEX(0xffdef2), HEX(0xf2e2ff), HEX(0xe2eeff), HEX(0xddfffc), HEX(0xffffe3)}
};
const wchar_t *const PALETTE_NAMES[PALETTE_COUNT] = {
    L"Pastel", L"Black and white", L"Yellow only", L"Pastel rainbow", L"Faded Rose",
    L"Muted colors", L"Soft Black", L"Grey-Lavender", L"Pastels", L"Kawaii pastel"
};
const unsigned char PALETTE_SIZES[PALETTE_COUNT] = {6,6,6,5,5,5,5,5,5,5};
/* Precomputed maximum of the minimum body/title-bar contrast. Integration
   tests independently verify every foreground against both backgrounds. */
const unsigned char PALETTE_LIGHT_INK[PALETTE_COUNT] = {0,42,0,0,0,9,31,0,0,0};
