/* Test di round-trip RTF: verifica serializzazione e deserializzazione
 * del testo formattato in GtkTextBuffer. */
#include <gtk/gtk.h>
#include <string.h>

#include "rtf.h"

static void test_rtf_roundtrip(void) {
    GtkTextBuffer *buffer;
    GtkTextBuffer *copy;
    GtkTextTag *bold;
    GtkTextTag *italic;
    GtkTextTag *underline;
    GtkTextTag *strike;
    GtkTextTag *foreground;
    GtkTextTagTable *table;
    GtkTextTag *bold_tag;
    GtkTextIter start;
    GtkTextIter end;
    GtkTextIter match_start;
    GtkTextIter match_end;
    GtkTextIter search;
    char *rtf;
    char *text;

    if (!gtk_init_check(NULL, NULL)) {
        g_test_skip("GTK could not be initialized (no display)");
        return;
    }

    buffer = gtk_text_buffer_new(NULL);
    g_assert_nonnull(buffer);

    bold = gtk_text_buffer_create_tag(buffer, "bold",
                                      "weight", PANGO_WEIGHT_BOLD, NULL);
    italic = gtk_text_buffer_create_tag(buffer, "italic",
                                        "style", PANGO_STYLE_ITALIC, NULL);
    underline = gtk_text_buffer_create_tag(buffer, "underline",
                                           "underline", PANGO_UNDERLINE_SINGLE, NULL);
    strike = gtk_text_buffer_create_tag(buffer, "strike",
                                        "strikethrough", TRUE, NULL);
    foreground = gtk_text_buffer_create_tag(buffer, "foreground",
                                            "foreground", "#ff0000", NULL);
    g_assert_nonnull(bold);
    g_assert_nonnull(italic);
    g_assert_nonnull(underline);
    g_assert_nonnull(strike);
    g_assert_nonnull(foreground);

    gtk_text_buffer_set_text(buffer, "hello bold world", -1);

    gtk_text_buffer_get_iter_at_offset(buffer, &start, 6);
    gtk_text_buffer_get_iter_at_offset(buffer, &end, 10);
    gtk_text_buffer_apply_tag(buffer, bold, &start, &end);

    gtk_text_buffer_get_iter_at_offset(buffer, &start, 11);
    gtk_text_buffer_get_iter_at_offset(buffer, &end, 16);
    gtk_text_buffer_apply_tag(buffer, italic, &start, &end);
    gtk_text_buffer_apply_tag(buffer, underline, &start, &end);
    gtk_text_buffer_apply_tag(buffer, strike, &start, &end);
    gtk_text_buffer_apply_tag(buffer, foreground, &start, &end);

    /* Serializza il buffer formattato in RTF. */
    rtf = rtf_from_buffer(buffer);
    g_assert_nonnull(rtf);
    g_assert_nonnull(strstr(rtf, "\\rtf1"));

    copy = gtk_text_buffer_new(NULL);
    g_assert_nonnull(copy);
    rtf_to_buffer(rtf, copy);

    gtk_text_buffer_get_bounds(copy, &start, &end);
    text = gtk_text_buffer_get_text(copy, &start, &end, FALSE);
    g_assert_nonnull(text);
    g_assert_nonnull(strstr(text, "hello bold world"));
    g_free(text);

    gtk_text_buffer_get_start_iter(copy, &search);
    g_assert_true(gtk_text_iter_forward_search(&search, "bold", 0,
                                               &match_start, &match_end, NULL));

    table = gtk_text_buffer_get_tag_table(copy);
    bold_tag = gtk_text_tag_table_lookup(table, "bold");
    g_assert_nonnull(bold_tag);
    g_assert_true(gtk_text_iter_has_tag(&match_start, bold_tag));

    g_free(rtf);
    g_object_unref(copy);
    g_object_unref(buffer);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/rtf/roundtrip", test_rtf_roundtrip);

    return g_test_run();
}
