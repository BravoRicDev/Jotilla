/* Interfaccia di astrazione piattaforma per Jotilla. */
#ifndef JOTILLA_PLATFORM_H
#define JOTILLA_PLATFORM_H
#include <gtk/gtk.h>

/* Nome del backend display corrente. */
const char *platform_name(void);
/* Imposta always-on-top (avviso su Wayland). */
void platform_set_keep_above(GtkWidget *window, gboolean above);
/* Imposta finestra appiccicosa (solo X11). */
void platform_set_sticky(GtkWidget *window, gboolean sticky);
/* True se il display e' Wayland. */
gboolean platform_is_wayland(void);
#endif
