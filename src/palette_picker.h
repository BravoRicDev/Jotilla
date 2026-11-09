#ifndef JOTILLA_PALETTE_PICKER_H
#define JOTILLA_PALETTE_PICKER_H
#include <windows.h>

#define PALETTE_PICKER_CLASS L"Jotilla.PalettePicker"
typedef void (*PaletteSelection)(void *context, int palette, int color);
/* A favorite applies its first background color, retaining the current color
   when already in that palette. Removing it (-1) leaves the note unchanged. */
typedef BOOL (*PaletteFavorite)(void *context, int palette);
typedef struct PalettePickerOptions {
    int palette, color, favorite; /* -1 means no selected color / favorite. */
    BOOL text_mode;
    PaletteSelection select;
    PaletteFavorite favorite_changed;
    void *context;
} PalettePickerOptions;

/* One transient, owner-bound popup. Browsing never mutates the note. Color
   selection closes the popup before its callback; favorite changes keep it open. */
HWND palette_picker_show(HWND owner, POINT anchor, const PalettePickerOptions *options);
#endif
