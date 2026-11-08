#ifndef JOTILA_METADATA_H
#define JOTILA_METADATA_H
#include <windows.h>

/* One bounded read per snapshot. The caller owns the decoded buffer. */
BOOL metadata_read(const wchar_t *path, wchar_t **result, BOOL *unicode);
typedef struct MetadataCursor { wchar_t *next, *section; } MetadataCursor;
/* Iteration splits the owned buffer in place; returned strings borrow it. */
BOOL metadata_next(MetadataCursor *cursor, wchar_t **key, wchar_t **value);
#endif
