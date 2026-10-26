/* power.h - SRS "Power & cost" page for srs-gui, and its headless background recorder.
 *
 * The page shows the whole-PC power estimate with its range, what it costs, history graphs and
 * CSV export, the parts / power-supply / price setup and alarms. Recording (off / for a set time /
 * always) writes one row per minute to ~/.local/share/srs/power.db. Settings: ~/.config/srs/power.ini.
 * Overrides for tests: SRS_POWER_DB and SRS_POWER_INI (file paths).
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef SRS_POWER_H
#define SRS_POWER_H

#include <gtk/gtk.h>

/* the page for the main window's sidebar; open_item opens an SRS item (the expert tabs) */
GtkWidget *power_page_new(void (*open_item)(const char *id));
/* `srs-gui --power-recorder`: record without a window (systemd user service); returns the exit code */
int power_recorder_main(int argc, char **argv);

#endif
