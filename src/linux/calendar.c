/* Selettore data modale basato su GtkCalendar per Jotilla.
 * Espone un dialog con pulsanti "Clear"/"OK" e callback di scelta. */
#include "calendar.h"
#include "date.h"

typedef struct {
    GtkWidget *calendar;
    void (*on_pick)(guint32 date, gpointer data);
    gpointer data;
} CalendarPicker;

/* Gestisce la risposta del dialog: restituisce la data scelta o 0 se annullata. */
static void calendar_picker_response(GtkDialog *dialog, gint response_id,
                                     gpointer data) {    CalendarPicker *picker = data;

    if (response_id == GTK_RESPONSE_OK) {
        guint year = 0;
        guint month = 0;
        guint day = 0;
        guint32 date;

        gtk_calendar_get_date(GTK_CALENDAR(picker->calendar),
                              &year, &month, &day);
         date = (guint32)(year * 10000u + (month + 1u) * 100u + day); /* codifica AAAAMMGG */
         if (!date_valid(date)) date = 0; /* data non valida -> nessuna data */
        if (picker->on_pick) picker->on_pick(date, picker->data);
    } else if (response_id == GTK_RESPONSE_REJECT) {
        if (picker->on_pick) picker->on_pick(0, picker->data);
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

/* Crea e configura il dialog calendario, preselezionando la data corrente se valida. */
GtkWidget *calendar_picker_new(GtkWindow *parent, guint32 current,
                               void (*on_pick)(guint32 date, gpointer data),
                               gpointer data) {    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *calendar;
    CalendarPicker *picker;

    dialog = gtk_dialog_new_with_buttons("Choose date", parent,
                                         GTK_DIALOG_MODAL |
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "Clear", GTK_RESPONSE_REJECT,
                                         "OK", GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 8);

    calendar = gtk_calendar_new();
    gtk_widget_set_halign(calendar, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(content), calendar, TRUE, TRUE, 0);

    if (current != 0 && date_valid(current)) {
         guint year = current / 10000u; /* decodifica AAAAMMGG */
         guint month = current / 100u % 100u;
         guint day = current % 100u;
        GDateTime *moment = g_date_time_new_local((gint)year, (gint)month,
                                                  (gint)day, 12, 0, 0);
        if (moment) {
            gtk_calendar_select_month(GTK_CALENDAR(calendar),
                                      (guint)(g_date_time_get_month(moment) - 1),
                                      (guint)g_date_time_get_year(moment));
            gtk_calendar_select_day(GTK_CALENDAR(calendar),
                                    (guint)g_date_time_get_day_of_month(moment));
            g_date_time_unref(moment);
        }
    }

    picker = g_new0(CalendarPicker, 1);
    picker->calendar = calendar;
    picker->on_pick = on_pick;
    picker->data = data;
    g_object_set_data_full(G_OBJECT(dialog), "calendar-picker", picker, g_free);
    g_signal_connect(dialog, "response",
                     G_CALLBACK(calendar_picker_response), picker);

    return dialog;
}
