/* Suite di test per il core di Jotilla.
 * Copre date, tag, palette e persistenza dello store su disco. */
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "date.h"
#include "tags.h"
#include "palette.h"
#include "jotilla.h"

/* Rimuove ricorsivamente una directory temporanea di test. */
static void remove_tree(const char *path) {
    GDir *dir = g_dir_open(path, 0, NULL);
    if (dir) {
        const char *name;
        while ((name = g_dir_read_name(dir)) != NULL) {
            char *child = g_build_filename(path, name, NULL);
            if (g_file_test(child, G_FILE_TEST_IS_DIR))
                remove_tree(child);
            else
                g_remove(child);
            g_free(child);
        }
        g_dir_close(dir);
    }
    g_rmdir(path);
}

static void test_date(void) {
    char *formatted;

    g_assert_cmpint(date_days(2024, 2), ==, 29);
    g_assert_cmpint(date_days(2023, 2), ==, 28);

    g_assert_cmpint(date_valid(20240229), ==, 1);
    g_assert_cmpint(date_valid(20230229), ==, 0);

    g_assert_cmpint(date_ordinal(20240101), ==, 0);
    g_assert_cmpint(date_ordinal(20241231), ==, 365);

    g_assert_cmpint(date_weekday(20240101), ==, 0);

    g_assert_cmpuint(date_shift(20240131, 1), ==, 20240201u);
    g_assert_cmpuint(date_shift(20240301, -1), ==, 20240229u);

    g_assert_cmpuint(date_parse("20240115"), ==, 20240115u);
    g_assert_cmpuint(date_parse("2024-01-15"), ==, 0u);

    formatted = date_format(20240115);
    g_assert_cmpstr(formatted, ==, "2024-01-15");
    g_free(formatted);

    formatted = date_format(0);
    g_assert_cmpstr(formatted, ==, "");
    g_free(formatted);
}

static void test_tags(void) {
    char *normalized;
    GPtrArray *parts;

    normalized = tags_normalize(" Work ; #work ; urgent ");
    g_assert_cmpstr(normalized, ==, "urgent;work");
    g_free(normalized);

    normalized = tags_normalize("");
    g_assert_cmpstr(normalized, ==, "");
    g_free(normalized);

    g_assert_true(tags_contains("a;b", "A"));
    g_assert_false(tags_contains("a;b", "c"));

    parts = tags_split("a;b");
    g_assert_nonnull(parts);
    g_assert_cmpuint(parts->len, ==, 2);
    g_ptr_array_free(parts, TRUE);
}

static void test_palette(void) {
    const char *name = palette_name(0);
    const char *hex = palette_color_hex(0, 0);
    int def;

    g_assert_nonnull(name);

    g_assert_nonnull(hex);
    g_assert_cmpuint(strlen(hex), ==, 7);
    g_assert_cmpint(hex[0], ==, '#');

    g_assert_cmpstr(palette_ink_hex(1, 1), ==, "#ffffff");

    def = palette_default_for_note(7);
    g_assert_cmpint(def, >=, 0);
    g_assert_cmpint(def, <=, 5);
}

static void test_store(void) {
    GError *err = NULL;
    char *archive_dir;
    char *config_dir;
    char *id1;
    char *id2;
    Store *store;
    Store *reopened;
    Store *final_store;
    Note *note1;
    Note *note2;
    Note *found;
    GPtrArray *results;

    archive_dir = g_dir_make_tmp("jotilla-test-arc-XXXXXX", &err);
    g_assert_no_error(err);
    g_assert_nonnull(archive_dir);

    config_dir = g_dir_make_tmp("jotilla-test-cfg-XXXXXX", &err);
    g_assert_no_error(err);
    g_assert_nonnull(config_dir);

    store = store_open(archive_dir, config_dir, &err);
    g_assert_no_error(err);
    g_assert_nonnull(store);

    note1 = store_new(store, "hello world");
    g_assert_nonnull(note1);
    g_assert_nonnull(note1->id);
    id1 = g_strdup(note1->id);
    g_assert_nonnull(id1);
    g_assert_true(store_save(store, note1, &err));
    g_assert_no_error(err);

    note2 = store_new(store, "urgent task");
    g_assert_nonnull(note2);
    g_assert_nonnull(note2->id);
    id2 = g_strdup(note2->id);
    g_assert_nonnull(id2);

    store_set_tags(store, note2, "Work; urgent");
    store_set_date(store, note2, 20240115);
    g_assert_true(store_save(store, note2, &err));
    g_assert_no_error(err);

    store_free(store);

    /* Riapre l'archivio per verificare la persistenza su disco. */
    reopened = store_open(archive_dir, config_dir, &err);
    g_assert_no_error(err);
    g_assert_nonnull(reopened);
    g_assert_cmpuint(reopened->notes->len, ==, 2);

    found = store_get(reopened, id1);
    g_assert_nonnull(found);
    g_assert_cmpstr(found->text, ==, "hello world");

    found = store_get(reopened, id2);
    g_assert_nonnull(found);
    g_assert_cmpstr(found->text, ==, "urgent task");
    g_assert_cmpstr(found->tags, ==, "urgent;work");
    g_assert_cmpuint(found->date, ==, 20240115u);

    results = store_search(reopened, "hello", NULL, &err);
    g_assert_no_error(err);
    g_assert_nonnull(results);
    g_assert_cmpuint(results->len, ==, 1);
    g_ptr_array_free(results, TRUE);

    results = store_search(reopened, "", "urgent", &err);
    g_assert_no_error(err);
    g_assert_nonnull(results);
    g_assert_cmpuint(results->len, ==, 1);
    g_ptr_array_free(results, TRUE);

    results = store_search(reopened, "-#", NULL, &err);
    g_assert_no_error(err);
    g_assert_nonnull(results);
    g_assert_cmpuint(results->len, ==, 1);
    g_ptr_array_free(results, TRUE);

    g_assert_true(store_delete(reopened, id2, &err));
    g_assert_no_error(err);

    store_free(reopened);

    /* Riapre dopo la cancellazione per confermare la rimozione. */
    final_store = store_open(archive_dir, config_dir, &err);
    g_assert_no_error(err);
    g_assert_nonnull(final_store);
    g_assert_cmpuint(final_store->notes->len, ==, 1);
    g_assert_nonnull(store_get(final_store, id1));
    g_assert_null(store_get(final_store, id2));
    store_free(final_store);

    g_free(id1);
    g_free(id2);

    remove_tree(archive_dir);
    remove_tree(config_dir);
    g_free(archive_dir);
    g_free(config_dir);
}

static void test_tag_counts_and_suggest(void) {
    GError *err = NULL;
    char *archive_dir;
    char *config_dir;
    char *counts;
    Store *store;
    Note *note;
    GPtrArray *suggestions;
    const char *catalog = "urgent\t1\nwork\t2\nurgent2\t5\nzebra\t9\nUrgent3\t3";

    archive_dir = g_dir_make_tmp("jotilla-test-tags-XXXXXX", &err);
    g_assert_no_error(err);
    g_assert_nonnull(archive_dir);

    config_dir = g_dir_make_tmp("jotilla-test-tags-cfg-XXXXXX", &err);
    g_assert_no_error(err);
    g_assert_nonnull(config_dir);

    store = store_open(archive_dir, config_dir, &err);
    g_assert_no_error(err);
    g_assert_nonnull(store);

    note = store_new(store, "urgent task");
    g_assert_nonnull(note);
    store_set_tags(store, note, "Work; urgent");

    note = store_new(store, "misc task");
    g_assert_nonnull(note);
    store_set_tags(store, note, "work; misc");

    counts = store_tag_counts(store);
    g_assert_nonnull(counts);
    g_assert_nonnull(strstr(counts, "urgent\t1"));
    g_assert_nonnull(strstr(counts, "work\t2"));
    g_assert_nonnull(strstr(counts, "misc\t1"));

    /* Prefisso case-insensitive, ordinamento per conteggio decrescente. */
    suggestions = tags_suggest_from(catalog, "ur", NULL, 10);
    g_assert_nonnull(suggestions);
    g_assert_cmpuint(suggestions->len, ==, 3);
    g_assert_cmpstr(g_ptr_array_index(suggestions, 0), ==, "urgent2");
    g_assert_cmpstr(g_ptr_array_index(suggestions, 1), ==, "Urgent3");
    g_assert_cmpstr(g_ptr_array_index(suggestions, 2), ==, "urgent");
    g_ptr_array_free(suggestions, TRUE);

    /* I tag gia' assegnati sono esclusi. */
    suggestions = tags_suggest_from(catalog, "UR", "urgent2", 10);
    g_assert_nonnull(suggestions);
    g_assert_cmpuint(suggestions->len, ==, 2);
    g_assert_cmpstr(g_ptr_array_index(suggestions, 0), ==, "Urgent3");
    g_assert_cmpstr(g_ptr_array_index(suggestions, 1), ==, "urgent");
    g_ptr_array_free(suggestions, TRUE);

    /* Il limite massimo viene rispettato. */
    suggestions = tags_suggest_from(catalog, "ur", "urgent2", 1);
    g_assert_nonnull(suggestions);
    g_assert_cmpuint(suggestions->len, ==, 1);
    g_assert_cmpstr(g_ptr_array_index(suggestions, 0), ==, "Urgent3");
    g_ptr_array_free(suggestions, TRUE);

    /* Nessun prefisso corrispondente, max non positivo e catalogo vuoto. */
    suggestions = tags_suggest_from(catalog, "zz", NULL, 10);
    g_assert_nonnull(suggestions);
    g_assert_cmpuint(suggestions->len, ==, 0);
    g_ptr_array_free(suggestions, TRUE);

    suggestions = tags_suggest_from(catalog, "ur", NULL, 0);
    g_assert_nonnull(suggestions);
    g_assert_cmpuint(suggestions->len, ==, 0);
    g_ptr_array_free(suggestions, TRUE);

    suggestions = tags_suggest_from("", "ur", NULL, 10);
    g_assert_nonnull(suggestions);
    g_assert_cmpuint(suggestions->len, ==, 0);
    g_ptr_array_free(suggestions, TRUE);

    g_free(counts);
    store_free(store);
    remove_tree(archive_dir);
    remove_tree(config_dir);
    g_free(archive_dir);
    g_free(config_dir);
}

static void test_app_int_persistence(void) {
    GError *err = NULL;
    char *archive_dir;
    char *config_dir;
    Store *store;
    Store *reopened;

    archive_dir = g_dir_make_tmp("jotilla-test-app-XXXXXX", &err);
    g_assert_no_error(err);
    g_assert_nonnull(archive_dir);

    config_dir = g_dir_make_tmp("jotilla-test-app-cfg-XXXXXX", &err);
    g_assert_no_error(err);
    g_assert_nonnull(config_dir);

    store = store_open(archive_dir, config_dir, &err);
    g_assert_no_error(err);
    g_assert_nonnull(store);

    /* Chiave assente: viene restituito il fallback. */
    g_assert_cmpint(store_app_get_int(store, "missing", 7), ==, 7);

    store_app_set_int(store, "window_width", 1280);
    g_assert_cmpint(store_app_get_int(store, "window_width", 0), ==, 1280);

    store_app_set_int(store, "negative", -5);
    g_assert_cmpint(store_app_get_int(store, "negative", 0), ==, -5);

    store_free(store);

    /* I valori sopravvivono alla riapertura dello store. */
    reopened = store_open(archive_dir, config_dir, &err);
    g_assert_no_error(err);
    g_assert_nonnull(reopened);
    g_assert_cmpint(store_app_get_int(reopened, "window_width", 0), ==, 1280);
    g_assert_cmpint(store_app_get_int(reopened, "negative", 0), ==, -5);
    g_assert_cmpint(store_app_get_int(reopened, "missing", 7), ==, 7);

    store_free(reopened);
    remove_tree(archive_dir);
    remove_tree(config_dir);
    g_free(archive_dir);
    g_free(config_dir);
}

static void test_note_id_format(void) {
    GError *err = NULL;
    char *archive_dir;
    char *config_dir;
    GRegex *regex;
    Store *store;
    guint i;

    archive_dir = g_dir_make_tmp("jotilla-test-id-XXXXXX", &err);
    g_assert_no_error(err);
    g_assert_nonnull(archive_dir);

    config_dir = g_dir_make_tmp("jotilla-test-id-cfg-XXXXXX", &err);
    g_assert_no_error(err);
    g_assert_nonnull(config_dir);

    regex = g_regex_new(
        "^note-[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}-[0-9]{2}-[0-9]{2}"
        "-[0-9]{3}-[0-9]{3}$",
        0, 0, &err);
    g_assert_no_error(err);
    g_assert_nonnull(regex);

    store = store_open(archive_dir, config_dir, &err);
    g_assert_no_error(err);
    g_assert_nonnull(store);

    for (i = 0; i < 8; ++i) {
        Note *note = store_new(store, "id format");
        g_assert_nonnull(note);
        g_assert_nonnull(note->id);
        g_assert_true(g_regex_match(regex, note->id, 0, NULL));
    }

    g_regex_unref(regex);
    store_free(store);
    remove_tree(archive_dir);
    remove_tree(config_dir);
    g_free(archive_dir);
    g_free(config_dir);
}

static void test_archive_path_persistence(void) {
    GError *err = NULL;
    char *base;
    char *archive_a;
    char *archive_b;
    char *config_dir;
    char *ini_path;
    char *stored;
    GKeyFile *keyfile;
    Store *store;
    Store *reopened;

    base = g_dir_make_tmp("jotilla-test-arcpath-XXXXXX", &err);
    g_assert_no_error(err);
    g_assert_nonnull(base);

    archive_a = g_build_filename(base, "archive-a", NULL);
    archive_b = g_build_filename(base, "archive-b", NULL);
    config_dir = g_build_filename(base, "config", NULL);
    g_assert_cmpint(g_mkdir_with_parents(archive_a, 0700), ==, 0);
    g_assert_cmpint(g_mkdir_with_parents(archive_b, 0700), ==, 0);
    g_assert_cmpint(g_mkdir_with_parents(config_dir, 0700), ==, 0);

    store = store_open(archive_a, config_dir, &err);
    g_assert_no_error(err);
    g_assert_nonnull(store);

    /* Un'altra chiave [app] non deve essere persa dal cambio percorso. */
    store_app_set_int(store, "keep_me", 321);
    store_set_archive_path(store, archive_b);

    ini_path = g_build_filename(config_dir, "notes.ini", NULL);
    keyfile = g_key_file_new();
    g_assert_true(g_key_file_load_from_file(keyfile, ini_path, G_KEY_FILE_NONE, &err));
    g_assert_no_error(err);

    stored = g_key_file_get_string(keyfile, "app", "archive.path", &err);
    g_assert_no_error(err);
    g_assert_cmpstr(stored, ==, archive_b);
    g_assert_cmpint(g_key_file_get_integer(keyfile, "app", "keep_me", &err), ==, 321);
    g_assert_no_error(err);

    g_free(stored);
    g_key_file_free(keyfile);
    g_free(ini_path);
    store_free(store);

    /* Riapre senza percorso esplicito: deve usare quello configurato. */
    reopened = store_open(NULL, config_dir, &err);
    g_assert_no_error(err);
    g_assert_nonnull(reopened);
    g_assert_cmpstr(store_archive_dir(reopened), ==, archive_b);
    g_assert_cmpint(store_app_get_int(reopened, "keep_me", 0), ==, 321);
    store_free(reopened);

    remove_tree(base);
    g_free(archive_a);
    g_free(archive_b);
    g_free(config_dir);
    g_free(base);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/core/date", test_date);
    g_test_add_func("/core/tags", test_tags);
    g_test_add_func("/core/palette", test_palette);
    g_test_add_func("/core/store", test_store);
    g_test_add_func("/core/tag_counts_suggest", test_tag_counts_and_suggest);
    g_test_add_func("/core/app_int_persistence", test_app_int_persistence);
    g_test_add_func("/core/note_id_format", test_note_id_format);
    g_test_add_func("/core/archive_path_persistence", test_archive_path_persistence);

    return g_test_run();
}
