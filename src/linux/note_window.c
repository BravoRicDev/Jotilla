/* Finestra nota GTK: crea, modifica e salva le note. */
/* Gestisce stile, menu, scorciatoie e stato della finestra. */
#include <gtk/gtk.h>

#include "jotilla.h"
#include "note_window.h"
#include "palette.h"
#include "rtf.h"
#include "platform.h"
#include "calendar.h"

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

/* Salva il contenuto e lo stato della finestra. */
static void save_now(GtkWidget *window);

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
        if (!store_save(store, note, &err)) {
            if (err != NULL) {
                g_warning("Could not save note: %s", err->message);
                g_error_free(err);
            }
        }
    }

    g_object_set_data(G_OBJECT(window), "dirty", GINT_TO_POINTER(0));
    remove_timeout(window);

    {
        ChangedHook *hook = g_object_get_data(G_OBJECT(window), "changed-hook");
        if (hook != NULL && hook->fn != NULL)
            hook->fn(note, hook->data);
    }
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
        "#%s textview, #%s textview text { background-color: transparent; color: %s; }\n"
        "#%s textview { font-size: %dpt; }\n"
        "#%s scrolledwindow, #%s scrolledwindow viewport { background-color: transparent; "
        "border: none; }\n",
        name, bg, ink, bar,
        name, bg, bar,
        name, ink,
        name, ink,
        name,
        name, name, ink,
        name, font_size,
        name, name);

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

    g_object_set_data(G_OBJECT(window), "loading", GINT_TO_POINTER(1));
    if (note->rtf != NULL && note->rtf[0] != '\0')
        rtf_to_buffer(note->rtf, buffer);
    else
        gtk_text_buffer_set_text(buffer, note->text != NULL ? note->text : "", -1);
    g_object_set_data(G_OBJECT(window), "loading", GINT_TO_POINTER(0));

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
    touch(window);
}

/* Inserisce un elemento di checklist nel cursore. */
static void insert_checklist(GtkWidget *window)
{
    GtkTextView *view = window_view(window);
    GtkTextBuffer *buffer;

    if (view == NULL)
        return;

    buffer = gtk_text_view_get_buffer(view);
    if (buffer == NULL)
        return;

    gtk_text_buffer_insert_at_cursor(buffer, "\xE2\x98\x90 ", -1);
    gtk_text_view_scroll_mark_onscreen(view, gtk_text_buffer_get_insert(buffer));
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

/* Dialogo per scegliere il colore del testo. */
static void on_color_activate(GtkMenuItem *item, gpointer data)
{
    GtkWidget *window = data;
    GtkTextView *view = window_view(window);
    GtkTextBuffer *buffer;
    GtkTextIter start, end;
    GtkWidget *dialog;
    GdkRGBA rgba;
    gint response;

    (void)item;

    if (view == NULL)
        return;

    buffer = gtk_text_view_get_buffer(view);
    if (buffer == NULL)
        return;

    if (!gtk_text_buffer_get_selection_bounds(buffer, &start, &end))
        return;

    dialog = gtk_color_chooser_dialog_new("Text color", GTK_WINDOW(window));
    rgba.red = 0.0;
    rgba.green = 0.0;
    rgba.blue = 0.0;
    rgba.alpha = 1.0;
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dialog), &rgba);

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK) {
        char hex[8];
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog), &rgba);
        g_snprintf(hex, sizeof(hex), "#%02x%02x%02x",
                   (unsigned)(rgba.red * 255.0 + 0.5),
                   (unsigned)(rgba.green * 255.0 + 0.5),
                   (unsigned)(rgba.blue * 255.0 + 0.5));
        apply_foreground(window, hex);
    }
    gtk_widget_destroy(dialog);
}

/* Conferma ed elimina la nota. */
static void on_delete_activate(GtkMenuItem *item, gpointer data)
{
    GtkWidget *window = data;
    Store *store = window_store(window);
    Note *note = window_note(window);
    GtkWidget *dialog;
    gint response;

    (void)item;

    if (store == NULL || note == NULL)
        return;

    dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
                                    "Delete this note?");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "This action cannot be undone.");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog),
                           "_Cancel", GTK_RESPONSE_CANCEL,
                           "_Delete", GTK_RESPONSE_ACCEPT,
                           NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response != GTK_RESPONSE_ACCEPT)
        return;

    remove_timeout(window);
    {
        GError *err = NULL;
        if (!store_delete(store, note->id, &err)) {
            if (err != NULL) {
                g_warning("Could not delete note: %s", err->message);
                g_error_free(err);
            }
            return;
        }
    }

    g_object_set_data(G_OBJECT(window), "deleted", GINT_TO_POINTER(1));
    g_object_set_data(G_OBJECT(window), "note", NULL);
    gtk_widget_destroy(window);
}

/* Salva e chiude la finestra. */
static void on_close_activate(GtkMenuItem *item, gpointer data)
{
    GtkWidget *window = data;

    (void)item;

    save_now(window);
    gtk_widget_destroy(window);
}

/* Cambia la palette della nota. */
static void on_palette_activate(GtkMenuItem *item, gpointer data)
{
    GtkWidget *window = data;
    Note *note = window_note(window);
    int palette = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "palette"));

    if (note == NULL)
        return;
    if (palette < 0 || palette >= PALETTE_COUNT)
        return;

    note->palette = palette;
    note->color = 0;
    restyle(window);
    touch(window);
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

/* Attiva/disattiva sempre in primo piano. */
static void on_keep_above_toggled(GtkCheckMenuItem *item, gpointer data)
{
    GtkWidget *window = data;

    platform_set_keep_above(window, gtk_check_menu_item_get_active(item));
}

/* Attiva/disattiva sticky sulle scrivanie. */
static void on_sticky_toggled(GtkCheckMenuItem *item, gpointer data)
{
    GtkWidget *window = data;

    platform_set_sticky(window, gtk_check_menu_item_get_active(item));
}

/* Gestore menu: nuova nota. */
static void on_menu_new(GtkMenuItem *item, gpointer data)
{
    (void)item;
    invoke_new(data);
}

/* Gestore menu: apri archivio. */
static void on_menu_archive(GtkMenuItem *item, gpointer data)
{
    (void)item;
    invoke_archive(data);
}

/* Gestore menu: grassetto. */
static void on_format_bold(GtkMenuItem *item, gpointer data)
{
    (void)item;
    apply_named_tag(data, "bold");
}

/* Gestore menu: corsivo. */
static void on_format_italic(GtkMenuItem *item, gpointer data)
{
    (void)item;
    apply_named_tag(data, "italic");
}

/* Gestore menu: sottolineato. */
static void on_format_underline(GtkMenuItem *item, gpointer data)
{
    (void)item;
    apply_named_tag(data, "underline");
}

/* Gestore menu: barrato. */
static void on_format_strike(GtkMenuItem *item, gpointer data)
{
    (void)item;
    apply_named_tag(data, "strike");
}

/* Gestore menu: checklist. */
static void on_format_checklist(GtkMenuItem *item, gpointer data)
{
    (void)item;
    insert_checklist(data);
}

/* Costruisce il menu contestuale della nota. */
static GtkWidget *build_menu(GtkWidget *window)
{
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *item;
    GtkWidget *submenu;
    Note *note = window_note(window);

    item = gtk_menu_item_new_with_label("New note");
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_new), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Archive");
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_archive), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Tags...");
    g_signal_connect(item, "activate", G_CALLBACK(on_tags_activate), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Calendar...");
    g_signal_connect(item, "activate", G_CALLBACK(on_calendar_activate), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Edit title...");
    g_signal_connect(item, "activate", G_CALLBACK(on_edit_title_activate), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    item = gtk_menu_item_new_with_label("Format");
    submenu = gtk_menu_new();

    {
        GtkWidget *bold = gtk_menu_item_new_with_label("Bold");
        GtkWidget *italic = gtk_menu_item_new_with_label("Italic");
        GtkWidget *underline = gtk_menu_item_new_with_label("Underline");
        GtkWidget *strike = gtk_menu_item_new_with_label("Strikethrough");
        GtkWidget *checklist = gtk_menu_item_new_with_label("Insert checklist item");
        GtkWidget *color = gtk_menu_item_new_with_label("Text color...");

        g_signal_connect(bold, "activate", G_CALLBACK(on_format_bold), window);
        g_signal_connect(italic, "activate", G_CALLBACK(on_format_italic), window);
        g_signal_connect(underline, "activate", G_CALLBACK(on_format_underline), window);
        g_signal_connect(strike, "activate", G_CALLBACK(on_format_strike), window);
        g_signal_connect(checklist, "activate", G_CALLBACK(on_format_checklist), window);
        g_signal_connect(color, "activate", G_CALLBACK(on_color_activate), window);

        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), bold);
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), italic);
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), underline);
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), strike);
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), gtk_separator_menu_item_new());
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), checklist);
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), color);
    }

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Palette");
    submenu = gtk_menu_new();
    {
        GSList *group = NULL;
        int i;
        for (i = 0; i < PALETTE_COUNT; i++) {
            GtkWidget *palette_item = gtk_radio_menu_item_new_with_label(group, palette_name(i));
            group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(palette_item));
            if (note != NULL && note->palette == i)
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(palette_item), TRUE);
            g_object_set_data(G_OBJECT(palette_item), "palette", GINT_TO_POINTER(i));
            g_signal_connect(palette_item, "activate", G_CALLBACK(on_palette_activate), window);
            gtk_menu_shell_append(GTK_MENU_SHELL(submenu), palette_item);
        }
    }
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Opacity");
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
            g_object_set_data(G_OBJECT(opacity_item), "opacity", GINT_TO_POINTER(levels[i]));
            g_signal_connect(opacity_item, "activate", G_CALLBACK(on_opacity_activate), window);
            gtk_menu_shell_append(GTK_MENU_SHELL(submenu), opacity_item);
        }
    }
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    item = gtk_check_menu_item_new_with_label("Always on top");
    g_signal_connect(item, "toggled", G_CALLBACK(on_keep_above_toggled), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_check_menu_item_new_with_label("Sticky");
    g_signal_connect(item, "toggled", G_CALLBACK(on_sticky_toggled), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    item = gtk_menu_item_new_with_label("Delete note...");
    g_signal_connect(item, "activate", G_CALLBACK(on_delete_activate), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Close");
    g_signal_connect(item, "activate", G_CALLBACK(on_close_activate), window);
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

/* Salva prima della chiusura della finestra. */
static gboolean on_delete(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    GtkWidget *window = widget;

    (void)event;
    (void)data;

    if (!GPOINTER_TO_INT(g_object_get_data(G_OBJECT(window), "deleted")))
        save_now(window);
    return FALSE;
}

/* Pulisce timer/CSS e salva se necessario. */
static void on_destroy(GtkWidget *widget, gpointer data)
{
    GtkWidget *window = widget;

    (void)data;

    remove_timeout(window);

    if (!GPOINTER_TO_INT(g_object_get_data(G_OBJECT(window), "deleted")) &&
        GPOINTER_TO_INT(g_object_get_data(G_OBJECT(window), "dirty")))
        save_now(window);

    remove_css(window);

    g_object_set_data(G_OBJECT(window), "store", NULL);
    g_object_set_data(G_OBJECT(window), "note", NULL);
    g_object_set_data(G_OBJECT(window), "textview", NULL);
    g_object_set_data(G_OBJECT(window), "title-label", NULL);
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

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    GtkWidget *window = widget;
    guint key = event->keyval;
    gboolean ctrl = (event->state & GDK_CONTROL_MASK) != 0;
    gboolean shift = (event->state & GDK_SHIFT_MASK) != 0;

    (void)data;

    if (!ctrl && !shift && key == GDK_KEY_Escape) {
        save_now(window);
        gtk_widget_destroy(window);
        return TRUE;
    }

    if (!ctrl && shift && key == GDK_KEY_Return) {
        if (promote_title(window))
            return TRUE;
    }

    if (ctrl) {
        switch (key) {
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

    {
        GtkWidget *calendar_button = gtk_button_new_from_icon_name(
            "x-office-calendar-symbolic", GTK_ICON_SIZE_MENU);
        gtk_button_set_relief(GTK_BUTTON(calendar_button), GTK_RELIEF_NONE);
        gtk_widget_set_tooltip_text(calendar_button, "Assign a date");
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
        g_signal_connect(minimize, "clicked",
                         G_CALLBACK(header_collapse_clicked), window);
        gtk_box_pack_start(GTK_BOX(header), minimize, FALSE, FALSE, 0);

        gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
        gtk_widget_set_tooltip_text(close, "Close (Esc)");
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
    g_object_set_data(G_OBJECT(window), "textview", view);
    g_object_set_data(G_OBJECT(window), "scrolled", scrolled);
    g_signal_connect(view, "destroy", G_CALLBACK(on_view_destroy), window);
    g_signal_connect(view, "button-press-event", G_CALLBACK(on_context_menu), window);

    load_note(window);
    restyle(window);
    apply_opacity(window);

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
