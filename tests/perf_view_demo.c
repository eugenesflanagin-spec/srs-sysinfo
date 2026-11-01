/* perf_view_demo.c - a window with only the Performance tab (perf_view_new), for screenshots and
 * smoke tests under xvfb (tests/perf-view-test.sh). SRS_PERF_PAGE / SRS_PERF_RES pick the page;
 * SRS_PERF_DEMO_SECONDS closes the window after that many seconds (default: stay open).
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#include "../perf_view.h"

#include <stdlib.h>

static gboolean quit(gpointer d) { gtk_widget_destroy(GTK_WIDGET(d)); return G_SOURCE_REMOVE; }

int main(int argc, char **argv)
{
    GtkWidget *w, *nb;
    const char *secs = getenv("SRS_PERF_DEMO_SECONDS");
    gtk_init(&argc, &argv);
    w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(w), "Performance View Test");
    gtk_window_set_default_size(GTK_WINDOW(w), 1400, 900);
    nb = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), perf_view_new(), gtk_label_new_with_mnemonic("_Performance"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), gtk_label_new("other tab"), gtk_label_new_with_mnemonic("_GPU"));
    gtk_container_add(GTK_CONTAINER(w), nb);
    g_signal_connect(w, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(w);
    if (secs && atoi(secs) > 0) g_timeout_add_seconds((guint)atoi(secs), quit, w);
    gtk_main();
    return 0;
}
