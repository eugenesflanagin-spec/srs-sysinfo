/* perf_autostart.c - what starts at login, and safe per-user changes to it (see perf_autostart.h).
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "perf_autostart.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DE "Desktop Entry"
#define KF_FLAGS (G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS)
#define MAX_UNDO 50

/* ------------------------------------------------------------------ small helpers */

static void set_err(char **err, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
static void set_err(char **err, const char *fmt, ...)
{
    va_list ap;
    if (!err) return;
    g_free(*err);
    va_start(ap, fmt);
    *err = g_strdup_vprintf(fmt, ap);
    va_end(ap);
}

static char *user_dir(void) { return g_build_filename(g_get_user_config_dir(), "autostart", NULL); }
static char *trash_dir(void) { return g_build_filename(g_get_user_config_dir(), "autostart", ".srs-trash", NULL); }
static char *env_dir(void) { return g_build_filename(g_get_user_config_dir(), "plasma-workspace", "env", NULL); }
static char *scripts_dir(void) { return g_build_filename(g_get_user_config_dir(), "autostart-scripts", NULL); }
static char *state_file(const char *name) { return g_build_filename(g_get_user_state_dir(), "srs", name, NULL); }

static int exists(const char *p) { return p && access(p, F_OK) == 0; }

/* the system autostart folders, most important first: $XDG_CONFIG_DIRS (default /etc/xdg), then /etc/xdg */
static GPtrArray *system_dirs(void)
{
    GPtrArray *a = g_ptr_array_new_with_free_func(g_free);
    const gchar *const *d = g_get_system_config_dirs();
    int have_etc = 0;
    for (int i = 0; d && d[i]; i++) {
        if (!d[i][0]) continue;
        if (!strcmp(d[i], "/etc/xdg") || !strcmp(d[i], "/etc/xdg/")) have_etc = 1;
        g_ptr_array_add(a, g_build_filename(d[i], "autostart", NULL));
    }
    if (!have_etc && !getenv("SRS_TEST_AUTOSTART_NO_ETC")) g_ptr_array_add(a, g_strdup("/etc/xdg/autostart"));
    return a;
}

/* write data to path atomically: <path>.tmp in the same folder, fsync, rename, fsync the folder */
static gboolean write_atomic(const char *path, const char *data, gsize len, char **err)
{
    char *dir = g_path_get_dirname(path), *tmp = g_strdup_printf("%s.tmp", path);
    int fd, ok = 0;
    if (g_mkdir_with_parents(dir, 0755) != 0) { set_err(err, "Cannot create %s: %s", dir, g_strerror(errno)); goto out; }
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) { set_err(err, "Cannot write %s: %s", tmp, g_strerror(errno)); goto out; }
    {
        gsize off = 0;
        while (off < len) {
            ssize_t w = write(fd, data + off, len - off);
            if (w < 0) { if (errno == EINTR) continue; break; }
            off += (gsize)w;
        }
        if (off != len || fsync(fd) != 0) {
            set_err(err, "Cannot write %s: %s", tmp, g_strerror(errno));
            close(fd);
            unlink(tmp);
            goto out;
        }
    }
    close(fd);
    if (rename(tmp, path) != 0) { set_err(err, "Cannot replace %s: %s", path, g_strerror(errno)); unlink(tmp); goto out; }
    fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) { fsync(fd); close(fd); }
    ok = 1;
out:
    g_free(dir);
    g_free(tmp);
    return ok;
}

static gboolean save_kf(GKeyFile *kf, const char *path, char **err)
{
    gsize len;
    char *d = g_key_file_to_data(kf, &len, NULL);
    gboolean ok = write_atomic(path, d, len, err);
    g_free(d);
    return ok;
}

static GKeyFile *load_kf(const char *path)
{
    GKeyFile *kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, path, KF_FLAGS, NULL)) { g_key_file_free(kf); return NULL; }
    return kf;
}

static gboolean kbool(GKeyFile *kf, const char *k, gboolean def)
{
    GError *e = NULL;
    gboolean v = g_key_file_get_boolean(kf, DE, k, &e);
    if (e) { g_error_free(e); return def; }
    return v;
}

static char *kstr(GKeyFile *kf, const char *k)
{
    return g_key_file_get_string(kf, DE, k, NULL);
}

/* ------------------------------------------------------------------ the undo log */

typedef struct { char *path; int existed; char *data; gsize len; } snap;
static struct { int on; char *what; GPtrArray *files; char **cmd; } CH;

static void snap_free(gpointer p) { snap *s = p; g_free(s->path); g_free(s->data); g_free(s); }

static void ch_begin(const char *what)
{
    CH.on = 1;
    g_free(CH.what);
    CH.what = g_strdup(what);
    if (CH.files) g_ptr_array_unref(CH.files);
    CH.files = g_ptr_array_new_with_free_func(snap_free);
    g_strfreev(CH.cmd);
    CH.cmd = NULL;
}

/* remember path as it is now (before this change touches it) */
static void ch_file(const char *path)
{
    snap *s;
    if (!CH.on) return;
    for (guint i = 0; i < CH.files->len; i++) if (!strcmp(((snap *)CH.files->pdata[i])->path, path)) return;
    s = g_new0(snap, 1);
    s->path = g_strdup(path);
    s->existed = g_file_get_contents(path, &s->data, &s->len, NULL);
    g_ptr_array_add(CH.files, s);
}

static void ch_cmd(const char *const *argv) { if (CH.on) { g_strfreev(CH.cmd); CH.cmd = g_strdupv((char **)argv); } }

static void ch_abort(void) { CH.on = 0; }

static void ch_commit(void)
{
    char *lf = state_file("autostart-undo.log"), *grp, **groups;
    GKeyFile *kf = g_key_file_new();
    gsize n = 0;
    long top = 0;
    if (!CH.on) { g_free(lf); g_key_file_free(kf); return; }
    CH.on = 0;
    g_key_file_load_from_file(kf, lf, G_KEY_FILE_NONE, NULL);
    groups = g_key_file_get_groups(kf, &n);
    for (gsize i = 0; i < n; i++) if (!strncmp(groups[i], "change-", 7) && atol(groups[i] + 7) > top) top = atol(groups[i] + 7);
    /* keep the last MAX_UNDO changes */
    if (n >= MAX_UNDO) {
        long lo = top;
        for (gsize i = 0; i < n; i++) if (!strncmp(groups[i], "change-", 7) && atol(groups[i] + 7) < lo) lo = atol(groups[i] + 7);
        for (gsize i = 0; i < n; i++) if (atol(groups[i] + 7) == lo) g_key_file_remove_group(kf, groups[i], NULL);
    }
    g_strfreev(groups);
    grp = g_strdup_printf("change-%ld", top + 1);
    g_key_file_set_string(kf, grp, "what", CH.what ? CH.what : "Change");
    g_key_file_set_int64(kf, grp, "time", (gint64)time(NULL));
    g_key_file_set_integer(kf, grp, "files", (gint)CH.files->len);
    for (guint i = 0; i < CH.files->len; i++) {
        snap *s = CH.files->pdata[i];
        char k[32];
        snprintf(k, sizeof k, "file%u", i);
        g_key_file_set_string(kf, grp, k, s->path);
        snprintf(k, sizeof k, "existed%u", i);
        g_key_file_set_boolean(kf, grp, k, s->existed);
        if (s->existed) {
            char *b = g_base64_encode((const guchar *)s->data, s->len);
            snprintf(k, sizeof k, "data%u", i);
            g_key_file_set_string(kf, grp, k, b);
            g_free(b);
        }
    }
    if (CH.cmd) g_key_file_set_string_list(kf, grp, "undo_cmd", (const gchar *const *)CH.cmd, g_strv_length(CH.cmd));
    {
        char *e = NULL;
        save_kf(kf, lf, &e);   /* an undo log that cannot be written does not stop the change itself */
        g_free(e);
    }
    g_free(grp);
    g_free(lf);
    g_key_file_free(kf);
}

static char *last_group(GKeyFile *kf)
{
    gsize n = 0;
    char **g = g_key_file_get_groups(kf, &n), *best = NULL;
    long top = 0;
    for (gsize i = 0; i < n; i++)
        if (!strncmp(g[i], "change-", 7) && atol(g[i] + 7) > top) { top = atol(g[i] + 7); g_free(best); best = g_strdup(g[i]); }
    g_strfreev(g);
    return best;
}

gboolean as_can_undo(char **describe)
{
    char *lf = state_file("autostart-undo.log"), *g;
    GKeyFile *kf = g_key_file_new();
    gboolean ok = FALSE;
    if (g_key_file_load_from_file(kf, lf, G_KEY_FILE_NONE, NULL) && (g = last_group(kf))) {
        ok = TRUE;
        if (describe) *describe = g_key_file_get_string(kf, g, "what", NULL);
        g_free(g);
    }
    g_key_file_free(kf);
    g_free(lf);
    return ok;
}

static gboolean run_systemctl(const char *const *args, char **out, char **err);

gboolean as_undo(char **describe, char **err)
{
    char *lf = state_file("autostart-undo.log"), *g = NULL;
    GKeyFile *kf = g_key_file_new();
    gboolean ok = TRUE;
    int nf;
    if (!g_key_file_load_from_file(kf, lf, G_KEY_FILE_NONE, NULL) || !(g = last_group(kf))) {
        set_err(err, "Nothing to undo.");
        ok = FALSE;
        goto out;
    }
    if (describe) *describe = g_key_file_get_string(kf, g, "what", NULL);
    nf = g_key_file_get_integer(kf, g, "files", NULL);
    for (int i = 0; i < nf && ok; i++) {
        char k[32], *path, *b64;
        snprintf(k, sizeof k, "file%d", i);
        path = g_key_file_get_string(kf, g, k, NULL);
        snprintf(k, sizeof k, "existed%d", i);
        if (!path) continue;
        if (g_key_file_get_boolean(kf, g, k, NULL)) {
            gsize len = 0;
            guchar *d;
            snprintf(k, sizeof k, "data%d", i);
            b64 = g_key_file_get_string(kf, g, k, NULL);
            d = g_base64_decode(b64 ? b64 : "", &len);
            ok = write_atomic(path, (const char *)d, len, err);
            g_free(d);
            g_free(b64);
        } else if (exists(path) && unlink(path) != 0) {
            set_err(err, "Cannot remove %s: %s", path, g_strerror(errno));
            ok = FALSE;
        }
        g_free(path);
    }
    if (ok) {
        char **cmd = g_key_file_get_string_list(kf, g, "undo_cmd", NULL, NULL);
        if (cmd && cmd[0]) ok = run_systemctl((const char *const *)cmd, NULL, err);
        g_strfreev(cmd);
    }
    if (ok) {
        char *e = NULL;
        g_key_file_remove_group(kf, g, NULL);
        save_kf(kf, lf, &e);
        g_free(e);
    }
out:
    g_free(g);
    g_free(lf);
    g_key_file_free(kf);
    return ok;
}

/* first change to a file the user already had (not one SRS wrote): keep <file>.srs-bak once */
static void backup_once(const char *path)
{
    char *bak = g_strdup_printf("%s.srs-bak", path), *data = NULL;
    gsize len;
    GKeyFile *kf;
    if (!exists(path) || exists(bak)) { g_free(bak); return; }
    kf = load_kf(path);
    if (kf && (kbool(kf, "X-SRS-Added", FALSE) || kbool(kf, "X-SRS-Override", FALSE))) { g_key_file_free(kf); g_free(bak); return; }
    if (kf) g_key_file_free(kf);
    if (g_file_get_contents(path, &data, &len, NULL)) {
        char *e = NULL;
        write_atomic(bak, data, len, &e);
        g_free(e);
    }
    g_free(data);
    g_free(bak);
}

/* ------------------------------------------------------------------ systemctl */

static gboolean run_systemctl(const char *const *args, char **out, char **err)
{
    const char *bin = getenv("SRS_TEST_SYSTEMCTL");
    GPtrArray *av = g_ptr_array_new();
    char *o = NULL, *e = NULL;
    int st = 0;
    GError *ge = NULL;
    gboolean ok;
    g_ptr_array_add(av, (gpointer)(bin && *bin ? bin : "systemctl"));
    for (int i = 0; args[i]; i++) g_ptr_array_add(av, (gpointer)args[i]);
    g_ptr_array_add(av, NULL);
    ok = g_spawn_sync(NULL, (char **)av->pdata, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &o, &e, &st, &ge);
    if (!ok) { set_err(err, "Cannot run systemctl: %s", ge ? ge->message : "?"); if (ge) g_error_free(ge); }
    else if (!g_spawn_check_wait_status(st, NULL)) {
        g_strstrip(e);
        set_err(err, "systemctl %s failed: %s", args[1] ? args[1] : "", e && *e ? e : "error");
        ok = FALSE;
    }
    if (out) *out = o; else g_free(o);
    g_free(e);
    g_ptr_array_free(av, TRUE);
    return ok;
}

/* units SRS turned off (they leave `list-unit-files --state=enabled`, so we keep showing them) */
static GPtrArray *srs_units_read(void)
{
    char *f = state_file("autostart-units.list"), *d = NULL;
    GPtrArray *a = g_ptr_array_new_with_free_func(g_free);
    if (g_file_get_contents(f, &d, NULL, NULL)) {
        char **l = g_strsplit(d, "\n", -1);
        for (int i = 0; l[i]; i++) if (*g_strstrip(l[i])) g_ptr_array_add(a, g_strdup(l[i]));
        g_strfreev(l);
    }
    g_free(d);
    g_free(f);
    return a;
}

static void srs_units_set(const char *unit, int add)
{
    GPtrArray *a = srs_units_read();
    GString *s = g_string_new(NULL);
    char *f = state_file("autostart-units.list"), *e = NULL;
    int have = 0;
    for (guint i = 0; i < a->len; i++) {
        if (!strcmp(a->pdata[i], unit)) { have = 1; if (!add) continue; }
        g_string_append_printf(s, "%s\n", (char *)a->pdata[i]);
    }
    if (add && !have) g_string_append_printf(s, "%s\n", unit);
    write_atomic(f, s->str, s->len, &e);
    g_free(e);
    g_free(f);
    g_string_free(s, TRUE);
    g_ptr_array_unref(a);
}

/* ------------------------------------------------------------------ commands, delay wrapper */

static GRegex *delay_re(void)
{
    static GRegex *re;
    if (!re) re = g_regex_new("^\\s*(?:/usr/bin/|/bin/)?(?:ba|da)?sh\\s+-c\\s+([\"'])\\s*sleep\\s+(\\d+)\\s*(?:&&|;)\\s*(.*?)\\s*\\1\\s*$",
                              G_REGEX_DOTALL, 0, NULL);
    return re;
}

char *as_unwrap_delay(const char *exec, int *delay)
{
    GMatchInfo *mi = NULL;
    char *res = NULL;
    if (!exec) return NULL;
    if (g_regex_match(delay_re(), exec, 0, &mi)) {
        char *q = g_match_info_fetch(mi, 1), *n = g_match_info_fetch(mi, 2), *in = g_match_info_fetch(mi, 3);
        GString *s = g_string_new(NULL);
        for (const char *p = in; *p; p++) {
            if (q[0] == '"' && *p == '\\' && p[1] && strchr("\"`$\\", p[1])) p++;
            g_string_append_c(s, *p);
        }
        if (delay) *delay = atoi(n);
        res = g_string_free(s, FALSE);
        g_free(q); g_free(n); g_free(in);
    }
    g_match_info_free(mi);
    return res;
}

char *as_wrap_delay(const char *cmd, int delay)
{
    GString *s;
    if (delay <= 0) return g_strdup(cmd);
    s = g_string_new(NULL);
    g_string_append_printf(s, "sh -c \"sleep %d && ", delay);
    for (const char *p = cmd; *p; p++) {
        if (strchr("\"`$\\", *p)) g_string_append_c(s, '\\');
        g_string_append_c(s, *p);
    }
    g_string_append_c(s, '"');
    return g_string_free(s, FALSE);
}

/* drop the desktop-entry field codes (%f %U %i ...) a command copied from an application may have */
static char *strip_field_codes(const char *exec)
{
    GString *s = g_string_new(NULL);
    for (const char *p = exec; *p; p++) {
        if (*p == '%' && p[1]) {
            if (p[1] == '%') g_string_append_c(s, '%');
            p++;
            continue;
        }
        g_string_append_c(s, *p);
    }
    while (s->len && s->str[s->len - 1] == ' ') g_string_truncate(s, s->len - 1);
    {
        char *r = g_string_free(s, FALSE), *t;
        while ((t = strstr(r, "  "))) memmove(t, t + 1, strlen(t));
        return r;
    }
}

/* the program a command runs (skips env and VAR=value; looks inside sh -c "..."), NULL when unclear */
static char *program_of(const char *cmd)
{
    char **av = NULL, *res = NULL, *clean = strip_field_codes(cmd);
    int ac = 0, i = 0;
    if (!g_shell_parse_argv(clean, &ac, &av, NULL)) { g_free(clean); return NULL; }
    g_free(clean);
    while (i < ac && (!strcmp(av[i], "env") || (strchr(av[i], '=') && av[i][0] != '/' && av[i][0] != '='))) i++;
    if (i < ac) {
        const char *b = strrchr(av[i], '/') ? strrchr(av[i], '/') + 1 : av[i];
        if ((!strcmp(b, "sh") || !strcmp(b, "bash") || !strcmp(b, "dash")) && i + 2 < ac && !strcmp(av[i + 1], "-c")) {
            int d;
            char *in = NULL, *wrapped = g_strdup_printf("sh -c \"%s\"", av[i + 2]);
            in = as_unwrap_delay(wrapped, &d);
            g_free(wrapped);
            res = program_of(in ? in : av[i + 2]);
            g_free(in);
            if (!res) res = g_strdup(av[i]);
        } else res = g_strdup(av[i]);
    }
    g_strfreev(av);
    return res;
}

static int program_found(const char *prog)
{
    char *p;
    if (!prog || !*prog) return 0;
    if (prog[0] == '/') return access(prog, X_OK) == 0;
    if (prog[0] == '~') {
        char *x = g_build_filename(g_get_home_dir(), prog + 1, NULL);
        int ok = access(x, X_OK) == 0;
        g_free(x);
        return ok;
    }
    p = g_find_program_in_path(prog);
    g_free(p);
    return p != NULL;
}

char *as_check_cmd(const char *cmd)
{
    char *prog, *r = NULL;
    if (!cmd) return g_strdup("The command is empty.");
    {
        char *t = g_strdup(cmd);
        int empty = !*g_strstrip(t);
        g_free(t);
        if (empty) return g_strdup("The command is empty.");
    }
    prog = program_of(cmd);
    if (!prog) r = g_strdup("The command cannot be read (unbalanced quotes?).");
    else if (!program_found(prog)) r = g_strdup_printf("Program not found: %s", prog);
    g_free(prog);
    return r;
}

/* ------------------------------------------------------------------ reading */

static void entry_free(gpointer p)
{
    as_entry *e = p;
    g_free(e->key); g_free(e->id); g_free(e->name); g_free(e->comment); g_free(e->icon); g_free(e->exec);
    g_free(e->raw_exec); g_free(e->notes); g_free(e->path); g_free(e->sys_path); g_free(e->wanted_by);
    g_free(e);
}

static int desktop_match(char **list, char **desk)
{
    for (int i = 0; list && list[i]; i++)
        for (int j = 0; desk && desk[j]; j++)
            if (!g_ascii_strcasecmp(g_strstrip(list[i]), desk[j])) return 1;
    return 0;
}

static void note(GString *n, const char *t) { if (n->len) g_string_append(n, "; "); g_string_append(n, t); }

static as_entry *xdg_entry(const char *id, const char *upath, const char *spath, char **desk, const char *deskname)
{
    const char *path = upath ? upath : spath;
    GKeyFile *kf = load_kf(path), *skf = NULL;
    as_entry *e;
    GString *n;
    char *v, **only, **not_;
    if (!kf) return NULL;
    e = g_new0(as_entry, 1);
    n = g_string_new(NULL);
    e->id = g_strdup(id);
    e->key = g_strdup_printf("xdg:%s", id);
    e->path = g_strdup(path);
    e->sys_path = g_strdup(spath);
    e->source = upath && spath ? AS_SRC_XDG_OVERRIDE : upath ? AS_SRC_XDG_USER : AS_SRC_XDG_SYSTEM;
    e->name = g_key_file_get_locale_string(kf, DE, "Name", NULL, NULL);
    if (!e->name) { e->name = g_strdup(id); if (g_str_has_suffix(e->name, ".desktop")) e->name[strlen(e->name) - 8] = 0; }
    e->comment = g_key_file_get_locale_string(kf, DE, "Comment", NULL, NULL);
    e->icon = kstr(kf, "Icon");
    e->raw_exec = kstr(kf, "Exec");
    e->srs_override = kbool(kf, "X-SRS-Override", FALSE);
    e->srs_added = kbool(kf, "X-SRS-Added", FALSE);
    {
        int d = 0;
        char *in = as_unwrap_delay(e->raw_exec, &d);
        e->exec = in ? in : g_strdup(e->raw_exec ? e->raw_exec : "");
        e->delay = in ? d : 0;
        if (!in) {
            GError *ge = NULL;
            int dk = g_key_file_get_integer(kf, DE, "X-GNOME-Autostart-Delay", &ge);
            if (!ge) e->delay = dk;
            else g_error_free(ge);
        }
    }
    e->enabled = 1;
    e->starts = 1;
    if (kbool(kf, "Hidden", FALSE)) {
        e->enabled = 0;
        if (upath) note(n, "Turned off (Hidden=true)");
        else { note(n, "Turned off in the system entry (Hidden=true)"); e->hidden = 1; }
    }
    if (!kbool(kf, "X-GNOME-Autostart-enabled", TRUE)) {
        e->enabled = 0;
        note(n, "Turned off (X-GNOME-Autostart-enabled=false)");
    }
    if (kbool(kf, "NoDisplay", FALSE)) { e->hidden = 1; note(n, "NoDisplay=true (not shown in menus; still starts)"); }
    only = g_key_file_get_string_list(kf, DE, "OnlyShowIn", NULL, NULL);
    not_ = g_key_file_get_string_list(kf, DE, "NotShowIn", NULL, NULL);
    if (only && !desktop_match(only, desk)) {
        char *l = g_strjoinv(";", only), *t = g_strdup_printf("Not for this desktop (%s): only in %s", deskname, l);
        e->not_this_desktop = 1;
        note(n, t);
        g_free(l); g_free(t);
    } else if (not_ && desktop_match(not_, desk)) {
        char *t = g_strdup_printf("Not for this desktop (%s): NotShowIn lists it", deskname);
        e->not_this_desktop = 1;
        note(n, t);
        g_free(t);
    }
    g_strfreev(only);
    g_strfreev(not_);
    if (e->not_this_desktop) { e->hidden = 1; e->starts = 0; }
    if ((v = kstr(kf, "X-KDE-autostart-condition")) || (v = kstr(kf, "X-KDE-AutostartCondition"))) {
        char *t = g_strdup_printf("Starts only when this setting is on: %s", v);
        e->hidden = 1;
        note(n, t);
        g_free(t);
        g_free(v);
    }
    if ((v = kstr(kf, "TryExec")) && *v && !program_found(v)) {
        char *t = g_strdup_printf("TryExec program not found: %s", v);
        e->hidden = 1;
        e->starts = 0;
        note(n, t);
        g_free(t);
    }
    g_free(v);
    if (e->exec && *e->exec) {
        char *why = as_check_cmd(e->exec);
        if (why) { e->prog_missing = 1; e->starts = 0; note(n, why); g_free(why); }
    } else { e->prog_missing = 1; e->starts = 0; note(n, "No command (Exec is empty)"); }
    if (e->source == AS_SRC_XDG_OVERRIDE && (skf = load_kf(spath))) {
        if (e->srs_override) note(n, "Changed for you by SRS (a copy in your autostart folder)");
        else note(n, "Your own copy replaces the system entry");
        g_key_file_free(skf);
    }
    if (!e->enabled) e->starts = 0;
    e->notes = g_string_free(n, FALSE);
    g_key_file_free(kf);
    return e;
}

static void scan_xdg(GPtrArray *out, char **desk, const char *deskname)
{
    GHashTable *sys = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);   /* id -> path (first dir wins) */
    GHashTable *usr = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    GPtrArray *dirs = system_dirs();
    char *ud = user_dir();
    GDir *d;
    const char *f;
    for (guint i = 0; i < dirs->len; i++) {
        if (!(d = g_dir_open(dirs->pdata[i], 0, NULL))) continue;
        while ((f = g_dir_read_name(d)))
            if (g_str_has_suffix(f, ".desktop") && !g_hash_table_contains(sys, f))
                g_hash_table_insert(sys, g_strdup(f), g_build_filename(dirs->pdata[i], f, NULL));
        g_dir_close(d);
    }
    if ((d = g_dir_open(ud, 0, NULL))) {
        while ((f = g_dir_read_name(d)))
            if (g_str_has_suffix(f, ".desktop")) g_hash_table_insert(usr, g_strdup(f), g_build_filename(ud, f, NULL));
        g_dir_close(d);
    }
    {
        GHashTableIter it;
        gpointer k, v;
        g_hash_table_iter_init(&it, usr);
        while (g_hash_table_iter_next(&it, &k, &v)) {
            as_entry *e = xdg_entry(k, v, g_hash_table_lookup(sys, k), desk, deskname);
            if (e) g_ptr_array_add(out, e);
        }
        g_hash_table_iter_init(&it, sys);
        while (g_hash_table_iter_next(&it, &k, &v)) {
            as_entry *e;
            if (g_hash_table_contains(usr, k)) continue;
            if ((e = xdg_entry(k, NULL, v, desk, deskname))) g_ptr_array_add(out, e);
        }
    }
    g_hash_table_destroy(sys);
    g_hash_table_destroy(usr);
    g_ptr_array_unref(dirs);
    g_free(ud);
}

/* systemd --user units that are enabled (plus the ones SRS turned off) */
static void scan_systemd(GPtrArray *out)
{
    static const char *const lf[] = { "--user", "list-unit-files", "--state=enabled", "--no-legend", "--no-pager", NULL };
    char *o = NULL, **lines;
    GPtrArray *units = g_ptr_array_new_with_free_func(g_free), *mine = srs_units_read();
    if (run_systemctl(lf, &o, NULL) && o) {
        lines = g_strsplit(o, "\n", -1);
        for (int i = 0; lines[i]; i++) {
            char **w = g_strsplit_set(g_strstrip(lines[i]), " \t", 2);
            if (w[0] && *w[0] && !strstr(w[0], "@.") && !(g_str_has_prefix(w[0], "app-") && strstr(w[0], "@autostart")))
                g_ptr_array_add(units, g_strdup(w[0]));
            g_strfreev(w);
        }
        g_strfreev(lines);
    }
    g_free(o);
    for (guint i = 0; i < mine->len; i++) {
        int have = 0;
        for (guint j = 0; j < units->len; j++) if (!strcmp(units->pdata[j], mine->pdata[i])) have = 1;
        if (!have) g_ptr_array_add(units, g_strdup(mine->pdata[i]));
    }
    if (units->len) {
        GPtrArray *av = g_ptr_array_new();
        static const char *const head[] = { "--user", "show", "--no-pager", "-p", "Id", "-p", "Description", "-p", "WantedBy",
                                            "-p", "UnitFileState", "-p", "FragmentPath", "-p", "ExecStart", "-p", "ActiveState", NULL };
        for (int i = 0; head[i]; i++) g_ptr_array_add(av, (gpointer)head[i]);
        for (guint i = 0; i < units->len; i++) g_ptr_array_add(av, units->pdata[i]);
        g_ptr_array_add(av, NULL);
        o = NULL;
        if (run_systemctl((const char *const *)av->pdata, &o, NULL) && o) {
            char **blocks = g_regex_split_simple("\\n\\s*\\n", o, 0, 0);
            for (int b = 0; blocks[b]; b++) {
                char **kv = g_strsplit(blocks[b], "\n", -1);
                as_entry *e = g_new0(as_entry, 1);
                char *state = NULL, *active = NULL;
                GString *n = g_string_new(NULL);
                for (int k = 0; kv[k]; k++) {
                    char *eq = strchr(kv[k], '=');
                    if (!eq) continue;
                    *eq = 0;
                    if (!strcmp(kv[k], "Id")) e->id = g_strdup(eq + 1);
                    else if (!strcmp(kv[k], "Description")) e->name = g_strdup(eq + 1);
                    else if (!strcmp(kv[k], "WantedBy")) e->wanted_by = g_strdup(eq + 1);
                    else if (!strcmp(kv[k], "UnitFileState")) state = g_strdup(eq + 1);
                    else if (!strcmp(kv[k], "ActiveState")) active = g_strdup(eq + 1);
                    else if (!strcmp(kv[k], "FragmentPath")) e->path = g_strdup(eq + 1);
                    else if (!strcmp(kv[k], "ExecStart") && !e->exec) {
                        const char *a = strstr(eq + 1, "argv[]=");
                        if (a) { const char *z = strstr(a, " ;"); e->exec = g_strndup(a + 7, z ? (gsize)(z - a - 7) : strlen(a + 7)); }
                    }
                }
                g_strfreev(kv);
                if (!e->id || !*e->id) { g_string_free(n, TRUE); g_free(state); g_free(active); entry_free(e); continue; }
                e->source = AS_SRC_SYSTEMD;
                e->key = g_strdup_printf("systemd:%s", e->id);
                if (!e->name || !*e->name) { g_free(e->name); e->name = g_strdup(e->id); }
                if (!e->exec) e->exec = g_strdup("");
                e->raw_exec = g_strdup(e->exec);
                e->enabled = state && (!strcmp(state, "enabled") || !strcmp(state, "enabled-runtime") || !strcmp(state, "linked"));
                e->starts = e->enabled;
                note(n, e->id);
                if (e->wanted_by && *e->wanted_by) { char *t = g_strdup_printf("wanted by %s", e->wanted_by); note(n, t); g_free(t); }
                if (state && strcmp(state, "enabled")) { char *t = g_strdup_printf("unit file %s", state); note(n, t); g_free(t); }
                if (active) { char *t = g_strdup_printf("now %s", active); note(n, t); g_free(t); }
                e->notes = g_string_free(n, FALSE);
                g_free(state);
                g_free(active);
                g_ptr_array_add(out, e);
            }
            g_strfreev(blocks);
        }
        g_free(o);
        g_ptr_array_free(av, TRUE);
    }
    g_ptr_array_unref(units);
    g_ptr_array_unref(mine);
}

/* scripts: dir (enabled) and dir/disabled (turned off by SRS) */
static void scan_scripts(GPtrArray *out, const char *dir, as_source src, const char *suffix)
{
    for (int dis = 0; dis < 2; dis++) {
        char *dd = dis ? g_build_filename(dir, "disabled", NULL) : g_strdup(dir);
        GDir *d = g_dir_open(dd, 0, NULL);
        const char *f;
        while (d && (f = g_dir_read_name(d))) {
            as_entry *e;
            char *p = g_build_filename(dd, f, NULL);
            if (f[0] == '.' || g_file_test(p, G_FILE_TEST_IS_DIR) || (suffix && !g_str_has_suffix(f, suffix)) ||
                g_str_has_suffix(f, ".srs-bak") || g_str_has_suffix(f, ".tmp")) { g_free(p); continue; }
            e = g_new0(as_entry, 1);
            e->id = g_strdup(f);
            e->source = src;
            e->key = g_strdup_printf("%s:%s", src == AS_SRC_PLASMA_ENV ? "env" : "script", f);
            e->name = g_strdup(f);
            e->path = p;
            e->exec = g_strdup(p);
            e->raw_exec = g_strdup(p);
            e->enabled = e->starts = !dis;
            e->notes = g_strdup(src == AS_SRC_PLASMA_ENV
                                    ? (dis ? "Plasma login script, turned off (moved to env/disabled)" : "Plasma login script (sourced before Plasma starts)")
                                    : (dis ? "Autostart script, turned off (moved to autostart-scripts/disabled)" : "Legacy autostart script"));
            if (src == AS_SRC_SCRIPT && !dis && access(p, X_OK) != 0) {
                char *t = g_strdup_printf("%s; not executable, so it will not run", e->notes);
                g_free(e->notes);
                e->notes = t;
                e->starts = 0;
            }
            g_ptr_array_add(out, e);
        }
        if (d) g_dir_close(d);
        g_free(dd);
    }
}

static gint by_name(gconstpointer a, gconstpointer b)
{
    const as_entry *x = *(as_entry *const *)a, *y = *(as_entry *const *)b;
    int r = g_utf8_collate(x->name ? x->name : "", y->name ? y->name : "");
    return r ? r : strcmp(x->key, y->key);
}

as_list *as_load(void)
{
    as_list *l = g_new0(as_list, 1);
    const char *cd = getenv("XDG_CURRENT_DESKTOP");
    char **desk = g_strsplit(cd ? cd : "", ":", -1), *ed = env_dir(), *sd = scripts_dir();
    l->desktop = g_strdup(cd && *cd ? cd : "unknown desktop");
    l->items = g_ptr_array_new_with_free_func(entry_free);
    scan_xdg(l->items, desk, l->desktop);
    scan_systemd(l->items);
    scan_scripts(l->items, ed, AS_SRC_PLASMA_ENV, ".sh");
    scan_scripts(l->items, sd, AS_SRC_SCRIPT, NULL);
    g_ptr_array_sort(l->items, by_name);
    g_strfreev(desk);
    g_free(ed);
    g_free(sd);
    return l;
}

void as_list_free(as_list *l)
{
    if (!l) return;
    g_ptr_array_unref(l->items);
    g_free(l->desktop);
    g_free(l);
}

const as_entry *as_find(const as_list *l, const char *k)
{
    for (guint i = 0; i < l->items->len; i++) {
        const as_entry *e = l->items->pdata[i];
        if (!strcmp(e->key, k)) return e;
    }
    for (guint i = 0; i < l->items->len; i++) {
        const as_entry *e = l->items->pdata[i];
        if (!strcmp(e->id, k)) return e;
    }
    return NULL;
}

const char *as_source_name(as_source s)
{
    switch (s) {
    case AS_SRC_XDG_USER: return "User";
    case AS_SRC_XDG_SYSTEM: return "System";
    case AS_SRC_XDG_OVERRIDE: return "System, changed for you";
    case AS_SRC_SYSTEMD: return "systemd (user)";
    case AS_SRC_PLASMA_ENV: return "Plasma login script";
    default: return "Autostart script";
    }
}

const char *as_remove_label(const as_entry *e)
{
    if (!e) return "Remove";
    switch (e->source) {
    case AS_SRC_XDG_USER: return "Remove";
    case AS_SRC_SYSTEMD: return "Disable";
    case AS_SRC_PLASMA_ENV: case AS_SRC_SCRIPT: return "Disable";
    default: return "Disable for Me";
    }
}

char *as_folder(const as_entry *e)
{
    return e && e->path ? g_path_get_dirname(e->path) : user_dir();
}

/* ------------------------------------------------------------------ changes */

static void set_name(GKeyFile *kf, const char *name)
{
    const gchar *const *langs = g_get_language_names();
    g_key_file_set_string(kf, DE, "Name", name);
    for (int i = 0; langs[i]; i++) {
        char k[96];
        if (!strcmp(langs[i], "C")) continue;
        snprintf(k, sizeof k, "Name[%s]", langs[i]);
        if (g_key_file_has_key(kf, DE, k, NULL)) g_key_file_set_locale_string(kf, DE, "Name", langs[i], name);
    }
}

static void set_cmd(GKeyFile *kf, const char *cmd, int delay)
{
    char *ex = as_wrap_delay(cmd, delay);
    g_key_file_set_string(kf, DE, "Exec", ex);
    if (delay > 0) g_key_file_set_integer(kf, DE, "X-GNOME-Autostart-Delay", delay);
    else g_key_file_remove_key(kf, DE, "X-GNOME-Autostart-Delay", NULL);
    g_free(ex);
}

static void set_on(GKeyFile *kf, int on)
{
    g_key_file_set_boolean(kf, DE, "Hidden", !on);
    if (on && g_key_file_has_key(kf, DE, "X-GNOME-Autostart-enabled", NULL))
        g_key_file_set_boolean(kf, DE, "X-GNOME-Autostart-enabled", TRUE);
}

/* would the system file start by itself (not Hidden, not X-GNOME-Autostart-enabled=false)? */
static int system_on(const char *spath)
{
    GKeyFile *kf = load_kf(spath);
    int on;
    if (!kf) return 0;
    on = !kbool(kf, "Hidden", FALSE) && kbool(kf, "X-GNOME-Autostart-enabled", TRUE);
    g_key_file_free(kf);
    return on;
}

/* same Name / Exec / delay as the system file (an SRS override that only switches it off) */
static int override_plain(const char *upath, const char *spath)
{
    GKeyFile *u = load_kf(upath), *s = load_kf(spath);
    int same = 0;
    if (u && s) {
        char *a = kstr(u, "Exec"), *b = kstr(s, "Exec"), *c = kstr(u, "Name"), *d = kstr(s, "Name");
        same = !g_strcmp0(a, b) && !g_strcmp0(c, d);
        g_free(a); g_free(b); g_free(c); g_free(d);
    }
    if (u) g_key_file_free(u);
    if (s) g_key_file_free(s);
    return same;
}

static gboolean edit_xdg(const as_entry *e, const char *name, const char *cmd, int delay, int enabled, char **err)
{
    char *ud = user_dir(), *upath = g_build_filename(ud, e->id, NULL);
    GKeyFile *kf;
    gboolean ok = FALSE;
    int creating = !exists(upath);
    if (e->source == AS_SRC_XDG_SYSTEM || creating) {
        /* a per-user copy of the system entry; the system file itself is never touched */
        kf = e->sys_path ? load_kf(e->sys_path) : NULL;
        if (!kf) { set_err(err, "Cannot read %s", e->sys_path ? e->sys_path : e->id); goto out; }
        g_key_file_set_boolean(kf, DE, "X-SRS-Override", TRUE);
    } else {
        kf = load_kf(upath);
        if (!kf) { set_err(err, "Cannot read %s", upath); goto out; }
        backup_once(upath);
    }
    if (name) set_name(kf, name);
    if (cmd || delay >= 0) set_cmd(kf, cmd ? cmd : e->exec, delay >= 0 ? delay : e->delay);
    if (enabled >= 0) set_on(kf, enabled);
    ch_file(upath);
    ok = save_kf(kf, upath, err);
    g_key_file_free(kf);
out:
    g_free(ud);
    g_free(upath);
    return ok;
}

/* plain file move with undo snapshots of both ends */
static gboolean move_file(const char *from, const char *to, char **err)
{
    char *data = NULL, *dir = g_path_get_dirname(to);
    gsize len;
    gboolean ok = FALSE;
    struct stat st;
    if (exists(to)) { set_err(err, "%s already exists", to); goto out; }
    if (!g_file_get_contents(from, &data, &len, NULL)) { set_err(err, "Cannot read %s", from); goto out; }
    ch_file(from);
    ch_file(to);
    if (!write_atomic(to, data, len, err)) goto out;
    if (stat(from, &st) == 0) chmod(to, st.st_mode & 07777);
    if (unlink(from) != 0) { set_err(err, "Cannot move %s: %s", from, g_strerror(errno)); unlink(to); goto out; }
    ok = TRUE;
out:
    g_free(data);
    g_free(dir);
    return ok;
}

static gboolean script_toggle(const as_entry *e, int on, char **err)
{
    char *base = e->source == AS_SRC_PLASMA_ENV ? env_dir() : scripts_dir(), *to;
    gboolean ok;
    if (on) to = g_build_filename(base, e->id, NULL);
    else to = g_build_filename(base, "disabled", e->id, NULL);
    if (!strcmp(to, e->path)) { g_free(base); g_free(to); return TRUE; }
    ok = move_file(e->path, to, err);
    g_free(base);
    g_free(to);
    return ok;
}

static gboolean unit_toggle(const as_entry *e, int on, char **err)
{
    const char *const a[] = { "--user", on ? "enable" : "disable", "--", e->id, NULL };
    const char *const undo[] = { "--user", on ? "disable" : "enable", "--", e->id, NULL };
    if (!run_systemctl(a, NULL, err)) return FALSE;
    ch_cmd(undo);
    srs_units_set(e->id, !on);
    return TRUE;
}

gboolean as_edit(const as_entry *e, const char *name, const char *cmd, int delay, int enabled, char **err)
{
    gboolean ok;
    char *what;
    if (!e) { set_err(err, "No entry"); return FALSE; }
    if ((e->source == AS_SRC_SYSTEMD || e->source == AS_SRC_PLASMA_ENV || e->source == AS_SRC_SCRIPT) &&
        (name || cmd || delay >= 0)) {
        set_err(err, "Only On / Off can be changed for a %s here.", as_source_name(e->source));
        return FALSE;
    }
    what = enabled >= 0 && !name && !cmd && delay < 0 ? g_strdup_printf("%s \"%s\"", enabled ? "Enable" : "Disable", e->name)
                                                        : g_strdup_printf("Edit \"%s\"", e->name);
    ch_begin(what);
    g_free(what);
    switch (e->source) {
    case AS_SRC_SYSTEMD: ok = enabled < 0 || unit_toggle(e, enabled, err); break;
    case AS_SRC_PLASMA_ENV: case AS_SRC_SCRIPT: ok = enabled < 0 || script_toggle(e, enabled, err); break;
    default:
        /* enabling a system entry that SRS switched off: drop SRS's copy when it changes nothing else */
        if (e->source == AS_SRC_XDG_OVERRIDE && e->srs_override && enabled == 1 && !name && !cmd && delay < 0 &&
            system_on(e->sys_path) && override_plain(e->path, e->sys_path)) {
            ch_file(e->path);
            ok = unlink(e->path) == 0;
            if (!ok) set_err(err, "Cannot remove %s: %s", e->path, g_strerror(errno));
        } else
            ok = edit_xdg(e, name, cmd, delay, enabled, err);
        break;
    }
    if (ok) ch_commit();
    else ch_abort();
    return ok;
}

gboolean as_set_enabled(const as_entry *e, gboolean on, char **err) { return as_edit(e, NULL, NULL, -1, on ? 1 : 0, err); }

gboolean as_remove(const as_entry *e, char **err)
{
    gboolean ok;
    char *what;
    if (!e) { set_err(err, "No entry"); return FALSE; }
    if (e->source != AS_SRC_XDG_USER) return as_set_enabled(e, FALSE, err);
    what = g_strdup_printf("Remove \"%s\"", e->name);
    ch_begin(what);
    g_free(what);
    {
        char *td = trash_dir(), *to = g_build_filename(td, e->id, NULL);
        GKeyFile *kf = load_kf(e->path);
        if (exists(to)) {
            char *base = g_strndup(e->id, g_str_has_suffix(e->id, ".desktop") ? strlen(e->id) - 8 : strlen(e->id));
            g_free(to);
            to = g_strdup_printf("%s/%s-%ld.desktop", td, base, (long)time(NULL));
            g_free(base);
        }
        if (!kf) { set_err(err, "Cannot read %s", e->path); ok = FALSE; }
        else {
            /* the trashed copy says where it came from; the original is then removed */
            g_key_file_set_string(kf, DE, "X-SRS-Removed-From", e->id);
            ch_file(e->path);
            ch_file(to);
            ok = save_kf(kf, to, err);
            if (ok && unlink(e->path) != 0) { set_err(err, "Cannot remove %s: %s", e->path, g_strerror(errno)); unlink(to); ok = FALSE; }
            g_key_file_free(kf);
        }
        g_free(td);
        g_free(to);
    }
    if (ok) ch_commit();
    else ch_abort();
    return ok;
}

static char *slug(const char *name)
{
    GString *s = g_string_new(NULL);
    char *a = g_str_to_ascii(name, NULL);
    for (const char *p = a; *p && s->len < 48; p++) {
        if (g_ascii_isalnum(*p)) g_string_append_c(s, g_ascii_tolower(*p));
        else if (s->len && s->str[s->len - 1] != '-') g_string_append_c(s, '-');
    }
    while (s->len && s->str[s->len - 1] == '-') g_string_truncate(s, s->len - 1);
    g_free(a);
    if (!s->len) g_string_append(s, "startup-app");
    return g_string_free(s, FALSE);
}

gboolean as_add(const char *name, const char *cmd, const char *icon, const char *comment, int delay, gboolean only_this_desktop,
                const char *base_id, char **out_key, char **err)
{
    char *ud = user_dir(), *base, *id = NULL, *path = NULL, *clean, *what;
    GKeyFile *kf;
    gboolean ok;
    if (!name || !*name || !cmd || !*cmd) { set_err(err, "A name and a command are needed."); g_free(ud); return FALSE; }
    base = base_id && *base_id ? g_strndup(base_id, g_str_has_suffix(base_id, ".desktop") ? strlen(base_id) - 8 : strlen(base_id))
                               : slug(name);
    for (int n = 1; n < 1000; n++) {
        g_free(id);
        g_free(path);
        id = n == 1 ? g_strdup_printf("%s.desktop", base) : g_strdup_printf("%s-%d.desktop", base, n);
        path = g_build_filename(ud, id, NULL);
        if (!exists(path)) break;
    }
    clean = strip_field_codes(cmd);
    kf = g_key_file_new();
    g_key_file_set_string(kf, DE, "Type", "Application");
    g_key_file_set_string(kf, DE, "Name", name);
    if (comment && *comment) g_key_file_set_string(kf, DE, "Comment", comment);
    set_cmd(kf, clean, delay);
    if (icon && *icon) g_key_file_set_string(kf, DE, "Icon", icon);
    if (only_this_desktop) {
        const char *cd = getenv("XDG_CURRENT_DESKTOP");
        char **d = g_strsplit(cd && *cd ? cd : "KDE", ":", 2);
        char *v = g_strdup_printf("%s;", d[0]);
        g_key_file_set_string(kf, DE, "OnlyShowIn", v);
        g_free(v);
        g_strfreev(d);
    }
    g_key_file_set_boolean(kf, DE, "X-SRS-Added", TRUE);
    what = g_strdup_printf("Add \"%s\"", name);
    ch_begin(what);
    g_free(what);
    ch_file(path);
    ok = save_kf(kf, path, err);
    if (ok) { ch_commit(); if (out_key) *out_key = g_strdup_printf("xdg:%s", id); }
    else ch_abort();
    g_key_file_free(kf);
    g_free(clean);
    g_free(base);
    g_free(id);
    g_free(path);
    g_free(ud);
    return ok;
}

/* ------------------------------------------------------------------ the trash */

GPtrArray *as_trash_list(void)
{
    char *td = trash_dir();
    GPtrArray *a = g_ptr_array_new_with_free_func(g_free);
    GDir *d = g_dir_open(td, 0, NULL);
    const char *f;
    while (d && (f = g_dir_read_name(d))) if (g_str_has_suffix(f, ".desktop")) g_ptr_array_add(a, g_strdup(f));
    if (d) g_dir_close(d);
    g_ptr_array_sort(a, (GCompareFunc)g_strcmp0);
    g_free(td);
    return a;
}

char *as_trash_name(const char *file)
{
    char *td = trash_dir(), *p = g_build_filename(td, file, NULL), *n = NULL;
    GKeyFile *kf = load_kf(p);
    if (kf) { n = g_key_file_get_locale_string(kf, DE, "Name", NULL, NULL); g_key_file_free(kf); }
    g_free(td);
    g_free(p);
    return n ? n : g_strdup(file);
}

gboolean as_restore(const char *file, char **err)
{
    char *td = trash_dir(), *ud = user_dir(), *from = g_build_filename(td, file, NULL), *orig = NULL, *to = NULL, *what, *nm;
    GKeyFile *kf = load_kf(from);
    gboolean ok = FALSE;
    if (!kf) { set_err(err, "Cannot read %s", from); goto out; }
    orig = g_key_file_get_string(kf, DE, "X-SRS-Removed-From", NULL);
    if (!orig || strchr(orig, '/') || !g_str_has_suffix(orig, ".desktop")) { g_free(orig); orig = g_strdup(file); }
    to = g_build_filename(ud, orig, NULL);
    if (exists(to)) { set_err(err, "%s is in your autostart folder again; restore would replace it.", orig); goto out; }
    g_key_file_remove_key(kf, DE, "X-SRS-Removed-From", NULL);
    nm = g_key_file_get_locale_string(kf, DE, "Name", NULL, NULL);
    what = g_strdup_printf("Restore \"%s\"", nm ? nm : orig);
    g_free(nm);
    ch_begin(what);
    g_free(what);
    ch_file(from);
    ch_file(to);
    ok = save_kf(kf, to, err);
    if (ok && unlink(from) != 0) { set_err(err, "Cannot remove %s: %s", from, g_strerror(errno)); ok = FALSE; }
    if (ok) ch_commit();
    else ch_abort();
out:
    if (kf) g_key_file_free(kf);
    g_free(td); g_free(ud); g_free(from); g_free(orig); g_free(to);
    return ok;
}

/* ------------------------------------------------------------------ Restore Default */

/* the original of a user entry: <file>.srs-bak (this page) or the old 2.2 manager's startup-backup/<id>.orig */
static char *original_of(const as_entry *e)
{
    char *p = g_strdup_printf("%s.srs-bak", e->path ? e->path : "");
    if (e->path && exists(p)) return p;
    g_free(p);
    p = g_build_filename(g_get_user_config_dir(), "srs", "startup-backup", NULL);
    {
        char *o = g_strdup_printf("%s/%s.orig", p, e->id);
        g_free(p);
        if (exists(o)) return o;
        g_free(o);
    }
    return NULL;
}

const char *as_default_what(const as_entry *e)
{
    char *o;
    if (!e) return NULL;
    switch (e->source) {
    case AS_SRC_XDG_OVERRIDE:
        return e->srs_override ? "Remove SRS's copy of this system entry: the system default applies again"
                               : "Remove your own copy of this system entry: the system default applies again";
    case AS_SRC_XDG_USER:
        if ((o = original_of(e))) { g_free(o); return "Put back your original file (the copy kept before SRS first changed it)"; }
        return NULL;
    case AS_SRC_SYSTEMD:
        return "systemctl --user preset: on or off the way the unit's vendor preset says";
    default:
        return NULL;
    }
}

gboolean as_restore_default(const as_entry *e, char **err)
{
    char *what;
    gboolean ok = FALSE;
    if (!as_default_what(e)) { set_err(err, "This entry has no other default."); return FALSE; }
    what = g_strdup_printf("Restore Default \"%s\"", e->name);
    ch_begin(what);
    g_free(what);
    if (e->source == AS_SRC_XDG_OVERRIDE) {
        ch_file(e->path);
        ok = unlink(e->path) == 0;
        if (!ok) set_err(err, "Cannot remove %s: %s", e->path, g_strerror(errno));
    } else if (e->source == AS_SRC_XDG_USER) {
        char *o = original_of(e), *data = NULL;
        gsize len = 0;
        if (o && g_file_get_contents(o, &data, &len, NULL)) {
            ch_file(e->path);
            ok = write_atomic(e->path, data, len, err);   /* the original stays where it is (never deleted) */
        } else set_err(err, "The original file cannot be read.");
        g_free(data);
        g_free(o);
    } else if (e->source == AS_SRC_SYSTEMD) {
        const char *const a[] = { "--user", "preset", "--", e->id, NULL };
        const char *const undo[] = { "--user", e->enabled ? "enable" : "disable", "--", e->id, NULL };
        ok = run_systemctl(a, NULL, err);
        if (ok) ch_cmd(undo);
    }
    if (ok) ch_commit();
    else ch_abort();
    return ok;
}

/* ------------------------------------------------------------------ the old (2.2) Startup manager's backups */

static char *old_dir(void) { return g_build_filename(g_get_user_config_dir(), "srs", "startup-backup", NULL); }

/* "foo.desktop.orig", "foo.desktop.20261020-101500" or "foo.desktop.20261020-101500-2" -> "foo.desktop" */
static char *old_target(const char *file)
{
    const char *d = strstr(file, ".desktop");
    if (!d || d == file) return NULL;
    return g_strndup(file, (gsize)(d - file) + 8);
}

GPtrArray *as_old_backups(void)
{
    char *od = old_dir();
    GPtrArray *a = g_ptr_array_new_with_free_func(g_free);
    GDir *d = g_dir_open(od, 0, NULL);
    const char *f;
    while (d && (f = g_dir_read_name(d))) {
        char *t = old_target(f);
        if (t && strcmp(t, f)) g_ptr_array_add(a, g_strdup(f));
        g_free(t);
    }
    if (d) g_dir_close(d);
    g_ptr_array_sort(a, (GCompareFunc)g_strcmp0);
    g_free(od);
    return a;
}

char *as_old_backup_name(const char *file)
{
    char *od = old_dir(), *p = g_build_filename(od, file, NULL), *n = NULL;
    GKeyFile *kf = load_kf(p);
    if (kf) { n = g_key_file_get_locale_string(kf, DE, "Name", NULL, NULL); g_key_file_free(kf); }
    g_free(od);
    g_free(p);
    return n ? n : g_strdup(file);
}

gboolean as_import_old_backup(const char *file, char **err)
{
    char *od = old_dir(), *ud = user_dir(), *from = g_build_filename(od, file, NULL), *t = old_target(file), *to = NULL, *data = NULL, *what, *nm;
    gsize len = 0;
    gboolean ok = FALSE;
    if (!t || strchr(file, '/')) { set_err(err, "Not a startup entry backup: %s", file); goto out; }
    to = g_build_filename(ud, t, NULL);
    if (exists(to)) { set_err(err, "%s is in your autostart folder already; bringing the old copy back would replace it.", t); goto out; }
    if (!g_file_get_contents(from, &data, &len, NULL)) { set_err(err, "Cannot read %s", from); goto out; }
    nm = as_old_backup_name(file);
    what = g_strdup_printf("Bring back \"%s\" (old Startup manager)", nm);
    g_free(nm);
    ch_begin(what);
    g_free(what);
    ch_file(to);
    ok = write_atomic(to, data, len, err);   /* a copy: the old backup stays (never deleted) */
    if (ok) ch_commit();
    else ch_abort();
out:
    g_free(od); g_free(ud); g_free(from); g_free(t); g_free(to); g_free(data);
    return ok;
}

int as_old_backups_offer_once(void)
{
    char *f = state_file("old-startup-backups-offered");
    GPtrArray *a;
    int n = 0;
    if (!exists(f)) {
        a = as_old_backups();
        n = (int)a->len;
        g_ptr_array_unref(a);
        if (n) { char *e = NULL; write_atomic(f, "1\n", 2, &e); g_free(e); }
    }
    g_free(f);
    return n;
}

/* ------------------------------------------------------------------ installed applications */

void as_app_free(as_app *a)
{
    if (!a) return;
    g_free(a->id); g_free(a->name); g_free(a->exec); g_free(a->icon); g_free(a->comment);
    g_free(a);
}

static gint app_cmp(gconstpointer a, gconstpointer b)
{
    const as_app *x = *(as_app *const *)a, *y = *(as_app *const *)b;
    return g_utf8_collate(x->name, y->name);
}

static void apps_dir(GHashTable *seen, GPtrArray *out, const char *dir)
{
    GDir *d = g_dir_open(dir, 0, NULL);
    const char *f;
    while (d && (f = g_dir_read_name(d))) {
        char *p;
        GKeyFile *kf;
        if (!g_str_has_suffix(f, ".desktop") || g_hash_table_contains(seen, f)) continue;
        g_hash_table_add(seen, g_strdup(f));
        p = g_build_filename(dir, f, NULL);
        kf = g_key_file_new();
        if (g_key_file_load_from_file(kf, p, G_KEY_FILE_NONE, NULL)) {
            char *type = kstr(kf, "Type"), *ex = kstr(kf, "Exec");
            if (type && !strcmp(type, "Application") && ex && *ex && !kbool(kf, "NoDisplay", FALSE) && !kbool(kf, "Hidden", FALSE)) {
                as_app *a = g_new0(as_app, 1);
                a->id = g_strdup(f);
                a->name = g_key_file_get_locale_string(kf, DE, "Name", NULL, NULL);
                if (!a->name) a->name = g_strdup(f);
                a->exec = strip_field_codes(ex);
                a->icon = kstr(kf, "Icon");
                a->comment = g_key_file_get_locale_string(kf, DE, "Comment", NULL, NULL);
                g_ptr_array_add(out, a);
            }
            g_free(type);
            g_free(ex);
        }
        g_key_file_free(kf);
        g_free(p);
    }
    if (d) g_dir_close(d);
}

GPtrArray *as_apps(void)
{
    GPtrArray *out = g_ptr_array_new_with_free_func((GDestroyNotify)as_app_free);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    const gchar *const *dd = g_get_system_data_dirs();
    char *p = g_build_filename(g_get_user_data_dir(), "applications", NULL);
    apps_dir(seen, out, p);
    g_free(p);
    for (int i = 0; dd[i]; i++) {
        p = g_build_filename(dd[i], "applications", NULL);
        apps_dir(seen, out, p);
        g_free(p);
    }
    g_hash_table_destroy(seen);
    g_ptr_array_sort(out, app_cmp);
    return out;
}
