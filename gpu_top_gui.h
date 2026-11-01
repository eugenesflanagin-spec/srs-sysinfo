/* gpu_top_gui.h - SRS "GPU Top" window: live GPU activity, memory, processes, sensors and gpu_metrics
 * per graphics card, with an SMI table of every card (the AMD layout follows amdgpu_top's GUI).
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef SRS_GPU_TOP_GUI_H
#define SRS_GPU_TOP_GUI_H

#include <gtk/gtk.h>

/* open the GPU Top window, or raise it when it is already open (one window); parent may be NULL */
void gpu_top_window_open(GtkWindow *parent);

/* the same, showing one tab (the tray meters open the Performance tab) */
enum { GPU_TOP_PAGE_PERFORMANCE, GPU_TOP_PAGE_GPU, GPU_TOP_PAGE_SMI, GPU_TOP_PAGE_DRM, GPU_TOP_PAGE_PPTABLE, GPU_TOP_PAGE_NPU };
void gpu_top_window_open_page(GtkWindow *parent, int page);

/* the GPU Top window when it is open, else NULL (hide to tray / show again with the other SRS windows) */
GtkWidget *gpu_top_window_get(void);

/* open the user guide at a section anchor of GPU Top / the Task Manager ("gpu-top", "perf-summary", "tray-meters") */
void gpu_top_open_guide(GtkWindow *parent, const char *anchor);

#endif
