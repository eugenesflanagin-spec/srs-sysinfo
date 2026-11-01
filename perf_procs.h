/* perf_procs.h - process actions of the Performance tab (Processes page, Top CPU Processes on Summary):
 * End Task, Force Kill, End Process Tree, Suspend / Resume, Set Priority, Set CPU Affinity, Open File
 * Location, Copy Command Line, Properties, and End Parent Process for zombies.
 *
 * Safety: PID 1 and kernel threads are never acted on; every ending action asks first (with an extra
 * warning for parts of the desktop session and for SRS itself); a process is only signalled when its
 * start time still matches (no PID reuse). Other users' processes go through pkexec (polkit asks for
 * the password). Tests: SRS_TEST_PKEXEC=/path/to/fake replaces pkexec.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef SRS_PERF_PROCS_H
#define SRS_PERF_PROCS_H

#include <gtk/gtk.h>
#include "perf_data.h"

/* ---- the plain part (no GTK; tests drive it through tests/perf_procs_harness.c) ---- */

/* why nothing may be done to this process, or NULL: PID 1, kernel threads */
const char *pp_untouchable(int pid, int ppid, int kthread);
/* why ending it needs an extra warning, or NULL: SRS itself, an ancestor of SRS (your session), PID 1, systemd*,
   startplasma*, plasmashell, kwin_*, Xwayland, sddm*, dbus*, pipewire*, polkit* */
const char *pp_critical(int pid, const char *name);
/* the process's start time (clock ticks after boot) and state from /proc/<pid>/stat; 0 when gone */
int pp_stat(int pid, unsigned long long *start, char *state, int *ppid);
/* send sig if pid still has this start time. 0 sent, 1 not allowed (EPERM: needs pkexec), 2 gone, -1 other error */
int pp_signal(int pid, unsigned long long start, int sig, char *err, size_t errlen);
/* nice value: 0 set, 1 needs pkexec, 2 gone, -1 error */
int pp_renice(int pid, unsigned long long start, int nice, char *err, size_t errlen);
/* CPU affinity as a list "0-3,8" (sched_getaffinity / sched_setaffinity); same return codes */
int pp_get_affinity(int pid, char *list, size_t len);
int pp_set_affinity(int pid, unsigned long long start, const char *list, char *err, size_t errlen);
/* the processes below pid in s (deepest first), at most max; returns the count */
int pp_tree(const pd_snap *s, int pid, int *out, int max);
/* the argv for the elevated variant: pkexec kill -s TERM 1234 / renice -n 5 -p 1234 / taskset -p -c 0-3 1234
   (NULL-terminated, free with g_strfreev) */
char **pp_elevated_argv(const char *what, const char *arg, int pid);
/* "TERM", "KILL", "STOP", "CONT" */
const char *pp_signame(int sig);

/* ---- the GTK part ---- */
typedef struct pp_ui pp_ui;
/* the view tells the actions what is selected (pids into out, at most max) and the latest snapshot */
typedef int (*pp_sel_fn)(void *user, int *out, int max);
typedef const pd_snap *(*pp_snap_fn)(void *user);

pp_ui *pp_ui_new(pp_sel_fn sel, pp_snap_fn snap, void *user);
void pp_ui_free(pp_ui *u);
/* (the Summary page uses a second pp_ui whose selection is the right-clicked row and does not show its toolbar) */
/* the toolbar (End Task, Force Kill, End Process Tree, Suspend, Resume, Priority, Affinity, Properties ...) and the
   info bar for results; pack both where they belong */
GtkWidget *pp_toolbar(pp_ui *u);
GtkWidget *pp_infobar(pp_ui *u);
/* after the selection or the snapshot changed */
void pp_update(pp_ui *u);
/* right-click / Menu key menu for the selection; ev may be NULL (keyboard) */
void pp_popup(pp_ui *u, GtkWidget *anchor, GdkEvent *ev);
/* Delete = End Task, Shift+Delete = Force Kill; returns TRUE when handled */
gboolean pp_key(pp_ui *u, GdkEventKey *ev);

#endif
