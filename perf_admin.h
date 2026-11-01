/* perf_admin.h - the Startup Apps, Users and Services pages of the GPU Top window's Performance tab,
 * and the Run New Task dialog.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef SRS_PERF_ADMIN_H
#define SRS_PERF_ADMIN_H

#include <gtk/gtk.h>
#include "perf_data.h"

GtkWidget *pa_startup_new(void);
GtkWidget *pa_users_new(void);
/* refresh the Users page from the latest process snapshot (called once a second while it shows) */
void pa_users_update(GtkWidget *page, const pd_snap *s);
GtkWidget *pa_services_new(void);
/* the Run New Task dialog: runs a command line detached, with the user's environment */
void pa_run_new_task(GtkWindow *parent);

#endif
