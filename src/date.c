#include <stddef.h>
#include <wchar.h>
#include "date.h"

int date_days(int year, int month) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (year < DATE_MIN_YEAR || year > DATE_MAX_YEAR || month < 1 || month > 12) return 0;
    return days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

int date_valid(unsigned date) {
    int day = date % 100;
    return day > 0 && day <= date_days((int)(date / 10000), date / 100 % 100);
}

int date_ordinal(unsigned date) {
    int month, day = (int)(date % 100) - 1;
    if (!date_valid(date)) return -1;
    for (month = 1; month < (int)(date / 100 % 100); ++month) day += date_days((int)(date / 10000), month);
    return day;
}

int date_weekday(unsigned date) {
    int year = (int)(date / 10000) - 1, ordinal = date_ordinal(date);
    return ordinal < 0 ? -1 : (year * 365 + year / 4 - year / 100 + year / 400 + ordinal) % 7;
}

unsigned date_shift(unsigned date, int days) {
    int year = (int)(date / 10000), month = date / 100 % 100, day = date % 100;
    if (!date_valid(date)) return 0;
    while (days > 0) {
        int left = date_days(year, month) - day;
        if (days <= left) { day += days; break; }
        if (year == DATE_MAX_YEAR && month == 12) return 99991231;
        days -= left + 1; day = 1;
        if (++month > 12) { month = 1; ++year; }
    }
    while (days < 0) {
        if (days > -day) { day += days; break; }
        if (year == DATE_MIN_YEAR && month == 1) return 16010101;
        days += day;
        if (--month < 1) { month = 12; --year; }
        day = date_days(year, month);
    }
    return (unsigned)(year * 10000 + month * 100 + day);
}

unsigned date_parse(const wchar_t *value) {
    unsigned date = 0;
    int i;
    for (i = 0; i < 8; ++i) {
        if (value[i] < L'0' || value[i] > L'9') return 0;
        date = date * 10 + value[i] - L'0';
    }
    return !value[8] && date_valid(date) ? date : 0;
}
