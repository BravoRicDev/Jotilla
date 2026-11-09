/* Persistenza note su disco: archivio testo/RTF e metadati INI. */
/* Fornisce apertura, salvataggio, cancellazione e ricerca. */
#include "jotilla.h"
#include "tags.h"

#include <errno.h>
#include <string.h>

#include <glib/gstdio.h>

/* Libera una nota e le sue stringhe. */
static void note_free(gpointer data)
{
    Note *note = (Note *)data;

    if (!note)
        return;
    g_free(note->id);
    g_free(note->text);
    g_free(note->title);
    g_free(note->rtf);
    g_free(note->tags);
    g_free(note);
}

/* Costruisce il percorso file di una nota per estensione. */
static char *note_file_path(const Store *store, const char *id, const char *extension)
{
    char *name = g_strdup_printf("%s.%s", id, extension);
    char *path = g_build_filename(store->archive_dir, name, NULL);

    g_free(name);
    return path;
}

/* Imposta errore GIO da errno. */
static void set_file_error(GError **err, int error_code, const char *action, const char *path)
{
    if (err && !*err)
        g_set_error(err, G_FILE_ERROR, g_file_error_from_errno(error_code),
                    "%s \"%s\": %s", action, path, g_strerror(error_code));
}

/* Scrive file in modo atomico con fallback diretto. */
static gboolean write_atomic(const char *path, const char *data, gsize length, GError **err)
{
    char *temporary = g_strconcat(path, ".tmp", NULL);
    GError *local = NULL;
    gboolean written = FALSE;

    if (g_file_set_contents(temporary, data ? data : "", (gssize)length, &local)) {
        if (g_rename(temporary, path) == 0)
            written = TRUE;
        else
            (void)g_unlink(temporary);
    } else {
        (void)g_unlink(temporary);
    }
    g_clear_error(&local);
    g_free(temporary);

    /* Fallback diretto se rename atomico fallisce. */
    if (!written)
        written = g_file_set_contents(path, data ? data : "", (gssize)length, err);
    return written;
}

/* Legge file, rimuovendo il BOM UTF-8 se richiesto. */
static char *read_file(const char *path, gboolean strip_bom)
{
    char *contents = NULL;
    gsize length = 0;
    GError *error = NULL;

    if (!g_file_get_contents(path, &contents, &length, &error)) {
        g_clear_error(&error);
        return NULL;
    }
    if (strip_bom && length >= 3 &&
        (guchar)contents[0] == 0xef && (guchar)contents[1] == 0xbb &&
        (guchar)contents[2] == 0xbf) {
        char *stripped = g_strndup(contents + 3, length - 3);
        g_free(contents);
        return stripped;
    }
    return contents;
}

/* Alloca una nota con valori predefiniti. */
static Note *note_alloc(const char *id)
{
    Note *note = g_try_new0(Note, 1);

    if (!note)
        return NULL;
    note->id = g_strdup(id);
    note->text = g_strdup("");
    note->title = NULL;
    note->rtf = NULL;
    note->tags = g_strdup("");
    note->date = 0;
    note->x = 100;
    note->y = 100;
    note->w = 300;
    note->h = 240;
    note->palette = 0;
    note->color = 0;
    note->size = 1;
    note->opacity = 100;
    note->open = TRUE;
    note->collapsed = FALSE;
    note->has_title = FALSE;
    return note;
}

/* Legge intero da keyfile con fallback. */
static gint key_int(GKeyFile *keyfile, const char *group, const char *key, gint fallback)
{
    GError *error = NULL;
    gint value;

    if (!g_key_file_has_key(keyfile, group, key, NULL))
        return fallback;
    value = g_key_file_get_integer(keyfile, group, key, &error);
    if (error) {
        g_clear_error(&error);
        return fallback;
    }
    return value;
}

/* Legge booleano da keyfile con fallback. */
static gboolean key_bool(GKeyFile *keyfile, const char *group, const char *key, gboolean fallback)
{
    GError *error = NULL;
    gboolean value;

    if (!g_key_file_has_key(keyfile, group, key, NULL))
        return fallback;
    value = g_key_file_get_boolean(keyfile, group, key, &error);
    if (error) {
        g_clear_error(&error);
        return fallback;
    }
    return value;
}

/* Legge stringa da keyfile con fallback. */
static char *key_string(GKeyFile *keyfile, const char *group, const char *key, const char *fallback)
{
    GError *error = NULL;
    char *value;

    if (!g_key_file_has_key(keyfile, group, key, NULL))
        return g_strdup(fallback);
    value = g_key_file_get_string(keyfile, group, key, &error);
    if (error) {
        g_clear_error(&error);
        return g_strdup(fallback);
    }
    if (!value)
        return g_strdup(fallback);
    return value;
}

/* Confronta note per id. */
static gint note_id_compare(gconstpointer a, gconstpointer b)
{
    const Note *left = *(const Note * const *)a;
    const Note *right = *(const Note * const *)b;

    return g_strcmp0(left->id, right->id);
}

/* Confronta tag con collazione UTF-8. */
static gint tag_compare(gconstpointer a, gconstpointer b)
{
    const char *left = *(const char * const *)a;
    const char *right = *(const char * const *)b;

    return g_utf8_collate(left, right);
}

/* Salva metadati di tutte le note in notes.ini. */
static gboolean save_ini(Store *store, GError **err)
{
    GKeyFile *keyfile = g_key_file_new();
    GError *local = NULL;
    char *data;
    gsize length = 0;
    gboolean ok;
    guint i;

    for (i = 0; i < store->notes->len; ++i) {
        Note *note = g_ptr_array_index(store->notes, i);

        g_key_file_set_integer(keyfile, note->id, "x", note->x);
        g_key_file_set_integer(keyfile, note->id, "y", note->y);
        g_key_file_set_integer(keyfile, note->id, "w", note->w);
        g_key_file_set_integer(keyfile, note->id, "h", note->h);
        g_key_file_set_integer(keyfile, note->id, "color", note->color);
        g_key_file_set_integer(keyfile, note->id, "palette", note->palette);
        g_key_file_set_integer(keyfile, note->id, "size", note->size);
        g_key_file_set_integer(keyfile, note->id, "opacity", note->opacity);
        g_key_file_set_boolean(keyfile, note->id, "open", note->open);
        g_key_file_set_boolean(keyfile, note->id, "collapsed", note->collapsed);
        g_key_file_set_boolean(keyfile, note->id, "has_title", note->has_title);
        g_key_file_set_string(keyfile, note->id, "tags", note->tags ? note->tags : "");
        g_key_file_set_integer(keyfile, note->id, "calendar_date", (gint)note->date);
    }

    data = g_key_file_to_data(keyfile, &length, &local);
    if (!data) {
        if (local) {
            g_propagate_error(err, local);
            g_key_file_free(keyfile);
            return FALSE;
        }
        data = g_strdup("");
        length = 0;
    }

    ok = write_atomic(store->notes_ini, data, length, err);
    g_free(data);
    g_key_file_free(keyfile);
    return ok;
}

/* Rimuove gruppo INI di una nota. */
static gboolean remove_ini_group(Store *store, const char *id, GError **err)
{
    GKeyFile *keyfile;
    GError *local = NULL;
    char *data;
    gsize length = 0;
    gboolean ok;

    if (!g_file_test(store->notes_ini, G_FILE_TEST_EXISTS))
        return TRUE;

    keyfile = g_key_file_new();
    if (!g_key_file_load_from_file(keyfile, store->notes_ini, G_KEY_FILE_NONE, &local)) {
        g_clear_error(&local);
        g_key_file_free(keyfile);
        return save_ini(store, err);
    }

    g_key_file_remove_group(keyfile, id, NULL);
    data = g_key_file_to_data(keyfile, &length, &local);
    if (!data) {
        if (local) {
            g_propagate_error(err, local);
            g_key_file_free(keyfile);
            return FALSE;
        }
        data = g_strdup("");
        length = 0;
    }

    ok = write_atomic(store->notes_ini, data, length, err);
    g_free(data);
    g_key_file_free(keyfile);
    return ok;
}

/* Cancella file txt/rtf di una nota. */
static gboolean remove_note_files(Store *store, const char *id, GError **err)
{
    static const char *extensions[] = { "txt", "rtf" };
    gboolean ok = TRUE;
    int first_error = 0;
    guint i;

    for (i = 0; i < G_N_ELEMENTS(extensions); ++i) {
        char *path = note_file_path(store, id, extensions[i]);
        if (g_remove(path) != 0 && errno != ENOENT) {
            /* Memorizza primo errore. */
            if (ok)
                first_error = errno;
            ok = FALSE;
        }
        g_free(path);
    }

    if (!ok)
        set_file_error(err, first_error, "Failed to remove note files", store->archive_dir);
    return ok;
}

/* Percorso archivio predefinito. */
char *store_default_archive_dir(void)
{
    return g_build_filename(g_get_user_data_dir(), "jotilla", "notes", NULL);
}

/* Percorso config predefinito. */
char *store_default_config_dir(void)
{
    return g_build_filename(g_get_user_config_dir(), "jotilla", NULL);
}

/* Apre store e carica note da archivio. */
Store *store_open(const char *archive_dir, const char *config_dir, GError **err)
{
    Store *store;
    GKeyFile *keyfile;
    GError *ini_error = NULL;
    GDir *directory;
    const char *name;

    store = g_try_new0(Store, 1);
    if (!store) {
        g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_NOMEM, "Not enough memory");
        return NULL;
    }

    store->archive_dir = archive_dir ? g_strdup(archive_dir) : store_default_archive_dir();
    store->config_dir = config_dir ? g_strdup(config_dir) : store_default_config_dir();
    store->notes_ini = g_build_filename(store->config_dir, "notes.ini", NULL);
    store->notes = g_ptr_array_new_with_free_func(note_free);

    if (g_mkdir_with_parents(store->archive_dir, 0700) != 0) {
        int error_code = errno;
        set_file_error(err, error_code, "Failed to create directory", store->archive_dir);
        store_free(store);
        return NULL;
    }
    if (g_mkdir_with_parents(store->config_dir, 0700) != 0) {
        int error_code = errno;
        set_file_error(err, error_code, "Failed to create directory", store->config_dir);
        store_free(store);
        return NULL;
    }

    keyfile = g_key_file_new();
    if (g_file_test(store->notes_ini, G_FILE_TEST_IS_REGULAR) &&
        !g_key_file_load_from_file(keyfile, store->notes_ini, G_KEY_FILE_NONE, &ini_error))
        g_clear_error(&ini_error);

    directory = g_dir_open(store->archive_dir, 0, err);
    if (!directory) {
        g_key_file_free(keyfile);
        store_free(store);
        return NULL;
    }

    while ((name = g_dir_read_name(directory)) != NULL) {
        char *path;
        char *id;
        char *text;
        char *rtf_path;
        char *rtf;
        Note *note;

        if (!g_str_has_prefix(name, "note-") || !g_str_has_suffix(name, ".txt"))
            continue;

        path = g_build_filename(store->archive_dir, name, NULL);
        if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
            g_free(path);
            continue;
        }

        text = read_file(path, TRUE);
        g_free(path);
        if (!text)
            continue;

        id = g_strndup(name, strlen(name) - 4);
        note = note_alloc(id);
        if (!note) {
            g_free(id);
            g_free(text);
            g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_NOMEM, "Not enough memory");
            g_dir_close(directory);
            g_key_file_free(keyfile);
            store_free(store);
            return NULL;
        }

        g_free(note->text);
        note->text = text;

        rtf_path = note_file_path(store, id, "rtf");
        rtf = read_file(rtf_path, FALSE);
        g_free(rtf_path);
        if (rtf) {
            g_free(note->rtf);
            note->rtf = rtf;
        }

        g_free(note->tags);
        note->tags = key_string(keyfile, id, "tags", "");
        if (!note->tags)
            note->tags = g_strdup("");
        note->x = key_int(keyfile, id, "x", 100);
        note->y = key_int(keyfile, id, "y", 100);
        note->w = key_int(keyfile, id, "w", 300);
        note->h = key_int(keyfile, id, "h", 240);
        note->color = key_int(keyfile, id, "color", 0);
        note->palette = key_int(keyfile, id, "palette", 0);
        note->size = key_int(keyfile, id, "size", 1);
        note->opacity = key_int(keyfile, id, "opacity", 100);
        note->open = key_bool(keyfile, id, "open", TRUE);
        note->collapsed = key_bool(keyfile, id, "collapsed", FALSE);
        note->has_title = key_bool(keyfile, id, "has_title", FALSE);
        if (note->has_title && note->text && note->text[0] == '#') {
            char *body = note->text;
            size_t end = strcspn(body, "\r\n");
            size_t after = end;
            char *title;
            size_t start = 1, finish = end;
            /* trim spaces around the title */
            while (start < finish && (body[start] == ' ' || body[start] == '\t')) ++start;
            while (finish > start && (body[finish - 1] == ' ' || body[finish - 1] == '\t')) --finish;
            title = g_strndup(body + start, finish - start);
            if (body[after] == '\r') ++after;
            if (body[after] == '\n') ++after;
            g_free(note->title);
            note->title = title;
            {
                char *new_text = g_strdup(body + after);
                g_free(note->text);
                note->text = new_text ? new_text : g_strdup("");
            }
        } else {
            g_free(note->title);
            note->title = NULL;
            note->has_title = FALSE;
        }
        {
            gint date = key_int(keyfile, id, "calendar_date", 0);
            note->date = date > 0 ? (guint32)date : 0;
        }

        g_ptr_array_add(store->notes, note);
        g_free(id);
    }

    g_dir_close(directory);
    g_key_file_free(keyfile);
    g_ptr_array_sort(store->notes, note_id_compare);
    return store;
}

/* Libera store e note. */
void store_free(Store *store)
{
    if (!store)
        return;
    if (store->notes)
        g_ptr_array_free(store->notes, TRUE);
    g_free(store->archive_dir);
    g_free(store->config_dir);
    g_free(store->notes_ini);
    g_free(store);
}

/* Crea nuova nota con id univoco. */
Note *store_new(Store *store, const char *text)
{
    GDateTime *now;
    char *stamp;
    char *id = NULL;
    Note *note;
    guint position;
    guint attempt;

    if (!store)
        return NULL;

    now = g_date_time_new_now_local();
    if (!now)
        return NULL;
    stamp = g_date_time_format(now, "%Y-%m-%d_%H-%M-%S");
    if (!stamp) {
        g_date_time_unref(now);
        return NULL;
    }

    /* Tenta id univoco evitando collisioni. */
    for (attempt = 0; attempt < 1000; ++attempt) {
        char *candidate;
        char *text_path;
        char *rtf_path;
        gboolean taken;

        candidate = g_strdup_printf("note-%s-%03d-%03d", stamp,
                                    g_date_time_get_microsecond(now) / 1000,
                                    g_random_int_range(0, 1000));
        text_path = note_file_path(store, candidate, "txt");
        rtf_path = note_file_path(store, candidate, "rtf");
        taken = store_get(store, candidate) != NULL ||
                g_file_test(text_path, G_FILE_TEST_EXISTS) ||
                g_file_test(rtf_path, G_FILE_TEST_EXISTS);
        g_free(text_path);
        g_free(rtf_path);
        if (!taken) {
            id = candidate;
            break;
        }
        g_free(candidate);
    }

    g_free(stamp);
    g_date_time_unref(now);
    if (!id)
        return NULL;

    note = note_alloc(id);
    if (!note) {
        g_free(id);
        return NULL;
    }
    g_free(note->text);
    note->text = g_strdup(text ? text : "");

    position = store->notes->len;
    for (guint i = 0; i < store->notes->len; ++i) {
        Note *existing = g_ptr_array_index(store->notes, i);
        if (g_strcmp0(existing->id, id) > 0) {
            position = i;
            break;
        }
    }
    g_ptr_array_insert(store->notes, position, note);
    g_free(id);

    (void)store_save(store, note, NULL);
    return note;
}

/* Cerca nota per id. */
Note *store_get(Store *store, const char *id)
{
    guint i;

    if (!store || !id)
        return NULL;
    for (i = 0; i < store->notes->len; ++i) {
        Note *note = g_ptr_array_index(store->notes, i);
        if (g_strcmp0(note->id, id) == 0)
            return note;
    }
    return NULL;
}

/* Salva nota su disco e metadati. */
gboolean store_save(Store *store, Note *note, GError **err)
{
    char *path;
    gboolean ok;

    if (!store || !note || !note->id) {
        g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid note");
        return FALSE;
    }

    path = note_file_path(store, note->id, "txt");
    if (note->has_title && note->title) {
        GString *content = g_string_new("#");
        g_string_append(content, note->title);
        g_string_append(content, "\n");
        if (note->text)
            g_string_append(content, note->text);
        ok = write_atomic(path, content->str, content->len, err);
        g_string_free(content, TRUE);
    } else {
        ok = write_atomic(path, note->text ? note->text : "", note->text ? strlen(note->text) : 0, err);
    }
    g_free(path);
    if (!ok)
        return FALSE;

    if (note->rtf) {
        path = note_file_path(store, note->id, "rtf");
        ok = write_atomic(path, note->rtf, strlen(note->rtf), err);
        g_free(path);
        if (!ok)
            return FALSE;
    }

    return save_ini(store, err);
}

/* Elimina nota e suoi file. */
gboolean store_delete(Store *store, const char *id, GError **err)
{
    gssize index = -1;
    guint i;

    if (!store || !id) {
        g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid note");
        return FALSE;
    }

    for (i = 0; i < store->notes->len; ++i) {
        Note *note = g_ptr_array_index(store->notes, i);
        if (g_strcmp0(note->id, id) == 0) {
            index = (gssize)i;
            break;
        }
    }

    if (index < 0)
        return FALSE;

    if (!remove_note_files(store, id, err))
        return FALSE;

    (void)g_ptr_array_remove_index(store->notes, (guint)index);
    return remove_ini_group(store, id, err);
}

typedef struct {
    gboolean is_tag;
    gboolean untagged;
    char *value;
} SearchTerm;

/* Libera termine di ricerca. */
static void search_term_free(gpointer data)
{
    SearchTerm *term = (SearchTerm *)data;

    if (!term)
        return;
    g_free(term->value);
    g_free(term);
}

/* Analizza query in termini di ricerca. */
static gboolean query_parse(const char *query, GPtrArray *terms, GError **err)
{
    const char *cursor = query ? query : "";

    while (*cursor) {
        SearchTerm *term;
        GString *value;
        char *raw;

        while (*cursor && g_ascii_isspace(*cursor))
            ++cursor;
        if (!*cursor)
            break;

        term = g_try_new0(SearchTerm, 1);
        if (!term) {
            g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_NOMEM, "Not enough memory");
            return FALSE;
        }

        if (cursor[0] == '-' && cursor[1] == '#' &&
            (cursor[2] == '\0' || g_ascii_isspace(cursor[2]))) {
            term->untagged = TRUE;
            cursor += 2;
            g_ptr_array_add(terms, term);
            continue;
        }

        if (*cursor == '#') {
            term->is_tag = TRUE;
            ++cursor;
        }

        value = g_string_new(NULL);
        if (*cursor == '"') {
            ++cursor;
            while (*cursor && *cursor != '"') {
                g_string_append_c(value, *cursor);
                ++cursor;
            }
            if (*cursor == '"')
                ++cursor;
        } else {
            while (*cursor && !g_ascii_isspace(*cursor)) {
                g_string_append_c(value, *cursor);
                ++cursor;
            }
        }

        if (value->len == 0) {
            g_string_free(value, TRUE);
            search_term_free(term);
            g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Empty search term");
            return FALSE;
        }

        raw = g_string_free(value, FALSE);
        if (term->is_tag) {
            char *normalized = tags_normalize(raw);
            g_free(raw);
            if (!normalized || !*normalized) {
                g_free(normalized);
                search_term_free(term);
                g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid tag in search");
                return FALSE;
            }
            term->value = normalized;
        } else {
            term->value = g_utf8_casefold(raw, -1);
            g_free(raw);
            if (!term->value) {
                search_term_free(term);
                g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_NOMEM, "Not enough memory");
                return FALSE;
            }
        }

        g_ptr_array_add(terms, term);
    }

    return TRUE;
}

/* Cerca note per query/tag. */
GPtrArray *store_search(Store *store, const char *query, const char *tag, GError **err)
{
    GPtrArray *terms;
    GPtrArray *results;
    guint i;

    if (!store) {
        g_set_error_literal(err, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid store");
        return NULL;
    }

    terms = g_ptr_array_new_with_free_func(search_term_free);
    if (!query_parse(query, terms, err)) {
        g_ptr_array_free(terms, TRUE);
        return NULL;
    }

    results = g_ptr_array_new();
    for (i = 0; i < store->notes->len; ++i) {
        Note *note = g_ptr_array_index(store->notes, i);
        char *folded = NULL;
        gboolean matched = TRUE;
        guint j;

        if (tag && !tags_contains(note->tags ? note->tags : "", tag))
            continue;

        for (j = 0; j < terms->len; ++j) {
            SearchTerm *term = g_ptr_array_index(terms, j);

            if (term->untagged) {
                if (note->tags && note->tags[0]) {
                    matched = FALSE;
                    break;
                }
            } else if (term->is_tag) {
                if (!tags_contains(note->tags ? note->tags : "", term->value)) {
                    matched = FALSE;
                    break;
                }
            } else {
                /* Casefold del testo solo al primo termine testuale. */
                if (!folded)
                    folded = g_utf8_casefold(note->text ? note->text : "", -1);
                if (!folded || !strstr(folded, term->value)) {
                    matched = FALSE;
                    break;
                }
            }
        }

        g_free(folded);
        if (matched)
            g_ptr_array_add(results, note);
    }

    g_ptr_array_free(terms, TRUE);
    return results;
}

/* Catalogo tag unici ordinati. */
char *store_tags_catalog(Store *store)
{
    GPtrArray *tags;
    GHashTable *seen;
    GString *text;
    char *result;
    guint i;

    if (!store)
        return g_strdup("");

    tags = g_ptr_array_new_with_free_func(g_free);
    seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    for (i = 0; i < store->notes->len; ++i) {
        Note *note = g_ptr_array_index(store->notes, i);
        char **parts;
        guint j;

        if (!note->tags || !note->tags[0])
            continue;
        parts = g_strsplit(note->tags, ";", -1);
        for (j = 0; parts[j]; ++j) {
            if (!parts[j][0] || g_hash_table_contains(seen, parts[j]))
                continue;
            g_hash_table_add(seen, g_strdup(parts[j]));
            g_ptr_array_add(tags, g_strdup(parts[j]));
        }
        g_strfreev(parts);
    }

    g_ptr_array_sort(tags, tag_compare);

    text = g_string_new(NULL);
    for (i = 0; i < tags->len; ++i) {
        if (i)
            g_string_append_c(text, '\n');
        g_string_append(text, g_ptr_array_index(tags, i));
    }
    if (text->len)
        result = g_string_free(text, FALSE);
    else {
        g_string_free(text, TRUE);
        result = g_strdup("");
    }

    g_hash_table_destroy(seen);
    g_ptr_array_free(tags, TRUE);
    return result;
}

/* Imposta tag normalizzati e salva. */
void store_set_tags(Store *store, Note *note, const char *raw)
{
    char *normalized;

    if (!store || !note)
        return;
    normalized = tags_normalize(raw ? raw : "");
    if (!normalized)
        return;
    g_free(note->tags);
    note->tags = normalized;
    (void)store_save(store, note, NULL);
}

/* Imposta data calendario e salva. */
void store_set_date(Store *store, Note *note, guint32 date)
{
    if (!store || !note)
        return;
    note->date = date;
    (void)store_save(store, note, NULL);
}

/* Restituisce directory archivio. */
const char *store_archive_dir(Store *store)
{
    return store ? store->archive_dir : NULL;
}
