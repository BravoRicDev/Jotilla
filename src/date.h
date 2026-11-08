#ifndef JOTILA_DATE_H
#define JOTILA_DATE_H
#include <stddef.h>

/* Civil Gregorian dates, YYYYMMDD; zero means unassigned. No time zones. */
#define DATE_MIN_YEAR 1601
#define DATE_MAX_YEAR 9999
int date_days(int year, int month);
int date_valid(unsigned date);
int date_ordinal(unsigned date); /* zero-based day within its year, or -1 */
int date_weekday(unsigned date); /* Monday = 0 */
unsigned date_shift(unsigned date, int days);
unsigned date_parse(const wchar_t *value);

#endif
