/* gui_gtk.c - srs-gui: native Linux GUI (plain C + GTK3).
 *
 * Main window (1.2, see "the main window" below): a header bar with search and menu, a sidebar
 * of categories, an Overview page with a health check and summary cards, and one page per
 * category with a plain-words summary on top of the same section panels.
 * Classic view (--gui-classic, Menu -> Classic view): laid out the way SIV64X is. Its main window
 * shows [overview] with SIV's button row; every SIV item (538, see sec.h / CHECKLIST.tsv) opens
 * as a panel with SIV's own sub-button row, and Menu lists them all.
 * Tool windows: CPU Burnin, CPU Stress, Benchmark, IPI Delay, Latency, Device ID Lookup, Find.
 * With command-line arguments the same binary runs the CLI ("SIV64X -save" style). */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include "srs.h"
#include "sec.h"
#include "burner.h"
#include "bench.h"
#include "bench_cachemem.h"
#include "bench_os.h"
#include "bench_compute.h"
#include "bench_work.h"
#include "power.h"
#include "tblsort.h"
#include "gui_startup.h"

#include <dlfcn.h>
#include <signal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

/* optional hooks provided by sec_sensors.c (SIV's Zero and Hide buttons) */
void sensors_zero(void) __attribute__((weak));
void sensors_set_hide_unused(int on) __attribute__((weak));

static const char *const g_pal_light[C_NCOL] = { "#000000", "#3399FF", "#0000CC", "#9C0FB8", "#009999", "#FF8800", "#00A000", "#E00000", "#000000", "#808080" };
/* dark mode: same hues as SIV's palette, lifted so they read on a dark panel */
static const char *const g_pal_dark[C_NCOL] = { "#E6E6E6", "#5CB0FF", "#8FB4FF", "#D88CF0", "#3CD0D0", "#FFA64D", "#5AD65A", "#FF6B6B", "#E6E6E6", "#9A9A9A" };
static const char *const *g_pal = g_pal_light;
/* theme: 0 light (SIV classic), 1 dark, 2 follow the desktop */
enum { TH_LIGHT, TH_DARK, TH_SYSTEM };
static int g_theme = TH_LIGHT, g_dark, g_portal_scheme = -1, g_theme_set;
/* Menu -> Options -> Update interval: 0 = each panel's own rate, -1 = paused, else ms */
static int g_update_ms = 0;
static char g_env_theme[256];
static GDBusProxy *g_portal;
static void theme_apply(void);
static char g_findcol[16] = "#FFFF00", g_ini[1100], g_host[256] = "localhost", g_image[64] = "srs-gui", g_gpucmd[512];
static int g_highlight = 1, g_hide_unused;
/* SIV lives in the Icon Tray: tray options (SRSDEF.ini) */
static int g_opt_tray = 1, g_opt_close_tray = 0, g_opt_min_tray = 1, g_opt_start_tray = 0;
static int g_opt_redact = 0;   /* Options > Redact identifiers in saved reports */
static int g_opt_classic = 0;  /* Options > Start in classic view (SRSDEF.ini "Classic View") */
static int g_classic_session;  /* this run started in the classic (SIV-style) main window */

#define TS_MAXBLK 16
static int g_opt_sort = 1;     /* Options > Sortable tables (SRSDEF.ini "Sortable Tables") */

typedef enum { W_SECTION, W_IDLOOKUP, W_FIND, W_BURN, W_BENCH, W_HOME } wkind;
enum { BK_BENCH, BK_IPI, BK_LAT, BK_PI, BK_CRYPTO, BK_PRIME, BK_CACHEMEM, BK_STORAGE, BK_OSLAT, BK_COMPUTE, BK_WORK, BK_LAST = BK_WORK };
/* --gui-open / menu / launcher names of the 2.2 benchmark windows (the older ones are listed where they are used) */
static int bench_tool_sub(const char *t)
{
    if (!strcmp(t, "oslatency")) return BK_OSLAT;
    if (!strcmp(t, "compute")) return BK_COMPUTE;
    if (!strcmp(t, "work")) return BK_WORK;
    return -1;
}

typedef struct swin {
    wkind kind;
    char id[160];
    int is_main, sub;                     /* sub: W_BURN 0 burnin / 1 stress; W_BENCH BK_* */
    int embedded, shown, paused, cat;     /* a panel shown as a tab in the main window (not its own window) */
    int rendered;                         /* embedded: drawn at least once (static items are not redrawn on every show) */
    GtkWidget *page;                      /* embedded: the tab's page widget */
    GtkWidget *win, *view, *plot;
    GtkTextBuffer *buf;
    GtkTextBuffer *scratch;  /* live refresh is drawn here, then only changed lines are copied over */
    guint commit_id;
    guint timer;
    int own_ms;              /* the panel's own refresh period (0 = static) */
    /* id lookup */
    GtkWidget *l_id, *l_mode, *l_modelbl, *l_pci, *l_usb;
    int l_state;
    /* find */
    GtkWidget *f_pat, *f_icase, *f_fixed, *f_hl, *f_max, *f_report;
    srs_findres fr;
    int have_fr, fr_rc, busy, curmatch;
    char fr_pattern[512], fr_err[1400];
    GPtrArray *marks;
    /* burn */
    GtkWidget *b_time, *b_hours, *b_cool, *b_cycles, *b_limit, *b_same, *b_gpu, *b_gpucmd, *b_start, *b_stop;
    GtkWidget *b_cpu[BURN_MAXCPU];
    int nb_cpu;
    double hist[4][600];
    int nhist;
    burn_status st;
    /* bench */
    GtkWidget *bm_mode, *bm_secs, *bm_run, *bm_cpus, *bm_rounds, *bm_max, *bm_status;
    GtkWidget *bm_digits, *bm_algo, *bm_threads, *bm_limit, *bm_stop, *bm_superpi;
    GtkWidget *st_dir, *st_size, *st_profile, *st_usb, *st_info;
    GtkWidget *cp_isa, *cp_single;
    GtkWidget *wk_grp[WK_NGROUP], *wk_lib[WK_NLIB], *wk_row;
    int running;
    volatile int *cancelp;   /* the running job's cancel flag (SRS Pi, Prime sieve) */
    double run_t0;
    /* sortable tables (main-window tabs, 2.2): the last render op by op, replayed in the chosen order */
    GArray *rec;
    char *plain;             /* the text in the item's own order: what Copy copies, so it stays as before */
    int nblk, blk_line[TS_MAXBLK], blk_cols[TS_MAXBLK], sort_col[TS_MAXBLK], sort_dir[TS_MAXBLK];
    char *blk_head[TS_MAXBLK];   /* header cells, tab-separated (tooltips) */
    GtkWidget *reset_btn;
    /* main-window tab (2.2): a temporary tab is not kept in the page's tab list */
    int temp;
    GtkWidget *tablbl, *tabtxt;
} swin;

static GPtrArray *g_wins;
static swin *g_main;
static swin *g_classic;   /* the classic main window when it was opened from the new one */
static int g_home_open;   /* the new main window exists */

static void open_item(const char *id);   /* new main window: show the item's page, else a classic panel */
static void home_open(int as_main);
static void home_refresh_data(void);
static int home_key(swin *w, GdkEventKey *ev);
static void home_destroyed(swin *w);
static void open_main(int as_main);

static swin *open_section(const char *id);
static void open_guide(swin *w);
static void render(swin *w);
static void open_tool(wkind k, int sub);
static void tabs_options(GtkWidget *menu);   /* gui_tabs.inc */
static void lineup_menu(GtkWidget *m);
static int scan_empty_main(void);
static void empty_scan(int force);
/* Options > Check daily for items with nothing to show (SRSDEF.ini "Check Empty Items"): off unless the user turns it on */
static int g_opt_autoscan;

/* ======================================================================
 * GTK text backend for srs_out
 * ====================================================================== */
typedef struct { swin *w; GtkTextBuffer *buf; GtkTextIter end; const char *tabtag; } gctx;

/* (re)colour a buffer's palette tags for the current theme; find highlight keeps dark text in dark mode */
static void theme_tags(GtkTextBuffer *b)
{
    GtkTextTagTable *tt = gtk_text_buffer_get_tag_table(b);
    GtkTextTag *t;
    char n[8];
    int i;
    for (i = 0; i < C_NCOL; i++) {
        snprintf(n, sizeof n, "c%d", i);
        if ((t = gtk_text_tag_table_lookup(tt, n))) g_object_set(t, "foreground", g_pal[i], NULL);
    }
    if ((t = gtk_text_tag_table_lookup(tt, "hl"))) {
        if (g_dark) g_object_set(t, "foreground", "#000000", NULL);
        else g_object_set(t, "foreground-set", FALSE, NULL);
    }
}

static void ensure_tags(GtkTextBuffer *b)
{
    GtkTextTagTable *tt = gtk_text_buffer_get_tag_table(b);
    char n[8];
    int i;
    if (gtk_text_tag_table_lookup(tt, "c1")) return;
    for (i = 0; i < C_NCOL; i++) {
        snprintf(n, sizeof n, "c%d", i);
        gtk_text_buffer_create_tag(b, n, "foreground", g_pal[i], NULL);
    }
    gtk_text_buffer_create_tag(b, "b", "weight", PANGO_WEIGHT_BOLD, NULL);
    gtk_text_buffer_create_tag(b, "m", "family", "Courier New", NULL);
    gtk_text_buffer_create_tag(b, "hl", "background", g_findcol, NULL);
    gtk_text_buffer_create_tag(b, "sorthdr", "underline", PANGO_UNDERLINE_SINGLE, NULL);
    theme_tags(b);
}

static const char *tabs_tag(GtkTextBuffer *b, const int *px, int n)
{
    static char name[512];
    GtkTextTagTable *tt = gtk_text_buffer_get_tag_table(b);
    int i;
    size_t l;
    snprintf(name, sizeof name, "tabs");
    for (i = 0; i < n; i++) { l = strlen(name); snprintf(name + l, sizeof name - l, "_%d", px[i]); }
    if (!gtk_text_tag_table_lookup(tt, name)) {
        PangoTabArray *ta = pango_tab_array_new(n ? n : 1, TRUE);
        for (i = 0; i < n; i++) pango_tab_array_set_tab(ta, i, PANGO_TAB_LEFT, px[i]);
        gtk_text_buffer_create_tag(b, name, "tabs", ta, NULL);
        pango_tab_array_free(ta);
    }
    return gtk_text_tag_table_lookup(tt, name) ? name : NULL;
}

static char *clean(const char *s, gssize len)
{
    char *v = g_utf8_make_valid(s, len), *o = v, *i;
    for (i = v; *i; i++)
        if ((unsigned char)*i >= 0x20 || *i == '\t' || *i == '\n') *o++ = *i;
    *o = 0;
    return v;
}

static void ins(gctx *g, int col, int bold, int mono, int hl, const char *s, gssize len)
{
    char cn[8], *c = clean(s, len);
    GtkTextIter start;
    GtkTextBuffer *b = g->buf;
    int off = gtk_text_iter_get_offset(&g->end);
    snprintf(cn, sizeof cn, "c%d", col >= 0 && col < C_NCOL ? col : C_BLACK);
    gtk_text_buffer_insert(b, &g->end, c, -1);
    gtk_text_buffer_get_iter_at_offset(b, &start, off);
    gtk_text_buffer_apply_tag_by_name(b, cn, &start, &g->end);
    if (bold) gtk_text_buffer_apply_tag_by_name(b, "b", &start, &g->end);
    if (mono) gtk_text_buffer_apply_tag_by_name(b, "m", &start, &g->end);
    if (hl) gtk_text_buffer_apply_tag_by_name(b, "hl", &start, &g->end);
    if (g->tabtag) gtk_text_buffer_apply_tag_by_name(b, g->tabtag, &start, &g->end);
    g_free(c);
}

static void gx_text(srs_out *o, int c, int b, int m, const char *s) { ins(o->ctx, c, b, m, 0, s, -1); }
static void gx_tab(srs_out *o) { ins(o->ctx, C_BLACK, 0, 0, 0, "\t", -1); }
static void gx_par(srs_out *o) { ins(o->ctx, C_BLACK, 0, 0, 0, "\n", -1); }
static void gx_tabs(srs_out *o, const int *px, int n) { gctx *g = o->ctx; g->tabtag = tabs_tag(g->buf, px, n); }

static void out_setup(swin *w, srs_out *o, gctx *g, const char *arg, GtkTextBuffer *target)
{
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(target, &s, &e);
    gtk_text_buffer_delete(target, &s, &e);
    memset(g, 0, sizeof *g);
    g->w = w;
    g->buf = target;
    gtk_text_buffer_get_end_iter(target, &g->end);
    memset(o, 0, sizeof *o);
    o->text = gx_text; o->tab = gx_tab; o->par = gx_par; o->tabs = gx_tabs;
    o->ctx = g;
    o->gui = 1;
    o->arg = arg;
}

/* Draw straight into the visible buffer (one-off output: Find, ID lookup, benchmarks). */
static void out_begin(swin *w, srs_out *o, gctx *g, const char *arg) { out_setup(w, o, g, arg, w->buf); }

/* Split a buffer's text into lines (caller frees with g_strfreev). */
static char **buf_lines(GtkTextBuffer *b, int *n)
{
    GtkTextIter s, e;
    char *t, **v;
    gtk_text_buffer_get_bounds(b, &s, &e);
    t = gtk_text_buffer_get_text(b, &s, &e, FALSE);
    v = g_strsplit(t, "\n", -1);
    g_free(t);
    *n = (int)g_strv_length(v);
    return v;
}

static void line_iter(GtkTextBuffer *b, GtkTextIter *it, int line, int nlines)
{
    if (line >= nlines) gtk_text_buffer_get_end_iter(b, it);
    else gtk_text_buffer_get_iter_at_line(b, it, line);
}

/* Replace visible lines [va, vb) with scratch lines [sa, sb), text and tags. */
static void swap_lines(GtkTextBuffer *v, int va, int vb, int nv, GtkTextBuffer *sc, int sa, int sb, int ns)
{
    GtkTextIter a, b, c, d;
    line_iter(v, &a, va, nv);
    line_iter(v, &b, vb, nv);
    gtk_text_buffer_delete(v, &a, &b);
    line_iter(sc, &c, sa, ns);
    line_iter(sc, &d, sb, ns);
    {
        /* insert the text, then give it exactly the scratch copy's tags: text inserted next to
           tagged text can pick up its neighbour's colour, and a second colour tag wins over the
           right one (a re-sorted or refreshed line came out grey) */
        int off = gtk_text_iter_get_offset(&a), so = gtk_text_iter_get_offset(&c);
        GtkTextIter x, y, it = c;
        char *t = gtk_text_buffer_get_text(sc, &c, &d, FALSE);
        gtk_text_buffer_insert(v, &a, t, -1);
        g_free(t);
        gtk_text_buffer_get_iter_at_offset(v, &x, off);
        gtk_text_buffer_remove_all_tags(v, &x, &a);
        while (gtk_text_iter_compare(&it, &d) < 0) {
            GtkTextIter nx = it;
            GSList *tags, *l;
            if (!gtk_text_iter_forward_to_tag_toggle(&nx, NULL) || gtk_text_iter_compare(&nx, &d) > 0) nx = d;
            tags = gtk_text_iter_get_tags(&it);
            gtk_text_buffer_get_iter_at_offset(v, &x, off + gtk_text_iter_get_offset(&it) - so);
            gtk_text_buffer_get_iter_at_offset(v, &y, off + gtk_text_iter_get_offset(&nx) - so);
            for (l = tags; l; l = l->next) gtk_text_buffer_apply_tag(v, l->data, &x, &y);
            g_slist_free(tags);
            if (gtk_text_iter_equal(&nx, &it)) break;
            it = nx;
        }
    }
}

/* Copy only what changed from the scratch buffer to the visible one, so a live
 * refresh doesn't clear and re-lay-out the whole view (that was the flicker). */
static gboolean commit_idle(gpointer data)
{
    swin *w = data;
    char **vl, **sl;
    int nv, ns, p = 0, q = 0, i;
    w->commit_id = 0;
    if (!w->scratch || !w->buf) return G_SOURCE_REMOVE;
    vl = buf_lines(w->buf, &nv);
    sl = buf_lines(w->scratch, &ns);
    while (p < nv && p < ns && !strcmp(vl[p], sl[p])) p++;
    if (p == nv && p == ns) goto done; /* nothing changed: no redraw at all */
    while (q < nv - p && q < ns - p && !strcmp(vl[nv - 1 - q], sl[ns - 1 - q])) q++;
    if (nv - p - q == ns - p - q) {
        /* same shape: replace changed lines one at a time, bottom up so line numbers hold */
        for (i = nv - q - 1; i >= p; i--)
            if (strcmp(vl[i], sl[i])) swap_lines(w->buf, i, i + 1, nv, w->scratch, i, i + 1, ns);
    } else {
        swap_lines(w->buf, p, nv - q, nv, w->scratch, p, ns - q, ns);
    }
done:
    g_strfreev(vl);
    g_strfreev(sl);
    return G_SOURCE_REMOVE;
}

/* Draw a live refresh off screen; the diff is applied just before GTK paints. */
static void out_begin_live(swin *w, srs_out *o, gctx *g, const char *arg)
{
    if (!w->scratch) w->scratch = gtk_text_buffer_new(gtk_text_buffer_get_tag_table(w->buf));
    out_setup(w, o, g, arg, w->scratch);
    if (!w->commit_id) w->commit_id = g_idle_add_full(G_PRIORITY_HIGH_IDLE, commit_idle, w, NULL);
}

/* ======================================================================
 * sortable tables (2.2). A main-window tab records its item's output op by op, finds the tables
 * in it (a row of C_LABEL headings followed by rows on the same tab stops), and replays the ops
 * with the rows of each table in the order the user picked by clicking a heading. The text view,
 * its colours, Find, double-click lookup and the live refresh stay as they were; Copy copies the
 * item's own order (w->plain), so it is the same text as before. Save never goes through here.
 * ====================================================================== */
enum { RO_TEXT, RO_TAB, RO_PAR, RO_TABS };
typedef struct { int kind, c, b, m, n; char *s; int px[32]; } rop;

static void rop_clear(gpointer p) { g_free(((rop *)p)->s); }
static GArray *rec_new(void)
{
    GArray *a = g_array_new(FALSE, TRUE, sizeof(rop));
    g_array_set_clear_func(a, rop_clear);
    return a;
}
static void rc_text(srs_out *o, int c, int b, int m, const char *s) { rop r = { RO_TEXT, c, b, m, 0, g_strdup(s), { 0 } }; g_array_append_val((GArray *)o->ctx, r); }
static void rc_tab(srs_out *o) { rop r = { RO_TAB, 0, 0, 0, 0, NULL, { 0 } }; g_array_append_val((GArray *)o->ctx, r); }
static void rc_par(srs_out *o) { rop r = { RO_PAR, 0, 0, 0, 0, NULL, { 0 } }; g_array_append_val((GArray *)o->ctx, r); }
static void rc_tabs(srs_out *o, const int *px, int n)
{
    rop r = { RO_TABS, 0, 0, 0, n < 0 ? 0 : n > 32 ? 32 : n, NULL, { 0 } };
    memcpy(r.px, px, (size_t)r.n * sizeof(int));
    g_array_append_val((GArray *)o->ctx, r);
}

typedef struct {
    guint a, e;            /* ops [a, e): the row's ops, e = its RO_PAR (or the end) */
    int ncell, nl, alllabel, nonempty, tabsig;
    GPtrArray *cells;      /* cell texts */
} trow;

typedef struct { int first, last, idx; const char *key; int dir; } tgroup;   /* rows [first, last] */
static int tgroup_cmp(const void *x, const void *y)
{
    const tgroup *a = x, *b = y;
    int ma = ts_missing(a->key), mb = ts_missing(b->key), r;
    if (ma != mb) return ma - mb;   /* empty and "-" stay last both ways */
    r = ma ? 0 : ts_cmp(a->key, b->key) * a->dir;
    return r ? r : a->idx - b->idx;   /* stable */
}

static void replay_op(srs_out *o, const rop *r)
{
    switch (r->kind) {
    case RO_TEXT: o->text(o, r->c, r->b, r->m, r->s); break;
    case RO_TAB: o->tab(o); break;
    case RO_PAR: o->par(o); break;
    default: o->tabs(o, r->px, r->n); break;
    }
}

/* split the recorded ops into rows of cells */
static GArray *rec_rows(GArray *rec)
{
    GArray *rows = g_array_new(FALSE, TRUE, sizeof(trow));
    trow cur = { 0 };
    GString *cell = g_string_new(NULL);
    int sig = 0, cellcol = -1;
    guint i;
    cur.cells = g_ptr_array_new_with_free_func(g_free);
    cur.alllabel = 1;
    for (i = 0; i <= rec->len; i++) {
        rop *r = i < rec->len ? &g_array_index(rec, rop, i) : NULL;
        if (!r || r->kind == RO_TAB || r->kind == RO_PAR) {
            char *t = g_strstrip(g_string_free(cell, FALSE));
            if (*t) { cur.nonempty++; if (cellcol != C_LABEL) cur.alllabel = 0; }
            g_ptr_array_add(cur.cells, t);
            cell = g_string_new(NULL);
            cellcol = -1;
            cur.ncell++;
        }
        if (!r || r->kind == RO_PAR) {
            cur.e = i;
            cur.tabsig = sig;
            g_array_append_val(rows, cur);
            memset(&cur, 0, sizeof cur);
            cur.a = i + 1;
            cur.alllabel = 1;
            cur.cells = g_ptr_array_new_with_free_func(g_free);
            if (!r) break;
            continue;
        }
        if (r->kind == RO_TEXT) {
            if (strchr(r->s, '\n')) cur.nl = 1;
            if (cellcol < 0 && *r->s && strspn(r->s, " ") != strlen(r->s)) cellcol = r->c;
            g_string_append(cell, r->s);
        } else if (r->kind == RO_TABS) {
            int k;
            sig = 17;
            for (k = 0; k < r->n; k++) sig = sig * 31 + r->px[k];
            if (!sig) sig = 1;
        }
    }
    g_string_free(cell, TRUE);
    if (cur.cells) g_ptr_array_free(cur.cells, TRUE);
    return rows;
}

static void rows_free(GArray *rows)
{
    guint i;
    for (i = 0; i < rows->len; i++) g_ptr_array_free(g_array_index(rows, trow, i).cells, TRUE);
    g_array_free(rows, TRUE);
}

static int row_is_head(const trow *r) { return !r->nl && r->ncell >= 2 && r->nonempty >= 2 && r->alllabel; }

/* Replay w->rec into the tab's view with each table in its chosen order. */
static void tbl_replay(swin *w)
{
    GArray *rows;
    srs_out o;
    gctx g;
    GString *plain = g_string_new(NULL);
    int blk_first[TS_MAXBLK], blk_data[TS_MAXBLK], blk_end[TS_MAXBLK], nb = 0, line = 0, b;
    guint i, j;
    if (!w->rec) return;
    rows = rec_rows(w->rec);
    /* find the tables: heading row, optional blank row, then >= 2 rows on the same tab stops */
    for (i = 0; i < rows->len && nb < TS_MAXBLK; i++) {
        trow *h = &g_array_index(rows, trow, i);
        guint d, k;
        int groups = 0;
        if (!row_is_head(h)) continue;
        d = i + 1;
        if (d < rows->len && g_array_index(rows, trow, d).ncell == 1 && !*(char *)g_ptr_array_index(g_array_index(rows, trow, d).cells, 0)) d++;
        for (k = d; k < rows->len; k++) {
            trow *r = &g_array_index(rows, trow, k);
            const char *c0 = g_ptr_array_index(r->cells, 0);
            if (r->nl || r->ncell < 2 || r->tabsig != h->tabsig || row_is_head(r) || !r->nonempty) break;
            if (*c0 || !groups) groups++;
        }
        if (groups < 2) continue;
        blk_first[nb] = (int)i;
        blk_data[nb] = (int)d;
        blk_end[nb] = (int)k;
        nb++;
        i = k - 1;
    }
    /* a different table shape than last time (another item state): forget the sort */
    if (nb != w->nblk) for (b = 0; b < TS_MAXBLK; b++) w->sort_dir[b] = 0;
    for (b = 0; b < w->nblk; b++) { g_free(w->blk_head[b]); w->blk_head[b] = NULL; }
    w->nblk = nb;

    out_begin_live(w, &o, &g, NULL);
    b = 0;
    for (i = 0; i < rows->len; i++) {
        trow *r = &g_array_index(rows, trow, i);
        int inblk = b < nb && (int)i == blk_first[b];
        if (inblk) {
            /* the heading, with an arrow after the sorted column */
            int cellno = 0, sc = w->sort_dir[b] ? w->sort_col[b] : -1;
            GString *hd = g_string_new(NULL);
            for (j = 0; j < r->cells->len; j++) { if (j) g_string_append_c(hd, '\t'); g_string_append(hd, g_ptr_array_index(r->cells, j)); }
            w->blk_head[b] = g_string_free(hd, FALSE);
            w->blk_cols[b] = r->ncell;
            w->blk_line[b] = line;
            for (j = r->a; j <= r->e && j < w->rec->len; j++) {
                rop *x = &g_array_index(w->rec, rop, j);
                if ((x->kind == RO_TAB || x->kind == RO_PAR) && cellno == sc)
                    o.text(&o, C_LABEL, 1, 0, w->sort_dir[b] > 0 ? " ▲" : " ▼");
                if (x->kind == RO_TAB) cellno++;
                replay_op(&o, x);
            }
            line++;
            /* the rows between the heading and the data (a blank line), then the data in order */
            for (j = i + 1; (int)j < blk_data[b]; j++) {
                trow *y = &g_array_index(rows, trow, j);
                guint q;
                for (q = y->a; q <= y->e && q < w->rec->len; q++) replay_op(&o, &g_array_index(w->rec, rop, q));
                line++;
            }
            {
                int n = blk_end[b] - blk_data[b], ng = 0, k;
                tgroup *gv = g_new0(tgroup, n);
                for (k = blk_data[b]; k < blk_end[b]; k++) {
                    trow *y = &g_array_index(rows, trow, k);
                    const char *c0 = g_ptr_array_index(y->cells, 0);
                    if (*c0 || !ng) {
                        gv[ng].first = k;
                        gv[ng].idx = ng;
                        gv[ng].dir = w->sort_dir[b] < 0 ? -1 : 1;
                        gv[ng].key = w->sort_dir[b] && w->sort_col[b] < (int)y->cells->len ? g_ptr_array_index(y->cells, w->sort_col[b]) : "";
                        ng++;
                    }
                    gv[ng - 1].last = k;
                }
                if (w->sort_dir[b]) qsort(gv, (size_t)ng, sizeof *gv, tgroup_cmp);
                for (k = 0; k < ng; k++) {
                    int rr;
                    for (rr = gv[k].first; rr <= gv[k].last; rr++) {
                        trow *y = &g_array_index(rows, trow, rr);
                        guint q;
                        for (q = y->a; q <= y->e && q < w->rec->len; q++) replay_op(&o, &g_array_index(w->rec, rop, q));
                        line++;
                    }
                }
                g_free(gv);
            }
            i = (guint)blk_end[b] - 1;
            b++;
            continue;
        }
        for (j = r->a; j <= r->e && j < w->rec->len; j++) {
            rop *x = &g_array_index(w->rec, rop, j);
            replay_op(&o, x);
            if (x->kind == RO_TEXT) { const char *p; for (p = x->s; (p = strchr(p, '\n')); p++) line++; }
        }
        if (r->e < w->rec->len) line++;
    }
    /* underline the headings so they look clickable */
    for (b = 0; b < nb; b++) {
        GtkTextIter s0, e0;
        gtk_text_buffer_get_iter_at_line(g.buf, &s0, w->blk_line[b]);
        e0 = s0;
        gtk_text_iter_forward_to_line_end(&e0);
        {   /* each heading's text, not the gaps between them */
            GtkTextIter a0 = s0, c0;
            while (gtk_text_iter_compare(&a0, &e0) < 0) {
                while (gtk_text_iter_compare(&a0, &e0) < 0 && (gtk_text_iter_get_char(&a0) == '\t' || gtk_text_iter_get_char(&a0) == ' '))
                    gtk_text_iter_forward_char(&a0);
                c0 = a0;
                while (gtk_text_iter_compare(&c0, &e0) < 0 && gtk_text_iter_get_char(&c0) != '\t') gtk_text_iter_forward_char(&c0);
                if (gtk_text_iter_compare(&a0, &c0) < 0) gtk_text_buffer_apply_tag_by_name(g.buf, "sorthdr", &a0, &c0);
                a0 = c0;
            }
        }
    }
    /* Copy: the item's own order, exactly as the view had it before sorting existed */
    for (i = 0; i < w->rec->len; i++) {
        rop *x = &g_array_index(w->rec, rop, i);
        if (x->kind == RO_TEXT) { char *c = clean(x->s, -1); g_string_append(plain, c); g_free(c); }
        else if (x->kind == RO_TAB) g_string_append_c(plain, '\t');
        else if (x->kind == RO_PAR) g_string_append_c(plain, '\n');
    }
    g_free(w->plain);
    w->plain = g_string_free(plain, FALSE);
    rows_free(rows);
    if (w->reset_btn) {
        int sorted = 0;
        for (b = 0; b < nb; b++) sorted |= w->sort_dir[b] != 0;
        gtk_widget_set_visible(w->reset_btn, nb > 0);
        gtk_widget_set_sensitive(w->reset_btn, sorted);
    }
    gtk_widget_set_has_tooltip(w->view, nb > 0);
}

/* the table heading under a point of the view: block index and column, -1 if none */
static int tbl_hit(swin *w, int x, int y, int *colp)
{
    GtkTextIter it, ls;
    int bx, by, line, b;
    char *t;
    if (!w->nblk || !w->view) return -1;
    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(w->view), GTK_TEXT_WINDOW_WIDGET, x, y, &bx, &by);
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(w->view), &it, bx, by);
    line = gtk_text_iter_get_line(&it);
    for (b = 0; b < w->nblk; b++) if (w->blk_line[b] == line) break;
    if (b == w->nblk) return -1;
    {   /* below the last line the iter snaps to it: make sure the point is on the heading's row */
        GdkRectangle r;
        gtk_text_view_get_iter_location(GTK_TEXT_VIEW(w->view), &it, &r);
        if (by < r.y || by > r.y + r.height) return -1;
    }
    ls = it;
    gtk_text_iter_set_line_offset(&ls, 0);
    t = gtk_text_buffer_get_text(w->buf, &ls, &it, FALSE);
    {
        int col = 0;
        const char *p;
        for (p = t; *p; p++) col += *p == '\t';
        if (col >= w->blk_cols[b]) col = w->blk_cols[b] - 1;
        *colp = col;
    }
    g_free(t);
    return b;
}

static const char *tbl_head_name(swin *w, int b, int col, char *out, size_t n)
{
    const char *p = w->blk_head[b] ? w->blk_head[b] : "";
    int k;
    for (k = 0; k < col && p; k++) { p = strchr(p, '\t'); if (p) p++; }
    snprintf(out, n, "%.*s", p ? (int)strcspn(p, "\t") : 0, p ? p : "");
    return out;
}

static void tbl_click(swin *w, int b, int col)
{
    char name[120];
    if (!*tbl_head_name(w, b, col, name, sizeof name)) return;   /* no heading over this column */
    if (w->sort_dir[b] && w->sort_col[b] == col) w->sort_dir[b] = -w->sort_dir[b];
    else { w->sort_col[b] = col; w->sort_dir[b] = 1; }
    tbl_replay(w);
}

static void tbl_reset(swin *w)
{
    int b;
    for (b = 0; b < TS_MAXBLK; b++) w->sort_dir[b] = 0;
    if (w->rec) tbl_replay(w);
}

static gboolean on_view_motion(GtkWidget *v, GdkEventMotion *ev, gpointer d)
{
    swin *w = d;
    int col, hit = tbl_hit(w, (int)ev->x, (int)ev->y, &col) >= 0;
    GdkWindow *win = gtk_text_view_get_window(GTK_TEXT_VIEW(v), GTK_TEXT_WINDOW_TEXT);
    GdkCursor *c;
    if (!w->embedded || !win) return FALSE;
    c = gdk_cursor_new_from_name(gtk_widget_get_display(v), hit ? "pointer" : "text");
    gdk_window_set_cursor(win, c);
    if (c) g_object_unref(c);
    return FALSE;
}

static gboolean on_view_tooltip(GtkWidget *v, int x, int y, gboolean kb, GtkTooltip *tip, gpointer d)
{
    swin *w = d;
    int col, b;
    char name[120], s[500];
    (void)v;
    if (kb || (b = tbl_hit(w, x, y, &col)) < 0 || !*tbl_head_name(w, b, col, name, sizeof name)) return FALSE;
    snprintf(s, sizeof s, "Click to sort by %s: smallest or A first (▲), click again for largest or Z first (▼).\n"
             "Numbers sort by value with their units (MB / GB, MHz, °C, RPM, %%), dates by date.\n"
             "Reset view puts the item's own order back.", name);
    gtk_tooltip_set_text(tip, s);
    return TRUE;
}

/* ======================================================================
 * settings (SIV keeps SIVDEF.txt; Linux: ~/.config/srs/SRSDEF.ini)
 * ====================================================================== */
static void settings_load(void)
{
    GKeyFile *kf = g_key_file_new();
    char *dir = g_build_filename(g_get_user_config_dir(), "srs", NULL);
    snprintf(g_ini, sizeof g_ini, "%s/SRSDEF.ini", dir);
    g_free(dir);
    if (g_key_file_load_from_file(kf, g_ini, G_KEY_FILE_KEEP_COMMENTS, NULL)) {
        char *c = g_key_file_get_string(kf, "SRSDEF", "Colour Find Text", NULL);
        char *gc = g_key_file_get_string(kf, "SRSDEF", "GPU Load Command", NULL);
        GError *e = NULL;
        int h = g_key_file_get_integer(kf, "SRSDEF", "Highlight Matches", &e);
        if (c && strlen(c) == 6) snprintf(g_findcol, sizeof g_findcol, "#%s", c);
        if (!e) g_highlight = h; else g_error_free(e);
        if (gc) snprintf(g_gpucmd, sizeof g_gpucmd, "%s", gc);
        {
            GError *e3 = NULL;
            int u = g_key_file_get_integer(kf, "SRSDEF", "Update Interval", &e3);
            if (!e3) g_update_ms = u < -1 ? 0 : u; else g_error_free(e3);
        }
        {
            char *th = g_key_file_get_string(kf, "SRSDEF", "Theme", NULL);
            if (th) { g_theme = !g_ascii_strcasecmp(th, "Dark") ? TH_DARK : !g_ascii_strcasecmp(th, "System") ? TH_SYSTEM : TH_LIGHT; g_theme_set = 1; }
            g_free(th);
        }
        {
            static const char *const keys[] = { "Tray Icon", "Close To Tray", "Minimize To Tray", "Start In Tray", "Redact Reports", "Classic View", "Sortable Tables" };
            int *vars[] = { &g_opt_tray, &g_opt_close_tray, &g_opt_min_tray, &g_opt_start_tray, &g_opt_redact, &g_opt_classic, &g_opt_sort };
            int k;
            for (k = 0; k < 7; k++) {
                GError *e2 = NULL;
                int v = g_key_file_get_integer(kf, "SRSDEF", keys[k], &e2);
                if (!e2) *vars[k] = v; else g_error_free(e2);
            }
        }
        g_free(c);
        g_free(gc);
    }
    g_key_file_free(kf);
}

static void settings_save(void)
{
    GKeyFile *kf = g_key_file_new();
    char *dir = g_path_get_dirname(g_ini);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    g_key_file_load_from_file(kf, g_ini, G_KEY_FILE_KEEP_COMMENTS, NULL);
    g_key_file_set_string(kf, "SRSDEF", "Colour Find Text", g_findcol + 1);
    g_key_file_set_integer(kf, "SRSDEF", "Highlight Matches", g_highlight);
    g_key_file_set_string(kf, "SRSDEF", "Font Name", "Tahoma");
    g_key_file_set_string(kf, "SRSDEF", "GPU Load Command", g_gpucmd);
    g_key_file_set_integer(kf, "SRSDEF", "Tray Icon", g_opt_tray);
    g_key_file_set_integer(kf, "SRSDEF", "Close To Tray", g_opt_close_tray);
    g_key_file_set_integer(kf, "SRSDEF", "Minimize To Tray", g_opt_min_tray);
    g_key_file_set_integer(kf, "SRSDEF", "Start In Tray", g_opt_start_tray);
    g_key_file_set_integer(kf, "SRSDEF", "Redact Reports", g_opt_redact);
    g_key_file_set_integer(kf, "SRSDEF", "Classic View", g_opt_classic);
    g_key_file_set_integer(kf, "SRSDEF", "Sortable Tables", g_opt_sort);
    g_key_file_set_string(kf, "SRSDEF", "Theme", g_theme == TH_DARK ? "Dark" : g_theme == TH_SYSTEM ? "System" : "Light");
    g_key_file_set_integer(kf, "SRSDEF", "Update Interval", g_update_ms);
    g_key_file_save_to_file(kf, g_ini, NULL);
    g_key_file_free(kf);
}

/* ======================================================================
 * SIV button rows (transcribed from SIV64X's panels; "Windows" -> "Linux")
 * ====================================================================== */
typedef struct { const char *label, *target; } btn;

#define B(l, t) { l, t }
static const btn row_main[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_Machine", "machine"),
    B("_Sensors", "sensors"), B("_USB Bus", "usb-bus"), B("_Processors", "processors"), B("_Volumes", "volumes"),
    B("_Adapters", "adapters"), B("_PCI Bus", "pci-bus"), B("A_bout", "about"), B("_Help", "help"), B("Me_nu", "!menu"), B(NULL, NULL) };
static const btn row_windows[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_Machine", "machine"),
    B("_Sensors", "sensors"), B("_USB Bus", "usb-bus"), B("_Processors", "processors"), B("_Volumes", "volumes"),
    B("_PCI Bus", "pci-bus"), B("ALL _Dev", "all-dev"), B("_Graphics", "graphics"), B(NULL, NULL) };
static const btn row_machine[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_Machine", "machine"),
    B("_DIMMs", "dimms"), B("_Status", "dimm-status"), B("_USB Bus", "usb-bus"), B("_Volumes", "volumes"),
    B("_PCI Bus", "pci-bus"), B("ALL D_ev", "all-dev"), B(NULL, NULL) };
static const btn row_sensors[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Zero", "!zero"), B("_Linux", "windows"),
    B("_Sensors", "sensors"), B("S_tatus", "hw-status"), B("C_ooling", "cooling"), B("_Power", "power"),
    B("_Volumes", "volumes"), B("_Hide", "!hide"), B(NULL, NULL) };
static const btn row_usb[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_Machine", "machine"),
    B("L_ink Devices", "link-devices"), B("_USB Bus", "usb-bus"), B("USB _Disks", "usb-disks"), B("_HID Bus", "hid-bus"),
    B("_PCI Bus", "pci-bus"), B("USB _Info", "usb-info"), B(NULL, NULL) };
static const btn row_processors[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_Processors", "processors"),
    B("_Features", "features"), B("_Hypervisor", "hypervisor"), B("_Groups", "group"), B("CPU-_0", "processor:0"),
    B("CPU-_1", "processor:1"), B("CPU-_4", "processor:4"), B("CPU-_8", "processor:8"), B("Cache-0", "cache:0"),
    B("Cache-1", "cache:1"), B("Cache-2", "cache:2"), B("Cache-3", "cache:3"), B("CPU _Stress", "!stress"),
    B("CPU _Burnin", "!burn"), B("Bench_mark", "!bench"), B(NULL, NULL) };
static const btn row_volumes[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_Machine", "machine"),
    B("_Sensors", "sensors"), B("_USB Bus", "usb-bus"), B("_Volumes", "volumes"), B("Drive _/", "volumes:/"),
    B("_FS", "ntfs:/"), B("ALL _Dev", "all-dev"), B(NULL, NULL) };
static const btn row_adapters[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_Adapters", "adapters"),
    B("Co_unters", "counters"), B("_Interface", "interface"), B("A_RP", "arp-table"), B("Rou_ting", "routing"),
    B("TCP V_4", "tcp-ip4-ports"), B("TCP V_6", "tcp-ip6-ports"), B("_Wi-Fi", "wi-fi"), B("_Bluetooth", "bluetooth"),
    B("Show _DNS", "dns-names"), B("_Ping", "ping"), B("_Main", "!main"), B(NULL, NULL) };
static const btn row_pci[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_Machine", "machine"),
    B("_USB Bus", "usb-bus"), B("P_roperties", "properties"), B("P_nP Dev", "pnp-dev"), B("_Volumes", "volumes"),
    B("_Drives", "drives"), B("_PCI Bus", "pci-bus"), B("PCI D_ev", "pci-dev"), B("U_AA Dev", "uaa-dev"),
    B("ALL De_v", "all-dev"), B(NULL, NULL) };
static const btn row_help[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_DNS Names", "dns-names"),
    B("_Lookup", "lookup"), B("_USB Dev", "usb-dev"), B("A_bout", "about"), B("_Help", "help"), B(NULL, NULL) };
static const btn row_lookup[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Linux", "windows"), B("_DNS Names", "dns-names"),
    B("_Signature", "signing"), B("SRS _DLLs", "siv-dll"), B("_Lookup", "lookup"), B("_ID Lookup", "!idlookup"),
    B("A_bout", "about"), B("_Help", "help"), B(NULL, NULL) };
static const btn row_default[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Refresh", "!refresh"), B("Me_nu", "!menu"),
    B("_Help", "help"), B(NULL, NULL) };

static int is(const char *base, const char *const *list)
{
    for (; *list; list++) if (!strcmp(base, *list)) return 1;
    return 0;
}

static const btn *row_for(const char *id, int is_main)
{
    static const char *const l_mach[] = { "machine", "dimms", "dimm-status", NULL };
    static const char *const l_sens[] = { "sensors", "hw-status", "cooling", "power", NULL };
    static const char *const l_usb[] = { "usb-bus", "usb-info", "hid-bus", "usb-disks", "link-devices", NULL };
    static const char *const l_proc[] = { "processors", "processor", "features", "hypervisor", "group", "cache", NULL };
    static const char *const l_vol[] = { "volumes", "ntfs", "fat", NULL };
    static const char *const l_ad[] = { "adapters", "counters", "interface", "arp-table", "routing", "tcp-ip4-ports", "tcp-ip6-ports",
                                        "wi-fi", "bluetooth", "dns-names", "ping", NULL };
    static const char *const l_pci[] = { "pci-bus", "pci-dev", "pnp-dev", "properties", "uaa-dev", "drives", "all-dev", NULL };
    static const char *const l_help[] = { "help", "about", NULL };
    static const char *const l_look[] = { "lookup", "siv-dll", "signing", NULL };
    char base[64];
    snprintf(base, sizeof base, "%.*s", (int)strcspn(id, ":"), id);
    if (is_main) return row_main;
    if (!strcmp(base, "windows")) return row_windows;
    if (is(base, l_mach)) return row_machine;
    if (is(base, l_sens)) return row_sensors;
    if (is(base, l_usb)) return row_usb;
    if (is(base, l_proc)) return row_processors;
    if (is(base, l_vol)) return row_volumes;
    if (is(base, l_ad)) return row_adapters;
    if (is(base, l_pci)) return row_pci;
    if (is(base, l_help)) return row_help;
    if (is(base, l_look)) return row_lookup;
    return row_default;
}

/* ======================================================================
 * window bookkeeping
 * ====================================================================== */
static swin *find_win(wkind k, const char *id, int sub)
{
    guint i;
    for (i = 0; i < g_wins->len; i++) {
        swin *w = g_ptr_array_index(g_wins, i);
        if (w->kind == k && !w->is_main && !w->embedded && (k != W_SECTION || !strcmp(w->id, id)) && (k == W_SECTION || w->sub == sub)) return w;
    }
    return NULL;
}

static void burn_saved(swin *w);
static void on_destroy(GtkWidget *wid, gpointer data)
{
    swin *w = data;
    (void)wid;
    if (w->timer) g_source_remove(w->timer);
    if (w->commit_id) g_source_remove(w->commit_id);
    if (w->scratch) g_object_unref(w->scratch);
    if (w->kind == W_BURN && burn_active()) { burn_stop(); burn_tick(&w->st); burn_saved(w); }
    if (w->kind == W_BENCH && w->running && w->cancelp) *w->cancelp = 1;   /* closing the window stops the run */
    if (w->have_fr) srs_find_free(&w->fr);
    if (w->marks) g_ptr_array_free(w->marks, TRUE);
    if (w->rec) g_array_free(w->rec, TRUE);
    g_free(w->plain);
    { int b; for (b = 0; b < TS_MAXBLK; b++) g_free(w->blk_head[b]); }
    g_ptr_array_remove(g_wins, w);
    if (w == g_classic) g_classic = NULL;
    if (w->kind == W_HOME) home_destroyed(w);
    if (w->is_main) gtk_main_quit();
    g_free(w);
}

static void copy_all(swin *w)
{
    GtkTextIter s, e;
    char *txt, *r;
    if (!w->buf) return;
    gtk_text_buffer_get_bounds(w->buf, &s, &e);
    /* a sorted tab copies the item's own order: the same text as before sorting existed */
    txt = w->plain ? g_strdup(w->plain) : gtk_text_buffer_get_text(w->buf, &s, &e, FALSE);
    srs_redact = g_opt_redact;   /* the copied text is masked like a saved report; the panel is not */
    r = srs_redact_text(txt);
    srs_redact = 0;
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), r ? r : txt, -1);
    free(r);
    gtk_clipboard_store(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
    g_free(txt);
}

/* ======================================================================
 * Menu (SIV's tray menu): every item, grouped
 * ====================================================================== */
static void save_dialog(int all);
static void build_options(GtkWidget *menu);
static void run_root(void);
static void on_menu_item(GtkMenuItem *mi, gpointer data) { (void)mi; open_section(data); }
static void on_menu_tool(GtkMenuItem *mi, gpointer data)
{
    const char *t = data;
    (void)mi;
    if (!strcmp(t, "burn")) open_tool(W_BURN, 0);
    else if (!strcmp(t, "stress")) open_tool(W_BURN, 1);
    else if (!strcmp(t, "bench")) open_tool(W_BENCH, BK_BENCH);
    else if (!strcmp(t, "ipi")) open_tool(W_BENCH, BK_IPI);
    else if (!strcmp(t, "lat")) open_tool(W_BENCH, BK_LAT);
    else if (!strcmp(t, "pi")) open_tool(W_BENCH, BK_PI);
    else if (!strcmp(t, "crypto")) open_tool(W_BENCH, BK_CRYPTO);
    else if (!strcmp(t, "prime")) open_tool(W_BENCH, BK_PRIME);
    else if (!strcmp(t, "cachemem")) open_tool(W_BENCH, BK_CACHEMEM);
    else if (!strcmp(t, "storagebench")) open_tool(W_BENCH, BK_STORAGE);
    else if (bench_tool_sub(t) >= 0) open_tool(W_BENCH, bench_tool_sub(t));
    else if (!strcmp(t, "guide")) open_guide(NULL);
    else if (!strcmp(t, "idlookup")) open_tool(W_IDLOOKUP, 0);
    else if (!strcmp(t, "find")) open_tool(W_FIND, 0);
    else if (!strcmp(t, "save")) save_dialog(0);
    else if (!strcmp(t, "saveall")) save_dialog(1);
    else if (!strcmp(t, "root")) run_root();
    else if (!strcmp(t, "home")) home_open(0);
    else if (!strcmp(t, "classic")) open_main(0);
    else if (!strcmp(t, "exit") && g_main) gtk_widget_destroy(g_main->win);
}

static GtkWidget *menu_add(GtkWidget *menu, const char *label)
{
    GtkWidget *it = gtk_menu_item_new_with_mnemonic(label);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    return it;
}

static void build_menu(GtkWidget *menu)
{
    static const struct { const char *group, *label; } groups[] = {
        { "cpu", "_Processors" }, { "machine", "_Machine / BIOS / Memory" }, { "sensors", "_Sensors / Cooling / Power" },
        { "storage", "_Volumes / Drives" }, { "network", "_Network" }, { "os", "_Linux (SIV Windows items)" },
        { "gpu", "_Graphics / Monitors / Audio" }, { "bus", "_Buses / Devices" }, { "tools", "_Tools" }, { "siv", "S_RS (SIV items)" },
        { NULL, NULL } };
    static const struct { const char *label, *tool; } tools[] = {
        { "CPU _Burnin...", "burn" }, { "CPU _Stress...", "stress" }, { "_Benchmark...", "bench" }, { "_IPI Delay...", "ipi" },
        { "_Latency...", "lat" }, { "SRS _Pi...", "pi" }, { "C_rypto Benchmark...", "crypto" }, { "P_rime Sieve...", "prime" },
        { "Cache & _Memory...", "cachemem" }, { "St_orage Benchmark...", "storagebench" }, { "_OS Latency...", "oslatency" },
        { "CPU Co_mpute...", "compute" }, { "_Everyday Work...", "work" },
        { "Device _ID Lookup...", "idlookup" }, { "_Find in Report...", "find" }, { NULL, NULL } };
    int g, i, n = srs_section_count();
    for (i = 0; tools[i].label; i++) g_signal_connect(menu_add(menu, tools[i].label), "activate", G_CALLBACK(on_menu_tool), (gpointer)tools[i].tool);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    if (g_classic_session && (!g_main || g_main->kind != W_HOME))   /* classic start: the new window one click away */
        g_signal_connect(menu_add(menu, "Open the _new main window (sidebar, overview)"), "activate", G_CALLBACK(on_menu_tool), (gpointer)"home");
    else
        g_signal_connect(menu_add(menu, "Classic _view (SIV-style main window)"), "activate", G_CALLBACK(on_menu_tool), (gpointer)"classic");
    for (g = 0; groups[g].group; g++) {
        GtkWidget *sub = gtk_menu_new(), *top = menu_add(menu, groups[g].label);
        int cnt = 0;
        for (i = 0; i < n; i++) {
            const srs_item *s = srs_section_at(i);
            char lab[200];
            GtkWidget *it;
            if (!s || !s->group || strcmp(s->group, groups[g].group)) continue;
            snprintf(lab, sizeof lab, "%s  [%s]%s", s->title, s->id,
                     s->bucket == B_ABSENT ? "  (not present)" : s->bucket == B_WINONLY ? "  (Windows-only)" : "");
            it = gtk_menu_item_new_with_label(lab);
            g_signal_connect(it, "activate", G_CALLBACK(on_menu_item), (gpointer)s->id);
            gtk_menu_shell_append(GTK_MENU_SHELL(sub), it);
            cnt++;
        }
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(top), sub);
        if (!cnt) gtk_widget_set_sensitive(top, FALSE);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    {
        GtkWidget *opt = gtk_menu_new();
        build_options(opt);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_add(menu, "_Options")), opt);
    }
    g_signal_connect(menu_add(menu, "Save _Report..."), "activate", G_CALLBACK(on_menu_tool), (gpointer)"save");
    g_signal_connect(menu_add(menu, "Save _All Items..."), "activate", G_CALLBACK(on_menu_tool), (gpointer)"saveall");
    g_signal_connect(menu_add(menu, "Run as _root (srs-root)..."), "activate", G_CALLBACK(on_menu_tool), (gpointer)"root");
    g_signal_connect(menu_add(menu, "User _Guide..."), "activate", G_CALLBACK(on_menu_tool), (gpointer)"guide");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    g_signal_connect(menu_add(menu, "E_xit"), "activate", G_CALLBACK(on_menu_tool), (gpointer)"exit");
}

static void popup_menu(GtkWidget *anchor)
{
    GtkWidget *menu = gtk_menu_new();
    build_menu(menu);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), anchor, GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
}

/* ======================================================================
 * Save / Run as root
 * ====================================================================== */
static void save_dialog(int all)
{
    GtkWidget *d = gtk_file_chooser_dialog_new(all ? "Save All Items" : "Save Report", g_main ? GTK_WINDOW(g_main->win) : NULL,
                                               GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
    char name[300];
    snprintf(name, sizeof name, "srs-%s%s.txt", g_opt_redact ? "report" : g_host, all ? "-all" : "");
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), name);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
        char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
        srs_redact = g_opt_redact;   /* only the saved file is masked; the panels keep showing everything */
        srs_save(all ? "[all]" : NULL, f);
        srs_redact = 0;
        g_free(f);
    }
    gtk_widget_destroy(d);
}

static void root_done(GPid pid, gint status, gpointer data)
{
    guint i;
    char *copy = data;
    g_spawn_close_pid(pid);
    if (copy) {   /* the AppImage's helper copy: remove it and its private folder */
        char *dir = g_path_get_dirname(copy);
        g_unlink(copy);
        g_rmdir(dir);
        g_free(dir);
        g_free(copy);
    }
    for (i = 0; i < g_wins->len; i++) { swin *w = g_ptr_array_index(g_wins, i); if (w->kind == W_SECTION) render(w); }
    home_refresh_data();
    if (status == 0 && g_home_open && g_opt_autoscan) empty_scan(1);   /* items that were empty only for want of root data */
    if (g_main) {
        GtkWidget *m = gtk_message_dialog_new(GTK_WINDOW(g_main->win), GTK_DIALOG_MODAL, status == 0 ? GTK_MESSAGE_INFO : GTK_MESSAGE_WARNING,
                                              GTK_BUTTONS_OK, "%s", status == 0 ? "Root data collected into ~/.cache/srs/root"
                                                                                : "srs-root did not complete (cancelled or failed)");
        gtk_dialog_run(GTK_DIALOG(m));
        gtk_widget_destroy(m);
    }
}

/* Inside an AppImage the helper is in the AppImage's FUSE mount, which only the user who
 * started it can read: root (pkexec) gets "permission denied". So, right before the prompt,
 * copy the helper into a new private folder (mode 0700) in $XDG_RUNTIME_DIR (a local tmpfs that
 * root can always read; ~/.cache/srs if there is none or it is mounted noexec) and pkexec that
 * copy. pkexec and polkit are used exactly as for the packaged helper: the administrator password
 * is asked for and the prompt names the file that runs. The copy is the same script as in the
 * AppImage, which the user can already change, so nothing new is trusted; other users cannot
 * read or change the folder. root_done() deletes it. Returns the copy's path (g_free) or NULL. */
static char *root_helper_copy(const char *helper)
{
    const char *rt = g_getenv("XDG_RUNTIME_DIR");
    char *bases[2], *copy = NULL, *txt = NULL;
    gsize len = 0;
    int i;
    if (!g_file_get_contents(helper, &txt, &len, NULL)) return NULL;
    bases[0] = rt && *rt && g_file_test(rt, G_FILE_TEST_IS_DIR) ? g_strdup(rt) : NULL;
    bases[1] = g_build_filename(g_get_user_cache_dir(), "srs", NULL);
    g_mkdir_with_parents(bases[1], 0700);
    for (i = 0; i < 2 && !copy; i++) {
        char *tmpl, *dir;
        if (!bases[i]) continue;
        tmpl = g_build_filename(bases[i], "srs-root-XXXXXX", NULL);
        if ((dir = g_mkdtemp_full(tmpl, 0700))) {
            copy = g_build_filename(dir, "srs-root", NULL);
            /* access(X_OK) also fails on a noexec mount: then try the next place */
            if (!g_file_set_contents(copy, txt, (gssize)len, NULL) || g_chmod(copy, 0700) || access(copy, X_OK)) {
                g_unlink(copy); g_rmdir(dir); g_free(copy); copy = NULL;
            }
        }
        g_free(tmpl);
    }
    g_free(txt);
    g_free(bases[0]);
    g_free(bases[1]);
    return copy;
}

static void run_root(void)
{
    char exe[1024] = "", helper[1100], *copy = NULL;
    ssize_t l = readlink("/proc/self/exe", exe, sizeof exe - 1);
    int appimage = g_getenv("APPIMAGE") != NULL;
    GtkWidget *m;
    GPid pid;
    if (l > 0) { exe[l] = 0; if (strrchr(exe, '/')) *strrchr(exe, '/') = 0; }
    snprintf(helper, sizeof helper, "%s/srs-root", exe);
    if (access(helper, X_OK)) snprintf(helper, sizeof helper, "%s/.local/bin/srs-root", g_get_home_dir());
    m = gtk_message_dialog_new(g_main ? GTK_WINDOW(g_main->win) : NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL,
                               "Collect root-only data with one pkexec prompt?\n\n"
                               "dmidecode, decode-dimms, smartctl / nvme smart-log, ACPI table copies, MSR reads, "
                               "lspci config space, ioports/iomem, dmesg.\nRead-only; results go to ~/.cache/srs/root.\n\n%s%s", helper,
                               appimage ? "\n\nAppImage: the administrator account cannot look inside the AppImage, so this "
                                          "script is first copied to a private folder of yours and run from there. "
                                          "The copy is deleted when it has finished." : "");
    if (gtk_dialog_run(GTK_DIALOG(m)) == GTK_RESPONSE_OK) {
        char *argv[] = { (char *)"pkexec", helper, NULL };
        if (appimage) {
            if (!(copy = root_helper_copy(helper))) {
                GtkWidget *e = gtk_message_dialog_new(GTK_WINDOW(m), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "%s",
                                                      "Could not copy the srs-root script out of the AppImage, so root-only data "
                                                      "cannot be collected this time. The SRS package for your distribution does "
                                                      "not need this step.");
                gtk_dialog_run(GTK_DIALOG(e));
                gtk_widget_destroy(e);
                gtk_widget_destroy(m);
                return;
            }
            argv[1] = copy;
        }
        if (g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, NULL))
            g_child_watch_add(pid, root_done, copy);
        else if (copy)
            root_done(0, -1, copy);
    }
    gtk_widget_destroy(m);
}

/* ======================================================================
 * section windows
 * ====================================================================== */

static void render(swin *w)
{
    srs_out o;
    gctx g;
    const srs_item *it;
    const char *colon;
    if (w->kind != W_SECTION) return;
    colon = strchr(w->id, ':');
    it = srs_section_find(w->id);
    if (w->embedded && g_opt_sort && it) {   /* main-window tab: record, then replay with the tables sorted */
        if (w->rec) g_array_free(w->rec, TRUE);
        w->rec = rec_new();
        memset(&o, 0, sizeof o);
        o.text = rc_text; o.tab = rc_tab; o.par = rc_par; o.tabs = rc_tabs;
        o.ctx = w->rec;
        o.gui = 1;
        o.arg = colon ? colon + 1 : NULL;
        srs_section_render(it, &o);
        tbl_replay(w);
        return;
    }
    if (w->rec) { g_array_free(w->rec, TRUE); w->rec = NULL; g_free(w->plain); w->plain = NULL; w->nblk = 0;
                  if (w->reset_btn) gtk_widget_hide(w->reset_btn); }
    out_begin_live(w, &o, &g, colon ? colon + 1 : NULL);
    if (!it) { so_err(&o, "No such item"); return; }
    srs_section_render(it, &o);
}

static gboolean live_tick(gpointer data) { render(data); return G_SOURCE_CONTINUE; }

/* (Re)start a live panel's refresh timer at the chosen rate. */
static void live_start(swin *w)
{
    guint ms;
    if (w->timer) { g_source_remove(w->timer); w->timer = 0; }
    if (w->own_ms <= 0 || g_update_ms < 0) return;  /* static panel, or paused */
    if (w->embedded && (!w->shown || w->paused)) return;  /* main-window tab: only while on screen */
    ms = (guint)(g_update_ms > 0 ? g_update_ms : w->own_ms);
    w->timer = g_timeout_add(ms < 250 ? 250 : ms, live_tick, w);
}

static void on_btn(GtkButton *b, gpointer data);

static GtkWidget *button_row(swin *w, const btn *row)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    int i;
    for (i = 0; row[i].label; i++) {
        GtkWidget *b = gtk_button_new_with_mnemonic(row[i].label);
        gtk_widget_set_name(b, "sivbtn");
        gtk_widget_set_size_request(b, 66, -1);
        g_object_set_data(G_OBJECT(b), "target", (gpointer)row[i].target);
        g_signal_connect(b, "clicked", G_CALLBACK(on_btn), w);
        gtk_box_pack_start(GTK_BOX(box), b, FALSE, FALSE, 0);
    }
    {   /* every panel: open the user guide at this item's section (also F1) */
        GtkWidget *b = gtk_button_new_with_mnemonic("Gu_ide");
        gtk_widget_set_name(b, "sivbtn");
        gtk_widget_set_size_request(b, 66, -1);
        gtk_widget_set_tooltip_text(b, "Open the user guide at this panel's section (F1)");
        g_object_set_data(G_OBJECT(b), "target", (gpointer)"!guide");
        g_signal_connect(b, "clicked", G_CALLBACK(on_btn), w);
        gtk_box_pack_end(GTK_BOX(box), b, FALSE, FALSE, 0);
    }
    return box;
}

/* double-click: open a device ID found on the line in Device ID Lookup */
static gboolean on_view_click(GtkWidget *view, GdkEventButton *ev, gpointer data)
{
    swin *w = data;
    GtkTextIter it, s, e, b0;
    int bx, by, usb;
    char *line, *above, *pu, *pp;
    if (ev->type == GDK_BUTTON_PRESS && ev->button == 1 && w->nblk) {   /* a table heading: sort by it */
        int col, b = tbl_hit(w, (int)ev->x, (int)ev->y, &col);
        if (b >= 0) { tbl_click(w, b, col); return TRUE; }
    }
    if (ev->type != GDK_2BUTTON_PRESS || ev->button != 1) return FALSE;
    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(view), GTK_TEXT_WINDOW_WIDGET, (int)ev->x, (int)ev->y, &bx, &by);
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(view), &it, bx, by);
    s = it;
    gtk_text_iter_set_line_offset(&s, 0);
    e = s;
    gtk_text_iter_forward_to_line_end(&e);
    line = gtk_text_buffer_get_text(w->buf, &s, &e, FALSE);
    gtk_text_buffer_get_start_iter(w->buf, &b0);
    above = gtk_text_buffer_get_text(w->buf, &b0, &s, FALSE);
    pu = g_strrstr(above, "[USB]");
    pp = g_strrstr(above, "[PCI]");
    usb = pu && (!pp || pu > pp);
    g_free(above);
    {
        char tgt[100];
        if (srs_line_device_id(w->id, line, usb || strstr(line, "USB") != NULL, tgt, sizeof tgt)) {
            g_free(line);
            if (w->embedded) open_item(tgt); else open_section(tgt);
            return TRUE;
        }
    }
    g_free(line);
    return FALSE;
}

static void on_ctx_guide(GtkMenuItem *mi, gpointer data) { (void)mi; open_guide(data); }

/* Copy of a selection (right-click Copy, Ctrl+C): masked like the Copy button when Redact is on */
static void on_copy_clip(GtkTextView *v, gpointer data)
{
    GtkTextBuffer *b = gtk_text_view_get_buffer(v);
    GtkTextIter s, e;
    char *txt, *r;
    (void)data;
    if (!g_opt_redact || !gtk_text_buffer_get_selection_bounds(b, &s, &e)) return;
    txt = gtk_text_buffer_get_text(b, &s, &e, FALSE);
    srs_redact = 1;
    r = srs_redact_text(txt);
    srs_redact = 0;
    if (r) {
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), r, -1);
        gtk_clipboard_store(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
        g_signal_stop_emission_by_name(v, "copy-clipboard");
        free(r);
    }
    g_free(txt);
}

static void on_populate(GtkTextView *v, GtkWidget *popup, gpointer data)
{
    GtkWidget *sep = gtk_separator_menu_item_new(), *sub = gtk_menu_new(), *top, *gi;
    (void)v;
    gtk_menu_shell_append(GTK_MENU_SHELL(popup), sep);
    gi = gtk_menu_item_new_with_mnemonic("Open _guide for this item (F1)");
    g_signal_connect(gi, "activate", G_CALLBACK(on_ctx_guide), data);
    gtk_menu_shell_append(GTK_MENU_SHELL(popup), gi);
    top = gtk_menu_item_new_with_mnemonic("SRS _Menu");
    build_menu(sub);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(top), sub);
    gtk_menu_shell_append(GTK_MENU_SHELL(popup), top);
    gtk_widget_show_all(popup);
}

static gboolean on_key(GtkWidget *wid, GdkEventKey *ev, gpointer data);

static GtkWidget *make_window(swin *w, const char *title, int width, int height)
{
    w->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(w->win), title);
    gtk_window_set_default_size(GTK_WINDOW(w->win), width, height);
    gtk_window_set_icon_name(GTK_WINDOW(w->win), "srs");
    gtk_style_context_add_class(gtk_widget_get_style_context(w->win), "siv");   /* classic look (load_css) */
    if (!w->is_main && g_main) {
        gtk_window_set_transient_for(GTK_WINDOW(w->win), GTK_WINDOW(g_main->win));
        gtk_window_move(GTK_WINDOW(w->win), 40 + (int)(g_wins->len % 12) * 24, 40 + (int)(g_wins->len % 12) * 22);
    }
    g_signal_connect(w->win, "destroy", G_CALLBACK(on_destroy), w);
    g_signal_connect(w->win, "key-press-event", G_CALLBACK(on_key), w);
    g_ptr_array_add(g_wins, w);
    return w->win;
}

static GtkWidget *make_view(swin *w)
{
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    w->view = gtk_text_view_new();
    gtk_widget_set_name(w->view, "panel");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(w->view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(w->view), GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(w->view), 6);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(w->view), 4);
    w->buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->view));
    ensure_tags(w->buf);
    g_signal_connect(w->view, "button-press-event", G_CALLBACK(on_view_click), w);
    g_signal_connect(w->view, "populate-popup", G_CALLBACK(on_populate), w);
    g_signal_connect(w->view, "copy-clipboard", G_CALLBACK(on_copy_clip), w);
    g_signal_connect(w->view, "motion-notify-event", G_CALLBACK(on_view_motion), w);
    g_signal_connect(w->view, "query-tooltip", G_CALLBACK(on_view_tooltip), w);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sw), w->view);
    return sw;
}

static swin *open_section(const char *id)
{
    swin *w = find_win(W_SECTION, id, 0);
    const srs_item *it = srs_section_find(id);
    GtkWidget *vb;
    char title[300];
    if (w) { gtk_window_present(GTK_WINDOW(w->win)); render(w); return w; }
    w = g_new0(swin, 1);
    w->kind = W_SECTION;
    snprintf(w->id, sizeof w->id, "%s", id);
    if (it && strchr(id, ':')) snprintf(title, sizeof title, "%s - %s %s", g_image, it->title, strchr(id, ':') + 1);
    else snprintf(title, sizeof title, "%s - %s", g_image, it ? it->title : id);
    make_window(w, title, 1180, 600);
    vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vb), 4);
    gtk_container_add(GTK_CONTAINER(w->win), vb);
    gtk_box_pack_start(GTK_BOX(vb), make_view(w), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vb), button_row(w, row_for(id, 0)), FALSE, FALSE, 0);
    render(w);
    w->own_ms = it && it->live_ms > 0 ? (it->live_ms < 500 ? 500 : it->live_ms) : 0;
    live_start(w);
    gtk_widget_show_all(w->win);
    return w;
}

/* ======================================================================
 * Device ID Lookup tool (tri-state Tree / Scan / Info like SIV's Port Names box)
 * ====================================================================== */
static srs_db g_db[2];
static int g_dbok[2];

static const char *mode_text(int m)
{
    return m == MODE_TREE ? "Tree Lookup mode - Do bsearch() of a sorted table" :
           m == MODE_SCAN ? "Scan Lookup mode - Scan the .ids file" : "Info Lookup mode - Use the OS device database";
}

static void render_idlookup(swin *w)
{
    static const int tabs[] = { 24, 200, 700 }, t2[] = { 110 };
    srs_out o;
    gctx g;
    char buf[1400], recs[32];
    const char *id = gtk_entry_get_text(GTK_ENTRY(w->l_id));
    int mode = w->l_state, i;
    enum srs_bus bus = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->l_usb)) ? BUS_USB : BUS_PCI;
    out_begin(w, &o, &g, NULL);
    so_tabs(&o, tabs, 3);
    snprintf(buf, sizeof buf, "Lookup %s on the %s bus", id, bus == BUS_USB ? "USB" : "PCI");
    so_b(&o, C_VALUE, buf); so_t(&o, C_GRAY, "   "); so_t(&o, C_TEAL, mode_text(mode)); so_par(&o); so_par(&o);
    if (mode == MODE_INFO) {
        srs_info in;
        int rc = srs_info_lookup(bus, id, &in);
        so_tabs(&o, t2, 1);
        so_kv(&o, "Query", C_PURPLE, in.query);
        if (in.err[0]) so_kv(&o, "Error", C_RED, in.err);
        so_kv(&o, "Vendor", C_VALUE, in.vendor[0] ? in.vendor : "-");
        so_kv(&o, "Resolved", rc == 0 ? C_BLACK : C_RED, in.model[0] ? in.model : "-");
        snprintf(buf, sizeof buf, "info mode (systemd-hwdb) in %.2f ms", in.ms);
        so_par(&o); so_note(&o, buf);
        return;
    }
    if (!g_dbok[bus] && srs_db_load(&g_db[bus], bus) == 0) g_dbok[bus] = 1;
    if (!g_dbok[bus]) { so_err(&o, "cannot find the .ids file"); return; }
    {
        srs_result r;
        char err[256];
        if (srs_resolve(&g_db[bus], id, mode, &r, err, sizeof err)) { so_err(&o, err); return; }
        so_tab(&o); so_t(&o, C_LABEL, "Key"); so_tab(&o); so_t(&o, C_LABEL, "Entry"); so_par(&o);
        for (i = 0; i < r.nchain; i++) {
            so_t(&o, C_ORANGE, i == r.matched ? "<" : ""); so_tab(&o);
            so_t(&o, i == r.matched ? C_PURPLE : C_GRAY, r.chain[i]); so_tab(&o);
            so_t(&o, r.name[i][0] ? (i == r.matched ? C_BLACK : C_VALUE) : C_GRAY, r.name[i][0] ? r.name[i] : "(no entry)");
            so_par(&o);
        }
        so_par(&o);
        so_tabs(&o, t2, 1);
        so_kv(&o, "Vendor", C_VALUE, r.vendor[0] ? r.vendor : "-");
        if (r.subvendor[0]) so_kv(&o, "Subsystem", C_GREEN, r.subvendor);
        so_kv(&o, "Stock", C_TEAL, r.stock[0] ? r.stock : "-");
        if (r.resolved[0]) so_kv(&o, "Resolved", C_BLACK, r.resolved);
        else { snprintf(buf, sizeof buf, "No Matches for %s", id); so_kv(&o, "Resolved", C_RED, buf); }
        srs_thousands(g_db[bus].n, recs, sizeof recs);
        snprintf(buf, sizeof buf, "%s mode: %s records loaded in %.1f ms, lookup %.3f ms   (%s)", mode == MODE_TREE ? "tree" : "scan",
                 recs, g_db[bus].load_ms, r.lookup_ms, g_db[bus].path);
        so_par(&o); so_note(&o, buf);
    }
}

static void set_mode_box(swin *w)
{
    g_signal_handlers_block_matched(w->l_mode, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, w);
    gtk_toggle_button_set_inconsistent(GTK_TOGGLE_BUTTON(w->l_mode), w->l_state == MODE_SCAN);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->l_mode), w->l_state != MODE_INFO);
    g_signal_handlers_unblock_matched(w->l_mode, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, w);
    gtk_label_set_text(GTK_LABEL(w->l_modelbl), mode_text(w->l_state));
}

static void on_mode(GtkToggleButton *b, gpointer data)
{
    swin *w = data;
    (void)b;
    w->l_state = w->l_state == MODE_TREE ? MODE_SCAN : w->l_state == MODE_SCAN ? MODE_INFO : MODE_TREE;
    set_mode_box(w);
    render_idlookup(w);
}
static void on_bus(GtkToggleButton *b, gpointer data) { if (gtk_toggle_button_get_active(b)) render_idlookup(data); }
static void on_lookup_run(GtkWidget *x, gpointer data) { (void)x; render_idlookup(data); }

static GtkWidget *lbl(const char *s) { GtkWidget *l = gtk_label_new_with_mnemonic(s); gtk_widget_set_name(l, "lbl"); return l; }
static GtkWidget *sbtn(const char *s) { GtkWidget *b = gtk_button_new_with_mnemonic(s); gtk_widget_set_name(b, "sivbtn"); return b; }

/* ======================================================================
 * Find tool (worker thread, highlight, F3 navigation)
 * ====================================================================== */
typedef struct { swin *w; char report[1100], pattern[512]; int icase, fixed, max; srs_findres fr; int rc; char err[1400]; } findjob;

static void goto_match(swin *w, int dir)
{
    int n = w->marks ? (int)w->marks->len / 2 : 0;
    GtkTextIter a, b;
    char title[300];
    if (!n) { gtk_widget_error_bell(w->win); return; }
    w->curmatch = (w->curmatch + dir + n) % n;
    gtk_text_buffer_get_iter_at_mark(w->buf, &a, g_ptr_array_index(w->marks, 2 * w->curmatch));
    gtk_text_buffer_get_iter_at_mark(w->buf, &b, g_ptr_array_index(w->marks, 2 * w->curmatch + 1));
    gtk_text_buffer_select_range(w->buf, &a, &b);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(w->view), g_ptr_array_index(w->marks, 2 * w->curmatch), 0.1, FALSE, 0, 0);
    snprintf(title, sizeof title, "%s - Find - Match %d of %d shown (%d total)", g_image, w->curmatch + 1, n, w->fr.total);
    gtk_window_set_title(GTK_WINDOW(w->win), title);
}

static void render_find(swin *w)
{
    srs_out o;
    gctx g;
    char buf[1600], title[300];
    int i, j, k, maxshow = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w->f_max));
    out_begin(w, &o, &g, NULL);
    g_ptr_array_set_size(w->marks, 0);
    w->curmatch = -1;
    if (w->busy) { snprintf(buf, sizeof buf, "Searching for \"%s\" ...", w->fr_pattern); so_t(&o, C_TEAL, buf); so_par(&o); return; }
    if (!w->have_fr) {
        so_note(&o, "Enter a pattern (Python-style regex, or tick Literal) and press Enter or Find.");
        so_note(&o, "Searches every section (*.txt, lshw.html) of the selected sysreport folder.");
        return;
    }
    if (w->fr_rc) { so_err(&o, w->fr_err); return; }
    snprintf(buf, sizeof buf, "Searching %s for \"%s\"", w->fr.report, w->fr_pattern);
    so_t(&o, C_TEAL, buf); so_par(&o);
    for (i = 0; i < w->fr.nsec; i++) {
        const srs_section *s = &w->fr.secs[i];
        so_par(&o);
        snprintf(buf, sizeof buf, "== %s - %d Match%s", s->file, s->count, s->count != 1 ? "es" : "");
        so_b(&o, C_VALUE, buf); so_par(&o);
        for (j = 0; j < s->nshown; j++) {
            const srs_hit *h = &s->hits[j];
            int at = 0;
            snprintf(buf, sizeof buf, "%7d: ", h->lineno);
            ins(&g, C_GRAY, 0, 1, 0, buf, -1);
            for (k = 0; k < h->nspan; k++) {
                ins(&g, C_BLACK, 0, 1, 0, h->text + at, h->spans[k].so - at);
                g_ptr_array_add(w->marks, gtk_text_buffer_create_mark(w->buf, NULL, &g.end, TRUE));
                ins(&g, C_BLACK, 0, 1, g_highlight, h->text + h->spans[k].so, h->spans[k].eo - h->spans[k].so);
                g_ptr_array_add(w->marks, gtk_text_buffer_create_mark(w->buf, NULL, &g.end, TRUE));
                at = h->spans[k].eo;
            }
            ins(&g, C_BLACK, 0, 1, 0, h->text + at, -1);
            so_par(&o);
        }
        if (s->count > s->nshown && s->count > maxshow) { snprintf(buf, sizeof buf, "         ... %d more (raise Max)", s->count - s->nshown); so_note(&o, buf); }
    }
    so_par(&o);
    if (!w->fr.total) { snprintf(buf, sizeof buf, "No Matches for %s", w->fr_pattern); so_err(&o, buf); }
    else { snprintf(buf, sizeof buf, "%d Matches in %d of %d sections  (%.1f ms)", w->fr.total, w->fr.nsec, w->fr.nfiles, w->fr.ms); so_t(&o, C_GREEN, buf); so_par(&o); }
    snprintf(title, sizeof title, "%s - Find - %d Matches", g_image, w->fr.total);
    gtk_window_set_title(GTK_WINDOW(w->win), title);
}

static gboolean find_done(gpointer data)
{
    findjob *j = data;
    swin *w = j->w;
    if (g_ptr_array_find(g_wins, w, NULL)) {
        if (w->have_fr) srs_find_free(&w->fr);
        w->fr = j->fr;
        w->fr_rc = j->rc;
        snprintf(w->fr_err, sizeof w->fr_err, "%s", j->err);
        w->have_fr = 1;
        w->busy = 0;
        render_find(w);
        if (w->marks->len) goto_match(w, +1);
    } else if (!j->rc) srs_find_free(&j->fr);
    g_free(j);
    return G_SOURCE_REMOVE;
}

static gpointer find_worker(gpointer data)
{
    findjob *j = data;
    j->rc = srs_find(j->report[0] ? j->report : NULL, j->pattern, j->icase, j->fixed, j->max, &j->fr, j->err, sizeof j->err);
    g_idle_add(find_done, j);
    return NULL;
}

static void start_find(swin *w)
{
    findjob *j;
    GThread *th;
    if (w->busy) return;
    j = g_new0(findjob, 1);
    j->w = w;
    gtk_spin_button_update(GTK_SPIN_BUTTON(w->f_max));
    snprintf(j->pattern, sizeof j->pattern, "%s", gtk_entry_get_text(GTK_ENTRY(w->f_pat)));
    snprintf(j->report, sizeof j->report, "%s", gtk_entry_get_text(GTK_ENTRY(w->f_report)));
    j->max = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w->f_max));
    j->icase = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->f_icase));
    j->fixed = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->f_fixed));
    snprintf(w->fr_pattern, sizeof w->fr_pattern, "%s", j->pattern);
    w->busy = 1;
    render_find(w);
    th = g_thread_try_new("srs-find", find_worker, j, NULL);
    if (!th) { w->busy = 0; w->fr_rc = 2; w->have_fr = 1; snprintf(w->fr_err, sizeof w->fr_err, "Failed to Create Worker Thread"); g_free(j); render_find(w); return; }
    g_thread_unref(th);
}

static void on_find_run(GtkWidget *x, gpointer data) { (void)x; start_find(data); }

static void toggle_highlight(void)
{
    guint i;
    g_highlight = !g_highlight;
    settings_save();
    for (i = 0; i < g_wins->len; i++) {
        swin *w = g_ptr_array_index(g_wins, i);
        if (w->kind == W_FIND) {
            g_signal_handlers_block_matched(w->f_hl, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, w);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->f_hl), g_highlight);
            g_signal_handlers_unblock_matched(w->f_hl, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, w);
            render_find(w);
        }
    }
}
static void on_hl_check(GtkToggleButton *b, gpointer data) { (void)data; if (gtk_toggle_button_get_active(b) != g_highlight) toggle_highlight(); }

static void choose_colour(swin *w)
{
    GtkWidget *d = gtk_color_chooser_dialog_new("Colour Find Text", GTK_WINDOW(w->win));
    GdkRGBA c;
    guint i;
    gdk_rgba_parse(&c, g_findcol);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(d), &c);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(d), &c);
        snprintf(g_findcol, sizeof g_findcol, "#%02X%02X%02X", (int)(c.red * 255 + .5), (int)(c.green * 255 + .5), (int)(c.blue * 255 + .5));
        settings_save();
        for (i = 0; i < g_wins->len; i++) {
            swin *x = g_ptr_array_index(g_wins, i);
            GtkTextTag *t = x->buf ? gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(x->buf), "hl") : NULL;
            if (t) g_object_set(t, "background", g_findcol, NULL);
        }
    }
    gtk_widget_destroy(d);
}

static void on_browse(GtkWidget *x, gpointer data)
{
    swin *w = data;
    GtkWidget *d = gtk_file_chooser_dialog_new("Select a sysreport folder", GTK_WINDOW(w->win), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                               "_Cancel", GTK_RESPONSE_CANCEL, "_Select", GTK_RESPONSE_ACCEPT, NULL);
    (void)x;
    if (srs_reports_root()) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(d), srs_reports_root());
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
        char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
        gtk_entry_set_text(GTK_ENTRY(w->f_report), f);
        g_free(f);
    }
    gtk_widget_destroy(d);
}

static void on_latest(GtkWidget *x, gpointer data)
{
    swin *w = data;
    char rep[1100] = "";
    (void)x;
    srs_latest_report(rep, sizeof rep);
    gtk_entry_set_text(GTK_ENTRY(w->f_report), rep);
}

/* ======================================================================
 * CPU Burnin / CPU Stress window
 * ====================================================================== */
static void render_burn(swin *w)
{
    srs_out o;
    gctx g;
    out_begin_live(w, &o, &g, NULL);
    burn_render(&o, &w->st);
}

/* A run has just ended (Pass, Fail or Stopped): keep its summary in ~/.cache/srs/burnin-*.txt. */
static void burn_saved(swin *w)
{
    char path[1100];
    if (w->st.state != BURN_PASS && w->st.state != BURN_FAIL && w->st.state != BURN_STOPPED) return;
    if (burn_save_summary(&w->st, path, sizeof path) == 0 && !strstr(w->st.status, "saved to")) {
        const char *base = strrchr(path, '/');
        size_t n = strlen(w->st.status);
        snprintf(w->st.status + n, sizeof w->st.status - n, " - saved to ~/.cache/srs/%s", base ? base + 1 : path);
    }
}

static gboolean burn_timer(gpointer data)
{
    swin *w = data;
    int k, was = w->st.state;
    burn_tick(&w->st);
    if (was == BURN_RUNNING || was == BURN_COOLING) burn_saved(w);
    if (w->nhist == 600) { for (k = 0; k < 4; k++) memmove(w->hist[k], w->hist[k] + 1, sizeof(double) * 599); w->nhist--; }
    w->hist[0][w->nhist] = w->st.cur.cpu_load;
    w->hist[1][w->nhist] = w->st.cur.cpu_temp;
    w->hist[2][w->nhist] = w->st.cur.sio_temp;
    w->hist[3][w->nhist] = w->st.cur.gpu_temp;
    w->nhist++;
    render_burn(w);
    gtk_widget_set_sensitive(w->b_start, !burn_active());
    gtk_widget_set_sensitive(w->b_stop, burn_active());
    gtk_widget_queue_draw(w->plot);
    return G_SOURCE_CONTINUE;
}

static gboolean draw_plot(GtkWidget *wid, cairo_t *cr, gpointer data)
{
    swin *w = data;
    int W = gtk_widget_get_allocated_width(wid), H = gtk_widget_get_allocated_height(wid), k, i;
    static const double col[4][3] = { { 0, .63, 0 }, { .88, 0, 0 }, { 1, .53, 0 }, { 0, .2, .8 } };
    double lift = g_dark ? .35 : 0;   /* dark mode: lines mixed toward white so they stay visible */
#define PLOT_RGB(r, g, b) cairo_set_source_rgb(cr, (r) + (1 - (r)) * lift, (g) + (1 - (g)) * lift, (b) + (1 - (b)) * lift)
    if (g_dark) cairo_set_source_rgb(cr, .12, .12, .12); else cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    if (g_dark) cairo_set_source_rgb(cr, .3, .3, .3); else cairo_set_source_rgb(cr, .85, .85, .85);
    for (i = 1; i < 4; i++) { cairo_move_to(cr, 0, H * i / 4.0); cairo_line_to(cr, W, H * i / 4.0); }
    cairo_stroke(cr);
    if (w->kind == W_BURN) {
        for (k = 0; k < 4; k++) {
            PLOT_RGB(col[k][0], col[k][1], col[k][2]);
            for (i = 0; i < w->nhist; i++) {
                double x = W - (w->nhist - 1 - i) * 2.0, y = H - w->hist[k][i] / 100.0 * H;
                if (i == 0) cairo_move_to(cr, x, y); else cairo_line_to(cr, x, y);
            }
            cairo_stroke(cr);
        }
        if (g_dark) cairo_set_source_rgb(cr, .75, .75, .75); else cairo_set_source_rgb(cr, .3, .3, .3);
        cairo_move_to(cr, 6, 14);
        cairo_show_text(cr, "green CPU load %   red hottest core C   orange SIO C   blue GPU C   (scale 0-100)");
    } else if (w->sub == BK_IPI) {
        int n;
        const double *m = bench_ipi_last(&n);
        double mx = 1;
        if (m) {
            for (i = 0; i < n * n; i++) if (m[i] > mx) mx = m[i];
            for (i = 0; i < n; i++)
                for (k = 0; k < n; k++) {
                    double v = i == k ? 0 : m[i * n + k] / mx;
                    cairo_set_source_rgb(cr, v, 1 - v * 0.8, 0.2);
                    cairo_rectangle(cr, k * (double)W / n, i * (double)H / n, (double)W / n - 1, (double)H / n - 1);
                    cairo_fill(cr);
                }
        }
    } else if (w->sub == BK_LAT) {
        const long *sz;
        const double *ns;
        int n = bench_latency_last(&sz, &ns);
        double mx = 1;
        for (i = 0; i < n; i++) if (ns[i] > mx) mx = ns[i];
        PLOT_RGB(0, .2, .8);
        for (i = 0; i < n; i++) {
            double x = n > 1 ? i * (W - 20.0) / (n - 1) + 10 : 10, y = H - 10 - ns[i] / mx * (H - 20);
            if (i == 0) cairo_move_to(cr, x, y); else cairo_line_to(cr, x, y);
        }
        cairo_stroke(cr);
    }
#undef PLOT_RGB
    return FALSE;
}

/* before starting: the GPU load command's program must exist (a typo otherwise fails silently) */
static int gpu_cmd_ok(swin *w, const char *cmd)
{
    static const char *const builtins[] = { "exec", "command", "time", "nohup", "env", "cd", ".", "source", "{", "(", NULL };
    char **argv = NULL, *path = NULL, prog[512] = "";
    int argc = 0, ok = 1, i;
    if (!g_shell_parse_argv(cmd, &argc, &argv, NULL) || argc < 1) {
        snprintf(prog, sizeof prog, "%s", cmd);
        ok = 0;
    } else {
        int a = 0;
        while (a < argc && strchr(argv[a], '=') && argv[a][0] != '=') a++;   /* VAR=value prefixes */
        if (a < argc) {
            snprintf(prog, sizeof prog, "%s", argv[a]);
            for (i = 0; builtins[i]; i++) if (!strcmp(prog, builtins[i])) break;
            if (!builtins[i]) {
                if (strchr(prog, '/')) ok = g_file_test(prog, G_FILE_TEST_IS_EXECUTABLE) && !g_file_test(prog, G_FILE_TEST_IS_DIR);
                else ok = (path = g_find_program_in_path(prog)) != NULL;
            }
        }
    }
    g_free(path);
    g_strfreev(argv);
    if (!ok) {
        GtkWidget *m = gtk_message_dialog_new(GTK_WINDOW(w->win), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "GPU load command not found:\n\n    %s\n\nCheck the spelling, or untick \"GPU Load command\" to run without it. "
            "Nothing was started.", prog);
        gtk_window_set_title(GTK_WINDOW(m), "GPU load command");
        gtk_dialog_run(GTK_DIALOG(m));
        gtk_widget_destroy(m);
    }
    return ok;
}

/* one benchmark at a time across all windows, and no burner while one runs: two loads at once
   would spoil each other's results */
static int g_bench_busy;

static void on_burn_start(GtkWidget *x, gpointer data)
{
    swin *w = data;
    burn_cfg c;
    int i, n = 0;
    (void)x;
    if (g_bench_busy) {
        GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(w->win), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                              "A benchmark is running. Start the burner when it has finished.");
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        return;
    }
    burn_cfg_default(&c);
    for (i = 0; i < w->nb_cpu; i++) { c.enabled[i] = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->b_cpu[i])); n += c.enabled[i]; }
    c.mode = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->b_same)) ? BURN_SAME : BURN_SWAP;
    gtk_spin_button_update(GTK_SPIN_BUTTON(w->b_limit));
    c.temp_limit_c = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->b_limit));
    if (w->sub == 0) {
        double t;
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->b_time));
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->b_cool));
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->b_cycles));
        t = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->b_time));
        c.burn_s = t * (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->b_hours)) ? 3600 : 60);
        c.cool_s = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->b_cool));
        c.cycles = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w->b_cycles));
        if (c.burn_s <= 0) c.burn_s = 60;
    } else {
        c.burn_s = 0;   /* CPU Stress: until stopped (capped) */
        c.cool_s = 0;
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->b_gpu))) {
        snprintf(g_gpucmd, sizeof g_gpucmd, "%s", gtk_entry_get_text(GTK_ENTRY(w->b_gpucmd)));
        settings_save();
        c.gpu_cmd = *g_gpucmd ? g_gpucmd : NULL;
        if (c.gpu_cmd && !gpu_cmd_ok(w, c.gpu_cmd)) return;
    }
    if (!n) { gtk_widget_error_bell(w->win); return; }
    if (!getenv("SRS_TEST_NOCONFIRM")) {   /* SIV-style warning before making CPUs 100% busy */
        char msg[700];
        GtkWidget *m;
        int ok;
        snprintf(msg, sizeof msg, "%s will make %d CPU%s 100%% busy %s.\n\nThe run stops automatically if the hottest CPU sensor "
                 "reaches %.0f°C, or on Stop / closing this window.%s\n\nStart now?", w->sub ? "CPU Stress" : "CPU Burnin", n, n == 1 ? "" : "s",
                 w->sub ? "until you press Stop" : "for the configured burn time", c.temp_limit_c,
                 c.gpu_cmd ? "\nThe GPU load command will run too." : "");
        m = gtk_message_dialog_new(GTK_WINDOW(w->win), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK_CANCEL, "%s", msg);
        gtk_window_set_title(GTK_WINDOW(m), "Start CPU load?");
        ok = gtk_dialog_run(GTK_DIALOG(m)) == GTK_RESPONSE_OK;
        gtk_widget_destroy(m);
        if (!ok) return;
    }
    if (getenv("SRS_TEST_MAXSECS")) c.cap_s = atof(getenv("SRS_TEST_MAXSECS"));   /* test hook: hard cap */
    w->nhist = 0;
    burn_start(&c);
    burn_timer(w);
}

static void on_burn_stop(GtkWidget *x, gpointer data) { (void)x; burn_stop(); burn_timer(data); }
static void on_burn_all(GtkWidget *x, gpointer data)
{
    swin *w = data;
    int i, on = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(x), "on"));
    for (i = 0; i < w->nb_cpu; i++) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->b_cpu[i]), on);
}

static GtkWidget *spin(double lo, double hi, double step, double val)
{
    GtkWidget *s = gtk_spin_button_new_with_range(lo, hi, step);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), val);
    return s;
}

static void build_burn(swin *w, GtkWidget *vb)
{
    GtkWidget *grid = gtk_grid_new(), *cpus = gtk_flow_box_new(), *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), *b, *swap;
    int i, ncpu = burn_ncpu();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    i = 0;
    if (w->sub == 0) {
        gtk_grid_attach(GTK_GRID(grid), lbl("Burn Time"), 0, 0, 1, 1);
        w->b_time = spin(0, 750, 1, 15);
        gtk_grid_attach(GTK_GRID(grid), w->b_time, 1, 0, 1, 1);
        w->b_hours = gtk_check_button_new_with_label("Hours (otherwise Minutes)");
        gtk_grid_attach(GTK_GRID(grid), w->b_hours, 2, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), lbl("Cool Time"), 3, 0, 1, 1);
        w->b_cool = spin(0, 900, 5, 60);
        gtk_grid_attach(GTK_GRID(grid), w->b_cool, 4, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), lbl("Seconds   Cycles"), 5, 0, 1, 1);
        w->b_cycles = spin(1, 99, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), w->b_cycles, 6, 0, 1, 1);
        i = 1;
    }
    gtk_grid_attach(GTK_GRID(grid), lbl("Temperature limit °C"), 0, i, 1, 1);
    w->b_limit = spin(50, 100, 1, 90);
    gtk_grid_attach(GTK_GRID(grid), w->b_limit, 1, i, 1, 1);
    w->b_same = gtk_radio_button_new_with_label(NULL, "Same CPU (Whetstone)");
    swap = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(w->b_same), "Swap CPU (affinity test)");
    gtk_grid_attach(GTK_GRID(grid), w->b_same, 2, i, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), swap, 3, i, 2, 1);
    w->b_gpu = gtk_check_button_new_with_label("GPU Load command:");
    w->b_gpucmd = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(w->b_gpucmd), g_gpucmd);
    gtk_entry_set_placeholder_text(GTK_ENTRY(w->b_gpucmd), "e.g. gpu-burn-rocm 600  (none by default)");
    gtk_widget_set_hexpand(w->b_gpucmd, TRUE);
    gtk_grid_attach(GTK_GRID(grid), w->b_gpu, 0, i + 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w->b_gpucmd, 1, i + 1, 6, 1);
    gtk_box_pack_start(GTK_BOX(vb), grid, FALSE, FALSE, 0);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(cpus), 16);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(cpus), GTK_SELECTION_NONE);
    w->nb_cpu = ncpu;
    for (i = 0; i < ncpu; i++) {
        char n[32];
        snprintf(n, sizeof n, "CPU-%d", i);
        w->b_cpu[i] = gtk_check_button_new_with_label(n);
        gtk_widget_set_tooltip_text(w->b_cpu[i], "Make this CPU busy or idle");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->b_cpu[i]), TRUE);
        gtk_container_add(GTK_CONTAINER(cpus), w->b_cpu[i]);
    }
    gtk_box_pack_start(GTK_BOX(vb), cpus, FALSE, FALSE, 0);
    b = sbtn("_Select All"); g_object_set_data(G_OBJECT(b), "on", GINT_TO_POINTER(1)); g_signal_connect(b, "clicked", G_CALLBACK(on_burn_all), w);
    gtk_widget_set_tooltip_text(b, "Select all the Worker Threads");
    gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
    b = sbtn("_Deselect All"); g_object_set_data(G_OBJECT(b), "on", GINT_TO_POINTER(0)); g_signal_connect(b, "clicked", G_CALLBACK(on_burn_all), w);
    gtk_widget_set_tooltip_text(b, "Deselect all the Worker Threads");
    gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
    w->b_start = sbtn("S_tart");
    g_signal_connect(w->b_start, "clicked", G_CALLBACK(on_burn_start), w);
    gtk_box_pack_start(GTK_BOX(row), w->b_start, FALSE, FALSE, 0);
    w->b_stop = sbtn("St_op");
    g_signal_connect(w->b_stop, "clicked", G_CALLBACK(on_burn_stop), w);
    gtk_box_pack_start(GTK_BOX(row), w->b_stop, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), lbl(w->sub ? "  CPU Stress: worker threads stay busy until Stop" :
                                         "  CPU Burnin: burn / cool cycles, Pass or Fail at the end"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), row, FALSE, FALSE, 0);
}

/* ======================================================================
 * Benchmark / IPI / Latency window (runs on a worker thread, never automatically)
 * ====================================================================== */
typedef struct {
    swin *w;
    int sub, mode, cpus, rounds, threads, algo, rc, profile, allow_usb, stopped;
    int isa, single;                      /* CPU Compute: code path, 1 thread only (Everyday Work too) */
    unsigned groups, libs;                /* Everyday Work: test groups, compression libraries */
    double secs, limit, size_mb;
    long max;
    size_t digits;
    char *dir, why[300];
    volatile int cancel;
} benchjob;

/* the result file (bench-<name>.txt), window title and guide item of each benchmark window */
static const char *bk_result(int sub, int mode)
{
    switch (sub) {
    case BK_IPI: return "ipi-delay";
    case BK_LAT: return "latency";
    case BK_PI: return "pi";
    case BK_CRYPTO: return "crypto";
    case BK_PRIME: return "prime";
    case BK_CACHEMEM: return "cachemem";
    case BK_STORAGE: return "storage";
    case BK_OSLAT: return "os-latency";
    case BK_COMPUTE: return "compute";
    case BK_WORK: return "work";
    default: return mode < 0 ? "benchmark" : bench_mode_id((bench_mode)mode);
    }
}
static const char *bk_title(int sub)
{
    static const char *const t[] = { "Benchmark", "IPI Delay", "Latency", "SRS Pi", "Crypto Benchmark", "Prime Sieve", "Cache & Memory", "Storage Benchmark",
                                       "OS Latency", "CPU Compute", "Everyday Work" };
    return sub >= 0 && sub <= BK_LAST ? t[sub] : "Benchmark";
}
static const char *bk_item(int sub)
{
    static const char *const t[] = { "benchmark", "ipi-delay", "latency", "srs-pi", "crypto-bench", "prime-sieve", "cache-memory", "storage-bench",
                                       "os-latency", "cpu-compute", "everyday-work" };
    return sub >= 0 && sub <= BK_LAST ? t[sub] : "benchmark";
}

static gboolean bench_done(gpointer data)
{
    benchjob *j = data;
    swin *w = j->w;
    g_bench_busy = 0;
    if (g_ptr_array_find(g_wins, w, NULL)) {
        srs_out o;
        gctx g;
        const char *name = bk_result(j->sub, j->mode);
        w->running = 0;
        w->cancelp = NULL;
        if (w->timer) { g_source_remove(w->timer); w->timer = 0; }
        gtk_widget_set_sensitive(w->bm_run, TRUE);
        if (w->bm_stop) gtk_widget_set_sensitive(w->bm_stop, FALSE);
        if (j->sub == BK_STORAGE) {
            char b[400];
            snprintf(b, sizeof b, "%s%s", j->rc == 0 ? "Finished" : j->rc == 1 ? "Stopped - the rows not measured stay empty" :
                                          j->rc == -1 ? "Not started: " : "Finished with an error: ", j->rc < 0 ? j->why : "");
            gtk_label_set_text(GTK_LABEL(w->bm_status), b);
        } else if (j->stopped) gtk_label_set_text(GTK_LABEL(w->bm_status), "Stopped - the empty cells were not measured");
        else if (j->rc == -4) {
            char b[400];
            snprintf(b, sizeof b, "Not started: %s", j->why);
            gtk_label_set_text(GTK_LABEL(w->bm_status), b);
        }
        else
        gtk_label_set_text(GTK_LABEL(w->bm_status), j->rc == -2 ? "Stopped - no result" : j->rc == -3 ? "Not enough memory for this size" :
                                                    j->rc == 1 ? "Finished - the result is WRONG (see Check)" : "Finished");
        out_begin(w, &o, &g, NULL);
        if (j->sub == BK_STORAGE) {
            if (!bench_show_last(name, &o)) so_note(&o, "No result yet.");
        } else if (j->rc == -4) {
            char b[400];
            snprintf(b, sizeof b, "Not started: %s. The last finished result is below.", j->why);
            so_err(&o, b);
            so_par(&o);
            if (!bench_show_last(name, &o)) so_note(&o, "No finished run yet.");
        } else if (j->rc == -2 || j->rc == -3) {
            so_err(&o, j->rc == -2 ? "Stopped before the end - this run has no result. The last finished result is below."
                                   : "Not enough memory for this size - try a smaller one. The last finished result is below.");
            so_par(&o);
            if (!bench_show_last(name, &o)) so_note(&o, "No finished run yet.");
        } else if (!bench_show_last(name, &o)) so_err(&o, "No result (nothing to run in this mode on this CPU)");
        if (w->plot) gtk_widget_queue_draw(w->plot);
    }
    g_free(j->dir);
    g_free(j);
    return G_SOURCE_REMOVE;
}

static gpointer bench_worker(gpointer data)
{
    benchjob *j = data;
    if (j->sub == BK_IPI) bench_ipi(j->cpus, j->rounds, NULL);
    else if (j->sub == BK_LAT) bench_latency(j->max, NULL);
    else if (j->sub == BK_PI) j->rc = bench_pi(j->digits, j->threads, j->algo, NULL, &j->cancel, NULL);
    else if (j->sub == BK_CRYPTO) j->rc = bench_crypto(j->threads, j->secs, NULL);
    else if (j->sub == BK_PRIME) j->rc = bench_prime((uint64_t)j->limit, j->threads, NULL, &j->cancel);
    else if (j->sub == BK_CACHEMEM) {
        cm_cfg c;
        cm_cfg_default(&c);
        c.threads = j->threads;
        c.seconds = j->secs;
        j->rc = bench_cachemem(&c, NULL, &j->cancel);
        if (j->rc == 1) { j->rc = 0; j->stopped = 1; }   /* stopped: the partial result is shown, marked as stopped */
    } else if (j->sub == BK_STORAGE) {
        storage_cfg c;
        memset(&c, 0, sizeof c);
        c.dir = j->dir; c.size_mb = j->size_mb; c.secs = j->secs; c.profile = j->profile; c.allow_usb = j->allow_usb;
        j->rc = bench_storage(&c, NULL, &j->cancel, j->why, sizeof j->why);
    } else if (j->sub == BK_OSLAT) {
        os_cfg c;
        os_cfg_default(&c);
        c.samples = j->max;
        c.jitter_secs = j->secs;
        j->rc = bench_os(&c, NULL, NULL, &j->cancel);
        if (j->rc == 1) { j->rc = 0; j->stopped = 1; }   /* stopped: the partial result is shown, marked as stopped */
        else if (j->rc < 0) { j->rc = -4; snprintf(j->why, sizeof j->why, "not available on this system, or out of memory"); }
    } else if (j->sub == BK_COMPUTE) {
        cp_cfg c;
        cp_cfg_default(&c);
        c.threads = j->threads;
        c.seconds = j->secs;
        c.isa = j->isa;
        c.single_only = j->single;
        j->rc = bench_compute(&c, NULL, &j->cancel);
        if (j->rc == -2) { j->rc = 0; j->stopped = 1; }
        else if (j->rc == -1) { j->rc = -4; snprintf(j->why, sizeof j->why, "CPU Burnin / Stress is running, or out of memory"); }
    } else if (j->sub == BK_WORK) {
        wk_cfg c;
        wk_cfg_default(&c);
        c.threads = j->threads;
        c.seconds = j->secs;
        c.run_mask = j->groups;
        c.codec_mask = j->libs;
        c.single_only = j->single;
        j->rc = bench_work(&c, NULL, &j->cancel);
        if (j->rc == -2) { j->rc = 0; j->stopped = 1; }
        else if (j->rc == -1) { j->rc = -4; snprintf(j->why, sizeof j->why, "CPU Burnin / Stress is running, or out of memory"); }
    }
    else bench_run((bench_mode)j->mode, j->secs, NULL);
    g_idle_add(bench_done, j);
    return NULL;
}

/* status line while a long benchmark runs: elapsed time and, for SRS Pi, the current step */
static gboolean bench_tick(gpointer data)
{
    swin *w = data;
    char b[200];
    double el = g_get_monotonic_time() / 1e6 - w->run_t0;
    if (w->sub == BK_PI) snprintf(b, sizeof b, "Running %.0f s: %s", el, pi_stage());
    else if (w->sub == BK_CACHEMEM) snprintf(b, sizeof b, "Running %.0f s: %s", el, cm_stage());
    else if (w->sub == BK_STORAGE) snprintf(b, sizeof b, "Running %.0f s: %s", el, storage_stage());
    else if (w->sub == BK_OSLAT) snprintf(b, sizeof b, "Running %.0f s: %s", el, os_stage());
    else if (w->sub == BK_COMPUTE) snprintf(b, sizeof b, "Running %.0f s: %s", el, cp_stage());
    else if (w->sub == BK_WORK) snprintf(b, sizeof b, "Running %.0f s: %s", el, wk_stage());
    else snprintf(b, sizeof b, "Running %.0f s...", el);
    gtk_label_set_text(GTK_LABEL(w->bm_status), b);
    return G_SOURCE_CONTINUE;
}

static const double prime_limits[] = { 1e8, 1e9, 1e10, 1e11 };

static void on_bench_stop(GtkWidget *x, gpointer data)
{
    swin *w = data;
    (void)x;
    if (w->running && w->cancelp) { *w->cancelp = 1; gtk_label_set_text(GTK_LABEL(w->bm_status), "Stopping..."); }
}

static void on_bench_run(GtkWidget *x, gpointer data)
{
    swin *w = data;
    benchjob *j;
    GThread *th;
    (void)x;
    if (w->running || g_bench_busy || burn_active()) {
        gtk_widget_error_bell(w->win);
        gtk_label_set_text(GTK_LABEL(w->bm_status), burn_active() ? "Not started - CPU Burnin / Stress is running"
                                                                  : "Not started - another benchmark is running; wait for it to finish");
        return;
    }
    if (w->sub == BK_CACHEMEM) {
        GtkWidget *d;
        int ok;
        if (!cm_available()) { gtk_label_set_text(GTK_LABEL(w->bm_status), "Not available: needs an x86-64 CPU"); return; }
        d = gtk_message_dialog_new(GTK_WINDOW(w->win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                   "Start the Cache & Memory benchmark?");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d),
            "It keeps the chosen CPU threads and the memory bus busy for about 20 seconds.\n"
            "For accurate results close other programs, and do not run any other benchmark at the same time.");
        ok = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_YES;
        gtk_widget_destroy(d);
        if (!ok || w->running || g_bench_busy || burn_active()) return;
    }
    if (w->sub == BK_OSLAT) {
        GtkWidget *d;
        int ok;
        double n, s;
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->bm_max));
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->bm_secs));
        n = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->bm_max));
        s = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->bm_secs));
        d = gtk_message_dialog_new(GTK_WINDOW(w->win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                   "Start the OS Latency benchmark?");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d),
            "It times system calls, task switches, starting threads and programs, wake-ups, timers, page faults and "
            "small-file operations for about %.0f seconds. Light load: one or two CPUs are busy, never all of them.\n\n"
            "Nothing on the system is changed. The file test works in its own temporary folder in ~/.cache/srs, which is "
            "removed at the end. Close other programs for steadier numbers.", 1 + 2.4 * n / 20000 + s);
        ok = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_YES;
        gtk_widget_destroy(d);
        if (!ok || w->running || g_bench_busy || burn_active()) return;
    }
    if (w->sub == BK_COMPUTE) {
        GtkWidget *d;
        int ok;
        cp_cfg c;
        cp_cfg_default(&c);
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->bm_threads));
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->bm_secs));
        c.threads = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w->bm_threads));
        c.seconds = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->bm_secs));
        c.single_only = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->cp_single));
        d = gtk_message_dialog_new(GTK_WINDOW(w->win), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
                                   "Start the CPU Compute benchmark?");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d),
            "It runs matrix multiply, peak multiply-add, FFT, N-body, Mandelbrot and a small ray tracer for about %.0f seconds, "
            "first on one thread%s.\n\n%s%s"
            "An unstable overclock or undervolt can crash the PC under this kind of load. Tick \"1 thread only\" to keep it light. "
            "Close other programs for accurate results.",
            cp_estimate_seconds(&c), c.single_only ? " only" : c.threads > 1 ? " and then on the chosen threads at once" : "",
            c.single_only ? "" : "With all threads every CPU is 100% busy and gets hot. ",
            cp_isa_best() == CP_ISA_AVX512 ? "This CPU has AVX-512, which draws a lot of power. " : "");
        ok = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_YES;
        gtk_widget_destroy(d);
        if (!ok || w->running || g_bench_busy || burn_active()) return;
    }
    if (w->sub == BK_WORK) {
        GtkWidget *d;
        int ok, i;
        wk_cfg c;
        wk_cfg_default(&c);
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->bm_threads));
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->bm_secs));
        c.threads = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w->bm_threads));
        c.seconds = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->bm_secs));
        c.single_only = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->cp_single));
        for (i = 0; i < WK_NGROUP; i++) if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->wk_grp[i]))) c.run_mask |= 1u << i;
        for (i = 0; i < WK_NLIB; i++) if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->wk_lib[i]))) c.codec_mask |= 1u << i;
        if (!c.run_mask) { gtk_label_set_text(GTK_LABEL(w->bm_status), "Not started: tick at least one test group"); return; }
        if ((c.run_mask >> WK_COMPRESS & 1) && !c.codec_mask) {
            gtk_label_set_text(GTK_LABEL(w->bm_status), "Not started: tick at least one compression library, or untick Compression");
            return;
        }
        d = gtk_message_dialog_new(GTK_WINDOW(w->win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                   "Start the Everyday Work benchmark?");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d),
            "It runs compression, sorting, checksum, text and interpreter tests for about %.0f seconds, first on one thread%s.\n\n"
            "%sThe test data is generated by SRS; none of your files are read or written (only the result is saved). "
            "Close other programs for accurate results.",
            wk_estimate_seconds(&c), c.single_only ? " only" : c.threads > 1 ? " and then on the chosen threads at once" : "",
            c.single_only ? "" : "With all threads every CPU is 100% busy, and xz / zstd -19 use about 100 MB of memory per thread. ");
        ok = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_YES;
        gtk_widget_destroy(d);
        if (!ok || w->running || g_bench_busy || burn_active()) return;
    }
    if (w->sub == BK_STORAGE) {
        static const double sizes[] = { 256, 1024, 4096, 16384 };
        storage_where where;
        GtkWidget *d;
        char *dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(w->st_dir));
        int ok, k = gtk_combo_box_get_active(GTK_COMBO_BOX(w->st_size)), usb = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->st_usb));
        double mb = sizes[k < 0 ? 1 : k];
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->bm_secs));
        if (!dir || !storage_probe(dir, &where) || (where.usb && !usb)) {
            char b[400];
            snprintf(b, sizeof b, "Not started: %s", !dir ? "choose a folder" : where.problem[0] ? where.problem :
                                                       "the folder is on a USB drive - tick Allow USB drives to test it");
            gtk_label_set_text(GTK_LABEL(w->bm_status), b);
            g_free(dir);
            return;
        }
        d = gtk_message_dialog_new(GTK_WINDOW(w->win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                   "Start the storage benchmark on %s?", where.model[0] ? where.model : where.disk);
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d),
            "Connection: %s. Folder: %s (%s).\n\n"
            "SRS writes a new temporary test file of %.0f MB there, then reads and rewrites it for %.0f seconds per test "
            "(6 tests). Your existing files are not touched; the test file has no name while the test runs and is gone at the end, "
            "even if SRS is stopped or crashes.%s",
            where.transport, where.folder, where.fstype, mb, gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->bm_secs)),
            where.rotational ? "\n\nThis is a hard disk: the random tests are slow and noisy." :
            where.usb ? "\n\nThis is a USB drive: some USB enclosures drop off under heavy load. Make sure nothing important is open on it." : "");
        ok = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_YES;
        gtk_widget_destroy(d);
        if (!ok || w->running || g_bench_busy || burn_active()) { g_free(dir); return; }
        {   /* remember the folder for next time */
            char *f = g_build_filename(g_get_home_dir(), ".cache", "srs", "storage-folder.txt", NULL), *d2 = g_path_get_dirname(f);
            g_mkdir_with_parents(d2, 0755);
            g_file_set_contents(f, dir, -1, NULL);
            g_free(d2);
            g_free(f);
        }
        j = g_new0(benchjob, 1);
        j->dir = dir;
        j->size_mb = mb;
        j->secs = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->bm_secs));
        j->profile = gtk_combo_box_get_active(GTK_COMBO_BOX(w->st_profile)) == 1;
        j->allow_usb = usb;
    } else
    j = g_new0(benchjob, 1);
    j->w = w;
    j->sub = w->sub;
    if (w->sub == BK_BENCH) {
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->bm_secs));
        j->mode = gtk_combo_box_get_active(GTK_COMBO_BOX(w->bm_mode));
        j->secs = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->bm_secs));
    } else if (w->sub == BK_IPI) {
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->bm_cpus));
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->bm_rounds));
        j->cpus = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w->bm_cpus));
        j->rounds = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w->bm_rounds));
    } else if (w->sub == BK_LAT) {
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->bm_max));
        j->max = (long)gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->bm_max)) * 1024 * 1024;
    } else if (w->sub == BK_STORAGE) {
        /* filled in above */
    } else if (w->sub == BK_OSLAT) {
        j->max = (long)gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->bm_max));
        j->secs = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->bm_secs));
    } else {
        int k;
        gtk_spin_button_update(GTK_SPIN_BUTTON(w->bm_threads));
        j->threads = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w->bm_threads));
        if (w->sub == BK_PI) {
            int sp = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->bm_superpi)), i, n = 0;
            k = gtk_combo_box_get_active(GTK_COMBO_BOX(w->bm_digits));
            j->digits = 1000000;
            for (i = 0; i < pi_size_count; i++)
                if (pi_size_tab[i].superpi == sp && n++ == k) j->digits = pi_size_tab[i].digits;
            j->algo = gtk_combo_box_get_active(GTK_COMBO_BOX(w->bm_algo)) == 1 ? PI_AGM : PI_CHUDNOVSKY;
        } else if (w->sub == BK_PRIME) {
            k = gtk_combo_box_get_active(GTK_COMBO_BOX(w->bm_limit));
            j->limit = prime_limits[k < 0 ? 1 : k];
        } else if (w->sub == BK_COMPUTE) {
            k = gtk_combo_box_get_active(GTK_COMBO_BOX(w->cp_isa));
            j->isa = k < 0 ? CP_ISA_AUTO : k;
            j->single = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->cp_single));
            j->secs = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->bm_secs));
        } else if (w->sub == BK_WORK) {
            int i;
            j->single = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->cp_single));
            j->secs = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->bm_secs));
            for (i = 0; i < WK_NGROUP; i++) if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->wk_grp[i]))) j->groups |= 1u << i;
            for (i = 0; i < WK_NLIB; i++) if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->wk_lib[i]))) j->libs |= 1u << i;
        } else {   /* crypto, cache & memory */
            gtk_spin_button_update(GTK_SPIN_BUTTON(w->bm_secs));
            j->secs = gtk_spin_button_get_value(GTK_SPIN_BUTTON(w->bm_secs));
        }
    }
    w->running = 1;
    w->cancelp = w->sub == BK_PI || w->sub == BK_PRIME || w->sub == BK_CACHEMEM || w->sub == BK_STORAGE || w->sub == BK_OSLAT || w->sub == BK_COMPUTE || w->sub == BK_WORK ? &j->cancel : NULL;
    w->run_t0 = g_get_monotonic_time() / 1e6;
    gtk_widget_set_sensitive(w->bm_run, FALSE);
    if (w->bm_stop && w->cancelp) gtk_widget_set_sensitive(w->bm_stop, TRUE);
    gtk_label_set_text(GTK_LABEL(w->bm_status), "Running...");
    if (w->sub >= BK_PI && !w->timer) w->timer = g_timeout_add(500, bench_tick, w);
    g_bench_busy = 1;
    th = g_thread_try_new("srs-bench", bench_worker, j, NULL);
    if (th) g_thread_unref(th);
    else {
        g_bench_busy = 0;
        w->running = 0;
        w->cancelp = NULL;
        if (w->timer) { g_source_remove(w->timer); w->timer = 0; }
        gtk_widget_set_sensitive(w->bm_run, TRUE);
        g_free(j->dir);
        g_free(j);
    }
}

/* Storage: show which drive the chosen folder is on (never the serial number) */
static void on_storage_dir(GtkWidget *x, gpointer data)
{
    swin *w = data;
    char *dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(w->st_dir)), b[600];
    storage_where s;
    (void)x;
    if (!w->st_info) return;
    if (!dir) { gtk_label_set_text(GTK_LABEL(w->st_info), "Choose a folder on the drive to test."); return; }
    storage_probe(dir, &s);
    if (s.problem[0]) snprintf(b, sizeof b, "Cannot test here: %s.", s.problem);
    else snprintf(b, sizeof b, "Drive: %s (%s), %s, %.0f GB free.%s", s.model[0] ? s.model : s.disk[0] ? s.disk : "unknown",
                  s.transport[0] ? s.transport : "unknown connection", s.fstype, s.free_bytes / 1e9,
                  s.usb ? " USB drive: tick Allow USB drives to test it." : "");
    gtk_label_set_text(GTK_LABEL(w->st_info), b);
    g_free(dir);
}

/* SRS Pi: fill the Digits list with the decimal sizes or the SuperPi (power-of-two) sizes; 1M stays chosen */
static void on_pi_sizes(GtkWidget *x, gpointer data)
{
    swin *w = data;
    int sp = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->bm_superpi)), i, n = 0, sel = 0;
    (void)x;
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(w->bm_digits));
    for (i = 0; i < pi_size_count; i++) {
        if (pi_size_tab[i].superpi != sp) continue;
        if (pi_size_tab[i].digits == (sp ? 1048576u : 1000000u)) sel = n;
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->bm_digits), pi_size_tab[i].label);
        n++;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(w->bm_digits), sel);
}

static GtkWidget *threads_spin(swin *w, GtkWidget *row, int def, const char *tip)
{
    gtk_box_pack_start(GTK_BOX(row), lbl("Threads"), FALSE, FALSE, 0);
    w->bm_threads = spin(1, burn_ncpu(), 1, def);
    gtk_widget_set_tooltip_text(w->bm_threads, tip);
    gtk_box_pack_start(GTK_BOX(row), w->bm_threads, FALSE, FALSE, 0);
    return w->bm_threads;
}

static void build_bench(swin *w, GtkWidget *vb)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    int m;
    if (w->sub == BK_BENCH) {
        gtk_box_pack_start(GTK_BOX(row), lbl("Mode"), FALSE, FALSE, 0);
        w->bm_mode = gtk_combo_box_text_new();
        for (m = 0; m < BM_COUNT; m++) {
            char l[200];
            snprintf(l, sizeof l, "%s - %s", bench_mode_id((bench_mode)m) + 10, bench_mode_desc((bench_mode)m));
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->bm_mode), l);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(w->bm_mode), 0);
        gtk_box_pack_start(GTK_BOX(row), w->bm_mode, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row), lbl("Seconds per measurement"), FALSE, FALSE, 0);
        w->bm_secs = spin(0.1, 60, 0.1, 1);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(w->bm_secs), 1);
        gtk_box_pack_start(GTK_BOX(row), w->bm_secs, FALSE, FALSE, 0);
    } else if (w->sub == BK_IPI) {
        gtk_box_pack_start(GTK_BOX(row), lbl("CPUs"), FALSE, FALSE, 0);
        w->bm_cpus = spin(2, burn_ncpu(), 1, burn_ncpu());
        gtk_box_pack_start(GTK_BOX(row), w->bm_cpus, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), lbl("Rounds per pair"), FALSE, FALSE, 0);
        w->bm_rounds = spin(1000, 1000000, 1000, 20000);
        gtk_box_pack_start(GTK_BOX(row), w->bm_rounds, FALSE, FALSE, 0);
    } else if (w->sub == BK_LAT) {
        gtk_box_pack_start(GTK_BOX(row), lbl("Largest block (MB)"), FALSE, FALSE, 0);
        w->bm_max = spin(1, 4096, 1, 256);
        gtk_box_pack_start(GTK_BOX(row), w->bm_max, FALSE, FALSE, 0);
    } else if (w->sub == BK_PI) {
        gtk_box_pack_start(GTK_BOX(row), lbl("Digits"), FALSE, FALSE, 0);
        w->bm_digits = gtk_combo_box_text_new();
        gtk_box_pack_start(GTK_BOX(row), w->bm_digits, FALSE, FALSE, 0);
        w->bm_superpi = gtk_check_button_new_with_mnemonic("Super_Pi sizes");
        gtk_widget_set_tooltip_text(w->bm_superpi, "Powers of two, the digit counts SuperPi uses: its \"1M\" is 1,048,576 digits, "
                                                   "its \"32M\" 33,554,432. Off: round decimal sizes (1 million = 1,000,000).");
        g_signal_connect(w->bm_superpi, "toggled", G_CALLBACK(on_pi_sizes), w);
        gtk_box_pack_start(GTK_BOX(row), w->bm_superpi, FALSE, FALSE, 0);
        on_pi_sizes(NULL, w);
        gtk_box_pack_start(GTK_BOX(row), lbl("Method"), FALSE, FALSE, 0);
        w->bm_algo = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->bm_algo), "Chudnovsky (fast, can use threads)");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->bm_algo), "Gauss-Legendre / AGM (SuperPi's method, 1 thread)");
        gtk_combo_box_set_active(GTK_COMBO_BOX(w->bm_algo), 0);
        gtk_box_pack_start(GTK_BOX(row), w->bm_algo, FALSE, FALSE, 0);
        threads_spin(w, row, 1, "1 = single-thread score (like SuperPi). More threads split the series between cores.");
    } else if (w->sub == BK_PRIME) {
        static const char *const lim[] = { "100 million", "1 billion", "10 billion", "100 billion (long)" };
        gtk_box_pack_start(GTK_BOX(row), lbl("Count primes up to"), FALSE, FALSE, 0);
        w->bm_limit = gtk_combo_box_text_new();
        for (m = 0; m < 4; m++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->bm_limit), lim[m]);
        gtk_combo_box_set_active(GTK_COMBO_BOX(w->bm_limit), 2);
        gtk_box_pack_start(GTK_BOX(row), w->bm_limit, FALSE, FALSE, 0);
        threads_spin(w, row, burn_ncpu(), "Threads that share the work. 1 = single-thread score.");
    } else if (w->sub == BK_STORAGE) {
        static const char *const sz[] = { "256 MB", "1 GB", "4 GB", "16 GB" };
        gtk_box_pack_start(GTK_BOX(row), lbl("Folder"), FALSE, FALSE, 0);
        w->st_dir = gtk_file_chooser_button_new("Choose a folder on the drive to test", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
        {   /* the folder of the last run, else the home folder */
            char *last = NULL, *f = g_build_filename(g_get_home_dir(), ".cache", "srs", "storage-folder.txt", NULL);
            if (g_file_get_contents(f, &last, NULL, NULL)) g_strstrip(last);
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(w->st_dir), last && *last && g_file_test(last, G_FILE_TEST_IS_DIR) ? last : g_get_home_dir());
            g_free(last);
            g_free(f);
        }
        gtk_widget_set_tooltip_text(w->st_dir, "The test file goes into this folder, so the drive that holds it is tested. Existing files are never touched.");
        g_signal_connect(w->st_dir, "selection-changed", G_CALLBACK(on_storage_dir), w);
        gtk_box_pack_start(GTK_BOX(row), w->st_dir, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), lbl("Test file"), FALSE, FALSE, 0);
        w->st_size = gtk_combo_box_text_new();
        for (m = 0; m < 4; m++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->st_size), sz[m]);
        gtk_combo_box_set_active(GTK_COMBO_BOX(w->st_size), 1);
        gtk_box_pack_start(GTK_BOX(row), w->st_size, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), lbl("Seconds per test"), FALSE, FALSE, 0);
        w->bm_secs = spin(1, 30, 1, 5);
        gtk_box_pack_start(GTK_BOX(row), w->bm_secs, FALSE, FALSE, 0);
        w->st_profile = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->st_profile), "Standard (4K random Q32T16)");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->st_profile), "Light (4K random Q8T4)");
        gtk_combo_box_set_active(GTK_COMBO_BOX(w->st_profile), 0);
        gtk_widget_set_tooltip_text(w->st_profile, "Q = requests in flight per thread, T = threads. Light uses at most 4 threads.");
        gtk_box_pack_start(GTK_BOX(row), w->st_profile, FALSE, FALSE, 0);
        w->st_usb = gtk_check_button_new_with_mnemonic("Allow _USB drives");
        gtk_widget_set_tooltip_text(w->st_usb, "Off by default: some USB enclosures drop off under heavy load.");
        gtk_box_pack_start(GTK_BOX(row), w->st_usb, FALSE, FALSE, 0);
    } else if (w->sub == BK_OSLAT) {
        gtk_box_pack_start(GTK_BOX(row), lbl("Samples"), FALSE, FALSE, 0);
        w->bm_max = spin(1000, 200000, 1000, 20000);
        gtk_widget_set_tooltip_text(w->bm_max, "Samples for the fast tests; thread / process start, wake-ups and files use a tenth.");
        gtk_box_pack_start(GTK_BOX(row), w->bm_max, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), lbl("Tick test (s)"), FALSE, FALSE, 0);
        w->bm_secs = spin(1, 30, 1, 3);
        gtk_widget_set_tooltip_text(w->bm_secs, "How long a 1 ms periodic timer is watched for late wake-ups.");
        gtk_box_pack_start(GTK_BOX(row), w->bm_secs, FALSE, FALSE, 0);
    } else if (w->sub == BK_COMPUTE) {
        threads_spin(w, row, burn_ncpu(), "The right-hand column uses this many threads (one per CPU); the left column always uses one.");
        gtk_box_pack_start(GTK_BOX(row), lbl("Seconds per test"), FALSE, FALSE, 0);
        w->bm_secs = spin(0.1, 10, 0.1, 0.5);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(w->bm_secs), 1);
        gtk_widget_set_tooltip_text(w->bm_secs, "Measuring time of each of the 9 tests, per column.");
        gtk_box_pack_start(GTK_BOX(row), w->bm_secs, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), lbl("Code"), FALSE, FALSE, 0);
        w->cp_isa = gtk_combo_box_text_new();
        for (m = CP_ISA_AUTO; m < CP_NISA; m++) {
            char l[80];
            snprintf(l, sizeof l, "%s%s", m == CP_ISA_AUTO ? "Best this CPU has: " : cp_isa_name(m),
                     m == CP_ISA_AUTO ? cp_isa_name(cp_isa_best()) : m == CP_ISA_C || cp_isa_ok(m) ? "" : " (not on this CPU)");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->cp_isa), l);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(w->cp_isa), 0);
        gtk_widget_set_tooltip_text(w->cp_isa, "The instruction set for matrix multiply, multiply-add, N-body and Mandelbrot. "
                                               "A choice the CPU lacks falls back to the best it has. FFT and the ray tracer are plain C.");
        gtk_box_pack_start(GTK_BOX(row), w->cp_isa, FALSE, FALSE, 0);
        w->cp_single = gtk_check_button_new_with_mnemonic("_1 thread only");
        gtk_widget_set_tooltip_text(w->cp_single, "Skip the all-threads column: light load, about half the time.");
        gtk_box_pack_start(GTK_BOX(row), w->cp_single, FALSE, FALSE, 0);
    } else if (w->sub == BK_WORK) {
        GtkWidget *r2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        threads_spin(w, row, burn_ncpu(), "The right-hand column uses this many threads (one per CPU); the left column always uses one.");
        gtk_box_pack_start(GTK_BOX(row), lbl("Seconds per test"), FALSE, FALSE, 0);
        w->bm_secs = spin(0.1, 10, 0.1, 0.5);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(w->bm_secs), 1);
        gtk_widget_set_tooltip_text(w->bm_secs, "Measuring time of each test, per column.");
        gtk_box_pack_start(GTK_BOX(row), w->bm_secs, FALSE, FALSE, 0);
        w->cp_single = gtk_check_button_new_with_mnemonic("_1 thread only");
        gtk_widget_set_tooltip_text(w->cp_single, "Skip the all-threads column: light load, about half the time.");
        gtk_box_pack_start(GTK_BOX(row), w->cp_single, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(r2), lbl("Tests"), FALSE, FALSE, 0);
        for (m = 0; m < WK_NGROUP; m++) {
            w->wk_grp[m] = gtk_check_button_new_with_label(wk_group_name[m]);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->wk_grp[m]), TRUE);
            gtk_box_pack_start(GTK_BOX(r2), w->wk_grp[m], FALSE, FALSE, 0);
        }
        gtk_box_pack_start(GTK_BOX(r2), lbl("   Libraries"), FALSE, FALSE, 0);
        for (m = 0; m < WK_NLIB; m++) {
            w->wk_lib[m] = gtk_check_button_new_with_label(wk_lib_id[m]);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->wk_lib[m]), TRUE);
            gtk_widget_set_tooltip_text(w->wk_lib[m], "Compression library of the system, loaded when the test runs; shown as \"not installed\" when missing.");
            gtk_box_pack_start(GTK_BOX(r2), w->wk_lib[m], FALSE, FALSE, 0);
        }
        w->wk_row = r2;   /* goes under the first row (below) */
    } else if (w->sub == BK_CACHEMEM) {
        threads_spin(w, row, burn_ncpu(), "Bandwidth tests use one pinned thread per chosen CPU thread (all = the full result). Latency always uses one.");
        gtk_box_pack_start(GTK_BOX(row), lbl("Seconds per test"), FALSE, FALSE, 0);
        w->bm_secs = spin(0.2, 10, 0.1, 1);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(w->bm_secs), 1);
        gtk_widget_set_tooltip_text(w->bm_secs, "Measuring time for each of the 16 results; the best trial is shown. 1 s gives the same results as 5 s.");
        gtk_box_pack_start(GTK_BOX(row), w->bm_secs, FALSE, FALSE, 0);
    } else {   /* BK_CRYPTO */
        threads_spin(w, row, burn_ncpu(), "The right-hand column uses this many threads; the left column always uses one.");
        gtk_box_pack_start(GTK_BOX(row), lbl("Seconds per measurement"), FALSE, FALSE, 0);
        w->bm_secs = spin(0.1, 10, 0.1, 1);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(w->bm_secs), 1);
        gtk_box_pack_start(GTK_BOX(row), w->bm_secs, FALSE, FALSE, 0);
    }
    w->bm_run = sbtn("_Run");
    g_signal_connect(w->bm_run, "clicked", G_CALLBACK(on_bench_run), w);
    gtk_box_pack_start(GTK_BOX(row), w->bm_run, FALSE, FALSE, 0);
    if (w->sub == BK_PI || w->sub == BK_PRIME || w->sub == BK_CACHEMEM || w->sub == BK_STORAGE || w->sub == BK_OSLAT || w->sub == BK_COMPUTE || w->sub == BK_WORK) {
        w->bm_stop = sbtn("S_top");
        gtk_widget_set_sensitive(w->bm_stop, FALSE);
        g_signal_connect(w->bm_stop, "clicked", G_CALLBACK(on_bench_stop), w);
        gtk_box_pack_start(GTK_BOX(row), w->bm_stop, FALSE, FALSE, 0);
    }
    w->bm_status = lbl(w->sub == BK_STORAGE ? "Not run - press Run" :
                       w->sub == BK_OSLAT ? "Not run - press Run (asks first; one or two CPUs busy for about 6 seconds)" :
                       "Not run - press Run (the CPUs will be busy while it runs)");
    if (w->sub == BK_STORAGE) {   /* the status goes on a second line: the first one is full */
        GtkWidget *r2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_box_pack_start(GTK_BOX(vb), row, FALSE, FALSE, 0);
        w->st_info = lbl("");
        gtk_box_pack_start(GTK_BOX(r2), w->st_info, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(r2), w->bm_status, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vb), r2, FALSE, FALSE, 0);
        on_storage_dir(NULL, w);
        return;
    }
    gtk_box_pack_start(GTK_BOX(row), w->bm_status, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), row, FALSE, FALSE, 0);
    if (w->wk_row) gtk_box_pack_start(GTK_BOX(vb), w->wk_row, FALSE, FALSE, 0);
}

/* ======================================================================
 * tool windows
 * ====================================================================== */
static void open_tool(wkind k, int sub)
{
    static const btn row_tool[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("Me_nu", "!menu"), B("_Help", "help"), B(NULL, NULL) };
    static const btn row_find[] = { B("_OK", "!close"), B("_Copy", "!copy"), B("_Next", "!next"), B("Pre_v", "!prev"),
        B("_Highlight", "!hilite"), B("Co_lour", "!colour"), B("_Lookup", "!idlookup"), B("_Help", "help"), B(NULL, NULL) };
    swin *w = find_win(k, "", sub);
    GtkWidget *vb, *row, *b;
    const char *title;
    char t[200];
    if (w) { gtk_window_present(GTK_WINDOW(w->win)); return; }
    w = g_new0(swin, 1);
    w->kind = k;
    w->sub = sub;
    title = k == W_BURN ? (sub ? "CPU Stress" : "CPU Burnin") : k == W_BENCH ? bk_title(sub)
          : k == W_IDLOOKUP ? "Device ID Lookup" : "Find";
    snprintf(t, sizeof t, "%s - %s", g_image, title);
    make_window(w, t, 1180, k == W_BURN ? 760 : 600);
    vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vb), 4);
    gtk_container_add(GTK_CONTAINER(w->win), vb);
    if (k == W_IDLOOKUP) {
        row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(row), lbl("ID"), FALSE, FALSE, 0);
        w->l_id = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(w->l_id), "1002:7550");
        gtk_entry_set_width_chars(GTK_ENTRY(w->l_id), 26);
        g_signal_connect(w->l_id, "activate", G_CALLBACK(on_lookup_run), w);
        gtk_box_pack_start(GTK_BOX(row), w->l_id, FALSE, FALSE, 0);
        w->l_mode = gtk_check_button_new();
        w->l_state = MODE_TREE;
        g_signal_connect(w->l_mode, "toggled", G_CALLBACK(on_mode), w);
        gtk_box_pack_start(GTK_BOX(row), w->l_mode, FALSE, FALSE, 0);
        w->l_modelbl = lbl(mode_text(MODE_TREE));
        gtk_widget_set_size_request(w->l_modelbl, 330, -1);
        gtk_label_set_xalign(GTK_LABEL(w->l_modelbl), 0);
        gtk_box_pack_start(GTK_BOX(row), w->l_modelbl, FALSE, FALSE, 0);
        w->l_pci = gtk_radio_button_new_with_label(NULL, "PCI");
        w->l_usb = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(w->l_pci), "USB");
        g_signal_connect(w->l_pci, "toggled", G_CALLBACK(on_bus), w);
        g_signal_connect(w->l_usb, "toggled", G_CALLBACK(on_bus), w);
        gtk_box_pack_start(GTK_BOX(row), w->l_pci, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), w->l_usb, FALSE, FALSE, 0);
        b = sbtn("_Lookup");
        g_signal_connect(b, "clicked", G_CALLBACK(on_lookup_run), w);
        gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vb), row, FALSE, FALSE, 0);
    } else if (k == W_FIND) {
        char rep[1100] = "";
        GtkWidget *r2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        w->marks = g_ptr_array_new();
        gtk_box_pack_start(GTK_BOX(row), lbl("Match"), FALSE, FALSE, 0);
        w->f_pat = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(w->f_pat), "RX 9070");
        gtk_entry_set_width_chars(GTK_ENTRY(w->f_pat), 40);
        g_signal_connect(w->f_pat, "activate", G_CALLBACK(on_find_run), w);
        gtk_box_pack_start(GTK_BOX(row), w->f_pat, FALSE, FALSE, 0);
        w->f_icase = gtk_check_button_new_with_mnemonic("Ignore _case");
        w->f_fixed = gtk_check_button_new_with_mnemonic("Li_teral");
        w->f_hl = gtk_check_button_new_with_mnemonic("Highlight _Matches");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->f_hl), g_highlight);
        g_signal_connect(w->f_hl, "toggled", G_CALLBACK(on_hl_check), w);
        gtk_box_pack_start(GTK_BOX(row), w->f_icase, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), w->f_fixed, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), w->f_hl, FALSE, FALSE, 0);
        b = sbtn("_Find");
        g_signal_connect(b, "clicked", G_CALLBACK(on_find_run), w);
        gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vb), row, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(r2), lbl("Max"), FALSE, FALSE, 0);
        w->f_max = spin(1, 100000, 1, 20);
        g_signal_connect(w->f_max, "activate", G_CALLBACK(on_find_run), w);
        gtk_box_pack_start(GTK_BOX(r2), w->f_max, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(r2), lbl("Report"), FALSE, FALSE, 0);
        w->f_report = gtk_entry_new();
        srs_latest_report(rep, sizeof rep);
        gtk_entry_set_text(GTK_ENTRY(w->f_report), rep);
        g_signal_connect(w->f_report, "activate", G_CALLBACK(on_find_run), w);
        gtk_box_pack_start(GTK_BOX(r2), w->f_report, TRUE, TRUE, 0);
        b = sbtn("_Browse...");
        g_signal_connect(b, "clicked", G_CALLBACK(on_browse), w);
        gtk_box_pack_start(GTK_BOX(r2), b, FALSE, FALSE, 0);
        b = sbtn("L_atest");
        g_signal_connect(b, "clicked", G_CALLBACK(on_latest), w);
        gtk_box_pack_start(GTK_BOX(r2), b, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vb), r2, FALSE, FALSE, 0);
    } else if (k == W_BURN) {
        build_burn(w, vb);
    } else {
        build_bench(w, vb);
    }
    gtk_box_pack_start(GTK_BOX(vb), make_view(w), TRUE, TRUE, 0);
    if (k == W_BURN || (k == W_BENCH && (sub == BK_IPI || sub == BK_LAT))) {
        w->plot = gtk_drawing_area_new();
        gtk_widget_set_size_request(w->plot, -1, k == W_BURN ? 160 : 200);
        g_signal_connect(w->plot, "draw", G_CALLBACK(draw_plot), w);
        gtk_box_pack_start(GTK_BOX(vb), w->plot, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(vb), button_row(w, k == W_FIND ? row_find : row_tool), FALSE, FALSE, 0);
    gtk_widget_show_all(w->win);
    if (k == W_IDLOOKUP) { set_mode_box(w); render_idlookup(w); gtk_widget_grab_focus(w->l_id); }
    else if (k == W_FIND) { render_find(w); gtk_widget_grab_focus(w->f_pat); }
    else if (k == W_BURN) { burn_timer(w); w->timer = g_timeout_add(1000, burn_timer, w); }
    else {
        srs_out o;
        gctx g;
        const char *name = bk_result(sub, -1);
        out_begin(w, &o, &g, NULL);
        if (sub == BK_BENCH) srs_section_render(srs_section_find("benchmark"), &o);
        else if (!bench_show_last(name, &o)) so_note(&o, "Not run yet. Press Run.");
    }
}

/* ======================================================================
 * buttons and keys
 * ====================================================================== */
/* item whose guide section belongs to this window */
static const char *guide_item(const swin *w)
{
    switch (w->kind) {
    case W_IDLOOKUP: return "id-lookup";
    case W_FIND: return "find";
    case W_BURN: return w->sub ? "cpu-stress" : "burnin";
    case W_BENCH: return bk_item(w->sub);
    default: return w->id;
    }
}

static void open_guide(swin *w)
{
    char msg[4000];
    const char *id = w ? guide_item(w) : NULL;
    if (id && !srs_guide_anchor(id)) id = NULL;   /* items without a section: the guide's start */
    if (!srs_guide_open(id, getenv("SRS_GUIDE_DRY_RUN") != NULL, msg, sizeof msg) || *msg) {
        GtkWidget *d = gtk_message_dialog_new(w ? GTK_WINDOW(w->win) : NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s",
                                              *msg ? msg : "Could not open the user guide.");
        gtk_window_set_title(GTK_WINDOW(d), "SRS User Guide");
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
    }
}

static void do_target(swin *w, const char *t)
{
    if (!t) return;
    if (t[0] != '!') { open_section(t); return; }
    if (!strcmp(t, "!close")) gtk_widget_destroy(w->win);
    else if (!strcmp(t, "!copy")) copy_all(w);
    else if (!strcmp(t, "!guide")) open_guide(w);
    else if (!strcmp(t, "!refresh")) { if (w->kind == W_SECTION) render(w); }
    else if (!strcmp(t, "!menu")) popup_menu(w->win);
    else if (!strcmp(t, "!main")) { if (g_main) gtk_window_present(GTK_WINDOW(g_main->win)); }
    else if (!strcmp(t, "!zero")) { if (sensors_zero) sensors_zero(); render(w); }
    else if (!strcmp(t, "!hide")) { g_hide_unused = !g_hide_unused; if (sensors_set_hide_unused) sensors_set_hide_unused(g_hide_unused); render(w); }
    else if (!strcmp(t, "!burn")) open_tool(W_BURN, 0);
    else if (!strcmp(t, "!stress")) open_tool(W_BURN, 1);
    else if (!strcmp(t, "!bench")) open_tool(W_BENCH, BK_BENCH);
    else if (!strcmp(t, "!idlookup")) open_tool(W_IDLOOKUP, 0);
    else if (!strcmp(t, "!next")) goto_match(w, +1);
    else if (!strcmp(t, "!prev")) goto_match(w, -1);
    else if (!strcmp(t, "!hilite")) toggle_highlight();
    else if (!strcmp(t, "!colour")) choose_colour(w);
}

static void on_btn(GtkButton *b, gpointer data) { do_target(data, g_object_get_data(G_OBJECT(b), "target")); }

static gboolean on_key(GtkWidget *wid, GdkEventKey *ev, gpointer data)
{
    swin *w = data, *f;
    int ctrl = (ev->state & GDK_CONTROL_MASK) != 0, shift = (ev->state & GDK_SHIFT_MASK) != 0;
    (void)wid;
    if (w->kind == W_HOME) return home_key(w, ev);
    switch (ev->keyval) {
    case GDK_KEY_Escape: if (!w->is_main) gtk_widget_destroy(w->win); return TRUE;
    case GDK_KEY_F1: if (shift) open_section("help"); else open_guide(w); return TRUE;   /* Shift+F1: Help panel */
    case GDK_KEY_F5: if (w->kind == W_SECTION) render(w); return TRUE;
    case GDK_KEY_F3:
        f = w->kind == W_FIND ? w : find_win(W_FIND, "", 0);
        if (f) goto_match(f, shift ? -1 : +1);
        return TRUE;
    default: break;
    }
    if (ctrl) {
        switch (ev->keyval) {
        case GDK_KEY_f: open_tool(W_FIND, 0); return TRUE;
        case GDK_KEY_l: open_tool(W_IDLOOKUP, 0); return TRUE;
        case GDK_KEY_d: open_section("all-dev"); return TRUE;
        case GDK_KEY_h: toggle_highlight(); return TRUE;
        case GDK_KEY_m: popup_menu(w->win); return TRUE;
        case GDK_KEY_s: save_dialog(0); return TRUE;
        default: break;
        }
    }
    return FALSE;
}


/* ======================================================================
 * System tray (SIV lives in the Icon Tray). StatusNotifierItem through libayatana-appindicator,
 * loaded at run time so it stays optional; KDE/GNOME (with the AppIndicator extension)/XFCE show it.
 * ====================================================================== */
typedef void *(*ai_new_t)(const char *, const char *, int);
typedef void (*ai_status_t)(void *, int);
typedef void (*ai_menu_t)(void *, GtkMenu *);
typedef void (*ai_title_t)(void *, const char *);
typedef void (*ai_target_t)(void *, GtkWidget *);
static struct {
    void *lib, *ind;
    ai_new_t new_; ai_status_t status; ai_menu_t menu; ai_title_t title; ai_target_t target;
    GtkWidget *showitem;
    int hidden, tried;
    guint timer;
} g_tray;

static int tray_available(void)
{
    static const char *const libs[] = { "libayatana-appindicator3.so.1", "libappindicator3.so.1", NULL };
    int i;
    if (g_tray.lib) return 1;
    if (g_tray.tried) return 0;
    g_tray.tried = 1;
    for (i = 0; libs[i] && !g_tray.lib; i++) g_tray.lib = dlopen(libs[i], RTLD_NOW | RTLD_LOCAL);
    if (!g_tray.lib) return 0;
    *(void **)&g_tray.new_ = dlsym(g_tray.lib, "app_indicator_new");
    *(void **)&g_tray.status = dlsym(g_tray.lib, "app_indicator_set_status");
    *(void **)&g_tray.menu = dlsym(g_tray.lib, "app_indicator_set_menu");
    *(void **)&g_tray.title = dlsym(g_tray.lib, "app_indicator_set_title");
    *(void **)&g_tray.target = dlsym(g_tray.lib, "app_indicator_set_secondary_activate_target");
    if (!g_tray.new_ || !g_tray.status || !g_tray.menu) { dlclose(g_tray.lib); g_tray.lib = NULL; return 0; }
    return 1;
}

static int tray_active(void) { return g_tray.ind && g_opt_tray; }

static void srs_hide_all(void)
{
    guint i;
    if (!tray_active()) return;   /* never hide without a way back */
    for (i = 0; i < g_wins->len; i++) gtk_widget_hide(((swin *)g_ptr_array_index(g_wins, i))->win);
    g_tray.hidden = 1;
    if (g_tray.showitem) gtk_menu_item_set_label(GTK_MENU_ITEM(g_tray.showitem), "Show SRS");
}

static void srs_show_all(void)
{
    guint i;
    for (i = 0; i < g_wins->len; i++) gtk_widget_show(((swin *)g_ptr_array_index(g_wins, i))->win);
    if (g_main) gtk_window_present(GTK_WINDOW(g_main->win));
    g_tray.hidden = 0;
    if (g_tray.showitem) gtk_menu_item_set_label(GTK_MENU_ITEM(g_tray.showitem), "Hide to Tray");
}

static void on_tray_toggle(GtkMenuItem *mi, gpointer d) { (void)mi; (void)d; if (g_tray.hidden) srs_show_all(); else srs_hide_all(); }
static void on_tray_open(GtkMenuItem *mi, gpointer d)
{
    const char *t = d;
    (void)mi;
    if (!strcmp(t, "burnin")) open_tool(W_BURN, 0);
    else if (!strcmp(t, "stress")) open_tool(W_BURN, 1);
    else if (!strcmp(t, "bench")) open_tool(W_BENCH, BK_BENCH);
    else if (!strcmp(t, "find")) open_tool(W_FIND, 0);
    else open_item(t);
}
static void on_tray_exit(GtkMenuItem *mi, gpointer d) { (void)mi; (void)d; if (g_main) gtk_widget_destroy(g_main->win); }

static void tray_build_menu(void)
{
    static const struct { const char *label, *target; } quick[] = {
        { "Sensors", "sensors" }, { "Processors", "processors" }, { "CPU Burnin...", "burnin" }, { "CPU Stress...", "stress" },
        { "Benchmark...", "bench" }, { "Find...", "find" }, { NULL, NULL } };
    GtkWidget *m = gtk_menu_new(), *it, *sub;
    int i;
    g_tray.showitem = gtk_menu_item_new_with_label(g_tray.hidden ? "Show SRS" : "Hide to Tray");
    g_signal_connect(g_tray.showitem, "activate", G_CALLBACK(on_tray_toggle), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), g_tray.showitem);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    for (i = 0; quick[i].label; i++) {
        it = gtk_menu_item_new_with_label(quick[i].label);
        g_signal_connect(it, "activate", G_CALLBACK(on_tray_open), (gpointer)quick[i].target);
        gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    sub = gtk_menu_new();
    build_menu(sub);
    it = gtk_menu_item_new_with_label("SRS Menu");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(it), sub);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    it = gtk_menu_item_new_with_label("Exit");
    g_signal_connect(it, "activate", G_CALLBACK(on_tray_exit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
    gtk_widget_show_all(m);
    g_tray.menu(g_tray.ind, GTK_MENU(m));
    if (g_tray.target) g_tray.target(g_tray.ind, g_tray.showitem);   /* middle click = show / hide */
}

static gboolean tray_tick(gpointer d)
{
    burn_sample s;
    char t[200];
    (void)d;
    if (!g_tray.ind || !g_tray.title) return G_SOURCE_CONTINUE;
    burn_sample_now(&s);
    snprintf(t, sizeof t, "SRS  -  CPU %.0f°C %.0f%%  -  GPU %.0f°C %.0f%%", s.cpu_temp, s.cpu_load, s.gpu_temp, s.gpu_load);
    g_tray.title(g_tray.ind, t);
    return G_SOURCE_CONTINUE;
}

static void tray_apply(void)
{
    if (g_opt_tray && tray_available()) {
        if (!g_tray.ind) {
            g_tray.ind = g_tray.new_("srs-sysinfo", "srs", 3 /* APP_INDICATOR_CATEGORY_HARDWARE */);
            if (!g_tray.ind) return;
            g_tray.timer = g_timeout_add_seconds(2, tray_tick, NULL);
        }
        tray_build_menu();
        g_tray.status(g_tray.ind, 1 /* ACTIVE */);
        tray_tick(NULL);
    } else if (g_tray.ind) {
        g_tray.status(g_tray.ind, 0 /* PASSIVE: hidden */);
        if (g_tray.hidden) srs_show_all();
    }
}

/* Start with the desktop: ~/.config/autostart/srs-gui.desktop launching straight into the tray */
static char *autostart_path(void) { return g_build_filename(g_get_user_config_dir(), "autostart", "srs-gui.desktop", NULL); }
static int autostart_on(void) { char *p = autostart_path(); int on = g_file_test(p, G_FILE_TEST_EXISTS); g_free(p); return on; }
static void autostart_set(int on)
{
    char *p = autostart_path();
    if (on) {
        char exe[1024] = "/usr/bin/srs-gui", *dir = g_path_get_dirname(p), *txt;
        ssize_t l = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (l > 0) exe[l] = 0;
        if (g_getenv("APPIMAGE")) {   /* the AppImage file, not its mount folder, which changes every start */
            /* Desktop Entry Exec quoting: double quotes, with " ` $ \\ escaped by a backslash */
            const char *in = g_getenv("APPIMAGE");
            size_t o = 0;
            exe[o++] = '"';
            for (; *in && o < sizeof exe - 3; in++) {
                if (strchr("\"`$\\", *in)) exe[o++] = '\\';
                exe[o++] = *in;
            }
            exe[o++] = '"';
            exe[o] = 0;
        }
        g_mkdir_with_parents(dir, 0755);
        txt = g_strdup_printf("[Desktop Entry]\nType=Application\nName=SRS System Information (tray)\n"
                              "Comment=Start SRS hidden in the system tray\nExec=%s --gui-tray\nIcon=srs\nTerminal=false\n"
                              "X-GNOME-Autostart-enabled=true\nX-KDE-autostart-after=panel\n", exe);
        g_file_set_contents(p, txt, -1, NULL);
        g_free(txt);
        g_free(dir);
    } else {
        g_unlink(p);
    }
    g_free(p);
}

enum { OPT_TRAY, OPT_CLOSE, OPT_MIN, OPT_START, OPT_AUTOSTART, OPT_BOOTROOT, OPT_REDACT, OPT_CLASSIC, OPT_SORT };
/* "Refresh root data at boot": the packaged srs-root@<user>.service runs srs-root as root
 * once per boot, so the root-only panels are filled without a password prompt. */
static char *bootroot_unit(void) { return g_strdup_printf("srs-root@%s.service", g_get_user_name()); }

static int bootroot_on(void)
{
    char *unit = bootroot_unit(), *out = NULL;
    char *argv[] = { "systemctl", "is-enabled", unit, NULL };
    int on = 0;
    if (g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, NULL, NULL))
        on = out && !strncmp(out, "enabled", 7);
    g_free(out);
    g_free(unit);
    return on;
}

static int bootroot_available(void)
{
    return g_file_test("/usr/lib/systemd/system/srs-root@.service", G_FILE_TEST_EXISTS) ||
           g_file_test("/etc/systemd/system/srs-root@.service", G_FILE_TEST_EXISTS);
}

/* One pkexec prompt; enabling also collects now (--no-block: don't wait for it). */
static int bootroot_set(int on)
{
    char *unit = bootroot_unit();
    char *en[] = { "pkexec", "systemctl", "enable", "--now", "--no-block", unit, NULL };
    char *dis[] = { "pkexec", "systemctl", "disable", unit, NULL };
    int status = -1;
    gboolean ok = g_spawn_sync(NULL, on ? en : dis, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, &status, NULL);
    g_free(unit);
    return ok && g_spawn_check_wait_status(status, NULL);
}

static void on_option(GtkCheckMenuItem *mi, gpointer d)
{
    int on = gtk_check_menu_item_get_active(mi);
    if (GPOINTER_TO_INT(d) == OPT_BOOTROOT) {
        if (!bootroot_set(on)) {
            /* cancelled or failed: put the tick back without re-running this */
            g_signal_handlers_block_by_func(mi, on_option, d);
            gtk_check_menu_item_set_active(mi, bootroot_on());
            g_signal_handlers_unblock_by_func(mi, on_option, d);
        }
        return;
    }
    switch (GPOINTER_TO_INT(d)) {
    case OPT_TRAY: g_opt_tray = on; break;
    case OPT_CLOSE: g_opt_close_tray = on; break;
    case OPT_MIN: g_opt_min_tray = on; break;
    case OPT_START: g_opt_start_tray = on; break;
    case OPT_AUTOSTART: autostart_set(on); break;
    case OPT_REDACT: g_opt_redact = on; settings_save(); return;
    case OPT_CLASSIC: g_opt_classic = on; settings_save(); return;
    case OPT_SORT: {
        guint i;
        g_opt_sort = on;
        settings_save();
        for (i = 0; i < g_wins->len; i++) {
            swin *x = g_ptr_array_index(g_wins, i);
            if (x->embedded && x->rendered) render(x);
        }
        return;
    }
    }
    settings_save();
    tray_apply();   /* also rebuilds the tray menu so its check marks stay in sync */
}

static void on_theme(GtkCheckMenuItem *mi, gpointer d)
{
    int t = GPOINTER_TO_INT(d);
    if (!gtk_check_menu_item_get_active(mi) || t == g_theme) return;
    g_theme = t;
    settings_save();
    theme_apply();
    tray_apply();   /* rebuild the tray menu so its radio marks follow */
}

static const struct { const char *label; int ms; } g_intervals[] = {
    { "_Default (each panel's own rate)", 0 }, { "0.5 seconds", 500 }, { "_1 second", 1000 }, { "_2 seconds", 2000 },
    { "_3 seconds", 3000 }, { "_5 seconds", 5000 }, { "1_0 seconds", 10000 }, { "_Paused (refresh by hand)", -1 }, { NULL, 0 } };

static void on_interval(GtkCheckMenuItem *mi, gpointer d)
{
    int ms = GPOINTER_TO_INT(d);
    guint i;
    if (!gtk_check_menu_item_get_active(mi) || ms == g_update_ms) return;
    g_update_ms = ms;
    settings_save();
    for (i = 0; i < g_wins->len; i++) {
        swin *x = g_ptr_array_index(g_wins, i);
        if (x->kind == W_SECTION) live_start(x);
    }
}

static void build_options(GtkWidget *menu)
{
    static const struct { const char *label; int id; } opts[] = {
        { "System _tray icon", OPT_TRAY }, { "_Close button hides to tray", OPT_CLOSE },
        { "_Minimize hides to tray (X11 / XWayland)", OPT_MIN }, { "_Start hidden in tray", OPT_START },
        { "Start with the _desktop (autostart)", OPT_AUTOSTART },
        { "Refresh _root data at boot (system service, asks for your password once)", OPT_BOOTROOT },
        { "R_edact identifiers in saved reports and copied text (serials, UUIDs, MACs, host name)", OPT_REDACT },
        { "Start in c_lassic view (SIV-style main window)", OPT_CLASSIC },
        { "Sortable ta_bles in the main window (click a column heading)", OPT_SORT }, { NULL, 0 } };
    int i, have = tray_available();
    if (g_home_open) tabs_options(menu);   /* main window: tab bar, lineups, every item as a tab, hidden items */
    {
        static const char *const tl[] = { "_Light (SIV classic)", "_Dark", "_Follow desktop" };
        GtkWidget *sub = gtk_menu_new(), *top = gtk_menu_item_new_with_mnemonic("T_heme");
        GSList *grp = NULL;
        for (i = 0; i < 3; i++) {
            GtkWidget *it = gtk_radio_menu_item_new_with_mnemonic(grp, tl[i]);
            grp = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(it));
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), g_theme == i);
            g_signal_connect(it, "toggled", G_CALLBACK(on_theme), GINT_TO_POINTER(i));
            gtk_menu_shell_append(GTK_MENU_SHELL(sub), it);
        }
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(top), sub);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), top);
    }
    {
        GtkWidget *sub = gtk_menu_new(), *top = gtk_menu_item_new_with_mnemonic("_Update interval");
        GSList *grp = NULL;
        for (i = 0; g_intervals[i].label; i++) {
            GtkWidget *it = gtk_radio_menu_item_new_with_mnemonic(grp, g_intervals[i].label);
            grp = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(it));
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), g_update_ms == g_intervals[i].ms);
            g_signal_connect(it, "toggled", G_CALLBACK(on_interval), GINT_TO_POINTER(g_intervals[i].ms));
            gtk_menu_shell_append(GTK_MENU_SHELL(sub), it);
        }
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(top), sub);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), top);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    }
    for (i = 0; opts[i].label; i++) {
        GtkWidget *it = gtk_check_menu_item_new_with_mnemonic(opts[i].label);
        int v = opts[i].id == OPT_TRAY ? g_opt_tray : opts[i].id == OPT_CLOSE ? g_opt_close_tray : opts[i].id == OPT_MIN ? g_opt_min_tray
              : opts[i].id == OPT_START ? g_opt_start_tray : opts[i].id == OPT_BOOTROOT ? bootroot_on()
              : opts[i].id == OPT_REDACT ? g_opt_redact : opts[i].id == OPT_CLASSIC ? g_opt_classic
              : opts[i].id == OPT_SORT ? g_opt_sort : autostart_on();
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), v);
        if (!have && opts[i].id != OPT_AUTOSTART && opts[i].id != OPT_BOOTROOT && opts[i].id != OPT_REDACT && opts[i].id != OPT_CLASSIC && opts[i].id != OPT_SORT)
            gtk_widget_set_sensitive(it, FALSE);
        if (opts[i].id == OPT_BOOTROOT && !bootroot_available()) {
            gtk_widget_set_sensitive(it, FALSE);
            if (g_getenv("APPIMAGE"))
                gtk_widget_set_tooltip_text(it, "Needs the SRS package for your distribution: the boot-time service cannot "
                                                "be installed from the AppImage. Run as root works.");
        }
        g_signal_connect(it, "toggled", G_CALLBACK(on_option), GINT_TO_POINTER(opts[i].id));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    }
    if (!have) {
        GtkWidget *it = gtk_menu_item_new_with_label("(tray needs libayatana-appindicator)");
        gtk_widget_set_sensitive(it, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    }
    {
        GtkWidget *it = gtk_menu_item_new_with_mnemonic("_Hide to Tray");
        g_signal_connect(it, "activate", G_CALLBACK(on_tray_toggle), NULL);
        gtk_widget_set_sensitive(it, tray_active());
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    }
}

/* main window: close / minimise go to the tray when those options are on */
static gboolean on_main_delete(GtkWidget *wid, GdkEvent *ev, gpointer d)
{
    (void)wid; (void)ev; (void)d;
    if (g_opt_close_tray && tray_active()) { srs_hide_all(); return TRUE; }
    return FALSE;
}

static gboolean on_main_state(GtkWidget *wid, GdkEventWindowState *ev, gpointer d)
{
    (void)d;
    if ((ev->changed_mask & GDK_WINDOW_STATE_ICONIFIED) && (ev->new_window_state & GDK_WINDOW_STATE_ICONIFIED) &&
        g_opt_min_tray && tray_active()) {
        gtk_window_deiconify(GTK_WINDOW(wid));
        srs_hide_all();
    }
    return FALSE;
}

/* ======================================================================
 * main window
 * ====================================================================== */

/* as_main 0: opened from the new main window (Menu -> Classic view); closing it does not quit */
static void open_main(int as_main)
{
    swin *w;
    GtkWidget *vb;
    char title[400];
    if (!as_main && g_classic) { gtk_window_present(GTK_WINDOW(g_classic->win)); return; }
    /* a classic-only session already has its classic main window */
    if (!as_main && g_main && g_main->kind == W_SECTION) { gtk_window_present(GTK_WINDOW(g_main->win)); return; }
    w = g_new0(swin, 1);
    w->kind = W_SECTION;
    w->is_main = as_main;
    snprintf(w->id, sizeof w->id, "overview");
    if (as_main) g_main = w; else g_classic = w;
    snprintf(title, sizeof title, "%s - System Report Search V%s  %s", g_image, SRS_VERSION, g_host);
    make_window(w, title, 1100, 520);
    vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vb), 4);
    gtk_container_add(GTK_CONTAINER(w->win), vb);
    gtk_box_pack_start(GTK_BOX(vb), make_view(w), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vb), button_row(w, row_main), FALSE, FALSE, 0);
    render(w);
    w->own_ms = 2000;
    live_start(w);
    if (as_main) {
        g_signal_connect(w->win, "delete-event", G_CALLBACK(on_main_delete), NULL);
        g_signal_connect(w->win, "window-state-event", G_CALLBACK(on_main_state), NULL);
    }
    gtk_widget_show_all(w->win);
}

/* Classic windows keep SIV's look (8pt Tahoma, square grey buttons). In a classic session the rules
 * apply to everything, as before 1.2; in a new-window session they are scoped to windows with the
 * "siv" style class, so the new main window follows the desktop's GTK theme (light or dark). */
static void load_css(void)
{
    static GtkCssProvider *c;
    static const char light[] =
        "window, #panel, #panel text { background-color: #F5F5F5; }"
        "#panel, #panel text { font: 8pt Tahoma; color: #000000; }"
        "#panel text selection { background-color: #3399FF; color: #FFFFFF; }"
        "button, entry, label, checkbutton, radiobutton, spinbutton, combobox, menuitem { font: 8pt Tahoma; }"
        "#sivbtn { padding: 1px 4px; min-height: 18px; border-radius: 0; background-image: none;"
        "  background-color: #E1E1E1; color: #000000; border: 1px solid #ADADAD; box-shadow: none; }"
        "#sivbtn:hover { background-color: #E5F1FB; border-color: #0078D7; }"
        "#sivbtn:active { background-color: #CCE4F7; }"
        "#lbl, radiobutton label, checkbutton label { color: #0000CC; }";
    static const char dark[] =
        "window, #panel, #panel text { background-color: #1E1E1E; }"
        "#panel, #panel text { font: 8pt Tahoma; color: #E6E6E6; }"
        "#panel text selection { background-color: #264F78; color: #FFFFFF; }"
        "button, entry, label, checkbutton, radiobutton, spinbutton, combobox, menuitem { font: 8pt Tahoma; }"
        "#sivbtn { padding: 1px 4px; min-height: 18px; border-radius: 0; background-image: none;"
        "  background-color: #383838; color: #E6E6E6; border: 1px solid #5A5A5A; box-shadow: none; text-shadow: none; }"
        "#sivbtn:hover { background-color: #3F4B57; border-color: #3399FF; }"
        "#sivbtn:active { background-color: #2A4A66; }"
        "#lbl, radiobutton label, checkbutton label { color: #8FB4FF; }";
    /* the same rules, limited to classic windows */
    static const char light_scoped[] =
        "window.siv, .siv #panel, .siv #panel text { background-color: #F5F5F5; }"
        "#panel, #panel text { font: 8pt Tahoma; }"
        ".siv #panel, .siv #panel text { color: #000000; }"
        ".siv #panel text selection { background-color: #3399FF; color: #FFFFFF; }"
        ".siv button, .siv entry, .siv label, .siv checkbutton, .siv radiobutton, .siv spinbutton, .siv combobox { font: 8pt Tahoma; }"
        "#sivbtn { padding: 1px 4px; min-height: 18px; border-radius: 0; background-image: none;"
        "  background-color: #E1E1E1; color: #000000; border: 1px solid #ADADAD; box-shadow: none; }"
        "#sivbtn:hover { background-color: #E5F1FB; border-color: #0078D7; }"
        "#sivbtn:active { background-color: #CCE4F7; }"
        "#lbl, .siv radiobutton label, .siv checkbutton label { color: #0000CC; }";
    static const char dark_scoped[] =
        "window.siv, .siv #panel, .siv #panel text { background-color: #1E1E1E; }"
        "#panel, #panel text { font: 8pt Tahoma; }"
        ".siv #panel, .siv #panel text { color: #E6E6E6; }"
        ".siv #panel text selection { background-color: #264F78; color: #FFFFFF; }"
        ".siv button, .siv entry, .siv label, .siv checkbutton, .siv radiobutton, .siv spinbutton, .siv combobox { font: 8pt Tahoma; }"
        "#sivbtn { padding: 1px 4px; min-height: 18px; border-radius: 0; background-image: none;"
        "  background-color: #383838; color: #E6E6E6; border: 1px solid #5A5A5A; box-shadow: none; text-shadow: none; }"
        "#sivbtn:hover { background-color: #3F4B57; border-color: #3399FF; }"
        "#sivbtn:active { background-color: #2A4A66; }"
        "#lbl, .siv radiobutton label, .siv checkbutton label { color: #8FB4FF; }";
    /* the new main window: theme colours only (works in light and dark), no fixed palette */
    static const char modern[] =
        ".srs-side { border-right: 1px solid @borders; }"
        ".srs-side row { padding: 7px 14px 7px 12px; }"
        ".srs-side row image { margin-right: 10px; }"
        ".srs-side-head { padding: 14px 14px 6px 14px; font-weight: bold; }"
        ".srs-h1 { font-size: 20px; font-weight: bold; }"
        ".srs-h2 { font-size: 15px; font-weight: bold; }"
        ".srs-big { font-size: 14px; font-weight: bold; }"
        ".srs-cap { font-size: smaller; }"
        ".srs-val { font-weight: bold; }"
        ".srs-card { background-color: @theme_base_color; border: 1px solid @borders; border-radius: 8px; padding: 14px 16px; }"
        ".srs-fact { background-color: @theme_base_color; border: 1px solid @borders; border-radius: 6px; padding: 7px 12px; }"
        ".srs-health { background-color: @theme_base_color; border: 1px solid @borders; border-radius: 8px; padding: 6px 16px 10px 16px; }"
        ".srs-dot { font-size: 16px; }"
        ".srs-ok { color: @success_color; }"
        ".srs-warn { color: @warning_color; }"
        ".srs-bad { color: @error_color; }"
        ".srs-unk { color: @insensitive_fg_color; }"
        ".srs-tabbar { padding: 4px 6px; border-bottom: 1px solid @borders; }"
        ".srs-tabbar button { padding: 2px 8px; }"
        "notebook.srs-nb > header { border-top: 1px solid @borders; }"
        ".srs-strip { padding: 4px 6px; border-top: 1px solid @borders; border-bottom: 1px solid @borders; }"
        ".srs-strip button { padding: 2px 10px; }"
        "notebook.srs-nb > header.left tab { padding: 3px 10px; min-height: 0; }"
        ".srs-tool button { min-height: 0; }"
        ".srs-main #panel, .srs-main #panel text { font: 9pt Tahoma; }";
    GString *css = g_string_new(NULL);
    if (!c) {
        c = gtk_css_provider_new();
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(c), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    if (g_classic_session && !g_home_open) g_string_append(css, g_dark ? dark : light);
    else { g_string_append(css, g_dark ? dark_scoped : light_scoped); g_string_append(css, modern); }
    gtk_css_provider_load_from_data(c, css->str, -1, NULL);
    g_string_free(css, TRUE);
}

/* desktop colour scheme: xdg-desktop-portal org.freedesktop.appearance color-scheme (0 none, 1 dark, 2 light) */
static int portal_scheme_from(GVariant *v)
{
    int r = -1;
    g_variant_ref(v);
    while (v && g_variant_is_of_type(v, G_VARIANT_TYPE_VARIANT)) { GVariant *in = g_variant_get_variant(v); g_variant_unref(v); v = in; }
    if (v && g_variant_is_of_type(v, G_VARIANT_TYPE_UINT32)) r = (int)g_variant_get_uint32(v);
    if (v) g_variant_unref(v);
    return r;
}

static void on_portal_signal(GDBusProxy *p, const char *sender, const char *sig, GVariant *params, gpointer d)
{
    const char *ns, *key;
    GVariant *v;
    (void)p; (void)sender; (void)d;
    if (strcmp(sig, "SettingChanged") || !g_variant_is_of_type(params, G_VARIANT_TYPE("(ssv)"))) return;
    g_variant_get(params, "(&s&sv)", &ns, &key, &v);
    if (!strcmp(ns, "org.freedesktop.appearance") && !strcmp(key, "color-scheme")) {
        g_portal_scheme = portal_scheme_from(v);
        if (g_theme == TH_SYSTEM) theme_apply();
    }
    g_variant_unref(v);
}

static void portal_init(void)
{
    static int tried;
    GVariant *r;
    if (tried) return;
    tried = 1;
    g_portal = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, NULL,
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Settings", NULL, NULL);
    if (!g_portal) return;
    r = g_dbus_proxy_call_sync(g_portal, "ReadOne", g_variant_new("(ss)", "org.freedesktop.appearance", "color-scheme"),
                               G_DBUS_CALL_FLAGS_NONE, 1500, NULL, NULL);
    if (!r) r = g_dbus_proxy_call_sync(g_portal, "Read", g_variant_new("(ss)", "org.freedesktop.appearance", "color-scheme"),
                                       G_DBUS_CALL_FLAGS_NONE, 1500, NULL, NULL);   /* older portals */
    if (r) {
        GVariant *v = g_variant_get_child_value(r, 0);
        g_portal_scheme = portal_scheme_from(v);
        g_variant_unref(v);
        g_variant_unref(r);
    }
    g_signal_connect(g_portal, "g-signal", G_CALLBACK(on_portal_signal), NULL);
}

static int system_prefers_dark(void)
{
    portal_init();
    if (g_portal_scheme == 1) return 1;
    if (g_portal_scheme == 2) return 0;
    if (g_env_theme[0]) {   /* e.g. GTK_THEME=adw-gtk3-dark */
        char *low = g_ascii_strdown(g_env_theme, -1);
        int dark = strstr(low, "dark") != NULL;
        g_free(low);
        return dark;
    }
    {
        GKeyFile *kf = g_key_file_new();
        char *f = g_build_filename(g_get_user_config_dir(), "gtk-3.0", "settings.ini", NULL);
        int dark = 0;
        if (g_key_file_load_from_file(kf, f, G_KEY_FILE_NONE, NULL)) {
            char *tn = g_key_file_get_string(kf, "Settings", "gtk-theme-name", NULL), *low = tn ? g_ascii_strdown(tn, -1) : NULL;
            dark = g_key_file_get_boolean(kf, "Settings", "gtk-application-prefer-dark-theme", NULL) || (low && strstr(low, "dark"));
            g_free(low);
            g_free(tn);
        }
        g_free(f);
        g_key_file_free(kf);
        return dark;
    }
}

/* light / dark for this process: Adwaita + its dark variant, SIV palette swapped, every open window recoloured */
static void theme_apply(void)
{
    guint i;
    g_dark = g_theme == TH_DARK || (g_theme == TH_SYSTEM && system_prefers_dark());
    g_pal = g_dark ? g_pal_dark : g_pal_light;
    g_object_set(gtk_settings_get_default(), "gtk-theme-name", "Adwaita", "gtk-application-prefer-dark-theme", g_dark, NULL);
    load_css();
    for (i = 0; g_wins && i < g_wins->len; i++) {
        swin *x = g_ptr_array_index(g_wins, i);
        if (x->buf) theme_tags(x->buf);
        if (x->plot) gtk_widget_queue_draw(x->plot);
        if (x->win) gtk_widget_queue_draw(x->win);
    }
}

/* ======================================================================
 * the main window (1.2): header bar (search, menu), sidebar of categories, an Overview page with
 * an "Is my PC OK?" check and summary cards, and one page per category: a plain-words summary on
 * top of the same section panels as the classic windows (same text view, colours, Copy / redact,
 * Guide, live refresh, double-click lookup). The classic (SIV-style) windows stay one click away.
 * Read-only like everything else: the summaries are parsed from the sections' own output.
 * ====================================================================== */
enum { CAT_OVERVIEW, CAT_CPU, CAT_MEM, CAT_BOARD, CAT_GPU, CAT_STORAGE, CAT_NET, CAT_SENSORS, CAT_POWER, CAT_DEVICES, CAT_OS,
       CAT_BENCH, CAT_TOOLS, CAT_N };

typedef struct {
    const char *name, *title, *icons, *blurb;
    const char *tabs[16];       /* the sections shown as tabs; every other item of the category is under "All items" */
} catdef;

static const catdef g_cats[CAT_N] = {
    { "overview", "Overview", "computer-symbolic", "A quick health check and the key facts about this PC.", { NULL } },
    { "processor", "Processor", "cpu-symbolic|processor-symbolic|system-run-symbolic",
      "The processor (CPU) runs every program. Model, cores and threads, clock speeds, features and caches.",
      { "cpus", "processors", "cpu-clocks", "features", "cache", "cpuid", "microcode", "speculation", NULL } },
    { "memory", "Memory", "memory-symbolic|media-flash-symbolic",
      "Memory (RAM) holds the programs and data in use. How much is installed and in use, the memory sticks and their timings.",
      { "memory", "dimms", "spd", "mem-tweak", "dimm-status", "physical", "numa", NULL } },
    { "board", "Motherboard & BIOS", "motherboard-symbolic|application-x-firmware-symbolic|emblem-system-symbolic",
      "The main circuit board and its firmware (BIOS / UEFI), as the board describes itself.",
      { "machine", "hardware", "firmware", "bios-tables", "uefi-info", "acpi-tables", "tpm-device", NULL } },
    { "graphics", "Graphics & audio", "video-display-symbolic",
      "Graphics cards, monitors and sound devices.",
      { "graphics", "gpu-info", "gpu-clocks", "gpu-temp", "monitors", "opencl", "audio", NULL } },
    { "storage", "Storage", "drive-harddisk-symbolic",
      "Drives and the file systems on them: size, free space and drive health (SMART).",
      { "drives", "volumes", "smart", "partitions", "performance", "usb-disks", NULL } },
    { "network", "Network", "network-wired-symbolic",
      "Network adapters, addresses, Wi-Fi, Bluetooth and open connections.",
      { "adapters", "ip-address", "wi-fi", "bluetooth", "routing", "tcp-ip4-ports", "counters", NULL } },
    { "sensors", "Sensors", "sensors-temperature-symbolic|temperature-symbolic|power-profile-balanced-symbolic",
      "Temperatures, fan speeds, voltages and power, read live from the hardware monitor chips.",
      { "sensors", "hw-status", "cooling", "power", NULL } },
    /* 2.2: its own page (power.c), not section tabs; under Sensors (Alt+9) */
    { "power", "Power & cost", "battery-level-100-charged-symbolic|power-profile-balanced-symbolic|battery-full-symbolic",
      "How much power this PC uses, what it costs, and its history. Estimates: the CPU and graphics card are measured, the rest is guessed.",
      { NULL } },
    { "devices", "USB & PCI devices", "preferences-system-devices-symbolic|drive-removable-media-symbolic",
      "Every device in or plugged into the PC. Double-click a line with a device ID to look it up.",
      { "all-dev", "pci-bus", "usb-bus", "usb-tree", "hid-bus", "thunder", NULL } },
    { "os", "Operating system", "distributor-logo-symbolic|applications-system-symbolic",
      "Linux itself: distribution, kernel, drivers, running programs and services.",
      { "windows", "drivers", "processes", "startup", "software", "environment", "users", NULL } },
    { "bench", "Benchmarks & stress", "power-profile-performance-symbolic|utilities-system-monitor-symbolic",
      "Measure the processor and memory, or load the CPU to test cooling and stability. "
      "Nothing runs until you press Run or Start in the tool's window.",
      { "benchmark", "cache-memory", "storage-bench", "os-latency", "cpu-compute", "everyday-work", "srs-pi", "crypto-bench", "prime-sieve", "ipi-delay", "latency", "bench-history", NULL } },
    { "tools", "Tools", "applications-utilities-symbolic|preferences-other-symbolic",
      "Look up device IDs, search a full system report, save a report, and SRS's own items.",
      { "about", "help", NULL } },
};

enum { LV_UNK = -1, LV_OK, LV_WARN, LV_BAD };
#define NCHECK 9
#define NFACT 8
static const int g_card_cats[] = { CAT_CPU, CAT_GPU, CAT_MEM, CAT_STORAGE, CAT_OS, CAT_BOARD };
#define NCARD ((int)(sizeof g_card_cats / sizeof g_card_cats[0]))

typedef struct { char cap[48], val[320], tip[400]; double level; } fact;   /* level 0..1 draws a bar; <0 none */
typedef struct { GtkWidget *box, *tile[NFACT], *cap[NFACT], *val[NFACT]; int n; } factsui;
typedef struct { GtkWidget *main, *k[4], *v[4], *bar; } cardui;
typedef struct { GtkWidget *dot, *name, *text, *btn; } checkui;

typedef struct {
    swin *w;
    GtkWidget *hb, *side, *stack, *search, *menubtn;
    GtkWidget *badge, *badge_icon, *badge_lbl, *badge_root;   /* privilege badge (2.2) */
    GtkWidget *nb[CAT_N];
    GtkWidget *strip[CAT_N], *ctl[CAT_N], *alllbl[CAT_N], *pagebox[CAT_N];   /* tab lineups (2.2, gui_tabs.inc) */
    GtkWidget *lineup_lbl;
    factsui facts[CAT_N];
    GtkWidget *ov_sub, *ov_icon, *ov_verdict, *ov_vsub;
    checkui ck[NCHECK];
    cardui card[NCARD];
    GtkListStore *items;
    guint tick;
    int cur;
} home_t;
static home_t *g_home;

/* ---- capture: render a section into rows of cells (tab = next cell, paragraph = next row) ---- */
typedef struct { GPtrArray *rows, *cur; GString *cell; } cap;

static void cap_cell_done(cap *c)
{
    char *s = g_string_free(c->cell, FALSE);
    g_ptr_array_add(c->cur, g_strstrip(s));
    c->cell = g_string_new(NULL);
}
static void cp_text(srs_out *o, int col, int b, int m, const char *s) { (void)col; (void)b; (void)m; g_string_append(((cap *)o->ctx)->cell, s); }
static void cp_tab(srs_out *o) { cap_cell_done(o->ctx); }
static void cp_par(srs_out *o)
{
    cap *c = o->ctx;
    cap_cell_done(c);
    g_ptr_array_add(c->rows, c->cur);
    c->cur = g_ptr_array_new_with_free_func(g_free);
}
static void cp_tabs(srs_out *o, const int *px, int n) { (void)o; (void)px; (void)n; }

static void cap_free(cap *c)
{
    if (!c) return;
    g_ptr_array_free(c->rows, TRUE);
    g_ptr_array_free(c->cur, TRUE);
    g_string_free(c->cell, TRUE);
    g_free(c);
}

static cap *cap_render(const char *id)
{
    cap *c = g_new0(cap, 1);
    const srs_item *it = srs_section_find(id);
    const char *colon = strchr(id, ':');
    srs_out o;
    c->rows = g_ptr_array_new_with_free_func((GDestroyNotify)g_ptr_array_unref);
    c->cur = g_ptr_array_new_with_free_func(g_free);
    c->cell = g_string_new(NULL);
    memset(&o, 0, sizeof o);
    o.text = cp_text; o.tab = cp_tab; o.par = cp_par; o.tabs = cp_tabs;
    o.ctx = c;
    o.arg = colon ? colon + 1 : NULL;
    if (it) srs_section_render(it, &o);
    cp_par(&o);
    return c;
}

/* small cache so the Overview's 3-second tick doesn't re-run slow sections */
static struct { char id[48]; cap *c; double t; } g_cc[24];
static cap *cap_get(const char *id, double max_age_ms)
{
    double now = srs_now_ms();
    int i, slot = -1;
    for (i = 0; i < 24; i++) {
        if (!strcmp(g_cc[i].id, id)) { slot = i; break; }
        if (slot < 0 && !g_cc[i].id[0]) slot = i;
    }
    if (slot < 0) slot = 0;
    if (strcmp(g_cc[slot].id, id) || !g_cc[slot].c || now - g_cc[slot].t > max_age_ms) {
        cap_free(g_cc[slot].c);
        snprintf(g_cc[slot].id, sizeof g_cc[slot].id, "%s", id);
        g_cc[slot].c = cap_render(id);
        g_cc[slot].t = now;
    }
    return g_cc[slot].c;
}
static void cap_flush(void) { int i; for (i = 0; i < 24; i++) g_cc[i].t = -1e18; }

static const char *cell(GPtrArray *r, int i) { return r && i >= 0 && (guint)i < r->len ? g_ptr_array_index(r, i) : ""; }
static GPtrArray *cap_row(cap *c, const char *key)
{
    guint i;
    for (i = 0; i < c->rows->len; i++) { GPtrArray *r = g_ptr_array_index(c->rows, i); if (!strcmp(cell(r, 0), key)) return r; }
    return NULL;
}
static GPtrArray *cap_row_prefix(cap *c, const char *key)
{
    guint i;
    for (i = 0; i < c->rows->len; i++) { GPtrArray *r = g_ptr_array_index(c->rows, i); if (g_str_has_prefix(cell(r, 0), key)) return r; }
    return NULL;
}
static const char *cap_kv(cap *c, const char *key) { const char *v = cell(cap_row(c, key), 1); return strcmp(v, "-") ? v : ""; }
static int row_col(GPtrArray *r, const char *name) { int i; for (i = 0; r && (guint)i < r->len; i++) if (!strcmp(cell(r, i), name)) return i; return -1; }

/* "a  b  c" (two-space separated parts inside a cell): part n */
static void part(const char *s, int n, char *out, size_t sz)
{
    const char *p = s, *e;
    while (n-- > 0) { p = strstr(p, "  "); if (!p) { *out = 0; return; } while (*p == ' ') p++; }
    e = strstr(p, "  ");
    snprintf(out, sz, "%.*s", (int)(e ? (size_t)(e - p) : strlen(p)), p);
}
/* text before " (" - drops serial numbers and notes in brackets */
static void before_paren(const char *s, char *out, size_t sz)
{
    const char *p = strstr(s, " (");
    snprintf(out, sz, "%.*s", (int)(p ? (size_t)(p - s) : strlen(s)), s);
}
/* the number just before " word" in s, -1 if none */
static long num_before(const char *s, const char *word)
{
    const char *p = strstr(s, word);
    while (p && p > s && p[-1] == ' ') p--;
    if (!p || p == s || !g_ascii_isdigit(p[-1])) return -1;
    while (p > s && g_ascii_isdigit(p[-1])) p--;
    return atol(p);
}

/* ---- data for the summaries (read-only, from the sections SRS already shows) ---- */
static burn_sample g_hs;
static void hs_update(void) { burn_sample_now(&g_hs); }

static void fmt_uptime(char *out, size_t sz)
{
    char buf[64] = "";
    double up;
    long m, h, d;
    *out = 0;
    if (!rd_str("/proc/uptime", buf, sizeof buf)) return;
    up = atof(buf);
    m = (long)(up / 60) % 60; h = (long)(up / 3600) % 24; d = (long)(up / 86400);
    if (d) snprintf(out, sz, "%ld day%s %ld h", d, d == 1 ? "" : "s", h);
    else if (h) snprintf(out, sz, "%ld h %ld min", h, m);
    else snprintf(out, sz, "%ld min", m);
}

/* ---- temperature limits: each sensor's own where it publishes one ---- */

/* °C from a hwmon millidegree file, 0 when missing or not plausible */
static double rd_limit(const char *path)
{
    long v;
    return rd_long(path, &v) && v >= 40000 && v <= 150000 ? v / 1000.0 : 0;
}

/* highest value of a temp*_<kind> file of the first hwmon chip whose name starts with chip */
static double hwmon_limit(const char *chip, const char *kind)
{
    char **hw;
    int n = dir_list("/sys/class/hwmon", &hw), i, t;
    double best = 0;
    for (i = 0; i < n; i++) {
        char nm[64], p[512];
        snprintf(p, sizeof p, "/sys/class/hwmon/%s/name", hw[i]);
        if (!rd_str(p, nm, sizeof nm) || strncmp(nm, chip, strlen(chip))) continue;
        for (t = 1; t <= 64; t++) {
            snprintf(p, sizeof p, "/sys/class/hwmon/%s/temp%d_%s", hw[i], t, kind);
            best = MAX(best, rd_limit(p));
        }
        if (best > 0) break;
    }
    dir_free(hw, n);
    return best;
}

/* The CPU's maximum operating temperature (where it starts slowing itself down).
   Intel's coretemp publishes it (temp*_crit / _max); AMD's k10temp does not, so for
   AMD desktop Ryzen it comes from AMD's specification for the model ("Max. Operating
   Temperature (Tjmax)" on amd.com). 0 = unknown. */
static double cpu_limit(const char **source)
{
    double l;
    char *info = NULL, *model = NULL;
    if ((l = hwmon_limit("coretemp", "crit")) > 0 || (l = hwmon_limit("coretemp", "max")) > 0) {
        *source = "the CPU's own limit (coretemp)";
        return l;
    }
    if (rd_all("/proc/cpuinfo", &info) >= 0) {
        char *m = strstr(info, "model name");
        if (m && (m = strchr(m, ':'))) model = g_strndup(m + 2, strcspn(m + 2, "\n"));
        free(info);
    }
    if (!model) return 0;
    static const struct { const char *re; double tj; } amd[] = {
        { "Ryzen [579] 7[89]\\d0X3D", 89 },                  /* 7800X3D, 7900X3D, 7950X3D */
        { "Ryzen 5 7600X3D", 89 },
        { "Ryzen [3579] 9\\d\\d0X3D", 95 },                  /* 9800X3D, 9900X3D, 9950X3D */
        { "Ryzen [3579] 5[5-8]00X3D", 90 },                   /* 5500X3D - 5800X3D */
        { "Ryzen [3579] [79]\\d\\d\\d(X|F)?( |$)", 95 },      /* Ryzen 7000 / 9000 desktop */
        { "Ryzen [3579] 5\\d\\d\\dG(E|T)?( |$)", 95 },        /* Ryzen 5000 G (APU) */
        { "Ryzen 5 5600X( |$)", 95 },                       /* the one Ryzen 5000 desktop part rated 95 °C */
        { "Ryzen [3579] 5\\d\\d\\dXT?( |$)", 90 },            /* Ryzen 5000 desktop */
        { "Ryzen [3579] 5\\d\\d\\d( |$)", 90 },
    };
    for (size_t i = 0; i < G_N_ELEMENTS(amd) && !l; i++)
        if (g_regex_match_simple(amd[i].re, model, 0, 0)) l = amd[i].tj;
    g_free(model);
    if (l > 0) *source = "AMD's maximum for this processor model";
    return l;
}

/* the GPU junction (hotspot) sensor's own critical temperature (amdgpu temp2_crit) */
static double gpu_limit(void)
{
    char **c;
    int n = dir_list("/sys/class/drm", &c), i;
    double l = 0;
    for (i = 0; i < n && !l; i++) {
        char p[512], **h;
        int nh, j;
        long v;
        if (strncmp(c[i], "card", 4) || strchr(c[i], '-')) continue;
        snprintf(p, sizeof p, "/sys/class/drm/%s/device/gpu_busy_percent", c[i]);
        if (!rd_long(p, &v)) continue;
        snprintf(p, sizeof p, "/sys/class/drm/%s/device/hwmon", c[i]);
        nh = dir_list(p, &h);
        for (j = 0; j < nh && !l; j++) {
            snprintf(p, sizeof p, "/sys/class/drm/%s/device/hwmon/%s/temp2_crit", c[i], h[j]);
            if (!(l = rd_limit(p))) {
                snprintf(p, sizeof p, "/sys/class/drm/%s/device/hwmon/%s/temp1_crit", c[i], h[j]);
                l = rd_limit(p);
            }
        }
        dir_free(h, nh);
    }
    dir_free(c, n);
    return l;
}

/* a drive's own warning temperature (NVMe / drivetemp temp1_max), else its critical one minus 5 */
static double drive_limit(const char *dev)
{
    char d[64], p[512], **h;
    int nh, j;
    double l = 0;
    const char *b = strrchr(dev, '/') ? strrchr(dev, '/') + 1 : dev;
    snprintf(d, sizeof d, "%s", b);
    if (!strncmp(d, "nvme", 4)) {
        char *n = strchr(d + 4, 'n');   /* nvme2n1 -> controller nvme2 */
        if (n) *n = 0;
        snprintf(p, sizeof p, "/sys/class/nvme/%s", d);
    } else
        snprintf(p, sizeof p, "/sys/block/%s/device/hwmon", d);
    nh = dir_list(p, &h);
    for (j = 0; j < nh && !l; j++) {
        char q[700];
        if (strncmp(h[j], "hwmon", 5)) continue;
        snprintf(q, sizeof q, "%s/%s/temp1_max", p, h[j]);
        if (!(l = rd_limit(q))) {
            snprintf(q, sizeof q, "%s/%s/temp1_crit", p, h[j]);
            if ((l = rd_limit(q)) > 0) l -= 5;
        }
    }
    dir_free(h, nh);
    return l;
}

/* hot from `hot`, warm from `warm` */
static int temp_level(double x, double warm, double hot) { return x >= hot ? LV_BAD : x >= warm ? LV_WARN : LV_OK; }

typedef struct {
    int total, passed, failed, unknown;
    double tmax; char tdev[40];             /* warmest drive */
    double tw, tlim; char twdev[40];        /* drive closest to its own limit, and that limit (0 = generic) */
} smartsum;
static void smart_sum(smartsum *m)
{
    cap *c = cap_get("smart", 30000);
    guint i;
    int hcol = -1, tcol = -1, inrows = 0;
    memset(m, 0, sizeof *m);
    for (i = 0; i < c->rows->len; i++) {
        GPtrArray *r = g_ptr_array_index(c->rows, i);
        if (!inrows) {
            if (!strcmp(cell(r, 0), "Drive") && (hcol = row_col(r, "Health")) >= 0) { tcol = row_col(r, "Temperature"); inrows = 1; }
            continue;
        }
        if (!*cell(r, 0)) { if (m->total) break; continue; }
        if ((int)r->len <= hcol) break;
        m->total++;
        if (!strcmp(cell(r, hcol), "PASSED") || !strcmp(cell(r, hcol), "OK")) m->passed++;
        else if (strstr(cell(r, hcol), "FAIL")) m->failed++;
        else m->unknown++;
        if (tcol >= 0 && atof(cell(r, tcol)) > 0) {
            double t = atof(cell(r, tcol)), lim = drive_limit(cell(r, 0)), hot = lim > 0 ? lim : 70;
            if (t > m->tmax) { m->tmax = t; snprintf(m->tdev, sizeof m->tdev, "%s", cell(r, 0)); }
            if (!*m->twdev || t - hot > m->tw - (m->tlim > 0 ? m->tlim : 70)) {
                m->tw = t; m->tlim = lim; snprintf(m->twdev, sizeof m->twdev, "%s", cell(r, 0));
            }
        }
    }
}

typedef struct { char free[40], total[40]; double used; int ok; } volsum;
static void root_volume(volsum *v)
{
    cap *c = cap_get("volumes", 30000);
    GPtrArray *h = cap_row(c, "Drive and Path"), *r = cap_row(c, "/");
    memset(v, 0, sizeof *v);
    if (!h || !r) return;
    snprintf(v->free, sizeof v->free, "%s", cell(r, row_col(h, "Free")));
    snprintf(v->total, sizeof v->total, "%s", cell(r, row_col(h, "Total")));
    v->used = atof(cell(r, row_col(h, "Used"))) / 100.0;
    v->ok = *v->total != 0;
}

typedef struct { int chips, alarms; } hwsum;
static void hw_sum(hwsum *s)
{
    cap *c = cap_get("hw-status", 30000);
    guint i;
    int acol = -1, in = 0;
    memset(s, 0, sizeof *s);
    for (i = 0; i < c->rows->len; i++) {
        GPtrArray *r = g_ptr_array_index(c->rows, i);
        if (!in) { if (!strcmp(cell(r, 0), "Chip") && (acol = row_col(r, "Alarms")) >= 0) in = 1; continue; }
        if (!*cell(r, 0)) { if (s->chips) break; continue; }
        s->chips++;
        s->alarms += atoi(cell(r, acol));
    }
}

/* memory / CPU error counters: EDAC (ECC memory), HardwareCorrupted pages, kernel taint M (machine check) */
typedef struct { int edac, mce; long ce, ue, corrupt_kb; } errsum;
static void err_sum(errsum *e)
{
    char **mc;
    int n = dir_list("/sys/devices/system/edac/mc", &mc), i;
    long v;
    cap *c = cap_get("memory", 5000);
    memset(e, 0, sizeof *e);
    for (i = 0; i < n; i++) {
        char p[300];
        if (strncmp(mc[i], "mc", 2) || !g_ascii_isdigit(mc[i][2])) continue;
        e->edac = 1;
        snprintf(p, sizeof p, "/sys/devices/system/edac/mc/%s/ce_count", mc[i]);
        if (rd_long(p, &v)) e->ce += v;
        snprintf(p, sizeof p, "/sys/devices/system/edac/mc/%s/ue_count", mc[i]);
        if (rd_long(p, &v)) e->ue += v;
    }
    dir_free(mc, n);
    if (rd_long("/proc/sys/kernel/tainted", &v)) e->mce = (v >> 4) & 1;   /* taint flag M */
    e->corrupt_kb = (long)atof(cap_kv(c, "HardwareCorrupted"));
}

/* Intel CPUs count thermal throttling events; -1 when the CPU has no such counters */
static long throttle_count(void)
{
    long sum = -1, v;
    int i;
    for (i = 0; i < burn_ncpu(); i++) {
        char p[200];
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/thermal_throttle/core_throttle_count", i);
        if (rd_long(p, &v)) sum = (sum < 0 ? 0 : sum) + v;
    }
    return sum;
}

static void fact_set(fact *f, const char *capt, const char *val, const char *tip)
{
    snprintf(f->cap, sizeof f->cap, "%s", capt);
    snprintf(f->val, sizeof f->val, "%s", val && *val ? val : "-");
    snprintf(f->tip, sizeof f->tip, "%s", tip ? tip : "");
    f->level = -1;
}

/* The key facts of a category in plain words: the page's summary tiles and the Overview cards. */
static int facts_for(int cat, fact *f, int max)
{
    int n = 0;
    char b[400], t[200];
#define F(c, v, tip) do { if (n < max) fact_set(&f[n++], c, v, tip); } while (0)
    switch (cat) {
    case CAT_CPU: {
        cap *c = cap_get("cpus", 60000);
        const char *topo = cap_kv(c, "Topology");
        long cores = num_before(topo, " core"), thr = num_before(topo, " logical"), pk = num_before(topo, " package");
        F("Model", sys_cpu_name(), "The processor's own name for itself (/proc/cpuinfo)");
        if (cores > 0 && thr > 0) snprintf(b, sizeof b, "%ld cores, %ld threads%s", cores, thr, pk > 1 ? " (several packages)" : "");
        else snprintf(b, sizeof b, "%s", topo);
        F("Cores", b, "Physical cores, and the threads (logical CPUs) they run at once");
        if (g_hs.cpu_mhz > 0) snprintf(b, sizeof b, "%.2f GHz", g_hs.cpu_mhz / 1000); else *b = 0;
        F("Speed now", b, "Clock of the fastest core right now (cpufreq). It changes all the time with the load.");
        snprintf(b, sizeof b, "%.0f %%", g_hs.cpu_load);
        F("Busy", b, "How busy all CPUs are together, since the last update");
        if (g_hs.cpu_temp > 0) snprintf(b, sizeof b, "%.0f °C", g_hs.cpu_temp); else snprintf(b, sizeof b, "no sensor");
        snprintf(t, sizeof t, "Hottest CPU temperature sensor (%s)", *g_hs.cpu_temp_name ? g_hs.cpu_temp_name : "k10temp / coretemp");
        F("Temperature", b, t);
        F("Frequency driver", cap_kv(c, "Frequency driver"), "The Linux driver that sets the CPU clock speed");
        F("Microcode", cap_kv(c, "Microcode"), "Version of the CPU's built-in firmware patch, loaded at boot");
        break;
    }
    case CAT_MEM: {
        cap *c = cap_get("memory", 5000);
        GPtrArray *r = cap_row(c, "System Physical Memory"), *s = cap_row(c, "System Paging File (swap)");
        const char *inst = cap_kv(c, "Installed (SMBIOS)"), *edac = cap_kv(c, "EDAC");
        F("Installed", *inst ? inst : cell(r, 3), "Memory the board reports, and how many slots are filled (SMBIOS)");
        before_paren(cap_kv(c, "Memory Speed"), b, sizeof b);
        snprintf(t, sizeof t, "The speed the memory runs at now: %s", cap_kv(c, "Memory Speed"));
        F("Speed", b, t);
        if (r) {
            snprintf(b, sizeof b, "%s  (%s of %s)", cell(r, 4), cell(r, 1), cell(r, 3));
            F("In use", b, "Memory used by programs now. Cache and buffers don't count: Linux frees them when programs need memory.");
            f[n - 1].level = atof(cell(r, 4)) / 100.0;
        }
        if (s) { snprintf(b, sizeof b, "%s of %s", cell(s, 1), cell(s, 3)); F("Swap in use", b, "Swap space (on disk or zram) used now, and its size"); }
        if (strstr(edac, "no memory controllers")) snprintf(b, sizeof b, "Not available (no ECC)");
        else snprintf(b, sizeof b, "%s", edac);
        F("Error checking", b, "ECC memory finds and corrects memory errors; Linux reports them through EDAC");
        break;
    }
    case CAT_BOARD: {
        cap *c = cap_get("machine", 60000);
        const char *bd = sys_board(), *bios = strstr(bd, " BIOS ");
        GPtrArray *br = cap_row_prefix(c, "BIOS "), *boot = cap_row(c, "System Boot");
        snprintf(b, sizeof b, "%.*s", (int)(bios ? (size_t)(bios - bd) : strlen(bd)), bd);
        F("Motherboard", b, "Board maker and model (SMBIOS)");
        if (bios) {
            char v[120];
            part(cell(br, 3), 0, v, sizeof v);
            snprintf(b, sizeof b, "%s%s%s", bios + 6, *v ? " - " : "", v);
            F("BIOS", b, "Firmware version and date, and who wrote it. Board makers publish newer versions on their support page.");
        }
        if (boot) {
            char a[100], s2[100];
            part(cell(boot, 3), 0, a, sizeof a);
            part(cell(boot, 3), 1, s2, sizeof s2);
            snprintf(b, sizeof b, "%s%s%s", a, *s2 ? ", " : "", s2);
            F("Firmware type", b, "UEFI or legacy BIOS boot, and whether Secure Boot is on");
            F("Last boot", cell(boot, 2), "What the firmware reported about the last start-up");
        }
        break;
    }
    case CAT_GPU: {
        cap *g = cap_get("gpu-info", 10000), *d = cap_get("graphics", 60000);
        char v1[100], v2[100];
        guint i;
        int nd = 0;
        F("Graphics card", cap_kv(g, "GPU"), "The graphics processor (GPU) and its product name");
        part(cap_kv(g, "VRAM"), 0, v1, sizeof v1);
        part(cap_kv(g, "VRAM"), 1, v2, sizeof v2);
        snprintf(b, sizeof b, "%s%s%s", v1, *v2 ? ", " : "", v2);
        F("Video memory", b, "Memory on the graphics card (VRAM) and how much of it is in use");
        part(cap_kv(g, "Kernel driver"), 0, v1, sizeof v1);
        F("Driver", v1, "The Linux kernel driver in use for this card");
        *b = 0;
        for (i = 0; i < d->rows->len; i++) {
            GPtrArray *r = g_ptr_array_index(d->rows, i);
            if (strcmp(cell(r, 0), "Resolution")) continue;
            if (!strcmp(cell(r, 1), "-") || !*cell(r, 1)) { nd++; continue; }
            if (nd++ && *b) g_strlcat(b, ", ", sizeof b);
            g_strlcat(b, cell(r, 1), sizeof b);
        }
        if (nd) { char x[400]; snprintf(x, sizeof x, "%d%s%s", nd, *b ? ": " : "", b); F("Displays", x, "Monitors that are on, and their resolution @ refresh rate (Hz)"); }
        if (g_hs.gpu_temp > 0) {
            snprintf(b, sizeof b, "%.0f °C, %.0f %% busy", g_hs.gpu_temp, g_hs.gpu_load);
            F("Temperature", b, "The GPU's hottest point (junction / hotspot sensor) and how busy it is");
        }
        part(cap_kv(g, "Architecture"), 0, v1, sizeof v1);
        F("Architecture", v1, "The chip family");
        break;
    }
    case CAT_STORAGE: {
        cap *d = cap_get("drives", 30000);
        smartsum m;
        volsum v;
        before_paren(cap_kv(d, "Drives"), b, sizeof b);
        F("Drives", b, "Physical drives (loop, zram and device-mapper devices left out)");
        smart_sum(&m);
        if (!m.total) snprintf(b, sizeof b, "No SMART data yet (Run as root)");
        else {
            snprintf(b, sizeof b, "%d PASSED", m.passed);
            if (m.failed) { size_t l = strlen(b); snprintf(b + l, sizeof b - l, ", %d FAILED", m.failed); }
            if (m.unknown) { size_t l = strlen(b); snprintf(b + l, sizeof b - l, ", %d can't report", m.unknown); }
        }
        F("Drive health", b, "The drives' own self-check (SMART). USB adapters usually can't pass it on.");
        root_volume(&v);
        if (v.ok) {
            snprintf(b, sizeof b, "%s free of %s", v.free, v.total);
            F("System disk ( / )", b, "Free space on the file system Linux runs from");
            f[n - 1].level = v.used;
        }
        if (m.tmax > 0) { snprintf(b, sizeof b, "%.0f °C (%s)", m.tmax, m.tdev); F("Warmest drive", b, "Highest drive temperature from SMART / the drive's own sensor"); }
        break;
    }
    case CAT_NET: {
        char **ifs;
        int ni = dir_list("/sys/class/net", &ifs), i, phys = 0, up = 0, virt = 0;
        char con[300] = "";
        for (i = 0; i < ni; i++) {
            char p[300], st[32] = "";
            long sp = 0;
            int wifi;
            if (!strcmp(ifs[i], "lo")) continue;
            snprintf(p, sizeof p, "/sys/class/net/%s/device", ifs[i]);
            if (!path_exists(p)) { virt++; continue; }
            phys++;
            snprintf(p, sizeof p, "/sys/class/net/%s/wireless", ifs[i]);
            wifi = path_exists(p);
            snprintf(p, sizeof p, "/sys/class/net/%s/operstate", ifs[i]);
            rd_str(p, st, sizeof st);
            if (strcmp(st, "up")) continue;
            up++;
            snprintf(p, sizeof p, "/sys/class/net/%s/speed", ifs[i]);
            if (!wifi) rd_long(p, &sp);
            if (*con) g_strlcat(con, ", ", sizeof con);
            if (wifi) snprintf(b, sizeof b, "%s (Wi-Fi)", ifs[i]);
            else if (sp >= 1000) snprintf(b, sizeof b, "%s (wired, %g Gbps)", ifs[i], sp / 1000.0);
            else if (sp > 0) snprintf(b, sizeof b, "%s (wired, %ld Mbps)", ifs[i], sp);
            else snprintf(b, sizeof b, "%s (wired)", ifs[i]);
            g_strlcat(con, b, sizeof con);
        }
        dir_free(ifs, ni);
        snprintf(b, sizeof b, "%d, %d connected", phys, up);
        F("Network adapters", b, "Network hardware (wired, Wi-Fi, USB); virtual adapters are counted separately");
        F("Connected", *con ? con : "none", "Adapters with a link now, and the link speed of wired ones");
        snprintf(b, sizeof b, "%d", virt);
        F("Virtual adapters", b, "Bridges, VPNs, containers and virtual machines' networks");
        break;
    }
    case CAT_SENSORS: {
        hwsum h;
        if (g_hs.cpu_temp > 0) {
            snprintf(b, sizeof b, "%.0f °C", g_hs.cpu_temp);
            snprintf(t, sizeof t, "Hottest CPU sensor (%s)", g_hs.cpu_temp_name);
            F("CPU", b, t);
        }
        if (g_hs.gpu_temp > 0) { snprintf(b, sizeof b, "%.0f °C", g_hs.gpu_temp); F("Graphics (hotspot)", b, "The GPU's hottest point (junction sensor)"); }
        if (g_hs.sio_temp > 0) {
            snprintf(b, sizeof b, "%.0f °C", g_hs.sio_temp);
            snprintf(t, sizeof t, "Hottest motherboard sensor (%s)", g_hs.sio_name);
            F("Motherboard", b, t);
        }
        hw_sum(&h);
        snprintf(b, sizeof b, "%d", h.chips);
        F("Sensor chips", b, "Hardware monitor chips Linux can read (hwmon)");
        snprintf(b, sizeof b, "%d", h.alarms);
        F("Alarm flags", b, "Alarms the monitor chips raise for out-of-range readings; 0 is normal");
        break;
    }
    case CAT_DEVICES: {
        srs_dev *d = NULL;
        int np = srs_devices(BUS_PCI, &d), nu;
        free(d);
        d = NULL;
        nu = srs_devices(BUS_USB, &d);
        free(d);
        snprintf(b, sizeof b, "%d", np < 0 ? 0 : np);
        F("PCI devices", b, "Chips and cards on the PCI Express bus, including the ones built into the CPU and board");
        snprintf(b, sizeof b, "%d", nu < 0 ? 0 : nu);
        F("USB devices", b, "USB devices, including the hubs built into the PC");
        break;
    }
    case CAT_OS: {
        cap *c = cap_get("windows", 60000);
        struct utsname u;
        char *pn = g_get_os_info(G_OS_INFO_KEY_PRETTY_NAME), a[60], s2[60];
        F("Distribution", pn, "The Linux distribution (/etc/os-release)");
        g_free(pn);
        if (!uname(&u)) { F("Kernel", u.release, "The running Linux kernel"); F("Architecture", u.machine, NULL); }
        part(cap_kv(c, "Linux Desktop Session"), 0, a, sizeof a);
        part(cap_kv(c, "Linux Desktop Session"), 1, s2, sizeof s2);
        snprintf(b, sizeof b, "%s%s%s%s", a, *s2 ? " (" : "", s2, *s2 ? ")" : "");
        F("Desktop", b, "Desktop environment and display system (Wayland or X11)");
        fmt_uptime(b, sizeof b);
        F("Up time", b, "Time since the last start-up");
        break;
    }
    default: break;
    }
#undef F
    return n;
}

/* ---- "Is my PC OK?" ---- */
typedef struct { const char *name; int level; char text[300], tip[400]; const char *item; int show; } check;

static void checks_collect(check *c)
{
    smartsum m;
    volsum v;
    hwsum h;
    errsum e;
    long thr = throttle_count();
    GPtrArray *mr;
    int i;
    memset(c, 0, sizeof(check) * NCHECK);
    for (i = 0; i < NCHECK; i++) { c[i].show = 1; c[i].level = LV_UNK; }

    c[0].name = "CPU temperature"; c[0].item = "sensors";
    {
        const char *src = "a general limit (this CPU's own maximum is not known)";
        double lim = cpu_limit(&src), hot = lim > 0 ? lim - 2 : 95, warm = lim > 0 ? lim - 10 : 85;
        if (g_hs.cpu_temp > 0) {
            double x = g_hs.cpu_temp;
            c[0].level = temp_level(x, warm, hot);
            snprintf(c[0].text, sizeof c[0].text, "%.0f °C - %s", x, x >= hot ? "at its limit. The CPU is slowing itself down to stay cool; check the cooler and airflow."
                     : x >= warm ? "warm. Fine under heavy load; if it stays this high when idle, check the cooler." : "normal");
        } else snprintf(c[0].text, sizeof c[0].text, "No CPU temperature sensor found (k10temp / coretemp not loaded?)");
        if (lim > 0)
            snprintf(c[0].tip, sizeof c[0].tip, "Hottest CPU sensor (%s). Limit %.0f °C, from %s. OK below %.0f °C, warm %.0f-%.0f °C, at the limit from %.0f °C.",
                     *g_hs.cpu_temp_name ? g_hs.cpu_temp_name : "none", lim, src, warm, warm, hot - 1, hot);
        else
            snprintf(c[0].tip, sizeof c[0].tip, "Hottest CPU sensor (%s). This CPU's maximum is not known, so %s is used: OK below 85 °C, warm 85-94 °C, hot from 95 °C.",
                     *g_hs.cpu_temp_name ? g_hs.cpu_temp_name : "none", src);
    }

    c[1].name = "Graphics temperature"; c[1].item = "gpu-temp";
    {
        double lim = gpu_limit(), hot = lim > 0 ? lim - 5 : 105, warm = lim > 0 ? lim - 15 : 95;
        if (g_hs.gpu_temp > 0) {
            double x = g_hs.gpu_temp;
            c[1].level = temp_level(x, warm, hot);
            snprintf(c[1].text, sizeof c[1].text, "%.0f °C hotspot - %s", x, x >= hot ? "close to its limit. Check the card's fans and the case airflow."
                     : x >= warm ? "warm. Normal for a short heavy game or benchmark." : "normal");
        } else snprintf(c[1].text, sizeof c[1].text, "No graphics temperature sensor found");
        if (lim > 0)
            snprintf(c[1].tip, sizeof c[1].tip, "The GPU's hottest point (junction sensor), which runs hotter than the edge. The card's own critical "
                     "temperature is %.0f °C. OK below %.0f °C, warm %.0f-%.0f °C, close to the limit from %.0f °C.", lim, warm, warm, hot - 1, hot);
        else
            snprintf(c[1].tip, sizeof c[1].tip, "The GPU's hottest point (junction sensor). The card publishes no limit, so a general one is used: "
                     "OK below 95 °C, warm 95-104 °C, hot from 105 °C.");
    }

    smart_sum(&m);
    c[2].name = "Drive health (SMART)"; c[2].item = "smart";
    if (!m.total || m.passed + m.failed == 0) {
        snprintf(c[2].text, sizeof c[2].text, "Not checked yet. Use Menu > Run as root once to read the drives' self-checks.");
    } else if (m.failed) {
        c[2].level = LV_BAD;
        snprintf(c[2].text, sizeof c[2].text, "%d drive%s report%s FAILED - back up the data on %s now.", m.failed, m.failed > 1 ? "s" : "",
                 m.failed > 1 ? "" : "s", m.failed > 1 ? "them" : "it");
    } else {
        c[2].level = LV_OK;
        snprintf(c[2].text, sizeof c[2].text, "%d drive%s passed%s", m.passed, m.passed > 1 ? "s" : "",
                 m.unknown ? "; USB drives can't report SMART" : "");
    }
    snprintf(c[2].tip, sizeof c[2].tip, "Each drive's own health self-test (SMART overall assessment), from srs-root's cached smartctl / nvme data.");

    c[3].name = "Drive temperature"; c[3].item = "smart";
    if (m.tmax > 0) {
        /* judged by the drive closest to its own warning temperature */
        double hot = m.tlim > 0 ? m.tlim : 70, warm = hot - 10;
        c[3].level = temp_level(m.tw, warm, hot);
        if (c[3].level == LV_OK)
            snprintf(c[3].text, sizeof c[3].text, "Warmest %.0f °C (%s) - normal", m.tmax, m.tdev);
        else
            snprintf(c[3].text, sizeof c[3].text, "%.0f °C (%s) - %s", m.tw, m.twdev, c[3].level == LV_BAD
                     ? "at the drive's own warning temperature; it may slow down. Add airflow or a heatsink." : "warm, getting near the drive's own limit");
    } else snprintf(c[3].text, sizeof c[3].text, "No drive temperatures yet");
    snprintf(c[3].tip, sizeof c[3].tip, "Each drive is compared with its own warning temperature (NVMe / drivetemp temp1_max) where it publishes one, "
             "otherwise with 70 °C. Warm from 10 °C below that.");

    err_sum(&e);
    c[4].name = "Hardware errors"; c[4].item = "memory";
    if (e.mce || e.ue || e.corrupt_kb) {
        c[4].level = LV_BAD;
        snprintf(c[4].text, sizeof c[4].text, "%s%s%s", e.mce ? "The processor reported a hardware error (machine check) since start-up. " : "",
                 e.ue ? "Uncorrected memory errors were found. " : "", e.corrupt_kb ? "Linux has taken faulty memory pages out of use." : "");
    } else if (e.ce) {
        c[4].level = LV_WARN;
        snprintf(c[4].text, sizeof c[4].text, "%ld corrected memory error%s (ECC fixed them). A few are harmless; many point to a failing stick.", e.ce, e.ce > 1 ? "s" : "");
    } else {
        c[4].level = LV_OK;
        snprintf(c[4].text, sizeof c[4].text, "None reported%s", e.edac ? "" : " (memory errors are only counted with ECC memory)");
    }
    snprintf(c[4].tip, sizeof c[4].tip, "Kernel taint flag M (machine check), EDAC error counters (ECC memory) and HardwareCorrupted pages in /proc/meminfo.");

    root_volume(&v);
    c[5].name = "Free disk space"; c[5].item = "volumes";
    if (v.ok) {
        c[5].level = v.used >= 0.97 ? LV_BAD : v.used >= 0.90 ? LV_WARN : LV_OK;
        snprintf(c[5].text, sizeof c[5].text, "%s free on the system disk (%.0f %% used)%s", v.free, v.used * 100,
                 v.used >= 0.90 ? " - running low; updates may fail when it is full." : "");
    } else snprintf(c[5].text, sizeof c[5].text, "Could not read the system disk");
    snprintf(c[5].tip, sizeof c[5].tip, "The file system mounted at /. Low from 90 %% used, critical from 97 %%.");

    c[6].name = "Memory in use"; c[6].item = "memory";
    /* fetched here, not earlier: other checks call cap_get() and may refresh the cached "memory" item */
    mr = cap_row(cap_get("memory", 5000), "System Physical Memory");
    if (mr) {
        double l = atof(cell(mr, 4));
        c[6].level = l >= 95 ? LV_BAD : l >= 90 ? LV_WARN : LV_OK;
        snprintf(c[6].text, sizeof c[6].text, "%.0f %% (%s of %s)%s", l, cell(mr, 1), cell(mr, 3),
                 l >= 90 ? " - nearly full; programs may slow down or be closed." : "");
    } else snprintf(c[6].text, sizeof c[6].text, "Could not read the memory figures");
    snprintf(c[6].tip, sizeof c[6].tip, "Memory used by programs. Nearly full from 90 %%.");

    hw_sum(&h);
    c[7].name = "Sensor alarms"; c[7].item = "hw-status";
    if (h.chips) {
        c[7].level = h.alarms ? LV_WARN : LV_OK;
        if (h.alarms) snprintf(c[7].text, sizeof c[7].text, "%d alarm flag%s set by the monitor chips - see Sensors > Hardware status", h.alarms, h.alarms > 1 ? "s" : "");
        else snprintf(c[7].text, sizeof c[7].text, "No alarms from %d sensor chip%s", h.chips, h.chips > 1 ? "s" : "");
    } else snprintf(c[7].text, sizeof c[7].text, "No hardware monitor chips found");
    snprintf(c[7].tip, sizeof c[7].tip, "Alarm flags the hwmon chips set when a reading leaves its limits.");

    c[8].name = "CPU throttling"; c[8].item = "cpu-clocks";
    if (thr < 0) c[8].show = 0;   /* only Intel CPUs count throttling events */
    else {
        c[8].level = thr ? LV_WARN : LV_OK;
        if (thr) snprintf(c[8].text, sizeof c[8].text, "Slowed down %ld time%s since start-up because it was too hot", thr, thr > 1 ? "s" : "");
        else snprintf(c[8].text, sizeof c[8].text, "Never slowed down by heat since start-up");
    }
    snprintf(c[8].tip, sizeof c[8].tip, "Thermal throttling events counted by the CPU (thermal_throttle/core_throttle_count).");
}

/* ---- widgets ---- */
static GtkWidget *label_cls(const char *text, const char *cls, float xalign)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    if (cls) {
        char **v = g_strsplit(cls, " ", -1), **p;
        for (p = v; *p; p++) gtk_style_context_add_class(gtk_widget_get_style_context(l), *p);
        g_strfreev(v);
    }
    return l;
}

static const char *icon_pick(const char *list)
{
    static char out[8][96];
    static int k;
    char **v = g_strsplit(list, "|", -1), **p;
    GtkIconTheme *th = gtk_icon_theme_get_default();
    const char *r = NULL;
    k = (k + 1) % 8;
    for (p = v; *p; p++) if (gtk_icon_theme_has_icon(th, *p)) { r = *p; break; }
    snprintf(out[k], sizeof out[k], "%s", r ? r : v[g_strv_length(v) - 1]);
    g_strfreev(v);
    return out[k];
}

static void set_level_class(GtkWidget *w, int level)
{
    GtkStyleContext *sc = gtk_widget_get_style_context(w);
    gtk_style_context_remove_class(sc, "srs-ok");
    gtk_style_context_remove_class(sc, "srs-warn");
    gtk_style_context_remove_class(sc, "srs-bad");
    gtk_style_context_remove_class(sc, "srs-unk");
    gtk_style_context_add_class(sc, level == LV_OK ? "srs-ok" : level == LV_WARN ? "srs-warn" : level == LV_BAD ? "srs-bad" : "srs-unk");
}

static swin *home_cur_tab(void)
{
    GtkWidget *page;
    if (!g_home || g_home->cur == CAT_OVERVIEW || !g_home->nb[g_home->cur]) return NULL;
    page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(g_home->nb[g_home->cur]), gtk_notebook_get_current_page(GTK_NOTEBOOK(g_home->nb[g_home->cur])));
    return page ? g_object_get_data(G_OBJECT(page), "swin") : NULL;
}

static int cat_of_id(const char *id)
{
    char base[64];
    const srs_item *s;
    const char *g;
    int c, i;
    snprintf(base, sizeof base, "%.*s", (int)strcspn(id, ":"), id);
    for (c = 1; c < CAT_N; c++)
        for (i = 0; g_cats[c].tabs[i]; i++) if (!strcmp(base, g_cats[c].tabs[i])) return c;
    s = srs_section_find(base);
    g = s && s->group ? s->group : "";
    if (!strcmp(g, "cpu")) return CAT_CPU;
    if (!strcmp(g, "machine"))
        return !strncmp(base, "dimm", 4) || !strncmp(base, "spd", 3) || !strncmp(base, "mem", 3) || !strcmp(base, "physical") ||
               !strncmp(base, "numa", 4) ? CAT_MEM : CAT_BOARD;
    if (!strcmp(g, "sensors")) return CAT_SENSORS;
    if (!strcmp(g, "storage")) return CAT_STORAGE;
    if (!strcmp(g, "network")) return CAT_NET;
    if (!strcmp(g, "os")) return CAT_OS;
    if (!strcmp(g, "gpu")) return CAT_GPU;
    if (!strcmp(g, "bus")) return CAT_DEVICES;
    if (!strcmp(g, "tools") && (!strncmp(base, "benchmark", 9) || !strncmp(base, "ipi", 3) || !strcmp(base, "latency") ||
                                !strcmp(base, "srs-pi") || !strcmp(base, "crypto-bench") || !strcmp(base, "prime-sieve") ||
                                !strcmp(base, "bench-history") || !strcmp(base, "cache-memory") || !strcmp(base, "storage-bench") ||
                                !strcmp(base, "os-latency") || !strcmp(base, "cpu-compute") || !strcmp(base, "everyday-work") ||
                                !strcmp(base, "burnin") || !strcmp(base, "cpu-stress"))) return CAT_BENCH;
    return CAT_TOOLS;
}

/* ---- section tabs (an embedded panel: same swin, text view and render() as a classic panel) ---- */
/* a tab comes on screen: draw it the first time (live items every time), start its refresh timer */
static void on_tab_map(GtkWidget *v, gpointer d)
{
    swin *t = d;
    (void)v;
    t->shown = 1;
    if (!t->rendered || t->own_ms > 0) render(t);
    t->rendered = 1;
    live_start(t);
}
static void on_tab_unmap(GtkWidget *v, gpointer d) { swin *t = d; (void)v; t->shown = 0; if (t->timer) { g_source_remove(t->timer); t->timer = 0; } }
static void on_tab_destroy(GtkWidget *p, gpointer d)
{
    swin *t = d;
    (void)p;
    if (t->timer) g_source_remove(t->timer);
    if (t->commit_id) g_source_remove(t->commit_id);
    if (t->scratch) g_object_unref(t->scratch);
    if (t->rec) g_array_free(t->rec, TRUE);
    g_free(t->plain);
    { int b; for (b = 0; b < TS_MAXBLK; b++) g_free(t->blk_head[b]); }
    g_ptr_array_remove(g_wins, t);
    g_free(t);
}
static void on_tab_btn(GtkButton *b, gpointer d)
{
    swin *t = d;
    const char *tg = g_object_get_data(G_OBJECT(b), "target");
    if (!strcmp(tg, "!pop")) open_section(t->id);
    else if (!strcmp(tg, "!root")) run_root();
    else if (!strcmp(tg, "!resetview")) tbl_reset(t);
    else if (!strcmp(tg, "!copy") && !t->rendered) {   /* not drawn yet (Startup shows its manager first) */
        render(t);
        t->rendered = 1;
        if (t->commit_id) { g_source_remove(t->commit_id); commit_idle(t); }
        copy_all(t);
    }
    else do_target(t, tg);
}
static void on_tab_live(GObject *sw, GParamSpec *ps, gpointer d)
{
    swin *t = d;
    (void)ps;
    t->paused = !gtk_switch_get_active(GTK_SWITCH(sw));
    live_start(t);
}

static GtkWidget *tool_btn(swin *t, const char *icon, const char *label, const char *target, const char *tip)
{
    GtkWidget *b = gtk_button_new_with_mnemonic(label);
    if (icon) {
        gtk_button_set_image(GTK_BUTTON(b), gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON));
        gtk_button_set_always_show_image(GTK_BUTTON(b), TRUE);
    }
    gtk_widget_set_tooltip_text(b, tip);
    g_object_set_data(G_OBJECT(b), "target", (gpointer)target);
    g_signal_connect(b, "clicked", G_CALLBACK(on_tab_btn), t);
    return b;
}

static GtkWidget *tab_label_new(swin *t);
static void tabs_changed(int cat);
static int g_tabs_busy;   /* pages being (re)built: don't treat it as the user's edit */

/* the page's tab for an item, or NULL */
static swin *tab_find(int cat, const char *id, int *index)
{
    GtkNotebook *nb = GTK_NOTEBOOK(g_home->nb[cat]);
    int i, n = gtk_notebook_get_n_pages(nb);
    for (i = 0; i < n; i++) {
        swin *x = g_object_get_data(G_OBJECT(gtk_notebook_get_nth_page(nb, i)), "swin");
        if (x && !strcmp(x->id, id)) { if (index) *index = i; return x; }
    }
    return NULL;
}

/* temp: opened from the item list, search, Details or a lookup (not kept); pos -1 = at the end */
static swin *tab_add_at(int cat, const char *id, int select, int temp, int pos)
{
    GtkNotebook *nb = GTK_NOTEBOOK(g_home->nb[cat]);
    const srs_item *it = srs_section_find(id);
    GtkWidget *page, *bar, *src;
    swin *t;
    char s[600];
    int i;
    if ((t = tab_find(cat, id, &i))) {
        if (!temp && t->temp) { t->temp = 0; tab_label_new(t); tabs_changed(cat); }   /* kept from now on */
        if (select) gtk_notebook_set_current_page(nb, i);
        return t;
    }
    t = g_new0(swin, 1);
    t->kind = W_SECTION;
    t->embedded = 1;
    t->cat = cat;
    t->win = g_home->w->win;
    snprintf(t->id, sizeof t->id, "%s", id);
    t->own_ms = it && it->live_ms > 0 ? (it->live_ms < 500 ? 500 : it->live_ms) : 0;
    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_set_data(G_OBJECT(page), "swin", t);
    t->page = page;

    bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(bar), "srs-tool");
    gtk_style_context_add_class(gtk_widget_get_style_context(bar), "srs-tabbar");
    snprintf(s, sizeof s, "Source: %s", it && it->linux_src ? it->linux_src : "-");
    src = label_cls(s, "dim-label srs-cap", 0);
    gtk_label_set_ellipsize(GTK_LABEL(src), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(src, TRUE);
    snprintf(s, sizeof s, "Item [%s] - data from %s%s", id, it && it->linux_src ? it->linux_src : "-",
             it && it->needs_root ? ". Some of it needs root data (Run as root)." : "");
    gtk_widget_set_tooltip_text(src, s);
    gtk_box_pack_start(GTK_BOX(bar), src, TRUE, TRUE, 4);
    if (it && it->needs_root && !so_root_file("collected.txt"))
        gtk_box_pack_start(GTK_BOX(bar), tool_btn(t, "dialog-password-symbolic", "Needs root data...", "!root",
                           "Parts of this item can only be read as root. Collect them once with one password prompt (read-only)."), FALSE, FALSE, 0);
    if (t->own_ms > 0) {
        GtkWidget *sw = gtk_switch_new(), *ll = gtk_label_new_with_mnemonic("_Live");
        snprintf(s, sizeof s, "Refresh this view every %.1f s while it is shown. Menu > Options > Update interval changes the rate for all views.",
                 (g_update_ms > 0 ? g_update_ms : t->own_ms) / 1000.0);
        gtk_switch_set_active(GTK_SWITCH(sw), TRUE);
        gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
        gtk_label_set_mnemonic_widget(GTK_LABEL(ll), sw);
        gtk_widget_set_tooltip_text(sw, s);
        gtk_widget_set_tooltip_text(ll, s);
        g_signal_connect(sw, "notify::active", G_CALLBACK(on_tab_live), t);
        gtk_box_pack_start(GTK_BOX(bar), ll, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bar), sw, FALSE, FALSE, 4);
    }
    if (!strcmp(id, "sensors")) {
        gtk_box_pack_start(GTK_BOX(bar), tool_btn(t, NULL, "_Zero", "!zero", "Start the minimum and maximum columns again from now"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bar), tool_btn(t, NULL, "_Hide unused", "!hide", "Hide or show sensors that read nothing (press again to show them)"), FALSE, FALSE, 0);
    }
    t->reset_btn = tool_btn(t, "view-sort-ascending-symbolic", "Reset _view", "!resetview",
                            "Put the tables back in the item's own order. Click a column heading to sort by it: "
                            "first click smallest or A first (\u25B2), second click largest or Z first (\u25BC).");
    gtk_widget_set_no_show_all(t->reset_btn, TRUE);
    gtk_box_pack_start(GTK_BOX(bar), t->reset_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), tool_btn(t, "view-refresh-symbolic", "_Refresh", "!refresh", "Read this item again now (F5)"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), tool_btn(t, "edit-copy-symbolic", "_Copy", "!copy",
                       "Copy all of this text. With Options > Redact on, serials, UUIDs, MACs and the computer name are masked."), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), tool_btn(t, "help-browser-symbolic", "_Guide", "!guide", "Open the user guide at this item: what each value means (F1)"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), tool_btn(t, "view-dual-symbolic", "Open in _window", "!pop",
                       "Open this item as a classic SRS panel with its SIV-style button row"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), bar, FALSE, FALSE, 0);
    {
        GtkWidget *sw = make_view(t);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_NONE);
        if (!strcmp(id, "startup")) {   /* Startup: the report (read-only, shown first) and the manager (switches, Undo, Add) */
            GtkWidget *st = gtk_stack_new(), *ss = gtk_stack_switcher_new();
            gtk_stack_add_titled(GTK_STACK(st), sw, "report", "Report");   /* the first child: the tab opens on it */
            gtk_stack_add_titled(GTK_STACK(st), startup_manager_new(), "manage", "Manage");
            gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(ss), GTK_STACK(st));
            gtk_widget_set_tooltip_text(ss, "Report (where the tab opens): the Startup programs item as text, read-only (what Copy and "
                                            "Save Report give). Manage: turn startup items off and on, add one.");
            gtk_box_pack_start(GTK_BOX(bar), ss, FALSE, FALSE, 0);
            gtk_box_reorder_child(GTK_BOX(bar), ss, 1);
            gtk_box_pack_start(GTK_BOX(page), st, TRUE, TRUE, 0);
        } else gtk_box_pack_start(GTK_BOX(page), sw, TRUE, TRUE, 0);
    }
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(t->view), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(t->view), 8);
    g_signal_connect(t->view, "map", G_CALLBACK(on_tab_map), t);
    g_signal_connect(t->view, "unmap", G_CALLBACK(on_tab_unmap), t);
    g_signal_connect(page, "destroy", G_CALLBACK(on_tab_destroy), t);
    g_ptr_array_add(g_wins, t);

    t->temp = temp;
    gtk_widget_show_all(page);
    i = gtk_notebook_insert_page(nb, page, NULL, pos);
    tab_label_new(t);
    gtk_notebook_set_tab_reorderable(nb, page, TRUE);
    if (select) gtk_notebook_set_current_page(nb, i);
    return t;
}

static swin *tab_add(int cat, const char *id, int select) { return tab_add_at(cat, id, select, 1, -1); }

/* ---- navigation ---- */
static void facts_update(int cat);
static void ov_update(void);

static void home_subtitle(const char *extra)
{
    char s[400];
    if (!g_home) return;
    snprintf(s, sizeof s, "%s%s%s", g_cats[g_home->cur].title, extra && *extra ? "  -  " : "", extra ? extra : "");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(g_home->hb), s);
}

static void home_go(int cat)
{
    GtkListBoxRow *r;
    if (!g_home || cat < 0 || cat >= CAT_N) return;
    r = gtk_list_box_get_row_at_index(GTK_LIST_BOX(g_home->side), cat);
    if (r) gtk_list_box_select_row(GTK_LIST_BOX(g_home->side), r);
}

static void on_side_row(GtkListBox *lb, GtkListBoxRow *row, gpointer d)
{
    int c;
    (void)lb; (void)d;
    if (!row || !g_home) return;
    c = gtk_list_box_row_get_index(row);
    g_home->cur = c;
    gtk_stack_set_visible_child_name(GTK_STACK(g_home->stack), g_cats[c].name);
    home_subtitle(NULL);
    if (c == CAT_OVERVIEW) ov_update(); else facts_update(c);
}

static void open_item(const char *id)
{
    int cat;
    if (!g_home) { open_section(id); return; }
    cat = cat_of_id(id);
    home_go(cat);
    tab_add(cat, id, 1);
    gtk_window_present(GTK_WINDOW(g_home->w->win));
}

static void on_go_cat(GtkButton *b, gpointer d) { (void)b; home_go(GPOINTER_TO_INT(d)); }

static void on_launch(GtkButton *b, gpointer d)
{
    const char *t = d;
    (void)b;
    if (!strcmp(t, "burn")) open_tool(W_BURN, 0);
    else if (!strcmp(t, "stress")) open_tool(W_BURN, 1);
    else if (!strcmp(t, "bench")) open_tool(W_BENCH, BK_BENCH);
    else if (!strcmp(t, "ipi")) open_tool(W_BENCH, BK_IPI);
    else if (!strcmp(t, "lat")) open_tool(W_BENCH, BK_LAT);
    else if (!strcmp(t, "pi")) open_tool(W_BENCH, BK_PI);
    else if (!strcmp(t, "crypto")) open_tool(W_BENCH, BK_CRYPTO);
    else if (!strcmp(t, "prime")) open_tool(W_BENCH, BK_PRIME);
    else if (!strcmp(t, "cachemem")) open_tool(W_BENCH, BK_CACHEMEM);
    else if (!strcmp(t, "storagebench")) open_tool(W_BENCH, BK_STORAGE);
    else if (bench_tool_sub(t) >= 0) open_tool(W_BENCH, bench_tool_sub(t));
    else if (!strcmp(t, "idlookup")) open_tool(W_IDLOOKUP, 0);
    else if (!strcmp(t, "find")) open_tool(W_FIND, 0);
    else if (!strcmp(t, "save")) save_dialog(0);
    else if (!strcmp(t, "saveall")) save_dialog(1);
    else if (!strcmp(t, "root")) run_root();
    else if (!strcmp(t, "guide")) open_guide(NULL);
    else if (!strcmp(t, "classic")) open_main(0);
}

/* ---- summary tiles ---- */
static void facts_update(int cat)
{
    factsui *u;
    fact f[NFACT];
    int n, i;
    if (!g_home || !(u = &g_home->facts[cat])->box) return;
    n = facts_for(cat, f, 6);   /* one row of tiles at the default size; the rest is in the tabs */
    if (n != u->n) {
        for (i = 0; i < u->n; i++) gtk_widget_destroy(u->tile[i]);
        for (i = 0; i < n; i++) {
            GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
            gtk_style_context_add_class(gtk_widget_get_style_context(v), "srs-fact");
            u->cap[i] = label_cls("", "dim-label srs-cap", 0);
            u->val[i] = label_cls("", "srs-val", 0);
            gtk_label_set_ellipsize(GTK_LABEL(u->val[i]), PANGO_ELLIPSIZE_END);
            gtk_label_set_max_width_chars(GTK_LABEL(u->val[i]), 44);
            gtk_box_pack_start(GTK_BOX(v), u->cap[i], FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(v), u->val[i], FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(u->box), v, TRUE, TRUE, 0);
            u->tile[i] = v;
            gtk_widget_show_all(v);
        }
        u->n = n;
    }
    for (i = 0; i < n; i++) {
        char tip[800];
        gtk_label_set_text(GTK_LABEL(u->cap[i]), f[i].cap);
        gtk_label_set_text(GTK_LABEL(u->val[i]), f[i].val);
        snprintf(tip, sizeof tip, "%s\n%s", f[i].val, f[i].tip);
        gtk_widget_set_tooltip_text(u->tile[i], tip);
    }
}

/* ---- Overview ---- */
static void ov_update(void)
{
    check c[NCHECK];
    int i, warn = 0, bad = 0, unk = 0, k;
    char s[600], up[64], *pn;
    time_t now = time(NULL);
    struct tm tm;
    if (!g_home || !g_home->ov_verdict) return;
    checks_collect(c);
    for (i = 0; i < NCHECK; i++) {
        checkui *u = &g_home->ck[i];
        gtk_widget_set_visible(u->dot, c[i].show);
        gtk_widget_set_visible(u->name, c[i].show);
        gtk_widget_set_visible(u->text, c[i].show);
        gtk_widget_set_visible(u->btn, c[i].show);
        if (!c[i].show) continue;
        if (c[i].level == LV_WARN) warn++;
        if (c[i].level == LV_BAD) bad++;
        if (c[i].level == LV_UNK) unk++;
        set_level_class(u->dot, c[i].level);
        gtk_label_set_text(GTK_LABEL(u->dot), c[i].level == LV_UNK ? "○" : "●");
        gtk_label_set_text(GTK_LABEL(u->name), c[i].name);
        snprintf(s, sizeof s, "%s%s", c[i].level == LV_OK ? "OK: " : c[i].level == LV_WARN ? "Check: " : c[i].level == LV_BAD ? "Problem: " : "Unknown: ", c[i].text);
        gtk_label_set_text(GTK_LABEL(u->text), s);
        gtk_widget_set_tooltip_text(u->text, c[i].tip);
        gtk_widget_set_tooltip_text(u->dot, c[i].tip);
        g_object_set_data(G_OBJECT(u->btn), "item", (gpointer)c[i].item);
    }
    if (bad + warn == 0 && unk) snprintf(s, sizeof s, "Nothing wrong found (%d check%s could not be made)", unk, unk > 1 ? "s" : "");
    else if (bad + warn == 0) snprintf(s, sizeof s, "Everything looks fine");
    else snprintf(s, sizeof s, "%d thing%s to look at", bad + warn, bad + warn > 1 ? "s" : "");
    gtk_label_set_text(GTK_LABEL(g_home->ov_verdict), s);
    gtk_image_set_from_icon_name(GTK_IMAGE(g_home->ov_icon), bad ? icon_pick("dialog-error-symbolic") : warn ? icon_pick("dialog-warning-symbolic")
                                 : icon_pick("emblem-ok-symbolic|object-select-symbolic"), GTK_ICON_SIZE_DIALOG);
    set_level_class(g_home->ov_icon, bad ? LV_BAD : warn ? LV_WARN : unk ? LV_UNK : LV_OK);
    localtime_r(&now, &tm);
    strftime(up, sizeof up, "%H:%M:%S", &tm);
    snprintf(s, sizeof s, "Checked at %s from the sensors, the drives' SMART data and the kernel's error counters. Hover over a line for the limits.", up);
    gtk_label_set_text(GTK_LABEL(g_home->ov_vsub), s);

    fmt_uptime(up, sizeof up);
    pn = g_get_os_info(G_OS_INFO_KEY_PRETTY_NAME);
    {
        const char *bd = sys_board(), *bios = strstr(bd, " BIOS ");
        snprintf(s, sizeof s, "%.*s   ·   %s   ·   up %s", (int)(bios ? (size_t)(bios - bd) : strlen(bd)), bd, pn ? pn : "Linux", up);
    }
    g_free(pn);
    gtk_label_set_text(GTK_LABEL(g_home->ov_sub), s);

    for (k = 0; k < NCARD; k++) {
        cardui *u = &g_home->card[k];
        fact f[NFACT];
        int n = facts_for(g_card_cats[k], f, NFACT), j;
        double lv = -1;
        gtk_label_set_text(GTK_LABEL(u->main), n ? f[0].val : "-");
        gtk_widget_set_tooltip_text(u->main, n ? f[0].tip : NULL);
        for (j = 0; j < 4; j++) {
            int on = j + 1 < n;
            gtk_widget_set_visible(u->k[j], on);
            gtk_widget_set_visible(u->v[j], on);
            if (!on) continue;
            gtk_label_set_text(GTK_LABEL(u->k[j]), f[j + 1].cap);
            gtk_label_set_text(GTK_LABEL(u->v[j]), f[j + 1].val);
            gtk_widget_set_tooltip_text(u->v[j], f[j + 1].tip);
        }
        for (j = 0; j < n; j++) if (f[j].level >= 0) { lv = f[j].level; break; }
        gtk_widget_set_visible(u->bar, lv >= 0);
        if (lv >= 0) gtk_level_bar_set_value(GTK_LEVEL_BAR(u->bar), lv > 1 ? 1 : lv);
    }
}

static void on_check_btn(GtkButton *b, gpointer d) { const char *it = g_object_get_data(G_OBJECT(b), "item"); (void)d; if (it) open_item(it); }

static GtkWidget *build_overview(void)
{
    GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL), *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14), *h, *hb, *grid, *fb, *t;
    int i;
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_set_border_width(GTK_CONTAINER(vb), 20);
    gtk_container_add(GTK_CONTAINER(sc), vb);

    h = label_cls("This PC", "srs-h1", 0);
    gtk_box_pack_start(GTK_BOX(vb), h, FALSE, FALSE, 0);
    g_home->ov_sub = label_cls("", "dim-label", 0);
    gtk_label_set_ellipsize(GTK_LABEL(g_home->ov_sub), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(vb), g_home->ov_sub, FALSE, FALSE, 0);

    /* "Is my PC OK?" */
    t = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(t), "srs-health");
    hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    g_home->ov_icon = gtk_image_new();
    gtk_widget_set_valign(g_home->ov_icon, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(hb), g_home->ov_icon, FALSE, FALSE, 0);
    {
        GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_box_pack_start(GTK_BOX(v), label_cls("Is my PC OK?", "dim-label srs-cap", 0), FALSE, FALSE, 0);
        g_home->ov_verdict = label_cls("", "srs-h2", 0);
        gtk_box_pack_start(GTK_BOX(v), g_home->ov_verdict, FALSE, FALSE, 0);
        g_home->ov_vsub = label_cls("", "dim-label srs-cap", 0);
        gtk_label_set_line_wrap(GTK_LABEL(g_home->ov_vsub), TRUE);
        gtk_box_pack_start(GTK_BOX(v), g_home->ov_vsub, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(hb), v, TRUE, TRUE, 0);
    }
    gtk_widget_set_margin_top(hb, 10);
    gtk_widget_set_margin_bottom(hb, 6);
    gtk_box_pack_start(GTK_BOX(t), hb, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(t), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 2);
    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
    for (i = 0; i < NCHECK; i++) {
        checkui *u = &g_home->ck[i];
        u->dot = label_cls("○", "srs-dot srs-unk", 0.5);
        u->name = label_cls("", "srs-val", 0);
        u->text = label_cls("", NULL, 0);
        gtk_label_set_line_wrap(GTK_LABEL(u->text), TRUE);
        gtk_widget_set_hexpand(u->text, TRUE);
        u->btn = gtk_button_new_with_label("Details");
        gtk_button_set_relief(GTK_BUTTON(u->btn), GTK_RELIEF_NONE);
        gtk_widget_set_tooltip_text(u->btn, "Show the full readings this line is based on");
        g_signal_connect(u->btn, "clicked", G_CALLBACK(on_check_btn), NULL);
        gtk_widget_set_no_show_all(u->dot, TRUE);
        gtk_widget_set_no_show_all(u->name, TRUE);
        gtk_widget_set_no_show_all(u->text, TRUE);
        gtk_widget_set_no_show_all(u->btn, TRUE);
        gtk_grid_attach(GTK_GRID(grid), u->dot, 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), u->name, 1, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), u->text, 2, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), u->btn, 3, i, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(t), grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), t, FALSE, FALSE, 0);

    /* key facts: one card per category */
    gtk_box_pack_start(GTK_BOX(vb), label_cls("At a glance", "srs-h2", 0), FALSE, FALSE, 4);
    fb = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(fb), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(fb), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(fb), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(fb), 3);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(fb), 12);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(fb), 12);
    for (i = 0; i < NCARD; i++) {
        cardui *u = &g_home->card[i];
        int c = g_card_cats[i], j;
        GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6), *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8), *g2, *b;
        char lab[100];
        gtk_style_context_add_class(gtk_widget_get_style_context(card), "srs-card");
        gtk_widget_set_size_request(card, 300, -1);
        gtk_box_pack_start(GTK_BOX(head), gtk_image_new_from_icon_name(icon_pick(g_cats[c].icons), GTK_ICON_SIZE_LARGE_TOOLBAR), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(head), label_cls(g_cats[c].title, "dim-label", 0), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(card), head, FALSE, FALSE, 0);
        u->main = label_cls("", "srs-big", 0);
        gtk_label_set_line_wrap(GTK_LABEL(u->main), TRUE);
        gtk_label_set_lines(GTK_LABEL(u->main), 2);
        gtk_label_set_ellipsize(GTK_LABEL(u->main), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(u->main), 30);
        gtk_box_pack_start(GTK_BOX(card), u->main, FALSE, FALSE, 0);
        g2 = gtk_grid_new();
        gtk_grid_set_column_spacing(GTK_GRID(g2), 12);
        gtk_grid_set_row_spacing(GTK_GRID(g2), 3);
        for (j = 0; j < 4; j++) {
            u->k[j] = label_cls("", "dim-label", 0);
            u->v[j] = label_cls("", NULL, 0);
            gtk_label_set_ellipsize(GTK_LABEL(u->v[j]), PANGO_ELLIPSIZE_END);
            gtk_label_set_max_width_chars(GTK_LABEL(u->v[j]), 24);
            gtk_widget_set_hexpand(u->v[j], TRUE);
            gtk_widget_set_no_show_all(u->k[j], TRUE);
            gtk_widget_set_no_show_all(u->v[j], TRUE);
            gtk_grid_attach(GTK_GRID(g2), u->k[j], 0, j, 1, 1);
            gtk_grid_attach(GTK_GRID(g2), u->v[j], 1, j, 1, 1);
        }
        gtk_box_pack_start(GTK_BOX(card), g2, FALSE, FALSE, 0);
        u->bar = gtk_level_bar_new_for_interval(0, 1);
        gtk_level_bar_add_offset_value(GTK_LEVEL_BAR(u->bar), GTK_LEVEL_BAR_OFFSET_LOW, 0.9);
        gtk_level_bar_add_offset_value(GTK_LEVEL_BAR(u->bar), GTK_LEVEL_BAR_OFFSET_HIGH, 0.97);
        gtk_level_bar_set_inverted(GTK_LEVEL_BAR(u->bar), FALSE);
        gtk_widget_set_no_show_all(u->bar, TRUE);
        gtk_box_pack_start(GTK_BOX(card), u->bar, FALSE, FALSE, 2);
        snprintf(lab, sizeof lab, "%s details", g_cats[c].title);
        b = gtk_button_new_with_label(lab);
        gtk_widget_set_halign(b, GTK_ALIGN_START);
        snprintf(lab, sizeof lab, "Open the %s page: every detail, live readings and the expert items", g_cats[c].title);
        gtk_widget_set_tooltip_text(b, lab);
        g_signal_connect(b, "clicked", G_CALLBACK(on_go_cat), GINT_TO_POINTER(c));
        gtk_box_pack_end(GTK_BOX(card), b, FALSE, FALSE, 0);
        gtk_flow_box_insert(GTK_FLOW_BOX(fb), card, -1);
        gtk_widget_set_can_focus(gtk_widget_get_parent(card), FALSE);
    }
    gtk_box_pack_start(GTK_BOX(vb), fb, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), label_cls("Everything else is on the pages on the left, and in the search box at the top. "
                       "Menu > Classic view opens the SIV-style windows.", "dim-label srs-cap", 0), FALSE, FALSE, 0);
    return sc;
}

/* a launcher card on the Benchmarks and Tools pages: opens an existing tool window (nothing starts by itself) */
/* a launcher row on the Benchmarks and Tools pages: opens an existing tool window (nothing starts by itself) */
static GtkWidget *launcher(const char *title, const char *text, const char *note, const char *btn, const char *target, const char *tip)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16), *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2), *l, *b;
    gtk_widget_set_margin_top(row, 8);
    gtk_widget_set_margin_bottom(row, 8);
    gtk_box_pack_start(GTK_BOX(v), label_cls(title, "srs-val", 0), FALSE, FALSE, 0);
    l = label_cls(text, NULL, 0);
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    gtk_box_pack_start(GTK_BOX(v), l, FALSE, FALSE, 0);
    if (note) {
        l = label_cls(note, "dim-label srs-cap", 0);
        gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
        gtk_box_pack_start(GTK_BOX(v), l, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(row), v, TRUE, TRUE, 0);
    b = gtk_button_new_with_mnemonic(btn);
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(b, 130, -1);
    gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect(b, "clicked", G_CALLBACK(on_launch), (gpointer)target);
    gtk_box_pack_end(GTK_BOX(row), b, FALSE, FALSE, 0);
    return row;
}

/* launcher rows, two columns, separated by lines */
static void grid_add3(GtkWidget *grid, GtkWidget *w)
{
    int n = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(grid), "n"));
    gtk_widget_set_hexpand(w, TRUE);
    if (n >= 2) gtk_grid_attach(GTK_GRID(grid), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), n % 2, (n / 2) * 2 - 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w, n % 2, (n / 2) * 2, 1, 1);
    g_object_set_data(G_OBJECT(grid), "n", GINT_TO_POINTER(n + 1));
}

static gint item_title_cmp(gconstpointer a, gconstpointer b)
{
    const srs_item *x = *(const srs_item *const *)a, *y = *(const srs_item *const *)b;
    return g_ascii_strcasecmp(x->title, y->title);
}

#include "gui_tabs.inc"

static GtkWidget *build_cat_page(int cat)
{
    const catdef *d = &g_cats[cat];
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0), *head = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8), *tr, *img, *l, *nb;
    gtk_widget_set_margin_start(head, 20);
    gtk_widget_set_margin_end(head, 20);
    gtk_widget_set_margin_top(head, 16);
    gtk_widget_set_margin_bottom(head, 12);
    tr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    img = gtk_image_new_from_icon_name(icon_pick(d->icons), GTK_ICON_SIZE_DND);
    gtk_box_pack_start(GTK_BOX(tr), img, FALSE, FALSE, 0);
    {
        GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_box_pack_start(GTK_BOX(v), label_cls(d->title, "srs-h1", 0), FALSE, FALSE, 0);
        l = label_cls(d->blurb, "dim-label", 0);
        gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
        gtk_box_pack_start(GTK_BOX(v), l, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(tr), v, TRUE, TRUE, 0);
    }
    gtk_box_pack_start(GTK_BOX(head), tr, FALSE, FALSE, 0);
    if (cat != CAT_BENCH && cat != CAT_TOOLS) {
        factsui *u = &g_home->facts[cat];
        u->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_start(GTK_BOX(head), u->box, FALSE, FALSE, 0);
    } else {
        GtkWidget *fb = gtk_grid_new();   /* launcher rows in a framed box */
        gtk_grid_set_column_homogeneous(GTK_GRID(fb), TRUE);
        gtk_grid_set_column_spacing(GTK_GRID(fb), 32);
        gtk_style_context_add_class(gtk_widget_get_style_context(fb), "srs-health");
        if (cat == CAT_BENCH) {
            grid_add3(fb, launcher("CPU Benchmark", "Dhrystone and Whetstone scores per CPU, in ten modes. Compare before and after a BIOS, kernel or cooling change.",
                "Busy for a few seconds to a minute, only while you press Run.", "_Benchmark...", "bench", "Open the Benchmark window. Nothing runs until you press Run."));
            grid_add3(fb, launcher("CPU IPI Delay", "How long the cores take to hand data to each other (core-to-core latency), as a table and heat map.",
                "Busy for a few seconds while it measures.", "_IPI Delay...", "ipi", "Open the IPI Delay window. Nothing runs until you press Run."));
            grid_add3(fb, launcher("Memory Latency", "How quickly the caches and the RAM answer, by block size, with a graph.",
                "Busy for a few seconds while it measures.", "_Latency...", "lat", "Open the Latency window. Nothing runs until you press Run."));
            grid_add3(fb, launcher("Cache & Memory", "How fast the RAM and the L1, L2 and L3 caches read, write and copy, and how long they take to answer.",
                "Uses every CPU thread and the memory bus for about 20 seconds. Asks before it starts.", "Cache & _Memory...", "cachemem",
                "Open the Cache & Memory window. Nothing runs until you press Run and confirm."));
            grid_add3(fb, launcher("Storage", "How fast the drive that holds a folder you choose reads and writes: sequential, 4K random and response time.",
                "Works on its own temporary test file only; never touches your files. USB drives only when you allow them. Asks before it starts.",
                "St_orage...", "storagebench", "Open the Storage benchmark window. Nothing runs until you press Run and confirm."));
            grid_add3(fb, launcher("OS Latency", "How quickly the operating system answers a program: system calls, task switches, "
                "starting threads and programs, wake-ups, timer lateness (with the CPU scheduler in use), page faults and small-file operations.",
                "Busy on one or two CPUs for about 6 seconds. Asks before it starts.", "_OS Latency...", "oslatency",
                "Open the OS Latency window. Nothing runs until you press Run and confirm."));
            grid_add3(fb, launcher("CPU Compute", "Floating-point number crunching: matrix multiply, peak multiply-add, FFT, N-body gravity, "
                "Mandelbrot and a small ray tracer, on one thread and on all threads, with the best vector code the CPU has. Every answer is checked.",
                "All threads 100 % busy for about 10 seconds (AVX-512 where the CPU has it). Asks before it starts; \"1 thread only\" keeps it light.",
                "CPU Co_mpute...", "compute", "Open the CPU Compute window. Nothing runs until you press Run and confirm."));
            grid_add3(fb, launcher("Everyday Work", "The work a PC does all day: compressing files (zlib, zstd, lz4, xz), sorting, checksums, "
                "searching and checking text, and running a small program, on one thread and on all threads. Every result is checked.",
                "All threads busy for about half a minute. Uses generated data, never your files. Asks before it starts.",
                "_Everyday Work...", "work", "Open the Everyday Work window. Nothing runs until you press Run and confirm."));
            grid_add3(fb, launcher("SRS Pi", "Works out pi to 128 thousand up to 32 million digits and checks every digit. "
                "A classic single-thread test; a wrong digit points to an unstable overclock.",
                "One thread by default. 1 million digits takes a few seconds.", "S_RS Pi...", "pi", "Open the SRS Pi window. Nothing runs until you press Run."));
            grid_add3(fb, launcher("Crypto", "How fast the CPU encrypts (AES-128) and hashes (SHA-256), with and without its built-in crypto instructions.",
                "Busy for about 8 seconds.", "C_rypto...", "crypto", "Open the Crypto benchmark window. Nothing runs until you press Run."));
            grid_add3(fb, launcher("Prime Sieve", "Counts the prime numbers up to 10 billion and checks the count. Simple integer and cache work that scales with threads.",
                "Busy for about a second on all threads.", "P_rime Sieve...", "prime", "Open the Prime Sieve window. Nothing runs until you press Run."));
            grid_add3(fb, launcher("CPU Burnin", "Burn and cool cycles with a Pass or Fail at the end: tests cooling and stability.",
                "Makes the chosen CPUs 100 % busy. Asks before it starts and stops at the temperature limit (90 °C by default).",
                "B_urnin...", "burn", "Open the CPU Burnin window. Nothing starts until you press Start and confirm."));
            grid_add3(fb, launcher("CPU Stress", "Keeps the chosen CPUs busy until you press Stop, while showing clocks and temperatures.",
                "Makes the chosen CPUs 100 % busy. Asks before it starts and stops at the temperature limit.",
                "_Stress...", "stress", "Open the CPU Stress window. Nothing starts until you press Start and confirm."));
        } else {
            grid_add3(fb, launcher("Device ID Lookup", "Turn a PCI or USB ID such as 1002:7550 into the maker and product name.",
                "Ctrl+L. Also: double-click a device line on any page.", "_Lookup...", "idlookup", "Open the Device ID Lookup window"));
            grid_add3(fb, launcher("Find in a system report", "Search every section of a full text report made by SRS Full System Report (srs-sysreport).",
                "Ctrl+Shift+F", "_Find...", "find", "Open the Find window"));
            grid_add3(fb, launcher("Save a report", "Write this PC's details to a text file, in SIV's report format.",
                "Options > Redact hides serial numbers, UUIDs, MACs and the computer name in saved reports.", "_Save...", "save",
                "Save SRS's default set of items (Ctrl+S). Menu > Save All Items saves every item."));
            grid_add3(fb, launcher("Collect root data", "Memory stick details, drive health and serial numbers need administrator rights.",
                "One password prompt. Read-only; the results are cached in ~/.cache/srs/root.", "Run as r_oot...", "root", "Run srs-root with pkexec"));
            grid_add3(fb, launcher("Classic view", "The SIV-style main window with its button rows, every item in its own window.",
                "Menu > Options > Start in classic view makes it the default.", "O_pen...", "classic", "Open the classic main window"));
        }
        gtk_box_pack_start(GTK_BOX(head), fb, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(vb), head, FALSE, FALSE, 0);

    nb = tabs_area(cat);
    page_fill(cat);
    gtk_box_pack_start(GTK_BOX(vb), nb, TRUE, TRUE, 0);
    return vb;
}

/* ---- search: item names / ids (jump), else text in the current tab (F3 = next) ---- */
static void view_find(swin *t, const char *needle, int dir)
{
    GtkTextIter s, e, a, b, cur;
    int n = 0;
    char msg[300];
    if (!t || !t->buf || !*needle) return;
    gtk_text_buffer_get_bounds(t->buf, &s, &e);
    gtk_text_buffer_remove_tag_by_name(t->buf, "hl", &s, &e);
    a = s;
    while (gtk_text_iter_forward_search(&a, needle, GTK_TEXT_SEARCH_CASE_INSENSITIVE, &a, &b, NULL)) {
        gtk_text_buffer_apply_tag_by_name(t->buf, "hl", &a, &b);
        a = b;
        n++;
    }
    if (!n) {
        snprintf(msg, sizeof msg, "No \"%s\" on this tab", needle);
        home_subtitle(msg);
        gtk_widget_error_bell(t->win);
        return;
    }
    gtk_text_buffer_get_iter_at_mark(t->buf, &cur, gtk_text_buffer_get_insert(t->buf));
    if (dir > 0) {
        if (!gtk_text_iter_forward_search(&cur, needle, GTK_TEXT_SEARCH_CASE_INSENSITIVE, &a, &b, NULL))
            gtk_text_iter_forward_search(&s, needle, GTK_TEXT_SEARCH_CASE_INSENSITIVE, &a, &b, NULL);
    } else {
        GtkTextIter sb, se;
        if (gtk_text_buffer_get_selection_bounds(t->buf, &sb, &se)) cur = sb;
        if (!gtk_text_iter_backward_search(&cur, needle, GTK_TEXT_SEARCH_CASE_INSENSITIVE, &a, &b, NULL))
            gtk_text_iter_backward_search(&e, needle, GTK_TEXT_SEARCH_CASE_INSENSITIVE, &a, &b, NULL);
    }
    gtk_text_buffer_select_range(t->buf, &b, &a);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(t->view), &a, 0.1, FALSE, 0, 0);
    snprintf(msg, sizeof msg, "%d match%s for \"%s\" on this tab (F3 next, Shift+F3 previous)", n, n == 1 ? "" : "es", needle);
    home_subtitle(msg);
}

static gboolean on_match(GtkEntryCompletion *c, GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
    char *id = NULL;
    (void)c; (void)d;
    gtk_tree_model_get(m, it, 1, &id, -1);
    if (id) {
        if (id[0] == '@') home_go(atoi(id + 1));
        else open_item(id);
        g_free(id);
    }
    gtk_entry_set_text(GTK_ENTRY(g_home->search), "");
    return TRUE;
}

static gboolean match_func(GtkEntryCompletion *c, const char *key, GtkTreeIter *it, gpointer d)
{
    char *hay = NULL, **w, **p, *low;
    gboolean ok = TRUE;
    (void)d;
    gtk_tree_model_get(gtk_entry_completion_get_model(c), it, 2, &hay, -1);
    low = g_utf8_strdown(gtk_entry_get_text(GTK_ENTRY(gtk_entry_completion_get_entry(c))), -1);
    (void)key;
    w = g_strsplit(low, " ", -1);
    for (p = w; *p; p++) if (**p && (!hay || !strstr(hay, *p))) { ok = FALSE; break; }
    g_strfreev(w);
    g_free(low);
    g_free(hay);
    return ok;
}

static void on_search_activate(GtkEntry *e, gpointer d)
{
    const char *t = gtk_entry_get_text(e);
    int c;
    (void)d;
    if (!*t) return;
    if (srs_section_find(t) && !strchr(t, ' ')) { open_item(t); gtk_entry_set_text(e, ""); return; }
    for (c = 0; c < CAT_N; c++) if (!g_ascii_strcasecmp(t, g_cats[c].title)) { home_go(c); gtk_entry_set_text(e, ""); return; }
    if (home_cur_tab()) view_find(home_cur_tab(), t, +1);
    else home_subtitle("Open a page on the left to search its text, or pick an item from the list while you type");
}

static void on_search_stop(GtkSearchEntry *e, gpointer d)
{
    swin *t = home_cur_tab();
    (void)d;
    gtk_entry_set_text(GTK_ENTRY(e), "");
    if (t && t->buf) {
        GtkTextIter s, x;
        gtk_text_buffer_get_bounds(t->buf, &s, &x);
        gtk_text_buffer_remove_tag_by_name(t->buf, "hl", &s, &x);
        gtk_widget_grab_focus(t->view);
    }
    home_subtitle(NULL);
}

static GtkWidget *build_search(void)
{
    GtkWidget *e = gtk_search_entry_new();
    GtkEntryCompletion *comp = gtk_entry_completion_new();
    GtkListStore *ls = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    GtkTreeIter it;
    int i, n = srs_section_count();
    for (i = 0; i < CAT_N; i++) {
        char id[16], *mk = g_markup_printf_escaped("<b>%s</b>  <small>page</small>", g_cats[i].title), *hay = g_utf8_strdown(g_cats[i].title, -1);
        snprintf(id, sizeof id, "@%d", i);
        gtk_list_store_insert_with_values(ls, &it, -1, 0, mk, 1, id, 2, hay, -1);
        g_free(mk);
        g_free(hay);
    }
    for (i = 0; i < n; i++) {
        const srs_item *s = srs_section_at(i);
        char *mk, *h1, *hay;
        if (!s || !s->id) continue;
        mk = g_markup_printf_escaped("%s  <small>[%s]  %s%s</small>", s->title, s->id, g_cats[cat_of_id(s->id)].title,
                                     s->bucket == B_ABSENT ? "  (not present)" : s->bucket == B_WINONLY ? "  (Windows-only)" : "");
        h1 = g_strdup_printf("%s %s %s", s->title, s->id, g_cats[cat_of_id(s->id)].title);
        hay = g_utf8_strdown(h1, -1);
        gtk_list_store_insert_with_values(ls, &it, -1, 0, mk, 1, s->id, 2, hay, -1);
        g_free(mk);
        g_free(h1);
        g_free(hay);
    }
    gtk_entry_completion_set_model(comp, GTK_TREE_MODEL(ls));
    g_object_unref(ls);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(comp), r, TRUE);
    gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(comp), r, "markup", 0);
    gtk_entry_completion_set_match_func(comp, match_func, NULL, NULL);
    gtk_entry_completion_set_minimum_key_length(comp, 2);
    gtk_entry_completion_set_popup_single_match(comp, TRUE);
    g_signal_connect(comp, "match-selected", G_CALLBACK(on_match), NULL);
    gtk_entry_set_completion(GTK_ENTRY(e), comp);
    g_object_unref(comp);
    gtk_entry_set_width_chars(GTK_ENTRY(e), 34);
    gtk_entry_set_placeholder_text(GTK_ENTRY(e), "Search items, or text on this page");
    gtk_widget_set_tooltip_text(e, "Type a name like \"temperature\", \"SMART\" or \"BIOS\" and pick an item from the list (Ctrl+F). "
                                   "Press Enter to find the text on the current tab instead; F3 finds the next one. "
                                   "Item ids work too, with an argument: processor:3, volumes:/");
    g_signal_connect(e, "activate", G_CALLBACK(on_search_activate), NULL);
    g_signal_connect(e, "stop-search", G_CALLBACK(on_search_stop), NULL);
    return e;
}

/* ---- header menu ---- */
static void on_hmenu(GtkMenuItem *mi, gpointer d)
{
    const char *t = d;
    swin *tab = home_cur_tab();
    (void)mi;
    if (!strcmp(t, "copy")) { if (tab) copy_all(tab); }
    else if (!strcmp(t, "quit")) { if (g_main) gtk_widget_destroy(g_main->win); }
    else if (!strcmp(t, "about")) open_item("about");
    else if (!strcmp(t, "guide")) open_guide(tab);
    else on_launch(NULL, (gpointer)t);
}

static GtkWidget *hmenu_add(GtkWidget *m, const char *label, const char *act, guint key, GdkModifierType mods)
{
    GtkWidget *it = gtk_menu_item_new_with_mnemonic(label);
    if (key) gtk_accel_label_set_accel(GTK_ACCEL_LABEL(gtk_bin_get_child(GTK_BIN(it))), key, mods);
    if (act) g_signal_connect(it, "activate", G_CALLBACK(on_hmenu), (gpointer)act);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), it);
    return it;
}

static void on_menu_btn(GtkButton *b, gpointer d)
{
    GtkWidget *m = gtk_menu_new(), *sub, *it;
    (void)d;
    hmenu_add(m, "Save _Report...", "save", GDK_KEY_s, GDK_CONTROL_MASK);
    hmenu_add(m, "Save _All Items...", "saveall", 0, 0);
    gtk_widget_set_sensitive(hmenu_add(m, "_Copy This Tab", "copy", 0, 0), home_cur_tab() != NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    sub = gtk_menu_new();
    lineup_menu(sub);
    it = hmenu_add(m, "_Lineup (tabs on each page)", NULL, 0, 0);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(it), sub);
    sub = gtk_menu_new();
    build_options(sub);
    it = hmenu_add(m, "_Options", NULL, 0, 0);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(it), sub);
    hmenu_add(m, "Run as r_oot (srs-root)...", "root", 0, 0);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    hmenu_add(m, "_Find in System Report...", "find", GDK_KEY_f, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
    hmenu_add(m, "Device _ID Lookup...", "idlookup", GDK_KEY_l, GDK_CONTROL_MASK);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    hmenu_add(m, "Classic _View (SIV-style windows)", "classic", 0, 0);
    sub = gtk_menu_new();
    build_menu(sub);
    it = hmenu_add(m, "Classic _Menu (every item and tool)", NULL, 0, 0);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(it), sub);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    hmenu_add(m, "User _Guide", "guide", GDK_KEY_F1, 0);
    hmenu_add(m, "_About SRS", "about", 0, 0);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    hmenu_add(m, "_Quit", "quit", GDK_KEY_q, GDK_CONTROL_MASK);
    gtk_widget_show_all(m);
    gtk_menu_popup_at_widget(GTK_MENU(m), GTK_WIDGET(b), GDK_GRAVITY_SOUTH_EAST, GDK_GRAVITY_NORTH_EAST, NULL);
}

/* ---- privilege badge: what SRS can read right now ---- */
/* euid 0 in the first user namespace (a user namespace's mapped "root" cannot read root data) */
static int real_root(void)
{
    char b[128] = "";
    unsigned a = 1, c = 1;
    if (geteuid() != 0) return 0;
    if (!rd_str("/proc/self/uid_map", b, sizeof b)) return 1;
    return sscanf(b, "%u %u", &a, &c) == 2 && a == 0 && c == 0;
}

static void badge_update(void)
{
    const char *f;
    char tip[900];
    int root, full;
    GStatBuf st;
    if (!g_home || !g_home->badge) return;
    root = real_root();
    f = so_root_file("collected.txt");
    full = root || f;
    gtk_image_set_from_icon_name(GTK_IMAGE(g_home->badge_icon), icon_pick(full ? "security-high-symbolic|channel-secure-symbolic|emblem-ok-symbolic"
                                                                                  : "security-low-symbolic|dialog-password-symbolic"), GTK_ICON_SIZE_BUTTON);
    gtk_label_set_text(GTK_LABEL(g_home->badge_lbl), full ? "Full access" : "Limited (user)");
    set_level_class(g_home->badge_icon, full ? LV_OK : LV_UNK);
    if (root)
        snprintf(tip, sizeof tip, "SRS is running as root: everything it shows is read directly, including drive health (SMART), "
                                  "memory stick details (SPD), serial numbers and BIOS tables.");
    else if (f && !g_stat(f, &st)) {
        char when[64];
        time_t now = time(NULL), m = st.st_mtime;
        struct tm a, b;
        localtime_r(&m, &a);
        localtime_r(&now, &b);
        strftime(when, sizeof when, a.tm_yday == b.tm_yday && a.tm_year == b.tm_year ? "at %H:%M" : "on %Y-%m-%d at %H:%M", &a);
        snprintf(tip, sizeof tip, "Root data collected %s (srs-root, read-only). The items that need root - drive health (SMART), "
                                  "memory stick details (SPD), serial numbers, BIOS and ACPI tables - show it.\n"
                                  "Menu > Run as root collects it again; Options > Refresh root data at boot does it at every start.", when);
    } else
        snprintf(tip, sizeof tip, "SRS runs as your user. Some data can only be read as root: drive health (SMART), memory stick "
                                  "details (SPD), serial numbers and BIOS tables (DMI), ACPI tables, CPU registers (MSRs) and the "
                                  "kernel log.\nRun as root collects them once with one password prompt, read-only (srs-root); "
                                  "Options > Refresh root data at boot does it at every start.");
    gtk_widget_set_tooltip_text(g_home->badge, tip);
    gtk_widget_set_visible(g_home->badge_root, !full);
}

/* ---- refresh, keys, lifetime ---- */
static void home_refresh_data(void)
{
    swin *t;
    if (!g_home) return;
    badge_update();
    cap_flush();
    hs_update();
    if (g_home->cur == CAT_OVERVIEW) ov_update(); else facts_update(g_home->cur);
    if ((t = home_cur_tab())) render(t);
}

static void on_refresh_btn(GtkButton *b, gpointer d) { (void)b; (void)d; home_refresh_data(); }

static gboolean home_tick(gpointer d)
{
    (void)d;
    if (!g_home || g_update_ms < 0 || !gtk_widget_get_visible(g_home->w->win)) return G_SOURCE_CONTINUE;
    hs_update();
    if (g_home->cur == CAT_OVERVIEW) ov_update(); else facts_update(g_home->cur);
    return G_SOURCE_CONTINUE;
}

static int home_key(swin *w, GdkEventKey *ev)
{
    int ctrl = (ev->state & GDK_CONTROL_MASK) != 0, shift = (ev->state & GDK_SHIFT_MASK) != 0, alt = (ev->state & GDK_MOD1_MASK) != 0;
    swin *t = home_cur_tab();
    guint k = gdk_keyval_to_lower(ev->keyval);
    if (alt && !ctrl && k >= GDK_KEY_0 && k <= GDK_KEY_9) { home_go(k == GDK_KEY_0 ? 9 : (int)(k - GDK_KEY_1)); return TRUE; }
    if (alt && !ctrl && (k == GDK_KEY_minus || k == GDK_KEY_equal)) { home_go(k == GDK_KEY_minus ? 10 : 11); return TRUE; }
    if (alt && !ctrl && k == GDK_KEY_BackSpace) { home_go(12); return TRUE; }   /* the 13th page (Tools): the key after = */
    switch (ev->keyval) {
    case GDK_KEY_F1: if (shift) open_item("help"); else open_guide(t ? t : NULL); return TRUE;
    case GDK_KEY_F5: home_refresh_data(); return TRUE;
    case GDK_KEY_F3:
        if (t && *gtk_entry_get_text(GTK_ENTRY(g_home->search))) view_find(t, gtk_entry_get_text(GTK_ENTRY(g_home->search)), shift ? -1 : 1);
        return TRUE;
    default: break;
    }
    if (ctrl) {
        switch (k) {
        case GDK_KEY_f:
            if (shift) open_tool(W_FIND, 0);
            else { gtk_widget_grab_focus(g_home->search); gtk_editable_select_region(GTK_EDITABLE(g_home->search), 0, -1); }
            return TRUE;
        case GDK_KEY_l: open_tool(W_IDLOOKUP, 0); return TRUE;
        case GDK_KEY_s: save_dialog(0); return TRUE;
        case GDK_KEY_q: gtk_widget_destroy(w->win); return TRUE;
        case GDK_KEY_m: on_menu_btn(GTK_BUTTON(g_home->menubtn), NULL); return TRUE;
        case GDK_KEY_d: open_item("all-dev"); return TRUE;
        case GDK_KEY_h: toggle_highlight(); return TRUE;
        case GDK_KEY_w: if (t) gtk_widget_destroy(t->page); return TRUE;
        case GDK_KEY_t: if (g_home->cur != CAT_OVERVIEW && g_home->ctl[g_home->cur]) picker_open(g_home->ctl[g_home->cur], g_home->cur, PK_ADD, NULL); return TRUE;
        case GDK_KEY_Page_Up: case GDK_KEY_Page_Down:   /* every tab-bar layout, wherever the focus is */
            if (g_home->cur != CAT_OVERVIEW && g_home->nb[g_home->cur]) {
                GtkNotebook *nb = GTK_NOTEBOOK(g_home->nb[g_home->cur]);
                int n = gtk_notebook_get_n_pages(nb), c = gtk_notebook_get_current_page(nb);
                if (n) gtk_notebook_set_current_page(nb, (c + (k == GDK_KEY_Page_Up ? n - 1 : 1)) % n);
            }
            return TRUE;
        default: break;
        }
    }
    return FALSE;
}

static void home_destroyed(swin *w)
{
    int i;
    (void)w;
    if (!g_home) return;
    if (g_home->tick) g_source_remove(g_home->tick);
    for (i = 0; i < 24; i++) { cap_free(g_cc[i].c); g_cc[i].c = NULL; g_cc[i].id[0] = 0; }
    g_free(g_home);
    g_home = NULL;
    g_home_open = 0;
    load_css();
}

static void home_open(int as_main)
{
    swin *w;
    GtkWidget *hb, *paned, *side, *sw, *lb, *b, *box;
    int c;
    if (g_home) { gtk_window_present(GTK_WINDOW(g_home->w->win)); return; }
    g_home = g_new0(home_t, 1);
    g_home_open = 1;
    load_css();
    w = g_new0(swin, 1);
    w->kind = W_HOME;
    w->is_main = as_main;
    snprintf(w->id, sizeof w->id, "overview");
    g_home->w = w;
    if (as_main) g_main = w;
    make_window(w, "SRS System Information", 1240, 800);
    gtk_style_context_remove_class(gtk_widget_get_style_context(w->win), "siv");
    gtk_style_context_add_class(gtk_widget_get_style_context(w->win), "srs-main");
    gtk_window_set_transient_for(GTK_WINDOW(w->win), NULL);
    hs_update();

    hb = gtk_header_bar_new();
    g_home->hb = hb;
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(hb), "SRS System Information");
    b = gtk_button_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(b, "Menu: save a report, options, run as root, classic view, guide (Ctrl+M)");
    g_signal_connect(b, "clicked", G_CALLBACK(on_menu_btn), NULL);
    g_home->menubtn = b;
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), b);
    g_home->search = build_search();
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), g_home->search);
    b = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(b, "Read everything on this page again (F5)");
    g_signal_connect(b, "clicked", G_CALLBACK(on_refresh_btn), NULL);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), b);
    {   /* privilege badge: "Full access" with root data, else "Limited (user)" and a Run as root button */
        GtkWidget *bx = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        g_home->badge = gtk_event_box_new();
        g_home->badge_icon = gtk_image_new();
        g_home->badge_lbl = label_cls("", "srs-cap", 0);
        gtk_box_pack_start(GTK_BOX(bx), g_home->badge_icon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bx), g_home->badge_lbl, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(g_home->badge), bx);
        gtk_widget_set_margin_start(g_home->badge, 8);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), g_home->badge);
        g_home->badge_root = gtk_button_new_with_label("Run as root...");
        gtk_widget_set_tooltip_text(g_home->badge_root, "Collect the root-only data once with one password prompt (srs-root, read-only)");
        gtk_widget_set_no_show_all(g_home->badge_root, TRUE);
        gtk_widget_set_valign(g_home->badge_root, GTK_ALIGN_CENTER);
        g_signal_connect(g_home->badge_root, "clicked", G_CALLBACK(on_launch), (gpointer)"root");
        gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), g_home->badge_root);
    }
    gtk_window_set_titlebar(GTK_WINDOW(w->win), hb);

    paned = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(side), "srs-side");
    gtk_style_context_add_class(gtk_widget_get_style_context(side), "sidebar");
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    lb = gtk_list_box_new();
    g_home->side = lb;
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(lb), GTK_SELECTION_BROWSE);
    for (c = 0; c < CAT_N; c++) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        char tip[300];
        gtk_box_pack_start(GTK_BOX(row), gtk_image_new_from_icon_name(icon_pick(g_cats[c].icons), GTK_ICON_SIZE_MENU), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), label_cls(g_cats[c].title, NULL, 0), FALSE, FALSE, 0);
        static const char *const keyname[CAT_N] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "Backspace" };
        if (keyname[c]) snprintf(tip, sizeof tip, "%s\n(Alt+%s)", g_cats[c].blurb, keyname[c]);
        else snprintf(tip, sizeof tip, "%s", g_cats[c].blurb);
        gtk_widget_set_tooltip_text(row, tip);
        gtk_list_box_insert(GTK_LIST_BOX(lb), row, -1);
    }
    g_signal_connect(lb, "row-selected", G_CALLBACK(on_side_row), NULL);
    gtk_container_add(GTK_CONTAINER(sw), lb);
    gtk_box_pack_start(GTK_BOX(side), sw, TRUE, TRUE, 0);
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    b = gtk_button_new_with_mnemonic("Classic _view");
    gtk_widget_set_tooltip_text(b, "The SIV-style main window and panels, with every button row (Menu > Classic view)");
    g_signal_connect(b, "clicked", G_CALLBACK(on_launch), (gpointer)"classic");
    gtk_box_pack_start(GTK_BOX(box), b, FALSE, FALSE, 0);
    g_home->lineup_lbl = label_cls("", "dim-label srs-cap", 0.5);
    gtk_label_set_ellipsize(GTK_LABEL(g_home->lineup_lbl), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(box), g_home->lineup_lbl, FALSE, FALSE, 2);
    {
        char v[64];
        snprintf(v, sizeof v, "SRS %s", SRS_VERSION);
        gtk_box_pack_start(GTK_BOX(box), label_cls(v, "dim-label srs-cap", 0.5), FALSE, FALSE, 2);
    }
    gtk_box_pack_end(GTK_BOX(side), box, FALSE, FALSE, 0);
    gtk_widget_set_size_request(side, 230, -1);
    gtk_box_pack_start(GTK_BOX(paned), side, FALSE, FALSE, 0);

    tabs_state_load();
    empty_load();
    g_home->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(g_home->stack), GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_stack_add_named(GTK_STACK(g_home->stack), build_overview(), g_cats[CAT_OVERVIEW].name);
    for (c = 1; c < CAT_N; c++)
        gtk_stack_add_named(GTK_STACK(g_home->stack), c == CAT_POWER ? power_page_new(open_item) : build_cat_page(c), g_cats[c].name);
    gtk_box_pack_start(GTK_BOX(paned), g_home->stack, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(w->win), paned);
    if (as_main) {
        g_signal_connect(w->win, "delete-event", G_CALLBACK(on_main_delete), NULL);
        g_signal_connect(w->win, "window-state-event", G_CALLBACK(on_main_state), NULL);
    }
    gtk_widget_show_all(w->win);
    layout_apply();
    lineup_label_update();
    if (g_opt_autoscan) empty_scan(0);   /* opt-in: in a child process, at most once a day, which items have nothing to show */
    home_go(CAT_OVERVIEW);
    badge_update();
    ov_update();
    if (as_main) g_idle_add(lineup_first_ask, NULL);
    gtk_widget_grab_focus(lb);
    g_home->tick = g_timeout_add(3000, home_tick, NULL);
}

int main(int argc, char **argv)
{
    const char *autop;
    char *base = g_path_get_basename(argv[0]);
    snprintf(g_image, sizeof g_image, "%s", base);
    g_free(base);
    if (argc > 1 && !strcmp(argv[1], "--power-recorder")) return power_recorder_main(argc, argv);   /* Power & cost, no window */
    if (argc > 1 && strncmp(argv[1], "--g", 3)) return srs_cli_main(argc, argv);   /* CLI mode, like SIV64X -save */
    if (argc > 1 && !strcmp(argv[1], "--gui-scan-empty")) return scan_empty_main();   /* child of the main window */
    gethostname(g_host, sizeof g_host);
    /* GTK_THEME (e.g. adw-gtk3-dark) would override the in-app theme choice: remember it for
       "Follow desktop", then let the Theme option drive Adwaita light / dark for this process */
    if (getenv("GTK_THEME")) snprintf(g_env_theme, sizeof g_env_theme, "%s", getenv("GTK_THEME"));
    g_unsetenv("GTK_THEME");
    gtk_init(&argc, &argv);
    settings_load();
    {   /* which main window: the new one, or the classic SIV-style one (--gui-classic / Options) */
        int i;
        g_classic_session = g_opt_classic;
        for (i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--gui-classic")) g_classic_session = 1;
            else if (!strcmp(argv[i], "--gui-new")) g_classic_session = 0;
        }
    }
    if (!g_theme_set) g_theme = g_classic_session ? TH_LIGHT : TH_SYSTEM;   /* no Theme= yet: classic stays light, new follows the desktop */
    theme_apply();
    {
        char *icon = g_build_filename(g_get_user_data_dir(), "icons", "hicolor", "256x256", "apps", "srs.png", NULL);
        if (g_file_test(icon, G_FILE_TEST_EXISTS)) gtk_window_set_default_icon_from_file(icon, NULL);
        g_free(icon);
    }
    g_wins = g_ptr_array_new();
    {
        int i, start_hidden = g_opt_start_tray;
        for (i = 1; i < argc; i++) if (!strcmp(argv[i], "--gui-tray")) { start_hidden = 1; g_opt_tray = 1; }   /* autostart */
        if (g_classic_session) open_main(1); else home_open(1);
        tray_apply();
        if (start_hidden) srs_hide_all();   /* only hides when a tray icon really exists */
    }
    autop = getenv("SRS_OPEN");   /* comma-separated items or tool names to open at start */
    {
        int i;
        for (i = 1; i < argc; i++) if (!strncmp(argv[i], "--gui-open=", 11)) autop = argv[i] + 11;   /* menu entries / jump list */
    }
    if (autop) {
        char tmp[2048], *t, *save;
        snprintf(tmp, sizeof tmp, "%s", autop);
        for (t = strtok_r(tmp, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
            if (!strcmp(t, "burnin")) open_tool(W_BURN, 0);
            else if (!strcmp(t, "stress")) open_tool(W_BURN, 1);
            else if (!strcmp(t, "bench")) open_tool(W_BENCH, BK_BENCH);
            else if (!strcmp(t, "ipi")) open_tool(W_BENCH, BK_IPI);
            else if (!strcmp(t, "latency")) open_tool(W_BENCH, BK_LAT);
            else if (!strcmp(t, "pi")) open_tool(W_BENCH, BK_PI);
            else if (!strcmp(t, "crypto")) open_tool(W_BENCH, BK_CRYPTO);
            else if (!strcmp(t, "prime")) open_tool(W_BENCH, BK_PRIME);
            else if (!strcmp(t, "cachemem")) open_tool(W_BENCH, BK_CACHEMEM);
            else if (!strcmp(t, "storagebench")) open_tool(W_BENCH, BK_STORAGE);
            else if (bench_tool_sub(t) >= 0) open_tool(W_BENCH, bench_tool_sub(t));
            else if (!strcmp(t, "idlookup")) open_tool(W_IDLOOKUP, 0);
            else if (!strcmp(t, "find")) open_tool(W_FIND, 0);
            else if (!strcmp(t, "menu")) popup_menu(g_main->win);
            else if (!strcmp(t, "power-cost") && g_home) home_go(CAT_POWER);
            else open_item(t);
        }
    }
    gtk_main();
    return 0;
}
