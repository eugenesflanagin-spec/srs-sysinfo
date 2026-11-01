/* perf_view.h - the Performance tab of the GPU Top window: a live task-manager view of the whole PC
 * (Summary, Performance, Processes, Startup Apps, Users, Services) drawn with Cairo LED-segment
 * meters and glowing history graphs.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef SRS_PERF_VIEW_H
#define SRS_PERF_VIEW_H

#include <gtk/gtk.h>

/* the whole tab content. It samples once a second only while it is mapped (its notebook page is
   the current one and the window is not minimised), and frees everything when destroyed.
   Test hooks: SRS_PERF_PAGE=summary|performance|processes|startup|users|services picks the first
   page; SRS_PERF_RES=cpu|memory|power|thermals|disk|network|gpu the Performance page's resource. */
GtkWidget *perf_view_new(void);
/* show this page ("summary", "performance", "processes", "startup", "users", "services") in every open view, or in the
   next one made (call it before opening the window, e.g. before gpu_top_window_open_page(.., GPU_TOP_PAGE_PERFORMANCE)) */
void perf_view_request_page(const char *name);

#endif
