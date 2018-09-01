#include <gtk/gtk.h>
#include "db-misc.h"
#include "otpclient.h"
#include "common.h"
#include "manual-add-cb.h"
#include "gquarks.h"
#include "message-dialogs.h"
#include "imports.h"
#include "get-builder.h"

static void changed_otp_cb (GtkWidget *cb, gpointer user_data);


void
add_data_dialog (GSimpleAction *simple    __attribute__((unused)),
                 GVariant      *parameter __attribute__((unused)),
                 gpointer       user_data)
{
    ImportData *import_data = (ImportData *)user_data;
    GtkListStore *list_store = g_object_get_data (G_OBJECT (import_data->main_window), "lstore");
    Widgets *widgets = g_new0 (Widgets, 1);

    GtkBuilder *builder = get_builder_from_partial_path (UI_PARTIAL_PATH);
    widgets->dialog = GTK_WIDGET(gtk_builder_get_object (builder, "manual_add_diag_id"));
    widgets->otp_cb = GTK_WIDGET(gtk_builder_get_object (builder, "otp_combotext_id"));
    widgets->algo_cb = GTK_WIDGET(gtk_builder_get_object (builder, "algo_combotext_id"));
    widgets->steam_ck = GTK_WIDGET(gtk_builder_get_object (builder, "steam_ck_btn"));
    widgets->iss_entry = GTK_WIDGET(gtk_builder_get_object (builder, "manual_diag_issuer_entry_id"));
    widgets->sec_entry = GTK_WIDGET(gtk_builder_get_object (builder, "manual_diag_secret_entry_id"));
    widgets->digits_entry = GTK_WIDGET(gtk_builder_get_object (builder, "digits_entry_manual_diag"));
    widgets->period_entry = GTK_WIDGET(gtk_builder_get_object (builder, "period_entry_manual_diag"));
    widgets->counter_entry = GTK_WIDGET(gtk_builder_get_object (builder, "counter_entry_manual_diag"));
    gtk_widget_set_sensitive (widgets->counter_entry, FALSE); // by default TOTP is selected, so we don't need counter_cb

    g_signal_connect (widgets->sec_entry, "icon-press", G_CALLBACK(icon_press_cb), NULL);
    g_signal_connect (widgets->otp_cb, "changed", G_CALLBACK(changed_otp_cb), widgets);
    g_signal_connect (widgets->steam_ck, "", G_CALLBACK(steam_toggled_cb), widgets);

    GError *err = NULL;
    gint result = gtk_dialog_run (GTK_DIALOG(widgets->dialog));
    switch (result) {
        case GTK_RESPONSE_OK:
            if (parse_user_data (widgets, import_data->db_data)) {
                update_and_reload_db (import_data->db_data, list_store, TRUE, &err);
                if (err != NULL && !g_error_matches (err, missing_file_gquark (), MISSING_FILE_CODE)) {
                    show_message_dialog (import_data->main_window, err->message, GTK_MESSAGE_ERROR);
                }
            }
            break;
        case GTK_RESPONSE_CANCEL:
        default:
            break;
    }
    gtk_widget_destroy (widgets->dialog);
    g_free (widgets);
}


static void
changed_otp_cb (GtkWidget *cb,
                gpointer   user_data)
{
    Widgets *widgets = (Widgets *)user_data;
    // id 0 is totp, id 1 is hotp
    gtk_widget_set_sensitive (widgets->counter_entry, gtk_combo_box_get_active (cb));
}


static void
steam_toggled_cb (GtkWidget *ck_btn,
                  gpointer   user_data)
{
    Widgets *widgets = (Widgets *)user_data;
    gboolean button_toggled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(widgets->steam_ck));
    gtk_widget_set_sensitive (widgets->otp_cb, !button_toggled);
    gtk_widget_set_sensitive (widgets->algo_cb, !button_toggled);
    gtk_widget_set_sensitive (widgets->digits_entry, !button_toggled);
    gtk_widget_set_sensitive (widgets->period_entry, !button_toggled);
    gtk_widget_set_sensitive (widgets->counter_entry, !button_toggled);
    g_object_set (widgets->iss_entry, "editable", !button_toggled, NULL);
    if (button_toggled) {
        gtk_combo_box_set_active (widgets->otp_cb, 0); // TOTP
        gtk_combo_box_set_active (widgets->algo_cb, 0); // SHA1
        gtk_entry_set_text (GTK_ENTRY (widgets->iss_entry), "Steam");
        gtk_entry_set_text (GTK_ENTRY (widgets->period_entry), "");
        gtk_entry_set_text (GTK_ENTRY (widgets->digits_entry), "5");
    } else {
        gtk_entry_set_text (GTK_ENTRY (widgets->iss_entry), "");
        gtk_entry_set_text (GTK_ENTRY (widgets->digits_entry), "");
        gtk_entry_set_text (GTK_ENTRY (widgets->counter_entry), "");
    }
}