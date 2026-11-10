/* Finestra nota GTK: crea, modifica e salva le note. */
/* Gestisce stile, menu, scorciatoie e stato della finestra. */
#include <gtk/gtk.h>
#include <atk/atk.h>
#include <gio/gio.h>

#include "jotilla.h"
#include "note_window.h"
#include "palette.h"
#include "palette_picker.h"
#include "archive_window.h"
#include "rtf.h"
#include "platform.h"
#include "calendar.h"
#include "date.h"

/* La funzione puo' essere assente in build parziali dell'archivio. */
extern void archive_window_focus_search(GtkWidget *window) __attribute__((weak));

typedef struct {
    NoteChangedFunc fn;
    gpointer data;
} ChangedHook;

typedef void (*SimpleNoteFunc)(gpointer);

typedef struct {
    SimpleNoteFunc fn;
    gpointer data;
} SimpleHook;

static guint note_window_seq = 0;

/* Restituisce lo store associato alla finestra. */
static Store *window_store(GtkWidget *window)
{
    return g_object_get_data(G_OBJECT(window), "store");
}

/* Restituisce la nota associata alla finestra. */
static Note *window_note(GtkWidget *window)
{
    return g_object_get_data(G_OBJECT(window), "note");
}

/* Restituisce la text view se ancora valida. */
static GtkTextView *window_view(GtkWidget *window)
{
    if (!GPOINTER_TO_INT(g_object_get_data(G_OBJECT(window), "view-alive")))
        return NULL;
    return g_object_get_data(G_OBJECT(window), "textview");
}

typedef struct {
    char *rtf;
    int cursor;
} UndoSnapshot;

/* Salva il contenuto e lo stato della finestra. */
static void save_now(GtkWidget *window);
/* Applica un colore di testo alla selezione. */
static void apply_foreground(GtkWidget *window, const char *hex);
/* Registra lo stato corrente nella pila undo. */
static void snapshot_push(GtkWidget *window, const char *rtf, int cursor);
/* Offset del cursore corrente. */
static int snapshot_cursor(GtkWidget *window);
/* Azzera le pile e registra lo stato corrente come base. */
static void snapshot_reset(GtkWidget *window);
/* Libera tutte le istantanee della finestra. */
static void snapshot_clear_all(GtkWidget *window);
/* Annulla/ripristina l'ultima modifica. */
static void do_undo(GtkWidget *window);
static void do_redo(GtkWidget *window);

/* Annulla il timer di salvataggio pendente. */
static void remove_timeout(GtkWidget *window)
{
    guint id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(window), "save-timeout"));
    if (id != 0) {
        g_source_remove(id);
        g_object_set_data(G_OBJECT(window), "save-timeout", GUINT_TO_POINTER(0));
    }
}

/* Callback del timer: esegue il salvataggio. */
static gboolean on_save_timeout(gpointer data)
{
    GtkWidget *window = data;

    g_object_set_data(G_OBJECT(window), "save-timeout", GUINT_TO_POINTER(0));
    save_now(window);
    return G_SOURCE_REMOVE;
}

/* Programma un salvataggio ritardato (debounce). */
static void schedule_save(GtkWidget *window)
{
    guint id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(window), "save-timeout"));

    if (id != 0)
        g_source_remove(id);
    id = g_timeout_add(400, on_save_timeout, window); /* debounce 400 ms */
    g_object_set_data(G_OBJECT(window), "save-timeout", GUINT_TO_POINTER(id));
}

/* Segna la nota come modificata e programma il salvataggio. */
static void touch(GtkWidget *window)
{
    g_object_set_data(G_OBJECT(window), "dirty", GINT_TO_POINTER(1));
    schedule_save(window);
}

/* Aggiorna titolo ed etichetta dalla prima riga. */
static void update_title(GtkWidget *window)
{
    GtkTextView *view = window_view(window);
    GtkWidget *label = g_object_get_data(G_OBJECT(window), "title-label");
    char *title = NULL;
    Note *note = window_note(window);

    if (label == NULL)
        return;

    if (note != NULL && note->has_title && note->title != NULL && note->title[0] != '\0') {
        title = g_strdup(note->title);
    } else if (view != NULL) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
        if (buffer != NULL) {
            GtkTextIter st, en;
            gtk_text_buffer_get_start_iter(buffer, &st);
            en = st;
            gtk_text_iter_forward_to_line_end(&en);
            title = gtk_text_buffer_get_text(buffer, &st, &en, FALSE);
            if (title != NULL)
                g_strstrip(title);
        }
    }

    if (title == NULL || title[0] == '\0') {
        g_free(title);
        title = g_strdup("Untitled");
    }
    if (title == NULL)
        title = g_strdup("Untitled");

    gtk_label_set_text(GTK_LABEL(label), title);
    gtk_window_set_title(GTK_WINDOW(window), title);
    g_free(title);
}

/* Crea i tag di formattazione se mancanti. */
static void ensure_tags(GtkTextBuffer *buffer)
{
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);

    if (gtk_text_tag_table_lookup(table, "bold") == NULL)
        gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    if (gtk_text_tag_table_lookup(table, "italic") == NULL)
        gtk_text_buffer_create_tag(buffer, "italic", "style", PANGO_STYLE_ITALIC, NULL);
    if (gtk_text_tag_table_lookup(table, "underline") == NULL)
        gtk_text_buffer_create_tag(buffer, "underline", "underline", PANGO_UNDERLINE_SINGLE, NULL);
    if (gtk_text_tag_table_lookup(table, "strike") == NULL)
        gtk_text_buffer_create_tag(buffer, "strike", "strikethrough", TRUE, NULL);
}

/* Aggiunge/rimuove il bordo rosso di errore salvataggio. */
static void set_save_error(GtkWidget *window, gboolean failed)
{
    GtkStyleContext *context = gtk_widget_get_style_context(window);

    if (failed)
        gtk_style_context_add_class(context, "save-error");
    else
        gtk_style_context_remove_class(context, "save-error");
}

/* Salva il contenuto e lo stato della finestra. */
static void save_now(GtkWidget *window)
{
    Store *store = window_store(window);
    Note *note = window_note(window);
    GtkTextView *view = window_view(window);

    if (store == NULL || note == NULL)
        return;

    if (view != NULL) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
        if (buffer != NULL) {
            GtkTextIter start, end;
            char *text;
            char *rtf;

            gtk_text_buffer_get_bounds(buffer, &start, &end);
            text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
            g_free(note->text);
            note->text = text != NULL ? text : g_strdup("");

            rtf = rtf_from_buffer(buffer);
            g_free(note->rtf);
            note->rtf = rtf != NULL ? rtf : g_strdup("");
        }
    }

    if (gtk_widget_get_mapped(window)) {
        int width = 0, height = 0;
        gtk_window_get_size(GTK_WINDOW(window), &width, &height);
        if (width > 0)
            note->w = width;
        if (height > 0 && !note->collapsed)
            note->h = height;

        /* Su Wayland la posizione è gestita dal compositor */
    if (!platform_is_wayland() && gtk_widget_get_window(window) != NULL) {
            int x = 0, y = 0;
            gtk_window_get_position(GTK_WINDOW(window), &x, &y);
            note->x = x;
            note->y = y;
        }
    }

    {
        GError *err = NULL;
        gboolean saved = store_save(store, note, &err);

        if (!saved) {
            if (err != NULL) {
                g_warning("Could not save note: %s", err->message);
                g_error_free(err);
            }
        }

        remove_timeout(window);
        if (saved) {
            set_save_error(window, FALSE);
            g_object_set_data(G_OBJECT(window), "dirty", GINT_TO_POINTER(0));
            if (!GPOINTER_TO_INT(g_object_get_data(G_OBJECT(window), "restoring")) &&
                note->rtf != NULL)
                snapshot_push(window, note->rtf, snapshot_cursor(window));
        } else {
            guint id;

            set_save_error(window, TRUE);
            g_object_set_data(G_OBJECT(window), "dirty", GINT_TO_POINTER(1));
            id = g_timeout_add(1000, on_save_timeout, window);
            g_object_set_data(G_OBJECT(window), "save-timeout", GUINT_TO_POINTER(id));
        }
    }

    {
        ChangedHook *hook = g_object_get_data(G_OBJECT(window), "changed-hook");
        if (hook != NULL && hook->fn != NULL)
            hook->fn(note, hook->data);
    }
}

/* Libera una istantanea. */
static void snapshot_free(gpointer data)
{
    UndoSnapshot *snap = data;

    if (snap == NULL)
        return;
    g_free(snap->rtf);
    g_free(snap);
}

/* Copia una istantanea. */
static UndoSnapshot *snapshot_clone(const UndoSnapshot *src)
{
    UndoSnapshot *copy;

    if (src == NULL)
        return NULL;

    copy = g_new0(UndoSnapshot, 1);
    copy->rtf = g_strdup(src->rtf != NULL ? src->rtf : "");
    copy->cursor = src->cursor;
    return copy;
}

/* Restituisce (creandola se richiesto) una pila di istantanee. */
static GPtrArray *snapshot_stack(GtkWidget *window, const char *key, gboolean create)
{
    GPtrArray *stack = g_object_get_data(G_OBJECT(window), key);

    if (stack == NULL && create) {
        stack = g_ptr_array_new_with_free_func(snapshot_free);
        g_object_set_data_full(G_OBJECT(window), key, stack,
                               (GDestroyNotify)g_ptr_array_unref);
    }
    return stack;
}

/* Offset del cursore corrente. */
static int snapshot_cursor(GtkWidget *window)
{
    GtkTextView *view = window_view(window);
    GtkTextBuffer *buffer;
    GtkTextIter iter;

    if (view == NULL)
        return 0;

    buffer = gtk_text_view_get_buffer(view);
    if (buffer == NULL)
        return 0;

    gtk_text_buffer_get_iter_at_mark(buffer, &iter,
                                     gtk_text_buffer_get_insert(buffer));
    return gtk_text_iter_get_offset(&iter);
}

/* Registra lo stato corrente nella pila undo. */
static void snapshot_push(GtkWidget *window, const char *rtf, int cursor)
{
    GPtrArray *stack = snapshot_stack(window, "undo-stack", TRUE);
    GPtrArray *redo;
    UndoSnapshot *snap;
    const char *value = rtf != NULL ? rtf : "";

    if (stack == NULL)
        return;

    if (stack->len > 0) {
        UndoSnapshot *top = g_ptr_array_index(stack, stack->len - 1);
        if (top != NULL && g_strcmp0(top->rtf, value) == 0)
            return;
    }

    snap = g_new0(UndoSnapshot, 1);
    snap->rtf = g_strdup(value);
    snap->cursor = cursor;
    g_ptr_array_add(stack, snap);

    while (stack->len > 100)
        g_ptr_array_remove_index(stack, 0);

    redo = snapshot_stack(window, "redo-stack", TRUE);
    if (redo != NULL)
        g_ptr_array_set_size(redo, 0);
}

/* Azzera le pile e registra lo stato corrente come base. */
static void snapshot_reset(GtkWidget *window)
{
    GtkTextView *view = window_view(window);
    GtkTextBuffer *buffer;
    char *rtf;
    int cursor;

    g_object_set_data(G_OBJECT(window), "undo-stack", NULL);
    g_object_set_data(G_OBJECT(window), "redo-stack", NULL);
    snapshot_stack(window, "undo-stack", TRUE);
    snapshot_stack(window, "redo-stack", TRUE);

    if (view == NULL)
        return;

    buffer = gtk_text_view_get_buffer(view);
    if (buffer == NULL)
        return;

    rtf = rtf_from_buffer(buffer);
    cursor = snapshot_cursor(window);
    snapshot_push(window, rtf != NULL ? rtf : "", cursor);
    g_free(rtf);
}

/* Libera tutte le istantanee della finestra. */
static void snapshot_clear_all(GtkWidget *window)
{
    g_object_set_data(G_OBJECT(window), "undo-stack", NULL);
    g_object_set_data(G_OBJECT(window), "redo-stack", NULL);
}

/* Ripristina una istantanea nel buffer senza registrarla di nuovo. */
static void snapshot_restore(GtkWidget *window, const UndoSnapshot *snap)
{
    GtkTextView *view = window_view(window);
    GtkTextBuffer *buffer;
    Note *note = window_note(window);

    if (snap == NULL || view == NULL)
        return;

    buffer = gtk_text_view_get_buffer(view);
    if (buffer == NULL)
        return;

    g_object_set_data(G_OBJECT(window), "loading", GINT_TO_POINTER(1));
    g_object_set_data(G_OBJECT(window), "restoring", GINT_TO_POINTER(1));

    rtf_to_buffer(snap->rtf != NULL ? snap->rtf : "", buffer);

    {
        GtkTextIter iter;
        int count = gtk_text_buffer_get_char_count(buffer);
        int pos = snap->cursor;

        if (pos < 0)
            pos = 0;
        if (pos > count)
            pos = count;
        gtk_text_buffer_get_iter_at_offset(buffer, &iter, pos);
        gtk_text_buffer_place_cursor(buffer, &iter);
    }

    if (note != NULL) {
        g_free(note->rtf);
        note->rtf = g_strdup(snap->rtf != NULL ? snap->rtf : "");
    }

    g_object_set_data(G_OBJECT(window), "loading", GINT_TO_POINTER(0));

    update_title(window);
    touch(window);
    save_now(window);

    g_object_set_data(G_OBJECT(window), "restoring", GINT_TO_POINTER(0));
}

/* Annulla l'ultima modifica. */
static void do_undo(GtkWidget *window)
{
    GPtrArray *undo = g_object_get_data(G_OBJECT(window), "undo-stack");
    GPtrArray *redo;
    UndoSnapshot *cur;
    UndoSnapshot *copy;
    UndoSnapshot *target;

    if (undo == NULL || undo->len <= 1)
        return;

    redo = snapshot_stack(window, "redo-stack", TRUE);

    cur = g_ptr_array_index(undo, undo->len - 1);
    copy = snapshot_clone(cur);
    if (copy != NULL && redo != NULL) {
        g_ptr_array_add(redo, copy);
        while (redo->len > 100)
            g_ptr_array_remove_index(redo, 0);
    } else {
        snapshot_free(copy);
    }

    g_ptr_array_remove_index(undo, undo->len - 1);

    target = g_ptr_array_index(undo, undo->len - 1);
    snapshot_restore(window, target);
}

/* Ripristina l'ultima modifica annullata. */
static void do_redo(GtkWidget *window)
{
    GPtrArray *redo = g_object_get_data(G_OBJECT(window), "redo-stack");
    GPtrArray *undo;
    UndoSnapshot *cur;
    UndoSnapshot *copy;
    UndoSnapshot *target;

    if (redo == NULL || redo->len == 0)
        return;

    undo = snapshot_stack(window, "undo-stack", TRUE);

    cur = g_ptr_array_index(redo, redo->len - 1);
    copy = snapshot_clone(cur);
    if (copy != NULL && undo != NULL) {
        g_ptr_array_add(undo, copy);
        while (undo->len > 100)
            g_ptr_array_remove_index(undo, 0);
    } else {
        snapshot_free(copy);
    }

    g_ptr_array_remove_index(redo, redo->len - 1);

    target = g_ptr_array_index(undo, undo->len - 1);
    snapshot_restore(window, target);
}

/* Rimuove il provider CSS dallo schermo/contesto. */
static void remove_css(GtkWidget *window)
{
    GtkCssProvider *provider = g_object_get_data(G_OBJECT(window), "css-provider");
    GdkScreen *screen;

    if (provider == NULL)
        return;

    screen = gtk_widget_get_screen(window);
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(window), "css-on-screen")) && screen != NULL)
        gtk_style_context_remove_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider));
    else
        gtk_style_context_remove_provider(gtk_widget_get_style_context(window),
                                          GTK_STYLE_PROVIDER(provider));

    g_object_set_data(G_OBJECT(window), "css-provider", NULL);
}

/* Applica i colori, i font e i bordi della nota. */
static void restyle(GtkWidget *window)
{
    Note *note = window_note(window);
    const char *name;
    const char *bg;
    const char *ink;
    const char *bar;
    int palette;
    int color;
    int font_size;
    char *css;
    GtkCssProvider *provider;
    GError *err = NULL;
    GdkScreen *screen;

    if (note == NULL)
        return;

    remove_css(window);

    palette = note->palette;
    color = note->color;
    if (palette < 0 || palette >= PALETTE_COUNT)
        palette = 0;
    if (color < 0 || color >= COLOR_COUNT)
        color = 0;

    bg = palette_color_hex(palette, color);
    ink = palette_ink_hex(palette, color);
    bar = palette_bar_hex(palette, color);

    font_size = 10 + 2 * note->size;
    if (font_size < 8)
        font_size = 8;
    if (font_size > 72)
        font_size = 72;

    name = gtk_widget_get_name(window);
    {
        char color_decl[64];

        /* Con inchiostro personalizzato i tag RTF gestiscono il colore testo. */
        if (note->custom_ink)
            color_decl[0] = '\0';
        else
            g_snprintf(color_decl, sizeof(color_decl), "color: %s;", ink);

        /* CSS generato da palette, colore e dimensione font */
        css = g_strdup_printf(
            "#%s { background-color: %s; color: %s; border: 1px solid %s; }\n"
            "#%s .jotilla-header { background-color: %s; border-bottom: 1px solid %s; "
            "padding: 0 2px; min-height: 30px; }\n"
            "#%s .jotilla-title { color: %s; font-weight: bold; padding: 0 4px; }\n"
            "#%s .jotilla-header button { background: transparent; background-image: none; "
            "border: none; box-shadow: none; padding: 0 6px; min-height: 22px; min-width: 22px; "
            "color: %s; }\n"
            "#%s .jotilla-header button:hover { background-color: rgba(0,0,0,0.10); "
            "border-radius: 3px; }\n"
            "#%s textview, #%s textview text { background-color: transparent; %s }\n"
            "#%s textview { font-size: %dpt; }\n"
            "#%s scrolledwindow, #%s scrolledwindow viewport { background-color: transparent; "
            "border: none; }\n"
            ".save-error { border: 2px solid #cc2222; }\n"
            "#%s.save-error { border: 2px solid #cc2222; }\n",
            name, bg, ink, bar,
            name, bg, bar,
            name, ink,
            name, ink,
            name,
            name, name, color_decl,
            name, font_size,
            name, name,
            name);
    }

    provider = gtk_css_provider_new();
    if (!gtk_css_provider_load_from_data(provider, css, -1, &err)) {
        if (err != NULL) {
            g_warning("Could not apply note styles: %s", err->message);
            g_error_free(err);
        }
    }

    screen = gtk_widget_get_screen(window);
    if (screen != NULL) {
        gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_set_data(G_OBJECT(window), "css-on-screen", GINT_TO_POINTER(1));
    } else {
        gtk_style_context_add_provider(gtk_widget_get_style_context(window),
                                       GTK_STYLE_PROVIDER(provider),
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_set_data(G_OBJECT(window), "css-on-screen", GINT_TO_POINTER(0));
    }

    g_object_set_data_full(G_OBJECT(window), "css-provider", provider, g_object_unref);
    g_free(css);
}

/* Imposta l'opacità della finestra. */
static void apply_opacity(GtkWidget *window)
{
    Note *note = window_note(window);

    if (note == NULL)
        return;

    if (note->opacity > 0 && note->opacity <= 100)
        gtk_widget_set_opacity(window, note->opacity / 100.0);
    else
        gtk_widget_set_opacity(window, 1.0);
}

static const int NOTE_SIZE_W[] = { 240, 300, 420, 540, 300 };
static const int NOTE_SIZE_H[] = { 180, 240, 340, 440, 300 };
static const char *const NOTE_SIZE_NAMES[] = {
    "Small", "Medium", "Large", "Extra large", "Square"
};
#define NOTE_SIZE_COUNT ((int)G_N_ELEMENTS(NOTE_SIZE_W))

/* Rimuove i tag di dimensione font "fs-*" dal range indicato. */
static void remove_font_size_tags(GtkTextBuffer *buffer, GtkTextIter *start,
                                  GtkTextIter *end)
{
    GSList *tags;
    GSList *l;

    tags = gtk_text_iter_get_tags(start);
    for (l = tags; l != NULL; l = l->next) {
        GtkTextTag *tag = GTK_TEXT_TAG(l->data);
        char *name = NULL;

        g_object_get(tag, "name", &name, NULL);
        if (name != NULL && g_str_has_prefix(name, "fs-"))
            gtk_text_buffer_remove_tag(buffer, tag, start, end);
        g_free(name);
    }
    g_slist_free(tags);
}

/* Dimensione font effettiva all'inizio della selezione. */
static int selection_font_size(GtkTextIter *start, Note *note)
{
    GtkTextAttributes *attrs;
    int size;

    size = note != NULL ? 10 + 2 * note->size : 10;
    attrs = gtk_text_attributes_new();
    gtk_text_iter_get_attributes(start, attrs);
    if (attrs->font != NULL) {
        int points = pango_font_description_get_size(attrs->font);

        if (points > 0)
            size = points / PANGO_SCALE;
    }
    gtk_text_attributes_unref(attrs);
    return size;
}

/* Ridimensiona il testo selezionato oppure la finestra (Ctrl +/- e rotella). */
static void change_note_size(GtkWidget *window, int direction)
{
    Note *note = window_note(window);
    GtkTextView *view = window_view(window);
    GtkTextBuffer *buffer = NULL;
    GtkTextIter start, end;

    if (note == NULL)
        return;

    if (view != NULL)
        buffer = gtk_text_view_get_buffer(view);

    if (buffer != NULL &&
        gtk_text_buffer_get_selection_bounds(buffer, &start, &end) &&
        !gtk_text_iter_equal(&start, &end)) {
        char tag_name[32];
        GtkTextTag *tag;
        int size;

        size = selection_font_size(&start, note) + direction;
        if (size < 6)
            size = 6;
        if (size > 72)
            size = 72;

        g_snprintf(tag_name, sizeof(tag_name), "fs-%d", size);
        tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer),
                                        tag_name);
        if (tag == NULL)
            tag = gtk_text_buffer_create_tag(buffer, tag_name,
                                             "size-points", (gdouble)size, NULL);

        gtk_text_buffer_begin_user_action(buffer);
        remove_font_size_tags(buffer, &start, &end);
        if (tag != NULL)
            gtk_text_buffer_apply_tag(buffer, tag, &start, &end);
        gtk_text_buffer_end_user_action(buffer);

        touch(window);
        save_now(window);
        return;
    }

    {
        int width = note->w;
        int height = note->h;
        int new_w;
        int new_h;
        double ratio;

        if (width <= 0 || height <= 0) {
            gtk_window_get_size(GTK_WINDOW(window), &width, &height);
            if (width <= 0)
                width = 280;
            if (height <= 0)
                height = 260;
        }

        ratio = (double)height / (double)width;
        if (direction > 0)
            new_w = (int)(width * 1.10 + 0.5);
        else
            new_w = (int)(width / 1.10 + 0.5);

        if (new_w < 160)
            new_w = 160;
        if (new_w > 4096)
            new_w = 4096;

        new_h = (int)(new_w * ratio + 0.5);
        if (new_h < 100)
            new_h = 100;
        if (new_h > 4096)
            new_h = 4096;

        gtk_window_resize(GTK_WINDOW(window), new_w, new_h);
        note->w = new_w;
        note->h = new_h;
        touch(window);
        save_now(window);
    }
}

/* Applica una dimensione predefinita alla finestra. */
static void set_note_size(GtkWidget *window, int index)
{
    Note *note = window_note(window);

    if (note == NULL || index < 0 || index >= NOTE_SIZE_COUNT)
        return;

    note->w = NOTE_SIZE_W[index];
    note->h = NOTE_SIZE_H[index];
    note->size = index;
    gtk_window_resize(GTK_WINDOW(window), note->w,
                      note->collapsed ? 32 : note->h);
    restyle(window);
    touch(window);
    save_now(window);
}

/* Aggiorna il tooltip dell'header con la data della nota. */
static void update_date_tooltip(GtkWidget *window)
{
    Note *note = window_note(window);
    GtkWidget *header = g_object_get_data(G_OBJECT(window), "header");

    if (header == NULL)
        return;

    if (note != NULL && note->date != 0) {
        char *date = date_format(note->date);
        char *tip = g_strdup_printf("Date: %s", date != NULL ? date : "");
        gtk_widget_set_tooltip_text(header, tip);
        g_free(tip);
        g_free(date);
    } else {
        gtk_widget_set_tooltip_text(header, "No date");
    }
}

/* Applica una palette scelta dal picker. */
static void on_palette_picked(int palette, int color, gpointer data)
{
    GtkWidget *window = data;
    Note *note = window_note(window);

    if (note == NULL || palette < 0 || palette >= PALETTE_COUNT ||
        color < 0 || color >= COLOR_COUNT)
        return;

    note->palette = palette;
    note->color = color;
    restyle(window);
    touch(window);
    save_now(window);
}

/* Applica la palette preferita scelta dal picker. */
static void on_palette_favorite(int palette, gpointer data)
{
    GtkWidget *window = data;
    Note *note = window_note(window);

    if (note == NULL || palette < 0 || palette >= PALETTE_COUNT)
        return;

    note->palette = palette;
    if (note->color < 0 || note->color >= COLOR_COUNT)
        note->color = 0;
    restyle(window);
    touch(window);
    save_now(window);
}

/* Evita puntatori pendenti quando il popover viene distrutto. */
static void on_palette_popover_destroy(GtkWidget *popover, gpointer data)
{
    GtkWidget *window = data;

    if (g_object_get_data(G_OBJECT(window), "palette-popover") == popover)
        g_object_set_data(G_OBJECT(window), "palette-popover", NULL);
}

/* Applica al testo selezionato il colore scelto dal picker. */
static void on_text_color_picked(int palette, int color, gpointer data)
{
    GtkWidget *window = data;
    const char *hex;

    if (palette < 0 || palette >= PALETTE_COUNT ||
        color < 0 || color >= COLOR_COUNT)
        return;

    hex = palette_color_hex(palette, color);
    if (hex == NULL || hex[0] == '\0')
        return;

    apply_foreground(window, hex);
}

/* Apre il picker palette; event puo' essere NULL per il menu. */
static void show_palette_picker_mode(GtkWidget *window, GdkEventButton *event,
                                     gboolean text_mode)
{
    Note *note = window_note(window);
    Store *store = window_store(window);
    GtkWidget *old = g_object_get_data(G_OBJECT(window), "palette-popover");
    GtkWidget *popover;

    if (note == NULL)
        return;

    if (old != NULL)
        gtk_widget_destroy(old);

    popover = palette_picker_new(GTK_WINDOW(window),
                                 store != NULL ? store->config_dir : NULL,
                                 note->palette, note->color, text_mode,
                                 text_mode ? on_text_color_picked : on_palette_picked,
                                 text_mode ? NULL : on_palette_favorite, window);
    if (popover == NULL)
        return;

    g_object_set_data(G_OBJECT(window), "palette-popover", popover);
    g_signal_connect(popover, "destroy",
                     G_CALLBACK(on_palette_popover_destroy), window);

    if (event != NULL) {
        GtkWidget *view = GTK_WIDGET(window_view(window));
        int x = 0, y = 0;

        if (view != NULL &&
            gtk_widget_translate_coordinates(view, window,
                                             (int)event->x, (int)event->y,
                                             &x, &y)) {
            GdkRectangle rect;
            rect.x = x;
            rect.y = y;
            rect.width = 1;
            rect.height = 1;
            gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
        }
    }

    gtk_popover_popup(GTK_POPOVER(popover));
}

/* L'RTF e' valido solo se il suo testo coincide con il TXT autorevole. */
static gboolean rtf_matches_text(const char *rtf, const char *text)
{
    char *plain = rtf_plain_text(rtf);
    char *read;
    char *write;
    gboolean same;

    if (plain == NULL)
        return FALSE;
    read = write = plain;
    while (*read) {
        if (*read == '\r') {
            *write++ = '\n';
            ++read;
            if (*read == '\n')
                ++read;
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
    same = g_strcmp0(plain, text ? text : "") == 0;
    g_free(plain);
    return same;
}

/* Carica il testo/RTF della nota nella view. */
static void load_note(GtkWidget *window)
{
    Note *note = window_note(window);
    GtkTextView *view = window_view(window);
    GtkTextBuffer *buffer;

    if (note == NULL || view == NULL)
        return;

    buffer = gtk_text_view_get_buffer(view);
    if (buffer == NULL)
        return;

    ensure_tags(buffer);

    if (note->rtf != NULL && note->rtf[0] != '\0' &&
        !rtf_matches_text(note->rtf, note->text)) {
        g_free(note->rtf);
        note->rtf = NULL;
    }

    g_object_set_data(G_OBJECT(window), "loading", GINT_TO_POINTER(1));
    if (note->rtf != NULL && note->rtf[0] != '\0')
        rtf_to_buffer(note->rtf, buffer);
    else
        gtk_text_buffer_set_text(buffer, note->text != NULL ? note->text : "", -1);
    g_object_set_data(G_OBJECT(window), "loading", GINT_TO_POINTER(0));

    snapshot_reset(window);
    update_title(window);
}

/* Invoca il callback di nuova nota o crea una nota. */
static void invoke_new(GtkWidget *window)
{
    SimpleHook *hook = g_object_get_data(G_OBJECT(window), "new-hook");

    if (hook != NULL && hook->fn != NULL) {
        hook->fn(hook->data);
        return;
    }

    {
        Store *store = window_store(window);
        Note *note;

        if (store == NULL)
            return;

        note = store_new(store, "");
        if (note != NULL) {
            GtkWidget *other = note_window_new(store, note);
            gtk_widget_show_all(other);
        }
    }
}

/* Invoca il callback di archivio. */
static void invoke_archive(GtkWidget *window)
{
    SimpleHook *hook = g_object_get_data(G_OBJECT(window), "archive-hook");

    if (hook != NULL && hook->fn != NULL)
        hook->fn(hook->data);
}

/* Trova la finestra archivio tra i toplevel esistenti. */
static GtkWidget *find_archive_window(void)
{
    GList *windows = gtk_window_list_toplevels();
    GList *iter;
    GtkWidget *archive = NULL;

    for (iter = windows; iter != NULL; iter = iter->next) {
        GtkWidget *candidate = GTK_WIDGET(iter->data);
        if (g_object_get_data(G_OBJECT(candidate), "archive-window-state") != NULL) {
            archive = candidate;
            break;
        }
    }
    g_list_free(windows);
    return archive;
}

/* Apre l'archivio e porta il focus sulla ricerca. */
static void invoke_archive_search(GtkWidget *window)
{
    GtkWidget *archive;

    invoke_archive(window);
    archive = find_archive_window();
    if (archive != NULL && archive_window_focus_search != NULL)
        archive_window_focus_search(archive);
}

/* Applica/rimuove un tag di formato sulla selezione. */
static void apply_named_tag(GtkWidget *window, const char *name)
{
    GtkTextView *view = window_view(window);
    GtkTextBuffer *buffer;
    GtkTextIter start, end;
    GtkTextTag *tag;

    if (view == NULL)
        return;

    buffer = gtk_text_view_get_buffer(view);
    if (buffer == NULL)
        return;

    ensure_tags(buffer);

    if (!gtk_text_buffer_get_selection_bounds(buffer, &start, &end))
        return;

    tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), name);
    if (tag == NULL)
        return;

    if (gtk_text_iter_has_tag(&start, tag))
        gtk_text_buffer_remove_tag(buffer, tag, &start, &end);
    else
        gtk_text_buffer_apply_tag(buffer, tag, &start, &end);

    touch(window);
}

/* Applica un colore di testo alla selezione. */
static void apply_foreground(GtkWidget *window, const char *hex)
{
    GtkTextView *view = window_view(window);
    GtkTextBuffer *buffer;
    GtkTextIter start, end;
    GtkTextTagTable *table;
    GtkTextTag *tag;
    const char *digits;
    char name[32];

    if (view == NULL)
        return;

    buffer = gtk_text_view_get_buffer(view);
    if (buffer == NULL)
        return;

    if (!gtk_text_buffer_get_selection_bounds(buffer, &start, &end))
        return;

    digits = hex[0] == '#' ? hex + 1 : hex;
    g_snprintf(name, sizeof(name), "fg-%s", digits);

    table = gtk_text_buffer_get_tag_table(buffer);
    tag = gtk_text_tag_table_lookup(table, name);
    if (tag == NULL)
        tag = gtk_text_buffer_create_tag(buffer, name, "foreground", hex, NULL);
    if (tag == NULL)
        return;

    gtk_text_buffer_apply_tag(buffer, tag, &start, &end);

    {
        Note *note = window_note(window);
        if (note != NULL)
            note->custom_ink = TRUE;
    }

    touch(window);
    save_now(window);
}

/* Alterna il marcatore todo del paragrafo corrente. */
void toggle_todo_marker(GtkWidget *window)
{
    GtkTextView *view = window_view(window);
    GtkTextBuffer *buffer;
    GtkTextMark *mark;
    GtkTextIter cursor, para_start, para_end, mstart, mend, s, t;
    gint cursor_offset;
    gunichar marker;
    GtkTextTag *tag;

    if (view == NULL)
        return;

    buffer = gtk_text_view_get_buffer(view);
    if (buffer == NULL)
        return;

    ensure_tags(buffer);

    mark = gtk_text_buffer_get_insert(buffer);
    gtk_text_buffer_get_iter_at_mark(buffer, &cursor, mark);
    cursor_offset = gtk_text_iter_get_offset(&cursor);

    para_start = cursor;
    gtk_text_iter_set_line_offset(&para_start, 0);
    para_end = para_start;
    gtk_text_iter_forward_to_line_end(&para_end);

    marker = gtk_text_iter_get_char(&para_start);
    t = para_start;
    gtk_text_iter_forward_char(&t);
    if (!((marker == 0x2610 || marker == 0x2611) && gtk_text_iter_get_char(&t) == ' '))
        return;

    gtk_text_buffer_begin_user_action(buffer);

    mstart = para_start;
    mend = para_start;
    gtk_text_iter_forward_chars(&mend, 2);
    gtk_text_buffer_delete(buffer, &mstart, &mend);
    if (marker == 0x2610)
        gtk_text_buffer_insert(buffer, &mstart, "\xE2\x98\x91 ", -1);
    else
        gtk_text_buffer_insert(buffer, &mstart, "\xE2\x98\x90 ", -1);

    gtk_text_buffer_get_iter_at_offset(buffer, &cursor, cursor_offset);
    para_start = cursor;
    gtk_text_iter_set_line_offset(&para_start, 0);
    para_end = para_start;
    gtk_text_iter_forward_to_line_end(&para_end);

    s = para_start;
    gtk_text_iter_forward_chars(&s, 2);
    tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), "strike");
    if (tag != NULL) {
        if (marker == 0x2610)
            gtk_text_buffer_apply_tag(buffer, tag, &s, &para_end);
        else
            gtk_text_buffer_remove_tag(buffer, tag, &s, &para_end);
    }

    gtk_text_buffer_end_user_action(buffer);

    gtk_text_buffer_get_iter_at_offset(buffer, &cursor, cursor_offset);
    gtk_text_buffer_place_cursor(buffer, &cursor);

    touch(window);
    save_now(window);
}

/* Rimuove il marcatore todo se il cursore e' subito dopo di esso. */
gboolean remove_todo_marker(GtkWidget *window)
{
    GtkTextView *view = window_view(window);
    GtkTextBuffer *buffer;
    GtkTextIter cursor, para_start, para_end, mstart, mend, t;
    GtkTextIter sel_start, sel_end;
    gint cursor_offset;
    gunichar marker;

    if (view == NULL)
        return FALSE;

    buffer = gtk_text_view_get_buffer(view);
    if (buffer == NULL)
        return FALSE;

    ensure_tags(buffer);

    gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));
    cursor_offset = gtk_text_iter_get_offset(&cursor);

    if (gtk_text_buffer_get_selection_bounds(buffer, &sel_start, &sel_end))
        return FALSE;

    para_start = cursor;
    gtk_text_iter_set_line_offset(&para_start, 0);
    para_end = para_start;
    gtk_text_iter_forward_to_line_end(&para_end);

    if (cursor_offset != gtk_text_iter_get_offset(&para_start) + 2)
        return FALSE;

    marker = gtk_text_iter_get_char(&para_start);
    t = para_start;
    gtk_text_iter_forward_char(&t);
    if (!((marker == 0x2610 || marker == 0x2611) && gtk_text_iter_get_char(&t) == ' '))
        return FALSE;

    gtk_text_buffer_begin_user_action(buffer);

    mstart = para_start;
    mend = para_start;
    gtk_text_iter_forward_chars(&mend, 2);
    gtk_text_buffer_delete(buffer, &mstart, &mend);

    {
        GtkTextIter c;
        GtkTextTag *tag;
        gtk_text_buffer_get_iter_at_offset(buffer, &c, cursor_offset - 2);
        para_start = c;
        gtk_text_iter_set_line_offset(&para_start, 0);
        para_end = para_start;
        gtk_text_iter_forward_to_line_end(&para_end);
        tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), "strike");
        if (tag != NULL)
            gtk_text_buffer_remove_tag(buffer, tag, &para_start, &para_end);
    }

    gtk_text_buffer_end_user_action(buffer);

    {
        GtkTextIter c;
        gtk_text_buffer_get_iter_at_offset(buffer, &c, cursor_offset - 2);
        gtk_text_buffer_place_cursor(buffer, &c);
    }

    touch(window);
    save_now(window);
    return TRUE;
}

/* Click sinistro sul marcatore todo; click destro apre il picker palette. */
static gboolean on_view_button_press(GtkWidget *widget, GdkEventButton *event,
                                     gpointer data)
{
    GtkWidget *window = data;
    GtkTextView *view = GTK_TEXT_VIEW(widget);

    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
        GtkTextIter start, end;
        gboolean has_selection = FALSE;

        if (buffer != NULL &&
            gtk_text_buffer_get_selection_bounds(buffer, &start, &end) &&
            !gtk_text_iter_equal(&start, &end))
            has_selection = TRUE;

        show_palette_picker_mode(window, event, has_selection);
        return TRUE;
    }

    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
        GtkTextIter iter, start, after;
        gunichar marker;

        if (buffer == NULL)
            return FALSE;
        if (!gtk_text_view_get_iter_at_location(view, &iter,
                                                (int)event->x, (int)event->y))
            return FALSE;

        start = iter;
        gtk_text_iter_set_line_offset(&start, 0);
        if (gtk_text_iter_get_offset(&iter) != gtk_text_iter_get_offset(&start))
            return FALSE;

        marker = gtk_text_iter_get_char(&start);
        after = start;
        gtk_text_iter_forward_char(&after);
        if (!((marker == 0x2610 || marker == 0x2611) &&
              gtk_text_iter_get_char(&after) == ' '))
            return FALSE;

        gtk_text_buffer_place_cursor(buffer, &iter);
        toggle_todo_marker(window);
        return TRUE;
    }

    return FALSE;
}

/* Gestisce Enter su paragrafo todo: nuova riga con marcatore. */
static gboolean todo_enter_linux(GtkWidget *window)
{
    GtkTextView *view = window_view(window);
    GtkTextBuffer *buffer;
    GtkTextIter cursor, para_start, para_end, ins;
    GtkTextIter sel_start, sel_end;
    gint cursor_offset, para_start_offset, para_len;
    gunichar marker;
    GtkTextTag *tag;

    if (view == NULL)
        return FALSE;

    buffer = gtk_text_view_get_buffer(view);
    if (buffer == NULL)
        return FALSE;

    ensure_tags(buffer);

    gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));
    cursor_offset = gtk_text_iter_get_offset(&cursor);

    if (gtk_text_buffer_get_selection_bounds(buffer, &sel_start, &sel_end))
        return FALSE;

    para_start = cursor;
    gtk_text_iter_set_line_offset(&para_start, 0);
    para_end = para_start;
    gtk_text_iter_forward_to_line_end(&para_end);

    marker = gtk_text_iter_get_char(&para_start);
    {
        GtkTextIter t = para_start;
        gtk_text_iter_forward_char(&t);
        if (!((marker == 0x2610 || marker == 0x2611) && gtk_text_iter_get_char(&t) == ' '))
            return FALSE;
    }

    para_start_offset = gtk_text_iter_get_offset(&para_start);
    if (cursor_offset < para_start_offset + 2)
        return FALSE;

    para_len = gtk_text_iter_get_offset(&para_end) - para_start_offset;

    gtk_text_buffer_begin_user_action(buffer);

    if (para_len <= 2) {
        GtkTextIter ms = para_start;
        GtkTextIter me = para_start;
        gtk_text_iter_forward_chars(&me, 2);
        gtk_text_buffer_delete(buffer, &ms, &me);
        ins = ms;
    } else {
        ins = cursor;
        gtk_text_buffer_insert(buffer, &ins, "\n\xE2\x98\x90 ", -1);
    }

    {
        GtkTextIter c = ins;
        GtkTextIter ps, pe;
        ps = c;
        gtk_text_iter_set_line_offset(&ps, 0);
        pe = ps;
        gtk_text_iter_forward_to_line_end(&pe);
        tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), "strike");
        if (tag != NULL)
            gtk_text_buffer_remove_tag(buffer, tag, &ps, &pe);
    }

    gtk_text_buffer_place_cursor(buffer, &ins);
    gtk_text_buffer_end_user_action(buffer);

    touch(window);
    save_now(window);
    return TRUE;
}

/* Dialogo per modificare i tag della nota. */
static void on_tags_activate(GtkMenuItem *item, gpointer data)
{
    GtkWidget *window = data;
    Store *store = window_store(window);
    Note *note = window_note(window);
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *entry;

    (void)item;

    if (store == NULL || note == NULL)
        return;

    dialog = gtk_dialog_new_with_buttons("Tags", GTK_WINDOW(window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Apply", GTK_RESPONSE_ACCEPT,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 8);

    entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), note->tags != NULL ? note->tags : "");
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "tag1, tag2");
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(content), entry, TRUE, TRUE, 0);

    {
        char *catalog = store_tags_catalog(store);
        GtkEntryCompletion *completion = gtk_entry_completion_new();
        GtkListStore *model = gtk_list_store_new(1, G_TYPE_STRING);

        if (catalog != NULL) {
            char **lines = g_strsplit(catalog, "\n", -1);
            guint i = 0;

            if (lines[0] != NULL &&
                g_strcmp0(g_strstrip(lines[0]), "All tags") == 0)
                i = 1;

            for (; lines[i] != NULL; ++i) {
                char *name = g_strstrip(lines[i]);
                GtkTreeIter iter;

                if (name[0] == '\0')
                    continue;
                gtk_list_store_append(model, &iter);
                gtk_list_store_set(model, &iter, 0, name, -1);
            }
            g_strfreev(lines);
        }

        gtk_entry_completion_set_model(completion, GTK_TREE_MODEL(model));
        gtk_entry_completion_set_text_column(completion, 0);
        gtk_entry_set_completion(GTK_ENTRY(entry), completion);
        g_object_unref(model);
        g_object_unref(completion);
        g_free(catalog);
    }

    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        store_set_tags(store, note, gtk_entry_get_text(GTK_ENTRY(entry)));
        save_now(window);
        update_title(window);
    }
    gtk_widget_destroy(dialog);
}

/* Salva la data scelta per la nota. */
static void on_calendar_picked(guint32 date, gpointer data)
{
    GtkWidget *window = data;
    Store *store = window_store(window);
    Note *note = window_note(window);

    if (store == NULL || note == NULL)
        return;

    store_set_date(store, note, date);
    update_date_tooltip(window);
    save_now(window);
}

/* Apre il selettore della data. */
static void on_calendar_activate(GtkMenuItem *item, gpointer data)
{
    GtkWidget *window = data;
    Note *note = window_note(window);
    GtkWidget *picker;

    (void)item;

    if (note == NULL)
        return;

    picker = calendar_picker_new(GTK_WINDOW(window), note->date, on_calendar_picked, window);
    if (picker != NULL)
        gtk_widget_show_all(picker);
}


/* Dialogo per modificare il titolo della nota. */
static void on_edit_title_activate(GtkMenuItem *item, gpointer data)
{
    GtkWidget *window = data;
    Note *note = window_note(window);
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *entry;

    (void)item;

    if (note == NULL)
        return;

    dialog = gtk_dialog_new_with_buttons("Edit title", GTK_WINDOW(window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "Cancel", GTK_RESPONSE_CANCEL,
                                         "OK", GTK_RESPONSE_OK, NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), note->title ? note->title : "");
    gtk_entry_set_max_length(GTK_ENTRY(entry), 200);
    gtk_box_pack_start(GTK_BOX(content), entry, TRUE, TRUE, 8);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
        char *title = NULL;
        size_t start = 0, finish = strlen(text);
        while (start < finish && (text[start] == ' ' || text[start] == '\t')) ++start;
        while (finish > start && (text[finish - 1] == ' ' || text[finish - 1] == '\t')) --finish;
        if (finish > start)
            title = g_strndup(text + start, finish - start);

        g_free(note->title);
        note->title = title;
        note->has_title = (title != NULL);
        update_title(window);
        touch(window);
        save_now(window);
    }

    gtk_widget_destroy(dialog);
}

/* Vero se la stringa e' vuota o composta solo da spazi. */
static gboolean text_is_blank(const char *text)
{
    const char *p = text;

    if (p == NULL)
        return TRUE;

    while (*p != '\0') {
        if (!g_unichar_isspace(g_utf8_get_char(p)))
            return FALSE;
        p = g_utf8_next_char(p);
    }
    return TRUE;
}

/* Vero se la nota non ha titolo, corpo, tag o data. */
static gboolean note_is_empty(GtkWidget *window)
{
    Note *note = window_note(window);
    GtkTextView *view = window_view(window);
    gboolean empty = TRUE;

    if (note == NULL)
        return FALSE;

    if (note->has_title && !text_is_blank(note->title))
        return FALSE;
    if (note->date != 0)
        return FALSE;
    if (!text_is_blank(note->tags))
        return FALSE;

    if (view != NULL) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
        if (buffer != NULL) {
            GtkTextIter start, end;
            char *text;

            gtk_text_buffer_get_bounds(buffer, &start, &end);
            text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
            empty = text_is_blank(text);
            g_free(text);
        }
    } else {
        empty = text_is_blank(note->text);
    }

    return empty;
}

/* Elimina una nota vuota; ritorna TRUE se eliminata. */
static gboolean delete_empty_note(GtkWidget *window)
{
    Store *store = window_store(window);
    Note *note = window_note(window);

    if (store == NULL || note == NULL || !note_is_empty(window))
        return FALSE;

    remove_timeout(window);
    g_object_set_data(G_OBJECT(window), "deleted", GINT_TO_POINTER(1));
    if (!store_delete(store, note->id, NULL)) {
        g_object_set_data(G_OBJECT(window), "deleted", GINT_TO_POINTER(0));
        return FALSE;
    }
    return TRUE;
}

/* Cambia la dimensione predefinita della nota. */
static void on_note_size_activate(GtkMenuItem *item, gpointer data)
{
    GtkWidget *window = data;
    int size = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "size"));

    set_note_size(window, size);
}

/* Imposta la palette usata per le nuove note. */
static void on_new_palette_activate(GtkMenuItem *item, gpointer data)
{
    GtkWidget *window = data;
    Note *note = window_note(window);
    int palette = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "palette"));

    if (note == NULL || palette < 0 || palette >= PALETTE_COUNT)
        return;

    note->palette = palette;
    note->color = 0;
    restyle(window);
    touch(window);
    save_now(window);
}

/* Cambia l'opacità della nota. */
static void on_opacity_activate(GtkMenuItem *item, gpointer data)
{
    GtkWidget *window = data;
    Note *note = window_note(window);
    int percent = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "opacity"));

    if (percent < 10)
        percent = 10;
    if (percent > 100)
        percent = 100;

    gtk_widget_set_opacity(window, percent / 100.0);
    if (note != NULL) {
        note->opacity = percent;
        touch(window);
    }
}

/* Gestore menu: apri la cartella archivio nel file manager. */
static void on_open_archive_folder(GtkMenuItem *item, gpointer data)
{
    GtkWidget *window = data;
    Store *store = window_store(window);
    char *uri;

    (void)item;

    if (store == NULL || store->archive_dir == NULL)
        return;

    uri = g_filename_to_uri(store->archive_dir, NULL, NULL);
    if (uri == NULL)
        return;

    {
        GError *err = NULL;
        if (!g_app_info_launch_default_for_uri(uri, NULL, &err)) {
            if (err != NULL) {
                g_warning("Could not open archive folder: %s", err->message);
                g_error_free(err);
            }
        }
    }

    g_free(uri);
}

/* Gestore menu: apri archivio. */
static void on_browse_notes(GtkMenuItem *item, gpointer data)
{
    (void)item;
    invoke_archive(data);
}

/* Crea una voce di menu con tooltip. */
static GtkWidget *tip_item(const char *label, const char *tip)
{
    GtkWidget *item = gtk_menu_item_new_with_label(label);

    gtk_widget_set_tooltip_text(item, tip);
    return item;
}

/* Costruisce il menu contestuale della nota. */
static GtkWidget *build_menu(GtkWidget *window)
{
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *item;
    GtkWidget *submenu;
    Note *note = window_note(window);

    item = tip_item("Open notes archive", "Open the archive folder in the file manager");
    g_signal_connect(item, "activate", G_CALLBACK(on_open_archive_folder), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = tip_item("Browse notes", "Open the notes archive (Ctrl+E)");
    g_signal_connect(item, "activate", G_CALLBACK(on_browse_notes), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    item = tip_item("Opacity", "Change the note opacity");
    submenu = gtk_menu_new();
    {
        static const int levels[] = { 100, 90, 80, 70, 60 };
        GSList *group = NULL;
        guint i;
        for (i = 0; i < G_N_ELEMENTS(levels); i++) {
            char label[16];
            GtkWidget *opacity_item;

            g_snprintf(label, sizeof(label), "%d%%", levels[i]);
            opacity_item = gtk_radio_menu_item_new_with_label(group, label);
            group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(opacity_item));
            if (note != NULL && note->opacity == levels[i])
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(opacity_item), TRUE);
            gtk_widget_set_tooltip_text(opacity_item, "Set the note opacity");
            g_object_set_data(G_OBJECT(opacity_item), "opacity", GINT_TO_POINTER(levels[i]));
            g_signal_connect(opacity_item, "activate", G_CALLBACK(on_opacity_activate), window);
            gtk_menu_shell_append(GTK_MENU_SHELL(submenu), opacity_item);
        }
    }
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = tip_item("Edit title", "Edit the note title");
    g_signal_connect(item, "activate", G_CALLBACK(on_edit_title_activate), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = tip_item("Note size", "Resize the note window and text");
    submenu = gtk_menu_new();
    {
        GSList *group = NULL;
        int i;
        for (i = 0; i < NOTE_SIZE_COUNT; i++) {
            GtkWidget *size_item =
                gtk_radio_menu_item_new_with_label(group, NOTE_SIZE_NAMES[i]);
            group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(size_item));
            if (note != NULL && note->size == i)
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(size_item), TRUE);
            gtk_widget_set_tooltip_text(size_item, "Apply this note size");
            g_object_set_data(G_OBJECT(size_item), "size", GINT_TO_POINTER(i));
            g_signal_connect(size_item, "activate", G_CALLBACK(on_note_size_activate), window);
            gtk_menu_shell_append(GTK_MENU_SHELL(submenu), size_item);
        }
    }
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    item = tip_item("New note palette", "Choose the palette for new notes");
    submenu = gtk_menu_new();
    {
        int i;
        for (i = 0; i < 3 && i < PALETTE_COUNT; i++) {
            GtkWidget *palette_item = tip_item(palette_name(i),
                                               "Use this palette, colour 0");
            g_object_set_data(G_OBJECT(palette_item), "palette", GINT_TO_POINTER(i));
            g_signal_connect(palette_item, "activate",
                             G_CALLBACK(on_new_palette_activate), window);
            gtk_menu_shell_append(GTK_MENU_SHELL(submenu), palette_item);
        }
    }
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    item = tip_item("Tags", "Edit the note tags (Ctrl+T)");
    g_signal_connect(item, "activate", G_CALLBACK(on_tags_activate), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_widget_show_all(menu);
    return menu;
}

/* Aggiorna titolo e segna modifiche al buffer. */
static void on_buffer_changed(GtkTextBuffer *buffer, gpointer data)
{
    GtkWidget *window = data;

    (void)buffer;

    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(window), "loading")))
        return;

    update_title(window);
    touch(window);
}

/* Salva dimensioni/posizione dopo ridimensionamento. */
static gboolean on_configure(GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
    GtkWidget *window = widget;
    Note *note = window_note(window);

    (void)data;

    if (note == NULL)
        return FALSE;

    /* Su Wayland non salvare le coordinate */
    if (!platform_is_wayland()) {
        note->x = event->x;
        note->y = event->y;
    }
    if (event->width > 0)
        note->w = event->width;
    if (event->height > 0 && !note->collapsed)
        note->h = event->height;

    g_object_set_data(G_OBJECT(window), "dirty", GINT_TO_POINTER(1));
    schedule_save(window);
    return FALSE;
}

/* Salva (o elimina se vuota) prima della chiusura della finestra. */
static gboolean on_delete(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    GtkWidget *window = widget;

    (void)event;
    (void)data;

    if (!GPOINTER_TO_INT(g_object_get_data(G_OBJECT(window), "deleted"))) {
        if (!delete_empty_note(window))
            save_now(window);
    }
    return FALSE;
}

/* Pulisce timer/CSS e salva o elimina la nota se necessario. */
static void on_destroy(GtkWidget *widget, gpointer data)
{
    GtkWidget *window = widget;
    GtkWidget *popover;

    (void)data;

    remove_timeout(window);

    if (!GPOINTER_TO_INT(g_object_get_data(G_OBJECT(window), "deleted"))) {
        if (delete_empty_note(window)) {
            /* Nota vuota rimossa: niente salvataggio. */
        } else if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(window), "dirty"))) {
            save_now(window);
        }
    }

    /* Rimuove l'eventuale retry programmato da un salvataggio fallito. */
    remove_timeout(window);

    snapshot_clear_all(window);

    popover = g_object_get_data(G_OBJECT(window), "palette-popover");
    if (popover != NULL) {
        g_object_set_data(G_OBJECT(window), "palette-popover", NULL);
        gtk_widget_destroy(popover);
    }

    remove_css(window);

    g_object_set_data(G_OBJECT(window), "store", NULL);
    g_object_set_data(G_OBJECT(window), "note", NULL);
    g_object_set_data(G_OBJECT(window), "textview", NULL);
    g_object_set_data(G_OBJECT(window), "title-label", NULL);
    g_object_set_data(G_OBJECT(window), "header", NULL);
}

/* Segna la view come non più valida. */
static void on_view_destroy(GtkWidget *view, gpointer data)
{
    GtkWidget *window = data;

    (void)view;

    g_object_set_data(G_OBJECT(window), "view-alive", GINT_TO_POINTER(0));
    g_object_set_data(G_OBJECT(window), "textview", NULL);
}

/* Avvia il trascinamento della finestra dall'header. */
static gboolean on_header_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    GtkWidget *window = data;

    (void)widget;

    if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
        on_edit_title_activate(NULL, window);
        return TRUE;
    }

    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        GdkWindow *gdk_window = gtk_widget_get_window(window);
        if (gdk_window != NULL) {
            if (event->device != NULL) {
                gdk_window_begin_move_drag_for_device(gdk_window, event->device,
                                                      event->button,
                                                      (gint)event->x_root,
                                                      (gint)event->y_root,
                                                      event->time);
            } else {
                gdk_window_begin_move_drag(gdk_window, event->button,
                                           (gint)event->x_root, (gint)event->y_root,
                                           event->time);
            }
            return TRUE;
        }
    }
    return FALSE;
}

/* Gestisce le scorciatoie da tastiera. */
static gboolean promote_title(GtkWidget *window)
{
    Note *note = window_note(window);
    GtkTextView *view = window_view(window);
    GtkTextBuffer *buffer;
    GtkTextIter start, end, line_end;
    char *text;
    size_t line_len;
    char *title;
    size_t tstart, tfinish;
    GtkTextIter insert_iter;

    if (note == NULL || view == NULL)
        return FALSE;

    buffer = gtk_text_view_get_buffer(view);
    if (buffer == NULL)
        return FALSE;

    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    if (text == NULL || text[0] != '#')
    {
        g_free(text);
        return FALSE;
    }

    /* selezione deve essere vuota e il cursore a fine della prima riga */
    {
        GtkTextIter isel, esel;
        if (gtk_text_buffer_get_selection_bounds(buffer, &isel, &esel))
        {
            g_free(text);
            return FALSE;
        }
    }

    line_len = strcspn(text, "\n");
    /* rimuovi \r finale */
    if (line_len > 0 && text[line_len - 1] == '\r')
        --line_len;
    /* posizione cursore deve essere a fine riga */
    gtk_text_buffer_get_iter_at_mark(buffer, &insert_iter, gtk_text_buffer_get_insert(buffer));
    if (gtk_text_iter_get_offset(&insert_iter) != (gint)line_len)
    {
        g_free(text);
        return FALSE;
    }

    /* copia titolo senza # e con trim */
    tstart = 1;
    tfinish = line_len;
    while (tstart < tfinish && (text[tstart] == ' ' || text[tstart] == '\t')) ++tstart;
    while (tfinish > tstart && (text[tfinish - 1] == ' ' || text[tfinish - 1] == '\t')) --tfinish;
    if (tfinish <= tstart)
    {
        g_free(text);
        return FALSE;
    }
    title = g_strndup(text + tstart, tfinish - tstart);

    /* rimuovi la prima riga (incluso newline) dal buffer */
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_iter_forward_to_line_end(&start);
    gtk_text_iter_forward_char(&start); /* newline */
    /* se c'e' \r\n, avanza anche \r */
    /* (gia' gestito: forward_char avanza il \n; il \r e' prima del \n e resta? gestiamo sotto) */

    gtk_text_buffer_begin_user_action(buffer);
    /* cancella dallo start alla fine della prima riga inclusa newline */
    gtk_text_buffer_get_start_iter(buffer, &start);
    line_end = start;
    gtk_text_iter_forward_to_line_end(&line_end);
    if (!gtk_text_iter_is_end(&line_end))
        gtk_text_iter_forward_char(&line_end); /* \n */
    /* gestisci \r\n: se il char prima del \n e' \r, includilo */
    {
        GtkTextIter prev = line_end;
        if (gtk_text_iter_backward_char(&prev) && gtk_text_iter_get_char(&prev) == '\r')
            line_end = prev;
    }
    gtk_text_buffer_delete(buffer, &start, &line_end);
    gtk_text_buffer_end_user_action(buffer);

    g_free(note->title);
    note->title = title;
    note->has_title = TRUE;

    g_free(text);

    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_place_cursor(buffer, &start);

    update_title(window);
    touch(window);
    save_now(window);
    return TRUE;
}

/* Promuove un paragrafo "*" in elemento checklist (Shift+Invio). */
static gboolean promote_checklist(GtkWidget *window)
{
    GtkTextView *view = window_view(window);
    GtkTextBuffer *buffer;
    GtkTextIter cursor, para_start, para_end;
    GtkTextIter sel_start, sel_end;
    GtkTextIter start, end, ins;
    GtkTextTag *tag;
    gint offset, start_offset, end_offset;

    if (view == NULL)
        return FALSE;

    buffer = gtk_text_view_get_buffer(view);
    if (buffer == NULL)
        return FALSE;

    ensure_tags(buffer);

    if (gtk_text_buffer_get_selection_bounds(buffer, &sel_start, &sel_end))
        return FALSE;

    gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));

    para_start = cursor;
    gtk_text_iter_set_line_offset(&para_start, 0);
    para_end = para_start;
    gtk_text_iter_forward_to_line_end(&para_end);

    start_offset = gtk_text_iter_get_offset(&para_start);
    end_offset = gtk_text_iter_get_offset(&para_end);
    offset = gtk_text_iter_get_offset(&cursor);

    /* Il cursore deve essere a fine paragrafo e il paragrafo deve essere "*". */
    if (offset != end_offset || end_offset - start_offset != 1)
        return FALSE;
    if (gtk_text_iter_get_char(&para_start) != '*')
        return FALSE;

    gtk_text_buffer_begin_user_action(buffer);

    gtk_text_buffer_get_iter_at_offset(buffer, &start, start_offset);
    end = start;
    gtk_text_iter_forward_char(&end);
    gtk_text_buffer_delete(buffer, &start, &end);

    gtk_text_buffer_get_iter_at_offset(buffer, &ins, start_offset);
    gtk_text_buffer_insert(buffer, &ins, "\xE2\x98\x90 ", -1);

    {
        GtkTextIter ps = ins;
        GtkTextIter pe = ins;
        gtk_text_iter_set_line_offset(&ps, 0);
        gtk_text_iter_forward_to_line_end(&pe);
        tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), "strike");
        if (tag != NULL)
            gtk_text_buffer_remove_tag(buffer, tag, &ps, &pe);
    }

    gtk_text_buffer_place_cursor(buffer, &ins);
    gtk_text_buffer_end_user_action(buffer);

    touch(window);
    save_now(window);
    return TRUE;
}

/* Ctrl+rotella cambia la dimensione del testo. */
static gboolean on_view_scroll(GtkWidget *widget, GdkEventScroll *event,
                               gpointer data)
{
    GtkWidget *window = data;

    (void)widget;

    if ((event->state & GDK_CONTROL_MASK) == 0)
        return FALSE;

    if (event->direction == GDK_SCROLL_UP) {
        change_note_size(window, 1);
        return TRUE;
    }
    if (event->direction == GDK_SCROLL_DOWN) {
        change_note_size(window, -1);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    GtkWidget *window = widget;
    guint key = event->keyval;
    gboolean ctrl = (event->state & GDK_CONTROL_MASK) != 0;
    gboolean shift = (event->state & GDK_SHIFT_MASK) != 0;

    (void)data;

    if (key == GDK_KEY_Menu || (shift && key == GDK_KEY_F10)) {
        GtkWidget *menu = g_object_get_data(G_OBJECT(window), "menu");

        if (menu != NULL) {
            gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
            return TRUE;
        }
    }

    if (!ctrl && !shift && key == GDK_KEY_Escape) {
        save_now(window);
        gtk_widget_destroy(window);
        return TRUE;
    }

    if (!ctrl && shift && key == GDK_KEY_Return) {
        if (promote_checklist(window))
            return TRUE;
        if (promote_title(window))
            return TRUE;
    }

    if (!ctrl && !shift && key == GDK_KEY_BackSpace) {
        if (remove_todo_marker(window))
            return TRUE;
    }

    if (!ctrl && !shift && key == GDK_KEY_Return) {
        if (todo_enter_linux(window))
            return TRUE;
    }

    if (ctrl) {
        switch (key) {
        case GDK_KEY_plus:
        case GDK_KEY_equal:
        case GDK_KEY_KP_Add:
            change_note_size(window, 1);
            return TRUE;
        case GDK_KEY_minus:
        case GDK_KEY_KP_Subtract:
            change_note_size(window, -1);
            return TRUE;
        case GDK_KEY_z:
        case GDK_KEY_Z:
            if (shift)
                do_redo(window);
            else
                do_undo(window);
            return TRUE;
        case GDK_KEY_y:
        case GDK_KEY_Y:
            do_redo(window);
            return TRUE;
        case GDK_KEY_a:
        case GDK_KEY_A:
        {
            GtkTextView *view = window_view(window);
            GtkTextBuffer *buffer = view != NULL ? gtk_text_view_get_buffer(view) : NULL;
            if (buffer != NULL) {
                GtkTextIter start, end;
                gtk_text_buffer_get_start_iter(buffer, &start);
                gtk_text_buffer_get_end_iter(buffer, &end);
                gtk_text_buffer_select_range(buffer, &start, &end);
                gtk_widget_grab_focus(GTK_WIDGET(view));
            }
            return TRUE;
        }
        case GDK_KEY_f:
        case GDK_KEY_F:
            invoke_archive_search(window);
            return TRUE;
        case GDK_KEY_s:
        case GDK_KEY_S:
            save_now(window);
            return TRUE;
        case GDK_KEY_n:
        case GDK_KEY_N:
            invoke_new(window);
            return TRUE;
        case GDK_KEY_e:
        case GDK_KEY_E:
            invoke_archive(window);
            return TRUE;
        case GDK_KEY_t:
        case GDK_KEY_T:
            on_tags_activate(NULL, window);
            return TRUE;
        case GDK_KEY_b:
        case GDK_KEY_B:
            apply_named_tag(window, "bold");
            return TRUE;
        case GDK_KEY_i:
        case GDK_KEY_I:
            apply_named_tag(window, "italic");
            return TRUE;
        case GDK_KEY_u:
        case GDK_KEY_U:
            apply_named_tag(window, "underline");
            return TRUE;
        case GDK_KEY_x:
        case GDK_KEY_X:
            if (shift) {
                apply_named_tag(window, "strike");
                return TRUE;
            }
            break;
        default:
            break;
        }
    }

    return FALSE;
}

/* Aggiorna lo stato collassato e le dimensioni. */
static void update_collapsed(GtkWidget *window, gboolean collapsed)
{
    Note *note = window_note(window);
    GtkWidget *scrolled = g_object_get_data(G_OBJECT(window), "scrolled");
    int width;

    if (note == NULL || scrolled == NULL)
        return;

    width = note->w > 0 ? note->w : 280;
    if (note->h < 100)
        note->h = 260;
    note->collapsed = collapsed;
    /* Nasconde il contenuto e ridimensiona la finestra */
    gtk_widget_set_visible(scrolled, !collapsed);
    if (collapsed)
        gtk_window_resize(GTK_WINDOW(window), width, 32);
    else
        gtk_window_resize(GTK_WINDOW(window), width, note->h);
    touch(window);
}

/* Apre il calendario dall'header. */
static void header_calendar_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    on_calendar_activate(NULL, data);
}

/* Alterna lo stato collassato. */
static void header_collapse_clicked(GtkButton *button, gpointer data)
{
    GtkWidget *window = data;
    Note *note = window_note(window);

    (void)button;

    if (note != NULL)
        update_collapsed(window, !note->collapsed);
}

/* Salva e chiude dall'header. */
static void header_close_clicked(GtkButton *button, gpointer data)
{
    GtkWidget *window = data;

    (void)button;

    save_now(window);
    gtk_widget_destroy(window);
}

/* Mostra il menu contestuale. */
static gboolean on_context_menu(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    GtkWidget *window = data;
    GtkWidget *menu;

    (void)widget;

    if (event->type != GDK_BUTTON_PRESS || event->button != 3)
        return FALSE;

    menu = g_object_get_data(G_OBJECT(window), "menu");
    if (menu == NULL)
        return FALSE;

    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
    return TRUE;
}

/* Imposta il nome accessibile di un widget. */
static void set_accessible_name(GtkWidget *widget, const char *name)
{
    AtkObject *accessible = gtk_widget_get_accessible(widget);

    if (accessible != NULL)
        atk_object_set_name(accessible, name);
}

/* Crea e inizializza una nuova finestra nota. */
GtkWidget *note_window_new(Store *store, Note *note)
{
    GtkWidget *window;
    GtkWidget *outer;
    GtkWidget *header;
    GtkWidget *title;
    GtkWidget *menu;
    GtkWidget *scrolled;
    GtkWidget *view;
    GtkTextBuffer *buffer;
    char name[48];

    g_return_val_if_fail(store != NULL, NULL);
    g_return_val_if_fail(note != NULL, NULL);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    g_snprintf(name, sizeof(name), "jotilla-note-%u", ++note_window_seq);
    gtk_widget_set_name(window, name);

    g_object_set_data(G_OBJECT(window), "store", store);
    g_object_set_data(G_OBJECT(window), "note", note);
    g_object_set_data(G_OBJECT(window), "dirty", GINT_TO_POINTER(0));
    g_object_set_data(G_OBJECT(window), "loading", GINT_TO_POINTER(0));
    g_object_set_data(G_OBJECT(window), "view-alive", GINT_TO_POINTER(1));
    g_object_set_data(G_OBJECT(window), "deleted", GINT_TO_POINTER(0));

    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window),
                                note->w > 0 ? note->w : 280,
                                (note->collapsed || note->h < 100) ? 260 :
                                (note->h > 0 ? note->h : 260));
    gtk_window_move(GTK_WINDOW(window), note->x, note->y);
    gtk_window_set_title(GTK_WINDOW(window), "Jotilla");

    outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), outer);

    header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(header), "jotilla-header");
    gtk_container_set_border_width(GTK_CONTAINER(header), 0);
    gtk_box_pack_start(GTK_BOX(outer), header, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(window), "header", header);

    {
        GtkWidget *calendar_button = gtk_button_new_from_icon_name(
            "x-office-calendar-symbolic", GTK_ICON_SIZE_MENU);
        gtk_button_set_relief(GTK_BUTTON(calendar_button), GTK_RELIEF_NONE);
        gtk_widget_set_tooltip_text(calendar_button, "Assign a date");
        set_accessible_name(calendar_button, "Assign a date");
        g_signal_connect(calendar_button, "clicked",
                         G_CALLBACK(header_calendar_clicked), window);
        gtk_box_pack_start(GTK_BOX(header), calendar_button, FALSE, FALSE, 0);
    }

    title = gtk_label_new("Untitled");
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode(GTK_LABEL(title), TRUE);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_widget_set_halign(title, GTK_ALIGN_FILL);
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "jotilla-title");
    gtk_box_pack_start(GTK_BOX(header), title, TRUE, TRUE, 0);
    g_object_set_data(G_OBJECT(window), "title-label", title);

    {
        GtkWidget *minimize = gtk_button_new_with_label("—");
        GtkWidget *close = gtk_button_new_with_label("×");

        gtk_button_set_relief(GTK_BUTTON(minimize), GTK_RELIEF_NONE);
        gtk_widget_set_tooltip_text(minimize, "Collapse");
        set_accessible_name(minimize, "Collapse");
        g_signal_connect(minimize, "clicked",
                         G_CALLBACK(header_collapse_clicked), window);
        gtk_box_pack_start(GTK_BOX(header), minimize, FALSE, FALSE, 0);

        gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
        gtk_widget_set_tooltip_text(close, "Close (Esc)");
        set_accessible_name(close, "Close");
        g_signal_connect(close, "clicked",
                         G_CALLBACK(header_close_clicked), window);
        gtk_box_pack_start(GTK_BOX(header), close, FALSE, FALSE, 0);
    }

    menu = build_menu(window);
    g_object_set_data_full(G_OBJECT(window), "menu", menu,
                           (GDestroyNotify)gtk_widget_destroy);

    g_signal_connect(header, "button-press-event", G_CALLBACK(on_header_press), window);
    g_signal_connect(header, "button-press-event", G_CALLBACK(on_context_menu), window);

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_NONE);
    gtk_box_pack_start(GTK_BOX(outer), scrolled, TRUE, TRUE, 0);

    view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), 8);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 8);
    gtk_container_add(GTK_CONTAINER(scrolled), view);
    set_accessible_name(view, "Note text");
    g_object_set_data(G_OBJECT(window), "textview", view);
    g_object_set_data(G_OBJECT(window), "scrolled", scrolled);
    g_signal_connect(view, "destroy", G_CALLBACK(on_view_destroy), window);
    g_signal_connect(view, "button-press-event", G_CALLBACK(on_view_button_press), window);
    g_signal_connect(view, "scroll-event", G_CALLBACK(on_view_scroll), window);

    load_note(window);
    restyle(window);
    apply_opacity(window);
    update_date_tooltip(window);

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    g_signal_connect(buffer, "changed", G_CALLBACK(on_buffer_changed), window);
    g_signal_connect(window, "configure-event", G_CALLBACK(on_configure), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), NULL);
    g_signal_connect(window, "delete-event", G_CALLBACK(on_delete), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);

    gtk_widget_show_all(outer);

    if (note->collapsed) {
        GtkWidget *collapsed_view = g_object_get_data(G_OBJECT(window), "scrolled");
        if (collapsed_view != NULL)
            gtk_widget_hide(collapsed_view);
        gtk_window_resize(GTK_WINDOW(window), note->w > 0 ? note->w : 280, 32);
    }

    return window;
}

/* Ricarica stile, contenuto e stato della finestra. */
void note_window_refresh(GtkWidget *window)
{
    if (window == NULL)
        return;

    remove_timeout(window);
    load_note(window);
    restyle(window);
    apply_opacity(window);
    update_title(window);
    update_date_tooltip(window);
    g_object_set_data(G_OBJECT(window), "dirty", GINT_TO_POINTER(0));
}

/* Imposta il callback di modifica nota. */
void note_window_set_changed_callback(GtkWidget *window, NoteChangedFunc cb, gpointer data)
{
    ChangedHook *hook;

    if (window == NULL)
        return;

    if (cb == NULL) {
        g_object_set_data(G_OBJECT(window), "changed-hook", NULL);
        return;
    }

    hook = g_new0(ChangedHook, 1);
    hook->fn = cb;
    hook->data = data;
    g_object_set_data_full(G_OBJECT(window), "changed-hook", hook, g_free);
}

/* Restituisce la nota della finestra. */
Note *note_window_get_note(GtkWidget *window)
{
    if (window == NULL)
        return NULL;
    return window_note(window);
}

/* Salva la nota su richiesta di altri moduli. */
void note_window_save(GtkWidget *window)
{
    if (window == NULL)
        return;
    save_now(window);
}

/* Imposta il callback di nuova nota. */
void note_window_set_new_callback(GtkWidget *window, void (*cb)(gpointer), gpointer data)
{
    SimpleHook *hook;

    if (window == NULL)
        return;

    if (cb == NULL) {
        g_object_set_data(G_OBJECT(window), "new-hook", NULL);
        return;
    }

    hook = g_new0(SimpleHook, 1);
    hook->fn = cb;
    hook->data = data;
    g_object_set_data_full(G_OBJECT(window), "new-hook", hook, g_free);
}

/* Imposta il callback di archivio. */
void note_window_set_archive_callback(GtkWidget *window, void (*cb)(gpointer), gpointer data)
{
    SimpleHook *hook;

    if (window == NULL)
        return;

    if (cb == NULL) {
        g_object_set_data(G_OBJECT(window), "archive-hook", NULL);
        return;
    }

    hook = g_new0(SimpleHook, 1);
    hook->fn = cb;
    hook->data = data;
    g_object_set_data_full(G_OBJECT(window), "archive-hook", hook, g_free);
}
