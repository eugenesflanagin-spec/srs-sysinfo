/* gui_tabs.inc - included by gui_gtk.c (one translation unit): the main window's tabs (2.2).
 *
 * Each page shows a list of items as tabs. The list, its order, "every item as a tab" and the tab-bar
 * layout make up the current state, kept in SRSDEF.ini [Tabs] across restarts. A lineup is a named
 * copy of such a state: Basic, Enthusiast (the 2.0 tabs) and Expert (the default) are built in, users
 * save their own (~/.config/srs/lineups/NAME.srslineup, the same file format as Export / Import).
 * Changes made on the fly (add, close, replace, drag, keep) change the current state only, until
 * "Save to this lineup" or "Save as new lineup".
 * Tabs opened from the item list, the search, Details or a device lookup are temporary (italic):
 * they are not kept unless "Keep this tab" is chosen.
 * Items that cannot show anything here are hidden: SIV's Windows-only items and hardware SIV checks for
 * that is not present (bucket flags, always), and items whose output on this PC holds no values. That
 * last check runs in a child process, cached in ~/.cache/srs/empty-items.txt, only when the user asks
 * (Options > Check now) or turns on Options > Check daily; it never runs the network items or Ping.
 * "Show hidden items" lists them greyed.
 */

enum { LAY_ROW, LAY_WRAP, LAY_SIDE, LAY_N };
static const char *const g_lay_key[LAY_N] = { "row", "wrap", "side" };
enum { LU_BASIC = 1, LU_ENTHUSIAST, LU_EXPERT };
#define LU_DEFAULT LU_EXPERT
static const char *const g_lu_builtin[] = { NULL, "Basic", "Enthusiast", "Expert" };

typedef struct { GPtrArray *tabs; int every; } pagestate;
typedef struct { char name[128]; int layout; pagestate pg[CAT_N]; } lineup;

static lineup g_cur;                       /* what the main window shows */
static char g_lineup[128] = "Expert";  /* the lineup it came from */
static int g_lu_ask;                    /* no lineup chosen yet: ask on the first start */
static int g_lu_returning;              /* ... and SRS was used before (2.0): keep their tabs unless they choose */
static int g_opt_alltabs, g_opt_showhidden;
static GHashTable *g_empty;                /* item id -> "1": nothing to show on this PC */
static int g_scan_running;

/* ---- lineups: model ---- */
static void lu_clear(lineup *l)
{
    int c;
    for (c = 0; c < CAT_N; c++) { if (l->pg[c].tabs) g_ptr_array_free(l->pg[c].tabs, TRUE); l->pg[c].tabs = NULL; l->pg[c].every = 0; }
}

static void lu_page_set(lineup *l, int c, const char *const *ids, int every)
{
    if (l->pg[c].tabs) g_ptr_array_free(l->pg[c].tabs, TRUE);
    l->pg[c].tabs = g_ptr_array_new_with_free_func(g_free);
    for (; ids && *ids; ids++) g_ptr_array_add(l->pg[c].tabs, g_strdup(*ids));
    l->pg[c].every = every;
}

static void lu_page_copy(lineup *l, int c, const GPtrArray *ids, int every)
{
    guint i;
    if (l->pg[c].tabs) g_ptr_array_free(l->pg[c].tabs, TRUE);
    l->pg[c].tabs = g_ptr_array_new_with_free_func(g_free);
    for (i = 0; ids && i < ids->len; i++) g_ptr_array_add(l->pg[c].tabs, g_strdup(g_ptr_array_index(ids, i)));
    l->pg[c].every = every;
}

static void lu_copy(lineup *d, const lineup *s)
{
    int c;
    guint i;
    lu_clear(d);
    snprintf(d->name, sizeof d->name, "%s", s->name);
    d->layout = s->layout;
    for (c = 0; c < CAT_N; c++) {
        d->pg[c].tabs = g_ptr_array_new_with_free_func(g_free);
        d->pg[c].every = s->pg[c].every;
        for (i = 0; s->pg[c].tabs && i < s->pg[c].tabs->len; i++) g_ptr_array_add(d->pg[c].tabs, g_strdup(g_ptr_array_index(s->pg[c].tabs, i)));
    }
}

static int lu_equal(const lineup *a, const lineup *b)
{
    int c;
    guint i;
    if (a->layout != b->layout) return 0;
    for (c = 1; c < CAT_N; c++) {
        const GPtrArray *x = a->pg[c].tabs, *y = b->pg[c].tabs;
        if (a->pg[c].every != b->pg[c].every) return 0;
        if ((x ? x->len : 0) != (y ? y->len : 0)) return 0;
        for (i = 0; x && i < x->len; i++) if (strcmp(g_ptr_array_index(x, i), g_ptr_array_index(y, i))) return 0;
    }
    return 1;
}

static void lu_builtin(lineup *l, int which)
{
    /* Basic: the friendliest few per page; pages not listed keep the standard tabs */
    static const struct { const char *page; const char *ids[6]; } basic[] = {
        { "processor", { "cpus", "cpu-clocks", "cache", NULL } }, { "memory", { "memory", "dimms", NULL } },
        { "board", { "machine", "firmware", NULL } }, { "graphics", { "graphics", "monitors", "audio", NULL } },
        { "storage", { "drives", "volumes", "smart", NULL } }, { "network", { "adapters", "ip-address", "wi-fi", NULL } },
        { "sensors", { "sensors", "cooling", NULL } }, { "devices", { "all-dev", "usb-tree", NULL } },
        { "os", { "windows", "startup", "software", NULL } }, { "bench", { "benchmark", NULL } }, { "tools", { "about", "help", NULL } },
        { NULL, { NULL } } };
    int c, k;
    lu_clear(l);
    snprintf(l->name, sizeof l->name, "%s", g_lu_builtin[which]);
    l->layout = which == LU_EXPERT ? LAY_SIDE : LAY_ROW;
    for (c = 1; c < CAT_N; c++) {
        lu_page_set(l, c, which == LU_EXPERT ? NULL : g_cats[c].tabs, which == LU_EXPERT);
        if (which == LU_BASIC)
            for (k = 0; basic[k].page; k++) if (!strcmp(basic[k].page, g_cats[c].name)) lu_page_set(l, c, basic[k].ids, 0);
    }
}

static int lu_builtin_index(const char *name)
{
    int i;
    for (i = 1; i <= LU_EXPERT; i++) if (!g_ascii_strcasecmp(name, g_lu_builtin[i])) return i;
    return 0;
}

static int lay_from(const char *s)
{
    int i;
    for (i = 0; s && i < LAY_N; i++) if (!g_ascii_strcasecmp(s, g_lay_key[i])) return i;
    return LAY_ROW;
}

/* write / read a lineup in a key file: pages in group "<prefix><page name>", or keys "<page>" in one group */
static void lu_to_kf(const lineup *l, GKeyFile *kf, int state)
{
    int c;
    if (state) {
        g_key_file_set_string(kf, "Tabs", "Lineup", g_lineup);
        g_key_file_set_string(kf, "Tabs", "Layout", g_lay_key[l->layout]);
    } else {
        g_key_file_set_string(kf, "SRS Lineup", "Name", l->name);
        g_key_file_set_integer(kf, "SRS Lineup", "Version", 1);
        g_key_file_set_string(kf, "SRS Lineup", "Layout", g_lay_key[l->layout]);
    }
    for (c = 1; c < CAT_N; c++) {
        char grp[80], key[96];
        const GPtrArray *t = l->pg[c].tabs;
        if (c == CAT_POWER) continue;   /* no item tabs on that page */
        snprintf(grp, sizeof grp, state ? "Tabs" : "Page %s", g_cats[c].name);
        snprintf(key, sizeof key, "%s", state ? g_cats[c].name : "Tabs");
        g_key_file_set_string_list(kf, grp, key, t ? (const char *const *)t->pdata : NULL, t ? t->len : 0);
        snprintf(key, sizeof key, state ? "%s Every" : "Every", g_cats[c].name);
        g_key_file_set_boolean(kf, grp, key, l->pg[c].every);
    }
}

/* 0 if the key file holds no lineup; pages it does not name keep `base`'s tabs */
static int lu_from_kf(lineup *l, GKeyFile *kf, int state, const lineup *base)
{
    int c;
    char *v;
    const char *g0 = state ? "Tabs" : "SRS Lineup";
    if (!g_key_file_has_group(kf, g0)) return 0;
    lu_copy(l, base);
    if (!state) {
        if (!(v = g_key_file_get_string(kf, g0, "Name", NULL)) || !*g_strstrip(v)) { g_free(v); return 0; }
        snprintf(l->name, sizeof l->name, "%s", v);
        g_free(v);
    }
    v = g_key_file_get_string(kf, g0, "Layout", NULL);
    if (v) l->layout = lay_from(v);
    g_free(v);
    for (c = 1; c < CAT_N; c++) {
        char grp[80], key[96], **ids;
        gsize n = 0;
        snprintf(grp, sizeof grp, state ? "Tabs" : "Page %s", g_cats[c].name);
        snprintf(key, sizeof key, "%s", state ? g_cats[c].name : "Tabs");
        if (!g_key_file_has_key(kf, grp, key, NULL)) continue;
        ids = g_key_file_get_string_list(kf, grp, key, &n, NULL);
        snprintf(key, sizeof key, state ? "%s Every" : "Every", g_cats[c].name);
        lu_page_set(l, c, (const char *const *)ids, g_key_file_get_boolean(kf, grp, key, NULL));
        g_strfreev(ids);
    }
    return 1;
}

static char *lu_dir(void) { return g_build_filename(g_get_user_config_dir(), "srs", "lineups", NULL); }

static char *lu_path(const char *name)
{
    char *dir = lu_dir(), *safe = g_strdup(name), *p, *f, *path;
    for (p = safe; *p; p++) if (!g_ascii_isalnum(*p) && *p != '-' && *p != '_') *p = '_';
    f = g_strdup_printf("%s.srslineup", safe);
    path = g_build_filename(dir, f, NULL);
    g_free(dir); g_free(safe); g_free(f);
    return path;
}

static int lu_save_file(const lineup *l, const char *path)
{
    GKeyFile *kf = g_key_file_new();
    char *dir = g_path_get_dirname(path);
    int ok;
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    g_key_file_set_comment(kf, NULL, NULL, " SRS tab lineup: which items each page shows as tabs, in this order "
                           "(srs-gui: Menu > Lineup > Import lineup...)", NULL);
    lu_to_kf(l, kf, 0);
    ok = g_key_file_save_to_file(kf, path, NULL);
    g_key_file_free(kf);
    return ok;
}

static int lu_load_file(lineup *l, const char *path)
{
    GKeyFile *kf = g_key_file_new();
    lineup base = { "", 0, { { 0 } } };
    int ok = 0;
    lu_builtin(&base, LU_ENTHUSIAST);
    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) ok = lu_from_kf(l, kf, 0, &base);
    g_key_file_free(kf);
    lu_clear(&base);
    return ok;
}

/* a lineup by name: built-in or the user's; 0 if there is none */
static int lu_get(lineup *l, const char *name)
{
    int b = lu_builtin_index(name);
    char *p;
    int ok;
    if (b) { lu_builtin(l, b); return 1; }
    p = lu_path(name);
    ok = lu_load_file(l, p);
    g_free(p);
    return ok;
}

/* names of the user's lineups (sorted; free with g_ptr_array_free) */
static GPtrArray *lu_user_names(void)
{
    GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
    char *dir = lu_dir();
    GDir *d = g_dir_open(dir, 0, NULL);
    const char *f;
    while (d && (f = g_dir_read_name(d))) {
        lineup l = { "", 0, { { 0 } } };
        char *p;
        if (!g_str_has_suffix(f, ".srslineup")) continue;
        p = g_build_filename(dir, f, NULL);
        if (lu_load_file(&l, p) && !lu_builtin_index(l.name)) g_ptr_array_add(v, g_strdup(l.name));
        lu_clear(&l);
        g_free(p);
    }
    if (d) g_dir_close(d);
    g_free(dir);
    {   /* by name */
        guint i, j;
        for (i = 1; i < v->len; i++)
            for (j = i; j > 0 && g_ascii_strcasecmp(g_ptr_array_index(v, j - 1), g_ptr_array_index(v, j)) > 0; j--) {
                gpointer t = v->pdata[j]; v->pdata[j] = v->pdata[j - 1]; v->pdata[j - 1] = t;
            }
    }
    return v;
}

static int lineup_changed(void)
{
    lineup l = { "", 0, { { 0 } } };
    int ch = !lu_get(&l, g_lineup) || !lu_equal(&l, &g_cur);
    lu_clear(&l);
    return ch;
}

/* ---- current state in SRSDEF.ini [Tabs], and the options ---- */
static void tabs_state_load(void)
{
    GKeyFile *kf = g_key_file_new();
    lineup base = { "", 0, { { 0 } } };
    int used = g_file_test(g_ini, G_FILE_TEST_EXISTS);   /* SRS ran here before */
    g_lu_ask = 1;
    g_lu_returning = 0;
    if (used && g_key_file_load_from_file(kf, g_ini, G_KEY_FILE_NONE, NULL)) {
        char *n = g_key_file_get_string(kf, "Tabs", "Lineup", NULL);
        g_lu_returning = !(n && *n);   /* 2.0 had no lineups: the tabs they know are the Enthusiast ones */
        g_free(n);
    }
    lu_builtin(&base, g_lu_returning ? LU_ENTHUSIAST : LU_DEFAULT);
    snprintf(g_lineup, sizeof g_lineup, "%s", g_lu_builtin[g_lu_returning ? LU_ENTHUSIAST : LU_DEFAULT]);
    lu_copy(&g_cur, &base);
    if (used && g_key_file_load_from_file(kf, g_ini, G_KEY_FILE_NONE, NULL)) {
        char *n = g_key_file_get_string(kf, "Tabs", "Lineup", NULL);
        if (n && *n) g_lu_ask = 0;
        lineup from = { "", 0, { { 0 } } };
        if (n && *n) {
            snprintf(g_lineup, sizeof g_lineup, "%s", n);
            if (lu_get(&from, n)) lu_copy(&base, &from);   /* pages the state does not name: the lineup's */
        }
        g_free(n);
        lu_from_kf(&g_cur, kf, 1, &base);
        lu_clear(&from);
        g_opt_alltabs = g_key_file_get_boolean(kf, "SRSDEF", "All Items As Tabs", NULL);
        g_opt_showhidden = g_key_file_get_boolean(kf, "SRSDEF", "Show Hidden Items", NULL);
        g_opt_autoscan = g_key_file_get_boolean(kf, "SRSDEF", "Check Empty Items", NULL);
    }
    lu_clear(&base);
    g_key_file_free(kf);
}

static void tabs_state_save(void)
{
    GKeyFile *kf = g_key_file_new();
    char *dir = g_path_get_dirname(g_ini);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    g_key_file_load_from_file(kf, g_ini, G_KEY_FILE_KEEP_COMMENTS, NULL);
    lu_to_kf(&g_cur, kf, 1);
    g_key_file_set_boolean(kf, "SRSDEF", "All Items As Tabs", g_opt_alltabs);
    g_key_file_set_boolean(kf, "SRSDEF", "Show Hidden Items", g_opt_showhidden);
    g_key_file_set_boolean(kf, "SRSDEF", "Check Empty Items", g_opt_autoscan);
    g_key_file_save_to_file(kf, g_ini, NULL);
    g_key_file_free(kf);
}

static void lineup_label_update(void)
{
    char s[220];
    if (!g_home || !g_home->lineup_lbl) return;
    snprintf(s, sizeof s, "Lineup: %s%s", g_lineup, lineup_changed() ? " (changed)" : "");
    gtk_label_set_text(GTK_LABEL(g_home->lineup_lbl), s);
    gtk_widget_set_tooltip_text(g_home->lineup_lbl, lineup_changed()
        ? "The tabs differ from the saved lineup. Menu > Lineup: Save to this lineup, Save as new lineup, or pick a lineup to go back."
        : "Which tabs each page shows. Menu > Lineup switches, saves, exports and imports lineups.");
}

/* ---- which items can show something here ---- */
static const char *item_hidden(const char *id)
{
    const srs_item *s;
    if (strchr(id, ':')) return NULL;
    if (!(s = srs_section_find(id))) return "unknown item";
    if (s->bucket == B_WINONLY) return "Windows-only";
    if (s->bucket == B_ABSENT) return "not present";
    if (g_empty && g_hash_table_contains(g_empty, id)) return "nothing to show on this PC";
    return NULL;
}

static char *empty_cache_path(void) { return g_build_filename(g_get_user_cache_dir(), "srs", "empty-items.txt", NULL); }

/* "nothing to show": no value in the output except grey notes, red errors, headings, "-" / "none" */
typedef struct { int vals, line0; GString *first; } emptyctx;
static void em_text(srs_out *o, int c, int b, int m, const char *s)
{
    emptyctx *e = o->ctx;
    char *t = g_strstrip(g_strdup(s));
    (void)m;
    if (e->line0) g_string_append(e->first, s);
    if (*t && !b && c != C_LABEL && c != C_GRAY && c != C_RED && strcmp(t, "-") && g_ascii_strcasecmp(t, "none") &&
        g_ascii_strcasecmp(t, "(none)") && g_ascii_strcasecmp(t, "n/a") && strcmp(t, "☐") && strcmp(t, "☑") &&
        !(e->line0 && g_str_has_prefix(e->first->str, "srs-gui - ")))
        e->vals++;
    g_free(t);
}
static void em_tab(srs_out *o) { (void)o; }
static void em_par(srs_out *o) { ((emptyctx *)o->ctx)->line0 = 0; }
static void em_tabs(srs_out *o, const int *px, int n) { (void)o; (void)px; (void)n; }

/* srs-gui --gui-scan-empty: one line per item that has nothing to show ("id"); run as a child */
static int scan_empty_main(void)
{
    int i, n = srs_section_count();
    for (i = 0; i < n; i++) {
        const srs_item *s = srs_section_at(i);
        emptyctx e = { 0, 1, NULL };
        srs_out o;
        if (!s || !s->id || !s->fn || s->bucket != B_LINUX) continue;
        /* never the network items or Ping: they look at (or send to) the network, and this check must
           not reach the network, whether the user started it or the daily check did */
        if (!strcmp(s->id, "ping") || (s->group && !strcmp(s->group, "network"))) continue;
        if (s->needs_root && !so_root_file("collected.txt")) continue;   /* empty only for want of root data: keep it (and its Run as root button) */
        e.first = g_string_new(NULL);
        memset(&o, 0, sizeof o);
        o.text = em_text; o.tab = em_tab; o.par = em_par; o.tabs = em_tabs;
        o.ctx = &e;
        o.gui = 1;
        srs_section_render(s, &o);
        if (!e.vals) printf("%s\n", s->id);
        fflush(stdout);
        g_string_free(e.first, TRUE);
    }
    return 0;
}

static void empty_load(void)
{
    char *p = empty_cache_path(), *txt = NULL, **v, **q;
    if (!g_empty) g_empty = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (g_file_get_contents(p, &txt, NULL, NULL)) {
        g_hash_table_remove_all(g_empty);
        v = g_strsplit(txt, "\n", -1);
        for (q = v; *q; q++) if (**q && **q != '#') g_hash_table_add(g_empty, g_strdup(g_strstrip(*q)));
        g_strfreev(v);
    }
    g_free(txt);
    g_free(p);
}

static void page_fill(int cat);
static void alllbl_update(int cat);

static void scan_done(GPid pid, gint status, gpointer d)
{
    char *out = d;
    (void)status;
    g_spawn_close_pid(pid);
    g_scan_running = 0;
    if (out) {
        char *p = empty_cache_path(), *dir = g_path_get_dirname(p), *txt;
        g_mkdir_with_parents(dir, 0700);
        txt = g_strdup_printf("# srs-gui %s: items with nothing to show on this PC (Options > Show hidden items lists them)\n", SRS_VERSION);
        {
            char *all = g_strconcat(txt, out, NULL);
            g_file_set_contents(p, all, -1, NULL);
            g_free(all);
        }
        g_free(txt); g_free(dir); g_free(p);
        g_free(out);
        empty_load();
    }
    if (g_home) {
        int c;
        for (c = 1; c < CAT_N; c++) {
            alllbl_update(c);
            /* pages built from "every item": drop the empty ones, except on the page in view */
            if (c != g_home->cur && (g_cur.pg[c].every || g_opt_alltabs) && !g_opt_showhidden) page_fill(c);
        }
    }
}

typedef struct { GString *acc; GIOChannel *ch; int exited; GPid pid; guint watch, timer; } scanjob;
static void scan_finish(scanjob *j)
{
    char *out = g_string_free(j->acc, FALSE);
    if (j->timer) g_source_remove(j->timer);
    g_io_channel_unref(j->ch);
    scan_done(j->pid, 0, out);
    g_free(j);
}
/* a section that hangs must not keep the scan "running" for the whole session: stop our own child
   after two minutes and keep what it found (the rest counts as not empty) */
static gboolean scan_timeout(gpointer d)
{
    scanjob *j = d;
    j->timer = 0;
    kill(j->pid, SIGTERM);
    if (j->watch) g_source_remove(j->watch);
    scan_finish(j);
    return G_SOURCE_REMOVE;
}
static gboolean scan_io(GIOChannel *ch, GIOCondition cond, gpointer d)
{
    scanjob *j = d;
    char buf[4096];
    gsize n = 0;
    GIOStatus st = G_IO_STATUS_EOF;
    if (cond & G_IO_IN) {
        st = g_io_channel_read_chars(ch, buf, sizeof buf, &n, NULL);
        if (n) g_string_append_len(j->acc, buf, (gssize)n);
        if (st == G_IO_STATUS_NORMAL || st == G_IO_STATUS_AGAIN) return G_SOURCE_CONTINUE;
    }
    scan_finish(j);   /* end of output (or an error): the child is done writing */
    return G_SOURCE_REMOVE;
}
static void scan_child(GPid pid, gint status, gpointer d) { (void)status; (void)d; g_spawn_close_pid(pid); }

/* start the scan in a child process (the GUI never waits for it); force: even with a fresh cache */
static void empty_scan(int force)
{
    char exe[1024], *p = empty_cache_path();
    ssize_t l = readlink("/proc/self/exe", exe, sizeof exe - 1);
    GStatBuf st;
    int fresh = !g_stat(p, &st) && time(NULL) - st.st_mtime < 86400;
    g_free(p);
    if (g_scan_running || l <= 0 || (fresh && !force)) return;
    exe[l] = 0;
    {
        char *argv[] = { exe, (char *)"--gui-scan-empty", NULL };
        GPid pid;
        int fd = -1;
        if (g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &pid, NULL, &fd, NULL, NULL)) {
            scanjob *j = g_new0(scanjob, 1);
            j->acc = g_string_new(NULL);
            j->pid = pid;
            j->ch = g_io_channel_unix_new(fd);
            g_io_channel_set_close_on_unref(j->ch, TRUE);
            g_io_channel_set_encoding(j->ch, NULL, NULL);
            j->watch = g_io_add_watch(j->ch, G_IO_IN | G_IO_HUP | G_IO_ERR, scan_io, j);
            j->timer = g_timeout_add_seconds(120, scan_timeout, j);
            g_child_watch_add(pid, scan_child, NULL);
            g_scan_running = 1;
        }
    }
}

/* ---- the items of a page ---- */
static GPtrArray *page_items(int cat)
{
    GPtrArray *v = g_ptr_array_new();
    int i, n = srs_section_count();
    for (i = 0; i < n; i++) {
        const srs_item *s = srs_section_at(i);
        if (s && s->id && cat_of_id(s->id) == cat) g_ptr_array_add(v, (gpointer)s);
    }
    g_ptr_array_sort(v, item_title_cmp);
    return v;
}

static void item_title(const char *id, char *out, size_t n)
{
    const srs_item *it = srs_section_find(id);
    if (it && strchr(id, ':')) snprintf(out, n, "%s %s", it->title, strchr(id, ':') + 1);
    else snprintf(out, n, "%s", it ? it->title : id);
}

static void alllbl_update(int cat)
{
    GPtrArray *v;
    guint i;
    int shown = 0, hid = 0;
    char s[80];
    if (!g_home || !g_home->alllbl[cat]) return;
    v = page_items(cat);
    for (i = 0; i < v->len; i++) { if (item_hidden(((const srs_item *)g_ptr_array_index(v, i))->id)) hid++; else shown++; }
    g_ptr_array_free(v, TRUE);
    snprintf(s, sizeof s, "All %d items", g_opt_showhidden ? shown + hid : shown);
    gtk_label_set_text(GTK_LABEL(g_home->alllbl[cat]), s);
    snprintf(s, sizeof s, "%d", hid);
    g_object_set_data_full(G_OBJECT(g_home->alllbl[cat]), "hidden", g_strdup(s), g_free);
}

/* ---- the page's tabs: from the state, back into the state ---- */
static void strip_rebuild(int cat);

/* the user changed a page's tabs: the kept (not temporary) tabs, in order, become its list */
static void tabs_changed(int cat)
{
    GtkNotebook *nb;
    int i, n;
    if (g_tabs_busy || !g_home || !g_home->nb[cat]) return;
    nb = GTK_NOTEBOOK(g_home->nb[cat]);
    n = gtk_notebook_get_n_pages(nb);
    if (g_cur.pg[cat].tabs) g_ptr_array_free(g_cur.pg[cat].tabs, TRUE);
    g_cur.pg[cat].tabs = g_ptr_array_new_with_free_func(g_free);
    for (i = 0; i < n; i++) {
        swin *x = g_object_get_data(G_OBJECT(gtk_notebook_get_nth_page(nb, i)), "swin");
        if (x && !x->temp) g_ptr_array_add(g_cur.pg[cat].tabs, g_strdup(x->id));
    }
    g_cur.pg[cat].every = 0;   /* an edited page is its own list from now on */
    tabs_state_save();
    lineup_label_update();
    strip_rebuild(cat);
}

static void page_fill(int cat)
{
    GtkNotebook *nb;
    GPtrArray *want = g_ptr_array_new_with_free_func(g_free);
    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
    const pagestate *ps = &g_cur.pg[cat];
    char keep[160] = "";
    swin *cur;
    guint i;
    int n;
    if (!g_home || !g_home->nb[cat]) return;
    nb = GTK_NOTEBOOK(g_home->nb[cat]);
    if (cat == CAT_BENCH) {   /* 2.3.1: every converted tool is a tab here, first and always (Stress & Burnin leads) */
        size_t k;
        for (k = 0; k < sizeof g_toolitems / sizeof g_toolitems[0]; k++)
            if (g_toolitems[k].on && !g_hash_table_contains(seen, g_toolitems[k].id)) {
                g_hash_table_add(seen, (gpointer)g_toolitems[k].id);
                g_ptr_array_add(want, g_strdup(g_toolitems[k].id));
            }
    }
    for (i = 0; ps->tabs && i < ps->tabs->len; i++) {
        const char *id = cat == CAT_BENCH ? tool_alias(g_ptr_array_index(ps->tabs, i)) : g_ptr_array_index(ps->tabs, i);
        if (g_hash_table_contains(seen, id) || !srs_section_find(id) || (!g_opt_showhidden && item_hidden(id))) continue;
        g_hash_table_add(seen, (gpointer)id);
        g_ptr_array_add(want, g_strdup(id));
    }
    if (ps->every || g_opt_alltabs) {
        GPtrArray *v = page_items(cat);
        for (i = 0; i < v->len; i++) {
            const char *id = ((const srs_item *)g_ptr_array_index(v, i))->id;
            if (cat == CAT_BENCH) id = tool_alias(id);
            if (g_hash_table_contains(seen, id) || (!g_opt_showhidden && item_hidden(id))) continue;
            g_hash_table_add(seen, (gpointer)id);
            g_ptr_array_add(want, g_strdup(id));
        }
        g_ptr_array_free(v, TRUE);
    }
    g_hash_table_destroy(seen);
    g_tabs_busy++;
    if ((cur = (swin *)g_object_get_data(G_OBJECT(gtk_notebook_get_nth_page(nb, gtk_notebook_get_current_page(nb)) ?
               (GObject *)gtk_notebook_get_nth_page(nb, gtk_notebook_get_current_page(nb)) : G_OBJECT(nb)), "swin")))
        snprintf(keep, sizeof keep, "%s", cur->id);
    /* keep the open tabs that stay (no re-reading), drop the others, add the new ones in order */
    n = gtk_notebook_get_n_pages(nb);
    while (n-- > 0) {
        GtkWidget *pg = gtk_notebook_get_nth_page(nb, n);
        swin *x = g_object_get_data(G_OBJECT(pg), "swin");
        gboolean stay = FALSE;
        for (i = 0; x && i < want->len; i++) if (!strcmp(g_ptr_array_index(want, i), x->id)) stay = TRUE;
        if (!stay && !(x && x->temp)) gtk_widget_destroy(pg);
    }
    for (i = 0; i < want->len; i++) {
        int at = -1;
        swin *x = tab_find(cat, g_ptr_array_index(want, i), &at);
        if (!x) x = tab_add_at(cat, g_ptr_array_index(want, i), 0, 0, (int)i);
        else {
            if (x->temp) { x->temp = 0; tab_label_new(x); }
            gtk_notebook_reorder_child(nb, x->page, (int)i);
        }
    }
    if (*keep) { int at; if (tab_find(cat, keep, &at)) gtk_notebook_set_current_page(nb, at); }
    g_ptr_array_free(want, TRUE);
    g_tabs_busy--;
    strip_rebuild(cat);
}

static void pages_fill_all(void)
{
    int c;
    for (c = 1; c < CAT_N; c++) page_fill(c);
}

/* ---- tab-bar layout: one scrolling row, wrapped rows (a strip of buttons), or a side list ---- */
static void on_strip_btn(GtkToggleButton *b, gpointer d)
{
    swin *t = d;
    int at;
    if (g_tabs_busy) return;
    if (!gtk_toggle_button_get_active(b)) {   /* the tab in view stays pressed */
        int cur;
        if (tab_find(t->cat, t->id, &at) && at == (cur = gtk_notebook_get_current_page(GTK_NOTEBOOK(g_home->nb[t->cat])))) {
            g_tabs_busy++;
            gtk_toggle_button_set_active(b, TRUE);
            g_tabs_busy--;
        }
        return;
    }
    if (tab_find(t->cat, t->id, &at)) gtk_notebook_set_current_page(GTK_NOTEBOOK(g_home->nb[t->cat]), at);
}

static void tab_menu(swin *t, GdkEvent *ev);
static gboolean on_tab_press(GtkWidget *w, GdkEventButton *ev, gpointer d)
{
    (void)w;
    if (ev->type == GDK_BUTTON_PRESS && ev->button == 3) { tab_menu(d, (GdkEvent *)ev); return TRUE; }
    if (ev->type == GDK_BUTTON_PRESS && ev->button == 2) { gtk_widget_destroy(((swin *)d)->page); return TRUE; }   /* middle click: close */
    return FALSE;
}

/* drag a strip button onto another to move the tab there */
static const GtkTargetEntry g_tab_target[] = { { (char *)"application/x-srs-tab", GTK_TARGET_SAME_APP, 0 } };
static void on_strip_drag_get(GtkWidget *w, GdkDragContext *c, GtkSelectionData *sd, guint info, guint tm, gpointer d)
{
    swin *t = d;
    (void)w; (void)c; (void)info; (void)tm;
    gtk_selection_data_set(sd, gtk_selection_data_get_target(sd), 8, (const guchar *)t->id, (gint)strlen(t->id));
}
static void on_strip_drop(GtkWidget *w, GdkDragContext *c, int x, int y, GtkSelectionData *sd, guint info, guint tm, gpointer d)
{
    swin *to = d, *from;
    char id[160];
    int len = gtk_selection_data_get_length(sd), at;
    (void)w; (void)x; (void)y; (void)info;
    if (len <= 0 || len >= (int)sizeof id) { gtk_drag_finish(c, FALSE, FALSE, tm); return; }
    memcpy(id, gtk_selection_data_get_data(sd), (size_t)len);
    id[len] = 0;
    if ((from = tab_find(to->cat, id, NULL)) && from != to && tab_find(to->cat, to->id, &at)) {
        gtk_notebook_reorder_child(GTK_NOTEBOOK(g_home->nb[to->cat]), from->page, at);   /* page-reordered -> kept */
    }
    gtk_drag_finish(c, TRUE, FALSE, tm);
}

static void strip_rebuild(int cat)
{
    GtkNotebook *nb;
    GList *ch, *l;
    int i, n, cur, took = 0;
    if (g_tabs_busy || !g_home || !g_home->strip[cat]) return;
    nb = GTK_NOTEBOOK(g_home->nb[cat]);
    ch = gtk_container_get_children(GTK_CONTAINER(g_home->strip[cat]));
    for (l = ch; l; l = l->next) {
        GtkWidget *b = gtk_bin_get_child(GTK_BIN(l->data));
        if (b == g_home->ctl[cat]) { g_object_ref(b); gtk_container_remove(GTK_CONTAINER(l->data), b); took = 1; }
        gtk_widget_destroy(l->data);
    }
    g_list_free(ch);
    if (g_cur.layout != LAY_WRAP) {
        if (took) {   /* back to the notebook's tab row */
            gtk_notebook_set_action_widget(nb, g_home->ctl[cat], GTK_PACK_END);
            gtk_widget_show_all(g_home->ctl[cat]);
            g_object_unref(g_home->ctl[cat]);
        }
        return;
    }
    g_tabs_busy++;
    n = gtk_notebook_get_n_pages(nb);
    cur = gtk_notebook_get_current_page(nb);
    for (i = 0; i < n; i++) {
        swin *t = g_object_get_data(G_OBJECT(gtk_notebook_get_nth_page(nb, i)), "swin");
        GtkWidget *b, *lab;
        char title[200], *mk;
        if (!t) continue;
        item_title(t->id, title, sizeof title);
        b = gtk_toggle_button_new();
        mk = g_markup_printf_escaped(t->temp ? "<i>%s</i>" : "%s", title);
        lab = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(lab), mk);
        g_free(mk);
        gtk_container_add(GTK_CONTAINER(b), lab);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), i == cur);
        gtk_button_set_relief(GTK_BUTTON(b), i == cur ? GTK_RELIEF_NORMAL : GTK_RELIEF_NONE);
        snprintf(title, sizeof title, "[%s]%s - right-click for Close, Replace with..., Keep; drag to move", t->id,
                 t->temp ? " (temporary)" : "");
        gtk_widget_set_tooltip_text(b, title);
        g_signal_connect(b, "toggled", G_CALLBACK(on_strip_btn), t);
        g_signal_connect(b, "button-press-event", G_CALLBACK(on_tab_press), t);
        gtk_drag_source_set(b, GDK_BUTTON1_MASK, g_tab_target, 1, GDK_ACTION_MOVE);
        gtk_drag_dest_set(b, GTK_DEST_DEFAULT_ALL, g_tab_target, 1, GDK_ACTION_MOVE);
        g_signal_connect(b, "drag-data-get", G_CALLBACK(on_strip_drag_get), t);
        g_signal_connect(b, "drag-data-received", G_CALLBACK(on_strip_drop), t);
        gtk_flow_box_insert(GTK_FLOW_BOX(g_home->strip[cat]), b, -1);
        gtk_widget_show_all(gtk_widget_get_parent(b));   /* the strip is no-show-all: show each child */
    }
    if (g_home->ctl[cat]) {
        GtkWidget *c = g_home->ctl[cat];
        if (!took && gtk_widget_get_parent(c) == GTK_WIDGET(nb)) {   /* still the notebook's action widget */
            g_object_ref(c);
            gtk_notebook_set_action_widget(nb, NULL, GTK_PACK_END);
            took = 1;
        }
        if (!gtk_widget_get_parent(c)) {
            gtk_flow_box_insert(GTK_FLOW_BOX(g_home->strip[cat]), c, -1);
            gtk_widget_show_all(gtk_widget_get_parent(c));
            if (took) g_object_unref(c);
        }
    }
    g_tabs_busy--;
}

static void layout_apply(void)
{
    int c;
    if (!g_home) return;
    for (c = 1; c < CAT_N; c++) {
        GtkNotebook *nb = GTK_NOTEBOOK(g_home->nb[c]);
        GtkWidget *ctl = g_home->ctl[c];
        if (!nb) continue;
        gtk_notebook_set_tab_pos(nb, g_cur.layout == LAY_SIDE ? GTK_POS_LEFT : GTK_POS_TOP);
        gtk_notebook_set_show_tabs(nb, g_cur.layout != LAY_WRAP);
        gtk_widget_set_visible(g_home->strip[c], g_cur.layout == LAY_WRAP);
        if (g_cur.layout != LAY_WRAP && ctl && gtk_widget_get_parent(ctl) != GTK_WIDGET(nb)) {
            if (gtk_widget_get_parent(ctl)) {
                GtkWidget *par = gtk_widget_get_parent(ctl);
                g_object_ref(ctl);
                gtk_container_remove(GTK_CONTAINER(par), ctl);
                gtk_notebook_set_action_widget(nb, ctl, GTK_PACK_END);
                g_object_unref(ctl);
            } else gtk_notebook_set_action_widget(nb, ctl, GTK_PACK_END);
            gtk_widget_show_all(ctl);
        }
        strip_rebuild(c);
    }
}

static void on_nb_changed(GtkNotebook *nb, GtkWidget *child, guint n, gpointer d)
{
    int cat = GPOINTER_TO_INT(d);
    swin *t = child ? g_object_get_data(G_OBJECT(child), "swin") : NULL;
    (void)nb; (void)n;
    if (!g_tabs_busy && t && !t->temp) tabs_changed(cat);
    strip_rebuild(cat);
}
static void on_nb_added(GtkNotebook *nb, GtkWidget *child, guint n, gpointer d) { (void)nb; (void)child; (void)n; strip_rebuild(GPOINTER_TO_INT(d)); }
static gboolean strip_idle(gpointer d) { strip_rebuild(GPOINTER_TO_INT(d)); return G_SOURCE_REMOVE; }
static void on_nb_switch(GtkNotebook *nb, GtkWidget *child, guint n, gpointer d)
{
    int i, np = gtk_notebook_get_n_pages(nb);
    (void)child;
    if (getenv("SRS_TEST_TAB_LOG")) fprintf(stderr, "SRS-TEST: tab %d of page %d\n", (int)n, GPOINTER_TO_INT(d));   /* tests */
    for (i = 0; i < np; i++) {   /* the close button: on the tab in view, and on temporary tabs */
        swin *t = g_object_get_data(G_OBJECT(gtk_notebook_get_nth_page(nb, i)), "swin");
        GtkWidget *x = t && t->tablbl ? g_object_get_data(G_OBJECT(t->tablbl), "x") : NULL;
        if (x) gtk_widget_set_visible(x, t->temp || i == (int)n);
    }
    if (g_home && g_cur.layout == LAY_WRAP) g_idle_add_full(G_PRIORITY_HIGH_IDLE, strip_idle, d, NULL);
}

/* ---- the item list: jump to / open (All N items), add (+), replace (right-click) ---- */
enum { PK_JUMP, PK_ADD, PK_REPLACE };
typedef struct { int mode, cat; char target[160]; GtkWidget *pop, *entry, *list, *foot; } picker;

static gboolean destroy_idle(gpointer w) { gtk_widget_destroy(w); return G_SOURCE_REMOVE; }
static void on_pop_closed(GtkPopover *pop, gpointer d) { (void)d; g_idle_add(destroy_idle, pop); }

static void picker_act(picker *p, const char *id0)
{
    int at;
    swin *x;
    GtkNotebook *nb = GTK_NOTEBOOK(g_home->nb[p->cat]);
    char id[160];
    snprintf(id, sizeof id, "%s", id0);   /* the row (and its id) goes away with the popover */
    gtk_popover_popdown(GTK_POPOVER(p->pop));
    if (p->mode == PK_JUMP) {
        if ((x = tab_find(p->cat, id, &at))) gtk_notebook_set_current_page(nb, at);
        else tab_add_at(p->cat, id, 1, 1, -1);
    } else if (p->mode == PK_ADD) {
        tab_add_at(p->cat, id, 1, 0, -1);
        tabs_changed(p->cat);
    } else {
        swin *old = tab_find(p->cat, p->target, &at);
        if (!old || !strcmp(id, p->target)) return;
        if ((x = tab_find(p->cat, id, NULL))) gtk_widget_destroy(x->page);
        old = tab_find(p->cat, p->target, &at);
        g_tabs_busy++;
        x = tab_add_at(p->cat, id, 1, old ? old->temp : 0, at);
        if (old) gtk_widget_destroy(old->page);
        g_tabs_busy--;
        tabs_changed(p->cat);
        if (x) { int a2; if (tab_find(p->cat, id, &a2)) gtk_notebook_set_current_page(nb, a2); }
    }
}

static void on_pick_row(GtkListBox *lb, GtkListBoxRow *row, gpointer d)
{
    (void)lb;
    if (row) picker_act(d, g_object_get_data(G_OBJECT(row), "id"));
}

static gboolean pick_filter(GtkListBoxRow *row, gpointer d)
{
    picker *p = d;
    const char *hay = g_object_get_data(G_OBJECT(row), "hay");
    char *low = g_utf8_strdown(gtk_entry_get_text(GTK_ENTRY(p->entry)), -1), **w, **q;
    gboolean ok = TRUE;
    w = g_strsplit(low, " ", -1);
    for (q = w; *q; q++) if (**q && (!hay || !strstr(hay, *q))) { ok = FALSE; break; }
    g_strfreev(w);
    g_free(low);
    return ok;
}

static void on_pick_search(GtkSearchEntry *e, gpointer d) { (void)e; gtk_list_box_invalidate_filter(GTK_LIST_BOX(((picker *)d)->list)); }

static void pick_first(GtkWidget *row, gpointer d)
{
    GtkWidget **first = d;
    if (!*first && gtk_widget_get_child_visible(row)) *first = row;
}
static void on_pick_enter(GtkEntry *e, gpointer d)
{
    picker *p = d;
    GtkWidget *first = NULL;
    (void)e;
    gtk_container_foreach(GTK_CONTAINER(p->list), pick_first, &first);
    if (first) picker_act(p, g_object_get_data(G_OBJECT(first), "id"));
}

static void pick_row_add(picker *p, const char *id, const char *title)
{
    const char *why = item_hidden(id);
    int at;
    int open = tab_find(p->cat, id, &at) != NULL;
    GtkWidget *row, *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8), *l;
    char *mk, *hay, *h1;
    if (why && !g_opt_showhidden) return;
    mk = g_markup_printf_escaped("%s  <small>[%s]</small>", title, id);
    l = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(l), mk);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    g_free(mk);
    gtk_box_pack_start(GTK_BOX(box), l, TRUE, TRUE, 0);
    if (why || open) {
        GtkWidget *r = label_cls(why ? why : "open", "dim-label srs-cap", 1);
        gtk_box_pack_end(GTK_BOX(box), r, FALSE, FALSE, 0);
    }
    if (why) gtk_style_context_add_class(gtk_widget_get_style_context(l), "dim-label");
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);
    gtk_widget_set_margin_top(box, 3);
    gtk_widget_set_margin_bottom(box, 3);
    row = gtk_list_box_row_new();
    gtk_container_add(GTK_CONTAINER(row), box);
    g_object_set_data_full(G_OBJECT(row), "id", g_strdup(id), g_free);
    h1 = g_strdup_printf("%s %s", title, id);
    hay = g_utf8_strdown(h1, -1);
    g_free(h1);
    g_object_set_data_full(G_OBJECT(row), "hay", hay, g_free);
    gtk_list_box_insert(GTK_LIST_BOX(p->list), row, -1);
}

static void picker_free(gpointer d) { g_free(d); }

static void picker_open(GtkWidget *rel, int cat, int mode, const char *target)
{
    picker *p = g_new0(picker, 1);
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6), *sw, *hd;
    GPtrArray *v = page_items(cat);
    guint i;
    int hid = 0;
    char s[300];
    p->mode = mode;
    p->cat = cat;
    snprintf(p->target, sizeof p->target, "%s", target ? target : "");
    p->pop = gtk_popover_new(rel);
    g_object_set_data_full(G_OBJECT(p->pop), "picker", p, picker_free);
    gtk_container_set_border_width(GTK_CONTAINER(vb), 8);
    if (mode == PK_JUMP) snprintf(s, sizeof s, "Every item of %s. Pick one to go to its tab, or to open it as a temporary tab.", g_cats[cat].title);
    else if (mode == PK_ADD) snprintf(s, sizeof s, "Add a tab to %s. It stays on this page (and in this lineup once you save it).", g_cats[cat].title);
    else { char t[200]; item_title(target, t, sizeof t); snprintf(s, sizeof s, "Replace the %s tab with:", t); }
    hd = label_cls(s, "dim-label srs-cap", 0);
    gtk_label_set_line_wrap(GTK_LABEL(hd), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(hd), 50);
    gtk_box_pack_start(GTK_BOX(vb), hd, FALSE, FALSE, 0);
    p->entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(p->entry), "Type to filter, Enter opens the first");
    g_signal_connect(p->entry, "search-changed", G_CALLBACK(on_pick_search), p);
    g_signal_connect(p->entry, "activate", G_CALLBACK(on_pick_enter), p);
    gtk_box_pack_start(GTK_BOX(vb), p->entry, FALSE, FALSE, 0);
    p->list = gtk_list_box_new();
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(p->list), TRUE);
    gtk_list_box_set_filter_func(GTK_LIST_BOX(p->list), pick_filter, p, NULL);
    g_signal_connect(p->list, "row-activated", G_CALLBACK(on_pick_row), p);
    for (i = 0; i < v->len; i++) {
        const srs_item *it = g_ptr_array_index(v, i);
        if (item_hidden(it->id)) hid++;
        pick_row_add(p, it->id, it->title);
    }
    if (cat == CAT_CPU) {   /* SIV's CPU-n and Cache-n buttons, after the items */
        int c2;
        for (c2 = 0; c2 < burn_ncpu(); c2++) {
            char id[40], t[60];
            snprintf(id, sizeof id, "processor:%d", c2); snprintf(t, sizeof t, "Processor CPU-%d", c2); pick_row_add(p, id, t);
        }
        for (c2 = 0; c2 < burn_ncpu(); c2++) {
            char id[40], t[60];
            snprintf(id, sizeof id, "cache:%d", c2); snprintf(t, sizeof t, "Cache levels CPU-%d", c2); pick_row_add(p, id, t);
        }
    }
    g_ptr_array_free(v, TRUE);
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sw), 360);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(sw), 420);
    gtk_container_add(GTK_CONTAINER(sw), p->list);
    gtk_box_pack_start(GTK_BOX(vb), sw, TRUE, TRUE, 0);
    if (hid) {
        snprintf(s, sizeof s, g_opt_showhidden ? "%d item%s greyed: nothing to show on this PC (Windows-only, not present or empty)."
                                               : "%d item%s hidden: nothing to show on this PC. Options > Show hidden items lists them.",
                 hid, hid == 1 ? "" : "s");
        p->foot = label_cls(s, "dim-label srs-cap", 0);
        gtk_label_set_line_wrap(GTK_LABEL(p->foot), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(p->foot), 50);
        gtk_box_pack_start(GTK_BOX(vb), p->foot, FALSE, FALSE, 0);
    }
    gtk_container_add(GTK_CONTAINER(p->pop), vb);
    gtk_widget_show_all(vb);
    gtk_popover_popup(GTK_POPOVER(p->pop));
    gtk_widget_grab_focus(p->entry);
    g_signal_connect(p->pop, "closed", G_CALLBACK(on_pop_closed), NULL);
}

static void on_all_btn(GtkButton *b, gpointer d) { picker_open(GTK_WIDGET(b), GPOINTER_TO_INT(d), PK_JUMP, NULL); }
static void on_add_btn(GtkButton *b, gpointer d) { picker_open(GTK_WIDGET(b), GPOINTER_TO_INT(d), PK_ADD, NULL); }

/* ---- a tab's label (and right-click menu) ---- */
static void on_tab_x(GtkButton *b, gpointer d) { (void)b; gtk_widget_destroy(((swin *)d)->page); }

static GtkWidget *tab_label_new(swin *t)
{
    GtkWidget *eb = gtk_event_box_new(), *tl = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4), *x;
    char title[200], s[400], *mk;
    item_title(t->id, title, sizeof title);
    t->tabtxt = gtk_label_new(NULL);
    mk = g_markup_printf_escaped(t->temp ? "<i>%s</i>" : "%s", title);
    gtk_label_set_markup(GTK_LABEL(t->tabtxt), mk);
    g_free(mk);
    gtk_box_pack_start(GTK_BOX(tl), t->tabtxt, FALSE, FALSE, 0);
    snprintf(s, sizeof s, "%s  [%s]%s\nRight-click: Keep, Replace with..., Move, Close. Drag to move.", title, t->id,
             t->temp ? "\nTemporary tab: not kept when SRS closes (right-click > Keep this tab)" : "");
    gtk_widget_set_tooltip_text(eb, s);
    x = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(x), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(x, FALSE);
    gtk_widget_set_tooltip_text(x, "Close this tab (Ctrl+W, or middle-click the tab). The item stays in All items.");
    g_signal_connect(x, "clicked", G_CALLBACK(on_tab_x), t);
    gtk_box_pack_start(GTK_BOX(tl), x, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(x, TRUE);   /* shown on the tab in view and on temporary tabs (tabs_x_update) */
    g_object_set_data(G_OBJECT(eb), "x", x);
    gtk_container_add(GTK_CONTAINER(eb), tl);
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(eb), FALSE);
    g_signal_connect(eb, "button-press-event", G_CALLBACK(on_tab_press), t);
    gtk_widget_show_all(eb);
    gtk_notebook_set_tab_label(GTK_NOTEBOOK(g_home->nb[t->cat]), t->page, eb);
    t->tablbl = eb;
    gtk_widget_set_visible(x, t->temp || gtk_notebook_page_num(GTK_NOTEBOOK(g_home->nb[t->cat]), t->page) ==
                                         gtk_notebook_get_current_page(GTK_NOTEBOOK(g_home->nb[t->cat])));
    strip_rebuild(t->cat);
    return eb;
}

static void tabs_dialog(int cat);
static void lineup_menu(GtkWidget *m);

static void on_tabm(GtkMenuItem *mi, gpointer d)
{
    const char *act = g_object_get_data(G_OBJECT(mi), "act");
    swin *t = d;
    GtkNotebook *nb = GTK_NOTEBOOK(g_home->nb[t->cat]);
    int at = gtk_notebook_page_num(nb, t->page), n = gtk_notebook_get_n_pages(nb);
    if (!strcmp(act, "keep")) { t->temp = 0; tab_label_new(t); tabs_changed(t->cat); }
    else if (!strcmp(act, "close")) gtk_widget_destroy(t->page);
    else if (!strcmp(act, "left") && at > 0) gtk_notebook_reorder_child(nb, t->page, at - 1);
    else if (!strcmp(act, "right") && at < n - 1) gtk_notebook_reorder_child(nb, t->page, at + 1);
    else if (!strcmp(act, "replace")) picker_open(t->tablbl && gtk_widget_get_mapped(t->tablbl) ? t->tablbl : g_home->ctl[t->cat], t->cat, PK_REPLACE, t->id);
    else if (!strcmp(act, "add")) picker_open(g_home->ctl[t->cat], t->cat, PK_ADD, NULL);
    else if (!strcmp(act, "page")) tabs_dialog(t->cat);
    else if (!strcmp(act, "reset")) {
        lineup l = { "", 0, { { 0 } } };
        if (lu_get(&l, g_lineup)) {
            lu_page_copy(&g_cur, t->cat, l.pg[t->cat].tabs, l.pg[t->cat].every);
            page_fill(t->cat);
            tabs_state_save();
            lineup_label_update();
        }
        lu_clear(&l);
    }
}

static GtkWidget *tabm_add(GtkWidget *m, const char *label, const char *act, swin *t, int sensitive)
{
    GtkWidget *it = gtk_menu_item_new_with_mnemonic(label);
    g_object_set_data(G_OBJECT(it), "act", (gpointer)act);
    g_signal_connect(it, "activate", G_CALLBACK(on_tabm), t);
    gtk_widget_set_sensitive(it, sensitive);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
    return it;
}

static void on_menu_gone(GtkMenuShell *m, gpointer d) { (void)d; g_idle_add(destroy_idle, m); }   /* after the item's "activate" */

static void tab_menu(swin *t, GdkEvent *ev)
{
    GtkWidget *m = gtk_menu_new(), *sub, *it;
    GtkNotebook *nb = GTK_NOTEBOOK(g_home->nb[t->cat]);
    int at = gtk_notebook_page_num(nb, t->page), n = gtk_notebook_get_n_pages(nb);
    /* MNEMONIC-MENU: tab-menu */
    tabm_add(m, t->temp ? "_Keep this tab" : "_Keep this tab (it is kept)", "keep", t, t->temp);
    tabm_add(m, "_Replace with...", "replace", t, TRUE);
    tabm_add(m, "Move _left", "left", t, at > 0);
    tabm_add(m, "Move ri_ght", "right", t, at < n - 1);
    tabm_add(m, "_Close tab (Ctrl+W)", "close", t, TRUE);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    tabm_add(m, "_Add a tab...", "add", t, TRUE);
    tabm_add(m, "_Tabs on this page...", "page", t, TRUE);
    tabm_add(m, "Reset this page's tabs to the line_up", "reset", t, TRUE);
    /* MNEMONIC-END */
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    sub = gtk_menu_new();
    lineup_menu(sub);
    it = gtk_menu_item_new_with_mnemonic("Li_neup");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(it), sub);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
    gtk_widget_show_all(m);
    g_signal_connect(m, "deactivate", G_CALLBACK(on_menu_gone), NULL);   /* GtkMenu is not freed by itself */
    gtk_menu_popup_at_pointer(GTK_MENU(m), ev);
}

/* ---- lineups: switch, save, export, import ---- */
static void lineup_switch(const char *name)
{
    lineup l = { "", 0, { { 0 } } };
    if (!lu_get(&l, name)) { lu_clear(&l); return; }
    snprintf(g_lineup, sizeof g_lineup, "%s", name);
    lu_copy(&g_cur, &l);
    lu_clear(&l);
    tabs_state_save();
    if (g_home) {
        layout_apply();
        pages_fill_all();
        lineup_label_update();
    }
}

static char *ask_name(const char *title, const char *text, const char *initial)
{
    GtkWidget *d = gtk_dialog_new_with_buttons(title, g_main ? GTK_WINDOW(g_main->win) : NULL, GTK_DIALOG_MODAL,
                                               "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *ca = gtk_dialog_get_content_area(GTK_DIALOG(d)), *e = gtk_entry_new(), *l = label_cls(text, NULL, 0);
    char *r = NULL;
    gtk_container_set_border_width(GTK_CONTAINER(ca), 12);
    gtk_box_set_spacing(GTK_BOX(ca), 8);
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    gtk_box_pack_start(GTK_BOX(ca), l, FALSE, FALSE, 0);
    gtk_entry_set_text(GTK_ENTRY(e), initial ? initial : "");
    gtk_entry_set_activates_default(GTK_ENTRY(e), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
    gtk_box_pack_start(GTK_BOX(ca), e, FALSE, FALSE, 0);
    gtk_widget_show_all(d);
    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
        char *n = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(e))));
        if (*n && !lu_builtin_index(n) && strlen(n) < 100) { r = n; break; }
        g_free(n);
        gtk_label_set_text(GTK_LABEL(l), "Please type a name (not Basic, Enthusiast or Expert; up to 100 characters).");
    }
    gtk_widget_destroy(d);
    return r;
}

static void msg(GtkMessageType t, const char *text)
{
    GtkWidget *m = gtk_message_dialog_new(g_main ? GTK_WINDOW(g_main->win) : NULL, GTK_DIALOG_MODAL, t, GTK_BUTTONS_OK, "%s", text);
    gtk_dialog_run(GTK_DIALOG(m));
    gtk_widget_destroy(m);
}

static void lineup_save_as(const char *name)
{
    char *p = lu_path(name);
    snprintf(g_cur.name, sizeof g_cur.name, "%s", name);
    if (lu_save_file(&g_cur, p)) { snprintf(g_lineup, sizeof g_lineup, "%s", name); tabs_state_save(); }
    else msg(GTK_MESSAGE_WARNING, "Could not write the lineup file.");
    g_free(p);
    lineup_label_update();
}

static void on_lum(GtkMenuItem *mi, gpointer d)
{
    const char *act = g_object_get_data(G_OBJECT(mi), "act");
    (void)d;
    if (!strcmp(act, "pick")) {
        if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(mi))) return;
        lineup_switch(g_object_get_data(G_OBJECT(mi), "name"));
    } else if (!strcmp(act, "save")) {
        lineup_save_as(g_lineup);
    } else if (!strcmp(act, "saveas")) {
        char *n = ask_name("Save as new lineup", "A name for this lineup (which tabs each page shows, their order and the tab bar):", "");
        if (n) {
            char *p = lu_path(n);
            int go = 1;
            if (g_file_test(p, G_FILE_TEST_EXISTS)) {
                GtkWidget *q = gtk_message_dialog_new(g_main ? GTK_WINDOW(g_main->win) : NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                                      GTK_BUTTONS_YES_NO, "A lineup named \"%s\" exists. Replace it?", n);
                go = gtk_dialog_run(GTK_DIALOG(q)) == GTK_RESPONSE_YES;
                gtk_widget_destroy(q);
            }
            if (go) lineup_save_as(n);
            g_free(p);
            g_free(n);
        }
    } else if (!strcmp(act, "export")) {
        GtkWidget *fc = gtk_file_chooser_dialog_new("Export lineup", g_main ? GTK_WINDOW(g_main->win) : NULL, GTK_FILE_CHOOSER_ACTION_SAVE,
                                                    "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
        char fn[200];
        snprintf(fn, sizeof fn, "%s.srslineup", g_lineup);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(fc), fn);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(fc), TRUE);
        if (gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
            char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
            snprintf(g_cur.name, sizeof g_cur.name, "%s", g_lineup);
            if (!lu_save_file(&g_cur, f)) msg(GTK_MESSAGE_WARNING, "Could not write the file.");
            g_free(f);
        }
        gtk_widget_destroy(fc);
    } else if (!strcmp(act, "import")) {
        GtkWidget *fc = gtk_file_chooser_dialog_new("Import lineup", g_main ? GTK_WINDOW(g_main->win) : NULL, GTK_FILE_CHOOSER_ACTION_OPEN,
                                                    "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
        GtkFileFilter *ff = gtk_file_filter_new();
        gtk_file_filter_set_name(ff, "SRS lineups (*.srslineup)");
        gtk_file_filter_add_pattern(ff, "*.srslineup");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), ff);
        if (gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
            char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
            lineup l = { "", 0, { { 0 } } };
            if (!lu_load_file(&l, f)) msg(GTK_MESSAGE_WARNING, "That file is not an SRS lineup.");
            else {
                char base[128], *p;
                int k = 2;
                snprintf(base, sizeof base, "%s", l.name);
                if (lu_builtin_index(base)) snprintf(l.name, sizeof l.name, "%s (imported)", base);
                snprintf(base, sizeof base, "%s", l.name);
                while (p = lu_path(l.name), g_file_test(p, G_FILE_TEST_EXISTS)) { g_free(p); snprintf(l.name, sizeof l.name, "%.100s %d", base, k++); }
                if (lu_save_file(&l, p)) lineup_switch(l.name);
                g_free(p);
            }
            lu_clear(&l);
            g_free(f);
        }
        gtk_widget_destroy(fc);
    } else if (!strcmp(act, "remove")) {
        GtkWidget *q = gtk_message_dialog_new(g_main ? GTK_WINDOW(g_main->win) : NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                              GTK_BUTTONS_YES_NO, "Remove the lineup \"%s\"? The tabs on screen stay as they are; "
                                              "the lineup switches to %s.", g_lineup, g_lu_builtin[LU_DEFAULT]);
        if (gtk_dialog_run(GTK_DIALOG(q)) == GTK_RESPONSE_YES) {
            char *p = lu_path(g_lineup);
            g_unlink(p);
            g_free(p);
            snprintf(g_lineup, sizeof g_lineup, "%s", g_lu_builtin[LU_DEFAULT]);
            tabs_state_save();
            lineup_label_update();
        }
        gtk_widget_destroy(q);
    } else if (!strcmp(act, "default")) {
        lineup_switch(g_lu_builtin[LU_DEFAULT]);
    }
}

static GtkWidget *lum_add(GtkWidget *m, const char *label, const char *act, int sensitive)
{
    GtkWidget *it = gtk_menu_item_new_with_mnemonic(label);
    g_object_set_data(G_OBJECT(it), "act", (gpointer)act);
    g_signal_connect(it, "activate", G_CALLBACK(on_lum), NULL);
    gtk_widget_set_sensitive(it, sensitive);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
    return it;
}

/* the Lineup menu (header menu, Options, a tab's right-click menu) */
static void lineup_menu(GtkWidget *m)
{
    static const char *const tip[] = { NULL, "A few friendly tabs per page", "The standard tabs (as in SRS 2.0)",
                                       "Every item of every page as a tab, in a side list" };
    GSList *grp = NULL;
    GPtrArray *u = lu_user_names();
    int i, custom = !lu_builtin_index(g_lineup), changed = lineup_changed();
    for (i = 1; i <= LU_EXPERT + (int)u->len; i++) {
        const char *n = i <= LU_EXPERT ? g_lu_builtin[i] : g_ptr_array_index(u, (guint)(i - LU_EXPERT - 1));
        char lab[200];
        GtkWidget *it;
        snprintf(lab, sizeof lab, "%s%s", n, !g_ascii_strcasecmp(n, g_lineup) && changed ? "  (changed)" : "");
        it = gtk_radio_menu_item_new_with_label(grp, lab);
        grp = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(it));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), !g_ascii_strcasecmp(n, g_lineup));
        g_object_set_data_full(G_OBJECT(it), "name", g_strdup(n), g_free);
        g_object_set_data(G_OBJECT(it), "act", (gpointer)"pick");
        if (i <= LU_EXPERT) gtk_widget_set_tooltip_text(it, tip[i]);
        g_signal_connect(it, "toggled", G_CALLBACK(on_lum), NULL);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
        if (i == LU_EXPERT && u->len) gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    /* MNEMONIC-MENU: lineup-menu */
    lum_add(m, custom ? "_Save to this lineup" : "_Save to this lineup (built-in: use Save as new)", "save", custom && changed);
    lum_add(m, "Save as _new lineup...", "saveas", TRUE);
    lum_add(m, "_Export lineup...", "export", TRUE);
    lum_add(m, "_Import lineup...", "import", TRUE);
    lum_add(m, "_Remove this lineup...", "remove", custom);
    lum_add(m, "Reset to the _default lineup (Expert)", "default", TRUE);
    /* MNEMONIC-END */
    g_ptr_array_free(u, TRUE);
}

/* ---- Options: tab bar, every item as a tab, hidden items, tabs on each page ---- */
static void on_lay(GtkCheckMenuItem *mi, gpointer d)
{
    if (!gtk_check_menu_item_get_active(mi) || g_cur.layout == GPOINTER_TO_INT(d)) return;
    g_cur.layout = GPOINTER_TO_INT(d);
    tabs_state_save();
    layout_apply();
    lineup_label_update();
}

static void on_tabopt(GtkCheckMenuItem *mi, gpointer d)
{
    int on = gtk_check_menu_item_get_active(mi), c;
    if (GPOINTER_TO_INT(d) == 0) g_opt_alltabs = on; else g_opt_showhidden = on;
    tabs_state_save();
    if (g_home) {
        pages_fill_all();
        for (c = 1; c < CAT_N; c++) alllbl_update(c);
    }
    if (on && g_opt_autoscan && (g_opt_alltabs || !GPOINTER_TO_INT(d))) empty_scan(0);
}

static void on_tabs_dialog(GtkMenuItem *mi, gpointer d) { (void)mi; (void)d; tabs_dialog(g_home && g_home->cur > 0 ? g_home->cur : CAT_CPU); }
static void on_rescan(GtkMenuItem *mi, gpointer d) { (void)mi; (void)d; empty_scan(1); }
static void on_autoscan(GtkCheckMenuItem *mi, gpointer d)
{
    (void)d;
    g_opt_autoscan = gtk_check_menu_item_get_active(mi);
    tabs_state_save();
    if (g_opt_autoscan) empty_scan(0);   /* now, unless the last check is less than a day old */
}

static void tabs_options(GtkWidget *menu)
{
    /* MNEMONIC-MENU: tab-bar-layout */
    static const char *const ll[LAY_N] = { "_One row that scrolls", "_Wrapped rows (every tab visible)", "_Side list (tabs on the left)" };
    /* MNEMONIC-END */
    /* MNEMONIC-MENU: options (with build_options in gui_gtk.c) */
    GtkWidget *sub = gtk_menu_new(), *top = gtk_menu_item_new_with_mnemonic("Tab _bar"), *it;
    GSList *grp = NULL;
    int i;
    for (i = 0; i < LAY_N; i++) {
        it = gtk_radio_menu_item_new_with_mnemonic(grp, ll[i]);
        grp = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(it));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), g_cur.layout == i);
        g_signal_connect(it, "toggled", G_CALLBACK(on_lay), GINT_TO_POINTER(i));
        gtk_menu_shell_append(GTK_MENU_SHELL(sub), it);
    }
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(top), sub);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), top);
    sub = gtk_menu_new();
    lineup_menu(sub);
    top = gtk_menu_item_new_with_mnemonic("Line_up");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(top), sub);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), top);
    it = gtk_menu_item_new_with_mnemonic("Tabs on each _page...");
    gtk_widget_set_tooltip_text(it, "Which tabs each page opens with: the standard ones, ones you pick, or every item");
    g_signal_connect(it, "activate", G_CALLBACK(on_tabs_dialog), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    it = gtk_check_menu_item_new_with_mnemonic("Show e_very item as a tab (all pages)");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), g_opt_alltabs);
    g_signal_connect(it, "toggled", G_CALLBACK(on_tabopt), GINT_TO_POINTER(0));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    it = gtk_check_menu_item_new_with_mnemonic("Show hidden _items (Windows-only, not present, nothing to show)");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), g_opt_showhidden);
    g_signal_connect(it, "toggled", G_CALLBACK(on_tabopt), GINT_TO_POINTER(1));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    it = gtk_check_menu_item_new_with_mnemonic("Chec_k daily which items have nothing to show");
    gtk_widget_set_tooltip_text(it, "Off (the default): SRS checks only when you choose Check now. On: once a day, in the "
                                    "background, SRS reads every item to find the ones with nothing to show and hides them. "
                                    "The network items and Ping are never part of the check. Windows-only items and hardware "
                                    "that is not present are hidden either way.");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), g_opt_autoscan);
    g_signal_connect(it, "toggled", G_CALLBACK(on_autoscan), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    it = gtk_menu_item_new_with_mnemonic(g_scan_running ? "Check _now which items have nothing to show (running...)"
                                                         : "Check _now which items have nothing to show");
    gtk_widget_set_tooltip_text(it, "Read every item once now, in the background, and hide the ones with nothing to show on "
                                    "this PC (never the network items or Ping)");
    g_signal_connect(it, "activate", G_CALLBACK(on_rescan), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    /* MNEMONIC-END */
}

/* ---- "Tabs on each page": standard tabs / standard plus the ticked ones / every item ---- */
typedef struct { GtkWidget *dlg, *combo, *r[3], *entry, *list; pagestate work[CAT_N]; int cat, busy; } tabsdlg;

static int ps_has(const pagestate *p, const char *id)
{
    guint i;
    for (i = 0; p->tabs && i < p->tabs->len; i++) if (!strcmp(g_ptr_array_index(p->tabs, i), id)) return 1;
    return 0;
}

static void td_tick(GtkToggleButton *b, gpointer d)
{
    tabsdlg *td = d;
    const char *id = g_object_get_data(G_OBJECT(b), "id");
    pagestate *p = &td->work[td->cat];
    guint i;
    if (td->busy) return;
    if (gtk_toggle_button_get_active(b)) { if (!ps_has(p, id)) g_ptr_array_add(p->tabs, g_strdup(id)); }
    else for (i = 0; i < p->tabs->len; i++) if (!strcmp(g_ptr_array_index(p->tabs, i), id)) { g_ptr_array_remove_index(p->tabs, i); break; }
    td->busy = 1;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(td->r[1]), TRUE);
    td->busy = 0;
}

static void td_fill(tabsdlg *td)
{
    GList *ch = gtk_container_get_children(GTK_CONTAINER(td->list)), *l;
    GPtrArray *v = page_items(td->cat);
    pagestate *p = &td->work[td->cat];
    guint i;
    for (l = ch; l; l = l->next) gtk_widget_destroy(l->data);
    g_list_free(ch);
    td->busy = 1;
    for (i = 0; i < v->len; i++) {
        const srs_item *it = g_ptr_array_index(v, i);
        const char *why = item_hidden(it->id);
        char lab[300], *h1;
        GtkWidget *cb, *row;
        snprintf(lab, sizeof lab, "%s  [%s]%s%s", it->title, it->id, why ? "  - " : "", why ? why : "");
        cb = gtk_check_button_new_with_label(lab);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb), ps_has(p, it->id));
        if (why) gtk_style_context_add_class(gtk_widget_get_style_context(cb), "dim-label");
        g_object_set_data_full(G_OBJECT(cb), "id", g_strdup(it->id), g_free);
        g_signal_connect(cb, "toggled", G_CALLBACK(td_tick), td);
        row = gtk_list_box_row_new();
        gtk_container_add(GTK_CONTAINER(row), cb);
        h1 = g_utf8_strdown(lab, -1);
        g_object_set_data_full(G_OBJECT(row), "hay", h1, g_free);
        gtk_list_box_insert(GTK_LIST_BOX(td->list), row, -1);
    }
    g_ptr_array_free(v, TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(td->r[p->every ? 2 : 1]), TRUE);
    {   /* the standard tabs exactly: say so */
        int std = !p->every && p->tabs->len > 0;
        for (i = 0; std && g_cats[td->cat].tabs[i]; i++) if (i >= p->tabs->len || strcmp(g_ptr_array_index(p->tabs, i), g_cats[td->cat].tabs[i])) std = 0;
        if (std && i == p->tabs->len) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(td->r[0]), TRUE);
    }
    gtk_widget_show_all(td->list);
    td->busy = 0;
}

static void td_radio(GtkToggleButton *b, gpointer d)
{
    tabsdlg *td = d;
    pagestate *p = &td->work[td->cat];
    int which = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "which"));
    if (td->busy || !gtk_toggle_button_get_active(b)) return;
    if (which == 0) {
        int i;
        g_ptr_array_set_size(p->tabs, 0);
        for (i = 0; g_cats[td->cat].tabs[i]; i++) g_ptr_array_add(p->tabs, g_strdup(g_cats[td->cat].tabs[i]));
        p->every = 0;
        td_fill(td);
    } else p->every = which == 2;
}

static gboolean td_filter(GtkListBoxRow *row, gpointer d)
{
    tabsdlg *td = d;
    const char *hay = g_object_get_data(G_OBJECT(row), "hay");
    char *low = g_utf8_strdown(gtk_entry_get_text(GTK_ENTRY(td->entry)), -1);
    gboolean ok = !*low || (hay && strstr(hay, low));
    g_free(low);
    return ok;
}
static void td_search(GtkSearchEntry *e, gpointer d) { (void)e; gtk_list_box_invalidate_filter(GTK_LIST_BOX(((tabsdlg *)d)->list)); }
static void td_page(GtkComboBox *cb, gpointer d)
{
    tabsdlg *td = d;
    const char *id = gtk_combo_box_get_active_id(cb);
    int c;
    for (c = 1; c < CAT_N; c++) if (id && !strcmp(id, g_cats[c].name)) { td->cat = c; break; }
    td_fill(td);
}

static void tabs_dialog(int cat)
{
    tabsdlg *td = g_new0(tabsdlg, 1);
    GtkWidget *ca, *grid, *sw, *l;
    int c, i;
    static const char *const rl[3] = { "The _standard tabs only", "The tabs _ticked below (the standard ones, or any others)",
                                       "_Every item of the page" };
    td->dlg = gtk_dialog_new_with_buttons("Tabs on each page", g_main ? GTK_WINDOW(g_main->win) : NULL, GTK_DIALOG_MODAL,
                                          "_Cancel", GTK_RESPONSE_CANCEL, "_Apply", GTK_RESPONSE_ACCEPT, NULL);
    gtk_window_set_default_size(GTK_WINDOW(td->dlg), 560, 620);
    ca = gtk_dialog_get_content_area(GTK_DIALOG(td->dlg));
    gtk_container_set_border_width(GTK_CONTAINER(ca), 12);
    gtk_box_set_spacing(GTK_BOX(ca), 8);
    for (c = 1; c < CAT_N; c++) {
        td->work[c].every = g_cur.pg[c].every;
        td->work[c].tabs = g_ptr_array_new_with_free_func(g_free);
        for (i = 0; g_cur.pg[c].tabs && i < (int)g_cur.pg[c].tabs->len; i++) g_ptr_array_add(td->work[c].tabs, g_strdup(g_ptr_array_index(g_cur.pg[c].tabs, i)));
    }
    l = label_cls("Which items each page opens with as tabs. The order stays as on screen (drag tabs to change it); ticked items "
                  "that are new go at the end. Items that cannot show anything on this PC stay hidden unless Options > Show hidden "
                  "items is on. Saved with the current lineup when you save it (Menu > Lineup).", "dim-label", 0);
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_width_chars(GTK_LABEL(l), 60);   /* a reading width, or GTK sizes the dialog for a one-word-wide label */
    gtk_label_set_max_width_chars(GTK_LABEL(l), 70);
    gtk_box_pack_start(GTK_BOX(ca), l, FALSE, FALSE, 0);
    grid = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(grid), gtk_label_new_with_mnemonic("_Page:"), FALSE, FALSE, 0);
    td->combo = gtk_combo_box_text_new();
    for (c = 1; c < CAT_N; c++)   /* Power & Cost is its own page, without item tabs */
        if (c != CAT_POWER) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(td->combo), g_cats[c].name, g_cats[c].title);
    gtk_box_pack_start(GTK_BOX(grid), td->combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(ca), grid, FALSE, FALSE, 0);
    for (i = 0; i < 3; i++) {
        td->r[i] = gtk_radio_button_new_with_mnemonic_from_widget(i ? GTK_RADIO_BUTTON(td->r[0]) : NULL, rl[i]);
        g_object_set_data(G_OBJECT(td->r[i]), "which", GINT_TO_POINTER(i));
        g_signal_connect(td->r[i], "toggled", G_CALLBACK(td_radio), td);
        gtk_box_pack_start(GTK_BOX(ca), td->r[i], FALSE, FALSE, 0);
    }
    td->entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(td->entry), "Filter the list");
    g_signal_connect(td->entry, "search-changed", G_CALLBACK(td_search), td);
    gtk_box_pack_start(GTK_BOX(ca), td->entry, FALSE, FALSE, 0);
    td->list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(td->list), GTK_SELECTION_NONE);
    gtk_list_box_set_filter_func(GTK_LIST_BOX(td->list), td_filter, td, NULL);
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw), td->list);
    gtk_box_pack_start(GTK_BOX(ca), sw, TRUE, TRUE, 0);
    td->cat = cat < 1 || cat == CAT_POWER ? 1 : cat;
    g_signal_connect(td->combo, "changed", G_CALLBACK(td_page), td);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(td->combo), g_cats[td->cat].name);
    gtk_widget_show_all(td->dlg);
    if (gtk_dialog_run(GTK_DIALOG(td->dlg)) == GTK_RESPONSE_ACCEPT) {
        for (c = 1; c < CAT_N; c++) {
            pagestate *w = &td->work[c], *g = &g_cur.pg[c];
            int same = w->every == g->every && (g->tabs ? g->tabs->len : 0) == w->tabs->len;
            for (i = 0; same && i < (int)w->tabs->len; i++) same = !strcmp(g_ptr_array_index(w->tabs, i), g_ptr_array_index(g->tabs, i));
            if (same) continue;
            if (g->tabs) g_ptr_array_free(g->tabs, TRUE);
            g->tabs = w->tabs;
            g->every = w->every;
            w->tabs = NULL;
            page_fill(c);
        }
        tabs_state_save();
        lineup_label_update();
    }
    for (c = 1; c < CAT_N; c++) if (td->work[c].tabs) g_ptr_array_free(td->work[c].tabs, TRUE);
    gtk_widget_destroy(td->dlg);
    g_free(td);
}

/* 2.3.1: the mouse wheel over a side (vertical) tab list goes to the tab above / below, one notch per tab, no wrap-around.
   GTK 3's tab list cannot scroll on its own (it always shows the current tab), so stepping through the tabs is also
   how a long list scrolls. Touchpad scrolling adds up to whole notches. A running test is never started or stopped by
   this: leaving a running tool's tab asks as always (on_bench_switch). Tabs on top are not changed. */
static double g_wheel_acc;
static gboolean on_nb_scroll(GtkWidget *nb, GdkEventScroll *ev, gpointer d)
{
    GtkAllocation na, ca;
    GtkWidget *cur;
    int x, y, step = 0, n, i;
    (void)d;
    if (g_cur.layout != LAY_SIDE) return FALSE;
    n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(nb));
    i = gtk_notebook_get_current_page(GTK_NOTEBOOK(nb));
    if (i < 0 || !(cur = gtk_notebook_get_nth_page(GTK_NOTEBOOK(nb), i))) return FALSE;
    gtk_widget_get_allocation(nb, &na);
    gtk_widget_get_allocation(cur, &ca);
    gdk_window_get_device_position(gtk_widget_get_window(nb), ev->device, &x, &y, NULL);
    if (x < na.x || x >= ca.x) return FALSE;   /* only over the tab list, left of the page */
    if (ev->direction == GDK_SCROLL_UP) step = -1;
    else if (ev->direction == GDK_SCROLL_DOWN) step = 1;
    else if (ev->direction == GDK_SCROLL_SMOOTH) {
        double dx, dy;
        gdk_event_get_scroll_deltas((GdkEvent *)ev, &dx, &dy);
        g_wheel_acc += dy;
        step = (int)g_wheel_acc;   /* whole notches (several when GTK merged queued scroll events) */
        g_wheel_acc -= step;
    }
    if (step) {
        int to = i + step < 0 ? 0 : i + step >= n ? n - 1 : i + step;   /* no wrap-around */
        if (to != i) gtk_notebook_set_current_page(GTK_NOTEBOOK(nb), to);
    }
    return TRUE;
}

/* Keys on a focused tab list: Home / End = first / last tab (GTK's own binding, kept explicit here), Page Up / Page Down
   = 10 tabs back / on, no wrap-around. Ctrl+Page Up / Down stays GTK's previous / next tab. Inside a page the keys go to
   the page's own widgets (lists, spin buttons), since the notebook only sees them while its tab list has the focus. */
static gboolean on_nb_key(GtkWidget *nb, GdkEventKey *ev, gpointer d)
{
    int n, i, to;
    (void)d;
    if (!gtk_widget_is_focus(nb) || (ev->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SHIFT_MASK))) return FALSE;
    n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(nb));
    i = gtk_notebook_get_current_page(GTK_NOTEBOOK(nb));
    if (n < 1 || i < 0) return FALSE;
    switch (ev->keyval) {
    case GDK_KEY_Home: case GDK_KEY_KP_Home: to = 0; break;
    case GDK_KEY_End: case GDK_KEY_KP_End: to = n - 1; break;
    case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up: to = i - 10 < 0 ? 0 : i - 10; break;
    case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: to = i + 10 >= n ? n - 1 : i + 10; break;
    default: return FALSE;
    }
    if (to != i) gtk_notebook_set_current_page(GTK_NOTEBOOK(nb), to);
    return TRUE;
}

/* ---- a page's tab area: the notebook, the wrapped strip, and the "+" / "All N items" buttons ---- */
static GtkWidget *tabs_area(int cat)
{
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0), *nb = gtk_notebook_new(), *ctl, *b, *bx;
    char s[200];
    g_home->nb[cat] = nb;
    gtk_style_context_add_class(gtk_widget_get_style_context(nb), "srs-nb");
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(nb), TRUE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(nb), FALSE);
    gtk_widget_add_events(nb, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    g_signal_connect(nb, "scroll-event", G_CALLBACK(on_nb_scroll), NULL);
    g_signal_connect(nb, "key-press-event", G_CALLBACK(on_nb_key), NULL);
    g_home->strip[cat] = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(g_home->strip[cat]), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(g_home->strip[cat]), 40);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(g_home->strip[cat]), 2);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(g_home->strip[cat]), 2);
    gtk_style_context_add_class(gtk_widget_get_style_context(g_home->strip[cat]), "srs-strip");
    gtk_widget_set_no_show_all(g_home->strip[cat], TRUE);
    gtk_box_pack_start(GTK_BOX(vb), g_home->strip[cat], FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), nb, TRUE, TRUE, 0);

    ctl = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    g_home->ctl[cat] = ctl;
    b = gtk_button_new_from_icon_name("list-add-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
    snprintf(s, sizeof s, "Add a tab: pick any item of %s. It stays on this page.", g_cats[cat].title);
    gtk_widget_set_tooltip_text(b, s);
    g_signal_connect(b, "clicked", G_CALLBACK(on_add_btn), GINT_TO_POINTER(cat));
    gtk_box_pack_start(GTK_BOX(ctl), b, FALSE, FALSE, 0);
    b = gtk_button_new();
    bx = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    g_home->alllbl[cat] = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(bx), g_home->alllbl[cat], FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bx), gtk_image_new_from_icon_name("pan-down-symbolic", GTK_ICON_SIZE_BUTTON), FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(b), bx);
    gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
    snprintf(s, sizeof s, "Every SRS item in %s, the expert ones too: type to find one, pick it to go to its tab "
                          "(or open it as a temporary tab)", g_cats[cat].title);
    gtk_widget_set_tooltip_text(b, s);
    g_signal_connect(b, "clicked", G_CALLBACK(on_all_btn), GINT_TO_POINTER(cat));
    gtk_box_pack_start(GTK_BOX(ctl), b, FALSE, FALSE, 0);
    gtk_widget_show_all(ctl);
    gtk_notebook_set_action_widget(GTK_NOTEBOOK(nb), ctl, GTK_PACK_END);
    alllbl_update(cat);

    g_signal_connect(nb, "page-reordered", G_CALLBACK(on_nb_changed), GINT_TO_POINTER(cat));
    g_signal_connect(nb, "page-removed", G_CALLBACK(on_nb_changed), GINT_TO_POINTER(cat));
    g_signal_connect(nb, "page-added", G_CALLBACK(on_nb_added), GINT_TO_POINTER(cat));
    g_signal_connect(nb, "switch-page", G_CALLBACK(on_nb_switch), GINT_TO_POINTER(cat));
    g_signal_connect(nb, "switch-page", G_CALLBACK(on_bench_switch), NULL);   /* tool tabs (any page): ask before leaving a run */
    g_home->pagebox[cat] = vb;
    return vb;
}

/* first start (no lineup saved yet): let the user pick one, and say where to change it later */
static gboolean lineup_first_ask(gpointer d)
{
    static const char *const what[] = { NULL,
        "A few friendly tabs on each page. Good for a quick look.",
        "The standard tabs (as in SRS 2.0).",
        "Every item of every page as a tab, in a side list." };
    GtkWidget *dlg, *box, *r[LU_EXPERT + 1] = { 0 }, *l;
    int i, pick = g_lu_returning ? LU_ENTHUSIAST : LU_DEFAULT;
    (void)d;
    if (!g_lu_ask || !g_main) return G_SOURCE_REMOVE;
    dlg = gtk_dialog_new_with_buttons(g_lu_returning ? "SRS 2.2: your tabs" : "Welcome to SRS", GTK_WINDOW(g_main->win), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                      "_OK", GTK_RESPONSE_OK, NULL);
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 14);
    l = gtk_label_new(g_lu_returning ? "SRS 2.2 can arrange its pages in lineups.\nThe pages keep the standard tabs of SRS 2.0 (the Enthusiast lineup) unless you pick another:"
                                     : "Which tabs would you like on SRS's pages?");
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_box_pack_start(GTK_BOX(box), l, FALSE, FALSE, 0);
    for (i = LU_BASIC; i <= LU_EXPERT; i++) {
        char *t = g_strdup_printf("%s%s - %s", g_lu_builtin[i], i == (g_lu_returning ? LU_ENTHUSIAST : LU_DEFAULT) ? (g_lu_returning ? " (the standard tabs, kept)" : " (default)") : "", what[i]);
        r[i] = gtk_radio_button_new_with_label_from_widget(i > LU_BASIC ? GTK_RADIO_BUTTON(r[LU_BASIC]) : NULL, t);
        g_free(t);
        gtk_box_pack_start(GTK_BOX(box), r[i], FALSE, FALSE, 0);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r[pick]), TRUE);
    l = gtk_label_new("You can change this at any time: menu button (or Ctrl+M) -> Lineup. "
                      "The sidebar shows the lineup in use. The user guide explains lineups in 6.7.");
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    gtk_label_set_width_chars(GTK_LABEL(l), 56);   /* a reading width, or GTK sizes the dialog for a one-word-wide label */
    gtk_label_set_max_width_chars(GTK_LABEL(l), 60);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), "dim-label");
    gtk_box_pack_start(GTK_BOX(box), l, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), box, TRUE, TRUE, 0);
    gtk_widget_show_all(dlg);
    g_object_add_weak_pointer(G_OBJECT(dlg), (gpointer *)&dlg);
    /* closed without OK: the default. The main window can go away while the question is up (Quit from the tray menu):
       the dialog and its buttons are then destroyed with it; nothing is read and it is asked again next time */
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_NONE || !dlg || !g_main) {
        if (dlg) { g_object_remove_weak_pointer(G_OBJECT(dlg), (gpointer *)&dlg); gtk_widget_destroy(dlg); }
        return G_SOURCE_REMOVE;
    }
    g_object_remove_weak_pointer(G_OBJECT(dlg), (gpointer *)&dlg);
    for (i = LU_BASIC; i <= LU_EXPERT; i++)
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r[i]))) pick = i;
    gtk_widget_destroy(dlg);
    g_lu_ask = 0;
    lineup_switch(g_lu_builtin[pick]);   /* saves it, so this is asked only once */
    return G_SOURCE_REMOVE;
}
