#ifndef JOTILA_PALETTE_PICKER_H
#define JOTILA_PALETTE_PICKER_H
#include <windows.h>

#define PALETTE_PICKER_CLASS L"Jotila.PalettePicker"
typedef void (*PaletteSelection)(void *context, int palette, int color);
typedef BOOL (*PaletteFavorite)(void *context, int palette);
typedef struct PalettePickerOptions {
    int palette, color, favorite; /* -1 means no selected color / favorite. */
    BOOL text_mode;
    PaletteSelection select;
    PaletteFavorite favorite_changed;
    void *context;
} PalettePickerOptions;

/* One transient, owner-bound popup. Browsing and cancellation never mutate the
   note; the callback runs only after the selected popup has been destroyed. */
HWND palette_picker_show(HWND owner, POINT anchor, const PalettePickerOptions *options);
#endif
