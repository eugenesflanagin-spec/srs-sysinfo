/* gui_startup.c - the Startup tab's manager (srs-gui main window, 2.1).
 *
 * What starts with the desktop or the user session, and switches to turn each item off and on
 * again. Nothing is ever deleted:
 *   - a system autostart entry (/etc/xdg/autostart) is turned off by a user copy of it in
 *     $XDG_CONFIG_HOME/autostart with Hidden=true (the XDG rule: a user file of the same name wins);
 *     turning it back on moves that copy into ~/.config/srs/startup-backup;
 *   - a user autostart entry gets Hidden=true / false (the original is kept in the backup folder
 *     the first time SRS changes it);
 *   - systemd user units: systemctl --user enable / disable; system services (only when asked to
 *     show them): pkexec systemctl enable / disable, after a warning.
 * Undo puts back the last change of this session; Restore default puts back the item's default
 * (system entry: drop the user copy; user entry: the original file; systemd: systemctl preset).
 * Add to startup writes $XDG_CONFIG_HOME/autostart/srs-NAME.desktop for a program or a command,
 * with an optional delay. Desktop-essential items (Plasma, PipeWire, KWallet, portals, polkit
 * agent ...) ask before they are turned off. SRS_STARTUP_DRY_RUN=1: no systemctl / pkexec runs. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "gui_startup.h"

#include <gio/gdesktopappinfo.h>
#include <glib/gstdio.h>
#include <fnmatch.h>
#include <string.h>
#include <time.h>

typedef enum { SK_XDG_USER, SK_XDG_SYS, SK_SD_USER, SK_SD_SYS } skind;
static const char *const g_kind_name[] = { "Autostart (yours)", "Autostart (system)", "Service (user)", "Service (system)" };

typedef struct {
    skind kind;
    char *id;           /* file name (foo.desktop) or unit name */
    char *name, *what;  /* Name= / description, Exec= / unit state */
    char *sys_path;     /* system .desktop (SK_XDG_SYS) */
    char *user_path;    /* the user's file (SK_XDG_USER, or the override of a system entry) */
    int on, can, essential, srs_override, srs_added, has_orig;
    char *note;
} sitem;

typedef struct {   /* one change, for Undo */
    skind kind;
    char *id, *path;        /* XDG: the user file that changed */
    int existed;            /* XDG: it existed before */
    char *before;           /* XDG: its content before */
    int was_on;             /* systemd: enabled before */
} undo;

typedef struct {
    GtkWidget *box, *view, *filter, *kindsel, *sys, *status, *undo_btn, *restore_btn;
    GtkListStore *store;
    GtkTreeModel *fmodel;
    GPtrArray *items;       /* sitem* */
    GPtrArray *undos;       /* undo* */
    int busy;
} smgr;

enum { COL_ON, COL_NAME, COL_KIND, COL_STATE, COL_NOTE, COL_IDX, COL_CAN, COL_TIP, NCOL };

static int dry_run(void) { const char *d = g_getenv("SRS_STARTUP_DRY_RUN"); return d && *d && strcmp(d, "0"); }

static void sitem_free(gpointer p)
{
    sitem *s = p;
    g_free(s->id); g_free(s->name); g_free(s->what); g_free(s->sys_path); g_free(s->user_path); g_free(s->note);
    g_free(s);
}
static void undo_free(gpointer p) { undo *u = p; g_free(u->id); g_free(u->path); g_free(u->before); g_free(u); }

static GtkWindow *top(smgr *m)
{
    GtkWidget *t = gtk_widget_get_toplevel(m->box);
    return GTK_IS_WINDOW(t) ? GTK_WINDOW(t) : NULL;
}

static void status(smgr *m, const char *s) { gtk_label_set_text(GTK_LABEL(m->status), s); }

static int ask(smgr *m, GtkMessageType t, const char *yes, const char *text)
{
    GtkWidget *d = gtk_message_dialog_new(top(m), GTK_DIALOG_MODAL, t, GTK_BUTTONS_NONE, "%s", text);
    int r;
    gtk_dialog_add_buttons(GTK_DIALOG(d), "_Cancel", GTK_RESPONSE_CANCEL, yes, GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_CANCEL);
    r = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT;
    gtk_widget_destroy(d);
    return r;
}

static void tell(smgr *m, GtkMessageType t, const char *text)
{
    GtkWidget *d = gtk_message_dialog_new(top(m), GTK_DIALOG_MODAL, t, GTK_BUTTONS_OK, "%s", text);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

/* ---- the desktop's essentials: ask before turning them off ---- */
static const char *essential_what(const char *id)
{
    static const struct { const char *pat, *what; } e[] = {
        { "*plasma*", "the Plasma desktop" }, { "*ksmserver*", "the KDE session" }, { "*kwin*", "the window manager" },
        { "*kded*", "KDE's background services" }, { "*kglobalaccel*", "the keyboard shortcuts" },
        { "*kwallet*", "password storage (KWallet)" }, { "*gnome-keyring*", "password storage (GNOME Keyring)" },
        { "*ksecretd*", "password storage" }, { "*pipewire*", "sound and screen sharing (PipeWire)" },
        { "*wireplumber*", "sound (WirePlumber)" }, { "*pulseaudio*", "sound (PulseAudio)" },
        { "*xdg-desktop-portal*", "desktop portals (file dialogs, screen sharing, Flatpak)" },
        { "*polkit*", "the password prompt for admin tasks (polkit agent)" }, { "*gnome-shell*", "the GNOME desktop" },
        { "*gsd-*", "GNOME settings" }, { "*org.gnome.settingsdaemon*", "GNOME settings" }, { "*at-spi*", "accessibility" },
        { "*dbus*", "D-Bus (programs talking to each other)" }, { "*xembedsniproxy*", "tray icons" },
        { "*gmenudbusmenuproxy*", "application menus" }, { "*systemd-*", "a core systemd service" },
        { "*networkmanager*", "the network" }, { "*sddm*", "the login screen" }, { "*gdm*", "the login screen" },
        { "*lightdm*", "the login screen" }, { "*display-manager*", "the login screen" }, { "*udisks*", "drives and USB sticks" },
        { "*upower*", "power and battery" }, { "*xdg-user-dirs*", "the standard folders" }, { NULL, NULL } };
    char *low = g_ascii_strdown(id, -1);
    const char *r = NULL;
    int i;
    for (i = 0; e[i].pat && !r; i++) if (!fnmatch(e[i].pat, low, 0)) r = e[i].what;
    g_free(low);
    return r;
}

/* ---- running systemctl (never in a dry run) ---- */
static int run_cmd(smgr *m, char **argv, char **err)
{
    char *line = g_strjoinv(" ", argv), *e = NULL;
    int st = -1, ok;
    if (dry_run()) {
        char *s = g_strdup_printf("Dry run (SRS_STARTUP_DRY_RUN): would run  %s", line);
        status(m, s);
        fprintf(stderr, "srs: %s\n", s);
        g_free(s);
        g_free(line);
        return 1;
    }
    ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &e, &st, NULL) && g_spawn_check_wait_status(st, NULL);
    if (!ok && err) *err = g_strdup_printf("%s\n\n%s", line, e && *e ? e : "failed or cancelled");
    g_free(e);
    g_free(line);
    return ok;
}

/* ---- autostart files ---- */
static char *user_dir(void) { return g_build_filename(g_get_user_config_dir(), "autostart", NULL); }
static char *backup_dir(void)
{
    char *d = g_build_filename(g_get_user_config_dir(), "srs", "startup-backup", NULL);
    g_mkdir_with_parents(d, 0700);
    return d;
}

/* move a file into the backup folder (never deleted): name.YYYYMMDD-HHMMSS */
static int to_backup(const char *path)
{
    char *d = backup_dir(), *base = g_path_get_basename(path), stamp[32], *dst;
    time_t now = time(NULL);
    struct tm tm;
    int ok, k = 0;
    localtime_r(&now, &tm);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
    dst = g_strdup_printf("%s/%s.%s", d, base, stamp);
    while (g_file_test(dst, G_FILE_TEST_EXISTS)) { g_free(dst); dst = g_strdup_printf("%s/%s.%s-%d", d, base, stamp, ++k); }
    ok = !g_rename(path, dst);
    g_free(d); g_free(base); g_free(dst);
    return ok;
}

static char *orig_path(const char *id) { char *d = backup_dir(), *p = g_strdup_printf("%s/%s.orig", d, id); g_free(d); return p; }

static int desktop_matches(GKeyFile *kf, char **why)
{
    const char *cur = g_getenv("XDG_CURRENT_DESKTOP");
    char **only = g_key_file_get_string_list(kf, "Desktop Entry", "OnlyShowIn", NULL, NULL);
    char **not = g_key_file_get_string_list(kf, "Desktop Entry", "NotShowIn", NULL, NULL);
    char **cd = g_strsplit(cur ? cur : "", ":", -1), **p, **q;
    int match = only ? 0 : 1;
    for (p = cd; *p && **p; p++) {
        for (q = only; q && *q; q++) if (!g_ascii_strcasecmp(*p, *q)) match = 1;
        for (q = not; q && *q; q++) if (!g_ascii_strcasecmp(*p, *q)) match = 0;
    }
    if (!match && why) *why = g_strdup(only ? "only for other desktops (OnlyShowIn)" : "not for this desktop (NotShowIn)");
    g_strfreev(only); g_strfreev(not); g_strfreev(cd);
    return match;
}

static int kf_on(GKeyFile *kf)
{
    char *h = g_key_file_get_string(kf, "Desktop Entry", "Hidden", NULL), *g = g_key_file_get_string(kf, "Desktop Entry", "X-GNOME-Autostart-enabled", NULL);
    int on = !(h && !g_ascii_strcasecmp(g_strstrip(h), "true")) && !(g && !g_ascii_strcasecmp(g_strstrip(g), "false"));
    g_free(h); g_free(g);
    return on;
}

static GKeyFile *kf_load(const char *path)
{
    GKeyFile *kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, NULL)) { g_key_file_free(kf); return NULL; }
    return kf;
}

static sitem *find_item(GPtrArray *v, skind k, const char *id)
{
    guint i;
    for (i = 0; i < v->len; i++) { sitem *s = g_ptr_array_index(v, i); if (s->kind == k && !strcmp(s->id, id)) return s; }
    return NULL;
}

static void xdg_scan(GPtrArray *v)
{
    const char *const *sd = g_get_system_config_dirs();
    char *ud = user_dir();
    int n, i;
    GDir *d;
    const char *f;
    for (n = 0; sd[n]; n++) ;
    for (i = n - 1; i >= 0; i--) {   /* earlier dirs win: read them last */
        char *dir = g_build_filename(sd[i], "autostart", NULL);
        if ((d = g_dir_open(dir, 0, NULL))) {
            while ((f = g_dir_read_name(d))) {
                char *p;
                sitem *s;
                if (!g_str_has_suffix(f, ".desktop")) continue;
                p = g_build_filename(dir, f, NULL);
                if (!(s = find_item(v, SK_XDG_SYS, f))) { s = g_new0(sitem, 1); s->kind = SK_XDG_SYS; s->id = g_strdup(f); g_ptr_array_add(v, s); }
                g_free(s->sys_path);
                s->sys_path = p;
            }
            g_dir_close(d);
        }
        g_free(dir);
    }
    if ((d = g_dir_open(ud, 0, NULL))) {
        while ((f = g_dir_read_name(d))) {
            sitem *s;
            if (!g_str_has_suffix(f, ".desktop")) continue;
            if (!(s = find_item(v, SK_XDG_SYS, f))) { s = g_new0(sitem, 1); s->kind = SK_XDG_USER; s->id = g_strdup(f); g_ptr_array_add(v, s); }
            s->user_path = g_build_filename(ud, f, NULL);
        }
        g_dir_close(d);
    }
    g_free(ud);
    for (i = 0; i < (int)v->len; i++) {
        sitem *s = g_ptr_array_index(v, i);
        GKeyFile *kf = s->user_path ? kf_load(s->user_path) : NULL, *sk = s->sys_path ? kf_load(s->sys_path) : NULL, *eff = kf ? kf : sk;
        char *why = NULL, *te;
        if (s->kind != SK_XDG_SYS && s->kind != SK_XDG_USER) continue;
        s->can = 1;
        if (!eff) { s->name = g_strdup(s->id); s->what = g_strdup("(unreadable)"); s->can = 0; continue; }
        s->name = g_key_file_get_locale_string(sk ? sk : kf, "Desktop Entry", "Name", NULL, NULL);
        if (!s->name) s->name = g_strdup(s->id);
        s->what = g_key_file_get_string(eff, "Desktop Entry", "Exec", NULL);
        s->on = kf_on(eff) && desktop_matches(eff, &why);
        if (kf) {
            s->srs_override = g_key_file_get_boolean(kf, "Desktop Entry", "X-SRS-Override", NULL);
            s->srs_added = g_key_file_get_boolean(kf, "Desktop Entry", "X-SRS-Added", NULL);
        }
        {
            char *op = orig_path(s->id);
            s->has_orig = g_file_test(op, G_FILE_TEST_EXISTS);
            g_free(op);
        }
        if ((te = g_key_file_get_string(eff, "Desktop Entry", "TryExec", NULL)) && *te) {
            char *x = g_path_is_absolute(te) ? (g_file_test(te, G_FILE_TEST_IS_EXECUTABLE) ? g_strdup(te) : NULL) : g_find_program_in_path(te);
            if (!x && !why) why = g_strdup("program not installed (TryExec)");
            g_free(x);
        }
        g_free(te);
        if (s->kind == SK_XDG_SYS && kf)
            s->note = g_strdup_printf("%s%s%s", s->srs_override ? "turned off by SRS (your copy)" : "your copy replaces the system entry",
                                      why ? "; " : "", why ? why : "");
        else if (s->srs_added) s->note = g_strdup_printf("added by SRS%s%s", why ? "; " : "", why ? why : "");
        else s->note = why ? g_strdup(why) : NULL;
        if (why && strstr(why, "ShowIn")) {   /* the desktop would not start it anyway */
            s->can = 0;
            s->on = 0;
        }
        g_free(why);
        s->essential = essential_what(s->id) != NULL;
        if (kf) g_key_file_free(kf);
        if (sk) g_key_file_free(sk);
    }
}

static void sd_scan(GPtrArray *v, int user, char **err)
{
    char *out = NULL, *e = NULL, **ln, **p;
    char *argv_u[] = { "systemctl", "--user", "list-unit-files", "--type=service", "--type=timer", "--type=socket", "--no-legend", "--no-pager", NULL };
    char *argv_s[] = { "systemctl", "list-unit-files", "--type=service", "--type=timer", "--no-legend", "--no-pager", NULL };
    int st = -1;
    if (!g_spawn_sync(NULL, user ? argv_u : argv_s, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &out, &e, &st, NULL) || !g_spawn_check_wait_status(st, NULL)) {
        if (err) *err = g_strdup_printf("%s could not be listed (%s).", user ? "User services" : "System services",
                                        user ? "no systemd user session" : "systemctl is not available");
        g_free(out); g_free(e);
        return;
    }
    ln = g_strsplit(out ? out : "", "\n", -1);
    for (p = ln; *p; p++) {
        char u[256], state[64], preset[64] = "";
        sitem *s;
        if (sscanf(*p, "%255s %63s %63s", u, state, preset) < 2) continue;
        if (strchr(u, '@') && strstr(u, "@.")) continue;   /* templates: their instances are what starts */
        s = g_new0(sitem, 1);
        s->kind = user ? SK_SD_USER : SK_SD_SYS;
        s->id = g_strdup(u);
        s->name = g_strdup(u);
        s->what = g_strdup(state);
        s->on = !strcmp(state, "enabled") || !strcmp(state, "enabled-runtime");
        s->can = !strcmp(state, "enabled") || !strcmp(state, "disabled");
        if (!s->can) s->note = g_strdup_printf("%s: %s", state, !strcmp(state, "static") ? "started by other units, not switched on by itself"
                                              : !strcmp(state, "masked") ? "blocked (masked); SRS leaves it alone"
                                              : !strcmp(state, "indirect") || !strcmp(state, "alias") ? "follows another unit"
                                              : "cannot be switched here");
        else if (*preset) s->note = g_strdup_printf("default (preset): %s", preset);
        s->essential = essential_what(u) != NULL;
        g_ptr_array_add(v, s);
    }
    g_strfreev(ln);
    g_free(out); g_free(e);
}

/* ---- the list ---- */
static gint item_cmp(gconstpointer a, gconstpointer b)
{
    const sitem *x = *(const sitem *const *)a, *y = *(const sitem *const *)b;
    if (x->kind / 2 != y->kind / 2) return x->kind / 2 - y->kind / 2;   /* autostart first, then services */
    return g_utf8_collate(x->name, y->name);
}

static sitem *selected(smgr *m)
{
    GtkTreeModel *mod;
    GtkTreeIter it;
    int idx = -1;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(m->view)), &mod, &it)) return NULL;
    gtk_tree_model_get(mod, &it, COL_IDX, &idx, -1);
    return idx >= 0 && (guint)idx < m->items->len ? g_ptr_array_index(m->items, idx) : NULL;
}

static void fill(smgr *m)
{
    char *e1 = NULL, *e2 = NULL, s[600], *selid = NULL;
    guint i;
    int on = 0;
    GtkAdjustment *va = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(m->view));
    double pos = va ? gtk_adjustment_get_value(va) : 0;
    {   /* keep the selected item and the scroll position across the refill */
        sitem *sel = m->items ? selected(m) : NULL;
        if (sel) selid = g_strdup_printf("%d:%s", sel->kind, sel->id);
    }
    if (m->items) g_ptr_array_free(m->items, TRUE);
    m->items = g_ptr_array_new_with_free_func(sitem_free);
    xdg_scan(m->items);
    sd_scan(m->items, 1, &e1);
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->sys))) sd_scan(m->items, 0, &e2);
    g_ptr_array_sort(m->items, item_cmp);
    m->busy++;
    gtk_list_store_clear(m->store);
    for (i = 0; i < m->items->len; i++) {
        sitem *it = g_ptr_array_index(m->items, i);
        const char *ess = essential_what(it->id);
        char *note = g_strdup_printf("%s%s%s", ess ? "desktop essential: " : "", ess ? ess : "", it->note ? (ess ? "; " : "") : "");
        char *full = g_strdup_printf("%s%s", note, it->note ? it->note : "");
        char *tip = g_strdup_printf("%s\n%s%s%s\n%s", it->id, it->what ? it->what : "", it->sys_path ? "\nSystem file: " : "",
                                    it->sys_path ? it->sys_path : "", it->user_path ? it->user_path : "");
        gtk_list_store_insert_with_values(m->store, NULL, -1, COL_ON, it->on, COL_NAME, it->name, COL_KIND, g_kind_name[it->kind],
                                          COL_STATE, it->kind >= SK_SD_USER ? it->what : it->on ? "on" : "off", COL_NOTE, full,
                                          COL_IDX, (int)i, COL_CAN, it->can, COL_TIP, tip, -1);
        on += it->on;
        g_free(note); g_free(full); g_free(tip);
    }
    m->busy--;
    if (selid) {
        GtkTreeModel *vm = gtk_tree_view_get_model(GTK_TREE_VIEW(m->view));
        GtkTreeIter it;
        gboolean more = gtk_tree_model_get_iter_first(vm, &it);
        while (more) {
            int idx = -1;
            gtk_tree_model_get(vm, &it, COL_IDX, &idx, -1);
            if (idx >= 0) {
                sitem *x = g_ptr_array_index(m->items, idx);
                char *k = g_strdup_printf("%d:%s", x->kind, x->id);
                int same = !strcmp(k, selid);
                g_free(k);
                if (same) { gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(m->view)), &it); break; }
            }
            more = gtk_tree_model_iter_next(vm, &it);
        }
        g_free(selid);
    }
    if (va) gtk_adjustment_set_value(va, pos);
    snprintf(s, sizeof s, "%u items, %d on.%s%s%s%s", m->items->len, on, e1 ? "  " : "", e1 ? e1 : "", e2 ? "  " : "", e2 ? e2 : "");
    status(m, s);
    gtk_widget_set_sensitive(m->undo_btn, m->undos->len > 0);
    g_free(e1); g_free(e2);
}


/* ---- changing things ---- */
static void push_xdg_undo(smgr *m, sitem *s, const char *path)
{
    undo *u = g_new0(undo, 1);
    u->kind = s->kind;
    u->id = g_strdup(s->id);
    u->path = g_strdup(path);
    u->existed = g_file_get_contents(path, &u->before, NULL, NULL);
    g_ptr_array_add(m->undos, u);
}

static int write_kf(GKeyFile *kf, const char *path, char **err)
{
    GError *e = NULL;
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    if (g_key_file_save_to_file(kf, path, &e)) return 1;
    if (err) *err = g_strdup(e ? e->message : "write failed");
    g_clear_error(&e);
    return 0;
}

static int xdg_set(smgr *m, sitem *s, int on, char **err)
{
    char *ud = user_dir(), *path = g_build_filename(ud, s->id, NULL);
    GKeyFile *kf;
    int ok = 0;
    g_free(ud);
    if (s->kind == SK_XDG_SYS && on && s->user_path && s->srs_override) {   /* our copy: move it away */
        push_xdg_undo(m, s, path);
        ok = to_backup(path);
        if (!ok && err) *err = g_strdup("could not move the user copy to the backup folder");
        g_free(path);
        return ok;
    }
    push_xdg_undo(m, s, path);
    if (s->user_path) {
        if (!(kf = kf_load(path))) { if (err) *err = g_strdup("cannot read the file"); g_free(path); return 0; }
        if (!s->srs_override && !s->srs_added && !s->has_orig) {   /* keep the original once */
            char *op = orig_path(s->id), *txt = NULL;
            if (g_file_get_contents(path, &txt, NULL, NULL)) g_file_set_contents(op, txt, -1, NULL);
            g_free(txt); g_free(op);
        }
    } else {   /* a system entry with no user copy yet: copy it, turned off */
        if (!(kf = kf_load(s->sys_path))) { if (err) *err = g_strdup("cannot read the system file"); g_free(path); return 0; }
        g_key_file_set_boolean(kf, "Desktop Entry", "X-SRS-Override", TRUE);
        g_key_file_set_comment(kf, "Desktop Entry", NULL, " Copy made by SRS to turn this system autostart entry off for this user. "
                               "SRS's Startup tab (or deleting this file) turns it back on.", NULL);
    }
    g_key_file_set_boolean(kf, "Desktop Entry", "Hidden", !on);
    if (on && g_key_file_has_key(kf, "Desktop Entry", "X-GNOME-Autostart-enabled", NULL))
        g_key_file_set_boolean(kf, "Desktop Entry", "X-GNOME-Autostart-enabled", TRUE);
    ok = write_kf(kf, path, err);
    g_key_file_free(kf);
    g_free(path);
    return ok;
}

static int sd_set(smgr *m, sitem *s, const char *verb, char **err)
{
    char *argv[8];
    int n = 0, ok;
    if (s->kind == SK_SD_SYS) argv[n++] = "pkexec";
    argv[n++] = "systemctl";
    if (s->kind == SK_SD_USER) argv[n++] = "--user";
    argv[n++] = (char *)verb;
    argv[n++] = s->id;
    argv[n] = NULL;
    ok = run_cmd(m, argv, err);
    return ok;
}

static int confirm_off(smgr *m, sitem *s)
{
    const char *ess = essential_what(s->id);
    char t[1200];
    if (s->kind == SK_SD_SYS) {
        snprintf(t, sizeof t, "Turn the system service %s %s?\n\n%s%sSystem services start for every user when the PC boots. "
                 "This asks for the administrator password (pkexec) and changes it from the next boot on. "
                 "Turn it back on here, or with: sudo systemctl enable %s",
                 s->id, s->on ? "off" : "on", ess ? "It looks like part of the system: " : "", ess ? ess : "", s->id);
        if (ess) g_strlcat(t, "\n\nTurning it off can leave the PC without that function, or not starting properly.", sizeof t);
        return ask(m, ess ? GTK_MESSAGE_WARNING : GTK_MESSAGE_QUESTION, s->on ? "Turn _off" : "Turn _on", t);
    }
    if (s->on && ess) {
        snprintf(t, sizeof t, "%s looks like part of the desktop: %s.\n\nTurning it off can break that (no sound, no password prompts, "
                 "passwords not unlocked, file dialogs or screen sharing missing ...) from the next login on. "
                 "You can turn it back on here at any time.\n\nTurn it off anyway?", s->name, ess);
        return ask(m, GTK_MESSAGE_WARNING, "Turn _off anyway", t);
    }
    return 1;
}

static void apply(smgr *m, sitem *s, int on)
{
    char *err = NULL;
    int ok;
    if (!s || !s->can || on == s->on) return;
    if (!confirm_off(m, s)) { fill(m); return; }
    if (s->kind <= SK_XDG_SYS) ok = xdg_set(m, s, on, &err);
    else {
        int was = s->on;
        ok = sd_set(m, s, on ? "enable" : "disable", &err);
        if (ok) { undo *u = g_new0(undo, 1); u->kind = s->kind; u->id = g_strdup(s->id); u->was_on = was; g_ptr_array_add(m->undos, u); }
    }
    if (!ok) {
        char *t = g_strdup_printf("Could not change %s:\n%s", s->name, err ? err : "unknown error");
        tell(m, GTK_MESSAGE_WARNING, t);
        g_free(t);
    } else if (!dry_run() || s->kind <= SK_XDG_SYS) {
        char *t = g_strdup_printf("%s turned %s. It takes effect at the next login%s. Undo puts it back.", s->name, on ? "on" : "off",
                                  s->kind == SK_SD_SYS ? " / boot" : "");
        status(m, t);
        g_free(t);
    }
    g_free(err);
    {
        GtkWidget *keep = m->status;
        char *st = g_strdup(gtk_label_get_text(GTK_LABEL(keep)));
        fill(m);
        status(m, st);
        g_free(st);
    }
}

static void on_toggle(GtkCellRendererToggle *r, char *path, gpointer d)
{
    smgr *m = d;
    GtkTreeIter it;
    int idx = -1, can = 0;
    GtkTreeModel *mod = gtk_tree_view_get_model(GTK_TREE_VIEW(m->view));
    (void)r;
    if (m->busy || !gtk_tree_model_get_iter_from_string(mod, &it, path)) return;
    gtk_tree_model_get(mod, &it, COL_IDX, &idx, COL_CAN, &can, -1);
    if (can && idx >= 0 && (guint)idx < m->items->len) {
        sitem *s = g_ptr_array_index(m->items, idx);
        apply(m, s, !s->on);
    }
}

static void on_undo(GtkButton *b, gpointer d)
{
    smgr *m = d;
    undo *u;
    char *err = NULL, msg[400];
    int ok = 1;
    (void)b;
    if (!m->undos->len) return;
    u = g_ptr_array_index(m->undos, m->undos->len - 1);
    if (u->kind <= SK_XDG_SYS) {
        if (u->existed) ok = g_file_set_contents(u->path, u->before, -1, NULL);
        else if (g_file_test(u->path, G_FILE_TEST_EXISTS)) ok = to_backup(u->path);
    } else {
        sitem s = { 0 };
        s.kind = u->kind;
        s.id = u->id;
        ok = sd_set(m, &s, u->was_on ? "enable" : "disable", &err);
    }
    snprintf(msg, sizeof msg, ok ? "Undone: %s is back as it was." : "Could not undo the change to %s.", u->id);
    if (!ok && err) tell(m, GTK_MESSAGE_WARNING, err);
    g_free(err);
    const int ukind = u->kind;
    g_ptr_array_remove_index(m->undos, m->undos->len - 1);
    fill(m);
    if (!dry_run() || ukind <= SK_XDG_SYS) status(m, msg);
}

static void on_restore(GtkButton *b, gpointer d)
{
    smgr *m = d;
    sitem *s = selected(m);
    char *err = NULL, t[800];
    int ok = 1;
    (void)b;
    if (!s) { status(m, "Pick an item in the list first."); return; }
    if (s->kind == SK_XDG_SYS) {
        if (!s->user_path) { status(m, "This item already uses the system's own entry (its default)."); return; }
        snprintf(t, sizeof t, "Put %s back to the system's own entry?\n\nYour copy (%s)%s moves to the backup folder "
                 "~/.config/srs/startup-backup; nothing is deleted.", s->name, s->user_path,
                 s->srs_override ? ", made by SRS," : ", which you or another program made,");
        if (!ask(m, GTK_MESSAGE_QUESTION, "_Restore default", t)) return;
        push_xdg_undo(m, s, s->user_path);
        ok = to_backup(s->user_path);
    } else if (s->kind == SK_XDG_USER) {
        char *op = orig_path(s->id);
        if (s->srs_added) {
            snprintf(t, sizeof t, "%s was added by SRS. Take it out of the startup list?\n\nThe file moves to the backup folder "
                     "~/.config/srs/startup-backup; nothing is deleted.", s->name);
            if (ask(m, GTK_MESSAGE_QUESTION, "_Take out", t)) { push_xdg_undo(m, s, s->user_path); ok = to_backup(s->user_path); }
            else { g_free(op); return; }
        } else if (g_file_test(op, G_FILE_TEST_EXISTS)) {
            char *txt = NULL;
            push_xdg_undo(m, s, s->user_path);
            ok = g_file_get_contents(op, &txt, NULL, NULL) && g_file_set_contents(s->user_path, txt, -1, NULL);
            g_free(txt);
        } else {
            status(m, "SRS has not changed this entry, so it is as it was.");
            g_free(op);
            return;
        }
        g_free(op);
    } else {
        if (s->kind == SK_SD_SYS) {
            snprintf(t, sizeof t, "Put the system service %s back to the distribution's default (systemctl preset)? "
                     "This asks for the administrator password.", s->id);
            if (!ask(m, GTK_MESSAGE_QUESTION, "_Restore default", t)) return;
        }
        {
            int was = s->on;
            ok = sd_set(m, s, "preset", &err);
            if (ok) { undo *u = g_new0(undo, 1); u->kind = s->kind; u->id = g_strdup(s->id); u->was_on = was; g_ptr_array_add(m->undos, u); }
        }
    }
    if (!ok) { snprintf(t, sizeof t, "Could not restore %s.\n%s", s->name, err ? err : ""); tell(m, GTK_MESSAGE_WARNING, t); }
    g_free(err);
    snprintf(t, sizeof t, "%s is back to its default.", s->name);
    const int skind = s->kind;
    fill(m);
    if (ok && (!dry_run() || skind <= SK_XDG_SYS)) status(m, t);
}

static void on_refresh(GtkButton *b, gpointer d) { (void)b; fill(d); }
static void on_sys(GtkToggleButton *b, gpointer d) { (void)b; fill(d); }
static void on_filter(GtkWidget *w, gpointer d) { (void)w; gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(((smgr *)d)->fmodel)); }

static gboolean visible(GtkTreeModel *mod, GtkTreeIter *it, gpointer d)
{
    smgr *m = d;
    char *name = NULL, *note = NULL, *kind = NULL, *hay, *low;
    const char *txt = gtk_entry_get_text(GTK_ENTRY(m->filter));
    int k = gtk_combo_box_get_active(GTK_COMBO_BOX(m->kindsel)), idx = -1;
    gboolean ok = TRUE;
    gtk_tree_model_get(mod, it, COL_NAME, &name, COL_NOTE, &note, COL_KIND, &kind, COL_IDX, &idx, -1);
    if (k > 0 && idx >= 0 && m->items && (guint)idx < m->items->len) {
        sitem *s = g_ptr_array_index(m->items, idx);
        ok = k == 1 ? s->kind <= SK_XDG_SYS : k == 2 ? s->kind == SK_SD_USER : k == 3 ? s->kind == SK_SD_SYS : TRUE;
    }
    if (ok && *txt) {
        hay = g_strdup_printf("%s %s %s %s", name ? name : "", note ? note : "", kind ? kind : "",
                              idx >= 0 && m->items && (guint)idx < m->items->len ? ((sitem *)g_ptr_array_index(m->items, idx))->id : "");
        low = g_utf8_strdown(hay, -1);
        {
            char *t = g_utf8_strdown(txt, -1);
            ok = strstr(low, t) != NULL;
            g_free(t);
        }
        g_free(hay);
        g_free(low);
    }
    g_free(name); g_free(note); g_free(kind);
    return ok;
}

/* ---- Add to startup ---- */
typedef struct { GtkWidget *dlg, *r_app, *r_cmd, *search, *list, *cmd, *name, *delay; } adddlg;

static gboolean app_filter(GtkListBoxRow *row, gpointer d)
{
    adddlg *a = d;
    const char *hay = g_object_get_data(G_OBJECT(row), "hay");
    char *t = g_utf8_strdown(gtk_entry_get_text(GTK_ENTRY(a->search)), -1);
    gboolean ok = !*t || (hay && strstr(hay, t));
    g_free(t);
    return ok;
}
static void app_search(GtkSearchEntry *e, gpointer d) { (void)e; gtk_list_box_invalidate_filter(GTK_LIST_BOX(((adddlg *)d)->list)); }
static void app_pick(GtkListBox *lb, GtkListBoxRow *row, gpointer d)
{
    adddlg *a = d;
    GAppInfo *ai = row ? g_object_get_data(G_OBJECT(row), "app") : NULL;
    (void)lb;
    if (!ai) return;
    gtk_entry_set_text(GTK_ENTRY(a->name), g_app_info_get_display_name(ai));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(a->r_app), TRUE);
}
static gint app_cmp(gconstpointer x, gconstpointer y) { return g_utf8_collate(g_app_info_get_display_name((GAppInfo *)x), g_app_info_get_display_name((GAppInfo *)y)); }

/* an Exec argument with the desktop entry's quoting: "..." with " ` $ \ escaped */
static char *exec_quote(const char *s)
{
    GString *g = g_string_new("\"");
    for (; *s; s++) {
        if (strchr("\"`$\\", *s)) g_string_append_c(g, '\\');
        if (*s == '%') g_string_append_c(g, '%');   /* a literal % is %% in Exec */
        g_string_append_c(g, *s);
    }
    g_string_append_c(g, '"');
    return g_string_free(g, FALSE);
}

/* Exec without the field codes (%f %U ...), which mean nothing at login */
static char *strip_codes(const char *e)
{
    GString *g = g_string_new(NULL);
    for (; e && *e; e++) {
        if (*e == '%' && e[1]) { if (e[1] == '%') g_string_append(g, "%%"); e++; continue; }
        g_string_append_c(g, *e);
    }
    return g_strstrip(g_string_free(g, FALSE));
}

static void add_dialog(smgr *m)
{
    adddlg a;
    GtkWidget *ca, *sw, *grid, *l;
    GList *apps, *p;
    memset(&a, 0, sizeof a);
    a.dlg = gtk_dialog_new_with_buttons("Add to startup", top(m), GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_Add", GTK_RESPONSE_ACCEPT, NULL);
    gtk_window_set_default_size(GTK_WINDOW(a.dlg), 520, 600);
    ca = gtk_dialog_get_content_area(GTK_DIALOG(a.dlg));
    gtk_container_set_border_width(GTK_CONTAINER(ca), 12);
    gtk_box_set_spacing(GTK_BOX(ca), 8);
    l = gtk_label_new("Start a program, or a command, every time you log in. You can turn it off again in this list "
                      "(Restore default takes it out).");
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_box_pack_start(GTK_BOX(ca), l, FALSE, FALSE, 0);
    a.r_app = gtk_radio_button_new_with_mnemonic(NULL, "An installed _program:");
    gtk_box_pack_start(GTK_BOX(ca), a.r_app, FALSE, FALSE, 0);
    a.search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(a.search), "Find a program");
    g_signal_connect(a.search, "search-changed", G_CALLBACK(app_search), &a);
    gtk_box_pack_start(GTK_BOX(ca), a.search, FALSE, FALSE, 0);
    a.list = gtk_list_box_new();
    gtk_list_box_set_filter_func(GTK_LIST_BOX(a.list), app_filter, &a, NULL);
    g_signal_connect(a.list, "row-selected", G_CALLBACK(app_pick), &a);
    apps = g_list_sort(g_app_info_get_all(), app_cmp);
    for (p = apps; p; p = p->next) {
        GAppInfo *ai = p->data;
        GtkWidget *row, *bx, *lab;
        GIcon *ic;
        char *hay;
        if (!g_app_info_should_show(ai) || !g_app_info_get_commandline(ai)) continue;
        bx = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        ic = g_app_info_get_icon(ai);
        {
            GtkWidget *img = ic ? gtk_image_new_from_gicon(ic, GTK_ICON_SIZE_MENU) : gtk_image_new();
            gtk_image_set_pixel_size(GTK_IMAGE(img), 16);   /* file icons come at their own size otherwise */
            gtk_widget_set_size_request(img, 16, 16);
            gtk_box_pack_start(GTK_BOX(bx), img, FALSE, FALSE, 4);
        }
        lab = gtk_label_new(g_app_info_get_display_name(ai));
        gtk_label_set_xalign(GTK_LABEL(lab), 0);
        gtk_box_pack_start(GTK_BOX(bx), lab, TRUE, TRUE, 0);
        row = gtk_list_box_row_new();
        gtk_container_add(GTK_CONTAINER(row), bx);
        g_object_set_data_full(G_OBJECT(row), "app", g_object_ref(ai), g_object_unref);
        hay = g_utf8_strdown(g_app_info_get_display_name(ai), -1);
        g_object_set_data_full(G_OBJECT(row), "hay", hay, g_free);
        gtk_list_box_insert(GTK_LIST_BOX(a.list), row, -1);
    }
    g_list_free_full(apps, g_object_unref);
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sw), 220);
    gtk_container_add(GTK_CONTAINER(sw), a.list);
    gtk_box_pack_start(GTK_BOX(ca), sw, TRUE, TRUE, 0);
    a.r_cmd = gtk_radio_button_new_with_mnemonic_from_widget(GTK_RADIO_BUTTON(a.r_app), "A _command (run by sh):");
    gtk_box_pack_start(GTK_BOX(ca), a.r_cmd, FALSE, FALSE, 0);
    a.cmd = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(a.cmd), "for example: conky -c ~/.conkyrc");
    gtk_box_pack_start(GTK_BOX(ca), a.cmd, FALSE, FALSE, 0);
    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new_with_mnemonic("_Name:"), 0, 0, 1, 1);
    a.name = gtk_entry_new();
    gtk_widget_set_hexpand(a.name, TRUE);
    gtk_grid_attach(GTK_GRID(grid), a.name, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new_with_mnemonic("_Delay:"), 0, 1, 1, 1);
    a.delay = gtk_spin_button_new_with_range(0, 600, 5);
    gtk_widget_set_tooltip_text(a.delay, "Seconds to wait after login before it starts (0 = straight away)");
    {
        GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(h), a.delay, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(h), gtk_label_new("seconds after login"), FALSE, FALSE, 0);
        gtk_grid_attach(GTK_GRID(grid), h, 1, 1, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(ca), grid, FALSE, FALSE, 0);
    gtk_widget_show_all(a.dlg);
    while (gtk_dialog_run(GTK_DIALOG(a.dlg)) == GTK_RESPONSE_ACCEPT) {
        int is_app = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(a.r_app)), delay = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a.delay));
        GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(a.list));
        GAppInfo *ai = is_app && row ? g_object_get_data(G_OBJECT(row), "app") : NULL;
        const char *cmd = gtk_entry_get_text(GTK_ENTRY(a.cmd));
        char *name = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(a.name)))), *base, *exec, *slug, *p2, *ud, *path, *err = NULL;
        GKeyFile *kf;
        sitem fake = { 0 };
        if (is_app && !ai) { g_free(name); tell(m, GTK_MESSAGE_INFO, "Pick a program in the list, or choose A command."); continue; }
        if (!is_app && !cmd[strspn(cmd, " \t\r\n")]) { g_free(name); tell(m, GTK_MESSAGE_INFO, "Type the command to run."); continue; }
        if (!*name) { g_free(name); name = g_strdup(ai ? g_app_info_get_display_name(ai) : cmd); }
        base = ai ? strip_codes(g_app_info_get_commandline(ai)) : NULL;
        if (!ai || delay > 0) {   /* through sh: a command line, or a pause first */
            char *inner = delay > 0 ? g_strdup_printf("sleep %d; exec %s", delay, ai ? base : cmd) : g_strdup(cmd);
            char *q;
            if (ai) {   /* base is already Exec text: undo its %% before quoting again */
                char *x = inner, *o = inner;
                for (; *x; x++) { if (x[0] == '%' && x[1] == '%') x++; *o++ = *x; }
                *o = 0;
            }
            q = exec_quote(inner);
            exec = g_strdup_printf("sh -c %s", q);
            g_free(q);
            g_free(inner);
        } else exec = g_strdup(base);
        slug = g_ascii_strdown(name, -1);
        for (p2 = slug; *p2; p2++) if (!g_ascii_isalnum(*p2)) *p2 = '-';
        ud = user_dir();
        p2 = g_strdup_printf("srs-%s.desktop", slug);
        path = g_build_filename(ud, p2, NULL);
        {
            int k = 2;
            while (g_file_test(path, G_FILE_TEST_EXISTS)) { g_free(path); g_free(p2); p2 = g_strdup_printf("srs-%s-%d.desktop", slug, k++); path = g_build_filename(ud, p2, NULL); }
        }
        kf = g_key_file_new();
        g_key_file_set_string(kf, "Desktop Entry", "Type", "Application");
        g_key_file_set_string(kf, "Desktop Entry", "Name", name);
        g_key_file_set_string(kf, "Desktop Entry", "Exec", exec);
        if (ai && g_app_info_get_icon(ai)) { char *ic = g_icon_to_string(g_app_info_get_icon(ai)); if (ic) g_key_file_set_string(kf, "Desktop Entry", "Icon", ic); g_free(ic); }
        g_key_file_set_string(kf, "Desktop Entry", "Comment", delay > 0 ? "Added to startup by SRS (starts after a delay)" : "Added to startup by SRS");
        g_key_file_set_boolean(kf, "Desktop Entry", "Terminal", FALSE);
        g_key_file_set_boolean(kf, "Desktop Entry", "X-GNOME-Autostart-enabled", TRUE);
        g_key_file_set_boolean(kf, "Desktop Entry", "X-SRS-Added", TRUE);
        fake.kind = SK_XDG_USER;
        fake.id = p2;
        push_xdg_undo(m, &fake, path);
        if (write_kf(kf, path, &err)) {
            const char *h = g_get_home_dir();
            size_t hl = strlen(h);
            char *t = g_strdup_printf("%s added to startup (%s%s). It starts at the next login.", name,
                                      !strncmp(path, h, hl) && path[hl] == '/' ? "~" : "", !strncmp(path, h, hl) && path[hl] == '/' ? path + hl : path);
            fill(m);
            status(m, t);
            g_free(t);
        } else {
            tell(m, GTK_MESSAGE_WARNING, err ? err : "Could not write the file.");
            g_ptr_array_remove_index(m->undos, m->undos->len - 1);
        }
        g_key_file_free(kf);
        g_free(err); g_free(ud); g_free(p2); g_free(path); g_free(slug); g_free(exec); g_free(base); g_free(name);
        break;
    }
    gtk_widget_destroy(a.dlg);
}

static void on_add(GtkButton *b, gpointer d) { (void)b; add_dialog(d); }

static void smgr_free(gpointer d)
{
    smgr *m = d;
    if (m->items) g_ptr_array_free(m->items, TRUE);
    if (m->undos) g_ptr_array_free(m->undos, TRUE);
    g_free(m);
}

static void cell_dim(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *mod, GtkTreeIter *it, gpointer d)
{
    gboolean can = TRUE, on = TRUE;
    (void)c; (void)d;
    gtk_tree_model_get(mod, it, COL_CAN, &can, COL_ON, &on, -1);
    g_object_set(r, "sensitive", can, NULL);
    if (GTK_IS_CELL_RENDERER_TEXT(r)) g_object_set(r, "foreground-rgba", NULL, "style", on ? PANGO_STYLE_NORMAL : PANGO_STYLE_ITALIC, NULL);
}

static void on_map_fill(GtkWidget *w, gpointer d)
{
    smgr *m = d;
    (void)w;
    if (!m->items) fill(m);   /* first shown: read the lists */
}

GtkWidget *startup_manager_new(void)
{
    smgr *m = g_new0(smgr, 1);
    GtkWidget *bar, *b, *sw, *intro;
    GtkCellRenderer *r;
    GtkTreeViewColumn *c;
    static const char *const heads[] = { "Name", "Kind", "State", "Note" };
    int i;
    m->undos = g_ptr_array_new_with_free_func(undo_free);
    m->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(m->box), 8);
    g_object_set_data_full(G_OBJECT(m->box), "smgr", m, smgr_free);
    intro = gtk_label_new("What starts when you log in or the PC boots. Switch an item off or on: nothing is deleted "
                          "(a system entry gets a turned-off copy in your autostart folder; backups go to "
                          "~/.config/srs/startup-backup). Undo puts back the last change; Restore default the item's default. "
                          "Changes take effect at the next login.");
    gtk_label_set_line_wrap(GTK_LABEL(intro), TRUE);
    gtk_label_set_xalign(GTK_LABEL(intro), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(intro), "dim-label");
    gtk_box_pack_start(GTK_BOX(m->box), intro, FALSE, FALSE, 0);

    bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    m->filter = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(m->filter), "Filter");
    g_signal_connect(m->filter, "search-changed", G_CALLBACK(on_filter), m);
    gtk_box_pack_start(GTK_BOX(bar), m->filter, FALSE, FALSE, 0);
    m->kindsel = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->kindsel), "Everything");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->kindsel), "Desktop autostart");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->kindsel), "Services (user)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->kindsel), "Services (system)");
    gtk_combo_box_set_active(GTK_COMBO_BOX(m->kindsel), 0);
    g_signal_connect(m->kindsel, "changed", G_CALLBACK(on_filter), m);
    gtk_box_pack_start(GTK_BOX(bar), m->kindsel, FALSE, FALSE, 0);
    m->sys = gtk_check_button_new_with_mnemonic("Show s_ystem services");
    gtk_widget_set_tooltip_text(m->sys, "Services that start for every user at boot. Changing them asks for the administrator password.");
    g_signal_connect(m->sys, "toggled", G_CALLBACK(on_sys), m);
    gtk_box_pack_start(GTK_BOX(bar), m->sys, FALSE, FALSE, 0);
    b = gtk_button_new_with_mnemonic("_Add to startup...");
    gtk_widget_set_tooltip_text(b, "Start an installed program or a command when you log in, with an optional delay");
    g_signal_connect(b, "clicked", G_CALLBACK(on_add), m);
    gtk_box_pack_end(GTK_BOX(bar), b, FALSE, FALSE, 0);
    m->restore_btn = gtk_button_new_with_mnemonic("Restore _default");
    gtk_widget_set_tooltip_text(m->restore_btn, "Put the selected item back to its default: the system's own entry, the original file, "
                                                "or the distribution's preset for a service");
    g_signal_connect(m->restore_btn, "clicked", G_CALLBACK(on_restore), m);
    gtk_box_pack_end(GTK_BOX(bar), m->restore_btn, FALSE, FALSE, 0);
    m->undo_btn = gtk_button_new_with_mnemonic("_Undo");
    gtk_widget_set_tooltip_text(m->undo_btn, "Put back the last change made here");
    g_signal_connect(m->undo_btn, "clicked", G_CALLBACK(on_undo), m);
    gtk_box_pack_end(GTK_BOX(bar), m->undo_btn, FALSE, FALSE, 0);
    b = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(b, "Read the lists again");
    g_signal_connect(b, "clicked", G_CALLBACK(on_refresh), m);
    gtk_box_pack_end(GTK_BOX(bar), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(m->box), bar, FALSE, FALSE, 0);

    m->store = gtk_list_store_new(NCOL, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_BOOLEAN, G_TYPE_STRING);
    m->fmodel = gtk_tree_model_filter_new(GTK_TREE_MODEL(m->store), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(m->fmodel), visible, m, NULL);
    {
        GtkTreeModel *sorted = gtk_tree_model_sort_new_with_model(m->fmodel);
        m->view = gtk_tree_view_new_with_model(sorted);
        g_object_unref(sorted);
    }
    g_object_unref(m->fmodel);
    g_object_unref(m->store);
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(m->view), COL_TIP);
    r = gtk_cell_renderer_toggle_new();
    g_signal_connect(r, "toggled", G_CALLBACK(on_toggle), m);
    c = gtk_tree_view_column_new_with_attributes("On", r, "active", COL_ON, "activatable", COL_CAN, NULL);
    gtk_tree_view_column_set_cell_data_func(c, r, cell_dim, NULL, NULL);
    gtk_tree_view_column_set_sort_column_id(c, COL_ON);
    gtk_tree_view_append_column(GTK_TREE_VIEW(m->view), c);
    for (i = 0; i < 4; i++) {
        r = gtk_cell_renderer_text_new();
        if (i == 3) g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
        c = gtk_tree_view_column_new_with_attributes(heads[i], r, "text", COL_NAME + i, NULL);
        gtk_tree_view_column_set_cell_data_func(c, r, cell_dim, NULL, NULL);
        gtk_tree_view_column_set_sort_column_id(c, COL_NAME + i);
        gtk_tree_view_column_set_resizable(c, TRUE);
        gtk_tree_view_column_set_expand(c, i == 3);
        if (i == 0) gtk_tree_view_column_set_min_width(c, 220);
        gtk_tree_view_append_column(GTK_TREE_VIEW(m->view), c);
    }
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(m->view), COL_NAME);
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sw), m->view);
    gtk_box_pack_start(GTK_BOX(m->box), sw, TRUE, TRUE, 0);
    m->status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(m->status), 0);
    gtk_label_set_ellipsize(GTK_LABEL(m->status), PANGO_ELLIPSIZE_END);
    gtk_label_set_selectable(GTK_LABEL(m->status), TRUE);
    gtk_box_pack_start(GTK_BOX(m->box), m->status, FALSE, FALSE, 0);
    g_signal_connect(m->box, "map", G_CALLBACK(on_map_fill), m);
    gtk_widget_set_sensitive(m->undo_btn, FALSE);
    return m->box;
}
