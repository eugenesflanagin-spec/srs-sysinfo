/* perf_tray_test.c - drives perf_tray.c without the rest of SRS, for tests/perf-tray-test.sh.
 *   perf_tray_test sni SECONDS [FLIP_AT]   start the meters (settings from $XDG_CONFIG_HOME/srs/SRSDEF.ini), run SECONDS,
 *                                     quit; FLIP_AT: switch Tray Position (Normal <-> First) after that many seconds
 *   perf_tray_test settings OUT.png [dark]   draw the Task Manager settings page into a PNG (needs a display)
 * Use with SRS_TEST_TRAY_FEED=sine (synthetic machine) and a private session bus (dbus-run-session).
 * The hooks print "SRS-TEST: hook <name>" so the test sees what a click did.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#include "../perf_tray.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_close = 0, g_auto = 0;
static void h_show(void) { fprintf(stderr, "SRS-TEST: hook show_main\n"); }
static void h_perf(void) { fprintf(stderr, "SRS-TEST: hook open_performance\n"); }
static void h_quit(void) { fprintf(stderr, "SRS-TEST: hook quit, active %d\n", perf_tray_active()); perf_tray_shutdown(); gtk_main_quit(); }
static int h_hidden(void) { return 1; }
static int h_gclose(void) { return g_close; }
static void h_sclose(int on) { g_close = on; fprintf(stderr, "SRS-TEST: hook close_to_tray %d\n", on); }
static int h_gauto(void) { return g_auto; }
static void h_sauto(int on) { g_auto = on; fprintf(stderr, "SRS-TEST: hook autostart %d\n", on); }

static gboolean flip(gpointer d)
{
    (void)d;
    perf_prefs_set_tray_first(!perf_prefs_get()->tray_first);
    fprintf(stderr, "SRS-TEST: tray position %s\n", perf_prefs_get()->tray_first ? "first" : "normal");
    return G_SOURCE_REMOVE;
}

static gboolean stop(gpointer d)
{
    (void)d;
    fprintf(stderr, "SRS-TEST: time up, active %d\n", perf_tray_active());
    perf_tray_shutdown();
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

int main(int argc, char **argv)
{
    perf_tray_hooks h = { h_show, h_perf, h_quit, h_hidden, h_gclose, h_sclose, h_gauto, h_sauto, NULL };   /* no Guide button here */
    int disp = gtk_init_check(&argc, &argv);
    if (argc < 3) { fprintf(stderr, "usage: perf_tray_test sni SECONDS | settings OUT.png [dark]\n"); return 2; }
    perf_tray_set_hooks(&h);
    if (!strcmp(argv[1], "sni")) {
        perf_tray_init();
        fprintf(stderr, "SRS-TEST: active at start %d\n", perf_tray_active());
        g_timeout_add(atoi(argv[2]) * 1000, stop, NULL);
        if (argc > 3) g_timeout_add(atoi(argv[3]) * 1000, flip, NULL);
        gtk_main();
        return 0;
    }
    if (!strcmp(argv[1], "settings")) {
        GtkWidget *win, *page;
        cairo_surface_t *s;
        cairo_t *cr;
        int w, ht;
        if (!disp) { fprintf(stderr, "no display\n"); return 1; }
        if (argc > 3 && !strcmp(argv[3], "dark"))
            g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);
        win = gtk_offscreen_window_new();
        page = perf_tray_settings_widget();
        gtk_container_add(GTK_CONTAINER(win), page);
        gtk_widget_show_all(win);
        while (gtk_events_pending()) gtk_main_iteration();
        w = gtk_widget_get_allocated_width(win);
        ht = gtk_widget_get_allocated_height(win);
        s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, ht);
        cr = cairo_create(s);
        gtk_widget_draw(win, cr);
        cairo_destroy(cr);
        cairo_surface_write_to_png(s, argv[2]);
        cairo_surface_destroy(s);
        printf("settings %dx%d\n", w, ht);
        return 0;
    }
    return 2;
}
