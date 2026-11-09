/* API pubbliche per conversione RTF/buffer GTK. */
#ifndef JOTILLA_RTF_H
#define JOTILLA_RTF_H

#include <gtk/gtk.h>

/* Genera RTF da buffer GTK. */
char *rtf_from_buffer(GtkTextBuffer *buffer);
/* Carica RTF in buffer GTK. */
void rtf_to_buffer(const char *rtf, GtkTextBuffer *buffer);

#endif
