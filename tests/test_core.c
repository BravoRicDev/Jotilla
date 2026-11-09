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

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/core/date", test_date);
    g_test_add_func("/core/tags", test_tags);
    g_test_add_func("/core/palette", test_palette);
    g_test_add_func("/core/store", test_store);

    return g_test_run();
}
