/* Definizioni delle palette colori e accesso ai colori. */

#include <glib.h>
#include "palette.h"

typedef struct {
    const char *name;
    const char *colors[COLOR_COUNT];
    unsigned ink_mask;
} Palette;

static const Palette PALETTES[PALETTE_COUNT] = {
    {
        "Pastel",
        {"#ffee97", "#c7e5ff", "#d5efbb", "#ffd2da", "#e5d6ff", "#ffdeb4"},
        0u
    },
    {
        "Black and white",
        {"#ffffff", "#1c1c1c", "#ffffff", "#1c1c1c", "#ffffff", "#1c1c1c"},
        (1u << 1) | (1u << 3) | (1u << 5)
    },
    {
        "Yellow only",
        {"#ffee97", "#ffe070", "#fff5b8", "#fada80", "#ffef69", "#fffacd"},
        0u
    }
};

/* Limita l'indice palette all'intervallo valido (default 0). */
static int clamp_palette(int palette) {
    if (palette < 0 || palette >= PALETTE_COUNT) return 0;
    return palette;
}

/* Limita l'indice colore all'intervallo valido (default 0). */
static int clamp_color(int color) {
    if (color < 0 || color >= COLOR_COUNT) return 0;
    return color;
}

/* Converte due caratteri hex in valore 0-255. */
static unsigned parse_channel(const char *hex) {
    unsigned value = 0;
    int i;
    for (i = 0; i < 2; ++i) {
        char c = hex[i];
        value <<= 4;
        if (c >= '0' && c <= '9') value |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') value |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= (unsigned)(c - 'A' + 10);
    }
    return value;
}

/* Nome della palette. */
const char *palette_name(int palette) {
    return PALETTES[clamp_palette(palette)].name;
}

/* Colore esadecimale della palette/colore. */
const char *palette_color_hex(int palette, int color) {
    return PALETTES[clamp_palette(palette)].colors[clamp_color(color)];
}

/* Colore inchiostro: bianco se il bit della maschera è attivo. */
const char *palette_ink_hex(int palette, int color) {
    unsigned mask = PALETTES[clamp_palette(palette)].ink_mask;
    return (mask & (1u << clamp_color(color))) ? "#ffffff" : "#000000";
}

/* Colore scurito per la barra del titolo (cache statica). */
const char *palette_bar_hex(int palette, int color) {
    static char bar[PALETTE_COUNT][COLOR_COUNT][8];
    int p = clamp_palette(palette), c = clamp_color(color);
    const char *hex = PALETTES[p].colors[c];
    unsigned r = parse_channel(hex + 1);
    unsigned g = parse_channel(hex + 3);
    unsigned b = parse_channel(hex + 5);

    g_snprintf(bar[p][c], sizeof(bar[p][c]), "#%02x%02x%02x",
               r * 94 / 100, g * 94 / 100, b * 94 / 100);
    return bar[p][c];
}

/* Colore predefinito per una nota in base all'indice. */
int palette_default_for_note(int index) {
    int value = index % COLOR_COUNT;
    if (value < 0) value = 0;
    if (value >= COLOR_COUNT) value = COLOR_COUNT - 1;
    return value;
}
