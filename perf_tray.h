/* perf_tray.h - live telemetry tray meters (one notification-area icon per meter, redrawn every sample,
 * also while every SRS window is hidden) and the Task Manager settings (update speed, start page,
 * per-core CPU units, the meters, tray behaviour, summon shortcut).
 *
 * The icons are StatusNotifierItems spoken over D-Bus directly (perf_sni.c); no libappindicator.
 * The data comes from perf_data.h through one shared sampler thread (the "hub" below), which the
 * Performance tab can subscribe to as well, so the machine is read once per tick, not once per view.
 *
 * Settings live in SRS's own file, ~/.config/srs/SRSDEF.ini, group [Task Manager] (keys below).
 *
 * Tests: SRS_TEST_TRAY_FEED=sine feeds synthetic values instead of perf_data (no /proc, no GPU);
 * SRS_TEST_TRAY_PNG_DIR=/abs/dir writes every redrawn icon as <dir>/<meter>-<size>.png;
 * SRS_TEST_TRAY_LOG=1 prints "SRS-TEST: tray ..." lines to stderr.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef SRS_PERF_TRAY_H
#define SRS_PERF_TRAY_H

#include <gtk/gtk.h>
#include "perf_data.h"

/* ---- settings ([Task Manager] in SRSDEF.ini) ----
 *   Update Speed        0 = Low (one sample every 2 s), 1 = Normal (1 s, default), 2 = High (every 0.5 s),
 *                       3 = every 0.25 s, 4 = every 5 s
 *   Start Page          0 Summary (default), 1 Performance, 2 Processes, 3 Startup Apps, 4 Users, 5 Services
 *   Per Core Units      0 = 100 % is the whole CPU (default), 1 = 100 % is one logical CPU
 *   Telemetry Icons     0 = off (default), 1 = the chosen meters are in the tray
 *   Telemetry Meters    comma list of meter ids (see perf_tray_meter_*), default "cpu,mem,gpu0"
 *   Disk Space Mount    "" = the fullest mounted file system (default), else a mount point
 *   Summon Shortcut     0 = not bound (default), 1 = bound through the desktop's GlobalShortcuts portal
 *   Tray Position       0 = Normal (default): tray Category "Hardware", like Plasma's volume / network / battery icons;
 *                       1 = First: Category "Other", which Plasma sorts into its first group (any category it does not
 *                       know counts as "UnknownCategory", first in its order), and Titles starting with a space */
enum { PT_SPEED_LOW, PT_SPEED_NORMAL, PT_SPEED_HIGH, PT_SPEED_HIGHEST, PT_SPEED_LOWEST };   /* stored: 2 s, 1 s, 0.5 s, 0.25 s, 5 s */
/* the update-speed choices in list order, fastest first: the same list in GPU Top's "Update" drop-down (its card readings)
   and here (the Performance tab and the tray meters) */
#define PT_SPEED_CHOICES 5
int perf_speed_choice_ms(int i);              /* 250, 500, 1000, 2000, 5000 */
const char *perf_speed_choice_label(int i);   /* "Every 0.25 s", ..., "Every 1 s (Normal)", ... */
int perf_speed_choice_of_ms(int ms);          /* the list index of an interval (the nearest one) */
enum { PT_PAGE_SUMMARY, PT_PAGE_PERFORMANCE, PT_PAGE_PROCESSES, PT_PAGE_STARTUP, PT_PAGE_USERS, PT_PAGE_SERVICES, PT_PAGE_COUNT };

typedef struct {
    int speed;                     /* PT_SPEED_* */
    int start_page;                /* PT_PAGE_* */
    int per_core_units;            /* 1 = one logical CPU is 100 % */
    int icons_on;
    char meters[512];              /* "cpu,mem,gpu0" */
    char disk_mount[256];
    int shortcut_bound;
    int gpu_top_ms;                /* [GPU Top] Update ms: how often the GPU Top window reads the cards (250..5000, 1000) */
    int tray_first;                /* Tray Position: 1 = First */
} perf_prefs;

/* the current settings (loaded on first use); never NULL */
const perf_prefs *perf_prefs_get(void);
int perf_prefs_interval_ms(void);          /* 250 / 500 / 1000 / 2000 / 5000 */
/* GPU Top's own update interval (its GPU and SMI tabs), kept here so one settings page shows every live-update rate */
int perf_prefs_gpu_top_ms(void);
void perf_prefs_set_gpu_top_ms(int ms);    /* saves and tells the listeners */
const char *perf_page_name(int page);      /* "Summary", ... */
/* where SRS's tray icons go (Plasma sorts its tray by Category, then by Title): the SNI Category ("Hardware" or "Other")
   and the Title prefix ("SRS" or " SRS") for the current Tray Position. SRS's own tray icon uses the same, so all SRS icons
   stay together: libappindicator's APP_INDICATOR_CATEGORY_OTHER is "Other", _HARDWARE is "Hardware". */
const char *perf_tray_category(void);
const char *perf_tray_title_prefix(void);
void perf_prefs_set_tray_first(int on);    /* saves, makes the meter icons anew and tells the listeners */
/* called on the GUI thread after any setting changed (the Performance tab re-reads what it uses) */
typedef void (*perf_prefs_cb)(void *user);
void perf_prefs_listen(perf_prefs_cb cb, void *user);
void perf_prefs_unlisten(perf_prefs_cb cb, void *user);
/* CPU % in the chosen unit: whole-machine % in, the shown value out (x ncpu with Per Core Units) */
double perf_prefs_cpu_value(double whole_pct, int ncpu);

/* ---- the shared sampler ("hub") ----
 * One worker thread runs pd_sample() every perf_prefs_interval_ms() while anybody is subscribed and
 * hands each snapshot to the subscribers on the GUI thread. The snapshot is only valid during the
 * callback (copy what you keep). need_procs = 0: the hub skips the process walk (pd_set_procs) while no
 * subscriber needs it, so the tray alone costs little. */
typedef void (*perf_hub_cb)(const pd_snap *s, void *user);
guint perf_hub_subscribe(perf_hub_cb cb, void *user, int need_procs);
void perf_hub_unsubscribe(guint id);
/* the last snapshot (NULL before the first); GUI thread only, valid until the next sample */
const pd_snap *perf_hub_last(void);

/* ---- the tray meters ---- */
/* what SRS's main window gives the tray (gui_gtk.c fills this once at start; any member may be NULL) */
typedef struct {
    void (*show_main)(void);               /* show every SRS window and raise the main one (srs_show_all) */
    void (*open_performance)(void);        /* show the Performance tab (GPU Top window); NULL = show_main */
    void (*quit)(void);                    /* the normal quit path (asks first when a test is running) */
    int (*windows_hidden)(void);           /* 1 = SRS is hidden to the tray right now */
    /* the existing Options entries, so the settings page shows and changes the same values */
    int (*get_close_to_tray)(void);
    void (*set_close_to_tray)(int on);
    int (*get_autostart)(void);            /* ~/.config/autostart/srs-gui.desktop with --gui-tray */
    void (*set_autostart)(int on);
    /* open the user guide at a section ("tray-meters"); NULL = no Guide button in the settings dialog */
    void (*open_guide)(GtkWindow *parent, const char *anchor);
} perf_tray_hooks;
void perf_tray_set_hooks(const perf_tray_hooks *h);

/* start the meters when Telemetry Icons is on (call once after the main window exists); safe to call again */
void perf_tray_init(void);
/* 1 = at least one meter icon is registered with the desktop's tray, so hiding SRS leaves a way back */
int perf_tray_active(void);
/* remove every meter icon (on quit) */
void perf_tray_shutdown(void);

/* meter ids for the settings and the menu: "cpu", "cores", "cpuhist", "cputemp", "mem", "disk", "space", "net",
   and per GPU n (0-based): "gpu<n>", "gpuhist<n>", "gputemp<n>", "vram<n>" */
int perf_tray_meter_count(void);                                   /* with the GPUs found now */
const char *perf_tray_meter_id(int i, char *buf, size_t len);
const char *perf_tray_meter_label(int i, char *buf, size_t len);   /* "CPU Meter", "GPU 1 Temp", ... */

/* ---- settings UI ---- */
/* the Task Manager settings as a widget (for a settings dialog or a page); changes apply and save at once */
GtkWidget *perf_tray_settings_widget(void);
/* the same in a small dialog (one at a time; raised when already open) */
void perf_tray_settings_dialog(GtkWindow *parent);

/* ---- summon shortcut (xdg-desktop-portal GlobalShortcuts) ---- */
/* called when the bound shortcut is pressed; default: open_performance or show_main */
void perf_tray_shortcut_restore(void);     /* at start: re-open the portal session when it was bound */

#endif
