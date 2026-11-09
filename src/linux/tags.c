/* Normalizzazione, ricerca e suddivisione di tag separati da ';'. */

#include <string.h>
#include <glib.h>
#include "tags.h"

#define TAGS_MAX 24
#define TAG_BYTES_MAX 63

/* Verifica che il nome del tag non contenga caratteri di controllo o delimitatori. */
static gboolean tag_name_valid(const char *name) {
    const unsigned char *p = (const unsigned char *)name;
    if (!*p) return FALSE;
    for (; *p; ++p) {
        if (*p < 0x20 || *p == 0x7f) return FALSE;
        if (*p == '#' || *p == '"') return FALSE;
    }
    return TRUE;
}

/* Comparatore per ordinamento alfabetico Unicode. */
static gint tag_compare(gconstpointer a, gconstpointer b) {
    const char *left = *(const char *const *)a;
    const char *right = *(const char *const *)b;
    return g_utf8_collate(left, right);
}

/* Normalizza input: trim, rimuove '#', casefold, deduplica, ordina. */
char *tags_normalize(const char *input) {
    GPtrArray *names;
    GString *output;
    char **parts;
    guint i;

    if (!input) return g_strdup("");

    names = g_ptr_array_new_with_free_func(g_free);
    parts = g_strsplit(input, ";", -1);
    for (i = 0; parts[i]; ++i) {
        char *raw, *trimmed, *folded;
        gboolean duplicate = FALSE;
        guint j;

        raw = g_strdup(parts[i]);
        trimmed = g_strstrip(raw);
        if (*trimmed == '#') trimmed = g_strstrip(trimmed + 1); /* Rimuove prefisso '#' opzionale. */

        if (!tag_name_valid(trimmed) || strlen(trimmed) > TAG_BYTES_MAX) {
            g_free(raw);
            continue;
        }

        folded = g_utf8_casefold(trimmed, -1);
        g_free(raw);
        if (!folded || strlen(folded) > TAG_BYTES_MAX) {
            g_free(folded);
            continue;
        }

        for (j = 0; j < names->len; ++j)
            if (!g_strcmp0((const char *)g_ptr_array_index(names, j), folded)) {
                duplicate = TRUE;
                break;
            }
        if (duplicate) {
            g_free(folded);
            continue;
        }
        if (names->len >= TAGS_MAX) {
            g_free(folded);
            break; /* Limite massimo di tag raggiunto. */
        }
        g_ptr_array_add(names, folded);
    }
    g_strfreev(parts);

    g_ptr_array_sort(names, tag_compare);
    output = g_string_new("");
    for (i = 0; i < names->len; ++i) {
        if (i) g_string_append_c(output, ';');
        g_string_append(output, (const char *)g_ptr_array_index(names, i));
    }
    g_ptr_array_free(names, TRUE);
    return g_string_free(output, FALSE);
}

/* Verifica se un tag è presente nella lista (case-insensitive). */
gboolean tags_contains(const char *tags, const char *name) {
    char *key;
    char **parts;
    guint i;
    gboolean found = FALSE;

    if (!tags || !name || !*name) return FALSE;
    key = g_utf8_casefold(name, -1);
    if (!key) return FALSE;

    parts = g_strsplit(tags, ";", -1);
    for (i = 0; parts[i] && !found; ++i) {
        char *item = g_strstrip(g_strdup(parts[i]));
        if (*item) {
            char *folded = g_utf8_casefold(item, -1);
            if (folded && !g_strcmp0(folded, key)) found = TRUE;
            g_free(folded);
        }
        g_free(item);
    }
    g_strfreev(parts);
    g_free(key);
    return found;
}

/* Suddivide la stringa dei tag in array di stringhe. */
GPtrArray *tags_split(const char *tags) {
    GPtrArray *items = g_ptr_array_new_with_free_func(g_free);
    char **parts;
    guint i;

    if (!tags) return items;

    parts = g_strsplit(tags, ";", -1);
    for (i = 0; parts[i]; ++i) {
        char *item = g_strstrip(g_strdup(parts[i]));
        if (*item) g_ptr_array_add(items, item);
        else g_free(item);
    }
    g_strfreev(parts);
    return items;
}
