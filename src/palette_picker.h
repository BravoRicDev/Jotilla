#ifndef IOTA_PALETTE_PICKER_H
#define IOTA_PALETTE_PICKER_H
#include <windows.h>

#define PALETTE_PICKER_CLASS L"IOTA.PalettePicker"
typedef void (*PaletteSelection)(void *context, int palette, int color);

/* One transient, owner-bound popup. Browsing and cancellation never mutate the
   note; the callback runs only after the selected popup has been destroyed. */
HWND palette_picker_show(HWND owner, POINT anchor, int palette, int color,
                         PaletteSelection select, void *context);
#endif
