/* perf_procs.c - process actions of the Performance tab (see perf_procs.h).
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE                /* sched_setaffinity, cpu_set_t */
#endif
#include "perf_procs.h"

#include <dirent.h>
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define EM "—"

/* ================================================================== the plain part */

const char *pp_untouchable(int pid, int ppid, int kthread)
{
    if (pid <= 1) return "PID 1 (init / systemd) keeps the whole system running: SRS never ends or changes it.";
    if (kthread || ppid == 2 || pid == 2) return "This is a kernel thread: it cannot be ended or changed from here.";
    return NULL;
}

int pp_stat(int pid, unsigned long long *start, char *state, int *ppid)
{
    char p[64], b[1024], *r;
    FILE *f;
    size_t n;
    unsigned long long st = 0;
    char s = 0;
    int pp = 0;
    snprintf(p, sizeof p, "/proc/%d/stat", pid);
    if (!(f = fopen(p, "r"))) return 0;
    n = fread(b, 1, sizeof b - 1, f);
    fclose(f);
    b[n] = 0;
    if (!(r = strrchr(b, ')'))) return 0;
    if (sscanf(r + 2, "%c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d %*d %*d %*d %*d %llu", &s, &pp, &st) != 3) return 0;
    if (start) *start = st;
    if (state) *state = s;
    if (ppid) *ppid = pp;
    return 1;
}

static int is_ancestor_of_me(int pid)
{
    int cur = getppid();
    for (int i = 0; i < 64 && cur > 1; i++) {
        int pp;
        if (cur == pid) return 1;
        if (!pp_stat(cur, NULL, NULL, &pp)) break;
        cur = pp;
    }
    return 0;
}

const char *pp_critical(int pid, const char *name)
{
    static const struct { const char *pre; const char *why; } c[] = {
        { "systemd", "This is part of systemd: ending it can log you out or stop the system." },
        { "startplasma", "This starts your Plasma session: ending it logs you out." },
        { "plasmashell", "This is the Plasma desktop (panel, wallpaper, widgets): it disappears until it is started again." },
        { "kwin_", "This is the window manager / display server: ending it closes the session and every window in it." },
        { "Xwayland", "This runs every X11 window: they all close with it." },
        { "sddm", "This is the login manager: ending it can end your session." },
        { "dbus", "This is the message bus the desktop talks over: ending it breaks the session." },
        { "pipewire", "This is the sound and video server: sound stops until it restarts." },
        { "wireplumber", "This manages sound devices: sound may stop until it restarts." },
        { "polkit", "This asks for passwords for system changes: they stop working until it restarts." },
        { NULL, NULL } };
    if (pid == getpid()) return "This is SRS itself: it will close.";
    if (pid == 1) return "PID 1 keeps the whole system running.";
    if (is_ancestor_of_me(pid)) return "SRS was started from this process (it is part of your session): ending it may close SRS and your session.";
    for (int i = 0; name && c[i].pre; i++) if (!strncmp(name, c[i].pre, strlen(c[i].pre))) return c[i].why;
    return NULL;
}

const char *pp_signame(int sig)
{
    switch (sig) {
    case SIGTERM: return "TERM";
    case SIGKILL: return "KILL";
    case SIGSTOP: return "STOP";
    case SIGCONT: return "CONT";
    default: return "TERM";
    }
}

static int same(int pid, unsigned long long start)
{
    unsigned long long st;
    char s;
    if (!pp_stat(pid, &st, &s, NULL)) return 0;
    return !start || st == start;
}

int pp_signal(int pid, unsigned long long start, int sig, char *err, size_t errlen)
{
    if (pid <= 1) { snprintf(err, errlen, "Refused: PID %d", pid); return -1; }
    if (!same(pid, start)) { snprintf(err, errlen, "It has already ended."); return 2; }
    if (kill(pid, sig) == 0) return 0;
    if (errno == EPERM) { snprintf(err, errlen, "Not allowed (it belongs to another user): %s", g_strerror(errno)); return 1; }
    if (errno == ESRCH) { snprintf(err, errlen, "It has already ended."); return 2; }
    snprintf(err, errlen, "kill failed: %s", g_strerror(errno));
    return -1;
}

/* every thread of pid (nice and affinity are per thread on Linux) */
static int tids(int pid, int *out, int max)
{
    char p[64];
    DIR *d;
    struct dirent *e;
    int n = 0;
    snprintf(p, sizeof p, "/proc/%d/task", pid);
    if (!(d = opendir(p))) { out[0] = pid; return 1; }
    while ((e = readdir(d)) && n < max) if (e->d_name[0] >= '0' && e->d_name[0] <= '9') out[n++] = atoi(e->d_name);
    closedir(d);
    if (!n) out[n++] = pid;
    return n;
}

int pp_renice(int pid, unsigned long long start, int nice, char *err, size_t errlen)
{
    int t[4096], n, bad = 0, eperm = 0;
    if (pid <= 1) { snprintf(err, errlen, "Refused: PID %d", pid); return -1; }
    if (!same(pid, start)) { snprintf(err, errlen, "It has already ended."); return 2; }
    n = tids(pid, t, 4096);
    for (int i = 0; i < n; i++)
        if (setpriority(PRIO_PROCESS, (id_t)t[i], nice) != 0) { bad = errno; if (errno == EPERM || errno == EACCES) eperm = 1; }
    if (!bad) return 0;
    snprintf(err, errlen, "%s", eperm ? "Not allowed: a higher priority (or another user's process) needs your password"
                                      : g_strerror(bad));
    return eperm ? 1 : -1;
}

static void set_to_list(const cpu_set_t *cs, char *out, size_t len)
{
    size_t o = 0;
    out[0] = 0;
    for (int i = 0; i < CPU_SETSIZE && o + 16 < len; i++) {
        int j;
        if (!CPU_ISSET(i, cs)) continue;
        for (j = i; j + 1 < CPU_SETSIZE && CPU_ISSET(j + 1, cs); j++) {}
        o += (size_t)snprintf(out + o, len - o, o ? ",%d" : "%d", i);
        if (j > i) o += (size_t)snprintf(out + o, len - o, "-%d", j);
        i = j;
    }
}

static int list_to_set(const char *l, cpu_set_t *cs)
{
    char *dup = g_strdup(l), *tok, *save = NULL;
    int n = 0;
    CPU_ZERO(cs);
    for (tok = strtok_r(dup, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        int a, b;
        if (sscanf(tok, "%d-%d", &a, &b) == 2) {} else if (sscanf(tok, "%d", &a) == 1) b = a; else continue;
        for (int i = a; i <= b && i < CPU_SETSIZE; i++) if (i >= 0) { CPU_SET(i, cs); n++; }
    }
    g_free(dup);
    return n;
}

int pp_get_affinity(int pid, char *list, size_t len)
{
    cpu_set_t cs;
    if (sched_getaffinity(pid, sizeof cs, &cs) != 0) { snprintf(list, len, EM); return -1; }
    set_to_list(&cs, list, len);
    return 0;
}

int pp_set_affinity(int pid, unsigned long long start, const char *list, char *err, size_t errlen)
{
    cpu_set_t cs;
    int t[4096], n, bad = 0, eperm = 0;
    if (pid <= 1) { snprintf(err, errlen, "Refused: PID %d", pid); return -1; }
    if (list_to_set(list, &cs) == 0) { snprintf(err, errlen, "Pick at least one logical processor."); return -1; }
    if (!same(pid, start)) { snprintf(err, errlen, "It has already ended."); return 2; }
    n = tids(pid, t, 4096);
    for (int i = 0; i < n; i++)
        if (sched_setaffinity(t[i], sizeof cs, &cs) != 0) { bad = errno; if (errno == EPERM) eperm = 1; }
    if (!bad) return 0;
    snprintf(err, errlen, "%s", eperm ? "Not allowed (another user's process)" : g_strerror(bad));
    return eperm ? 1 : -1;
}

int pp_tree(const pd_snap *s, int pid, int *out, int max)
{
    int *q = g_new(int, (size_t)s->nproc + 1), nq = 0, n = 0;
    q[nq++] = pid;
    for (int i = 0; i < nq && nq < s->nproc + 1; i++)
        for (int k = 0; k < s->nproc && nq < s->nproc + 1; k++)
            if (s->proc[k].ppid == q[i] && s->proc[k].pid != q[i]) q[nq++] = s->proc[k].pid;
    for (int i = nq - 1; i >= 1 && n < max; i--) out[n++] = q[i];   /* deepest first, without pid itself */
    g_free(q);
    return n;
}

char **pp_elevated_argv(const char *what, const char *arg, int pid)
{
    const char *pk = getenv("SRS_TEST_PKEXEC");
    char *tool = g_find_program_in_path(what), pids[24];
    GPtrArray *a = g_ptr_array_new();
    snprintf(pids, sizeof pids, "%d", pid);
    g_ptr_array_add(a, g_strdup(pk && *pk ? pk : "pkexec"));
    g_ptr_array_add(a, tool ? tool : g_strdup(what));   /* pkexec wants the full path */
    if (!strcmp(what, "kill")) { g_ptr_array_add(a, g_strdup("-s")); g_ptr_array_add(a, g_strdup(arg)); }
    else if (!strcmp(what, "renice")) {
        /* nice is per thread on Linux: every thread's id after -p (renice takes several), like pp_renice does */
        int t[4096], n = tids(pid, t, 4096);
        g_ptr_array_add(a, g_strdup("-n")); g_ptr_array_add(a, g_strdup(arg)); g_ptr_array_add(a, g_strdup("-p"));
        for (int i = 0; i < n; i++) if (t[i] != pid) g_ptr_array_add(a, g_strdup_printf("%d", t[i]));
    }
    else { g_ptr_array_add(a, g_strdup("-a")); g_ptr_array_add(a, g_strdup("-p")); g_ptr_array_add(a, g_strdup("-c")); g_ptr_array_add(a, g_strdup(arg)); }
    g_ptr_array_add(a, g_strdup(pids));
    g_ptr_array_add(a, NULL);
    return (char **)g_ptr_array_free(a, FALSE);
}

/* ================================================================== the GTK part */

enum { A_END, A_KILL, A_TREE, A_SUSPEND, A_RESUME, A_END_PARENT };

typedef struct { int pid; unsigned long long start; char name[48]; } target;

struct pp_ui {
    pp_sel_fn sel;
    pp_snap_fn snap;
    void *user;
    int dead;
    GtkWidget *tb, *bar, *bar_label, *bar_btn;
    GtkWidget *b_end, *b_kill, *b_tree, *b_susp, *b_res, *b_prio, *b_aff, *b_props;
    int bar_action;                /* what the info bar's button does: -1 none, A_KILL (Force Kill the watched), A_END_PARENT */
    int bar_pid;
    unsigned long long bar_start;
    /* processes signalled and being watched for up to wait_ms */
    GArray *watch;                 /* target */
    guint watch_timer;
    int watch_left_ms, watch_sig;
};

static const pd_proc *find(pp_ui *u, int pid)
{
    const pd_snap *s = u->snap(u->user);
    int i = s ? pd_find(s, pid) : -1;
    return i >= 0 ? &s->proc[i] : NULL;
}

static GtkWindow *top_of(GtkWidget *w)
{
    GtkWidget *t = w ? gtk_widget_get_toplevel(w) : NULL;
    return t && GTK_IS_WINDOW(t) ? GTK_WINDOW(t) : NULL;
}

static void bar_show(pp_ui *u, GtkMessageType t, const char *msg, const char *btn, int action, int pid, unsigned long long start)
{
    gtk_info_bar_set_message_type(GTK_INFO_BAR(u->bar), t);
    gtk_label_set_text(GTK_LABEL(u->bar_label), msg);
    u->bar_action = btn ? action : -1;
    u->bar_pid = pid;
    u->bar_start = start;
    if (btn) { gtk_button_set_label(GTK_BUTTON(u->bar_btn), btn); gtk_widget_show(u->bar_btn); }
    else gtk_widget_hide(u->bar_btn);
    gtk_widget_show(u->bar);
}

static void bar_hide(pp_ui *u) { gtk_widget_hide(u->bar); u->bar_action = -1; }

/* the selection as targets (with their start times, so a reused PID is never signalled) */
static int targets(pp_ui *u, target *t, int max)
{
    int pids[256], n = u->sel(u->user, pids, 256), m = 0;
    for (int i = 0; i < n && m < max; i++) {
        const pd_proc *p = find(u, pids[i]);
        if (!p) continue;
        t[m].pid = p->pid;
        t[m].start = p->start;
        snprintf(t[m].name, sizeof t[m].name, "%s", p->name);
        m++;
    }
    return m;
}

static int confirm(pp_ui *u, const char *title, const char *body, const char *warn, const char *ok)
{
    GtkWidget *d = gtk_message_dialog_new(top_of(u->bar), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                          warn ? GTK_MESSAGE_WARNING : GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", title);
    int r;
    if (warn) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s\n\n%s", body, warn);
    else gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", body);
    gtk_dialog_add_button(GTK_DIALOG(d), "_Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(d), ok, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_CANCEL);
    r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    return r == GTK_RESPONSE_OK;
}

/* ---- watching what was signalled ---- */

static void watch_report(pp_ui *u, int final)
{
    GString *ended = g_string_new(NULL), *alive = g_string_new(NULL);
    int nalive = 0, first_alive = -1;
    for (guint i = 0; i < u->watch->len; i++) {
        target *t = &g_array_index(u->watch, target, i);
        unsigned long long st;
        char s;
        int up = pp_stat(t->pid, &st, &s, NULL) && st == t->start && s != 'Z' && s != 'X';
        GString *g = up ? alive : ended;
        if (up) { nalive++; if (first_alive < 0) first_alive = (int)i; }
        if (g->len < 300) g_string_append_printf(g, "%s%s (%d)", g->len ? ", " : "", t->name, t->pid);
    }
    if (!nalive || final) {
        if (u->watch_timer) { g_source_remove(u->watch_timer); u->watch_timer = 0; }
        if (!nalive) {
            char *m = g_strdup_printf("%s ended.", ended->str);
            bar_show(u, GTK_MESSAGE_INFO, m, NULL, -1, 0, 0);
            g_free(m);
        } else {
            target *t = &g_array_index(u->watch, target, first_alive);
            char *m = g_strdup_printf("%s %s still running%s", alive->str, nalive > 1 ? "are" : "is",
                                      u->watch_sig == SIGKILL ? " (it may be stuck in the kernel, state D)." :
                                      ". Force Kill ends it at once (unsaved work is lost).");
            if (u->watch_sig == SIGKILL) bar_show(u, GTK_MESSAGE_WARNING, m, NULL, -1, 0, 0);
            else bar_show(u, GTK_MESSAGE_WARNING, m, "Force _Kill", A_KILL, t->pid, t->start);
            g_free(m);
        }
    }
    g_string_free(ended, TRUE);
    g_string_free(alive, TRUE);
}

static gboolean watch_tick(gpointer d)
{
    pp_ui *u = d;
    u->watch_left_ms -= 250;
    watch_report(u, u->watch_left_ms <= 0);
    if (u->watch_left_ms <= 0) { u->watch_timer = 0; return G_SOURCE_REMOVE; }
    return u->watch_timer ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

static void watch_start(pp_ui *u, int sig, int ms)
{
    u->watch_sig = sig;
    u->watch_left_ms = ms;
    if (!u->watch_timer) u->watch_timer = g_timeout_add(250, watch_tick, u);
}

/* ---- elevated (pkexec) runs ---- */

typedef struct { pp_ui *u; target t; int sig; char *what; } elev;

static void elev_done(GObject *src, GAsyncResult *res, gpointer d)
{
    elev *e = d;
    pp_ui *u = e->u;
    char *err = NULL;
    GError *ge = NULL;
    gboolean ok = g_subprocess_communicate_utf8_finish(G_SUBPROCESS(src), res, NULL, &err, &ge) &&
                  g_subprocess_get_successful(G_SUBPROCESS(src));
    if (!u->dead) {
        if (!ok) {
            char *m = g_strdup_printf("%s %s (%d): %s", e->what, e->t.name, e->t.pid,
                                      ge ? ge->message : err && *g_strstrip(err) ? err : "not done (password not given?)");
            bar_show(u, GTK_MESSAGE_ERROR, m, NULL, -1, 0, 0);
            g_free(m);
        } else if (e->sig == SIGTERM || e->sig == SIGKILL) {
            g_array_append_val(u->watch, e->t);
            watch_start(u, e->sig, e->sig == SIGKILL ? 1000 : 3000);
        } else {
            char *m = g_strdup_printf("%s %s (%d): done.", e->what, e->t.name, e->t.pid);
            bar_show(u, GTK_MESSAGE_INFO, m, NULL, -1, 0, 0);
            g_free(m);
        }
    }
    if (ge) g_error_free(ge);
    g_free(err);
    g_object_unref(src);
    g_object_unref(u->tb);
    g_free(e->what);
    g_free(e);
}

/* Note: the start-time check (same()) ran before this; the password prompt can take a while and the elevated
   command has no such guard, so in theory a PID reused during the prompt would be hit. With the default pid_max
   (4 million) a PID is not reused within minutes on a normal desktop. */
static void elevate(pp_ui *u, const target *t, const char *tool, const char *arg, int sig, const char *what)
{
    char **argv = pp_elevated_argv(tool, arg, t->pid);
    GError *ge = NULL;
    GSubprocess *p = g_subprocess_newv((const char *const *)argv, G_SUBPROCESS_FLAGS_STDERR_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE, &ge);
    g_strfreev(argv);
    if (!p) {
        char *m = g_strdup_printf("Cannot run pkexec: %s", ge ? ge->message : "?");
        bar_show(u, GTK_MESSAGE_ERROR, m, NULL, -1, 0, 0);
        g_free(m);
        if (ge) g_error_free(ge);
        return;
    }
    {
        elev *e = g_new0(elev, 1);
        e->u = u;
        e->t = *t;
        e->sig = sig;
        e->what = g_strdup(what);
        g_object_ref(u->tb);
        g_subprocess_communicate_utf8_async(p, NULL, NULL, elev_done, e);
    }
}

/* ---- the actions ---- */

static void send_all(pp_ui *u, target *t, int n, int sig, const char *what)
{
    GString *errs = g_string_new(NULL);
    int watched = 0;
    g_array_set_size(u->watch, 0);
    for (int i = 0; i < n; i++) {
        char err[200];
        int r = pp_signal(t[i].pid, t[i].start, sig, err, sizeof err);
        if (r == 0) { g_array_append_val(u->watch, t[i]); watched++; }
        else if (r == 1) elevate(u, &t[i], "kill", pp_signame(sig), sig, what);
        else if (r == 2) { g_array_append_val(u->watch, t[i]); watched++; }   /* already gone: reported as ended */
        else g_string_append_printf(errs, "%s (%d): %s\n", t[i].name, t[i].pid, err);
    }
    if (errs->len) bar_show(u, GTK_MESSAGE_ERROR, g_strstrip(errs->str), NULL, -1, 0, 0);
    else if (sig == SIGSTOP || sig == SIGCONT) {
        char *m = g_strdup_printf("%s: %s%s.", what, n == 1 ? t[0].name : "the selected processes",
                                  sig == SIGSTOP ? " is suspended (Resume continues it)" : " runs again");
        if (watched) bar_show(u, GTK_MESSAGE_INFO, m, NULL, -1, 0, 0);
        g_free(m);
    }
    if (watched && (sig == SIGTERM || sig == SIGKILL)) watch_start(u, sig, sig == SIGKILL ? 1000 : 3000);
    g_string_free(errs, TRUE);
}

static char *describe(pp_ui *u, target *t, int n, int *other_user, const char **crit)
{
    GString *g = g_string_new(NULL);
    *other_user = 0;
    *crit = NULL;
    for (int i = 0; i < n; i++) {
        const pd_proc *p = find(u, t[i].pid);
        const char *c = pp_critical(t[i].pid, t[i].name);
        if (c && !*crit) *crit = c;
        if (p && p->uid != (int)getuid()) *other_user = 1;
        if (i < 8) g_string_append_printf(g, "%s%s (PID %d, user %s)", i ? "\n" : "", t[i].name, t[i].pid, p ? p->user : "?");
    }
    if (n > 8) g_string_append_printf(g, "\n... and %d more", n - 8);
    return g_string_free(g, FALSE);
}

static void act(pp_ui *u, int action, target *t, int n)
{
    char *list, *body, *title;
    const char *crit, *ok;
    int other, sig;
    if (n <= 0) return;
    for (int i = 0; i < n; i++) {
        const pd_proc *p = find(u, t[i].pid);
        const char *why = p ? pp_untouchable(p->pid, p->ppid, p->kthread) : NULL;
        if (why) { bar_show(u, GTK_MESSAGE_ERROR, why, NULL, -1, 0, 0); return; }
        if ((action == A_SUSPEND) && (t[i].pid == getpid())) {
            bar_show(u, GTK_MESSAGE_ERROR, "SRS cannot suspend itself (nothing could resume it).", NULL, -1, 0, 0);
            return;
        }
    }
    if (action == A_RESUME) { send_all(u, t, n, SIGCONT, "Resume"); return; }
    list = describe(u, t, n, &other, &crit);
    switch (action) {
    case A_KILL: title = g_strdup_printf("Force Kill %s?", n == 1 ? t[0].name : "these processes"); ok = "Force _Kill"; sig = SIGKILL;
        body = g_strdup_printf("%s\n\nIt is stopped at once, without a chance to save: unsaved work in it is lost.%s", list,
                               other ? (getuid() ? "\nIt belongs to another user: your password will be asked for (polkit)." : "") : "");
        break;
    case A_SUSPEND: title = g_strdup_printf("Suspend %s?", n == 1 ? t[0].name : "these processes"); ok = "S_uspend"; sig = SIGSTOP;
        body = g_strdup_printf("%s\n\nIt stops running until you choose Resume.%s", list,
                               other ? (getuid() ? "\nIt belongs to another user: your password will be asked for (polkit)." : "") : "");
        break;
    default: title = g_strdup_printf("End %s?", n == 1 ? t[0].name : "these processes"); ok = "_End Task"; sig = SIGTERM;
        body = g_strdup_printf("%s\n\nIt is asked to close; unsaved work in it is lost.%s", list,
                               other ? (getuid() ? "\nIt belongs to another user: your password will be asked for (polkit)." : "") : "");
        break;
    }
    /* suspending something harmless needs no question; the session parts and SRS do */
    if (action != A_SUSPEND || crit) {
        if (!confirm(u, title, body, crit, ok)) { g_free(title); g_free(body); g_free(list); return; }
    }
    send_all(u, t, n, sig, action == A_SUSPEND ? "Suspend" : action == A_KILL ? "Force Kill" : "End Task");
    g_free(title);
    g_free(body);
    g_free(list);
}

static void act_tree(pp_ui *u, target *root)
{
    const pd_snap *s = u->snap(u->user);
    int kids[1024], nk = s ? pp_tree(s, root->pid, kids, 1024) : 0, other;
    target *all = g_new0(target, (size_t)nk + 1);
    int n = 0;
    const char *crit;
    char *list, *body, *title;
    for (int i = 0; i < nk; i++) {
        int k = pd_find(s, kids[i]);
        if (k < 0 || s->proc[k].kthread) continue;
        all[n].pid = s->proc[k].pid;
        all[n].start = s->proc[k].start;
        snprintf(all[n].name, sizeof all[n].name, "%s", s->proc[k].name);
        n++;
    }
    all[n++] = *root;   /* the children first, the process itself last */
    list = describe(u, root, 1, &other, &crit);
    for (int i = 0; i < n && !crit; i++) crit = pp_critical(all[i].pid, all[i].name);
    title = g_strdup_printf("End %s and everything it started?", root->name);
    body = g_strdup_printf("%s\n\n%d process%s in this tree end (the children first); unsaved work in them is lost.%s", list, n,
                           n == 1 ? "" : "es", other && getuid() ? "\nOther users' processes ask for your password (polkit)." : "");
    if (confirm(u, title, body, crit, "End Process _Tree")) send_all(u, all, n, SIGTERM, "End Process Tree");
    g_free(title); g_free(body); g_free(list); g_free(all);
}

static void act_parent(pp_ui *u, int zombie_pid)
{
    const pd_proc *z = find(u, zombie_pid), *p;
    target t;
    if (!z || !(p = find(u, z->ppid))) { bar_show(u, GTK_MESSAGE_ERROR, "The parent process is not there any more.", NULL, -1, 0, 0); return; }
    if (pp_untouchable(p->pid, p->ppid, p->kthread)) {
        bar_show(u, GTK_MESSAGE_ERROR, "Its parent is PID 1 or a kernel thread: it will clean the zombie up itself.", NULL, -1, 0, 0);
        return;
    }
    t.pid = p->pid;
    t.start = p->start;
    snprintf(t.name, sizeof t.name, "%s", p->name);
    act(u, A_END, &t, 1);
}

static void set_nice(pp_ui *u, int nice)
{
    target t[256];
    int n = targets(u, t, 256), done = 0;
    GString *errs = g_string_new(NULL);
    char a[16];
    snprintf(a, sizeof a, "%d", nice);
    for (int i = 0; i < n; i++) {
        const pd_proc *p = find(u, t[i].pid);
        char err[200];
        int r;
        if (p && pp_untouchable(p->pid, p->ppid, p->kthread)) continue;
        r = pp_renice(t[i].pid, t[i].start, nice, err, sizeof err);
        if (r == 0) done++;
        else if (r == 1) elevate(u, &t[i], "renice", a, 0, "Set Priority");
        else g_string_append_printf(errs, "%s (%d): %s\n", t[i].name, t[i].pid, err);
    }
    if (errs->len) bar_show(u, GTK_MESSAGE_ERROR, g_strstrip(errs->str), NULL, -1, 0, 0);
    else if (done) {
        char *m = g_strdup_printf("Priority set to nice %d for %s.", nice, n == 1 ? t[0].name : "the selected processes");
        bar_show(u, GTK_MESSAGE_INFO, m, NULL, -1, 0, 0);
        g_free(m);
    }
    g_string_free(errs, TRUE);
}

static const struct { const char *label; int nice; } PRIO[] = {
    { "Very High (nice -15)", -15 }, { "High (nice -5)", -5 }, { "Normal (nice 0)", 0 }, { "Low (nice 10)", 10 }, { "Very Low (nice 19)", 19 } };

static void on_prio(GtkMenuItem *i, gpointer d)
{
    pp_ui *u = d;
    set_nice(u, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(i), "nice")));
}

static GtkWidget *prio_menu(pp_ui *u)
{
    GtkWidget *m = gtk_menu_new();
    for (int i = 0; i < 5; i++) {
        GtkWidget *it = gtk_menu_item_new_with_label(PRIO[i].label);
        g_object_set_data(G_OBJECT(it), "nice", GINT_TO_POINTER(PRIO[i].nice));
        if (PRIO[i].nice < 0) gtk_widget_set_tooltip_text(it, "Above normal needs your password (polkit) unless your limits allow it");
        g_signal_connect(it, "activate", G_CALLBACK(on_prio), u);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
    }
    gtk_widget_show_all(m);
    return m;
}

static void affinity_dialog(pp_ui *u)
{
    target t[256];
    int n = targets(u, t, 256);
    const pd_snap *s = u->snap(u->user);
    GtkWidget *d, *box, *g, **cb;
    char cur[512] = "";
    cpu_set_t cs;
    int ncpu = s ? s->ncpu : 0, cols;
    if (!n || !ncpu) return;
    pp_get_affinity(t[0].pid, cur, sizeof cur);
    list_to_set(cur, &cs);
    d = gtk_dialog_new_with_buttons("Set CPU Affinity", top_of(u->bar), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, NULL);
    box = gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_set_spacing(GTK_BOX(box), 8);
    {
        char *h = g_strdup_printf("Which logical processors may %s use? (all of its threads)", n == 1 ? t[0].name : "these processes");
        GtkWidget *l = gtk_label_new(h);
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        gtk_box_pack_start(GTK_BOX(box), l, FALSE, FALSE, 0);
        g_free(h);
    }
    g = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g), 12);
    cb = g_new0(GtkWidget *, (size_t)ncpu);
    cols = ncpu > 16 ? 8 : 4;
    for (int i = 0; i < ncpu; i++) {
        char lb[24];
        snprintf(lb, sizeof lb, "CPU %d", s->cpu_id[i]);
        cb[i] = gtk_check_button_new_with_label(lb);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb[i]), CPU_ISSET(s->cpu_id[i], &cs));
        gtk_grid_attach(GTK_GRID(g), cb[i], i % cols, i / cols, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(box), g, FALSE, FALSE, 0);
    gtk_widget_show_all(d);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        GString *l = g_string_new(NULL), *errs = g_string_new(NULL);
        int done = 0;
        for (int i = 0; i < ncpu; i++)
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb[i]))) g_string_append_printf(l, "%s%d", l->len ? "," : "", s->cpu_id[i]);
        for (int k = 0; k < n; k++) {
            char err[200];
            int r = pp_set_affinity(t[k].pid, t[k].start, l->str, err, sizeof err);
            if (r == 0) done++;
            else if (r == 1) elevate(u, &t[k], "taskset", l->str, 0, "Set CPU Affinity");
            else g_string_append_printf(errs, "%s (%d): %s\n", t[k].name, t[k].pid, err);
        }
        if (errs->len) bar_show(u, GTK_MESSAGE_ERROR, g_strstrip(errs->str), NULL, -1, 0, 0);
        else if (done) {
            char *m = g_strdup_printf("CPU affinity set to %s.", l->str);
            bar_show(u, GTK_MESSAGE_INFO, m, NULL, -1, 0, 0);
            g_free(m);
        }
        g_string_free(l, TRUE);
        g_string_free(errs, TRUE);
    }
    g_free(cb);
    gtk_widget_destroy(d);
}

static int count_dir(const char *p)
{
    DIR *d = opendir(p);
    struct dirent *e;
    int n = 0;
    if (!d) return -1;
    while ((e = readdir(d))) if (e->d_name[0] != '.') n++;
    closedir(d);
    return n;
}

static void properties(pp_ui *u, int pid)
{
    const pd_proc *p = find(u, pid);
    const pd_snap *s = u->snap(u->user);
    GtkWidget *d, *box, *g;
    char path[64], v[20][512];
    static const char *const k[] = { "Name:", "PID:", "Parent:", "User:", "Status:", "Started:", "CPU Time:", "Priority / Nice:",
                                     "Threads:", "Memory (Resident):", "CPU Affinity:", "Executable:", "Working Folder:", "Unit / Cgroup:",
                                     "Session:", "Environment:", "Open Files:", "Command Line:", NULL };
    int row = 0, nk = 18;
    ssize_t l;
    if (!p) return;
    snprintf(v[0], sizeof v[0], "%s", p->name);
    snprintf(v[1], sizeof v[1], "%d", p->pid);
    {
        int pi = pd_find(s, p->ppid);
        snprintf(v[2], sizeof v[2], "%s (%d)", pi >= 0 ? s->proc[pi].name : "?", p->ppid);
    }
    snprintf(v[3], sizeof v[3], "%s (%d)", p->user, p->uid);
    snprintf(v[4], sizeof v[4], "%s", p->state == 'T' ? "Suspended" : p->state == 'Z' ? "Zombie" : p->state == 'R' ? "Running" :
                                       p->state == 'D' ? "Disk Sleep" : "Sleeping");
    {
        double ago = s->uptime - (double)p->start / (double)sysconf(_SC_CLK_TCK);
        time_t t = time(NULL) - (time_t)ago;
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(v[5], sizeof v[5], "%Y-%m-%d %H:%M:%S", &tm);
    }
    pd_fmt_uptime(p->cpu_s, v[6], sizeof v[6]);
    snprintf(v[7], sizeof v[7], "%d / %d", p->prio, p->nice);
    snprintf(v[8], sizeof v[8], "%d", p->threads);
    pd_fmt_bytes((double)p->rss, v[9], sizeof v[9]);
    pp_get_affinity(pid, v[10], sizeof v[10]);
    snprintf(path, sizeof path, "/proc/%d/exe", pid);
    if ((l = readlink(path, v[11], sizeof v[11] - 1)) > 0) v[11][l] = 0; else snprintf(v[11], sizeof v[11], EM " (not readable: another user's process)");
    snprintf(path, sizeof path, "/proc/%d/cwd", pid);
    if ((l = readlink(path, v[12], sizeof v[12] - 1)) > 0) v[12][l] = 0; else snprintf(v[12], sizeof v[12], EM);
    snprintf(v[13], sizeof v[13], "%s", p->unit[0] ? p->unit : EM);
    if (p->session >= 0) snprintf(v[14], sizeof v[14], "%d", p->session); else snprintf(v[14], sizeof v[14], EM);
    {
        char *data = NULL;
        gsize len = 0;
        snprintf(path, sizeof path, "/proc/%d/environ", pid);
        if (g_file_get_contents(path, &data, &len, NULL)) {
            int n = 0;
            for (gsize i = 0; i < len; i++) if (!data[i]) n++;
            snprintf(v[15], sizeof v[15], "%d variables", n);
        } else snprintf(v[15], sizeof v[15], EM);
        g_free(data);
    }
    snprintf(path, sizeof path, "/proc/%d/fd", pid);
    {
        int n = count_dir(path);
        if (n >= 0) snprintf(v[16], sizeof v[16], "%d", n); else snprintf(v[16], sizeof v[16], EM);
    }
    snprintf(v[17], sizeof v[17], "%s", p->cmd);
    d = gtk_dialog_new_with_buttons("Process Properties", top_of(u->bar), GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE, NULL);
    box = gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    g = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g), 3);
    gtk_grid_set_column_spacing(GTK_GRID(g), 12);
    for (int i = 0; i < nk; i++) {
        GtkWidget *kl = gtk_label_new(k[i]), *vl = gtk_label_new(v[i]);
        gtk_label_set_xalign(GTK_LABEL(kl), 1);
        gtk_label_set_xalign(GTK_LABEL(vl), 0);
        gtk_label_set_selectable(GTK_LABEL(vl), TRUE);
        gtk_label_set_line_wrap(GTK_LABEL(vl), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(vl), PANGO_WRAP_CHAR);
        gtk_label_set_max_width_chars(GTK_LABEL(vl), 70);
        gtk_style_context_add_class(gtk_widget_get_style_context(kl), "dim-label");
        gtk_widget_set_valign(kl, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(g), kl, 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(g), vl, 1, row++, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(box), g, TRUE, TRUE, 0);
    g_signal_connect(d, "response", G_CALLBACK(gtk_widget_destroy), NULL);
    gtk_widget_show_all(d);
}

static void open_location(pp_ui *u, int pid)
{
    char path[64], exe[512], *dir, *uri;
    ssize_t l;
    GError *ge = NULL;
    snprintf(path, sizeof path, "/proc/%d/exe", pid);
    if ((l = readlink(path, exe, sizeof exe - 1)) <= 0) {
        bar_show(u, GTK_MESSAGE_ERROR, "Its program file cannot be read (another user's process).", NULL, -1, 0, 0);
        return;
    }
    exe[l] = 0;
    if (g_str_has_suffix(exe, " (deleted)")) exe[strlen(exe) - 10] = 0;
    dir = g_path_get_dirname(exe);
    uri = g_filename_to_uri(dir, NULL, NULL);
    if (!uri || !g_app_info_launch_default_for_uri(uri, NULL, &ge))
        bar_show(u, GTK_MESSAGE_ERROR, ge ? ge->message : "Cannot open the folder.", NULL, -1, 0, 0);
    if (ge) g_error_free(ge);
    g_free(uri);
    g_free(dir);
}

static void copy_cmd(pp_ui *u, int pid)
{
    const pd_proc *p = find(u, pid);
    if (!p) return;
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), p->cmd, -1);
    bar_show(u, GTK_MESSAGE_INFO, "Command line copied.", NULL, -1, 0, 0);
}

/* ---- buttons, menu, keys ---- */

static int first_pid(pp_ui *u) { int p[1]; return u->sel(u->user, p, 1) > 0 ? p[0] : 0; }

static void do_action(pp_ui *u, int a)
{
    target t[256];
    int n = targets(u, t, 256);
    if (!n) return;
    if (a == A_TREE) { act_tree(u, &t[0]); return; }
    if (a == A_END_PARENT) { act_parent(u, t[0].pid); return; }
    act(u, a, t, n);
}

static void b_end(GtkButton *b, gpointer d) { (void)b; do_action(d, A_END); }
static void b_kill(GtkButton *b, gpointer d) { (void)b; do_action(d, A_KILL); }
static void b_tree(GtkButton *b, gpointer d) { (void)b; do_action(d, A_TREE); }
static void b_susp(GtkButton *b, gpointer d) { (void)b; do_action(d, A_SUSPEND); }
static void b_res(GtkButton *b, gpointer d) { (void)b; do_action(d, A_RESUME); }
static void b_aff(GtkButton *b, gpointer d) { (void)b; affinity_dialog(d); }
static void b_props(GtkButton *b, gpointer d) { pp_ui *u = d; (void)b; if (first_pid(u)) properties(u, first_pid(u)); }

static void bar_resp(GtkInfoBar *b, gint r, gpointer d)
{
    pp_ui *u = d;
    (void)b;
    if (r == GTK_RESPONSE_ACCEPT && u->bar_action == A_KILL) {
        /* Force Kill offered after End Task: the user already confirmed ending it */
        target t;
        const pd_proc *p = find(u, u->bar_pid);
        t.pid = u->bar_pid;
        t.start = u->bar_start;
        snprintf(t.name, sizeof t.name, "%s", p ? p->name : "process");
        send_all(u, &t, 1, SIGKILL, "Force Kill");
        return;
    }
    if (r == GTK_RESPONSE_ACCEPT && u->bar_action == A_END_PARENT) { act_parent(u, u->bar_pid); return; }
    bar_hide(u);
}

static GtkWidget *btn(const char *mn, const char *tip, GCallback cb, pp_ui *u)
{
    GtkWidget *b = gtk_button_new_with_mnemonic(mn);
    gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect(b, "clicked", cb, u);
    return b;
}

static void tb_destroy(GtkWidget *w, gpointer d) { pp_ui *u = d; (void)w; u->dead = 1; if (u->watch_timer) { g_source_remove(u->watch_timer); u->watch_timer = 0; } }
static void tb_free(gpointer d) { pp_ui *u = d; g_array_free(u->watch, TRUE); g_free(u); }

pp_ui *pp_ui_new(pp_sel_fn sel, pp_snap_fn snap, void *user)
{
    pp_ui *u = g_new0(pp_ui, 1);
    GtkWidget *mb;
    u->sel = sel;
    u->snap = snap;
    u->user = user;
    u->bar_action = -1;
    u->watch = g_array_new(FALSE, TRUE, sizeof(target));
    /* mnemonics: the window always shows D I A C U E (top row) and P G S R W N (tabs); here T K O M Y F */
    u->tb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    u->b_end = btn("End _Task", "Ask the selected process(es) to close (SIGTERM); Delete key", G_CALLBACK(b_end), u);
    u->b_kill = btn("Force _Kill", "Stop at once (SIGKILL): no chance to save; Shift+Delete", G_CALLBACK(b_kill), u);
    u->b_tree = btn("End Pr_ocess Tree", "End it and every process it started (the children first)", G_CALLBACK(b_tree), u);
    u->b_susp = btn("Suspend", "Pause it (SIGSTOP) until Resume", G_CALLBACK(b_susp), u);
    u->b_res = btn("Resu_me", "Continue a suspended process (SIGCONT)", G_CALLBACK(b_res), u);
    mb = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(mb), "Priorit_y");
    gtk_button_set_use_underline(GTK_BUTTON(mb), TRUE);
    gtk_widget_set_tooltip_text(mb, "Set the priority (nice) of the selected process(es) and all their threads");
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(mb), prio_menu(u));
    u->b_prio = mb;
    u->b_aff = btn("A_ffinity…", "Choose the logical processors it may run on", G_CALLBACK(b_aff), u);
    u->b_props = btn("Properties…", "Program, folder, start time, unit, environment and open files", G_CALLBACK(b_props), u);
    gtk_box_pack_start(GTK_BOX(u->tb), u->b_end, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(u->tb), u->b_kill, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(u->tb), u->b_tree, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(u->tb), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(u->tb), u->b_susp, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(u->tb), u->b_res, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(u->tb), u->b_prio, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(u->tb), u->b_aff, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(u->tb), u->b_props, FALSE, FALSE, 0);
    u->bar = gtk_info_bar_new();
    u->bar_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(u->bar_label), 0);
    gtk_label_set_line_wrap(GTK_LABEL(u->bar_label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(u->bar_label), TRUE);
    gtk_container_add(GTK_CONTAINER(gtk_info_bar_get_content_area(GTK_INFO_BAR(u->bar))), u->bar_label);
    u->bar_btn = gtk_info_bar_add_button(GTK_INFO_BAR(u->bar), "Force _Kill", GTK_RESPONSE_ACCEPT);
    gtk_info_bar_set_show_close_button(GTK_INFO_BAR(u->bar), TRUE);
    g_signal_connect(u->bar, "response", G_CALLBACK(bar_resp), u);
    gtk_widget_set_no_show_all(u->bar, TRUE);
    gtk_widget_show(u->bar_label);
    g_object_set_data_full(G_OBJECT(u->tb), "pp-ui", u, tb_free);
    g_signal_connect(u->tb, "destroy", G_CALLBACK(tb_destroy), u);
    pp_update(u);
    return u;
}

void pp_ui_free(pp_ui *u) { (void)u; /* the toolbar owns it (freed with the widget) */ }
GtkWidget *pp_toolbar(pp_ui *u) { return u->tb; }
GtkWidget *pp_infobar(pp_ui *u) { return u->bar; }

/* what may be done to the selection: sets *why for the tooltips; zombie = the single selected row is a zombie */
static void sel_state(pp_ui *u, int *n, int *untouch, int *zombie, int *stopped, int *self, const char **why)
{
    int pids[256];
    *n = u->sel(u->user, pids, 256);
    *untouch = *zombie = *stopped = *self = 0;
    *why = NULL;
    for (int i = 0; i < *n; i++) {
        const pd_proc *p = find(u, pids[i]);
        const char *w;
        if (!p) continue;
        if ((w = pp_untouchable(p->pid, p->ppid, p->kthread))) { (*untouch)++; *why = w; }
        if (p->state == 'Z') (*zombie)++;
        if (p->state == 'T' || p->state == 't') (*stopped)++;
        if (p->pid == getpid()) *self = 1;
    }
}

void pp_update(pp_ui *u)
{
    int n, un, zo, st, self;
    const char *why;
    int ok;
    sel_state(u, &n, &un, &zo, &st, &self, &why);
    ok = n > 0 && !un && zo < n;
    gtk_widget_set_sensitive(u->b_end, ok);
    gtk_widget_set_sensitive(u->b_kill, ok);
    gtk_widget_set_sensitive(u->b_tree, ok && n == 1);
    gtk_widget_set_sensitive(u->b_susp, ok && !self && st < n);
    gtk_widget_set_sensitive(u->b_res, n > 0 && !un && st > 0);
    gtk_widget_set_sensitive(u->b_prio, ok);
    gtk_widget_set_sensitive(u->b_aff, ok);
    gtk_widget_set_sensitive(u->b_props, n > 0);
    gtk_widget_set_tooltip_text(u->b_end, why ? why : zo && zo == n ? "A zombie has already ended: only its parent can clean it up"
                                                                       : "Ask the selected process(es) to close (SIGTERM); Delete key");
    /* one zombie selected: say what it is and offer its parent */
    if (n == 1 && zo == 1) {
        int p[1];
        const pd_proc *z, *par;
        u->sel(u->user, p, 1);
        z = find(u, p[0]);
        par = z ? find(u, z->ppid) : NULL;
        if (z && (u->bar_action != A_END_PARENT || u->bar_pid != z->pid)) {
            char *m = g_strdup_printf("Zombie: %s (%d) has already ended and is waiting for its parent %s (%d) to clean it up. "
                                      "It uses no CPU or memory; ending the parent removes it.", z->name, z->pid,
                                      par ? par->name : "?", z->ppid);
            bar_show(u, GTK_MESSAGE_INFO, m, par && !pp_untouchable(par->pid, par->ppid, par->kthread) ? "End Parent Process…" : NULL,
                     A_END_PARENT, z->pid, z->start);
            g_free(m);
        }
    } else if (u->bar_action == A_END_PARENT) bar_hide(u);
}

static void m_end(GtkMenuItem *i, gpointer d) { (void)i; do_action(d, A_END); }
static void m_kill(GtkMenuItem *i, gpointer d) { (void)i; do_action(d, A_KILL); }
static void m_tree(GtkMenuItem *i, gpointer d) { (void)i; do_action(d, A_TREE); }
static void m_susp(GtkMenuItem *i, gpointer d) { (void)i; do_action(d, A_SUSPEND); }
static void m_res(GtkMenuItem *i, gpointer d) { (void)i; do_action(d, A_RESUME); }
static void m_parent(GtkMenuItem *i, gpointer d) { (void)i; do_action(d, A_END_PARENT); }
static void m_aff(GtkMenuItem *i, gpointer d) { (void)i; affinity_dialog(d); }
static void m_props(GtkMenuItem *i, gpointer d) { pp_ui *u = d; (void)i; if (first_pid(u)) properties(u, first_pid(u)); }
static void m_loc(GtkMenuItem *i, gpointer d) { pp_ui *u = d; (void)i; if (first_pid(u)) open_location(u, first_pid(u)); }
static void m_copy(GtkMenuItem *i, gpointer d) { pp_ui *u = d; (void)i; if (first_pid(u)) copy_cmd(u, first_pid(u)); }

void pp_popup(pp_ui *u, GtkWidget *anchor, GdkEvent *ev)
{
    int n, un, zo, st, self, ok;
    const char *why;
    GtkWidget *m = gtk_menu_new(), *pi;
    sel_state(u, &n, &un, &zo, &st, &self, &why);
    if (!n) { gtk_widget_destroy(m); return; }
    ok = !un && zo < n;
#define MI(lbl, cb, sens) do { GtkWidget *it_ = gtk_menu_item_new_with_mnemonic(lbl); gtk_widget_set_sensitive(it_, sens); \
        g_signal_connect(it_, "activate", G_CALLBACK(cb), u); gtk_menu_shell_append(GTK_MENU_SHELL(m), it_); } while (0)
    if (zo == 1 && n == 1) MI("End _Parent Process…", m_parent, TRUE);
    MI("_End Task", m_end, ok);
    MI("Force _Kill", m_kill, ok);
    MI("End Process _Tree", m_tree, ok && n == 1);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    MI("S_uspend", m_susp, ok && !self && st < n);
    MI("Resu_me", m_res, !un && st > 0);
    pi = gtk_menu_item_new_with_mnemonic("Priorit_y");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(pi), prio_menu(u));
    gtk_widget_set_sensitive(pi, ok);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), pi);
    MI("Set CPU A_ffinity…", m_aff, ok);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    MI("Open File _Location", m_loc, n >= 1);
    MI("Copy Comma_nd Line", m_copy, n >= 1);
    MI("Pr_operties…", m_props, n >= 1);
#undef MI
    gtk_widget_show_all(m);
    if (ev) gtk_menu_popup_at_pointer(GTK_MENU(m), ev);
    else gtk_menu_popup_at_widget(GTK_MENU(m), anchor, GDK_GRAVITY_CENTER, GDK_GRAVITY_NORTH_WEST, NULL);
}

gboolean pp_key(pp_ui *u, GdkEventKey *ev)
{
    if (ev->keyval == GDK_KEY_Delete || ev->keyval == GDK_KEY_KP_Delete) {
        do_action(u, (ev->state & GDK_SHIFT_MASK) ? A_KILL : A_END);
        return TRUE;
    }
    return FALSE;
}
