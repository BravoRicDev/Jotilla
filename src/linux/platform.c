/* Astrazione piattaforma (Wayland/X11) per Jotilla. */
#include "platform.h"
#include <glib/gstdio.h>
#if defined(GDK_WINDOWING_WAYLAND)
#include <gdk/gdkwayland.h>
#endif
#if defined(GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#include <X11/keysym.h>
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

/* ------------------------- Global hotkey (X11) ------------------------- */

static PlatformHotkeyFunc hotkey_cb = NULL;
static gpointer hotkey_data = NULL;

#if defined(GDK_WINDOWING_X11)
static KeyCode hotkey_plus = 0;
static KeyCode hotkey_equal = 0;
static KeyCode hotkey_kp_add = 0;

/* Filtro eventi X11: intercetta Ctrl+Alt++ e Ctrl+Alt+Shift++ . */
static GdkFilterReturn hotkey_filter(GdkXEvent *xevent, GdkEvent *event,
                                     gpointer data)
{    XEvent *xev = (XEvent *)xevent;
    const guint wanted = ControlMask | Mod1Mask;

    (void)event;
    (void)data;

    if (xev->type != KeyPress)
        return GDK_FILTER_CONTINUE;

    if (xev->xkey.keycode != hotkey_plus &&
        xev->xkey.keycode != hotkey_equal &&
        xev->xkey.keycode != hotkey_kp_add)
        return GDK_FILTER_CONTINUE;

    if ((xev->xkey.state & wanted) != wanted)
        return GDK_FILTER_CONTINUE;

    if (hotkey_cb != NULL)
        hotkey_cb(hotkey_data);
    return GDK_FILTER_REMOVE;
}
#endif

/* Installa l'hotkey globale di nuova nota. */
gboolean platform_hotkey_install(PlatformHotkeyFunc cb, gpointer data)
{    if (platform_is_wayland()) {
        g_warning("Global hotkey Ctrl+Alt++ is not available on Wayland. "
                  "Configure a custom shortcut in GNOME Settings instead.");
        return FALSE;
    }

#if defined(GDK_WINDOWING_X11)
    {
        GdkDisplay *display = gdk_display_get_default();
        Display *xdisplay;

        if (display == NULL || !GDK_IS_X11_DISPLAY(display)) {
            g_warning("Global hotkey Ctrl+Alt++ requires an X11 display.");
            return FALSE;
        }

        xdisplay = GDK_DISPLAY_XDISPLAY(display);
        hotkey_plus = XKeysymToKeycode(xdisplay, XK_plus);
        hotkey_equal = XKeysymToKeycode(xdisplay, XK_equal);
        hotkey_kp_add = XKeysymToKeycode(xdisplay, XK_KP_Add);
        hotkey_cb = cb;
        hotkey_data = data;
        gdk_window_add_filter(NULL, hotkey_filter, data);
        return TRUE;
    }
#else
    (void)cb;
    (void)data;
    g_warning("Global hotkey Ctrl+Alt++ requires GTK built with X11 support.");
    return FALSE;
#endif
}

/* Rimuove il filtro eventi dell'hotkey globale. */
void platform_hotkey_uninstall(void)
{
#if defined(GDK_WINDOWING_X11)
    if (hotkey_cb != NULL && hotkey_data != NULL)
        gdk_window_remove_filter(NULL, hotkey_filter, hotkey_data);
#endif
    hotkey_cb = NULL;
    hotkey_data = NULL;
}

/* ----------------------------- Autostart ------------------------------ */

/* Percorso del file .desktop di autostart dell'utente. */
static char *platform_autostart_path(void)
{    return g_build_filename(g_get_user_config_dir(), "autostart",
                            "org.jotilla.Jotilla.desktop", NULL);
}

/* True se la voce autostart esiste. */
gboolean platform_autostart_enabled(void)
{    char *path = platform_autostart_path();
    gboolean exists = g_file_test(path, G_FILE_TEST_IS_REGULAR);

    g_free(path);
    return exists;
}

/* Crea o rimuove la voce autostart puntando all'eseguibile corrente. */
gboolean platform_autostart_set(gboolean enabled)
{    char *path = platform_autostart_path();
    gboolean ok = FALSE;

    if (!enabled) {
        ok = !g_file_test(path, G_FILE_TEST_EXISTS) || (g_remove(path) == 0);
        g_free(path);
        return ok;
    }

    {
        char *dir = g_path_get_dirname(path);
        char *exe = g_file_read_link("/proc/self/exe", NULL);

        if (exe != NULL && g_mkdir_with_parents(dir, 0700) == 0) {
            GString *quoted = g_string_new("\"");
            const char *p;

            for (p = exe; *p != '\0'; ++p) {
                if (*p == '\\' || *p == '"')
                    g_string_append_c(quoted, '\\');
                g_string_append_c(quoted, *p);
            }
            g_string_append_c(quoted, '"');

            {
                char *contents = g_strdup_printf(
                    "[Desktop Entry]\n"
                    "Type=Application\n"
                    "Name=Jotilla\n"
                    "Comment=Sticky notes\n"
                    "Exec=%s\n"
                    "Terminal=false\n"
                    "X-GNOME-Autostart-enabled=true\n",
                    quoted->str);
                ok = g_file_set_contents(path, contents, -1, NULL);
                g_free(contents);
            }
            g_string_free(quoted, TRUE);
        }
        g_free(exe);
        g_free(dir);
    }

    g_free(path);
    return ok;
}
