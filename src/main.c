#include <gtk/gtk.h>
#include "enchant_data.h"
#include "ui.h"

static void on_activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    ui_build_window(app);
}

int main(int argc, char **argv) {
    enchant_data_init();

    GtkApplication *app = gtk_application_new("dev.lokifisch.cheap",
                                                G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
