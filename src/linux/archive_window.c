/* Finestra archivio GTK per elencare e cercare note. */
/* Gestisce tile, calendario, filtri, albero date/tag e azioni. */
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>

#include "archive_window.h"
#include "date.h"
#include "palette.h"
#include "platform.h"
#include "tags.h"

#define ARCHIVE_STATE_KEY "archive-window-state"
#define ARCHIVE_STORE_KEY "archive-store"
#define ARCHIVE_RESULT_LIMIT 500
#define ARCHIVE_CARD_WIDTH 220
#define ARCHIVE_CARD_HEIGHT 170
#define ARCHIVE_HIGHLIGHT "#e4c46d"

enum {
    DATE_COL_LABEL,
    DATE_COL_VALUE,
    DATE_COL_KIND,
    DATE_COL_COUNT
};

enum {
    TAG_COL_LABEL,
    TAG_COL_VALUE,
    TAG_COL_COUNT,
    TAG_COL_N_COLUMNS
};

typedef struct {
    GtkWidget *window;
    Store *store;
    GtkWidget *header;
    GtkWidget *search;
    GtkWidget *tag_combo;
    GtkWidget *stack;
    GtkWidget *flow;
    GtkWidget *flow_scroll;
    GtkWidget *calendar;
    GtkWidget *calendar_list;
    GtkWidget *calendar_label;
    GtkWidget *sidebar_stack;
    GtkWidget *date_tree;
    GtkWidget *tag_tree;
    GtkTreeStore *date_model;
    GtkTreeStore *tag_model;
    ArchiveOpenFunc open_cb;
    gpointer open_data;
    ArchiveChangeFunc change_cb;
    gpointer change_data;
    Note *first_note;
    char *tag_filter;
    guint32 date_filter;
    gint date_kind;
    gboolean sidebar_tags;
    gboolean updating_sidebar;
    gdouble flow_scroll_x;
    gdouble flow_scroll_y;
    guint flow_scroll_restore_id;
} ArchiveWindow;

/* Ricarica la lista con filtri e ricerca. */
static void repopulate_list(ArchiveWindow *aw);
/* Aggiorna il calendario e le note del giorno. */
static void update_calendar(ArchiveWindow *aw);
/* Conferma ed elimina una nota dall'archivio. */
static void confirm_delete(ArchiveWindow *aw, Note *note);
/* Elimina le note selezionate nel flow box dopo conferma. */
static void delete_selected_notes(ArchiveWindow *aw);
/* Mostra il menu contestuale di una card. */
static void show_card_menu(ArchiveWindow *aw, Note *note,
                           GdkEventButton *event);
/* Ripristina la posizione di scorrimento del flow box. */
static gboolean restore_flow_scroll(gpointer data);
/* Popola il combo dei tag. */
static void populate_tag_combo(ArchiveWindow *aw);
/* Ricostruisce gli alberi di date e tag. */
static void populate_sidebar(ArchiveWindow *aw);
/* Seleziona il nodo tag corrente. */
static void sync_tag_tree(ArchiveWindow *aw);
/* Pulisce ricerca e filtri. */
static void clear_search_filters(ArchiveWindow *aw);

/* Restituisce lo stato della finestra archivio. */
static ArchiveWindow *get_state(GtkWidget *window) {
    return g_object_get_data(G_OBJECT(window), ARCHIVE_STATE_KEY);
}

/* Imposta tooltip e nome accessibile per un controllo. */
static void set_accessible_label(GtkWidget *widget, const char *label) {
    AtkObject *accessible;

    if (!widget || !label) return;
    gtk_widget_set_tooltip_text(widget, label);
    accessible = gtk_widget_get_accessible(widget);
    if (accessible) atk_object_set_name(accessible, label);
}

/* Svuota un list box rimuovendo i figli. */
static void clear_listbox(GtkWidget *list) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(list));
    GList *item;

    for (item = children; item != NULL; item = item->next)
        gtk_widget_destroy(GTK_WIDGET(item->data));
    g_list_free(children);
}

/* Svuota il flow box rimuovendo le card. */
static void clear_flowbox(GtkWidget *flow) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(flow));
    GList *item;

    for (item = children; item != NULL; item = item->next)
        gtk_widget_destroy(GTK_WIDGET(item->data));
    g_list_free(children);
}

/* Estrae il titolo dalla prima riga del testo. */
static char *note_title(const char *text) {
    const char *end;
    char *line;

    if (!text || !*text) return g_strdup("(empty)");
    end = text;
    while (*end && *end != '\n' && *end != '\r') ++end;
    line = g_strndup(text, (gsize)(end - text));
    g_strstrip(line);
    if (!*line) {
        g_free(line);
        return g_strdup("(empty)");
    }
    return line;
}

/* Crea un estratto del testo su una riga. */
static char *note_excerpt(const char *text) {
    char *flat;
    char *cursor;
    glong length;

    if (!text || !*text) return g_strdup("");
    flat = g_strdup(text);
    for (cursor = flat; *cursor; ++cursor) {
        if (*cursor == '\n' || *cursor == '\r' || *cursor == '\t')
            *cursor = ' ';
    }
    g_strstrip(flat);
    length = g_utf8_strlen(flat, -1);
    if (length > 160) {
        char *head = g_utf8_substring(flat, 0, 160);
        char *result = g_strconcat(head, "\342\200\246", NULL);
        g_free(head);
        g_free(flat);
        return result;
    }
    return flat;
}

/* Formatta i tag per il footer della card. */
static char *note_tag_list(const char *tags) {
    GPtrArray *items = tags_split(tags);
    GString *text;
    guint i;

    if (!items->len) {
        g_ptr_array_free(items, TRUE);
        return g_strdup("No tags");
    }
    text = g_string_new(NULL);
    for (i = 0; i < items->len; ++i) {
        if (i) g_string_append(text, ", ");
        g_string_append(text, (const char *)g_ptr_array_index(items, i));
    }
    g_ptr_array_free(items, TRUE);
    return g_string_free(text, FALSE);
}

/* Formatta i tag per la riga del calendario. */
static char *note_tags(const char *tags) {
    GPtrArray *items = tags_split(tags);
    GString *text;
    guint i;

    if (!items->len) {
        g_ptr_array_free(items, TRUE);
        return g_strdup("No tags");
    }
    text = g_string_new("Tags: ");
    for (i = 0; i < items->len; ++i) {
        if (i) g_string_append(text, ", ");
        g_string_append(text, (const char *)g_ptr_array_index(items, i));
    }
    g_ptr_array_free(items, TRUE);
    return g_string_free(text, FALSE);
}

/* Formatta la data della nota. */
static char *note_date(guint32 date) {
    char *formatted;

    if (!date || !date_valid(date)) return g_strdup("No date");
    formatted = date_format(date);
    if (!formatted || !*formatted) {
        g_free(formatted);
        return g_strdup("No date");
    }
    return formatted;
}

/* Evidenzia le occorrenze della query nell'estratto con markup Pango. */
static char *highlight_excerpt(const char *text, const char *query) {
    GString *folded;
    GString *markup;
    GArray *map;
    const char *cursor;
    char *folded_query;
    gsize text_len;
    gsize needle_len;
    gsize last = 0;
    gsize search_from = 0;

    if (!text) return g_strdup("");
    if (!query || !*query) return g_markup_escape_text(text, -1);

    folded_query = g_utf8_casefold(query, -1);
    if (!folded_query || !*folded_query) {
        g_free(folded_query);
        return g_markup_escape_text(text, -1);
    }

    folded = g_string_new(NULL);
    map = g_array_new(FALSE, FALSE, sizeof(gsize));
    cursor = text;
    while (*cursor) {
        const char *next = g_utf8_next_char(cursor);
        gsize original = (gsize)(cursor - text);
        char *piece = g_utf8_casefold(cursor, (gssize)(next - cursor));

        if (piece) {
            gsize bytes = strlen(piece);
            gsize b;
            g_string_append(folded, piece);
            for (b = 0; b < bytes; ++b)
                g_array_append_val(map, original);
            g_free(piece);
        } else {
            const char *byte;
            for (byte = cursor; byte < next; ++byte) {
                g_string_append_c(folded, *byte);
                g_array_append_val(map, original);
            }
        }
        cursor = next;
    }
    text_len = (gsize)strlen(text);
    g_array_append_val(map, text_len);

    needle_len = strlen(folded_query);
    markup = g_string_new(NULL);
    while (needle_len > 0 && search_from + needle_len <= folded->len) {
        char *found = g_strstr_len(folded->str + search_from,
                                   folded->len - search_from, folded_query);
        gsize at;
        gsize match_start;
        gsize match_end;
        char *escaped;

        if (!found) break;
        at = (gsize)(found - folded->str);
        match_start = g_array_index(map, gsize, at);
        match_end = g_array_index(map, gsize, at + needle_len);
        if (match_end <= match_start || match_start < last) {
            search_from = at + needle_len;
            continue;
        }
        escaped = g_markup_escape_text(text + last,
                                       (gssize)(match_start - last));
        g_string_append(markup, escaped);
        g_free(escaped);
        g_string_append(markup, "<span background=\""
                                ARCHIVE_HIGHLIGHT "\">");
        escaped = g_markup_escape_text(text + match_start,
                                       (gssize)(match_end - match_start));
        g_string_append(markup, escaped);
        g_free(escaped);
        g_string_append(markup, "</span>");
        last = match_end;
        search_from = at + needle_len;
    }
    {
        char *tail = g_markup_escape_text(text + last,
                                          (gssize)(text_len - last));
        g_string_append(markup, tail);
        g_free(tail);
    }

    g_array_free(map, TRUE);
    g_string_free(folded, TRUE);
    g_free(folded_query);
    return g_string_free(markup, FALSE);
}

/* Verifica se la nota rientra nel filtro data della sidebar. */
static gboolean note_matches_date(const ArchiveWindow *aw, guint32 date) {
    if (!aw->date_filter || aw->date_kind <= 0) return TRUE;
    switch (aw->date_kind) {
    case 1:
        return date / 10000u == aw->date_filter / 10000u;
    case 2:
        return date / 100u == aw->date_filter / 100u;
    case 3:
        return date == aw->date_filter;
    default:
        return TRUE;
    }
}

/* Evidenzia le card selezionate. */
static void on_flow_selection_changed(GtkFlowBox *box, gpointer data) {
    GList *children;
    GList *item;

    (void)data;
    children = gtk_container_get_children(GTK_CONTAINER(box));
    for (item = children; item != NULL; item = item->next) {
        GtkWidget *child = GTK_WIDGET(item->data);
        GtkWidget *card = g_object_get_data(G_OBJECT(child), "card");

        if (!card) continue;
        if (gtk_flow_box_child_is_selected(GTK_FLOW_BOX_CHILD(child)))
            gtk_widget_set_state_flags(card, GTK_STATE_FLAG_SELECTED, FALSE);
        else
            gtk_widget_unset_state_flags(card, GTK_STATE_FLAG_SELECTED);
    }
    g_list_free(children);
}

/* Gestisce il clic destro sulla lista del calendario. */
static gboolean on_list_button_press(GtkWidget *widget, GdkEventButton *event,
                                     gpointer data) {
    ArchiveWindow *aw = data;
    GtkListBoxRow *row;
    Note *note;

    if (event->type != GDK_BUTTON_PRESS || event->button != 3) return FALSE;
    row = gtk_list_box_get_row_at_y(GTK_LIST_BOX(widget), (gint)event->y);
    if (!row) return FALSE;
    note = g_object_get_data(G_OBJECT(row), "note");
    confirm_delete(aw, note);
    return TRUE;
}

/* Apre la nota scelta dal menu contestuale. */
static void on_menu_open(GtkMenuItem *item, gpointer data) {
    ArchiveWindow *aw = data;
    Note *note = g_object_get_data(G_OBJECT(item), "note");

    if (note && aw->open_cb) aw->open_cb(note, aw->open_data);
}

/* Elimina la nota scelta dal menu contestuale. */
static void on_menu_delete(GtkMenuItem *item, gpointer data) {
    ArchiveWindow *aw = data;
    Note *note = g_object_get_data(G_OBJECT(item), "note");

    confirm_delete(aw, note);
}

/* Rilascia il menu contestuale quando viene chiuso. */
static void on_menu_selection_done(GtkMenuShell *menu, gpointer data) {
    (void)data;
    gtk_widget_destroy(GTK_WIDGET(menu));
    g_object_unref(menu);
}

/* Mostra il menu contestuale per la card indicata. */
static void show_card_menu(ArchiveWindow *aw, Note *note,
                           GdkEventButton *event) {
    GtkWidget *menu;
    GtkWidget *item;

    if (!aw || !note || !event) return;

    menu = gtk_menu_new();
    g_object_ref_sink(menu);
    gtk_menu_attach_to_widget(GTK_MENU(menu), aw->flow, NULL);
    g_signal_connect(menu, "selection-done",
                     G_CALLBACK(on_menu_selection_done), NULL);

    item = gtk_menu_item_new_with_label("Open");
    g_object_set_data(G_OBJECT(item), "note", note);
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_open), aw);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Delete from archive");
    g_object_set_data(G_OBJECT(item), "note", note);
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_delete), aw);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
}

/* Gestisce il clic destro sulla singola card. */
static gboolean on_card_button_press(GtkWidget *widget, GdkEventButton *event,
                                     gpointer data) {
    ArchiveWindow *aw = data;
    Note *note = g_object_get_data(G_OBJECT(widget), "note");

    if (event->type != GDK_BUTTON_PRESS || event->button != 3) return FALSE;
    if (!note) return FALSE;
    show_card_menu(aw, note, event);
    return TRUE;
}

/* Gestisce clic destro e clic singolo sulle card. */
static gboolean on_flow_button_press(GtkWidget *widget, GdkEventButton *event,
                                     gpointer data) {
    ArchiveWindow *aw = data;
    GtkFlowBoxChild *child;
    Note *note;

    if (event->type != GDK_BUTTON_PRESS) return FALSE;
    child = gtk_flow_box_get_child_at_pos(GTK_FLOW_BOX(widget),
                                          (gint)event->x, (gint)event->y);
    if (!child) return FALSE;
    note = g_object_get_data(G_OBJECT(child), "note");

    if (event->button == 3) {
        if (note) show_card_menu(aw, note, event);
        return TRUE;
    }
    if (event->button != 1) return FALSE;
    if (event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) return FALSE;
    if (note && aw->open_cb) aw->open_cb(note, aw->open_data);
    return TRUE;
}

/* Apre la card attivata da tastiera. */
static void on_flow_child_activated(GtkFlowBox *box, GtkFlowBoxChild *child,
                                    gpointer data) {
    ArchiveWindow *aw = data;
    Note *note = g_object_get_data(G_OBJECT(child), "note");

    (void)box;
    if (note && aw->open_cb) aw->open_cb(note, aw->open_data);
}

/* Avvia l'eliminazione dalla riga calendario. */
static void on_delete_clicked(GtkButton *button, gpointer data) {
    ArchiveWindow *aw = data;
    Note *note = g_object_get_data(G_OBJECT(button), "note");

    confirm_delete(aw, note);
}

/* Apre la nota selezionata nel calendario. */
static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    ArchiveWindow *aw = data;
    Note *note = g_object_get_data(G_OBJECT(row), "note");

    (void)box;
    if (note && aw->open_cb) aw->open_cb(note, aw->open_data);
}

/* Applica un provider CSS a un widget e a tutti i suoi discendenti. */
static void apply_card_provider(GtkWidget *widget, GtkCssProvider *provider) {
    GtkStyleContext *context = gtk_widget_get_style_context(widget);

    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    if (GTK_IS_CONTAINER(widget)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
        GList *item;

        for (item = children; item != NULL; item = item->next)
            apply_card_provider(GTK_WIDGET(item->data), provider);
        g_list_free(children);
    }
}

/* Costruisce la card tile per una nota. */
static GtkWidget *build_note_card(ArchiveWindow *aw, Note *note,
                                  const char *query) {
    static guint card_serial = 0;
    GtkWidget *child;
    GtkWidget *card;
    GtkWidget *header_row;
    GtkWidget *content;
    GtkWidget *title;
    GtkWidget *remove;
    GtkWidget *excerpt_label;
    GtkWidget *footer;
    GtkWidget *tags;
    GtkWidget *date;
    GtkCssProvider *provider;
    GtkStyleContext *context;
    PangoAttrList *attrs;
    char *value;
    char *excerpt;
    char *css;
    char class_name[64];
    const char *paper;
    const char *bar;
    const char *ink;

    child = gtk_flow_box_child_new();
    card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(card, ARCHIVE_CARD_WIDTH, ARCHIVE_CARD_HEIGHT);

    paper = palette_color_hex(note->palette, note->color);
    bar = palette_bar_hex(note->palette, note->color);
    ink = palette_ink_hex(note->palette, note->color);

    ++card_serial;
    g_snprintf(class_name, sizeof(class_name), "note-card-%u", card_serial);
    context = gtk_widget_get_style_context(card);
    gtk_style_context_add_class(context, class_name);
    css = g_strdup_printf(
        ".%s { background-color: %s; border: 1px solid %s;"
        " border-radius: 4px; }"
        ".%s .card-header { background-color: %s;"
        " border-radius: 3px 3px 0 0; }"
        ".%s label { color: %s; }"
        ".%s:selected { border: 2px solid %s; }",
        class_name, paper, bar, class_name, bar, class_name, ink,
        class_name, ink);
    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    g_free(css);

    header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(header_row),
                                "card-header");
    gtk_widget_set_margin_start(header_row, 8);
    gtk_widget_set_margin_end(header_row, 4);
    gtk_widget_set_margin_top(header_row, 4);
    gtk_widget_set_margin_bottom(header_row, 4);

    value = (note->has_title && note->title)
                ? g_strdup(note->title)
                : note_title(note->text);
    title = gtk_label_new(value);
    g_free(value);
    attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    pango_attr_list_unref(attrs);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_label_set_lines(GTK_LABEL(title), 1);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_box_pack_start(GTK_BOX(header_row), title, TRUE, TRUE, 0);

    remove = gtk_button_new_from_icon_name("user-trash-symbolic",
                                           GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_valign(remove, GTK_ALIGN_CENTER);
    set_accessible_label(remove, "Delete note");
    gtk_style_context_add_class(gtk_widget_get_style_context(remove), "flat");
    g_object_set_data(G_OBJECT(remove), "note", note);
    g_signal_connect(remove, "clicked", G_CALLBACK(on_delete_clicked), aw);
    gtk_box_pack_end(GTK_BOX(header_row), remove, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), header_row, FALSE, FALSE, 0);

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(content, 8);
    gtk_widget_set_margin_end(content, 8);
    gtk_widget_set_margin_top(content, 6);
    gtk_widget_set_margin_bottom(content, 6);
    gtk_widget_set_vexpand(content, TRUE);

    excerpt = note_excerpt(note->text);
    excerpt_label = gtk_label_new(NULL);
    if (query && *query) {
        char *markup = highlight_excerpt(excerpt, query);
        gtk_label_set_markup(GTK_LABEL(excerpt_label), markup);
        g_free(markup);
    } else {
        gtk_label_set_text(GTK_LABEL(excerpt_label), excerpt);
    }
    g_free(excerpt);
    gtk_label_set_xalign(GTK_LABEL(excerpt_label), 0.0f);
    gtk_label_set_yalign(GTK_LABEL(excerpt_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(excerpt_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_line_wrap(GTK_LABEL(excerpt_label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(excerpt_label),
                                 PANGO_WRAP_WORD_CHAR);
    gtk_label_set_lines(GTK_LABEL(excerpt_label), 4);
    gtk_widget_set_vexpand(excerpt_label, TRUE);
    gtk_box_pack_start(GTK_BOX(content), excerpt_label, TRUE, TRUE, 0);

    footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    value = note_tag_list(note->tags);
    tags = gtk_label_new(value);
    g_free(value);
    gtk_label_set_xalign(GTK_LABEL(tags), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(tags), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(tags, TRUE);
    gtk_box_pack_start(GTK_BOX(footer), tags, TRUE, TRUE, 0);

    value = note_date(note->date);
    date = gtk_label_new(value);
    g_free(value);
    gtk_label_set_xalign(GTK_LABEL(date), 1.0f);
    gtk_box_pack_end(GTK_BOX(footer), date, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(content), footer, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(card), content, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(child), card);
    apply_card_provider(card, provider);
    g_object_unref(provider);
    g_object_set_data(G_OBJECT(child), "note", note);
    g_object_set_data(G_OBJECT(child), "card", card);
    g_signal_connect(child, "button-press-event",
                     G_CALLBACK(on_card_button_press), aw);
    gtk_widget_show_all(child);
    return child;
}

/* Costruisce la riga UI per una nota (vista calendario). */
static GtkWidget *build_note_row(ArchiveWindow *aw, Note *note) {
    GtkWidget *row;
    GtkWidget *box;
    GtkWidget *title_row;
    GtkWidget *title;
    GtkWidget *remove;
    GtkWidget *excerpt;
    GtkWidget *meta;
    GtkWidget *tags;
    GtkWidget *date;
    PangoAttrList *attrs;
    char *value;

    row = gtk_list_box_row_new();
    g_object_set_data(G_OBJECT(row), "note", note);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);

    title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    value = (note->has_title && note->title)
                ? g_strdup(note->title)
                : note_title(note->text);
    title = gtk_label_new(value);
    g_free(value);
    attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(title), attrs);
    pango_attr_list_unref(attrs);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_box_pack_start(GTK_BOX(title_row), title, TRUE, TRUE, 0);

    remove = gtk_button_new_from_icon_name("user-trash-symbolic",
                                           GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_valign(remove, GTK_ALIGN_CENTER);
    set_accessible_label(remove, "Delete note");
    gtk_style_context_add_class(gtk_widget_get_style_context(remove), "flat");
    g_object_set_data(G_OBJECT(remove), "note", note);
    g_signal_connect(remove, "clicked", G_CALLBACK(on_delete_clicked), aw);
    gtk_box_pack_end(GTK_BOX(title_row), remove, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), title_row, FALSE, FALSE, 0);

    value = note_excerpt(note->text);
    excerpt = gtk_label_new(value);
    g_free(value);
    gtk_label_set_xalign(GTK_LABEL(excerpt), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(excerpt), PANGO_ELLIPSIZE_END);
    gtk_label_set_line_wrap(GTK_LABEL(excerpt), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(excerpt), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_lines(GTK_LABEL(excerpt), 2);
    gtk_style_context_add_class(gtk_widget_get_style_context(excerpt),
                                "dim-label");
    gtk_box_pack_start(GTK_BOX(box), excerpt, FALSE, FALSE, 0);

    meta = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    value = note_tags(note->tags);
    tags = gtk_label_new(value);
    g_free(value);
    gtk_label_set_xalign(GTK_LABEL(tags), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(tags), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(tags, TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(tags),
                                "dim-label");
    gtk_box_pack_start(GTK_BOX(meta), tags, TRUE, TRUE, 0);

    value = note_date(note->date);
    date = gtk_label_new(value);
    g_free(value);
    gtk_label_set_xalign(GTK_LABEL(date), 1.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(date),
                                "dim-label");
    gtk_box_pack_end(GTK_BOX(meta), date, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), meta, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(row), box);
    gtk_widget_show_all(row);
    return row;
}

/* Conferma ed elimina una nota dall'archivio. */
static void confirm_delete(ArchiveWindow *aw, Note *note) {
    GtkWidget *dialog;
    GError *error = NULL;
    gint response;
    char *id;

    if (!aw || !note || !note->id) return;

    dialog = gtk_message_dialog_new(GTK_WINDOW(aw->window),
                                    GTK_DIALOG_MODAL |
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_QUESTION,
                                    GTK_BUTTONS_YES_NO,
                                    "Delete this note?");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "This action cannot be undone.");
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response != GTK_RESPONSE_YES) return;

    id = g_strdup(note->id);
    if (!store_delete(aw->store, id, &error)) {
        GtkWidget *failure = gtk_message_dialog_new(
            GTK_WINDOW(aw->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
            "Could not delete the note: %s",
            error ? error->message : "unknown error");
        gtk_dialog_run(GTK_DIALOG(failure));
        gtk_widget_destroy(failure);
        if (error) g_error_free(error);
        g_free(id);
        return;
    }
    g_free(id);
    archive_window_refresh(aw->window);
}

/* Elimina tutte le note selezionate nel flow box. */
static void delete_selected_notes(ArchiveWindow *aw) {
    GList *selected;
    GList *item;
    guint count;
    GtkWidget *dialog;
    gint response;
    GPtrArray *ids;
    GHashTable *seen;
    guint failures = 0;
    guint i;
    char *message;

    selected = gtk_flow_box_get_selected_children(GTK_FLOW_BOX(aw->flow));
    if (!selected) return;

    ids = g_ptr_array_new_with_free_func(g_free);
    seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (item = selected; item != NULL; item = item->next) {
        Note *note = g_object_get_data(G_OBJECT(item->data), "note");

        if (!note || !note->id) continue;
        if (g_hash_table_contains(seen, note->id)) continue;
        g_hash_table_add(seen, g_strdup(note->id));
        g_ptr_array_add(ids, g_strdup(note->id));
    }
    g_hash_table_destroy(seen);
    g_list_free(selected);

    count = ids->len;
    if (!count) {
        g_ptr_array_free(ids, TRUE);
        return;
    }

    message = g_strdup_printf("Delete %u selected note%s from the archive?",
                              count, count == 1 ? "" : "s");
    dialog = gtk_message_dialog_new(GTK_WINDOW(aw->window),
                                    GTK_DIALOG_MODAL |
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_QUESTION,
                                    GTK_BUTTONS_YES_NO,
                                    "%s", message);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "This action cannot be undone.");
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_free(message);
    if (response != GTK_RESPONSE_YES) {
        g_ptr_array_free(ids, TRUE);
        return;
    }

    for (i = 0; i < ids->len; ++i) {
        GError *error = NULL;
        const char *id = g_ptr_array_index(ids, i);
        if (!store_delete(aw->store, id, &error)) {
            ++failures;
            if (error) g_error_free(error);
        }
    }
    g_ptr_array_free(ids, TRUE);
    archive_window_refresh(aw->window);

    if (failures) {
        GtkWidget *failure = gtk_message_dialog_new(
            GTK_WINDOW(aw->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
            "Some notes could not be deleted.");
        gtk_dialog_run(GTK_DIALOG(failure));
        gtk_widget_destroy(failure);
    }
}

/* Elimina le note selezionate dal pulsante dell'header. */
static void on_delete_selected(GtkButton *button, gpointer data) {
    (void)button;
    delete_selected_notes(data);
}

/* Crea una nuova nota dall'archivio. */
static void on_new_note(GtkButton *button, gpointer data) {
    ArchiveWindow *aw = data;
    Note *note;

    (void)button;
    note = store_new(aw->store, "");
    if (!note) return;
    if (aw->open_cb) aw->open_cb(note, aw->open_data);
    archive_window_refresh(aw->window);
}

/* Alterna vista calendario/lista. */
static void on_calendar_clicked(GtkButton *button, gpointer data) {
    ArchiveWindow *aw = data;
    const char *visible;

    (void)button;
    visible = gtk_stack_get_visible_child_name(GTK_STACK(aw->stack));
    if (visible && g_strcmp0(visible, "calendar") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(aw->stack), "list");
        repopulate_list(aw);
    } else {
        gtk_stack_set_visible_child_name(GTK_STACK(aw->stack), "calendar");
        update_calendar(aw);
    }
}

/* Alterna albero date/tag nella sidebar. */
static void on_sidebar_toggle(GtkButton *button, gpointer data) {
    ArchiveWindow *aw = data;

    aw->sidebar_tags = !aw->sidebar_tags;
    gtk_stack_set_visible_child_name(
        GTK_STACK(aw->sidebar_stack),
        aw->sidebar_tags ? "tags" : "dates");
    set_accessible_label(GTK_WIDGET(button),
                         aw->sidebar_tags ? "Show dates" : "Show tags");
}

/* Mostra il catalogo dei tag. */
static void on_tags_clicked(GtkButton *button, gpointer data) {
    ArchiveWindow *aw = data;
    char *catalog;
    char **parts;
    GString *text;
    gboolean any = FALSE;
    guint i;
    GtkWidget *dialog;

    (void)button;
    catalog = store_tags_catalog(aw->store);
    text = g_string_new(NULL);
    if (!catalog || !*catalog) {
        g_string_append(text, "No tags yet.");
    } else {
        g_string_append(text, "Tags in this archive:\n\n");
        parts = g_strsplit(catalog, "\n", -1);
        for (i = 0; parts[i] != NULL; ++i) {
            char *name = g_strstrip(parts[i]);
            if (!*name) continue;
            if (any) g_string_append_c(text, '\n');
            g_string_append(text, "\342\200\242 ");
            g_string_append(text, name);
            any = TRUE;
        }
        g_strfreev(parts);
        if (!any) {
            g_string_truncate(text, 0);
            g_string_append(text, "No tags yet.");
        }
    }
    g_free(catalog);

    dialog = gtk_message_dialog_new(GTK_WINDOW(aw->window),
                                    GTK_DIALOG_MODAL |
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
                                    "%s", text->str);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_string_free(text, TRUE);
}

/* Ripoppola la lista dopo la ricerca. */
static void on_search_changed(GtkSearchEntry *entry, gpointer data) {
    (void)entry;
    repopulate_list(data);
}

/* Ripoppola la lista dopo il filtro tag. */
static void on_filter_changed(GtkComboBox *combo, gpointer data) {
    ArchiveWindow *aw = data;
    gint active = gtk_combo_box_get_active(combo);
    char *text = active > 0
                     ? gtk_combo_box_text_get_active_text(
                           GTK_COMBO_BOX_TEXT(combo))
                     : NULL;

    g_free(aw->tag_filter);
    aw->tag_filter = text;
    sync_tag_tree(aw);
    repopulate_list(aw);
}

/* Aggiorna la lista del giorno selezionato. */
static void on_calendar_day_selected(GtkCalendar *calendar, gpointer data) {
    (void)calendar;
    update_calendar(data);
}

/* Comparatore per chiavi numeriche. */
static gint compare_uint_keys(gconstpointer a, gconstpointer b) {
    guint left = *(const guint *)a;
    guint right = *(const guint *)b;
    return left < right ? -1 : left > right ? 1 : 0;
}

/* Comparatore per nomi di tag. */
static gint compare_tag_keys(gconstpointer a, gconstpointer b) {
    return g_utf8_collate((const char *)a, (const char *)b);
}

/* Incrementa il contatore associato a una chiave numerica. */
static void count_key(GHashTable *table, guint key) {
    guint count = GPOINTER_TO_UINT(
        g_hash_table_lookup(table, GUINT_TO_POINTER(key)));
    g_hash_table_insert(table, GUINT_TO_POINTER(key),
                        GUINT_TO_POINTER(count + 1));
}

/* Estrae le chiavi numeriche ordinate da una hash table. */
static GArray *sorted_table_keys(GHashTable *table) {
    GArray *keys = g_array_new(FALSE, FALSE, sizeof(guint));
    GList *items = g_hash_table_get_keys(table);
    GList *item;

    for (item = items; item != NULL; item = item->next) {
        guint key = GPOINTER_TO_UINT(item->data);
        g_array_append_val(keys, key);
    }
    g_list_free(items);
    g_array_sort(keys, compare_uint_keys);
    return keys;
}

/* Cerca ricorsivamente un nodo data per tipo e valore. */
static gboolean find_date_node(GtkTreeModel *model, GtkTreeIter *parent,
                               gint kind, guint32 value, GtkTreeIter *out) {
    GtkTreeIter iter;
    gboolean has;

    has = parent ? gtk_tree_model_iter_children(model, &iter, parent)
                 : gtk_tree_model_get_iter_first(model, &iter);
    while (has) {
        gint node_kind = 0;
        guint node_value = 0;

        gtk_tree_model_get(model, &iter,
                           DATE_COL_KIND, &node_kind,
                           DATE_COL_VALUE, &node_value, -1);
        if (node_kind == kind && node_value == value) {
            *out = iter;
            return TRUE;
        }
        if (find_date_node(model, &iter, kind, value, out)) return TRUE;
        has = gtk_tree_model_iter_next(model, &iter);
    }
    return FALSE;
}

/* Seleziona il nodo data corrente o la radice. */
static void select_date_node(ArchiveWindow *aw) {
    GtkTreeSelection *selection;
    GtkTreeIter iter;

    if (!aw->date_tree || !aw->date_model) return;
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(aw->date_tree));
    if (!find_date_node(GTK_TREE_MODEL(aw->date_model), NULL,
                        aw->date_kind, aw->date_filter, &iter)) {
        aw->date_filter = 0;
        aw->date_kind = 0;
        if (!find_date_node(GTK_TREE_MODEL(aw->date_model), NULL, 0, 0, &iter))
            return;
    }
    gtk_tree_selection_select_iter(selection, &iter);
}

/* Seleziona il nodo tag corrente o la radice. */
static void select_tag_node(ArchiveWindow *aw) {
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    GtkTreeIter root;

    if (!aw->tag_tree || !aw->tag_model) return;
    model = GTK_TREE_MODEL(aw->tag_model);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(aw->tag_tree));
    if (!gtk_tree_model_get_iter_first(model, &root)) return;
    if (aw->tag_filter && *aw->tag_filter) {
        GtkTreeIter child;
        gboolean has = gtk_tree_model_iter_children(model, &child, &root);

        while (has) {
            char *tag = NULL;
            gtk_tree_model_get(model, &child, TAG_COL_VALUE, &tag, -1);
            if (g_strcmp0(tag, aw->tag_filter) == 0) {
                g_free(tag);
                gtk_tree_selection_select_iter(selection, &child);
                return;
            }
            g_free(tag);
            has = gtk_tree_model_iter_next(model, &child);
        }
    }
    gtk_tree_selection_select_iter(selection, &root);
}

/* Mantiene sincronizzato l'albero dei tag con il filtro corrente. */
static void sync_tag_tree(ArchiveWindow *aw) {
    aw->updating_sidebar = TRUE;
    select_tag_node(aw);
    aw->updating_sidebar = FALSE;
}

/* Costruisce l'albero date con conteggi. */
static void populate_date_tree(ArchiveWindow *aw) {
    GtkTreeStore *model;
    GtkTreeIter root;
    GHashTable *years;
    GHashTable *months;
    GHashTable *days;
    GArray *year_keys;
    GArray *month_keys;
    GArray *day_keys;
    guint total = 0;
    guint i;
    guint j;
    guint k;

    if (!aw->date_model) return;
    model = aw->date_model;

    aw->updating_sidebar = TRUE;
    gtk_tree_store_clear(model);

    years = g_hash_table_new(g_direct_hash, g_direct_equal);
    months = g_hash_table_new(g_direct_hash, g_direct_equal);
    days = g_hash_table_new(g_direct_hash, g_direct_equal);

    if (aw->store && aw->store->notes) {
        total = aw->store->notes->len;
        for (i = 0; i < aw->store->notes->len; ++i) {
            Note *note = g_ptr_array_index(aw->store->notes, i);

            if (!note || !note->date || !date_valid(note->date)) continue;
            count_key(years, note->date / 10000u);
            count_key(months, note->date / 100u);
            count_key(days, note->date);
        }
    }

    gtk_tree_store_append(model, &root, NULL);
    {
        char *label = g_strdup_printf("All dates (%u)", total);
        gtk_tree_store_set(model, &root,
                           DATE_COL_LABEL, label,
                           DATE_COL_VALUE, 0u,
                           DATE_COL_KIND, 0, -1);
        g_free(label);
    }

    year_keys = sorted_table_keys(years);
    month_keys = sorted_table_keys(months);
    day_keys = sorted_table_keys(days);

    for (i = 0; i < year_keys->len; ++i) {
        guint year = g_array_index(year_keys, guint, i);
        guint count = GPOINTER_TO_UINT(
            g_hash_table_lookup(years, GUINT_TO_POINTER(year)));
        GtkTreeIter year_iter;
        char *label = g_strdup_printf("%04u (%u)", year, count);

        gtk_tree_store_append(model, &year_iter, &root);
        gtk_tree_store_set(model, &year_iter,
                           DATE_COL_LABEL, label,
                           DATE_COL_VALUE, year * 10000u,
                           DATE_COL_KIND, 1, -1);
        g_free(label);

        for (j = 0; j < month_keys->len; ++j) {
            guint month = g_array_index(month_keys, guint, j);
            GtkTreeIter month_iter;

            if (month / 100u != year) continue;
            count = GPOINTER_TO_UINT(
                g_hash_table_lookup(months, GUINT_TO_POINTER(month)));
            label = g_strdup_printf("%02u (%u)", month % 100u, count);
            gtk_tree_store_append(model, &month_iter, &year_iter);
            gtk_tree_store_set(model, &month_iter,
                               DATE_COL_LABEL, label,
                               DATE_COL_VALUE, month * 100u,
                               DATE_COL_KIND, 2, -1);
            g_free(label);

            for (k = 0; k < day_keys->len; ++k) {
                guint day = g_array_index(day_keys, guint, k);
                GtkTreeIter day_iter;
                char *date_text;

                if (day / 100u != month) continue;
                count = GPOINTER_TO_UINT(
                    g_hash_table_lookup(days, GUINT_TO_POINTER(day)));
                date_text = date_format(day);
                label = g_strdup_printf("%s (%u)",
                                        date_text ? date_text : "",
                                        count);
                g_free(date_text);
                gtk_tree_store_append(model, &day_iter, &month_iter);
                gtk_tree_store_set(model, &day_iter,
                                   DATE_COL_LABEL, label,
                                   DATE_COL_VALUE, day,
                                   DATE_COL_KIND, 3, -1);
                g_free(label);
            }
        }
    }

    g_array_free(year_keys, TRUE);
    g_array_free(month_keys, TRUE);
    g_array_free(day_keys, TRUE);
    g_hash_table_destroy(years);
    g_hash_table_destroy(months);
    g_hash_table_destroy(days);

    select_date_node(aw);
    aw->updating_sidebar = FALSE;
}

/* Costruisce l'albero dei tag con conteggi. */
static void populate_tag_tree(ArchiveWindow *aw) {
    GtkTreeStore *model;
    GHashTable *counts;
    GtkTreeIter root;
    GList *keys;
    GList *item;
    guint i;

    if (!aw->tag_model) return;
    model = aw->tag_model;

    aw->updating_sidebar = TRUE;
    gtk_tree_store_clear(model);

    counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (aw->store && aw->store->notes) {
        for (i = 0; i < aw->store->notes->len; ++i) {
            Note *note = g_ptr_array_index(aw->store->notes, i);
            GPtrArray *tags;
            guint j;

            if (!note) continue;
            tags = tags_split(note->tags);
            for (j = 0; j < tags->len; ++j) {
                const char *name = g_ptr_array_index(tags, j);
                guint count = GPOINTER_TO_UINT(
                    g_hash_table_lookup(counts, name));
                g_hash_table_insert(counts, g_strdup(name),
                                    GUINT_TO_POINTER(count + 1));
            }
            g_ptr_array_free(tags, TRUE);
        }
    }

    gtk_tree_store_append(model, &root, NULL);
    {
        char *label = g_strdup_printf("All tags (%u)",
                                      g_hash_table_size(counts));
        gtk_tree_store_set(model, &root,
                           TAG_COL_LABEL, label,
                           TAG_COL_VALUE, "",
                           TAG_COL_COUNT, 0, -1);
        g_free(label);
    }

    keys = g_hash_table_get_keys(counts);
    keys = g_list_sort(keys, compare_tag_keys);
    for (item = keys; item != NULL; item = item->next) {
        const char *name = item->data;
        guint count = GPOINTER_TO_UINT(g_hash_table_lookup(counts, name));
        GtkTreeIter child;
        char *label = g_strdup_printf("%s (%u)", name, count);

        gtk_tree_store_append(model, &child, &root);
        gtk_tree_store_set(model, &child,
                           TAG_COL_LABEL, label,
                           TAG_COL_VALUE, name,
                           TAG_COL_COUNT, (gint)count, -1);
        g_free(label);
    }
    g_list_free(keys);
    g_hash_table_destroy(counts);

    select_tag_node(aw);
    aw->updating_sidebar = FALSE;
}

/* Ricostruisce entrambi gli alberi della sidebar. */
static void populate_sidebar(ArchiveWindow *aw) {
    populate_date_tree(aw);
    populate_tag_tree(aw);
}

/* Popola il combo dei tag. */
static void populate_tag_combo(ArchiveWindow *aw) {
    GtkComboBox *combo = GTK_COMBO_BOX(aw->tag_combo);
    char *catalog;
    char **parts;
    gint active = 0;
    gint index = 1;
    gboolean found = FALSE;
    guint i;

    g_signal_handlers_block_by_func(combo, G_CALLBACK(on_filter_changed), aw);
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(combo));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "All tags");

    catalog = store_tags_catalog(aw->store);
    if (catalog && *catalog) {
        parts = g_strsplit(catalog, "\n", -1);
        for (i = 0; parts[i] != NULL; ++i) {
            char *name = g_strstrip(parts[i]);
            if (!*name) continue;
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), name);
            if (aw->tag_filter && g_strcmp0(aw->tag_filter, name) == 0) {
                active = index;
                found = TRUE;
            }
            ++index;
        }
        g_strfreev(parts);
    }
    g_free(catalog);

    if (aw->tag_filter && !found) {
        g_free(aw->tag_filter);
        aw->tag_filter = NULL;
    }
    gtk_combo_box_set_active(combo, active);
    g_signal_handlers_unblock_by_func(combo, G_CALLBACK(on_filter_changed),
                                      aw);
}

/* Salva la posizione di scorrimento corrente del flow box. */
static void capture_flow_scroll(ArchiveWindow *aw) {
    GtkAdjustment *hadj;
    GtkAdjustment *vadj;

    if (!aw || !aw->flow_scroll) return;
    hadj = gtk_scrolled_window_get_hadjustment(
        GTK_SCROLLED_WINDOW(aw->flow_scroll));
    vadj = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(aw->flow_scroll));
    if (hadj) aw->flow_scroll_x = gtk_adjustment_get_value(hadj);
    if (vadj) aw->flow_scroll_y = gtk_adjustment_get_value(vadj);
}

/* Ripristina lo scroll del flow box quando il layout è aggiornato. */
static gboolean restore_flow_scroll(gpointer data) {
    ArchiveWindow *aw = data;
    GtkAdjustment *hadj;
    GtkAdjustment *vadj;

    aw->flow_scroll_restore_id = 0;
    if (!aw->flow_scroll) return G_SOURCE_REMOVE;
    hadj = gtk_scrolled_window_get_hadjustment(
        GTK_SCROLLED_WINDOW(aw->flow_scroll));
    vadj = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(aw->flow_scroll));
    if (hadj) gtk_adjustment_set_value(hadj, aw->flow_scroll_x);
    if (vadj) gtk_adjustment_set_value(vadj, aw->flow_scroll_y);
    return G_SOURCE_REMOVE;
}

/* Programma il ripristino dello scroll al prossimo idle. */
static void restore_flow_scroll_later(ArchiveWindow *aw) {
    if (!aw) return;
    if (aw->flow_scroll_restore_id)
        g_source_remove(aw->flow_scroll_restore_id);
    aw->flow_scroll_restore_id =
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, restore_flow_scroll, aw, NULL);
}

/* Ricarica la lista con filtri e ricerca. */
static void repopulate_list(ArchiveWindow *aw) {
    const char *text;
    const char *query;
    GError *error = NULL;
    GPtrArray *results;
    GString *subtitle;
    guint total = 0;
    guint shown = 0;
    guint i;

    if (!aw || !aw->flow) return;

    clear_flowbox(aw->flow);
    aw->first_note = NULL;

    text = gtk_entry_get_text(GTK_ENTRY(aw->search));
    query = (text && *text) ? text : NULL;

    results = store_search(aw->store, query, aw->tag_filter, &error);
    if (results) {
        for (i = 0; i < results->len; ++i) {
            Note *note = g_ptr_array_index(results, i);

            if (!note) continue;
            if (!note_matches_date(aw, note->date)) continue;
            ++total;
            if (shown < ARCHIVE_RESULT_LIMIT) {
                gtk_flow_box_insert(GTK_FLOW_BOX(aw->flow),
                                    build_note_card(aw, note, query), -1);
                if (!aw->first_note) aw->first_note = note;
                ++shown;
            }
        }
        g_ptr_array_free(results, FALSE);
    }

    subtitle = g_string_new(NULL);
    if (error) {
        g_string_append(subtitle, "Search failed");
        g_error_free(error);
    } else {
        g_string_append_printf(subtitle, "Showing %u of %u note%s",
                               shown, total, total == 1 ? "" : "s");
    }
    if (aw->tag_filter && *aw->tag_filter)
        g_string_append_printf(subtitle, " \302\267 tag: %s", aw->tag_filter);
    if (query)
        g_string_append_printf(subtitle, " \302\267 \"%s\"", query);
    if (aw->date_filter && aw->date_kind > 0) {
        if (aw->date_kind == 1)
            g_string_append_printf(subtitle, " \302\267 year: %04u",
                                   aw->date_filter / 10000u);
        else if (aw->date_kind == 2)
            g_string_append_printf(subtitle, " \302\267 month: %04u-%02u",
                                   aw->date_filter / 10000u,
                                   aw->date_filter / 100u % 100u);
        else {
            char *formatted = date_format(aw->date_filter);
            g_string_append_printf(subtitle, " \302\267 date: %s",
                                   formatted ? formatted : "");
            g_free(formatted);
        }
    }
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(aw->header), subtitle->str);
    g_string_free(subtitle, TRUE);
}

/* Marca sul calendario ogni giorno distinto che contiene almeno una nota. */
static void refresh_calendar_marks(ArchiveWindow *aw) {
    GHashTable *seen;
    guint i;

    if (!aw || !aw->calendar) return;
    gtk_calendar_clear_marks(GTK_CALENDAR(aw->calendar));
    if (!aw->store || !aw->store->notes) return;

    seen = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (i = 0; i < aw->store->notes->len; ++i) {
        Note *note = g_ptr_array_index(aw->store->notes, i);
        guint day;

        if (!note || !note->date || !date_valid(note->date)) continue;
        day = note->date % 100u;
        if (!day || g_hash_table_contains(seen, GUINT_TO_POINTER(day)))
            continue;
        g_hash_table_add(seen, GUINT_TO_POINTER(day));
        gtk_calendar_mark_day(GTK_CALENDAR(aw->calendar), day);
    }
    g_hash_table_destroy(seen);
}

/* Aggiorna il calendario e le note del giorno. */
static void update_calendar(ArchiveWindow *aw) {
    guint year = 0;
    guint month = 0;
    guint day = 0;
    guint32 selected;
    guint total = 0;
    guint shown = 0;
    guint i;
    char date_text[16];
    char *label;
    const char *visible;

    if (!aw || !aw->calendar || !aw->calendar_list) return;

    refresh_calendar_marks(aw);
    gtk_calendar_get_date(GTK_CALENDAR(aw->calendar), &year, &month, &day);
    if (day == 0) {
        clear_listbox(aw->calendar_list);
        gtk_label_set_text(GTK_LABEL(aw->calendar_label), "No day selected");
        visible = gtk_stack_get_visible_child_name(GTK_STACK(aw->stack));
        if (visible && g_strcmp0(visible, "calendar") == 0)
            gtk_header_bar_set_subtitle(GTK_HEADER_BAR(aw->header),
                                        "No day selected");
        return;
    }
    selected = (guint32)(year * 10000u + (month + 1u) * 100u + day);

    clear_listbox(aw->calendar_list);
    if (aw->store && aw->store->notes) {
        for (i = 0; i < aw->store->notes->len; ++i) {
            Note *note = g_ptr_array_index(aw->store->notes, i);

            if (!note || note->date != selected) continue;
            ++total;
            if (shown < ARCHIVE_RESULT_LIMIT) {
                gtk_list_box_insert(GTK_LIST_BOX(aw->calendar_list),
                                    build_note_row(aw, note), -1);
                ++shown;
            }
        }
    }

    g_snprintf(date_text, sizeof(date_text), "%04u-%02u-%02u",
               year, month + 1u, day);
    label = g_strdup_printf("%u note%s on %s", total,
                            total == 1 ? "" : "s", date_text);
    gtk_label_set_text(GTK_LABEL(aw->calendar_label), label);

    visible = gtk_stack_get_visible_child_name(GTK_STACK(aw->stack));
    if (visible && g_strcmp0(visible, "calendar") == 0)
        gtk_header_bar_set_subtitle(GTK_HEADER_BAR(aw->header), label);
    g_free(label);
}

/* Apre la prima nota dei risultati. */
static void open_first_note(ArchiveWindow *aw) {
    if (aw->first_note && aw->open_cb)
        aw->open_cb(aw->first_note, aw->open_data);
}

/* Porta il focus sul flow box e seleziona la prima card. */
static void focus_flowbox(ArchiveWindow *aw) {
    GList *children;

    if (!aw->flow) return;
    gtk_widget_grab_focus(aw->flow);
    children = gtk_container_get_children(GTK_CONTAINER(aw->flow));
    if (children) {
        gtk_flow_box_select_child(GTK_FLOW_BOX(aw->flow),
                                  GTK_FLOW_BOX_CHILD(children->data));
        g_list_free(children);
    }
}

/* Pulisce ricerca, filtro data e selezione calendario. */
static void clear_search_filters(ArchiveWindow *aw) {
    if (!aw) return;
    aw->date_filter = 0;
    aw->date_kind = 0;
    aw->updating_sidebar = TRUE;
    select_date_node(aw);
    aw->updating_sidebar = FALSE;

    g_signal_handlers_block_by_func(aw->search,
                                    G_CALLBACK(on_search_changed), aw);
    gtk_entry_set_text(GTK_ENTRY(aw->search), "");
    g_signal_handlers_unblock_by_func(aw->search,
                                      G_CALLBACK(on_search_changed), aw);
    gtk_calendar_clear_marks(GTK_CALENDAR(aw->calendar));
    gtk_calendar_select_day(GTK_CALENDAR(aw->calendar), 0);
    repopulate_list(aw);
    update_calendar(aw);
}

/* Gestisce i tasti nella casella di ricerca. */
static gboolean on_search_key_press(GtkWidget *widget, GdkEventKey *event,
                                    gpointer data) {
    ArchiveWindow *aw = data;

    (void)widget;
    if (event->keyval == GDK_KEY_Down) {
        focus_flowbox(aw);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        open_first_note(aw);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Escape) {
        clear_search_filters(aw);
        return TRUE;
    }
    return FALSE;
}

/* Gestisce le scorciatoie della finestra archivio. */
static gboolean on_window_key_press(GtkWidget *widget, GdkEventKey *event,
                                    gpointer data) {
    ArchiveWindow *aw = data;
    gboolean ctrl = (event->state & GDK_CONTROL_MASK) != 0;

    (void)widget;
    if (ctrl && (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F)) {
        archive_window_focus_search(aw->window);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Escape) {
        clear_search_filters(aw);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_F5) {
        archive_window_refresh(aw->window);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Delete &&
        !gtk_widget_has_focus(aw->search)) {
        GList *selected =
            gtk_flow_box_get_selected_children(GTK_FLOW_BOX(aw->flow));

        if (selected) {
            g_list_free(selected);
            delete_selected_notes(aw);
            return TRUE;
        }
    }
    if ((event->keyval == GDK_KEY_Return ||
         event->keyval == GDK_KEY_KP_Enter) &&
        gtk_widget_has_focus(aw->search)) {
        open_first_note(aw);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Down && gtk_widget_has_focus(aw->search)) {
        focus_flowbox(aw);
        return TRUE;
    }
    return FALSE;
}

/* Cambia la cartella archivio tramite selettore. */
static void on_open_folder(GtkButton *button, gpointer data) {
    ArchiveWindow *aw = data;
    GtkWidget *dialog;

    (void)button;
    dialog = gtk_file_chooser_dialog_new(
        "Open archive folder", GTK_WINDOW(aw->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT, NULL);
    if (aw->store && aw->store->archive_dir)
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
                                            aw->store->archive_dir);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (path && aw->change_cb) aw->change_cb(path, aw->change_data);
        g_free(path);
    }
    gtk_widget_destroy(dialog);
}

/* Abilita o disabilita l'avvio automatico da menu. */
static void on_autostart_toggled(GtkCheckMenuItem *item, gpointer data) {
    (void)data;
    platform_autostart_set(gtk_check_menu_item_get_active(item));
}

/* Apre la cartella archivio corrente nel file manager. */
static void on_open_archive_folder(GtkMenuItem *item, gpointer data) {
    ArchiveWindow *aw = data;
    char *uri;
    GError *error = NULL;

    (void)item;
    if (!aw->store || !aw->store->archive_dir) return;
    uri = g_filename_to_uri(aw->store->archive_dir, NULL, NULL);
    if (!uri) return;
    if (!g_app_info_launch_default_for_uri(uri, NULL, &error)) {
        g_warning("Impossibile aprire la cartella archivio: %s",
                  error ? error->message : "errore sconosciuto");
        if (error) g_error_free(error);
    }
    g_free(uri);
}

/* Applica il filtro selezionato nell'albero delle date. */
static void on_date_tree_selection_changed(GtkTreeSelection *selection,
                                           gpointer data) {
    ArchiveWindow *aw = data;
    GtkTreeModel *model;
    GtkTreeIter iter;
    guint value = 0;
    gint kind = 0;

    if (aw->updating_sidebar) return;
    if (gtk_tree_selection_get_selected(selection, &model, &iter))
        gtk_tree_model_get(model, &iter,
                           DATE_COL_VALUE, &value,
                           DATE_COL_KIND, &kind, -1);
    aw->date_filter = value;
    aw->date_kind = kind;
    repopulate_list(aw);
}

/* Applica il filtro selezionato nell'albero dei tag. */
static void on_tag_tree_selection_changed(GtkTreeSelection *selection,
                                          gpointer data) {
    ArchiveWindow *aw = data;
    GtkTreeModel *model;
    GtkTreeIter iter;
    char *tag = NULL;

    if (aw->updating_sidebar) return;
    if (gtk_tree_selection_get_selected(selection, &model, &iter))
        gtk_tree_model_get(model, &iter, TAG_COL_VALUE, &tag, -1);
    g_free(aw->tag_filter);
    aw->tag_filter = (tag && *tag) ? g_strdup(tag) : NULL;
    g_free(tag);
    populate_tag_combo(aw);
    repopulate_list(aw);
}

/* Libera lo stato della finestra. */
static void archive_state_free(gpointer data) {
    ArchiveWindow *aw = data;

    if (!aw) return;
    if (aw->flow_scroll_restore_id)
        g_source_remove(aw->flow_scroll_restore_id);
    g_free(aw->tag_filter);
    g_free(aw);
}

/* Crea e inizializza la finestra archivio. */
GtkWidget *archive_window_new(Store *store) {
    ArchiveWindow *aw;
    GtkWidget *window;
    GtkWidget *header;
    GtkWidget *new_note;
    GtkWidget *calendar_button;
    GtkWidget *sidebar_button;
    GtkWidget *delete_button;
    GtkWidget *open_folder;
    GtkWidget *tags_button;
    GtkWidget *refresh_button;
    GtkWidget *menu_button;
    GtkWidget *menu;
    GtkWidget *autostart_item;
    GtkWidget *open_archive_item;
    GtkWidget *refresh_item;
    GtkWidget *content;
    GtkWidget *paned;
    GtkWidget *right;
    GtkWidget *top;
    GtkWidget *scroll;
    GtkWidget *calendar_page;
    GtkWidget *calendar_scroll;
    GtkWidget *date_scroll;
    GtkWidget *tag_scroll;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Jotilla \342\200\224 Archive");
    gtk_window_set_default_size(GTK_WINDOW(window), 980, 640);

    aw = g_new0(ArchiveWindow, 1);
    aw->window = window;
    aw->store = store;
    aw->sidebar_tags = FALSE;

    header = gtk_header_bar_new();
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Archive");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header), "");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    aw->header = header;

    new_note = gtk_button_new_with_label("New note");
    gtk_widget_set_tooltip_text(new_note, "Create a new note");
    g_signal_connect(new_note, "clicked", G_CALLBACK(on_new_note), aw);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), new_note);

    calendar_button = gtk_button_new_with_label("Calendar");
    set_accessible_label(calendar_button, "Toggle the calendar view");
    g_signal_connect(calendar_button, "clicked",
                     G_CALLBACK(on_calendar_clicked), aw);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), calendar_button);

    sidebar_button = gtk_button_new_with_label("Dates/Tags");
    set_accessible_label(sidebar_button, "Show tags");
    g_signal_connect(sidebar_button, "clicked",
                     G_CALLBACK(on_sidebar_toggle), aw);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), sidebar_button);

    delete_button = gtk_button_new_with_label("Delete selected");
    set_accessible_label(delete_button,
                         "Delete the selected notes from the archive");
    g_signal_connect(delete_button, "clicked",
                     G_CALLBACK(on_delete_selected), aw);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), delete_button);

    open_folder = gtk_button_new_with_label("Open folder\342\200\246");
    set_accessible_label(open_folder, "Switch archive folder");
    g_signal_connect(open_folder, "clicked",
                     G_CALLBACK(on_open_folder), aw);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), open_folder);

    tags_button = gtk_button_new_with_label("Tags");
    set_accessible_label(tags_button, "Show all tags");
    g_signal_connect(tags_button, "clicked", G_CALLBACK(on_tags_clicked), aw);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), tags_button);

    refresh_button = gtk_button_new_with_label("Refresh");
    set_accessible_label(refresh_button, "Reload the archive");
    g_signal_connect_swapped(refresh_button, "clicked",
                             G_CALLBACK(archive_window_refresh), window);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), refresh_button);

    menu = gtk_menu_new();

    autostart_item = gtk_check_menu_item_new_with_label("Start at login");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(autostart_item),
                                   platform_autostart_enabled());
    g_signal_connect(autostart_item, "toggled",
                     G_CALLBACK(on_autostart_toggled), aw);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), autostart_item);

    open_archive_item = gtk_menu_item_new_with_label("Open archive folder");
    g_signal_connect(open_archive_item, "activate",
                     G_CALLBACK(on_open_archive_folder), aw);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), open_archive_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                          gtk_separator_menu_item_new());

    refresh_item = gtk_menu_item_new_with_label("Refresh");
    g_signal_connect_swapped(refresh_item, "activate",
                             G_CALLBACK(archive_window_refresh), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), refresh_item);

    gtk_widget_show_all(menu);

    menu_button = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(menu_button), "\342\213\256");
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(menu_button), menu);
    set_accessible_label(menu_button, "Archive settings");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_button);

    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), content);

    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(paned, TRUE);

    aw->date_model = gtk_tree_store_new(DATE_COL_COUNT,
                                        G_TYPE_STRING, G_TYPE_UINT,
                                        G_TYPE_INT, G_TYPE_UINT);
    aw->date_tree = gtk_tree_view_new_with_model(
        GTK_TREE_MODEL(aw->date_model));
    g_object_unref(aw->date_model);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(aw->date_tree), FALSE);
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(
        "Dates", renderer, "text", DATE_COL_LABEL, NULL);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(aw->date_tree), column);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(aw->date_tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection, "changed",
                     G_CALLBACK(on_date_tree_selection_changed), aw);

    date_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(date_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(date_scroll), aw->date_tree);

    aw->tag_model = gtk_tree_store_new(TAG_COL_N_COLUMNS,
                                       G_TYPE_STRING, G_TYPE_STRING,
                                       G_TYPE_INT);
    aw->tag_tree = gtk_tree_view_new_with_model(
        GTK_TREE_MODEL(aw->tag_model));
    g_object_unref(aw->tag_model);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(aw->tag_tree), FALSE);
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(
        "Tags", renderer, "text", TAG_COL_LABEL, NULL);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(aw->tag_tree), column);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(aw->tag_tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection, "changed",
                     G_CALLBACK(on_tag_tree_selection_changed), aw);

    tag_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tag_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(tag_scroll), aw->tag_tree);

    aw->sidebar_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(aw->sidebar_stack),
                                  GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_stack_add_named(GTK_STACK(aw->sidebar_stack), date_scroll, "dates");
    gtk_stack_add_named(GTK_STACK(aw->sidebar_stack), tag_scroll, "tags");
    gtk_stack_set_visible_child_name(GTK_STACK(aw->sidebar_stack), "dates");
    gtk_widget_set_size_request(aw->sidebar_stack, 210, -1);

    right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(top, 8);
    gtk_widget_set_margin_end(top, 8);
    gtk_widget_set_margin_top(top, 8);
    gtk_widget_set_margin_bottom(top, 8);

    aw->search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(aw->search),
                                   "Search notes\342\200\246");
    gtk_widget_set_hexpand(aw->search, TRUE);
    gtk_widget_set_tooltip_text(aw->search, "Search text and tags");
    gtk_box_pack_start(GTK_BOX(top), aw->search, TRUE, TRUE, 0);

    aw->tag_combo = gtk_combo_box_text_new();
    gtk_widget_set_tooltip_text(aw->tag_combo, "Filter by tag");
    gtk_box_pack_start(GTK_BOX(top), aw->tag_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), top, FALSE, FALSE, 0);

    aw->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(aw->stack),
                                  GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_widget_set_vexpand(aw->stack, TRUE);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    aw->flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(aw->flow),
                                    GTK_SELECTION_MULTIPLE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(aw->flow), TRUE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(aw->flow), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(aw->flow), 8);
    gtk_widget_set_margin_start(aw->flow, 8);
    gtk_widget_set_margin_end(aw->flow, 8);
    gtk_widget_set_margin_top(aw->flow, 8);
    gtk_widget_set_margin_bottom(aw->flow, 8);
    g_signal_connect(aw->flow, "child-activated",
                     G_CALLBACK(on_flow_child_activated), aw);
    g_signal_connect(aw->flow, "selected-children-changed",
                     G_CALLBACK(on_flow_selection_changed), aw);
    g_signal_connect(aw->flow, "button-press-event",
                     G_CALLBACK(on_flow_button_press), aw);
    gtk_container_add(GTK_CONTAINER(scroll), aw->flow);
    aw->flow_scroll = scroll;
    gtk_stack_add_named(GTK_STACK(aw->stack), scroll, "list");

    calendar_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(calendar_page, 8);
    gtk_widget_set_margin_end(calendar_page, 8);
    gtk_widget_set_margin_top(calendar_page, 8);
    gtk_widget_set_margin_bottom(calendar_page, 8);

    aw->calendar = gtk_calendar_new();
    gtk_widget_set_halign(aw->calendar, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(calendar_page), aw->calendar, FALSE, FALSE, 0);

    aw->calendar_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(aw->calendar_label), 0.0f);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(aw->calendar_label), "dim-label");
    gtk_box_pack_start(GTK_BOX(calendar_page), aw->calendar_label,
                       FALSE, FALSE, 0);

    calendar_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(calendar_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(calendar_scroll, TRUE);
    aw->calendar_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(aw->calendar_list),
                                    GTK_SELECTION_SINGLE);
    g_signal_connect(aw->calendar_list, "row-activated",
                     G_CALLBACK(on_row_activated), aw);
    g_signal_connect(aw->calendar_list, "button-press-event",
                     G_CALLBACK(on_list_button_press), aw);
    gtk_container_add(GTK_CONTAINER(calendar_scroll), aw->calendar_list);
    gtk_box_pack_start(GTK_BOX(calendar_page), calendar_scroll, TRUE, TRUE, 0);
    gtk_stack_add_named(GTK_STACK(aw->stack), calendar_page, "calendar");

    gtk_box_pack_start(GTK_BOX(right), aw->stack, TRUE, TRUE, 0);
    gtk_paned_pack1(GTK_PANED(paned), aw->sidebar_stack, FALSE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), right, TRUE, FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 220);
    gtk_box_pack_start(GTK_BOX(content), paned, TRUE, TRUE, 0);

    g_object_set_data(G_OBJECT(window), ARCHIVE_STORE_KEY, store);
    g_object_set_data_full(G_OBJECT(window), ARCHIVE_STATE_KEY, aw,
                           archive_state_free);

    g_signal_connect(aw->search, "search-changed",
                     G_CALLBACK(on_search_changed), aw);
    g_signal_connect(aw->search, "key-press-event",
                     G_CALLBACK(on_search_key_press), aw);
    g_signal_connect(aw->tag_combo, "changed",
                     G_CALLBACK(on_filter_changed), aw);
    g_signal_connect(aw->calendar, "day-selected",
                     G_CALLBACK(on_calendar_day_selected), aw);
    g_signal_connect(window, "key-press-event",
                     G_CALLBACK(on_window_key_press), aw);

    populate_tag_combo(aw);
    populate_sidebar(aw);
    repopulate_list(aw);
    update_calendar(aw);
    gtk_widget_show_all(window);
    return window;
}

/* Ricarica tag, alberi, lista e calendario. */
void archive_window_refresh(GtkWidget *window) {
    ArchiveWindow *aw = get_state(window);

    if (!aw) return;
    capture_flow_scroll(aw);
    populate_tag_combo(aw);
    populate_sidebar(aw);
    repopulate_list(aw);
    update_calendar(aw);
    restore_flow_scroll_later(aw);
}

/* Imposta il callback di apertura nota. */
void archive_window_set_open_callback(GtkWidget *window, ArchiveOpenFunc cb,
                                      gpointer data) {
    ArchiveWindow *aw = get_state(window);

    if (!aw) return;
    aw->open_cb = cb;
    aw->open_data = data;
}

/* Porta il focus sulla casella di ricerca. */
void archive_window_focus_search(GtkWidget *window) {
    ArchiveWindow *aw = get_state(window);

    if (!aw || !aw->search) return;
    gtk_widget_grab_focus(aw->search);
    gtk_editable_select_region(GTK_EDITABLE(aw->search), 0, -1);
}

/* Imposta il callback di cambio cartella archivio. */
void archive_window_set_change_callback(GtkWidget *window,
                                        ArchiveChangeFunc cb, gpointer data) {
    ArchiveWindow *aw = get_state(window);

    if (!aw) return;
    aw->change_cb = cb;
    aw->change_data = data;
}

