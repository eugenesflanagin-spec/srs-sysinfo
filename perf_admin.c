/* perf_admin.c - the Startup Apps, Users and Services pages of the Performance tab, and Run New Task.
 *
 * Startup Apps: everything that starts at login (perf_autostart.c), with one-click on/off, Add (an
 * installed application, a program or script, or a typed command), Remove (to a restorable trash),
 * Properties, Undo, Restore Removed, Open File Location and Copy Command. Only the user's own files
 * change; after every action the list is read again from disk and shows what really happened.
 * Users: logind sessions (loginctl) with their processes; Disconnect = loginctl terminate-session,
 * never for the session SRS runs in, always after a confirmation.
 * Services: systemd units (system, or the user's with "User Services"); Start / Stop / Restart run
 * systemctl asynchronously after a confirmation (polkit asks for the password for system units).
 * Test hooks: SRS_TEST_SYSTEMCTL / SRS_TEST_LOGINCTL replace the two programs.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "perf_admin.h"
#include "perf_autostart.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EM "—"

/* ------------------------------------------------------------------ shared bits */

static GtkWidget *mk_label(const char *t, float xa, const char *cls)
{
    GtkWidget *l = gtk_label_new(t);
    gtk_label_set_xalign(GTK_LABEL(l), xa);
    if (cls) gtk_style_context_add_class(gtk_widget_get_style_context(l), cls);
    return l;
}

static GtkWidget *mk_button(const char *mnemonic, const char *tip)
{
    GtkWidget *b = gtk_button_new_with_mnemonic(mnemonic);
    if (tip) gtk_widget_set_tooltip_text(b, tip);
    return b;
}

static GtkWidget *icon_button(const char *icon, const char *tip)
{
    GtkWidget *b = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(b, tip);
    return b;
}

static GtkWindow *top_of(GtkWidget *w)
{
    GtkWidget *t = w ? gtk_widget_get_toplevel(w) : NULL;
    return t && GTK_IS_WINDOW(t) ? GTK_WINDOW(t) : NULL;
}

/* an info bar: msg_type GTK_MESSAGE_ERROR / INFO; hidden again with its close button */
typedef struct { GtkWidget *bar, *label; } infobar;

static void ib_response(GtkInfoBar *b, gint r, gpointer d) { (void)r; (void)d; gtk_widget_hide(GTK_WIDGET(b)); }

static void ib_make(infobar *ib)
{
    ib->bar = gtk_info_bar_new();
    ib->label = mk_label("", 0, NULL);
    gtk_label_set_line_wrap(GTK_LABEL(ib->label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(ib->label), TRUE);
    gtk_container_add(GTK_CONTAINER(gtk_info_bar_get_content_area(GTK_INFO_BAR(ib->bar))), ib->label);
    gtk_info_bar_set_show_close_button(GTK_INFO_BAR(ib->bar), TRUE);
    g_signal_connect(ib->bar, "response", G_CALLBACK(ib_response), NULL);
    gtk_widget_set_no_show_all(ib->bar, TRUE);
    gtk_widget_show(ib->label);
}

static void ib_show(infobar *ib, GtkMessageType t, const char *msg)
{
    gtk_info_bar_set_message_type(GTK_INFO_BAR(ib->bar), t);
    gtk_label_set_text(GTK_LABEL(ib->label), msg);
    gtk_widget_show(ib->bar);
}

static GtkWidget *header(const char *title, GtkWidget **right)
{
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(h), mk_label(title, 0, "pv-title"), FALSE, FALSE, 0);
    *right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_end(GTK_BOX(h), *right, FALSE, FALSE, 0);
    return h;
}

static int confirm(GtkWidget *parent, const char *title, const char *detail, const char *warn, const char *ok_label)
{
    GtkWidget *d = gtk_message_dialog_new(top_of(parent), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                          warn ? GTK_MESSAGE_WARNING : GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", title);
    int r;
    if (detail && warn) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s\n\n%s", detail, warn);
    else if (detail || warn) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", detail ? detail : warn);
    gtk_dialog_add_button(GTK_DIALOG(d), "_Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(d), ok_label, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_CANCEL);
    r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    return r == GTK_RESPONSE_OK;
}

static void message(GtkWidget *parent, GtkMessageType t, const char *title, const char *detail)
{
    GtkWidget *d = gtk_message_dialog_new(top_of(parent), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, t, GTK_BUTTONS_CLOSE, "%s", title);
    if (detail) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", detail);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

static const char *prog_systemctl(void) { const char *p = getenv("SRS_TEST_SYSTEMCTL"); return p && *p ? p : "systemctl"; }
static const char *prog_loginctl(void) { const char *p = getenv("SRS_TEST_LOGINCTL"); return p && *p ? p : "loginctl"; }

/* ------------------------------------------------------------------ Run New Task */

static int needs_shell(const char *s)
{
    return strpbrk(s, "|&;<>()$`*?[]#~{}\n") != NULL;
}

static gboolean run_task(const char *text, char **why)
{
    char **argv = NULL, *t = g_strstrip(g_strdup(text));
    GError *e = NULL;
    gboolean ok;
    if (!*t) { g_free(t); *why = g_strdup("Nothing to run."); return FALSE; }
    /* a folder or a document opens in its default application */
    if (t[0] == '/' && g_file_test(t, G_FILE_TEST_EXISTS) && !g_file_test(t, G_FILE_TEST_IS_EXECUTABLE | G_FILE_TEST_IS_REGULAR)) {
        char *uri = g_filename_to_uri(t, NULL, NULL);
        ok = uri && g_app_info_launch_default_for_uri(uri, NULL, &e);
        g_free(uri);
        if (!ok) *why = g_strdup(e ? e->message : "Cannot open it.");
        if (e) g_error_free(e);
        g_free(t);
        return ok;
    }
    if (needs_shell(t)) {
        argv = g_new0(char *, 4);
        argv[0] = g_strdup("/bin/sh");
        argv[1] = g_strdup("-c");
        argv[2] = g_strdup(t);
    } else if (!g_shell_parse_argv(t, NULL, &argv, &e)) {
        *why = g_strdup(e ? e->message : "The command cannot be read.");
        if (e) g_error_free(e);
        g_free(t);
        return FALSE;
    }
    /* no G_SPAWN_DO_NOT_REAP_CHILD: GLib detaches the child, it keeps running when SRS closes */
    ok = g_spawn_async(g_get_home_dir(), argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDIN_FROM_DEV_NULL, NULL, NULL, NULL, &e);
    if (!ok) *why = g_strdup(e ? e->message : "Cannot start it.");
    if (e) g_error_free(e);
    g_strfreev(argv);
    g_free(t);
    return ok;
}

static void rt_browse(GtkButton *b, gpointer entry)
{
    GtkWidget *d = gtk_file_chooser_dialog_new("Choose a Program or File", top_of(GTK_WIDGET(b)), GTK_FILE_CHOOSER_ACTION_OPEN,
                                               "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
        char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d)), *q = g_shell_quote(f);
        gtk_entry_set_text(GTK_ENTRY(entry), g_file_test(f, G_FILE_TEST_IS_EXECUTABLE) ? q : f);
        g_free(q);
        g_free(f);
    }
    gtk_widget_destroy(d);
}

void pa_run_new_task(GtkWindow *parent)
{
    GtkWidget *d = gtk_dialog_new_with_buttons("Run New Task", parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               "_Cancel", GTK_RESPONSE_CANCEL, "_Run", GTK_RESPONSE_OK, NULL);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(d)), *h, *e, *b;
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_set_spacing(GTK_BOX(box), 8);
    gtk_box_pack_start(GTK_BOX(box), mk_label("Type a program, command, folder or document, and SRS starts it for you\n"
                                              "(as you, with your normal environment; it keeps running when SRS closes).", 0, NULL),
                       FALSE, FALSE, 0);
    h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(h), mk_label("Open:", 0, NULL), FALSE, FALSE, 0);
    e = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(e), 44);
    gtk_entry_set_activates_default(GTK_ENTRY(e), TRUE);
    gtk_box_pack_start(GTK_BOX(h), e, TRUE, TRUE, 0);
    b = gtk_button_new_with_mnemonic("_Browse…");
    g_signal_connect(b, "clicked", G_CALLBACK(rt_browse), e);
    gtk_box_pack_start(GTK_BOX(h), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), h, FALSE, FALSE, 0);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_widget_show_all(d);
    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        char *why = NULL;
        if (run_task(gtk_entry_get_text(GTK_ENTRY(e)), &why)) break;
        message(d, GTK_MESSAGE_ERROR, "Could not start it", why);
        g_free(why);
    }
    gtk_widget_destroy(d);
}

static void on_run_task(GtkButton *b, gpointer d) { (void)d; pa_run_new_task(top_of(GTK_WIDGET(b))); }

static GtkWidget *run_task_button(void)
{
    GtkWidget *b = mk_button("Run New _Task…", "Start a program or command (as you)");
    g_signal_connect(b, "clicked", G_CALLBACK(on_run_task), NULL);
    return b;
}

/* ================================================================== Startup Apps */

enum { SC_KEY, SC_ON, SC_ICON, SC_NAME, SC_CMD, SC_DELAY, SC_SOURCE, SC_NOTES, SC_DIM, SC_MISSING, SC_HIDDEN, SC_DELAYN, SC_N };

typedef struct {
    GtkWidget *page, *view, *filter_entry, *show_hidden, *b_remove, *b_props, *b_undo, *b_restore, *b_folder, *b_copy, *count, *b_default;
    GtkListStore *store;
    GtkTreeModel *filter, *sort;   /* store -> filter (Show Hidden, filter text) -> sort (column headers) -> view */
    infobar ib;
    as_list *list;
    char ftxt[128];
} startup;

static const as_entry *st_selected(startup *s)
{
    GtkTreeModel *m;
    GtkTreeIter it;
    gchar *key = NULL;
    const as_entry *e = NULL;
    if (!s->list || !gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(s->view)), &m, &it)) return NULL;
    gtk_tree_model_get(m, &it, SC_KEY, &key, -1);
    if (key) e = as_find(s->list, key);
    g_free(key);
    return e;
}

static void st_buttons(startup *s)
{
    const as_entry *e = st_selected(s);
    char *what = NULL;
    gtk_widget_set_sensitive(s->b_remove, e != NULL);
    gtk_widget_set_sensitive(s->b_props, e != NULL);
    gtk_widget_set_sensitive(s->b_folder, e != NULL);
    gtk_widget_set_sensitive(s->b_copy, e && e->exec && *e->exec);
    gtk_button_set_label(GTK_BUTTON(s->b_remove), e ? (!strcmp(as_remove_label(e), "Remove") ? "Remo_ve" : as_remove_label(e)) : "Remo_ve");
    gtk_widget_set_tooltip_text(s->b_remove,
        !e ? "Remove the selected entry" :
        e->source == AS_SRC_XDG_USER ? "Move it to the startup trash (Restore Removed brings it back)" :
        e->source == AS_SRC_SYSTEMD ? "systemctl --user disable: the unit stays installed and can be enabled again" :
        e->source == AS_SRC_PLASMA_ENV || e->source == AS_SRC_SCRIPT ? "Move the script to the disabled folder (never deleted)" :
        "System entries belong to the system and are never removed: this turns it off for you only (a copy in your autostart folder)");
    gtk_widget_set_sensitive(s->b_undo, as_can_undo(&what));
    {
        char *t = what ? g_strdup_printf("Undo: %s", what) : g_strdup("Nothing to undo");
        gtk_widget_set_tooltip_text(s->b_undo, t);
        g_free(t);
    }
    g_free(what);
    {
        GPtrArray *tr = as_trash_list(), *ob = as_old_backups();
        gtk_widget_set_sensitive(s->b_restore, tr->len > 0 || ob->len > 0);
        g_ptr_array_unref(tr);
        g_ptr_array_unref(ob);
    }
    {
        const char *dw = as_default_what(e);
        gtk_widget_set_sensitive(s->b_default, dw != NULL);
        gtk_widget_set_tooltip_text(s->b_default, dw ? dw : "This entry has no other default (it is a system entry as shipped, "
                                                             "or one you added)");
    }
}

static void st_on_cell(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
    gboolean on, dim;
    (void)c; (void)d;
    gtk_tree_model_get(m, it, SC_ON, &on, SC_DIM, &dim, -1);
    g_object_set(r, "active", on, "inconsistent", on && dim, NULL);
}

static void st_reload(startup *s, const char *select_key)
{
    char *keep = select_key ? g_strdup(select_key) : NULL;
    int nstart = 0, nhid = 0;
    if (!keep) { const as_entry *e = st_selected(s); if (e) keep = g_strdup(e->key); }
    as_list_free(s->list);
    s->list = as_load();
    gtk_list_store_clear(s->store);
    for (guint i = 0; i < s->list->items->len; i++) {
        const as_entry *e = s->list->items->pdata[i];
        GtkTreeIter it;
        GIcon *ic = e->icon && *e->icon ? g_icon_new_for_string(e->icon, NULL) : NULL;
        char dl[24] = "";
        if (!ic) ic = g_themed_icon_new(e->source == AS_SRC_SYSTEMD ? "system-run-symbolic" :
                                        e->source == AS_SRC_PLASMA_ENV || e->source == AS_SRC_SCRIPT ? "text-x-script" :
                                        "application-x-executable");
        if (e->delay > 0) snprintf(dl, sizeof dl, "%d s", e->delay);
        gtk_list_store_insert_with_values(s->store, &it, -1, SC_KEY, e->key, SC_ON, e->enabled, SC_ICON, ic, SC_NAME, e->name,
                                          SC_CMD, e->exec, SC_DELAY, dl, SC_SOURCE, as_source_name(e->source), SC_NOTES, e->notes,
                                          SC_DIM, e->not_this_desktop || (e->enabled && !e->starts && !e->prog_missing),
                                          SC_MISSING, e->prog_missing, SC_HIDDEN, e->hidden, SC_DELAYN, e->delay, -1);
        g_object_unref(ic);
        nstart += e->starts;
        nhid += e->hidden;
        if (keep && !strcmp(keep, e->key)) {
            /* keep the selection across the reload, whatever the sort order */
            GtkTreeIter fi, si;
            if (gtk_tree_model_filter_convert_child_iter_to_iter(GTK_TREE_MODEL_FILTER(s->filter), &fi, &it) &&
                gtk_tree_model_sort_convert_child_iter_to_iter(GTK_TREE_MODEL_SORT(s->sort), &si, &fi)) {
                GtkTreePath *pa = gtk_tree_model_get_path(s->sort, &si);
                gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(s->view)), &si);
                gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(s->view), pa, NULL, FALSE, 0, 0);
                gtk_tree_path_free(pa);
            }
        }
    }
    {
        char b[160];
        snprintf(b, sizeof b, "%u entries · %d start at the next login · %d hidden · desktop: %s",
                 s->list->items->len, nstart, nhid, s->list->desktop);
        gtk_label_set_text(GTK_LABEL(s->count), b);
    }
    g_free(keep);
    st_buttons(s);
}

/* after an action: read again and say what really happened */
static void st_after(startup *s, gboolean ok, char *err, const char *key, int want_on, const char *done)
{
    st_reload(s, key);
    if (!ok) { ib_show(&s->ib, GTK_MESSAGE_ERROR, err ? err : "The change failed."); g_free(err); return; }
    g_free(err);
    if (key && want_on >= 0) {
        const as_entry *e = as_find(s->list, key);
        if (!e || e->enabled != want_on) {
            char *m = g_strdup_printf("%s was written, but reading it back shows it %s. Something else may manage this entry.",
                                      e ? e->name : key, e && e->enabled ? "still on" : "still off");
            ib_show(&s->ib, GTK_MESSAGE_ERROR, m);
            g_free(m);
            return;
        }
    }
    if (done) ib_show(&s->ib, GTK_MESSAGE_INFO, done);
}

/* turning off a part of the desktop session itself can leave the next login without a desktop: ask first */
static int session_critical(const as_entry *e)
{
    static const char *const pre[] = { "plasmashell", "org.kde.plasmashell", "powerdevil", "org_kde_powerdevil", "polkit",
                                       "pam_kwallet", "kwallet", "ksmserver", "kwin", "gnome-shell", "gnome-session",
                                       "dbus", "pipewire", "wireplumber", "xdg-desktop-portal", "plasma-", "at-spi", NULL };
    char *prog = NULL, *bn;
    int hit = 0;
    if (!e) return 0;
    if (e->exec && *e->exec) {
        char **av = NULL;
        if (g_shell_parse_argv(e->exec, NULL, &av, NULL) && av[0]) prog = g_path_get_basename(av[0]);
        g_strfreev(av);
    }
    bn = prog;
    for (int i = 0; pre[i] && !hit; i++)
        hit = !strncmp(e->id, pre[i], strlen(pre[i])) || (bn && !strncmp(bn, pre[i], strlen(pre[i])));
    g_free(prog);
    return hit;
}

static int ok_to_turn_off(startup *s, const as_entry *e)
{
    char *t;
    int go;
    if (!session_critical(e)) return 1;
    t = g_strdup_printf("Turn off \"%s\"?", e->name);
    go = confirm(s->page, t, "This is part of your desktop session: the next login may not start properly without it.",
                 "Undo is on this page, but only if you can still log in to a desktop.", "_Turn Off");
    g_free(t);
    return go;
}

/* column sorting: text without case, numbers as numbers, then by name */
static gint st_cmp(GtkTreeModel *m, GtkTreeIter *a, GtkTreeIter *b, gpointer d)
{
    int col = GPOINTER_TO_INT(d), r = 0;
    if (col == SC_ON || col == SC_DELAYN) {
        int x = 0, y = 0;
        if (col == SC_ON) {
            gboolean p, q, pd, qd;
            gtk_tree_model_get(m, a, SC_ON, &p, SC_DIM, &pd, -1);
            gtk_tree_model_get(m, b, SC_ON, &q, SC_DIM, &qd, -1);
            x = p ? (pd ? 1 : 2) : 0;   /* off < on but not here < on */
            y = q ? (qd ? 1 : 2) : 0;
        } else {
            gtk_tree_model_get(m, a, col, &x, -1);
            gtk_tree_model_get(m, b, col, &y, -1);
        }
        r = x - y;
    } else {
        gchar *x = NULL, *y = NULL, *fx, *fy;
        gtk_tree_model_get(m, a, col, &x, -1);
        gtk_tree_model_get(m, b, col, &y, -1);
        fx = g_utf8_casefold(x ? x : "", -1);
        fy = g_utf8_casefold(y ? y : "", -1);
        r = g_utf8_collate(fx, fy);
        g_free(x); g_free(y); g_free(fx); g_free(fy);
    }
    if (!r && col != SC_NAME) return st_cmp(m, a, b, GINT_TO_POINTER(SC_NAME));
    return r;
}

static void st_toggled(GtkCellRendererToggle *r, gchar *path, gpointer d)
{
    startup *s = d;
    GtkTreeIter it;
    gchar *key = NULL;
    const as_entry *e;
    (void)r;
    if (!gtk_tree_model_get_iter_from_string(s->sort, &it, path)) return;
    gtk_tree_model_get(s->sort, &it, SC_KEY, &key, -1);
    if (key && (e = as_find(s->list, key))) {
        char *err = NULL, *k, *msg;
        int on = !e->enabled;
        gboolean ok;
        if (!on && !ok_to_turn_off(s, e)) { g_free(key); return; }
        k = g_strdup(e->key);
        ok = as_set_enabled(e, on, &err);
        msg = g_strdup_printf("%s: %s at the next login.", e->name, on ? "starts" : "does not start");
        st_after(s, ok, err, k, on, msg);
        g_free(msg);
        g_free(k);
    }
    g_free(key);
}

static gboolean st_visible(GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
    startup *s = d;
    gboolean hid;
    gchar *n = NULL, *c = NULL, *x = NULL;
    int ok = 1;
    gtk_tree_model_get(m, it, SC_HIDDEN, &hid, -1);
    if (hid && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->show_hidden))) return FALSE;
    if (!s->ftxt[0]) return TRUE;
    gtk_tree_model_get(m, it, SC_NAME, &n, SC_CMD, &c, SC_SOURCE, &x, -1);
    {
        char *a = g_utf8_strdown(n ? n : "", -1), *b = g_utf8_strdown(c ? c : "", -1), *e = g_utf8_strdown(x ? x : "", -1);
        ok = strstr(a, s->ftxt) || strstr(b, s->ftxt) || strstr(e, s->ftxt);
        g_free(a); g_free(b); g_free(e);
    }
    g_free(n); g_free(c); g_free(x);
    return ok;
}

static void st_refilter(startup *s) { gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(s->filter)); }

static void st_filter_changed(GtkEditable *e, gpointer d)
{
    startup *s = d;
    char *t = g_utf8_strdown(gtk_entry_get_text(GTK_ENTRY(e)), -1);
    snprintf(s->ftxt, sizeof s->ftxt, "%s", g_strstrip(t));
    g_free(t);
    st_refilter(s);
}

static void st_hidden_toggled(GtkToggleButton *b, gpointer d) { (void)b; st_refilter(d); }

/* ---- Properties ---- */

static void st_properties(startup *s)
{
    const as_entry *e = st_selected(s);
    GtkWidget *d, *g, *name, *cmd, *delay, *on, *box;
    int editable, row = 0;
    char *key;
    if (!e) return;
    key = g_strdup(e->key);
    editable = e->source != AS_SRC_SYSTEMD && e->source != AS_SRC_PLASMA_ENV && e->source != AS_SRC_SCRIPT;
    d = gtk_dialog_new_with_buttons("Startup Entry Properties", top_of(s->page), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_OK, NULL);
    box = gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    g = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g), 6);
    gtk_grid_set_column_spacing(GTK_GRID(g), 10);
    name = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(name), e->name);
    cmd = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(cmd), e->exec);
    gtk_entry_set_width_chars(GTK_ENTRY(cmd), 56);
    delay = gtk_spin_button_new_with_range(0, 600, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(delay), e->delay);
    on = gtk_check_button_new_with_mnemonic("Start at _login");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(on), e->enabled);
    gtk_widget_set_sensitive(name, editable);
    gtk_widget_set_sensitive(cmd, editable);
    gtk_widget_set_sensitive(delay, editable);
#define ROW(k, w) do { gtk_grid_attach(GTK_GRID(g), mk_label(k, 1, "pv-dim"), 0, row, 1, 1); gtk_grid_attach(GTK_GRID(g), w, 1, row++, 1, 1); } while (0)
    ROW("Name:", name);
    ROW("Command:", cmd);
    {
        GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(h), delay, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(h), mk_label("seconds after login (0 = at once)", 0, "pv-dim"), FALSE, FALSE, 0);
        ROW("Delay:", h);
    }
    ROW("", on);
    ROW("Source:", mk_label(as_source_name(e->source), 0, NULL));
    {
        GtkWidget *f = mk_label(e->path ? e->path : EM, 0, NULL);
        gtk_label_set_selectable(GTK_LABEL(f), TRUE);
        gtk_label_set_ellipsize(GTK_LABEL(f), PANGO_ELLIPSIZE_MIDDLE);
        ROW("File:", f);
    }
    if (e->sys_path && e->source == AS_SRC_XDG_OVERRIDE) ROW("System Entry:", mk_label(e->sys_path, 0, NULL));
    if (e->notes && *e->notes) {
        GtkWidget *n = mk_label(e->notes, 0, NULL);
        gtk_label_set_line_wrap(GTK_LABEL(n), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(n), 60);
        ROW("Notes:", n);
    }
    if (!editable) ROW("", mk_label("Only On / Off can be changed for this kind of entry.", 0, "pv-dim"));
    else if (e->source == AS_SRC_XDG_SYSTEM)
        ROW("", mk_label("Saving makes your own copy in ~/.config/autostart; the system file is not changed.", 0, "pv-dim"));
#undef ROW
    gtk_box_pack_start(GTK_BOX(box), g, TRUE, TRUE, 0);
    gtk_widget_show_all(d);
    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        const as_entry *cur;
        const char *nn = gtk_entry_get_text(GTK_ENTRY(name)), *nc = gtk_entry_get_text(GTK_ENTRY(cmd));
        int nd = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(delay)), non = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(on));
        char *err = NULL, *why;
        gboolean ok;
        cur = as_find(s->list, key);
        if (!cur) break;
        if (!non && cur->enabled && !ok_to_turn_off(s, cur)) continue;
        if (editable && strcmp(nc, cur->exec) && (why = as_check_cmd(nc))) {
            int go = confirm(d, "Save this command anyway?", why, "At login it would fail to start.", "Save _Anyway");
            g_free(why);
            if (!go) continue;
        }
        ok = as_edit(cur, editable && strcmp(nn, cur->name) ? nn : NULL, editable && strcmp(nc, cur->exec) ? nc : NULL,
                     editable && nd != cur->delay ? nd : -1, non != cur->enabled ? non : -1, &err);
        st_after(s, ok, err, key, non, ok ? "Saved." : NULL);
        break;
    }
    gtk_widget_destroy(d);
    g_free(key);
}

/* ---- Add ---- */

typedef struct {
    GtkWidget *stack, *apps, *search, *file, *cmd, *name, *delay, *only;
    GPtrArray *applist;
    char stxt[128];
} adddlg;

static gboolean app_filter(GtkListBoxRow *row, gpointer d)
{
    adddlg *a = d;
    as_app *ap = g_object_get_data(G_OBJECT(row), "app");
    char *n;
    int ok;
    if (!a->stxt[0] || !ap) return TRUE;
    n = g_utf8_strdown(ap->name, -1);
    ok = strstr(n, a->stxt) != NULL || (ap->exec && strstr(ap->exec, a->stxt));
    g_free(n);
    return ok;
}

static void app_search(GtkEditable *e, gpointer d)
{
    adddlg *a = d;
    char *t = g_utf8_strdown(gtk_entry_get_text(GTK_ENTRY(e)), -1);
    snprintf(a->stxt, sizeof a->stxt, "%s", g_strstrip(t));
    g_free(t);
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(a->apps));
}

static void app_picked(GtkListBox *lb, GtkListBoxRow *row, gpointer d)
{
    adddlg *a = d;
    as_app *ap = row ? g_object_get_data(G_OBJECT(row), "app") : NULL;
    (void)lb;
    if (ap) gtk_entry_set_text(GTK_ENTRY(a->name), ap->name);
}

static void file_picked(GtkFileChooserButton *b, gpointer d)
{
    adddlg *a = d;
    char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(b));
    if (f) { char *bn = g_path_get_basename(f); gtk_entry_set_text(GTK_ENTRY(a->name), bn); g_free(bn); }
    g_free(f);
}

static GtkWidget *app_row(as_app *ap)
{
    GtkWidget *row = gtk_list_box_row_new(), *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8), *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GIcon *ic = ap->icon && *ap->icon ? g_icon_new_for_string(ap->icon, NULL) : g_themed_icon_new("application-x-executable");
    GtkWidget *im = gtk_image_new_from_gicon(ic, GTK_ICON_SIZE_DND), *c;
    gtk_image_set_pixel_size(GTK_IMAGE(im), 24);
    g_object_unref(ic);
    gtk_box_pack_start(GTK_BOX(h), im, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(v), mk_label(ap->name, 0, NULL), FALSE, FALSE, 0);
    c = mk_label(ap->comment && *ap->comment ? ap->comment : ap->exec, 0, "pv-dim");
    gtk_label_set_ellipsize(GTK_LABEL(c), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(v), c, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(h), v, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(row), h);
    g_object_set_data(G_OBJECT(row), "app", ap);
    return row;
}

static void st_add(startup *s)
{
    adddlg a;
    GtkWidget *d, *box, *sw, *sp, *pg, *g, *h;
    const char *cd = getenv("XDG_CURRENT_DESKTOP");
    char only_lbl[96];
    memset(&a, 0, sizeof a);
    d = gtk_dialog_new_with_buttons("Add a Startup App", top_of(s->page), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    "_Cancel", GTK_RESPONSE_CANCEL, "_Add", GTK_RESPONSE_OK, NULL);
    gtk_window_set_default_size(GTK_WINDOW(d), 620, 560);
    box = gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_set_spacing(GTK_BOX(box), 8);
    a.stack = gtk_stack_new();
    sp = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(sp), GTK_STACK(a.stack));
    gtk_widget_set_halign(sp, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(box), sp, FALSE, FALSE, 0);
    /* 1: an installed application */
    pg = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    a.search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(a.search), "Search installed applications");
    g_signal_connect(a.search, "search-changed", G_CALLBACK(app_search), &a);
    gtk_box_pack_start(GTK_BOX(pg), a.search, FALSE, FALSE, 0);
    a.apps = gtk_list_box_new();
    gtk_list_box_set_filter_func(GTK_LIST_BOX(a.apps), app_filter, &a, NULL);
    g_signal_connect(a.apps, "row-selected", G_CALLBACK(app_picked), &a);
    a.applist = as_apps();
    for (guint i = 0; i < a.applist->len; i++) gtk_list_box_insert(GTK_LIST_BOX(a.apps), app_row(a.applist->pdata[i]), -1);
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw), a.apps);
    gtk_box_pack_start(GTK_BOX(pg), sw, TRUE, TRUE, 0);
    gtk_stack_add_titled(GTK_STACK(a.stack), pg, "app", "Installed Application");
    /* 2: a program or script */
    pg = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(pg), mk_label("Choose a program or script file to run at login:", 0, NULL), FALSE, FALSE, 0);
    a.file = gtk_file_chooser_button_new("Choose a Program or Script", GTK_FILE_CHOOSER_ACTION_OPEN);
    g_signal_connect(a.file, "file-set", G_CALLBACK(file_picked), &a);
    gtk_box_pack_start(GTK_BOX(pg), a.file, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(pg), mk_label("A script that is not executable is run with sh.", 0, "pv-dim"), FALSE, FALSE, 0);
    gtk_stack_add_titled(GTK_STACK(a.stack), pg, "file", "Program or Script");
    /* 3: a typed command */
    pg = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(pg), mk_label("Type the command to run at login:", 0, NULL), FALSE, FALSE, 0);
    a.cmd = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(a.cmd), "program --option");
    gtk_box_pack_start(GTK_BOX(pg), a.cmd, FALSE, FALSE, 0);
    gtk_stack_add_titled(GTK_STACK(a.stack), pg, "cmd", "Command");
    gtk_box_pack_start(GTK_BOX(box), a.stack, TRUE, TRUE, 0);
    /* options */
    g = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g), 6);
    gtk_grid_set_column_spacing(GTK_GRID(g), 10);
    a.name = gtk_entry_new();
    gtk_widget_set_hexpand(a.name, TRUE);
    gtk_grid_attach(GTK_GRID(g), mk_label("Name:", 1, "pv-dim"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), a.name, 1, 0, 1, 1);
    h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    a.delay = gtk_spin_button_new_with_range(0, 600, 1);
    gtk_box_pack_start(GTK_BOX(h), a.delay, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(h), mk_label("seconds after login (0 = at once)", 0, "pv-dim"), FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(g), mk_label("Delay Start By:", 1, "pv-dim"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g), h, 1, 1, 1, 1);
    {
        char **dd = g_strsplit(cd && *cd ? cd : "KDE", ":", 2);
        snprintf(only_lbl, sizeof only_lbl, "Start only in this desktop (%s)", dd[0]);
        g_strfreev(dd);
    }
    a.only = gtk_check_button_new_with_label(only_lbl);
    gtk_grid_attach(GTK_GRID(g), a.only, 1, 2, 1, 1);
    gtk_box_pack_start(GTK_BOX(box), g, FALSE, FALSE, 0);
    gtk_widget_show_all(d);
    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        const char *page = gtk_stack_get_visible_child_name(GTK_STACK(a.stack));
        const char *name = gtk_entry_get_text(GTK_ENTRY(a.name));
        char *cmd = NULL, *why, *err = NULL, *key = NULL;
        const char *icon = NULL, *comment = NULL, *base = NULL;
        gboolean ok;
        if (!strcmp(page, "app")) {
            GtkListBoxRow *r = gtk_list_box_get_selected_row(GTK_LIST_BOX(a.apps));
            as_app *ap = r ? g_object_get_data(G_OBJECT(r), "app") : NULL;
            if (!ap) { message(d, GTK_MESSAGE_INFO, "Pick an application first", NULL); continue; }
            cmd = g_strdup(ap->exec);
            icon = ap->icon;
            comment = ap->comment;
            base = ap->id;
        } else if (!strcmp(page, "file")) {
            char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(a.file));
            if (!f) { message(d, GTK_MESSAGE_INFO, "Choose a file first", NULL); continue; }
            {
                char *q = g_shell_quote(f);
                cmd = g_file_test(f, G_FILE_TEST_IS_EXECUTABLE) ? g_strdup(q) : g_strdup_printf("sh %s", q);
                g_free(q);
            }
            g_free(f);
        } else
            cmd = g_strdup(gtk_entry_get_text(GTK_ENTRY(a.cmd)));
        if (!*name) { message(d, GTK_MESSAGE_INFO, "Give it a name", NULL); g_free(cmd); continue; }
        if ((why = as_check_cmd(cmd))) {
            int go = confirm(d, "Add this command anyway?", why, "At login it would fail to start.", "Add _Anyway");
            g_free(why);
            if (!go) { g_free(cmd); continue; }
        }
        ok = as_add(name, cmd, icon, comment, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a.delay)),
                    gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(a.only)), base, &key, &err);
        {
            char *m = g_strdup_printf("Added \"%s\": it starts at the next login.", name);
            st_after(s, ok, err, key, ok ? 1 : -1, ok ? m : NULL);
            g_free(m);
        }
        g_free(key);
        g_free(cmd);
        if (ok) break;
    }
    gtk_widget_destroy(d);
    g_ptr_array_unref(a.applist);
}

/* ---- Restore Removed ---- */

static void st_restore(startup *s)
{
    GPtrArray *tr = as_trash_list(), *ob = as_old_backups();
    GtkWidget *d, *box, *lb, *sw;
    d = gtk_dialog_new_with_buttons("Restore Removed Startup Apps", top_of(s->page), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    "_Close", GTK_RESPONSE_CANCEL, "_Restore", GTK_RESPONSE_OK, NULL);
    gtk_window_set_default_size(GTK_WINDOW(d), 480, 340);
    box = gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_set_spacing(GTK_BOX(box), 6);
    gtk_box_pack_start(GTK_BOX(box), mk_label("Entries you removed are kept in ~/.config/autostart/.srs-trash.", 0, "pv-dim"), FALSE, FALSE, 0);
    lb = gtk_list_box_new();
    for (guint i = 0; i < tr->len; i++) {
        char *n = as_trash_name(tr->pdata[i]), *t = g_strdup_printf("%s  (%s)", n, (char *)tr->pdata[i]);
        GtkWidget *row = gtk_list_box_row_new();
        gtk_container_add(GTK_CONTAINER(row), mk_label(t, 0, NULL));
        g_object_set_data_full(G_OBJECT(row), "file", g_strdup(tr->pdata[i]), g_free);
        gtk_list_box_insert(GTK_LIST_BOX(lb), row, -1);
        g_free(n);
        g_free(t);
    }
    if (ob->len) {   /* the old (2.2) Startup manager's saved copies: brought back as copies, never deleted */
        GtkWidget *hr = gtk_list_box_row_new();
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(hr), FALSE);
        GtkWidget *hl = mk_label("Saved by the old Startup manager (~/.config/srs/startup-backup; a copy comes back, the file "
                                 "stays):", 0, "pv-dim");
        gtk_label_set_line_wrap(GTK_LABEL(hl), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(hl), 60);
        gtk_container_add(GTK_CONTAINER(hr), hl);
        gtk_list_box_insert(GTK_LIST_BOX(lb), hr, -1);
    }
    for (guint i = 0; i < ob->len; i++) {
        char *n = as_old_backup_name(ob->pdata[i]), *t = g_strdup_printf("%s  (%s)", n, (char *)ob->pdata[i]);
        GtkWidget *row = gtk_list_box_row_new();
        gtk_container_add(GTK_CONTAINER(row), mk_label(t, 0, NULL));
        g_object_set_data_full(G_OBJECT(row), "file", g_strdup(ob->pdata[i]), g_free);
        g_object_set_data(G_OBJECT(row), "old", GINT_TO_POINTER(1));
        gtk_list_box_insert(GTK_LIST_BOX(lb), row, -1);
        g_free(n);
        g_free(t);
    }
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw), lb);
    gtk_box_pack_start(GTK_BOX(box), sw, TRUE, TRUE, 0);
    gtk_widget_show_all(d);
    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        GtkListBoxRow *r = gtk_list_box_get_selected_row(GTK_LIST_BOX(lb));
        const char *f = r ? g_object_get_data(G_OBJECT(r), "file") : NULL;
        char *err = NULL, *key;
        gboolean ok;
        if (!f) continue;
        ok = g_object_get_data(G_OBJECT(r), "old") ? as_import_old_backup(f, &err) : as_restore(f, &err);
        key = g_strdup_printf("xdg:%s", f);
        st_after(s, ok, err, NULL, -1, ok ? "Restored." : NULL);
        g_free(key);
        if (ok) gtk_widget_destroy(GTK_WIDGET(r));
        if (ok) break;
    }
    gtk_widget_destroy(d);
    g_ptr_array_unref(tr);
    g_ptr_array_unref(ob);
}

/* ---- Restore Default ---- */

static void st_default(startup *s)
{
    const as_entry *e = st_selected(s);
    const char *what = as_default_what(e);
    char *t, *err = NULL, *k;
    gboolean ok;
    if (!e || !what) return;
    t = g_strdup_printf("Restore the default of \"%s\"?", e->name);
    if (!confirm(s->page, t, what, NULL, "_Restore Default")) { g_free(t); return; }
    g_free(t);
    k = g_strdup(e->key);
    ok = as_restore_default(e, &err);
    st_after(s, ok, err, k, -1, ok ? "Default restored (Undo puts your change back)." : NULL);
    g_free(k);
}

/* ---- buttons, menu ---- */

static void st_do_remove(startup *s)
{
    const as_entry *e = st_selected(s);
    char *err = NULL, *k, *msg;
    gboolean ok;
    if (!e || !ok_to_turn_off(s, e)) return;
    k = g_strdup(e->key);
    msg = e->source == AS_SRC_XDG_USER ? g_strdup_printf("Removed \"%s\" (Restore Removed brings it back).", e->name)
                                       : g_strdup_printf("\"%s\" is turned off for you.", e->name);
    ok = as_remove(e, &err);
    st_after(s, ok, err, k, e->source == AS_SRC_XDG_USER ? -1 : 0, ok ? msg : NULL);
    g_free(msg);
    g_free(k);
}

static void st_undo(startup *s)
{
    char *what = NULL, *err = NULL, *m;
    gboolean ok = as_undo(&what, &err);
    m = g_strdup_printf("Undone: %s", what ? what : "");
    st_after(s, ok, err, NULL, -1, ok ? m : NULL);
    g_free(m);
    g_free(what);
}

static void st_folder(startup *s)
{
    const as_entry *e = st_selected(s);
    char *dir, *uri;
    GError *ge = NULL;
    if (!e) return;
    dir = as_folder(e);
    g_mkdir_with_parents(dir, 0755);
    uri = g_filename_to_uri(dir, NULL, NULL);
    if (!uri || !g_app_info_launch_default_for_uri(uri, NULL, &ge)) ib_show(&s->ib, GTK_MESSAGE_ERROR, ge ? ge->message : "Cannot open the folder.");
    if (ge) g_error_free(ge);
    g_free(uri);
    g_free(dir);
}

static void st_copy(startup *s)
{
    const as_entry *e = st_selected(s);
    if (!e) return;
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), e->exec, -1);
    ib_show(&s->ib, GTK_MESSAGE_INFO, "Command copied.");
}

static void b_add(GtkButton *b, gpointer d) { (void)b; st_add(d); }
static void b_remove(GtkButton *b, gpointer d) { (void)b; st_do_remove(d); }
static void b_props(GtkButton *b, gpointer d) { (void)b; st_properties(d); }
static void b_undo(GtkButton *b, gpointer d) { (void)b; st_undo(d); }
static void b_restore(GtkButton *b, gpointer d) { (void)b; st_restore(d); }
static void b_default(GtkButton *b, gpointer d) { (void)b; st_default(d); }
static void m_default(GtkMenuItem *i, gpointer d) { (void)i; st_default(d); }
static void b_folder(GtkButton *b, gpointer d) { (void)b; st_folder(d); }
static void b_copy(GtkButton *b, gpointer d) { (void)b; st_copy(d); }
static void b_refresh(GtkButton *b, gpointer d) { (void)b; st_reload(d, NULL); }
static void m_toggle(GtkMenuItem *i, gpointer d)
{
    startup *s = d;
    const as_entry *e = st_selected(s);
    (void)i;
    if (e) {
        char *err = NULL, *k;
        int on = !e->enabled;
        gboolean ok;
        if (!on && !ok_to_turn_off(s, e)) return;
        k = g_strdup(e->key);
        ok = as_set_enabled(e, on, &err);
        st_after(s, ok, err, k, on, NULL);
        g_free(k);
    }
}
static void m_add(GtkMenuItem *i, gpointer d) { (void)i; st_add(d); }
static void m_remove(GtkMenuItem *i, gpointer d) { (void)i; st_do_remove(d); }
static void m_props(GtkMenuItem *i, gpointer d) { (void)i; st_properties(d); }
static void m_folder(GtkMenuItem *i, gpointer d) { (void)i; st_folder(d); }
static void m_copy(GtkMenuItem *i, gpointer d) { (void)i; st_copy(d); }

static gboolean st_press(GtkWidget *w, GdkEventButton *ev, gpointer d)
{
    startup *s = d;
    GtkTreePath *p = NULL;
    const as_entry *e;
    GtkWidget *m;
    if (ev->type != GDK_BUTTON_PRESS || ev->button != 3) return FALSE;
    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(w), (gint)ev->x, (gint)ev->y, &p, NULL, NULL, NULL)) {
        gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(w)), p);
        gtk_tree_path_free(p);
    }
    e = st_selected(s);
    m = gtk_menu_new();
#define MI(lbl, cb, sens) do { GtkWidget *it_ = gtk_menu_item_new_with_mnemonic(lbl); gtk_widget_set_sensitive(it_, sens); \
        g_signal_connect(it_, "activate", G_CALLBACK(cb), s); gtk_menu_shell_append(GTK_MENU_SHELL(m), it_); } while (0)
    MI(e && e->enabled ? "_Disable" : "_Enable", m_toggle, e != NULL);
    MI("Pr_operties…", m_props, e != NULL);
    MI(e ? as_remove_label(e) : "Remove", m_remove, e != NULL);
    MI("Restore De_fault", m_default, as_default_what(e) != NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    MI("Open File _Location", m_folder, e != NULL);
    MI("_Copy Command", m_copy, e && e->exec && *e->exec);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    MI("_Add…", m_add, TRUE);
#undef MI
    gtk_widget_show_all(m);
    gtk_menu_popup_at_pointer(GTK_MENU(m), (GdkEvent *)ev);
    return TRUE;
}

static void st_activated(GtkTreeView *v, GtkTreePath *p, GtkTreeViewColumn *c, gpointer d) { (void)v; (void)p; (void)c; st_properties(d); }
static void st_sel_changed(GtkTreeSelection *sel, gpointer d) { (void)sel; st_buttons(d); }

static void cell_notes(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
    gboolean miss;
    (void)c; (void)d;
    gtk_tree_model_get(m, it, SC_MISSING, &miss, -1);
    if (miss) { GdkRGBA red = { 0.85, 0.15, 0.12, 1 }; g_object_set(r, "foreground-rgba", &red, NULL); }
    else g_object_set(r, "foreground-set", FALSE, NULL);
}

static void st_destroy(GtkWidget *w, gpointer d)
{
    startup *s = d;
    (void)w;
    as_list_free(s->list);
    g_object_unref(s->store);
    g_free(s);
}

static void st_map(GtkWidget *w, gpointer d)
{
    startup *s = d;
    int old;
    (void)w;
    st_reload(s, NULL);
    /* once ever: the old (2.2) Startup manager saved copies; say where to find them */
    if ((old = as_old_backups_offer_once()) > 0) {
        char *m = g_strdup_printf("%d item%s saved by the old Startup manager (~/.config/srs/startup-backup) can be brought back: "
                                  "Restore Removed... lists them. Nothing there is deleted.", old, old == 1 ? "" : "s");
        ib_show(&s->ib, GTK_MESSAGE_INFO, m);
        g_free(m);
    }
}

GtkWidget *pa_startup_new(void)
{
    startup *s = g_new0(startup, 1);
    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6), *right, *tb, *sw, *add, *refresh;
    GtkCellRenderer *r;
    GtkTreeViewColumn *c;
    s->page = b;
    gtk_container_set_border_width(GTK_CONTAINER(b), 8);
    gtk_box_pack_start(GTK_BOX(b), header("Startup Apps", &right), FALSE, FALSE, 0);
    s->filter_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(s->filter_entry), "Filter startup apps");
    gtk_entry_set_width_chars(GTK_ENTRY(s->filter_entry), 28);
    g_signal_connect(s->filter_entry, "search-changed", G_CALLBACK(st_filter_changed), s);
    s->show_hidden = gtk_check_button_new_with_mnemonic("Show _Hidden");
    gtk_widget_set_tooltip_text(s->show_hidden, "Also list entries that are hidden: NoDisplay, other desktops only, "
                                                "conditional (KDE), TryExec missing, or hidden by the system entry");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->show_hidden), TRUE);
    g_signal_connect(s->show_hidden, "toggled", G_CALLBACK(st_hidden_toggled), s);
    gtk_box_pack_end(GTK_BOX(right), s->filter_entry, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(right), s->show_hidden, FALSE, FALSE, 0);

    tb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    add = mk_button("Add…", "Add a startup app: an installed application, a program or script, or a command");
    s->b_remove = mk_button("Remo_ve", NULL);
    s->b_props = mk_button("Properties…", "Change the name, command, delay or On / Off (double-click does the same)");
    s->b_undo = mk_button("Und_o", NULL);   /* U is The Big Top's Update (top row) */
    s->b_restore = mk_button("Restore Re_moved…", "Bring back entries you removed, or ones the old Startup manager saved");
    s->b_default = mk_button("Restore De_fault", NULL);
    g_signal_connect(s->b_default, "clicked", G_CALLBACK(b_default), s);
    /* icon buttons (also in the right-click menu): the row of buttons stays inside a 1400-pixel window */
    s->b_folder = icon_button("folder-open-symbolic", "Open File Location: the folder that holds this entry");
    s->b_copy = icon_button("edit-copy-symbolic", "Copy Command: the command to the clipboard");
    refresh = icon_button("view-refresh-symbolic", "Read the list again");
    g_signal_connect(add, "clicked", G_CALLBACK(b_add), s);
    g_signal_connect(s->b_remove, "clicked", G_CALLBACK(b_remove), s);
    g_signal_connect(s->b_props, "clicked", G_CALLBACK(b_props), s);
    g_signal_connect(s->b_undo, "clicked", G_CALLBACK(b_undo), s);
    g_signal_connect(s->b_restore, "clicked", G_CALLBACK(b_restore), s);
    g_signal_connect(s->b_folder, "clicked", G_CALLBACK(b_folder), s);
    g_signal_connect(s->b_copy, "clicked", G_CALLBACK(b_copy), s);
    g_signal_connect(refresh, "clicked", G_CALLBACK(b_refresh), s);
    gtk_box_pack_start(GTK_BOX(tb), add, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb), s->b_remove, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb), s->b_props, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb), s->b_undo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb), s->b_restore, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb), s->b_default, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(tb), s->b_folder, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb), s->b_copy, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(tb), run_task_button(), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(tb), refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(b), tb, FALSE, FALSE, 0);
    ib_make(&s->ib);
    gtk_box_pack_start(GTK_BOX(b), s->ib.bar, FALSE, FALSE, 0);

    s->store = gtk_list_store_new(SC_N, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_ICON, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                  G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_INT);
    s->filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(s->store), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(s->filter), st_visible, s, NULL);
    s->sort = gtk_tree_model_sort_new_with_model(s->filter);
    g_object_unref(s->filter);
    {
        static const int cols[] = { SC_ON, SC_NAME, SC_CMD, SC_DELAYN, SC_SOURCE, SC_NOTES };
        for (int i = 0; i < 6; i++)
            gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(s->sort), cols[i], st_cmp, GINT_TO_POINTER(cols[i]), NULL);
    }
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(s->sort), SC_NAME, GTK_SORT_ASCENDING);
    s->view = gtk_tree_view_new_with_model(s->sort);
    g_object_unref(s->sort);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(s->view), FALSE);
    r = gtk_cell_renderer_toggle_new();
    g_signal_connect(r, "toggled", G_CALLBACK(st_toggled), s);
    c = gtk_tree_view_column_new_with_attributes("Enabled", r, "active", SC_ON, NULL);
    /* switched on but it will not start here (other desktop, condition, TryExec): a dash, not a tick */
    gtk_tree_view_column_set_cell_data_func(c, r, st_on_cell, NULL, NULL);
    gtk_tree_view_column_set_alignment(c, 0.5);
    gtk_tree_view_column_set_sort_column_id(c, SC_ON);
    gtk_tree_view_append_column(GTK_TREE_VIEW(s->view), c);
    c = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(c, "Name");
    gtk_tree_view_column_set_sort_column_id(c, SC_NAME);
    r = gtk_cell_renderer_pixbuf_new();
    g_object_set(r, "stock-size", GTK_ICON_SIZE_SMALL_TOOLBAR, NULL);
    gtk_tree_view_column_pack_start(c, r, FALSE);
    gtk_tree_view_column_add_attribute(c, r, "gicon", SC_ICON);
    r = gtk_cell_renderer_text_new();
    g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_END, "foreground", "gray", NULL);
    gtk_tree_view_column_pack_start(c, r, TRUE);
    gtk_tree_view_column_set_attributes(c, r, "text", SC_NAME, "foreground-set", SC_DIM, NULL);
    gtk_tree_view_column_set_resizable(c, TRUE);
    gtk_tree_view_column_set_sizing(c, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(c, 280);
    gtk_tree_view_append_column(GTK_TREE_VIEW(s->view), c);
#define TXT(title, col, w, xa, sortcol) do { r = gtk_cell_renderer_text_new(); \
        g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_END, "foreground", "gray", "xalign", xa, NULL); \
        c = gtk_tree_view_column_new_with_attributes(title, r, "text", col, "foreground-set", SC_DIM, NULL); \
        gtk_tree_view_column_set_resizable(c, TRUE); gtk_tree_view_column_set_alignment(c, xa); \
        gtk_tree_view_column_set_sort_column_id(c, sortcol); \
        if (w > 0) { gtk_tree_view_column_set_sizing(c, GTK_TREE_VIEW_COLUMN_FIXED); gtk_tree_view_column_set_fixed_width(c, w); } \
        gtk_tree_view_append_column(GTK_TREE_VIEW(s->view), c); } while (0)
    TXT("Command", SC_CMD, 360, 0.0f, SC_CMD);
    TXT("Delay", SC_DELAY, 64, 1.0f, SC_DELAYN);
    TXT("Source", SC_SOURCE, 190, 0.0f, SC_SOURCE);
    TXT("Notes", SC_NOTES, 0, 0.0f, SC_NOTES);
#undef TXT
    gtk_tree_view_column_set_cell_data_func(c, r, cell_notes, NULL, NULL);
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(s->view), SC_NOTES);
    g_signal_connect(s->view, "row-activated", G_CALLBACK(st_activated), s);
    g_signal_connect(s->view, "button-press-event", G_CALLBACK(st_press), s);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(s->view)), "changed", G_CALLBACK(st_sel_changed), s);
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw), s->view);
    gtk_box_pack_start(GTK_BOX(b), sw, TRUE, TRUE, 0);
    s->count = mk_label("", 0, "pv-dim");
    gtk_box_pack_start(GTK_BOX(b), s->count, FALSE, FALSE, 0);
    g_signal_connect(b, "map", G_CALLBACK(st_map), s);
    g_signal_connect(b, "destroy", G_CALLBACK(st_destroy), s);
    return b;
}

/* ================================================================== Users */

enum { UC_NAME, UC_SESSION, UC_TYPE, UC_STATUS, UC_PROCS, UC_CPU, UC_MEM, UC_PID, UC_KIND, UC_SID, UC_N };

typedef struct {
    char id[32], user[64], state[24], type[24], cls[24], seat[24], tty[24], service[48], desktop[48], remote_host[64];
    int uid, leader, remote, active;
} session;

typedef struct {
    GtkWidget *page, *view, *b_disc, *filter_entry, *count;
    GtkTreeStore *store;
    GtkTreeModel *filter;
    infobar ib;
    session *ses;
    int nses;
    GHashTable *rows;              /* "s<id>" or "p<pid>" -> GtkTreeIter* */
    unsigned gen;
    GHashTable *gens;              /* same keys -> generation */
    char ftxt[128];
    gint64 last_load;
    int loading;
    pd_snap *snap;                 /* last snapshot (for re-fills) */
    int dead;                      /* the page was destroyed; async replies only clean up */
} users;

/* a logind session id: letters, digits, '-' and '_', starting with a letter or digit (never an option), at most 31 */
static int session_id_ok(const char *t)
{
    size_t n = t ? strlen(t) : 0;
    if (!n || n > 31 || !g_ascii_isalnum(t[0])) return 0;
    for (size_t i = 0; i < n; i++) if (!g_ascii_isalnum(t[i]) && t[i] != '-' && t[i] != '_') return 0;
    return 1;
}

static void us_parse(users *u, const char *out)
{
    char **blocks = g_regex_split_simple("\\n\\s*\\n", out, 0, 0);
    int n = 0;
    for (int b = 0; blocks[b]; b++) if (strstr(blocks[b], "Id=")) n++;
    g_free(u->ses);
    u->ses = g_new0(session, n > 0 ? n : 1);
    u->nses = 0;
    for (int b = 0; blocks[b]; b++) {
        char **kv = g_strsplit(blocks[b], "\n", -1);
        session *s = &u->ses[u->nses];
        if (!strstr(blocks[b], "Id=")) { g_strfreev(kv); continue; }
        memset(s, 0, sizeof *s);
        s->uid = -1;
        for (int k = 0; kv[k]; k++) {
            char *eq = strchr(kv[k], '='), *v;
            if (!eq) continue;
            *eq = 0;
            v = eq + 1;
#define S(key, f) else if (!strcmp(kv[k], key)) snprintf(s->f, sizeof s->f, "%s", v)
            if (0) {}
            S("Id", id); S("Name", user); S("State", state); S("Type", type); S("Class", cls); S("Seat", seat); S("TTY", tty);
            S("Service", service); S("Desktop", desktop); S("RemoteHost", remote_host);
#undef S
            else if (!strcmp(kv[k], "User")) s->uid = atoi(v);
            else if (!strcmp(kv[k], "Leader")) s->leader = atoi(v);
            else if (!strcmp(kv[k], "Remote")) s->remote = !strcmp(v, "yes");
            else if (!strcmp(kv[k], "Active")) s->active = !strcmp(v, "yes");
        }
        g_strfreev(kv);
        if (s->id[0]) u->nses++;
    }
    g_strfreev(blocks);
}

static void us_fill(users *u);

static void us_show_done(GObject *src, GAsyncResult *res, gpointer d)
{
    users *u = d;
    char *out = NULL;
    if (g_subprocess_communicate_utf8_finish(G_SUBPROCESS(src), res, &out, NULL, NULL) && out && !u->dead) us_parse(u, out);
    g_free(out);
    u->loading = 0;
    g_object_unref(src);
    if (u->snap && !u->dead) us_fill(u);
    g_object_unref(u->page);
}

static void us_list_done(GObject *src, GAsyncResult *res, gpointer d)
{
    users *u = d;
    char *out = NULL;
    GPtrArray *av = g_ptr_array_new();
    GSubprocess *p;
    if (!g_subprocess_communicate_utf8_finish(G_SUBPROCESS(src), res, &out, NULL, NULL) || !out || u->dead) {
        g_free(out);
        u->loading = 0;
        g_object_unref(src);
        g_ptr_array_free(av, TRUE);
        g_object_unref(u->page);
        return;
    }
    g_object_unref(src);
    /* loginctl show-session -p ... -- ID...: the ids are checked (session_id_ok) and come after "--" */
    g_ptr_array_add(av, (gpointer)prog_loginctl());
    g_ptr_array_add(av, (gpointer)"show-session");
    g_ptr_array_add(av, (gpointer)"-p");
    g_ptr_array_add(av, (gpointer)"Id,Name,User,State,Active,Type,Class,Seat,TTY,Remote,RemoteHost,Service,Desktop,Leader");
    g_ptr_array_add(av, (gpointer)"--");
    {
        char **l = g_strsplit(out, "\n", -1);
        for (int i = 0; l[i]; i++) {
            char *t = g_strstrip(l[i]), *sp = t + strcspn(t, " \t");
            *sp = 0;
            if (session_id_ok(t)) g_ptr_array_add(av, g_strdup(t));
        }
        g_strfreev(l);
    }
    g_free(out);
    if (av->len == 5) { us_parse(u, ""); u->loading = 0; if (u->snap) us_fill(u); g_ptr_array_free(av, TRUE); g_object_unref(u->page); return; }
    g_ptr_array_add(av, NULL);
    p = g_subprocess_newv((const char *const *)av->pdata, G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL);
    for (guint i = 5; i + 1 < av->len; i++) g_free(av->pdata[i]);
    g_ptr_array_free(av, TRUE);
    if (!p) { u->loading = 0; g_object_unref(u->page); return; }
    g_subprocess_communicate_utf8_async(p, NULL, NULL, us_show_done, u);
}

/* sessions from logind (asynchronous; at most every 5 s) */
static void us_load(users *u, int force)
{
    gint64 now = g_get_monotonic_time();
    GSubprocess *p;
    const char *argv[] = { prog_loginctl(), "list-sessions", "--no-legend", "--no-pager", NULL };
    if (u->loading || (!force && now - u->last_load < 5 * G_TIME_SPAN_SECOND)) return;
    u->last_load = now;
    p = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL);
    if (!p) return;
    u->loading = 1;
    g_object_ref(u->page);
    g_subprocess_communicate_utf8_async(p, NULL, NULL, us_list_done, u);
}

static const char *my_session(void) { const char *s = getenv("XDG_SESSION_ID"); return s && *s ? s : NULL; }

static GtkTreeIter *us_row(users *u, const char *key, GtkTreeIter *parent, int *isnew)
{
    GtkTreeIter *it = g_hash_table_lookup(u->rows, key);
    *isnew = 0;
    if (!it) {
        it = g_new(GtkTreeIter, 1);
        gtk_tree_store_append(u->store, it, parent);
        g_hash_table_insert(u->rows, g_strdup(key), it);
        *isnew = 1;
    }
    g_hash_table_insert(u->gens, g_strdup(key), GUINT_TO_POINTER(u->gen));
    return it;
}

static gboolean us_stale_child(gpointer k, gpointer v, gpointer d)
{
    users *u = d;
    const char *key = k;
    if (key[0] != 'p' || GPOINTER_TO_UINT(g_hash_table_lookup(u->gens, key)) == u->gen) return FALSE;
    gtk_tree_store_remove(u->store, v);
    g_hash_table_remove(u->gens, key);
    return TRUE;
}

static gboolean us_stale_top(gpointer k, gpointer v, gpointer d)
{
    users *u = d;
    const char *key = k;
    if (GPOINTER_TO_UINT(g_hash_table_lookup(u->gens, key)) == u->gen) return FALSE;
    gtk_tree_store_remove(u->store, v);
    g_hash_table_remove(u->gens, key);
    return TRUE;
}

static void us_fill(users *u)
{
    const pd_snap *s = u->snap;
    int total = 0;
    u->gen++;
    for (int i = 0; i < u->nses; i++) {
        session *se = &u->ses[i];
        char key[48], ty[96], st[64];
        int isnew, np = 0;
        double cpu = 0;
        long long mem = 0;
        GtkTreeIter *si;
        int manager = !strcmp(se->cls, "manager");
        snprintf(key, sizeof key, "s%s", se->id);
        si = us_row(u, key, NULL, &isnew);
        for (int k = 0; k < s->nproc; k++) {
            const pd_proc *p = &s->proc[k];
            int mine = manager ? (p->uid == se->uid && p->session < 0 && !p->kthread) : (p->session >= 0 && atoi(se->id) == p->session && se->id[0] != 'c');
            char pk[24];
            GtkTreeIter *pi;
            int pn;
            if (!mine) continue;
            np++;
            cpu += p->cpu;
            mem += p->rss;
            snprintf(pk, sizeof pk, "p%d", p->pid);
            pi = us_row(u, pk, si, &pn);
            gtk_tree_store_set(u->store, pi, UC_NAME, p->name, UC_STATUS, p->state == 'R' ? "Running" : p->state == 'Z' ? "Zombie" : p->state == 'D' ? "Disk Sleep" : p->state == 'T' ? "Suspended" : "Sleeping",
                               UC_PROCS, -1, UC_CPU, p->cpu, UC_MEM, (gint64)p->rss, UC_PID, p->pid, UC_KIND, 1, UC_SID, se->id, -1);
        }
        total += np;
        snprintf(ty, sizeof ty, "%s%s%s%s%s", manager ? "user services" : se->type, se->seat[0] ? " · " : "", se->seat,
                 se->tty[0] ? " · " : "", se->tty);
        if (se->remote) snprintf(ty + strlen(ty), sizeof ty - strlen(ty), " · remote %s", se->remote_host);
        snprintf(st, sizeof st, "%s%s", se->state, my_session() && !strcmp(se->id, my_session()) ? " (this session)" : "");
        gtk_tree_store_set(u->store, si, UC_NAME, se->user, UC_SESSION, se->id, UC_TYPE, ty, UC_STATUS, st, UC_PROCS, np, UC_CPU, cpu,
                           UC_MEM, (gint64)mem, UC_PID, se->leader, UC_KIND, 0, UC_SID, se->id, -1);
        if (isnew && !manager) {
            GtkTreeIter fi;
            if (gtk_tree_model_filter_convert_child_iter_to_iter(GTK_TREE_MODEL_FILTER(u->filter), &fi, si)) {
                GtkTreePath *pa = gtk_tree_model_get_path(u->filter, &fi);
                gtk_tree_view_expand_row(GTK_TREE_VIEW(u->view), pa, FALSE);
                gtk_tree_path_free(pa);
            }
        }
    }
    g_hash_table_foreach_remove(u->rows, us_stale_child, u);
    g_hash_table_foreach_remove(u->rows, us_stale_top, u);
    {
        char b[128];
        snprintf(b, sizeof b, "%d sessions · %d processes in them", u->nses, total);
        gtk_label_set_text(GTK_LABEL(u->count), b);
    }
}

void pa_users_update(GtkWidget *page, const pd_snap *s)
{
    users *u = g_object_get_data(G_OBJECT(page), "users");
    if (!u) return;
    if (!u->snap) u->snap = g_new0(pd_snap, 1);
    pd_snap_copy(u->snap, s);
    us_load(u, 0);
    us_fill(u);
}

static session *us_selected(users *u)
{
    GtkTreeModel *m;
    GtkTreeIter it;
    gchar *sid = NULL;
    int kind;
    session *r = NULL;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(u->view)), &m, &it)) return NULL;
    gtk_tree_model_get(m, &it, UC_SID, &sid, UC_KIND, &kind, -1);
    if (sid && kind == 0) for (int i = 0; i < u->nses; i++) if (!strcmp(u->ses[i].id, sid)) r = &u->ses[i];
    g_free(sid);
    return r;
}

static void us_sel(GtkTreeSelection *sel, gpointer d)
{
    users *u = d;
    session *s = us_selected(u);
    const char *why = NULL;
    (void)sel;
    if (!s) why = "Select a session (a user row) to disconnect it";
    else if (my_session() && !strcmp(s->id, my_session())) why = "This is your current session";
    else if (!strcmp(s->cls, "manager")) why = "This is the user service manager, not a login session";
    else if (!my_session() && s->uid == (int)getuid()) why = "This may be your current session (SRS cannot tell which one it runs in)";
    gtk_widget_set_sensitive(u->b_disc, why == NULL);
    gtk_widget_set_tooltip_text(u->b_disc, why ? why : "End this session (loginctl terminate-session): every program in it is closed");
}

static void us_term_done(GObject *src, GAsyncResult *res, gpointer d)
{
    users *u = d;
    char *err = NULL;
    GError *ge = NULL;
    if (u->dead) { g_subprocess_communicate_utf8_finish(G_SUBPROCESS(src), res, NULL, NULL, NULL); g_object_unref(src); g_object_unref(u->page); return; }
    if (!g_subprocess_communicate_utf8_finish(G_SUBPROCESS(src), res, NULL, &err, &ge) || !g_subprocess_get_successful(G_SUBPROCESS(src)))
        ib_show(&u->ib, GTK_MESSAGE_ERROR, ge ? ge->message : err && *err ? g_strstrip(err) : "loginctl failed.");
    else ib_show(&u->ib, GTK_MESSAGE_INFO, "The session was ended.");
    if (ge) g_error_free(ge);
    g_free(err);
    g_object_unref(src);
    us_load(u, 1);
    g_object_unref(u->page);
}

/* the session with this id in the current list, or NULL (the list is replaced every few seconds) */
static session *us_find(users *u, const char *id)
{
    for (int i = 0; i < u->nses; i++) if (!strcmp(u->ses[i].id, id)) return &u->ses[i];
    return NULL;
}

static void us_disconnect(GtkButton *b, gpointer d)
{
    users *u = d;
    session *s = us_selected(u);
    char *t, *det, id[32], user[64], cls[24];
    int uid;
    (void)b;
    if (!s || (my_session() && !strcmp(s->id, my_session())) || !strcmp(s->cls, "manager") || !session_id_ok(s->id)) return;
    /* copies: the dialog below runs a main loop, and the session list (u->ses) is re-read and freed meanwhile */
    snprintf(id, sizeof id, "%s", s->id);
    snprintf(user, sizeof user, "%s", s->user);
    snprintf(cls, sizeof cls, "%s", s->cls);
    uid = s->uid;
    t = g_strdup_printf("Disconnect session %s of %s?", id, user);
    det = g_strdup_printf("Type: %s%s%s. Every program running in this session is closed; unsaved work in it is lost.",
                          s->type, s->tty[0] ? ", " : "", s->tty);
    s = NULL;
    if (confirm(u->page, t, det, NULL, "_Disconnect")) {
        const char *argv[] = { prog_loginctl(), "terminate-session", "--", id, NULL };
        session *now = us_find(u, id);
        /* checked again after the dialog: still there, still not this session, not the service manager, same user */
        if (!now || (my_session() && !strcmp(id, my_session())) || !strcmp(now->cls, "manager") || strcmp(cls, now->cls) ||
            now->uid != uid) {
            ib_show(&u->ib, GTK_MESSAGE_ERROR, "That session changed or ended while the question was open: nothing was done.");
            g_free(t);
            g_free(det);
            return;
        }
        GError *ge = NULL;
        GSubprocess *p = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDERR_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ge);
        if (!p) { ib_show(&u->ib, GTK_MESSAGE_ERROR, ge ? ge->message : "Cannot run loginctl."); if (ge) g_error_free(ge); }
        else { g_object_ref(u->page); g_subprocess_communicate_utf8_async(p, NULL, NULL, us_term_done, u); }
    }
    g_free(t);
    g_free(det);
}

static void us_refresh(GtkButton *b, gpointer d) { (void)b; us_load(d, 1); }

static gboolean us_visible(GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
    users *u = d;
    gchar *n = NULL;
    int ok;
    if (!u->ftxt[0]) return TRUE;
    gtk_tree_model_get(m, it, UC_NAME, &n, -1);
    {
        char *a = g_utf8_strdown(n ? n : "", -1);
        ok = strstr(a, u->ftxt) != NULL;
        g_free(a);
    }
    g_free(n);
    if (!ok) {   /* a session stays when one of its processes matches */
        GtkTreeIter c;
        if (gtk_tree_model_iter_children(m, &c, it)) do { if (us_visible(m, &c, d)) return TRUE; } while (gtk_tree_model_iter_next(m, &c));
    }
    return ok;
}

static void us_filter(GtkEditable *e, gpointer d)
{
    users *u = d;
    char *t = g_utf8_strdown(gtk_entry_get_text(GTK_ENTRY(e)), -1);
    snprintf(u->ftxt, sizeof u->ftxt, "%s", g_strstrip(t));
    g_free(t);
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(u->filter));
    if (u->ftxt[0]) gtk_tree_view_expand_all(GTK_TREE_VIEW(u->view));
}

static void cell_u(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
    int col = GPOINTER_TO_INT(d), kind;
    char b[48] = "";
    (void)c;
    gtk_tree_model_get(m, it, UC_KIND, &kind, -1);
    switch (col) {
    case UC_PROCS: { int v; gtk_tree_model_get(m, it, col, &v, -1); if (kind == 0) snprintf(b, sizeof b, "%d", v); break; }
    case UC_PID: { int v; gtk_tree_model_get(m, it, col, &v, -1); if (v > 0) snprintf(b, sizeof b, "%d", v); break; }
    case UC_CPU: {
        double v;
        gtk_tree_model_get(m, it, col, &v, -1);
        snprintf(b, sizeof b, "%.1f %%", v);
        if (v >= 0.05) { GdkRGBA bg = { 0.15, 0.62, 0.25, 0.22 + 0.40 * fmin(1.0, v / 25.0) }; g_object_set(r, "cell-background-rgba", &bg, NULL); }
        else g_object_set(r, "cell-background-set", FALSE, NULL);
        break;
    }
    case UC_MEM: { gint64 v; gtk_tree_model_get(m, it, col, &v, -1); pd_fmt_bytes((double)v, b, sizeof b); break; }
    }
    g_object_set(r, "text", b, NULL);
}

static void cell_uname(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
    int kind;
    (void)c; (void)d;
    gtk_tree_model_get(m, it, UC_KIND, &kind, -1);
    g_object_set(r, "weight", kind == 0 ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL, NULL);
}

static void us_destroy(GtkWidget *w, gpointer d) { (void)w; ((users *)d)->dead = 1; }

static void us_free(gpointer d)
{
    users *u = d;
    g_hash_table_destroy(u->rows);
    g_hash_table_destroy(u->gens);
    g_object_unref(u->store);
    g_free(u->ses);
    if (u->snap) { pd_snap_free(u->snap); g_free(u->snap); }
    g_free(u);
}

static PangoAttrList *tnum_attrs(void)
{
    static PangoAttrList *a;
    if (!a) { a = pango_attr_list_new(); pango_attr_list_insert(a, pango_attr_font_features_new("tnum")); }
    return a;
}

GtkWidget *pa_users_new(void)
{
    users *u = g_new0(users, 1);
    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6), *right, *tb, *sw, *refresh;
    GtkCellRenderer *r;
    GtkTreeViewColumn *c;
    u->page = b;
    gtk_container_set_border_width(GTK_CONTAINER(b), 8);
    gtk_box_pack_start(GTK_BOX(b), header("Users", &right), FALSE, FALSE, 0);
    u->filter_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(u->filter_entry), "Filter users");
    g_signal_connect(u->filter_entry, "search-changed", G_CALLBACK(us_filter), u);
    gtk_box_pack_end(GTK_BOX(right), u->filter_entry, FALSE, FALSE, 0);
    tb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    u->b_disc = mk_button("Disc_onnect…", NULL);
    gtk_widget_set_sensitive(u->b_disc, FALSE);
    g_signal_connect(u->b_disc, "clicked", G_CALLBACK(us_disconnect), u);
    refresh = icon_button("view-refresh-symbolic", "Read the sessions again");
    g_signal_connect(refresh, "clicked", G_CALLBACK(us_refresh), u);
    gtk_box_pack_start(GTK_BOX(tb), u->b_disc, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(tb), run_task_button(), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(tb), refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(b), tb, FALSE, FALSE, 0);
    ib_make(&u->ib);
    gtk_box_pack_start(GTK_BOX(b), u->ib.bar, FALSE, FALSE, 0);
    u->store = gtk_tree_store_new(UC_N, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_DOUBLE,
                                  G_TYPE_INT64, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING);
    u->filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(u->store), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(u->filter), us_visible, u, NULL);
    u->view = gtk_tree_view_new_with_model(u->filter);
    g_object_unref(u->filter);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(u->view), FALSE);
    r = gtk_cell_renderer_text_new();
    c = gtk_tree_view_column_new_with_attributes("User", r, "text", UC_NAME, NULL);
    gtk_tree_view_column_set_cell_data_func(c, r, cell_uname, NULL, NULL);
    gtk_tree_view_column_set_expand(c, TRUE);
    gtk_tree_view_column_set_resizable(c, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(u->view), c);
    r = gtk_cell_renderer_text_new();
    g_object_set(r, "xalign", 1.0, "attributes", tnum_attrs(), NULL);
    c = gtk_tree_view_column_new_with_attributes("Session", r, "text", UC_SESSION, NULL);
    gtk_tree_view_column_set_alignment(c, 1.0);
    gtk_tree_view_append_column(GTK_TREE_VIEW(u->view), c);
    r = gtk_cell_renderer_text_new();
    c = gtk_tree_view_column_new_with_attributes("Type", r, "text", UC_TYPE, NULL);
    gtk_tree_view_column_set_resizable(c, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(u->view), c);
    r = gtk_cell_renderer_text_new();
    c = gtk_tree_view_column_new_with_attributes("Status", r, "text", UC_STATUS, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(u->view), c);
    {
        static const struct { const char *t; int col; } nc[] = { { "Processes", UC_PROCS }, { "CPU", UC_CPU }, { "Memory", UC_MEM }, { "PID", UC_PID } };
        for (int i = 0; i < 4; i++) {
            r = gtk_cell_renderer_text_new();
            g_object_set(r, "xalign", 1.0, "attributes", tnum_attrs(), NULL);
            c = gtk_tree_view_column_new_with_attributes(nc[i].t, r, NULL);
            gtk_tree_view_column_set_cell_data_func(c, r, cell_u, GINT_TO_POINTER(nc[i].col), NULL);
            gtk_tree_view_column_set_alignment(c, 1.0);
            gtk_tree_view_column_set_min_width(c, 80);
            gtk_tree_view_append_column(GTK_TREE_VIEW(u->view), c);
        }
    }
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(u->view)), "changed", G_CALLBACK(us_sel), u);
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw), u->view);
    gtk_box_pack_start(GTK_BOX(b), sw, TRUE, TRUE, 0);
    u->count = mk_label("Reading sessions…", 0, "pv-dim");
    gtk_box_pack_start(GTK_BOX(b), u->count, FALSE, FALSE, 0);
    u->rows = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    u->gens = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_object_set_data_full(G_OBJECT(b), "users", u, us_free);   /* freed when the last async reply let go of the page */
    g_signal_connect(b, "destroy", G_CALLBACK(us_destroy), u);
    return b;
}

/* ================================================================== Services */

enum { VC_RUN, VC_NAME, VC_STATE, VC_PID, VC_STARTUP, VC_DESC, VC_N };

typedef struct {
    GtkWidget *page, *view, *user, *filter_entry, *b_start, *b_stop, *b_restart, *count;
    GtkListStore *store;
    GtkTreeModel *filter;
    infobar ib;
    char ftxt[128];
    int loading, busy;
    char *files_out;               /* list-unit-files output while the second call runs */
    int dead;
} services;

static void sv_load(services *v);

static int sv_user(services *v) { return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(v->user)); }

typedef struct { char *state, *sub, *desc, *startup; int pid; } unitrow;

static void ur_free(gpointer p) { unitrow *r = p; g_free(r->state); g_free(r->sub); g_free(r->desc); g_free(r->startup); g_free(r); }

static void sv_show_done(GObject *src, GAsyncResult *res, gpointer d)
{
    services *v = d;
    char *out = NULL, *keep = NULL;
    GHashTable *h = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, ur_free);
    GtkTreeModel *m;
    GtkTreeIter it;
    int nrun = 0;
    g_subprocess_communicate_utf8_finish(G_SUBPROCESS(src), res, &out, NULL, NULL);
    g_object_unref(src);
    if (v->dead) { g_free(out); g_hash_table_destroy(h); v->loading = 0; g_object_unref(v->page); return; }
    /* unit files first (installed, maybe not loaded), then the loaded units' state */
    if (v->files_out) {
        char **l = g_strsplit(v->files_out, "\n", -1);
        for (int i = 0; l[i]; i++) {
            char **w = g_strsplit_set(g_strstrip(l[i]), " \t", -1), *n = w[0], *st = NULL;
            for (int k = 1; w[k]; k++) if (*w[k]) { st = w[k]; break; }
            if (n && g_str_has_suffix(n, ".service") && !strstr(n, "@.")) {
                unitrow *r = g_new0(unitrow, 1);
                r->startup = g_strdup(st ? st : "");
                r->state = g_strdup("inactive");
                g_hash_table_replace(h, g_strdup(n), r);
            }
            g_strfreev(w);
        }
        g_strfreev(l);
    }
    if (out) {
        char **blocks = g_regex_split_simple("\\n\\s*\\n", out, 0, 0);
        for (int b = 0; blocks[b]; b++) {
            char **kv = g_strsplit(blocks[b], "\n", -1), *id = NULL;
            unitrow t = { 0 };
            for (int k = 0; kv[k]; k++) {
                char *eq = strchr(kv[k], '=');
                if (!eq) continue;
                *eq = 0;
                if (!strcmp(kv[k], "Id")) id = g_strdup(eq + 1);
                else if (!strcmp(kv[k], "ActiveState")) t.state = g_strdup(eq + 1);
                else if (!strcmp(kv[k], "SubState")) t.sub = g_strdup(eq + 1);
                else if (!strcmp(kv[k], "Description")) t.desc = g_strdup(eq + 1);
                else if (!strcmp(kv[k], "UnitFileState")) t.startup = g_strdup(eq + 1);
                else if (!strcmp(kv[k], "MainPID")) t.pid = atoi(eq + 1);
            }
            g_strfreev(kv);
            if (id && g_str_has_suffix(id, ".service")) {
                unitrow *r = g_hash_table_lookup(h, id);
                if (!r) { r = g_new0(unitrow, 1); g_hash_table_insert(h, g_strdup(id), r); }
                g_free(r->state); r->state = t.state; t.state = NULL;
                g_free(r->sub); r->sub = t.sub; t.sub = NULL;
                g_free(r->desc); r->desc = t.desc; t.desc = NULL;
                if (t.startup && *t.startup) { g_free(r->startup); r->startup = t.startup; t.startup = NULL; }
                r->pid = t.pid;
            }
            g_free(id); g_free(t.state); g_free(t.sub); g_free(t.desc); g_free(t.startup);
        }
        g_strfreev(blocks);
    }
    g_free(out);
    g_free(v->files_out);
    v->files_out = NULL;
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->view)), &m, &it))
        gtk_tree_model_get(m, &it, VC_NAME, &keep, -1);
    gtk_list_store_clear(v->store);
    {
        GList *keys = g_list_sort(g_hash_table_get_keys(h), (GCompareFunc)g_ascii_strcasecmp);
        for (GList *k = keys; k; k = k->next) {
            unitrow *r = g_hash_table_lookup(h, k->data);
            char st[64];
            GtkTreeIter ni;
            int run = r->state && (!strcmp(r->state, "active") || !strcmp(r->state, "activating") || !strcmp(r->state, "reloading"));
            snprintf(st, sizeof st, "%s%s%s%s", r->state ? r->state : "", r->sub ? " (" : "", r->sub ? r->sub : "", r->sub ? ")" : "");
            gtk_list_store_insert_with_values(v->store, &ni, -1, VC_RUN, run, VC_NAME, k->data, VC_STATE, st, VC_PID, r->pid,
                                              VC_STARTUP, r->startup ? r->startup : "", VC_DESC, r->desc ? r->desc : "", -1);
            nrun += run;
            if (keep && !strcmp(keep, k->data)) {
                GtkTreeIter fi;
                if (gtk_tree_model_filter_convert_child_iter_to_iter(GTK_TREE_MODEL_FILTER(v->filter), &fi, &ni)) {
                    GtkTreePath *pa = gtk_tree_model_get_path(v->filter, &fi);
                    gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->view)), &fi);
                    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(v->view), pa, NULL, FALSE, 0, 0);
                    gtk_tree_path_free(pa);
                }
            }
        }
        g_list_free(keys);
    }
    {
        char b[128];
        snprintf(b, sizeof b, "%u %s services · %d running", g_hash_table_size(h), sv_user(v) ? "user" : "system", nrun);
        gtk_label_set_text(GTK_LABEL(v->count), b);
    }
    g_free(keep);
    g_hash_table_destroy(h);
    v->loading = 0;
    g_object_unref(v->page);
}

static void sv_files_done(GObject *src, GAsyncResult *res, gpointer d)
{
    services *v = d;
    GSubprocess *p;
    const char *argv[12];
    int n = 0;
    g_subprocess_communicate_utf8_finish(G_SUBPROCESS(src), res, &v->files_out, NULL, NULL);
    g_object_unref(src);
    if (v->dead) { v->loading = 0; g_object_unref(v->page); return; }
    argv[n++] = prog_systemctl();
    if (sv_user(v)) argv[n++] = "--user";
    argv[n++] = "show";
    argv[n++] = "--no-pager";
    argv[n++] = "--property=Id,Description,MainPID,ActiveState,SubState,UnitFileState";
    argv[n++] = "*.service";
    argv[n] = NULL;
    p = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL);
    if (!p) { v->loading = 0; g_object_unref(v->page); return; }
    g_subprocess_communicate_utf8_async(p, NULL, NULL, sv_show_done, v);
}

static void sv_load(services *v)
{
    const char *argv[8];
    int n = 0;
    GSubprocess *p;
    if (v->loading) return;
    argv[n++] = prog_systemctl();
    if (sv_user(v)) argv[n++] = "--user";
    argv[n++] = "list-unit-files";
    argv[n++] = "--type=service";
    argv[n++] = "--no-legend";
    argv[n++] = "--no-pager";
    argv[n] = NULL;
    p = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL);
    if (!p) { ib_show(&v->ib, GTK_MESSAGE_ERROR, "Cannot run systemctl."); return; }
    v->loading = 1;
    g_object_ref(v->page);
    gtk_label_set_text(GTK_LABEL(v->count), "Reading services…");
    g_subprocess_communicate_utf8_async(p, NULL, NULL, sv_files_done, v);
}

static int sv_selected(services *v, char **name, int *running)
{
    GtkTreeModel *m;
    GtkTreeIter it;
    gboolean run;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->view)), &m, &it)) return 0;
    gtk_tree_model_get(m, &it, VC_NAME, name, VC_RUN, &run, -1);
    if (running) *running = run;
    return 1;
}

static void sv_buttons(services *v)
{
    char *n = NULL;
    int run = 0, sel = sv_selected(v, &n, &run);
    gtk_widget_set_sensitive(v->b_start, sel && !run && !v->busy);
    gtk_widget_set_sensitive(v->b_stop, sel && run && !v->busy);
    gtk_widget_set_sensitive(v->b_restart, sel && !v->busy);
    g_free(n);
}

static const char *critical_why(const char *u, int user)
{
    static const char *const pre[] = { "systemd-", "dbus", "NetworkManager", "display-manager", "sddm", "gdm", "lightdm", "polkit",
                                       "user@", "user-runtime-dir@", "getty@", "plasma-", "kwin", "pipewire", "wireplumber",
                                       "xdg-desktop-portal", "elogind", "udisks2", "upower", NULL };
    for (int i = 0; pre[i]; i++)
        if (!strncmp(u, pre[i], strlen(pre[i])))
            return user ? "This unit is part of your desktop session: stopping it can freeze or close the session."
                        : "This unit is part of the system's core: stopping it can cut the network, log you out or freeze the PC.";
    return NULL;
}

typedef struct { services *v; char *verb, *unit; } svjob;

static void sv_action_done(GObject *src, GAsyncResult *res, gpointer d)
{
    svjob *j = d;
    services *v = j->v;
    char *err = NULL;
    GError *ge = NULL;
    if (v->dead) {
        g_subprocess_communicate_utf8_finish(G_SUBPROCESS(src), res, NULL, NULL, NULL);
        g_object_unref(src);
        g_free(j->verb); g_free(j->unit); g_free(j);
        g_object_unref(v->page);
        return;
    }
    if (!g_subprocess_communicate_utf8_finish(G_SUBPROCESS(src), res, NULL, &err, &ge) || !g_subprocess_get_successful(G_SUBPROCESS(src))) {
        char *m = g_strdup_printf("%s %s failed: %s", j->verb, j->unit, ge ? ge->message : err && *err ? g_strstrip(err) : "error");
        ib_show(&v->ib, GTK_MESSAGE_ERROR, m);
        g_free(m);
    } else {
        char *m = g_strdup_printf("%s %s: done.", j->verb, j->unit);
        ib_show(&v->ib, GTK_MESSAGE_INFO, m);
        g_free(m);
    }
    if (ge) g_error_free(ge);
    g_free(err);
    g_object_unref(src);
    v->busy = 0;
    sv_load(v);
    sv_buttons(v);
    g_free(j->verb);
    g_free(j->unit);
    g_free(j);
    g_object_unref(v->page);
}

static void sv_action(services *v, const char *verb)
{
    char *name = NULL, *t;
    int user = sv_user(v);
    const char *warn;
    if (!sv_selected(v, &name, NULL) || v->busy) { g_free(name); return; }
    t = g_strdup_printf("%c%s %s?", g_ascii_toupper(verb[0]), verb + 1, name);
    warn = strcmp(verb, "start") ? critical_why(name, user) : NULL;
    if (confirm(v->page, t, user ? "A user service: it runs as you." :
                "A system service: your password may be asked for (polkit).", warn, strcmp(verb, "start") ? (strcmp(verb, "stop") ? "_Restart" : "S_top") : "_Start")) {
        const char *argv[7];
        int n = 0;
        GError *ge = NULL;
        GSubprocess *p;
        argv[n++] = prog_systemctl();
        if (user) argv[n++] = "--user";
        argv[n++] = verb;
        argv[n++] = "--";   /* unit names such as -.mount / -.slice must not be read as options */
        argv[n++] = name;
        argv[n] = NULL;
        p = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDERR_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ge);
        if (!p) { ib_show(&v->ib, GTK_MESSAGE_ERROR, ge ? ge->message : "Cannot run systemctl."); if (ge) g_error_free(ge); }
        else {
            svjob *j = g_new0(svjob, 1);
            j->v = v;
            j->verb = g_strdup_printf("%c%s", g_ascii_toupper(verb[0]), verb + 1);
            j->unit = g_strdup(name);
            v->busy = 1;
            sv_buttons(v);
            g_object_ref(v->page);
            g_subprocess_communicate_utf8_async(p, NULL, NULL, sv_action_done, j);
        }
    }
    g_free(t);
    g_free(name);
}

static void sv_start(GtkButton *b, gpointer d) { (void)b; sv_action(d, "start"); }
static void sv_stop(GtkButton *b, gpointer d) { (void)b; sv_action(d, "stop"); }
static void sv_restart(GtkButton *b, gpointer d) { (void)b; sv_action(d, "restart"); }
static void sv_refresh(GtkButton *b, gpointer d) { (void)b; sv_load(d); }
static void sv_user_toggled(GtkToggleButton *b, gpointer d) { (void)b; sv_load(d); }
static void sv_sel(GtkTreeSelection *s, gpointer d) { (void)s; sv_buttons(d); }

static void sv_toggled(GtkCellRendererToggle *r, gchar *path, gpointer d)
{
    services *v = d;
    GtkTreeIter it;
    gboolean run;
    (void)r;
    if (!gtk_tree_model_get_iter_from_string(v->filter, &it, path)) return;
    gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->view)), &it);
    gtk_tree_model_get(v->filter, &it, VC_RUN, &run, -1);
    sv_action(v, run ? "stop" : "start");
}

static gboolean sv_visible(GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
    services *v = d;
    gchar *n = NULL, *de = NULL;
    int ok;
    if (!v->ftxt[0]) return TRUE;
    gtk_tree_model_get(m, it, VC_NAME, &n, VC_DESC, &de, -1);
    {
        char *a = g_utf8_strdown(n ? n : "", -1), *b = g_utf8_strdown(de ? de : "", -1);
        ok = strstr(a, v->ftxt) || strstr(b, v->ftxt);
        g_free(a); g_free(b);
    }
    g_free(n); g_free(de);
    return ok;
}

static void sv_filter(GtkEditable *e, gpointer d)
{
    services *v = d;
    char *t = g_utf8_strdown(gtk_entry_get_text(GTK_ENTRY(e)), -1);
    snprintf(v->ftxt, sizeof v->ftxt, "%s", g_strstrip(t));
    g_free(t);
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(v->filter));
}

static void cell_pid(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
    int pid;
    char b[24];
    (void)c; (void)d;
    gtk_tree_model_get(m, it, VC_PID, &pid, -1);
    if (pid > 0) snprintf(b, sizeof b, "%d", pid);
    else snprintf(b, sizeof b, EM);
    g_object_set(r, "text", b, NULL);
}

static void sv_map(GtkWidget *w, gpointer d) { (void)w; sv_load(d); }
static void sv_destroy(GtkWidget *w, gpointer d) { (void)w; ((services *)d)->dead = 1; }
static void sv_free(gpointer d) { services *v = d; g_object_unref(v->store); g_free(v->files_out); g_free(v); }

GtkWidget *pa_services_new(void)
{
    services *v = g_new0(services, 1);
    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6), *right, *tb, *sw, *refresh;
    GtkCellRenderer *r;
    GtkTreeViewColumn *c;
    v->page = b;
    gtk_container_set_border_width(GTK_CONTAINER(b), 8);
    gtk_box_pack_start(GTK_BOX(b), header("Services", &right), FALSE, FALSE, 0);
    v->filter_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(v->filter_entry), "Filter services");
    gtk_entry_set_width_chars(GTK_ENTRY(v->filter_entry), 28);
    g_signal_connect(v->filter_entry, "search-changed", G_CALLBACK(sv_filter), v);
    v->user = gtk_check_button_new_with_mnemonic("User Ser_vices");
    gtk_widget_set_tooltip_text(v->user, "Show your own services (systemctl --user) instead of the system's");
    g_signal_connect(v->user, "toggled", G_CALLBACK(sv_user_toggled), v);
    gtk_box_pack_end(GTK_BOX(right), v->filter_entry, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(right), v->user, FALSE, FALSE, 0);
    tb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    v->b_start = mk_button("Start", "Start the selected service (asks first)");
    v->b_stop = mk_button("St_op", "Stop the selected service (asks first)");
    v->b_restart = mk_button("Restart", "Restart the selected service (asks first)");
    refresh = icon_button("view-refresh-symbolic", "Read the services again");
    g_signal_connect(v->b_start, "clicked", G_CALLBACK(sv_start), v);
    g_signal_connect(v->b_stop, "clicked", G_CALLBACK(sv_stop), v);
    g_signal_connect(v->b_restart, "clicked", G_CALLBACK(sv_restart), v);
    g_signal_connect(refresh, "clicked", G_CALLBACK(sv_refresh), v);
    gtk_box_pack_start(GTK_BOX(tb), v->b_start, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb), v->b_stop, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb), v->b_restart, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(tb), run_task_button(), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(tb), refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(b), tb, FALSE, FALSE, 0);
    ib_make(&v->ib);
    gtk_box_pack_start(GTK_BOX(b), v->ib.bar, FALSE, FALSE, 0);
    v->store = gtk_list_store_new(VC_N, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
    v->filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(v->store), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(v->filter), sv_visible, v, NULL);
    v->view = gtk_tree_view_new_with_model(v->filter);
    g_object_unref(v->filter);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(v->view), FALSE);
    r = gtk_cell_renderer_toggle_new();
    g_signal_connect(r, "toggled", G_CALLBACK(sv_toggled), v);
    c = gtk_tree_view_column_new_with_attributes("Running", r, "active", VC_RUN, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(v->view), c);
    r = gtk_cell_renderer_text_new();
    g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    c = gtk_tree_view_column_new_with_attributes("Name", r, "text", VC_NAME, NULL);
    gtk_tree_view_column_set_resizable(c, TRUE);
    gtk_tree_view_column_set_sizing(c, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(c, 300);
    gtk_tree_view_append_column(GTK_TREE_VIEW(v->view), c);
    r = gtk_cell_renderer_text_new();
    c = gtk_tree_view_column_new_with_attributes("State", r, "text", VC_STATE, NULL);
    gtk_tree_view_column_set_resizable(c, TRUE);
    gtk_tree_view_column_set_sizing(c, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(c, 150);
    gtk_tree_view_append_column(GTK_TREE_VIEW(v->view), c);
    r = gtk_cell_renderer_text_new();
    g_object_set(r, "xalign", 1.0, "attributes", tnum_attrs(), NULL);
    c = gtk_tree_view_column_new_with_attributes("PID", r, NULL);
    gtk_tree_view_column_set_cell_data_func(c, r, cell_pid, NULL, NULL);
    gtk_tree_view_column_set_alignment(c, 1.0);
    gtk_tree_view_column_set_min_width(c, 70);
    gtk_tree_view_append_column(GTK_TREE_VIEW(v->view), c);
    r = gtk_cell_renderer_text_new();
    c = gtk_tree_view_column_new_with_attributes("Startup", r, "text", VC_STARTUP, NULL);
    gtk_tree_view_column_set_min_width(c, 90);
    gtk_tree_view_append_column(GTK_TREE_VIEW(v->view), c);
    r = gtk_cell_renderer_text_new();
    g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    c = gtk_tree_view_column_new_with_attributes("Description", r, "text", VC_DESC, NULL);
    gtk_tree_view_column_set_expand(c, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(v->view), c);
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(v->view), VC_DESC);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->view)), "changed", G_CALLBACK(sv_sel), v);
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw), v->view);
    gtk_box_pack_start(GTK_BOX(b), sw, TRUE, TRUE, 0);
    v->count = mk_label("", 0, "pv-dim");
    gtk_box_pack_start(GTK_BOX(b), v->count, FALSE, FALSE, 0);
    sv_buttons(v);
    g_signal_connect(b, "map", G_CALLBACK(sv_map), v);
    g_object_set_data_full(G_OBJECT(b), "services", v, sv_free);
    g_signal_connect(b, "destroy", G_CALLBACK(sv_destroy), v);
    return b;
}
