/* gui_startup.h - the Startup tab's manager (srs-gui main window, 2.2) */
#ifndef GUI_STARTUP_H
#define GUI_STARTUP_H

#include <gtk/gtk.h>

/* A panel listing every startup item (desktop autostart entries, systemd user units, optionally
 * system services) with switches to turn them off and on again without deleting anything, Undo,
 * Restore default and Add to startup. SRS_STARTUP_DRY_RUN=1: systemctl / pkexec are never run, the
 * command is only shown (autostart files are still written, in $XDG_CONFIG_HOME). */
GtkWidget *startup_manager_new(void);

#endif
