/* perf_sni.h - a small StatusNotifierItem (the freedesktop / KDE notification-area protocol) with a
 * com.canonical.dbusmenu context menu, spoken over D-Bus with GDBus. No libappindicator: that library
 * cannot hand the tray a new picture every second, which the live meters need.
 *
 * Each item has its own session-bus connection and owns org.kde.StatusNotifierItem-<pid>-<n>, as the
 * specification asks; freeing the item closes the connection, so the tray drops the icon at once.
 * Items re-register by themselves when the tray (org.kde.StatusNotifierWatcher) restarts.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef SRS_PERF_SNI_H
#define SRS_PERF_SNI_H

#include <gtk/gtk.h>

typedef struct psni psni;

typedef struct {
    int id;                        /* > 0, what the menu callback gets */
    const char *label;             /* NULL = separator; '_' marks a mnemonic */
    int check;                     /* -1 plain entry, 0 / 1 = check box off / on */
    int enabled;
} psni_menu_item;

typedef struct {
    /* left click (x, y = screen position, 0 when the tray does not say); token = the xdg-activation token
       the tray gave just before (Wayland: needed to raise a window), "" when none */
    void (*activate)(psni *it, int x, int y, const char *token, void *user);
    void (*secondary)(psni *it, int x, int y, const char *token, void *user);   /* middle click */
    void (*menu)(psni *it, int id, const char *token, void *user);              /* a menu entry was chosen */
    void (*registered)(psni *it, int on, void *user);   /* 1 = the tray took the icon, 0 = the tray went away */
} psni_cbs;

/* id: a stable name ("srs-cpu"); title: the human name ("CPU Meter"). NULL when there is no session bus. */
psni *psni_new(const char *id, const char *title, const psni_cbs *cb, void *user);
void psni_free(psni *it);
int psni_registered(const psni *it);

/* the icon at several sizes (Cairo ARGB32 image surfaces, square). Converted to the protocol's
   big-endian, straight-alpha ARGB; NewIcon is sent only when the pixels changed. Returns 1 when sent. */
int psni_set_icon(psni *it, cairo_surface_t *const *surf, int n);
/* tooltip title and body (plain text); NewToolTip only on change */
void psni_set_tooltip(psni *it, const char *title, const char *body);
void psni_set_title(psni *it, const char *title);
/* the SNI Category ("Hardware" by default). Trays read it once, when the item registers: call it right after psni_new. */
void psni_set_category(psni *it, const char *category);
/* replace the whole menu (copied); LayoutUpdated is sent */
void psni_set_menu(psni *it, const psni_menu_item *items, int n);

/* tests: counters */
unsigned psni_icon_sends(const psni *it);
const char *psni_bus_name(const psni *it);

#endif
