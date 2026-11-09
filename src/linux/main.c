/* Punto di ingresso Jotilla: gestisce finestre note, archivio e ciclo GTK. */
#include <gtk/gtk.h>
#include <glib.h>
#include <string.h>
#include "jotilla.h"
#include "note_window.h"
#include "archive_window.h"
#include "platform.h"

typedef struct {
    Store *store;
    GtkWidget *archive;
    char *archive_dir;
    char *config_dir;
    int window_count;
} AppCtx;

typedef struct {
    AppCtx *ctx;
    Note *note;
} NoteRef;

static void open_note(AppCtx *ctx, Note *note);
static void on_archive_destroy(GtkWidget *widget, gpointer data);

/* Aggiorna l'archivio quando una nota cambia. */
static void on_note_changed(Note *note, gpointer data)
{    AppCtx *ctx = data;
    (void)note;
    if (ctx->archive != NULL)
        archive_window_refresh(ctx->archive);
}

/* Crea una nuova nota e la apre. */
static void on_new_note(gpointer data)
{    AppCtx *ctx = data;
    Note *note = store_new(ctx->store, "");
    if (note != NULL)
        open_note(ctx, note);
}

/* Apre una nota selezionata dall'archivio. */
static void on_archive_open(Note *note, gpointer data)
{    open_note(data, note);
}

/* Mostra o crea la finestra archivio. */
static void on_show_archive(gpointer data)
{    AppCtx *ctx = data;
    if (ctx->archive == NULL) {
        ctx->archive = archive_window_new(ctx->store);
        archive_window_set_open_callback(ctx->archive, on_archive_open, ctx);
        g_signal_connect(ctx->archive, "destroy",
                         G_CALLBACK(on_archive_destroy), ctx);
        ctx->window_count++;
    }
    gtk_widget_show_all(ctx->archive);
    gtk_window_present(GTK_WINDOW(ctx->archive));
}

/* Esce dall'app se non restano finestre. */
static gboolean maybe_quit_idle(gpointer data)
{    AppCtx *ctx = data;

    if (ctx == NULL)
        return G_SOURCE_REMOVE;

    if (ctx->window_count <= 0)
        gtk_main_quit();

    return G_SOURCE_REMOVE;
}

/* Salva la nota e aggiorna il contatore finestre alla chiusura. */
static void on_note_destroy(GtkWidget *widget, gpointer data)
{    NoteRef *ref = data;
    (void)widget;
    if (ref != NULL) {
        if (ref->note != NULL) {
            GError *err = NULL;
            ref->note->open = FALSE;
            if (!store_save(ref->ctx->store, ref->note, &err)) {
                if (err != NULL) {
                    g_warning("Could not save note: %s", err->message);
                    g_error_free(err);
                }
            }
        }
        {
            AppCtx *ctx = ref->ctx;
            if (ctx->window_count > 0)
                ctx->window_count--;
            g_free(ref);
            g_idle_add(maybe_quit_idle, ctx);
        }
    }
}

/* Resetta l'archivio e aggiorna il contatore finestre. */
static void on_archive_destroy(GtkWidget *widget, gpointer data)
{    AppCtx *ctx = data;
    (void)widget;
    if (ctx != NULL) {
        ctx->archive = NULL;
        if (ctx->window_count > 0)
            ctx->window_count--;
    }
    g_idle_add(maybe_quit_idle, ctx);
}

/* Apre una nota in nuova finestra, salvandola e collegando callback. */
static void open_note(AppCtx *ctx, Note *note)
{    GtkWidget *window;
    NoteRef *ref;
    GError *err = NULL;

    if (ctx == NULL || note == NULL)
        return;

    note->open = TRUE;
    if (!store_save(ctx->store, note, &err)) {
        if (err != NULL) {
            g_warning("Could not save note: %s", err->message);
            g_error_free(err);
        }
    }

    window = note_window_new(ctx->store, note);
    if (window == NULL)
        return;

    note_window_set_changed_callback(window, on_note_changed, ctx);
    note_window_set_new_callback(window, on_new_note, ctx);
    note_window_set_archive_callback(window, on_show_archive, ctx);

    ref = g_new0(NoteRef, 1);
    ref->ctx = ctx;
    ref->note = note;
    g_signal_connect(window, "destroy", G_CALLBACK(on_note_destroy), ref);

    ctx->window_count++;
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}

/* Forza l'uscita dal ciclo GTK (smoke test). */
static gboolean quit_loop(gpointer data)
{    (void)data;
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

/* Esegue test interni su store e ricerca, poi pulisce. */
static int run_self_test(const char *archive_dir, const char *config_dir)
{    char *tmp = g_dir_make_tmp("jotilla-selftest-XXXXXX", NULL);
    char *notes = archive_dir != NULL ? g_strdup(archive_dir) : g_build_filename(tmp, "notes", NULL);
    char *cfg = config_dir != NULL ? g_strdup(config_dir) : g_build_filename(tmp, "cfg", NULL);
    GError *err = NULL;
    Store *store;
    Note *note;
    GPtrArray *hits;
    int rc = 0;

    if (tmp == NULL) {
        g_printerr("self-test: cannot create temp dir\n");
        return 1;
    }

    store = store_open(notes, cfg, &err);
    if (store == NULL) {
        g_printerr("self-test: store_open failed: %s\n", err != NULL ? err->message : "unknown");
        g_clear_error(&err);
        rc = 1;
        goto out;
    }

    note = store_new(store, "hello self test");
    if (note == NULL) {
        g_printerr("self-test: store_new failed\n");
        rc = 1;
        goto out_store;
    }

    store_set_tags(store, note, "Work; urgent");
    store_set_date(store, note, 20260101);

    hits = store_search(store, "hello", NULL, &err);
    if (hits == NULL || hits->len != 1) {
        g_printerr("self-test: search failed\n");
        rc = 1;
    }
    if (hits != NULL)
        g_ptr_array_free(hits, TRUE);

    hits = store_search(store, "", "urgent", &err);
    if (hits == NULL || hits->len != 1) {
        g_printerr("self-test: tag search failed\n");
        rc = 1;
    }
    if (hits != NULL)
        g_ptr_array_free(hits, TRUE);

    if (store_get(store, note->id) == NULL) {
        g_printerr("self-test: lookup failed\n");
        rc = 1;
    }

    if (rc == 0)
        g_print("SELF-TEST OK\n");

out_store:
    store_free(store);
out:
    if (tmp != NULL) {
        char *cmd = g_strdup_printf("rm -rf '%s'", tmp);
        g_spawn_command_line_sync(cmd, NULL, NULL, NULL, NULL);
        g_free(cmd);
    }
    g_free(tmp);
    g_free(notes);
    g_free(cfg);
    return rc;
}

/* Inizializza GTK, apre store e note, avvia il ciclo principale. */
int main(int argc, char **argv)
{    char *archive_dir = NULL;
    char *config_dir = NULL;
    gboolean self_test = FALSE;
    gboolean smoke_test = FALSE;
    GError *err = NULL;
    GOptionEntry entries[] = {
        {"archive", 'a', 0, G_OPTION_ARG_FILENAME, &archive_dir, "Archive directory", "DIR"},
        {"config", 'c', 0, G_OPTION_ARG_FILENAME, &config_dir, "Config directory", "DIR"},
        {"self-test", 0, 0, G_OPTION_ARG_NONE, &self_test, "Run internal self test and exit", NULL},
        {"smoke-test", 0, 0, G_OPTION_ARG_NONE, &smoke_test, "Open windows, run briefly, exit", NULL},
        {NULL}
    };
    GOptionContext *opt = g_option_context_new(NULL);
    AppCtx ctx = {0};

    g_option_context_add_main_entries(opt, entries, NULL);
    g_option_context_add_group(opt, gtk_get_option_group(TRUE));

    if (!g_option_context_parse(opt, &argc, &argv, &err)) {
        g_printerr("option parsing failed: %s\n", err != NULL ? err->message : "unknown");
        g_clear_error(&err);
        g_option_context_free(opt);
        return 2;
    }
    g_option_context_free(opt);

    if (self_test) {
        int rc = run_self_test(archive_dir, config_dir);
        g_free(archive_dir);
        g_free(config_dir);
        return rc;
    }

    ctx.store = store_open(archive_dir, config_dir, &err);
    if (ctx.store == NULL) {
        g_printerr("cannot open store: %s\n", err != NULL ? err->message : "unknown");
        g_clear_error(&err);
        g_free(archive_dir);
        g_free(config_dir);
        return 1;
    }
    ctx.archive_dir = g_strdup(archive_dir != NULL ? archive_dir : store_archive_dir(ctx.store));
    ctx.config_dir = g_strdup(config_dir);

    if (ctx.store->notes->len == 0) { /* nessuna nota: crea una vuota */
        Note *note = store_new(ctx.store, "");
        if (note != NULL)
            open_note(&ctx, note);
    } else {        guint i;
        for (i = 0; i < ctx.store->notes->len; ++i) {
            Note *note = g_ptr_array_index(ctx.store->notes, i);
            if (note->open)
                open_note(&ctx, note);
        }
        if (ctx.store->notes->len > 0) {
            gboolean any = FALSE;
            for (i = 0; i < ctx.store->notes->len; ++i) {
                Note *note = g_ptr_array_index(ctx.store->notes, i);
                if (note->open) {
                    any = TRUE;
                    break;
                }
            }
            if (!any)
                open_note(&ctx, g_ptr_array_index(ctx.store->notes, 0));
        }
    }

    if (smoke_test) {
        on_show_archive(&ctx);
        g_timeout_add(1200, quit_loop, NULL);
    }

    gtk_main();

    store_free(ctx.store);
    g_free(ctx.archive_dir);
    g_free(ctx.config_dir);
    g_free(archive_dir);
    g_free(config_dir);
    return 0;
}
