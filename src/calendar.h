#ifndef JOTILA_CALENDAR_H
#define JOTILA_CALENDAR_H
#include <windows.h>
#include <commctrl.h>
#include "date.h"

#define CALENDAR_CLASS L"Jotila.Calendar"
#define CALENDAR_LIST_CLASS L"Jotila.Day"
#define CALENDAR_DAY (NM_FIRST - 80)
#define CALENDAR_YEAR (NM_FIRST - 81)
#define CALENDAR_LEAVE (NM_FIRST - 82)
typedef struct CalendarEvent { NMHDR header; unsigned date; } CalendarEvent;
typedef struct CalendarItem {
    wchar_t id[80], title[256];
    COLORREF paper, ink;
} CalendarItem;

unsigned calendar_today(void);
HWND calendar_create(HWND parent, int id);
/* A picker posts the selected date (zero removes it) to its owner. */
HWND calendar_picker(HWND owner, POINT anchor, unsigned selected, UINT message);
int calendar_year(HWND window);
void calendar_set_year(HWND window, int year);
void calendar_counts(HWND window, const unsigned counts[366]);
BOOL calendar_day_rect(HWND window, unsigned date, RECT *rect);
void calendar_icon(HDC dc, RECT rect, COLORREF ink, BOOL assigned);
/* Takes ownership of items, including on failure. */
HWND calendar_list(HWND owner, POINT anchor, unsigned date, CalendarItem *items,
                   int count, BOOL (*open)(const wchar_t *id));
void calendar_list_close(HWND owner);
#endif
