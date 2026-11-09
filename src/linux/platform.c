/* Astrazione piattaforma (Wayland/X11) per Jotilla. */
#include "platform.h"
#if defined(GDK_WINDOWING_WAYLAND)
#include <gdk/gdkwayland.h>
#endif
#if defined(GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#endif

/* Indica se il display corrente e' Wayland. */
gboolean platform_is_wayland(void)
{    GdkDisplay *display = gdk_display_get_default();

    if (display == NULL)
        return FALSE;

#if defined(GDK_WINDOWING_WAYLAND)
    return GDK_IS_WAYLAND_DISPLAY(display) ? TRUE : FALSE;
#else
    return FALSE;
#endif
}

/* Restituisce il nome del backend display ("wayland"/"x11"/"unknown"). */
const char *platform_name(void)
{    GdkDisplay *display = gdk_display_get_default();

    if (display == NULL)
        return "unknown";

#if defined(GDK_WINDOWING_WAYLAND)
    if (GDK_IS_WAYLAND_DISPLAY(display))
        return "wayland";
#endif

#if defined(GDK_WINDOWING_X11)
    if (GDK_IS_X11_DISPLAY(display))
        return "x11";
#endif

    return "unknown";
}

/* Imposta always-on-top, avvisando su Wayland (supporto non garantito). */
void platform_set_keep_above(GtkWidget *window, gboolean above)
{    if (window == NULL)
        return;

    if (platform_is_wayland())
        g_warning("always-on-top is not guaranteed on Wayland: the compositor "
                  "may ignore it. Run with GDK_BACKEND=x11 for reliable "
                  "keep-above behaviour.");

    gtk_window_set_keep_above(GTK_WINDOW(window), above);
}

/* Imposta finestra appiccicosa (solo X11). */
void platform_set_sticky(GtkWidget *window, gboolean sticky)
{    if (window == NULL)
        return;

    if (platform_is_wayland()) {
        g_warning("sticky windows are only supported on X11. Run with "
                  "GDK_BACKEND=x11 for sticky behaviour.");
        return;
    }

    if (sticky)
        gtk_window_stick(GTK_WINDOW(window));
    else
        gtk_window_unstick(GTK_WINDOW(window));
}
