/* Dichiarazioni per la gestione dei tag. */

#ifndef JOTILLA_TAGS_H
#define JOTILLA_TAGS_H

#include <glib.h>

/* Normalizza una stringa di tag. */
char *tags_normalize(const char *input);
/* Verifica la presenza di un tag. */
gboolean tags_contains(const char *tags, const char *name);
/* Suddivide i tag in array. */
GPtrArray *tags_split(const char *tags);

#endif
