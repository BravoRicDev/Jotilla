/* Interfaccia pubblica della finestra archivio. */
/* Dichiarazioni delle funzioni esportate. */
#ifndef JOTILLA_ARCHIVE_WINDOW_H
#define JOTILLA_ARCHIVE_WINDOW_H

#include "jotilla.h"

typedef void (*ArchiveOpenFunc)(Note *note, gpointer data);

/* Crea e inizializza la finestra archivio. */
GtkWidget *archive_window_new(Store *store);
/* Ricarica tag, lista e calendario. */
void archive_window_refresh(GtkWidget *window);
/* Imposta il callback di apertura nota. */
void archive_window_set_open_callback(GtkWidget *window, ArchiveOpenFunc cb, gpointer data);

#endif
