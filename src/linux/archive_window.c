/* Finestra archivio GTK per elencare e cercare note. */
/* Gestisce lista, calendario, filtri e azioni. */
#include <gtk/gtk.h>

#include "archive_window.h"
#include "date.h"

#define ARCHIVE_STATE_KEY "archive-window-state"
#define ARCHIVE_STORE_KEY "archive-store"
#define ARCHIVE_RESULT_LIMIT 500

typedef struct {
    GtkWidget *window;
    Store *store;
    GtkWidget *header;
    GtkWidget *search;
    GtkWidget *tag_combo;
    GtkWidget *stack;
    GtkWidget *list;
    GtkWidget *calendar;
    GtkWidget *calendar_list;
    GtkWidget *calendar_label;
    ArchiveOpenFunc open_cb;
    gpointer open_data;
} ArchiveWindow;

/* Ricarica la lista con filtri e ricerca. */
static void repopulate_list(ArchiveWindow *aw);
/* Aggiorna il calendario e le note del giorno. */
static void update_calendar(ArchiveWindow *aw);
/* Conferma ed elimina una nota dall'archivio. */
static void confirm_delete(ArchiveWindow *aw, Note *note);

/* Restituisce lo stato della finestra archivio. */
static ArchiveWindow *get_state(GtkWidget *window) {
    return g_object_get_data(G_OBJECT(window), ARCHIVE_STATE_KEY);
}

/* Svuota un list box rimuovendo i figli. */
static void clear_listbox(GtkWidget *list) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(list));
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

/* Formatta i tag per la visualizzazione. */
static char *note_tags(const char *tags) {
    char **parts;
    GString *text;
    gboolean any = FALSE;
    guint i;

    if (!tags || !*tags) return g_strdup("No tags");
    parts = g_strsplit(tags, ";", -1);
    text = g_string_new("Tags: ");
    for (i = 0; parts[i] != NULL; ++i) {
        char *name = g_strstrip(parts[i]);
        if (!*name) continue;
        if (any) g_string_append(text, ", ");
        g_string_append(text, name);
        any = TRUE;
    }
    g_strfreev(parts);
    if (!any) {
        g_string_free(text, TRUE);
        return g_strdup("No tags");
    }
    return g_string_free(text, FALSE);
}

/* Formatta la data della nota. */
static char *note_date(guint32 date) {
    if (!date || !date_valid(date)) return g_strdup("No date");
    return g_strdup_printf("%04u-%02u-%02u",
                           date / 10000u, date / 100u % 100u, date % 100u);
}

/* Gestisce il clic destro sulla lista. */
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

/* Avvia l'eliminazione dalla riga. */
static void on_delete_clicked(GtkButton *button, gpointer data) {
    ArchiveWindow *aw = data;
    Note *note = g_object_get_data(G_OBJECT(button), "note");

    confirm_delete(aw, note);
}

/* Apre la nota selezionata. */
static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    ArchiveWindow *aw = data;
    Note *note = g_object_get_data(G_OBJECT(row), "note");

    (void)box;
    if (note && aw->open_cb) aw->open_cb(note, aw->open_data);
}

/* Costruisce la riga UI per una nota. */
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
    value = (note->has_title && note->title) ? g_strdup(note->title) : note_title(note->text);
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
    gtk_widget_set_tooltip_text(remove, "Delete note");
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
        parts = g_strsplit(catalog, ";", -1);
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
    (void)combo;
    repopulate_list(data);
}

/* Aggiorna la lista del giorno selezionato. */
static void on_calendar_day_selected(GtkCalendar *calendar, gpointer data) {
    (void)calendar;
    update_calendar(data);
}

/* Popola il combo dei tag. */
static void populate_tag_combo(ArchiveWindow *aw) {
    GtkComboBox *combo = GTK_COMBO_BOX(aw->tag_combo);
    char *selected;
    char *catalog;
    char **parts;
    gint active = 0;
    gint index = 1;
    guint i;

    selected = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
    g_signal_handlers_block_by_func(combo, G_CALLBACK(on_filter_changed), aw);
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(combo));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "All tags");

    catalog = store_tags_catalog(aw->store);
    if (catalog) {
        parts = g_strsplit(catalog, ";", -1);
        for (i = 0; parts[i] != NULL; ++i) {
            char *name = g_strstrip(parts[i]);
            if (!*name) continue;
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), name);
            if (selected && g_strcmp0(selected, name) == 0) active = index;
            ++index;
        }
        g_strfreev(parts);
        g_free(catalog);
    }
    gtk_combo_box_set_active(combo, active);
    g_signal_handlers_unblock_by_func(combo, G_CALLBACK(on_filter_changed), aw);
    g_free(selected);
}

/* Ricarica la lista con filtri e ricerca. */
static void repopulate_list(ArchiveWindow *aw) {
    const char *text;
    const char *query;
    char *tag = NULL;
    GError *error = NULL;
    GPtrArray *results;
    GString *subtitle;
    guint total = 0;
    guint shown = 0;
    guint i;

    if (!aw || !aw->list) return;

    clear_listbox(aw->list);
    text = gtk_entry_get_text(GTK_ENTRY(aw->search));
    query = (text && *text) ? text : NULL;
    if (gtk_combo_box_get_active(GTK_COMBO_BOX(aw->tag_combo)) > 0)
        tag = gtk_combo_box_text_get_active_text(
            GTK_COMBO_BOX_TEXT(aw->tag_combo));

    results = store_search(aw->store, query, tag, &error);
    if (results) {
        total = results->len;
        for (i = 0; i < results->len && shown < ARCHIVE_RESULT_LIMIT; ++i) {
            Note *note = g_ptr_array_index(results, i);
            if (!note) continue;
            gtk_list_box_insert(GTK_LIST_BOX(aw->list),
                                build_note_row(aw, note), -1);
            ++shown;
        }
        g_ptr_array_free(results, FALSE);
    }

    subtitle = g_string_new(NULL);
    if (error) {
        g_string_append(subtitle, "Search failed");
        g_error_free(error);
    } else if (total > ARCHIVE_RESULT_LIMIT) {
        g_string_append_printf(subtitle, "%u notes (showing %u)",
                               total, ARCHIVE_RESULT_LIMIT);
    } else {
        g_string_append_printf(subtitle, "%u note%s", total,
                               total == 1 ? "" : "s");
    }
    if (tag && *tag)
        g_string_append_printf(subtitle, " \302\267 tag: %s", tag);
    if (query)
        g_string_append_printf(subtitle, " \302\267 \"%s\"", query);
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(aw->header), subtitle->str);
    g_string_free(subtitle, TRUE);
    g_free(tag);
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

    gtk_calendar_get_date(GTK_CALENDAR(aw->calendar), &year, &month, &day);
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

/* Crea e inizializza la finestra archivio. */
GtkWidget *archive_window_new(Store *store) {
    ArchiveWindow *aw;
    GtkWidget *window;
    GtkWidget *header;
    GtkWidget *new_note;
    GtkWidget *calendar_button;
    GtkWidget *tags_button;
    GtkWidget *refresh_button;
    GtkWidget *content;
    GtkWidget *top;
    GtkWidget *scroll;
    GtkWidget *calendar_page;
    GtkWidget *calendar_scroll;

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Jotilla \342\200\224 Archive");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 600);

    aw = g_new0(ArchiveWindow, 1);
    aw->window = window;
    aw->store = store;

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
    gtk_widget_set_tooltip_text(calendar_button, "Toggle the calendar view");
    g_signal_connect(calendar_button, "clicked",
                     G_CALLBACK(on_calendar_clicked), aw);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), calendar_button);

    tags_button = gtk_button_new_with_label("Tags");
    gtk_widget_set_tooltip_text(tags_button, "Show all tags");
    g_signal_connect(tags_button, "clicked", G_CALLBACK(on_tags_clicked), aw);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), tags_button);

    refresh_button = gtk_button_new_with_label("Refresh");
    gtk_widget_set_tooltip_text(refresh_button, "Reload the archive");
    g_signal_connect_swapped(refresh_button, "clicked",
                             G_CALLBACK(archive_window_refresh), window);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), refresh_button);

    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), content);

    top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(top, 8);
    gtk_widget_set_margin_end(top, 8);
    gtk_widget_set_margin_top(top, 8);
    gtk_widget_set_margin_bottom(top, 8);

    aw->search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(aw->search), "Search notes\342\200\246");
    gtk_widget_set_hexpand(aw->search, TRUE);
    gtk_widget_set_tooltip_text(aw->search, "Search text and tags");
    gtk_box_pack_start(GTK_BOX(top), aw->search, TRUE, TRUE, 0);

    aw->tag_combo = gtk_combo_box_text_new();
    gtk_widget_set_tooltip_text(aw->tag_combo, "Filter by tag");
    gtk_box_pack_start(GTK_BOX(top), aw->tag_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), top, FALSE, FALSE, 0);

    aw->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(aw->stack),
                                  GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_widget_set_vexpand(aw->stack, TRUE);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    aw->list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(aw->list),
                                    GTK_SELECTION_SINGLE);
    g_signal_connect(aw->list, "row-activated",
                     G_CALLBACK(on_row_activated), aw);
    g_signal_connect(aw->list, "button-press-event",
                     G_CALLBACK(on_list_button_press), aw);
    gtk_container_add(GTK_CONTAINER(scroll), aw->list);
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
    gtk_style_context_add_class(gtk_widget_get_style_context(aw->calendar_label),
                                "dim-label");
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

    gtk_box_pack_start(GTK_BOX(content), aw->stack, TRUE, TRUE, 0);

    g_object_set_data(G_OBJECT(window), ARCHIVE_STORE_KEY, store);
    g_object_set_data_full(G_OBJECT(window), ARCHIVE_STATE_KEY, aw, g_free);

    g_signal_connect(aw->search, "search-changed",
                     G_CALLBACK(on_search_changed), aw);
    g_signal_connect(aw->tag_combo, "changed",
                     G_CALLBACK(on_filter_changed), aw);
    g_signal_connect(aw->calendar, "day-selected",
                     G_CALLBACK(on_calendar_day_selected), aw);

    populate_tag_combo(aw);
    repopulate_list(aw);
    update_calendar(aw);
    gtk_widget_show_all(window);
    return window;
}

/* Ricarica tag, lista e calendario. */
void archive_window_refresh(GtkWidget *window) {
    ArchiveWindow *aw = get_state(window);

    if (!aw) return;
    populate_tag_combo(aw);
    repopulate_list(aw);
    update_calendar(aw);
}

/* Imposta il callback di apertura nota. */
void archive_window_set_open_callback(GtkWidget *window, ArchiveOpenFunc cb,
                                      gpointer data) {
    ArchiveWindow *aw = get_state(window);

    if (!aw) return;
    aw->open_cb = cb;
    aw->open_data = data;
}
