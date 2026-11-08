#ifndef JOTILA_COLORS_H
#define JOTILA_COLORS_H

#include <windows.h>

#define COLOR_COUNT 6
#define DEFAULT_PALETTE_COUNT 3
#define PALETTE_COUNT 10

/* Persisted indices are append-only. The first three palettes and their six
   slots retain the original new-note sequence, including monochrome repeats. */
extern const COLORREF PALETTES[PALETTE_COUNT][COLOR_COUNT];
extern const wchar_t *const PALETTE_NAMES[PALETTE_COUNT];
extern const unsigned char PALETTE_SIZES[PALETTE_COUNT];
extern const unsigned char PALETTE_LIGHT_INK[PALETTE_COUNT];

static inline COLORREF palette_ink(int palette, int color) {
    return PALETTE_LIGHT_INK[palette] & (1u << color) ? RGB(255,255,255) : RGB(0,0,0);
}

static inline COLORREF palette_bar(COLORREF paper) {
    return RGB(GetRValue(paper) * 94 / 100, GetGValue(paper) * 94 / 100, GetBValue(paper) * 94 / 100);
}

#endif
