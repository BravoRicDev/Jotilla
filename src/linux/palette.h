/* Dichiarazioni per la gestione delle palette. */

#ifndef JOTILLA_PALETTE_H
#define JOTILLA_PALETTE_H

#define PALETTE_COUNT 3
#define COLOR_COUNT 6

/* Nome della palette. */
const char *palette_name(int palette);
/* Colore esadecimale. */
const char *palette_color_hex(int palette, int color);
/* Colore inchiostro. */
const char *palette_ink_hex(int palette, int color);
/* Colore barra (scurito). */
const char *palette_bar_hex(int palette, int color);
/* Colore predefinito per nota. */
int palette_default_for_note(int index);

#endif
