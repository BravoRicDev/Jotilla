#ifndef JOTILLA_COLORS_H
#define JOTILLA_COLORS_H

#include <windows.h>

#define COLOR_COUNT 6
#define DEFAULT_PALETTE_COUNT 3
enum { POPULAR_PALETTE_COUNT = 0
#define POPULAR_PALETTE(...) + 1
#include "popular_palettes.inc"
#undef POPULAR_PALETTE
};
#define PALETTE_COUNT (DEFAULT_PALETTE_COUNT + POPULAR_PALETTE_COUNT)

/* Persisted indices are append-only. The first three palettes and their six
   slots retain the original new-note sequence, including monochrome repeats. */
extern const COLORREF PALETTES[PALETTE_COUNT][COLOR_COUNT];
extern const wchar_t *const PALETTE_NAMES[PALETTE_COUNT];
extern const unsigned char PALETTE_SIZES[PALETTE_COUNT];
extern const unsigned char PALETTE_LIGHT_INK[PALETTE_COUNT];
extern const unsigned char PALETTE_BAR_LIGHT_INK[PALETTE_COUNT];
extern const unsigned char POPULAR_PALETTES[POPULAR_PALETTE_COUNT];
extern const signed char PALETTE_POPULAR_INDEX[PALETTE_COUNT];

static inline COLORREF palette_ink(int palette, int color) {
    return PALETTE_LIGHT_INK[palette] & (1u << color) ? RGB(255,255,255) : RGB(0,0,0);
}

static inline COLORREF palette_bar_ink(int palette, int color) {
    return PALETTE_BAR_LIGHT_INK[palette] & (1u << color) ? RGB(255,255,255) : RGB(0,0,0);
}

static inline COLORREF palette_bar(COLORREF paper) {
    return RGB(GetRValue(paper) * 94 / 100, GetGValue(paper) * 94 / 100, GetBValue(paper) * 94 / 100);
}

#endif
