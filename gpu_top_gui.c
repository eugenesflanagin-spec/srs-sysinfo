/* gpu_top_gui.c - SRS "The Big Top" window (internally GPU Top): a live view of the PC and of each graphics card.
 *
 * The AMD layout follows amdgpu_top's GUI (crates/amdgpu_top_gui, "Single" mode) by Umio Yasuno
 * (MIT licence, Copyright (c) 2023 Umio Yasuno, https://github.com/Umio-Yasuno/amdgpu_top): the same
 * panels, data, order and grouping, drawn with GTK 3 and Cairo. Left: Device Info, XDNA NPU Info,
 * Hardware IP Info, IP Discovery Table, Video Caps Info, VBIOS Info, Connector Info. Right: GRBM,
 * GRBM2, VRAM, Activity, Processes (fdinfo), Sensors, PCIe Bandwidth, ECC Memory Error Count,
 * GPU Metrics, Throttling Log. Extra tabs: SMI (one line per GPU, like `amdgpu_top --smi`), DRM Info,
 * PowerPlay Table and NPU (XDNA). NVIDIA cards (NVML) show what NVML reports; panels it has no
 * data for say so.
 *
 * All data comes from the data layer (gpu_top.h); this file reads no files or devices itself.
 * Each card is sampled by its own worker thread (gt_update blocks for the interval); the window
 * copies the newest sample twice a second, keeps 30 s of history and redraws. The threads and the
 * timer run only while the window is shown.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpu_top.h"
#include "gpu_top_gui.h"
#include "perf_view.h"
#include "perf_tray.h"
#include "sec.h"

#define INTERVAL_MS 1000      /* default: amdgpu_top Sampling::low, 100 samples x 10 ms ([GPU Top] Update ms changes it) */
#define REDRAW_MS 500         /* amdgpu_top repaints every 500 ms (faster when the cards are read faster) */
#define HN 128                /* history points: 30 s (amdgpu_top HISTORY_LENGTH) at the fastest update, 0.25 s */
#define GUIDE_PREFIX "user-guide--"   /* the guide build prefixes ids in docs/USER-GUIDE.md with the file name */
#define HSPAN 30.0
#define TXT_LEN (256 * 1024)
#define NTHRLOG 30

/* ---- history ---- */
typedef struct { int n, head; double t[HN], v[HN]; } ring;

typedef struct {
    ring grbm[GT_MAX_PC], grbm2[GT_MAX_PC];
    ring vram, gtt, gfx, umc, media;
    ring fd_gfx, fd_compute, fd_dma, fd_dec, fd_enc, fd_vcnu, fd_vpe;
    ring sclk, mclk, fclk, vddgfx, pavg, pin, fan, edge, junction, memt, tctl, tx, rx;
    ring avclk, adclk, avclk1, adclk1, cvclk, cdclk, cvclk1, cdclk1;
    double tfirst, tlast;
    int nthr, thrhead;
    char thrlog[NTHRLOG][200];
    char lastthr[200];
} hist;

static void ring_push(ring *r, double t, double v)
{
    r->t[r->head] = t;
    r->v[r->head] = v;
    r->head = (r->head + 1) % HN;
    if (r->n < HN) r->n++;
}

/* the i-th oldest point */
static int ring_at(const ring *r, int i, double *t, double *v)
{
    int k;
    if (i >= r->n) return 0;
    k = (r->head - r->n + i + HN) % HN;
    *t = r->t[k];
    *v = r->v[k];
    return 1;
}

static double nv(long long v) { return v == GT_NA ? NAN : (double)v; }

/* ---- one worker per card ---- */
typedef struct {
    gint ref;
    GMutex mu;
    int index, no_pc, interval_ms;
    gint stop;
    int state;                 /* 0 opening, 1 open, -1 failed */
    char err[200];
    gt_info *info;             /* set once with state 1, then read-only */
    char *drm, *pp;
    int drm_ok, pp_ok;
    gt_stat *stat;
    unsigned long long seq;
} slot;

static void slot_unref(slot *s)
{
    if (!g_atomic_int_dec_and_test(&s->ref)) return;
    g_mutex_clear(&s->mu);
    g_free(s->info);
    g_free(s->drm);
    g_free(s->pp);
    g_free(s->stat);
    g_free(s);
}

/* the slot's state, read under its lock. The worker writes info, drm, pp and err before it sets state (under the same
   lock) and never again, so once 1 or -1 has been seen here those fields are safe to read without the lock. */
static int slot_state(slot *s)
{
    int st;
    if (!s) return 0;
    g_mutex_lock(&s->mu);
    st = s->state;
    g_mutex_unlock(&s->mu);
    return st;
}

static gpointer worker(gpointer p)
{
    slot *s = p;
    gt_opts o;
    gt_dev *d;
    char err[200] = "";
    gt_stat *tmp = g_new0(gt_stat, 1);
    memset(&o, 0, sizeof o);
    o.no_pc = s->no_pc;
    o.pc_samples = s->interval_ms / 10 > 10 ? s->interval_ms / 10 : 10;   /* GRBM: one read per 10 ms, as amdgpu_top's 100 x 10 ms */
    d = gt_open(s->index, &o, err, sizeof err);
    if (!d) {
        g_mutex_lock(&s->mu);
        s->state = -1;
        snprintf(s->err, sizeof s->err, "%s", *err ? err : "The card could not be opened");
        g_mutex_unlock(&s->mu);
    } else {
        gt_info *in = g_new(gt_info, 1);
        char *drm = g_malloc0(TXT_LEN), *pp = g_malloc0(TXT_LEN);
        int dok, pok;
        *in = *gt_info_of(d);
        dok = gt_drm_info_text(d, drm, TXT_LEN) == 0;
        pok = gt_pp_table_text(d, pp, TXT_LEN) == 0;
        g_mutex_lock(&s->mu);
        s->info = in;
        s->drm = drm;
        s->pp = pp;
        s->drm_ok = dok;
        s->pp_ok = pok;
        s->state = 1;
        g_mutex_unlock(&s->mu);
        while (!g_atomic_int_get(&s->stop)) {
            if (gt_update(d, s->interval_ms, tmp) == 0) {
                g_mutex_lock(&s->mu);
                if (!s->stat) s->stat = g_new0(gt_stat, 1);
                *s->stat = *tmp;
                s->seq++;
                g_mutex_unlock(&s->mu);
            } else g_usleep((gulong)s->interval_ms * 1000);
        }
        gt_close(d);
    }
    g_free(tmp);
    slot_unref(s);
    return NULL;
}

/* ---- graphs ---- */
typedef struct { size_t off; const char *name; int color; } series;
typedef struct {
    GtkWidget *da;
    int ns;
    series s[8];
    double ymin, ymax;         /* the range always shown */
    int autoy;                 /* grow above ymax with the data */
    int fill, legend, axes;
    const char *unit;
} plot;

static const double pal[8][3] = {
    { 0.21, 0.52, 0.89 }, { 0.90, 0.38, 0.00 }, { 0.18, 0.69, 0.42 }, { 0.57, 0.25, 0.67 },
    { 0.88, 0.11, 0.14 }, { 0.10, 0.65, 0.70 }, { 0.80, 0.62, 0.05 }, { 0.55, 0.42, 0.30 } };

/* ---- the window ---- */
enum { P_GPU, P_NAME, P_PID, P_KFD, P_VRAM, P_GTT, P_CPU, P_GFX, P_COMP, P_DMA, P_VCNU, P_DEC, P_ENC, P_VPE, P_N };
enum { S_IDX, S_NAME, S_TARGET, S_PCI, S_VRAM, S_GTT, S_SCLK, S_MCLK, S_VDD, S_POWER, S_GFX, S_UMC, S_MEDIA, S_TEMP, S_FAN, S_THR, S_N };

#define NSENS 7
#define NTEMP 4
#define NPLOTS (2 * GT_MAX_PC + NSENS + NTEMP + 8)

typedef struct {
    GtkWidget *win, *devlbl, *combo, *updlbl, *upd, *setbtn, *guide, *pause, *infobtn, *pcbtn, *status, *nb, *paned, *left, *right, *panels, *msg;
    GtkWidget *x_grbm, *x_grbm2, *grbm_box, *grbm2_box, *grbm_na, *grbm2_na;
    GtkWidget *grbm_lbl[GT_MAX_PC], *grbm2_lbl[GT_MAX_PC];
    GtkWidget *vram_bar[3], *vram_lbl[3];
    GtkWidget *act_lbl;
    GtkWidget *fd_view, *fd_sc, *fd_full;
    GtkTreeViewColumn *fd_cols[P_N];
    GtkListStore *fd_store;
    GtkWidget *sens_cell[NSENS + NTEMP], *sens_lbl[NSENS + NTEMP], *sens_flow, *link_lbl, *pstate_lbl, *profile_lbl, *thr_lbl, *dpm_lbl;
    GtkWidget *x_sens, *sens_na;
    GtkWidget *x_pcie, *pcie_lbl, *x_ecc, *ecc_lbl;
    GtkWidget *x_metrics, *metrics_grid, *metrics_agrid, *metrics_na, *metrics_thr, *x_vclk;
    GtkWidget *m_val[GT_M_COUNT], *m_name[GT_M_COUNT], *m_unit[GT_M_COUNT], *ma_val[GT_MA_COUNT], *ma_name[GT_MA_COUNT];
    GtkWidget *thrlog_lbl;
    GtkWidget *smi_view, *smi_proc_view;
    GtkListStore *smi_store, *smi_proc_store;
    GtkWidget *drm_view, *pp_view, *xdna_view;
    GtkWidget *page[GPU_TOP_PAGE_NPU + 1];   /* the tabs by GPU_TOP_PAGE_*; NULL = not there */
    plot plots[NPLOTS];
    int nplots;
    int sens_plot[NSENS + NTEMP], vram_plot;
    guint timer;
    int woken[GT_MAX_DEV];   /* 1 = the user picked this runtime-suspended card, so it may be opened (and woken) */
    int ndev, sel, no_pc, interval_ms, shown_info, building, named[GT_MAX_DEV], dirty;
    gt_device dev[GT_MAX_DEV];
    slot *sl[GT_MAX_DEV];
    gt_stat *cur[GT_MAX_DEV];
    unsigned long long seen[GT_MAX_DEV], pushed[GT_MAX_DEV];
    int have[GT_MAX_DEV];
    hist *h[GT_MAX_DEV];
    gint64 t0;
    char *xdna;
    int xdna_ok;
} gtw;

static gtw *G;

#define HOFF(f) offsetof(hist, f)

static const gt_info *cur_info(void)
{
    slot *s = G->sl[G->sel];
    return slot_state(s) == 1 ? s->info : NULL;
}

static int is_nv(void) { return G->dev[G->sel].vendor == GT_VENDOR_NVIDIA; }

/* ---- small widget helpers ---- */
static GtkWidget *lbl(const char *t, float xalign)
{
    GtkWidget *l = gtk_label_new(t);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    return l;
}

static GtkWidget *lbl_cls(const char *t, const char *cls)
{
    GtkWidget *l = lbl(t, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), cls);
    return l;
}

static void set_text(GtkWidget *l, const char *t)
{
    if (strcmp(gtk_label_get_text(GTK_LABEL(l)), t)) gtk_label_set_text(GTK_LABEL(l), t);
}

/* a collapsible panel, like amdgpu_top's collapsing headers */
static GtkWidget *panel(GtkWidget *parent, const char *title, int open, GtkWidget **body)
{
    GtkWidget *x = gtk_expander_new(NULL), *t = lbl_cls(title, "gt-h"), *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_expander_set_label_widget(GTK_EXPANDER(x), t);
    gtk_expander_set_expanded(GTK_EXPANDER(x), open);
    gtk_widget_set_margin_start(b, 18);
    gtk_widget_set_margin_top(b, 4);
    gtk_widget_set_margin_bottom(b, 6);
    gtk_container_add(GTK_CONTAINER(x), b);
    gtk_style_context_add_class(gtk_widget_get_style_context(x), "gt-panel");
    gtk_box_pack_start(GTK_BOX(parent), x, FALSE, FALSE, 0);
    *body = b;
    return x;
}

static gboolean plot_draw(GtkWidget *w, cairo_t *cr, gpointer d);

static plot *new_plot(int width, int height, int fill, int legend, int axes, double ymin, double ymax, int autoy, const char *unit)
{
    plot *p;
    g_assert(G->nplots < NPLOTS);
    p = &G->plots[G->nplots++];
    memset(p, 0, sizeof *p);
    p->da = gtk_drawing_area_new();
    gtk_widget_set_size_request(p->da, width, height);
    p->fill = fill;
    p->legend = legend;
    p->axes = axes;
    p->ymin = ymin;
    p->ymax = ymax;
    p->autoy = autoy;
    p->unit = unit;
    g_signal_connect(p->da, "draw", G_CALLBACK(plot_draw), p);
    return p;
}

static void plot_add(plot *p, size_t off, const char *name)
{
    p->s[p->ns].off = off;
    p->s[p->ns].name = name;
    p->s[p->ns].color = p->ns;
    p->ns++;
}

static gboolean plot_draw(GtkWidget *w, cairo_t *cr, gpointer d)
{
    plot *p = d;
    hist *h = G ? G->h[G->sel] : NULL;
    GdkRGBA fg;
    int W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w), i, k;
    double l = 1, r = W - 1, t = 1, b = H - 1, ymin = p->ymin, ymax = p->ymax, t1, x0, span;
    int has[8] = { 0 };
    gtk_style_context_get_color(gtk_widget_get_style_context(w), gtk_widget_get_state_flags(w), &fg);
    if (h)
        for (k = 0; k < p->ns; k++) {
            const ring *rg = (const ring *)((const char *)h + p->s[k].off);
            double tt, v;
            for (i = 0; ring_at(rg, i, &tt, &v); i++) if (!isnan(v)) { has[k] = 1; if (p->autoy && v > ymax) ymax = v * 1.1; }
        }
    if (ymax <= ymin) ymax = ymin + 1;
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);
    if (p->axes) {   /* room for the widest value label */
        char s[32];
        cairo_text_extents_t te;
        snprintf(s, sizeof s, "%.0f%s%s", ymax, p->unit && *p->unit ? " " : "", p->unit ? p->unit : "");
        cairo_text_extents(cr, s, &te);
        l = ceil(te.x_advance) + 8;
    }
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.04);
    cairo_rectangle(cr, l, t, r - l, b - t);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.25);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, l + 0.5, t + 0.5, r - l - 1, b - t - 1);
    cairo_stroke(cr);
    if (!h) return FALSE;
    t1 = h->tlast;   /* the x range follows the data (auto bounds), at most the last 30 s */
    x0 = h->tfirst > t1 - HSPAN ? h->tfirst : t1 - HSPAN;
    span = t1 - x0 > 1 ? t1 - x0 : 1;
    if (p->axes) {   /* three faint grid lines with their values */
        for (i = 0; i <= 2; i++) {
            double y = b - (b - t) * i / 2.0;
            char s[32];
            cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.12);
            cairo_move_to(cr, l, floor(y) + 0.5);
            cairo_line_to(cr, r, floor(y) + 0.5);
            cairo_stroke(cr);
            snprintf(s, sizeof s, "%.0f%s%s", ymin + (ymax - ymin) * i / 2.0, p->unit && *p->unit ? " " : "", p->unit ? p->unit : "");
            {
                cairo_text_extents_t te;
                cairo_text_extents(cr, s, &te);
                cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.7);
                cairo_move_to(cr, l - 4 - te.width, i == 0 ? b - 1 : i == 2 ? t + te.height + 1 : y + te.height / 2);
                cairo_show_text(cr, s);
            }
        }
    }
    for (k = 0; k < p->ns; k++) {
        const ring *rg = (const ring *)((const char *)h + p->s[k].off);
        const double *c = pal[p->s[k].color % 8];
        double tt, v, px = 0, firstx = -1;
        int pen = 0;
        cairo_new_path(cr);
        for (i = 0; ring_at(rg, i, &tt, &v); i++) {
            double x = l + (tt - x0) / span * (r - l), y;
            if (isnan(v) || x < l - 1) { pen = 0; continue; }
            if (v < ymin) v = ymin;
            if (v > ymax) v = ymax;
            y = b - (v - ymin) / (ymax - ymin) * (b - t);
            if (!pen) { cairo_move_to(cr, x, y); if (firstx < 0) firstx = x; pen = 1; }
            else cairo_line_to(cr, x, y);
            px = x;
        }
        if (p->fill && firstx >= 0 && p->ns == 1) {   /* fill under a single line, as amdgpu_top's fill(0.0) */
            cairo_path_t *path = cairo_copy_path(cr);
            cairo_line_to(cr, px, b);
            cairo_line_to(cr, firstx, b);
            cairo_close_path(cr);
            cairo_set_source_rgba(cr, c[0], c[1], c[2], 0.22);
            cairo_fill(cr);
            cairo_append_path(cr, path);
            cairo_path_destroy(path);
        }
        cairo_set_source_rgba(cr, c[0], c[1], c[2], 1);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
    }
    if (p->legend) {   /* top left, like Legend::default().position(Corner::LeftTop) */
        double x = l + 6, y = t + 6;
        for (k = 0; k < p->ns; k++) {
            const double *c = pal[p->s[k].color % 8];
            if (!has[k]) continue;   /* only the lines this card has */
            cairo_set_source_rgba(cr, c[0], c[1], c[2], 1);
            cairo_rectangle(cr, x, y + 2, 9, 9);
            cairo_fill(cr);
            cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.9);
            cairo_move_to(cr, x + 13, y + 10);
            cairo_show_text(cr, p->s[k].name);
            y += 14;
        }
    }
    return FALSE;
}

/* SRS writes temperatures as "26 °C"; the data layer keeps amdgpu_top's "26 C" for the CLI and JSON */
static const char *unit_disp(const char *u) { return u && !strcmp(u, "C") ? "°C" : u; }

static char *deg_c(const char *v)
{
    static GRegex *re;
    if (!re) re = g_regex_new("(\\d) C(?=$|[\\s,;)])", G_REGEX_OPTIMIZE, 0, NULL);
    return re ? g_regex_replace(re, v, -1, 0, "\\1 °C", 0, NULL) : g_strdup(v);
}

/* ---- the left panel: Device Info and the other static panels ---- */
static GtkWidget *kv_grid(void)
{
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g), 16);
    gtk_grid_set_row_spacing(GTK_GRID(g), 1);
    return g;
}

static void kv_row(GtkWidget *g, int *row, const char *k, const char *v)
{
    char *vs = g_strstrip(deg_c(v));   /* the data layer pads numbers, brackets names and writes "26 C" for the text dump */
    size_t n = strlen(vs);
    GtkWidget *a, *b;
    if (n >= 2 && vs[0] == '[' && vs[n - 1] == ']' && !strchr(vs + 1, '[')) { memmove(vs, vs + 1, n - 2); vs[n - 2] = 0; }
    a = lbl(k, 0);
    b = lbl(vs, 0);
    g_free(vs);
    gtk_style_context_add_class(gtk_widget_get_style_context(a), "dim-label");
    gtk_label_set_selectable(GTK_LABEL(b), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(b), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(b), 44);
    gtk_widget_set_can_focus(b, FALSE);
    gtk_grid_attach(GTK_GRID(g), a, 0, *row, 1, 1);
    gtk_grid_attach(GTK_GRID(g), b, 1, *row, 1, 1);
    (*row)++;
}

static void kv_head(GtkWidget *g, int *row, const char *t)
{
    GtkWidget *a = lbl_cls(t, "gt-sub");
    if (*row) gtk_widget_set_margin_top(a, 6);
    gtk_grid_attach(GTK_GRID(g), a, 0, *row, 2, 1);
    (*row)++;
}

static void kv_section(GtkWidget *g, int *row, const gt_section *s, int head)
{
    int i;
    if (!s->n) return;   /* no empty headings */
    if (head) kv_head(g, row, s->title);
    for (i = 0; i < s->n; i++) kv_row(g, row, s->kv[i].label, s->kv[i].value);
}

static const gt_section *find_sect(const gt_info *in, const char *title)
{
    int i;
    for (i = 0; i < in->nsect; i++) if (!g_ascii_strcasecmp(in->sect[i].title, title)) return &in->sect[i];
    return NULL;
}

static GtkWidget *mono_text(const char *t)
{
    GtkWidget *l = lbl(t, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), "gt-mono");
    gtk_label_set_selectable(GTK_LABEL(l), TRUE);
    gtk_widget_set_can_focus(l, FALSE);
    return l;
}

static void build_left(void)
{
    const gt_info *in = cur_info();
    GtkWidget *body, *g;
    GList *ch = gtk_container_get_children(GTK_CONTAINER(G->left)), *l;
    int row, i;
    static const char *own[] = { "Hardware IP", "IP Discovery", "Video Caps", "Video Decode", "Video Encode", "VBIOS", "Firmware",
                                 "pp_feature_mask", NULL };
    const gt_section *s, *dec, *enc;
    for (l = ch; l; l = l->next) gtk_widget_destroy(l->data);
    g_list_free(ch);
    if (!in) {
        slot *sl = G->sl[G->sel];
        int st = slot_state(sl);
        GtkWidget *m = lbl(st == -1 ? sl->err : "Reading the card...", 0);
        gtk_label_set_line_wrap(GTK_LABEL(m), TRUE);
        gtk_box_pack_start(GTK_BOX(G->left), m, FALSE, FALSE, 4);
        gtk_widget_show_all(G->left);
        G->shown_info = st == -1;   /* an error stays; "Reading" is replaced once the card is open */
        return;
    }
    G->shown_info = 1;
    /* Device Info: every section but the ones with their own panel below */
    panel(G->left, "Device Info", 1, &body);
    g = kv_grid();
    row = 0;
    for (i = 0; i < in->nsect; i++) {
        int k, skip = 0;
        for (k = 0; own[k]; k++) if (!g_ascii_strcasecmp(in->sect[i].title, own[k])) skip = 1;
        if (!skip) kv_section(g, &row, &in->sect[i], 1);
    }
    gtk_box_pack_start(GTK_BOX(body), g, FALSE, FALSE, 0);
    if (in->has_npu && G->xdna_ok) {
        panel(G->left, "XDNA NPU Info", 1, &body);
        gtk_box_pack_start(GTK_BOX(body), mono_text(G->xdna), FALSE, FALSE, 0);
    }
    if ((s = find_sect(in, "Hardware IP")) && s->n) {
        panel(G->left, "Hardware IP Info", 0, &body);
        g = kv_grid(); row = 0;
        kv_section(g, &row, s, 0);
        gtk_box_pack_start(GTK_BOX(body), g, FALSE, FALSE, 0);
    }
    if ((s = find_sect(in, "IP Discovery")) && s->n) {
        panel(G->left, "IP Discovery Table", 0, &body);
        g = kv_grid(); row = 0;
        kv_section(g, &row, s, 0);
        gtk_box_pack_start(GTK_BOX(body), g, FALSE, FALSE, 0);
    }
    dec = find_sect(in, "Video Decode");
    enc = find_sect(in, "Video Encode");
    if ((s = find_sect(in, "Video Caps")) && s->n) {   /* "4096x4096 (Decode), N/A (Encode)" -> Codec | Decode | Encode */
        int k;
        panel(G->left, "Video Caps Info", 0, &body);
        g = kv_grid();
        gtk_grid_attach(GTK_GRID(g), lbl_cls("Codec", "gt-sub"), 0, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(g), lbl_cls("Decode", "gt-sub"), 1, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(g), lbl_cls("Encode", "gt-sub"), 2, 0, 1, 1);
        for (k = 0; k < s->n; k++) {
            char d[80] = "", e[80] = "", *v = g_strdup(s->kv[k].value), *c = strstr(v, "(Decode)"), *x;
            GtkWidget *a = lbl(s->kv[k].label, 0);
            if (c) {
                *c = 0;
                snprintf(d, sizeof d, "%s", g_strstrip(v));
                x = c + strlen("(Decode)");
                while (*x == ',' || *x == ' ') x++;
                if ((c = strstr(x, "(Encode)"))) *c = 0;
                snprintf(e, sizeof e, "%s", g_strstrip(x));
            } else snprintf(d, sizeof d, "%s", g_strstrip(v));
            g_free(v);
            gtk_style_context_add_class(gtk_widget_get_style_context(a), "dim-label");
            gtk_grid_attach(GTK_GRID(g), a, 0, k + 1, 1, 1);
            gtk_grid_attach(GTK_GRID(g), lbl(d, 0), 1, k + 1, 1, 1);
            gtk_grid_attach(GTK_GRID(g), lbl(e, 0), 2, k + 1, 1, 1);
        }
        gtk_box_pack_start(GTK_BOX(body), g, FALSE, FALSE, 0);
    } else if ((dec && dec->n) || (enc && enc->n)) {   /* Codec | Decode | Encode, as amdgpu_top's codec table */
        GtkWidget *a;
        int r = 1, k;
        panel(G->left, "Video Caps Info", 0, &body);
        g = kv_grid();
        gtk_grid_attach(GTK_GRID(g), lbl_cls("Codec", "gt-sub"), 0, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(g), lbl_cls("Decode", "gt-sub"), 1, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(g), lbl_cls("Encode", "gt-sub"), 2, 0, 1, 1);
        for (k = 0; dec && k < dec->n; k++) {
            const char *e = "N/A";
            int j;
            for (j = 0; enc && j < enc->n; j++) if (!strcmp(enc->kv[j].label, dec->kv[k].label)) e = enc->kv[j].value;
            a = lbl(dec->kv[k].label, 0);
            gtk_style_context_add_class(gtk_widget_get_style_context(a), "dim-label");
            gtk_grid_attach(GTK_GRID(g), a, 0, r, 1, 1);
            gtk_grid_attach(GTK_GRID(g), lbl(dec->kv[k].value, 0), 1, r, 1, 1);
            gtk_grid_attach(GTK_GRID(g), lbl(e, 0), 2, r, 1, 1);
            r++;
        }
        for (k = 0; !dec && enc && k < enc->n; k++) {
            a = lbl(enc->kv[k].label, 0);
            gtk_style_context_add_class(gtk_widget_get_style_context(a), "dim-label");
            gtk_grid_attach(GTK_GRID(g), a, 0, r, 1, 1);
            gtk_grid_attach(GTK_GRID(g), lbl("N/A", 0), 1, r, 1, 1);
            gtk_grid_attach(GTK_GRID(g), lbl(enc->kv[k].value, 0), 2, r, 1, 1);
            r++;
        }
        gtk_box_pack_start(GTK_BOX(body), g, FALSE, FALSE, 0);
    }
    if ((s = find_sect(in, "VBIOS")) && s->n) {
        panel(G->left, "VBIOS Info", 0, &body);
        g = kv_grid(); row = 0;
        kv_section(g, &row, s, 0);
        gtk_box_pack_start(GTK_BOX(body), g, FALSE, FALSE, 0);
    }
    if ((s = find_sect(in, "Firmware")) && s->n) {
        panel(G->left, "Firmware Info", 0, &body);
        g = kv_grid(); row = 0;
        kv_section(g, &row, s, 0);
        gtk_box_pack_start(GTK_BOX(body), g, FALSE, FALSE, 0);
    }
    if ((s = find_sect(in, "pp_feature_mask")) && s->n) {
        panel(G->left, "PowerPlay Feature Mask", 0, &body);
        g = kv_grid(); row = 0;
        kv_section(g, &row, s, 0);
        gtk_box_pack_start(GTK_BOX(body), g, FALSE, FALSE, 0);
    }
    if (G->sl[G->sel] && G->sl[G->sel]->drm_ok && *G->sl[G->sel]->drm) {   /* state 1 was seen above (cur_info) */
        panel(G->left, "Connector Info", 0, &body);
        gtk_box_pack_start(GTK_BOX(body), lbl("The connectors, modes and planes are on the DRM Info tab.", 0), FALSE, FALSE, 0);
    }
    gtk_widget_show_all(G->left);
}

/* ---- the extra text tabs ---- */
static void text_set(GtkWidget *v, const char *t)
{
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(v)), t, -1);
}

static void fill_text_tabs(void)
{
    slot *s = G->sl[G->sel];
    const char *nvna = "Not available on NVIDIA cards.";
    int st = slot_state(s);
    if (st != 1) {
        text_set(G->drm_view, st == -1 ? s->err : !s ? "This card is asleep (runtime suspended) and is not read." : "Reading the card...");
        text_set(G->pp_view, "");
        return;
    }
    text_set(G->drm_view, *s->drm ? s->drm : "No DRM information for this card.");
    if (is_nv()) text_set(G->pp_view, nvna);
    else text_set(G->pp_view, *s->pp ? s->pp : "The PowerPlay table can be decoded on Radeon RX 5000, 6000 and 7000 cards (Navi 1x, 2x, 3x) only.");
}

/* ---- GRBM cells: "Graphics Pipe  42%" over a small graph; GT_MAX_PC made once, the unused ones hidden ---- */
static void build_pc(GtkWidget *box, GtkWidget **lbls, int two)
{
    int i;
    for (i = 0; i < GT_MAX_PC; i++) {
        GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
        plot *p = new_plot(300, 40, 1, 0, 0, 0, 100, 0, "%");
        lbls[i] = lbl("", 0);
        gtk_style_context_add_class(gtk_widget_get_style_context(lbls[i]), "gt-num");
        plot_add(p, two ? HOFF(grbm2) + i * sizeof(ring) : HOFF(grbm) + i * sizeof(ring), "");
        gtk_box_pack_start(GTK_BOX(v), lbls[i], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(v), p->da, FALSE, FALSE, 0);
        gtk_flow_box_insert(GTK_FLOW_BOX(box), v, -1);
    }
}

static void set_pc(GtkWidget **lbls, const gt_pc *pc, int n)
{
    char b[96];
    int i;
    for (i = 0; i < GT_MAX_PC; i++) {
        GtkWidget *cell = gtk_widget_get_parent(gtk_widget_get_parent(lbls[i]));   /* label -> box -> flowboxchild */
        gtk_widget_set_visible(cell, i < n);
        if (i >= n) continue;
        snprintf(b, sizeof b, "%-32s %3d%%", pc[i].name, pc[i].pct);
        set_text(lbls[i], b);
    }
}

/* ---- per-sample updates ---- */
static void hist_push(int i, const gt_stat *s)
{
    hist *h = G->h[i];
    double t = (double)(g_get_monotonic_time() - G->t0) / 1e6;
    const gt_metrics *m = &s->metrics;
    int k;
    char thr[200] = "";
    if (h->tlast <= 0) h->tfirst = t;
    h->tlast = t;
    for (k = 0; k < s->ngrbm && k < GT_MAX_PC; k++) ring_push(&h->grbm[k], t, s->pc_ok ? s->grbm[k].pct : NAN);
    for (k = 0; k < s->ngrbm2 && k < GT_MAX_PC; k++) ring_push(&h->grbm2[k], t, s->pc_ok ? s->grbm2[k].pct : NAN);
    ring_push(&h->vram, t, s->vram_used == GT_NA ? NAN : (double)(s->vram_used >> 20));
    ring_push(&h->gtt, t, s->gtt_used == GT_NA ? NAN : (double)(s->gtt_used >> 20));
    ring_push(&h->gfx, t, nv(s->gfx));
    ring_push(&h->umc, t, nv(s->umc));
    ring_push(&h->media, t, nv(s->media));
    ring_push(&h->fd_gfx, t, nv(s->total.gfx));
    ring_push(&h->fd_compute, t, nv(s->total.compute));
    ring_push(&h->fd_dma, t, nv(s->total.dma));
    ring_push(&h->fd_dec, t, nv(s->total.dec));
    ring_push(&h->fd_enc, t, nv(s->total.enc));
    ring_push(&h->fd_vcnu, t, nv(s->total.vcn_unified));
    ring_push(&h->fd_vpe, t, nv(s->total.vpe));
    ring_push(&h->sclk, t, nv(s->sclk));
    ring_push(&h->mclk, t, nv(s->mclk));
    ring_push(&h->fclk, t, nv(s->fclk));
    ring_push(&h->vddgfx, t, nv(s->vddgfx));
    ring_push(&h->pavg, t, s->power_avg < 0 ? NAN : s->power_avg);
    ring_push(&h->pin, t, s->power_input < 0 ? NAN : s->power_input);
    ring_push(&h->fan, t, nv(s->fan_rpm));
    ring_push(&h->edge, t, nv(s->edge));
    ring_push(&h->junction, t, nv(s->junction));
    ring_push(&h->memt, t, nv(s->memtemp));
    ring_push(&h->tctl, t, nv(s->tctl));
    ring_push(&h->tx, t, s->pcie_tx_kbs == GT_NA ? NAN : s->pcie_tx_kbs / 1024.0);
    ring_push(&h->rx, t, s->pcie_rx_kbs == GT_NA ? NAN : s->pcie_rx_kbs / 1024.0);
    ring_push(&h->avclk, t, m->present ? nv(m->v[GT_M_AVERAGE_VCLK_FREQUENCY]) : NAN);
    ring_push(&h->adclk, t, m->present ? nv(m->v[GT_M_AVERAGE_DCLK_FREQUENCY]) : NAN);
    ring_push(&h->avclk1, t, m->present ? nv(m->v[GT_M_AVERAGE_VCLK1_FREQUENCY]) : NAN);
    ring_push(&h->adclk1, t, m->present ? nv(m->v[GT_M_AVERAGE_DCLK1_FREQUENCY]) : NAN);
    ring_push(&h->cvclk, t, m->present ? nv(m->v[GT_M_CURRENT_VCLK]) : NAN);
    ring_push(&h->cdclk, t, m->present ? nv(m->v[GT_M_CURRENT_DCLK]) : NAN);
    ring_push(&h->cvclk1, t, m->present ? nv(m->v[GT_M_CURRENT_VCLK1]) : NAN);
    ring_push(&h->cdclk1, t, m->present ? nv(m->v[GT_M_CURRENT_DCLK1]) : NAN);
    /* throttling log: a line whenever a throttle reason is on, as amdgpu_top's throttling_history */
    for (k = 0; k < s->nthr; k++) {
        size_t n = strlen(thr);
        snprintf(thr + n, sizeof thr - n, "%s%s", k ? ", " : "", s->thr[k]);
    }
    if (*thr) {
        snprintf(h->thrlog[h->thrhead], sizeof h->thrlog[0], "%6.1f s: [%s]", t, thr);
        h->thrhead = (h->thrhead + 1) % NTHRLOG;
        if (h->nthr < NTHRLOG) h->nthr++;
    }
    snprintf(h->lastthr, sizeof h->lastthr, "%s", *thr ? thr : "none");
}

static void proc_set(GtkListStore *st, GtkTreeIter *it, int gpu, const gt_proc *p)
{
    gtk_list_store_set(st, it, P_GPU, gpu, P_NAME, p->name, P_PID, p->pid, P_KFD, p->is_kfd, P_VRAM, (gint64)p->vram_kib,
                       P_GTT, (gint64)p->gtt_kib, P_CPU, p->cpu, P_GFX, p->gfx, P_COMP, p->compute, P_DMA, p->dma,
                       P_VCNU, p->vcn_unified, P_DEC, p->dec, P_ENC, p->enc, P_VPE, p->vpe, -1);
}

/* the process rows of one card (only >= 0) or of all cards, updated in place by card + PID, so the scroll position,
   the selection and the sort stay put from one sample to the next */
static void update_procs(GtkListStore *st, int only)
{
    static unsigned char used[GT_MAX_DEV][GT_MAX_PROC];
    GArray *rows = g_array_new(FALSE, FALSE, sizeof(GtkTreeIter));
    GtkTreeModel *m = GTK_TREE_MODEL(st);
    GtkTreeIter it;
    gboolean ok;
    guint r;
    int g, i;
    memset(used, 0, sizeof used);
    for (ok = gtk_tree_model_get_iter_first(m, &it); ok; ok = gtk_tree_model_iter_next(m, &it)) g_array_append_val(rows, it);
    for (r = 0; r < rows->len; r++) {   /* list store iters stay valid while their row exists, also when the sort moves it */
        GtkTreeIter *ri = &g_array_index(rows, GtkTreeIter, r);
        int gpu, pid, found = 0;
        gtk_tree_model_get(m, ri, P_GPU, &gpu, P_PID, &pid, -1);
        if (gpu >= 0 && gpu < G->ndev && G->have[gpu] && (only < 0 || gpu == only))
            for (i = 0; i < G->cur[gpu]->nproc && i < GT_MAX_PROC; i++)
                if (!used[gpu][i] && G->cur[gpu]->proc[i].pid == pid) {
                    proc_set(st, ri, gpu, &G->cur[gpu]->proc[i]);
                    used[gpu][i] = 1;
                    found = 1;
                    break;
                }
        if (!found) gtk_list_store_remove(st, ri);
    }
    g_array_free(rows, TRUE);
    for (g = 0; g < G->ndev; g++) {
        if (!G->have[g] || (only >= 0 && g != only)) continue;
        for (i = 0; i < G->cur[g]->nproc && i < GT_MAX_PROC; i++)
            if (!used[g][i]) { gtk_list_store_append(st, &it); proc_set(st, &it, g, &G->cur[g]->proc[i]); }
    }
}

static void update_smi(void)
{
    GtkTreeIter it;
    int i;
    gtk_list_store_clear(G->smi_store);
    for (i = 0; i < G->ndev; i++) {
        const gt_device *d = &G->dev[i];
        slot *sl = G->sl[i];
        int st = slot_state(sl);
        const gt_info *in = st == 1 ? sl->info : NULL;
        const gt_stat *s = G->have[i] ? G->cur[i] : NULL;
        char name[200], vram[48], gtt[48], sclk[24], mclk[24], vdd[24], pw[32], gfx[12], umc[12], media[12], temp[24], fan[24], thr[200];
        snprintf(name, sizeof name, "%s", in && *in->dev.name ? in->dev.name : d->name);   /* NVML / amdgpu.ids name once open */
        strcpy(vram, "-"); strcpy(gtt, "-"); strcpy(sclk, "-"); strcpy(mclk, "-"); strcpy(vdd, "-");
        strcpy(pw, "-"); strcpy(gfx, "-"); strcpy(umc, "-"); strcpy(media, "-"); strcpy(temp, "-"); strcpy(fan, "-");
        snprintf(thr, sizeof thr, "%s", !sl ? (d->active ? "" : "Asleep (runtime suspended) - not read until you pick it") : st == -1 ? sl->err : s ? "" : "Reading...");
        if (s) {
            int tc = s->junction != GT_NA ? s->junction : s->edge;
            if (s->vram_used != GT_NA && s->vram_total > 0) snprintf(vram, sizeof vram, "%lld/%lld MiB", s->vram_used >> 20, s->vram_total >> 20);
            if (s->gtt_used != GT_NA && s->gtt_total > 0) snprintf(gtt, sizeof gtt, "%lld/%lld MiB", s->gtt_used >> 20, s->gtt_total >> 20);
#define SMI_I(b, v, u) do { if ((v) != GT_NA) snprintf(b, sizeof b, "%d%s", (int)(v), u); } while (0)
            SMI_I(sclk, s->sclk, " MHz");
            SMI_I(mclk, s->mclk, " MHz");
            SMI_I(vdd, s->vddgfx, " mV");
            if (s->power_avg >= 0 || s->power_input >= 0) {
                double pv = s->power_avg >= 0 ? s->power_avg : s->power_input;
                if (s->power_cap != GT_NA) snprintf(pw, sizeof pw, "%.0f/%d W", pv, s->power_cap);
                else snprintf(pw, sizeof pw, "%.0f W", pv);
            }
            SMI_I(gfx, s->gfx, "%");
            SMI_I(umc, s->umc, "%");
            SMI_I(media, s->media, "%");
            if (tc != GT_NA) {
                if (s->memtemp != GT_NA) snprintf(temp, sizeof temp, "%d/%d °C", tc, s->memtemp);
                else snprintf(temp, sizeof temp, "%d/- °C", tc);
            }
            SMI_I(fan, s->fan_rpm, " RPM");
#undef SMI_I
            if (s->nthr) {
                int k;
                thr[0] = 0;
                for (k = 0; k < s->nthr; k++) {
                    size_t n = strlen(thr);
                    snprintf(thr + n, sizeof thr - n, "%s%s", k ? ", " : "", s->thr[k]);
                }
            } else if (s->metrics.present || G->dev[i].vendor == GT_VENDOR_NVIDIA) snprintf(thr, sizeof thr, "none");
            else if (*s->pci_power_state && s->idle_d3) snprintf(thr, sizeof thr, "N/A (%s)", s->pci_power_state);
            else snprintf(thr, sizeof thr, "N/A");
        }
        gtk_list_store_insert_with_values(G->smi_store, &it, -1, S_IDX, i, S_NAME, name, S_TARGET, in ? in->gfx_target : "",
                                          S_PCI, d->pci, S_VRAM, vram, S_GTT, gtt, S_SCLK, sclk, S_MCLK, mclk, S_VDD, vdd,
                                          S_POWER, pw, S_GFX, gfx, S_UMC, umc, S_MEDIA, media, S_TEMP, temp, S_FAN, fan, S_THR, thr, -1);
    }
    update_procs(G->smi_proc_store, -1);
}

static void set_cols_visible(const gt_stat *s)
{
    int has_vcnu = 0, has_vpe = 0, nvml = is_nv(), i;
    const gt_info *in = cur_info();
    if (in) { has_vcnu = in->has_vcn_unified; has_vpe = in->has_vpe; }
    for (i = 0; s && i < s->nproc; i++) { if (s->proc[i].vcn_unified != GT_NA) has_vcnu = 1; if (s->proc[i].vpe != GT_NA) has_vpe = 1; }
    gtk_tree_view_column_set_visible(G->fd_cols[P_VCNU], has_vcnu && !nvml);
    gtk_tree_view_column_set_visible(G->fd_cols[P_DEC], !has_vcnu);
    gtk_tree_view_column_set_visible(G->fd_cols[P_ENC], !has_vcnu);
    gtk_tree_view_column_set_visible(G->fd_cols[P_VPE], has_vpe && !nvml);
    gtk_tree_view_column_set_visible(G->fd_cols[P_KFD], !nvml);
    gtk_tree_view_column_set_visible(G->fd_cols[P_GTT], !nvml);
    gtk_tree_view_column_set_visible(G->fd_cols[P_CPU], !nvml);
    gtk_tree_view_column_set_visible(G->fd_cols[P_COMP], !nvml);
    gtk_tree_view_column_set_visible(G->fd_cols[P_DMA], !nvml);
}

static void update_metrics(const gt_stat *s)
{
    const gt_metrics *m = &s->metrics;
    char b[256];
    int i, row = 0;
    if (!m->present) {
        gtk_widget_hide(G->metrics_grid);
        gtk_widget_hide(G->metrics_agrid);
        gtk_widget_hide(G->x_vclk);
        gtk_widget_show(G->metrics_na);
        set_text(G->metrics_na, is_nv() ? "Not available on NVIDIA cards." :
                 s->idle_d3 ? "Not read while the card is idle, so it can go to sleep; it comes back when the card is used." :
                 "This card or driver has no gpu_metrics file.");
        snprintf(b, sizeof b, "GPU Metrics");
        set_text(gtk_expander_get_label_widget(GTK_EXPANDER(G->x_metrics)), b);
        return;
    }
    gtk_widget_hide(G->metrics_na);
    gtk_widget_show(G->metrics_grid);
    gtk_widget_show(G->metrics_agrid);
    gtk_widget_show(G->x_vclk);
    snprintf(b, sizeof b, "GPU Metrics v%d.%d", m->format_rev, m->content_rev);
    set_text(gtk_expander_get_label_widget(GTK_EXPANDER(G->x_metrics)), b);
    for (i = 0; i < GT_M_COUNT; i++) {
        int show = m->v[i] != GT_NA && i != GT_M_THROTTLE_STATUS && i != GT_M_INDEP_THROTTLE_STATUS;
        if (show) {
            const char *u;
            double sv = gt_metric_shown(m, i, &u);   /* the same scaling as the CLI and the Activity panel */
            if (i == GT_M_GFXCLK_LOCK_STATUS) snprintf(b, sizeof b, "0x%llX", (unsigned long long)m->v[i]);
            else if (m->format_rev >= 2 && (!strcmp(u, "C") || !strcmp(u, "%")))   /* APU tables: 0.01 C / 0.01 % */
                snprintf(b, sizeof b, "%.2f", sv);
            else snprintf(b, sizeof b, "%lld", m->v[i]);
            set_text(G->m_val[i], b);
            set_text(G->m_unit[i], unit_disp(u));
            row++;
        }
        gtk_widget_set_visible(G->m_name[i], show);
        gtk_widget_set_visible(G->m_val[i], show);
        gtk_widget_set_visible(G->m_unit[i], show);
    }
    for (i = 0; i < GT_MA_COUNT; i++) {
        int show = m->na[i] > 0, k, centi = m->format_rev >= 2 && strstr(gt_metric_array_name(i), "temperature");   /* 0.01 C on APUs */
        if (show) {
            b[0] = 0;
            for (k = 0; k < m->na[i] && k < GT_MA_LEN; k++) {
                size_t n = strlen(b);
                if (centi) snprintf(b + n, sizeof b - n, "%s%5.1f", k ? ", " : "[", m->a[i][k] / 100.0);
                else snprintf(b + n, sizeof b - n, "%s%5d", k ? ", " : "[", m->a[i][k]);
            }
            strncat(b, "]", sizeof b - strlen(b) - 1);
            set_text(G->ma_val[i], b);
        }
        gtk_widget_set_visible(G->ma_name[i], show);
        gtk_widget_set_visible(G->ma_val[i], show);
    }
    (void)row;
}

static void update_sel(void)
{
    const gt_stat *s = G->have[G->sel] ? G->cur[G->sel] : NULL;
    const gt_info *in = cur_info();
    hist *h = G->h[G->sel];
    char b[400];
    int i, nvml = is_nv();
    if (!s) {
        slot *sl = G->sl[G->sel];
        gtk_widget_hide(G->panels);
        gtk_widget_show(G->msg);
        set_text(G->msg, slot_state(sl) == -1 ? sl->err
                         : !sl ? "This card is asleep (runtime suspended) and is not read, so it can stay asleep. Pick it in the list to wake it up."
                         : "Reading the card...");
        return;
    }
    gtk_widget_hide(G->msg);
    gtk_widget_show(G->panels);
    /* GRBM / GRBM2 */
    gtk_widget_set_visible(G->x_grbm, !G->no_pc);
    gtk_widget_set_visible(G->x_grbm2, !G->no_pc);
    if (!s->pc_ok || !s->ngrbm) {
        gtk_widget_hide(G->grbm_box);
        gtk_widget_hide(G->grbm2_box);
        gtk_widget_show(G->grbm_na);
        gtk_widget_show(G->grbm2_na);
        set_text(G->grbm_na, nvml ? "Not available on NVIDIA cards." : "The performance counters could not be read on this card.");
        set_text(G->grbm2_na, nvml ? "Not available on NVIDIA cards." : "The performance counters could not be read on this card.");
    } else {
        gtk_widget_hide(G->grbm_na);
        gtk_widget_hide(G->grbm2_na);
        gtk_widget_show(G->grbm_box);
        gtk_widget_show(G->grbm2_box);
        set_pc(G->grbm_lbl, s->grbm, s->ngrbm);
        set_pc(G->grbm2_lbl, s->grbm2, s->ngrbm2);
    }
    /* VRAM, CPU-Visible VRAM, GTT: "used / total (usable: x) MiB" */
    {
        long long u[3] = { s->vram_used, s->vis_vram_used, s->gtt_used }, t[3] = { s->vram_total, s->vis_vram_total, s->gtt_total },
                  us[3] = { s->vram_usable, in ? in->vis_vram_total : GT_NA, s->gtt_usable };
        plot *vp = &G->plots[G->vram_plot];
        vp->ymax = (double)((u[2] != GT_NA && t[2] > t[0] ? t[2] : t[0]) >> 20);   /* amdgpu_top: include_y(max(VRAM, GTT size)) */
        vp->autoy = vp->ymax <= 0;
        if (vp->ymax <= 0) vp->ymax = 1024;
        for (i = 0; i < 3; i++) {
            int ok = u[i] != GT_NA && t[i] > 0;
            gtk_widget_set_visible(G->vram_bar[i], ok);
            gtk_widget_set_visible(G->vram_lbl[i], ok);
            if (!ok) continue;
            if (us[i] != GT_NA && us[i] > 0) snprintf(b, sizeof b, "%5lld / %5lld (usable: %5lld) MiB", u[i] >> 20, t[i] >> 20, us[i] >> 20);
            else snprintf(b, sizeof b, "%5lld / %5lld MiB", u[i] >> 20, t[i] >> 20);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(G->vram_bar[i]), (double)u[i] / (double)t[i]);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(G->vram_bar[i]), b);
        }
    }
    /* Activity */
    {
        char a[3][12];
        long long v[3] = { s->gfx, s->umc, s->media };
        for (i = 0; i < 3; i++) if (v[i] == GT_NA) snprintf(a[i], sizeof a[i], "___"); else snprintf(a[i], sizeof a[i], "%3lld", v[i]);
        snprintf(b, sizeof b, "GFX: %s%%, Memory: %s%%, Media: %s%%", a[0], a[1], a[2]);
        set_text(G->act_lbl, b);
    }
    /* Processes */
    set_cols_visible(s);
    update_procs(G->fd_store, G->sel);
    /* Sensors: "GFX_SCLK (1234 MHz) (45%)" and the temperatures */
    {
        int maxg = in ? (in->max_od_gpu_clk > 0 ? in->max_od_gpu_clk : in->max_gpu_clk) : GT_NA;
        int maxm = in ? (in->max_od_mem_clk > 0 ? in->max_od_mem_clk : in->max_mem_clk) : GT_NA;
        int cap = s->power_cap != GT_NA ? s->power_cap : 350;
        struct { const char *name; double v; int min, max; const char *unit; } sn[NSENS] = {
            { "GFX_SCLK", nv(s->sclk), 0, maxg > 0 ? maxg : 3000, "MHz" },
            { "GFX_MCLK", nv(s->mclk), 0, maxm > 0 ? maxm : 3000, "MHz" },
            { "FCLK", nv(s->fclk), s->fclk_min != GT_NA ? s->fclk_min : 0, s->fclk_max != GT_NA ? s->fclk_max : 3000, "MHz" },
            { "VDDGFX", nv(s->vddgfx), 500, 1500, "mV" },
            { "Average Power", s->power_avg < 0 ? NAN : s->power_avg, 0, cap, "W" },
            { "Input Power", s->power_input < 0 ? NAN : s->power_input, 0, cap, "W" },
            { "Fan", nv(s->fan_rpm), 0, s->fan_max_rpm != GT_NA && s->fan_max_rpm > 0 ? s->fan_max_rpm : 6000, "RPM" } };
        struct { const char *name; double v; int crit; } tp[NTEMP] = {
            { "Edge", nv(s->edge), in && in->edge_crit != GT_NA ? in->edge_crit : 105 },
            { "Junction", nv(s->junction), in && in->junction_crit != GT_NA ? in->junction_crit : 105 },
            { "Memory", nv(s->memtemp), in && in->mem_crit != GT_NA ? in->mem_crit : 105 },
            { "CPU Tctl", nv(s->tctl), 100 } };
        for (i = 0; i < NSENS; i++) {
            plot *p = &G->plots[G->sens_plot[i]];
            int ok = !isnan(sn[i].v);
            gtk_widget_set_visible(gtk_widget_get_parent(G->sens_cell[i]), ok);
            if (!ok) continue;
            p->ymin = sn[i].min;
            p->ymax = sn[i].max;
            if (sn[i].min == 0 && sn[i].max > 0)
                snprintf(b, sizeof b, "%s (%4.0f %s) (%3.0f%%)", sn[i].name, sn[i].v, sn[i].unit, sn[i].v * 100.0 / sn[i].max);
            else snprintf(b, sizeof b, "%s (%4.0f %s)", sn[i].name, sn[i].v, sn[i].unit);
            set_text(G->sens_lbl[i], b);
        }
        for (i = 0; i < NTEMP; i++) {
            plot *p = &G->plots[G->sens_plot[NSENS + i]];
            int ok = !isnan(tp[i].v);
            gtk_widget_set_visible(gtk_widget_get_parent(G->sens_cell[NSENS + i]), ok);
            if (!ok) continue;
            p->ymax = tp[i].crit;
            snprintf(b, sizeof b, "%s Temp. (%4.0f °C)", tp[i].name, tp[i].v);
            set_text(G->sens_lbl[NSENS + i], b);
        }
        if (s->pcie_gen != GT_NA && s->pcie_width != GT_NA) {
            char mm[96] = "";
            if (in && in->pcie_dpm_min_gen != GT_NA && in->pcie_dpm_max_gen != GT_NA)
                snprintf(mm, sizeof mm, " (Gen%dx%d - Gen%dx%d)", in->pcie_dpm_min_gen, in->pcie_dpm_min_width, in->pcie_dpm_max_gen, in->pcie_dpm_max_width);
            else if (in && in->pcie_dpm_max_gen != GT_NA) snprintf(mm, sizeof mm, " (Max Gen%dx%d)", in->pcie_dpm_max_gen, in->pcie_dpm_max_width);
            snprintf(b, sizeof b, "PCIe Link Speed => Gen%dx%d%s", s->pcie_gen, s->pcie_width, mm);
            set_text(G->link_lbl, b);
            gtk_widget_show(G->link_lbl);
        } else gtk_widget_hide(G->link_lbl);
        snprintf(b, sizeof b, "PCI Power State: %s", s->pci_power_state);
        set_text(G->pstate_lbl, b);
        gtk_widget_set_visible(G->pstate_lbl, *s->pci_power_state != 0);
        snprintf(b, sizeof b, "Power Profile: %s", s->power_profile);
        set_text(G->profile_lbl, b);
        gtk_widget_set_visible(G->profile_lbl, *s->power_profile != 0);
        snprintf(b, sizeof b, "Throttle Status: %s", h->lastthr);
        set_text(G->thr_lbl, b);
        b[0] = 0;
        for (i = 0; i < s->ndpm && i < 5; i++) {   /* clock levels, '*' = now (amdgpu_top TUI's DPM lines) */
            const gt_dpm *d = &s->dpm[i];
            int k;
            size_t n = strlen(b);
            snprintf(b + n, sizeof b - n, "%s%-6s", i ? "\n" : "", d->name);
            for (k = 0; k < d->n && k < GT_MAX_DPM; k++) {
                n = strlen(b);
                if (!strcmp(d->name, "pcie")) snprintf(b + n, sizeof b - n, " %sGen%dx%d", k == d->cur ? "*" : " ", d->mhz[k], d->lanes[k]);
                else snprintf(b + n, sizeof b - n, " %s%d", k == d->cur ? "*" : " ", d->mhz[k]);
            }
        }
        set_text(G->dpm_lbl, *b ? b : "");
        gtk_widget_set_visible(G->dpm_lbl, *b != 0);
    }
    /* PCIe Bandwidth */
    gtk_widget_set_visible(G->x_pcie, s->pcie_tx_kbs != GT_NA || s->pcie_rx_kbs != GT_NA);
    if (s->pcie_tx_kbs != GT_NA || s->pcie_rx_kbs != GT_NA) {
        snprintf(b, sizeof b, "Sent: %5lld MiB/s, Received: %5lld MiB/s", s->pcie_tx_kbs == GT_NA ? 0 : s->pcie_tx_kbs / 1024,
                 s->pcie_rx_kbs == GT_NA ? 0 : s->pcie_rx_kbs / 1024);
        set_text(G->pcie_lbl, b);
    }
    /* ECC */
    gtk_widget_set_visible(G->x_ecc, s->mem_errors != GT_NA);
    if (s->mem_errors != GT_NA) {
        snprintf(b, sizeof b, "Corrected + UnCorrected: %lld", s->mem_errors);
        set_text(G->ecc_lbl, b);
    }
    /* GPU Metrics and the Throttling Log */
    update_metrics(s);
    {
        GString *g = g_string_new(NULL);
        int k;
        for (k = 0; k < h->nthr; k++) {
            int j = (h->thrhead - h->nthr + k + NTHRLOG) % NTHRLOG;
            g_string_append_printf(g, "%s%s", k ? "\n" : "", h->thrlog[j]);
        }
        set_text(G->thrlog_lbl, g->len ? g->str : "No throttling in the last samples.");
        g_string_free(g, TRUE);
    }
    snprintf(b, sizeof b, "Updated every %g s, graphs show the last %.0f s%s", G->interval_ms / 1000.0, HSPAN,
             s->idle_d3 ? "  -  the card is asleep (D3), so some values are not read" : "");
    set_text(G->status, b);
}

static void redraw_plots(void)
{
    int i;
    for (i = 0; i < G->nplots; i++) if (gtk_widget_get_mapped(G->plots[i].da)) gtk_widget_queue_draw(G->plots[i].da);
}

static gboolean tick(gpointer d)
{
    int i, changed = 0, selchg = 0;
    (void)d;
    if (!G) return G_SOURCE_REMOVE;
    for (i = 0; i < G->ndev; i++) {
        slot *s = G->sl[i];
        int st;
        if (!s) continue;
        g_mutex_lock(&s->mu);
        st = s->state;
        if (s->seq != G->seen[i] && s->stat) {
            *G->cur[i] = *s->stat;
            G->seen[i] = s->seq;
            G->have[i] = 1;
            changed = 1;
            if (i == G->sel) selchg = 1;
        }
        g_mutex_unlock(&s->mu);
        if (i == G->sel && st != 0 && !G->shown_info) { build_left(); fill_text_tabs(); if (!G->have[i]) update_sel(); }
        if (st == 1 && !G->named[i]) {   /* the device list: the card's full name once it is open, and no longer "Suspended" */
            G->named[i] = 1;
            if (*s->info->dev.name) snprintf(G->dev[i].name, sizeof G->dev[i].name, "%s", s->info->dev.name);
            if (G->combo) {
                char t[220];
                snprintf(t, sizeof t, "#%d %s (%s)", i, G->dev[i].name, G->dev[i].pci);
                G->building = 1;
                gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(G->combo), i);
                gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(G->combo), i, t);
                gtk_combo_box_set_active(GTK_COMBO_BOX(G->combo), G->sel);
                G->building = 0;
            }
        }
    }
    /* history: once per new sample, also while paused (amdgpu_top's sampler keeps its history; Pause freezes the view) */
    for (i = 0; changed && i < G->ndev; i++)
        if (G->have[i] && G->pushed[i] != G->seen[i]) { hist_push(i, G->cur[i]); G->pushed[i] = G->seen[i]; }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(G->pause))) { if (selchg) G->dirty = 1; return G_SOURCE_CONTINUE; }
    if (changed) update_smi();
    if (selchg || G->dirty) { update_sel(); G->dirty = 0; }
    redraw_plots();
    return G_SOURCE_CONTINUE;
}

/* ---- starting and stopping the workers ---- */
static void start_slot(int i)
{
    slot *s;
    GThread *t;
    if (G->sl[i]) return;
    if (getenv("SRS_TEST_GPU_ROOT") && getenv("SRS_TEST_BIGTOP_LOG")) fprintf(stderr, "SRS-TEST: bigtop open #%d %s\n", i, G->dev[i].pci);
    s = g_new0(slot, 1);
    g_mutex_init(&s->mu);
    s->ref = 2;   /* the window and the worker */
    s->index = G->dev[i].index;
    s->no_pc = G->no_pc;
    s->interval_ms = G->interval_ms;
    G->sl[i] = s;
    G->seen[i] = G->pushed[i] = 0;
    t = g_thread_try_new("gpu-top", worker, s, NULL);
    if (t) g_thread_unref(t);
    else { s->state = -1; snprintf(s->err, sizeof s->err, "Could not start a thread for this card"); slot_unref(s); }
}

static void stop_all(void)
{
    int i;
    for (i = 0; i < G->ndev; i++)
        if (G->sl[i]) { g_atomic_int_set(&G->sl[i]->stop, 1); slot_unref(G->sl[i]); G->sl[i] = NULL; }
    if (G->timer) { g_source_remove(G->timer); G->timer = 0; }
}

static void start_all(void)
{
    int i;
    for (i = 0; i < G->ndev; i++) if (G->dev[i].active || G->woken[i]) start_slot(i);   /* asleep: only once the user picked it */
    if (!G->timer) G->timer = g_timeout_add(G->interval_ms < REDRAW_MS ? (guint)G->interval_ms : REDRAW_MS, tick, NULL);
    G->shown_info = 0;
}

static void on_map(GtkWidget *w, gpointer d) { (void)w; (void)d; start_all(); }
static void on_unmap(GtkWidget *w, gpointer d) { (void)w; (void)d; if (G) stop_all(); }

static void select_dev(int i)
{
    if (i < 0 || i >= G->ndev) return;
    G->sel = i;
    G->woken[i] = 1;   /* picked by the user: a runtime-suspended card is opened (woken) only now */
    if (!G->sl[i] && gtk_widget_get_mapped(G->win)) start_slot(i);
    build_left();
    fill_text_tabs();
    update_sel();
    redraw_plots();
}

static void on_update(GtkComboBox *c, gpointer d)
{
    int i = gtk_combo_box_get_active(c);
    (void)d;
    if (i < 0 || G->building) return;
    perf_prefs_set_gpu_top_ms(perf_speed_choice_ms(i));   /* saved as [GPU Top] Update ms; on_prefs applies it */
}

/* a setting changed (here, or in the Task Manager settings): read the cards at the new rate; the history is kept */
static void on_prefs(void *user)
{
    int ms = perf_prefs_gpu_top_ms();
    (void)user;
    if (!G || ms == G->interval_ms) return;
    G->interval_ms = ms;
    if (G->upd) {
        G->building = 1;
        gtk_combo_box_set_active(GTK_COMBO_BOX(G->upd), perf_speed_choice_of_ms(ms));
        G->building = 0;
    }
    if (gtk_widget_get_mapped(G->win)) { stop_all(); start_all(); }
}

static void on_settings(GtkButton *b, gpointer d) { (void)b; (void)d; perf_tray_settings_dialog(GTK_WINDOW(G->win)); }

/* the user guide at a section ("gpu-top", "perf-summary", "tray-meters", ...): SRS_GUIDE_DRY_RUN=1 only shows the command */
void gpu_top_open_guide(GtkWindow *parent, const char *anchor)
{
    char id[128], msg[4000] = "";
    snprintf(id, sizeof id, "#%s%s", GUIDE_PREFIX, anchor && *anchor ? anchor : "gpu-top");
    if (!srs_guide_open(id, getenv("SRS_GUIDE_DRY_RUN") != NULL, msg, sizeof msg) || *msg) {
        GtkWidget *d = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s",
                                              *msg ? msg : "Could not open the user guide.");
        gtk_window_set_title(GTK_WINDOW(d), "SRS User Guide");
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
    }
}

/* the section for the tab shown now (the Performance tab says which of its pages is shown) */
static void on_guide(GtkButton *b, gpointer d)
{
    GtkWidget *page;
    const char *a = "gpu-top";
    (void)b; (void)d;
    if (!G || !G->nb) { gpu_top_open_guide(G ? GTK_WINDOW(G->win) : NULL, a); return; }
    page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(G->nb), gtk_notebook_get_current_page(GTK_NOTEBOOK(G->nb)));
    if (page && page == G->page[GPU_TOP_PAGE_PERFORMANCE]) {
        a = g_object_get_data(G_OBJECT(page), "srs-guide-anchor");
        if (!a) a = "perf-summary";
    } else if (page == G->page[GPU_TOP_PAGE_SMI]) a = "gpu-top-smi";
    else if (page == G->page[GPU_TOP_PAGE_DRM]) a = "gpu-top-drm-info";
    else if (page == G->page[GPU_TOP_PAGE_PPTABLE]) a = "gpu-top-pp-table";
    else if (page == G->page[GPU_TOP_PAGE_NPU]) a = "gpu-top-npu";
    gpu_top_open_guide(GTK_WINDOW(G->win), a);
}

/* a Guide button at the right of the tab row, like the Guide button of SRS's item tabs; it opens the section of the tab shown */
static void add_guide_btn(GtkWidget *nb)
{
    GtkWidget *b = gtk_button_new(), *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(hb), gtk_image_new_from_icon_name("help-browser-symbolic", GTK_ICON_SIZE_BUTTON), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), gtk_label_new("Guide"), FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(b), hb);
    gtk_widget_set_tooltip_text(b, "Open the user guide at this tab (and on the Performance tab, at its page shown now) (F1)");
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_end(b, 6);
    g_signal_connect(b, "clicked", G_CALLBACK(on_guide), NULL);
    gtk_widget_show_all(b);
    gtk_notebook_set_action_widget(GTK_NOTEBOOK(nb), b, GTK_PACK_END);
    G->guide = b;
}

/* the top row's card controls belong to the GPU tabs; the Performance tab has its own */
static void on_switch_page(GtkNotebook *nb, GtkWidget *page, guint num, gpointer d)
{
    GtkWidget *ctl[9];
    int i, gpu;
    (void)nb; (void)num; (void)d;
    if (!G) return;   /* the window is being destroyed */
    gpu = G->ndev && page != G->page[GPU_TOP_PAGE_PERFORMANCE];
    ctl[0] = G->devlbl; ctl[1] = G->combo; ctl[2] = G->infobtn; ctl[3] = G->pause; ctl[4] = G->pcbtn; ctl[5] = G->status;
    ctl[6] = G->updlbl; ctl[7] = G->upd; ctl[8] = G->setbtn;   /* the Performance tab has its own Settings button */
    for (i = 0; i < 9; i++) if (ctl[i]) gtk_widget_set_visible(ctl[i], gpu);
}

static void on_combo(GtkComboBox *c, gpointer d) { (void)d; if (!G->building) select_dev(gtk_combo_box_get_active(c)); }

static void on_pc_toggle(GtkToggleButton *b, gpointer d)
{
    (void)d;
    G->no_pc = !gtk_toggle_button_get_active(b);
    gtk_widget_set_visible(G->x_grbm, !G->no_pc);
    gtk_widget_set_visible(G->x_grbm2, !G->no_pc);
    if (gtk_widget_get_mapped(G->win)) { stop_all(); start_all(); }   /* the cards are opened again with the new setting */
}

static void on_info_toggle(GtkToggleButton *b, gpointer d)
{
    (void)d;
    if (G->paned) gtk_widget_set_visible(gtk_paned_get_child1(GTK_PANED(G->paned)), gtk_toggle_button_get_active(b));
}

static void on_full_list(GtkToggleButton *b, gpointer d)
{
    GtkScrolledWindow *sc = GTK_SCROLLED_WINDOW(G->fd_sc);
    int full = gtk_toggle_button_get_active(b);
    (void)d;
    gtk_scrolled_window_set_min_content_height(sc, -1);   /* min before max, so min <= max all the way */
    gtk_scrolled_window_set_max_content_height(sc, full ? -1 : 208);
    gtk_scrolled_window_set_propagate_natural_height(sc, full);
    if (!full) gtk_scrolled_window_set_min_content_height(sc, 208);
}

static gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer d)
{
    int ctrl = (ev->state & GDK_CONTROL_MASK) != 0;
    guint k = gdk_keyval_to_lower(ev->keyval);
    (void)d;
    if (k == GDK_KEY_Escape || (ctrl && k == GDK_KEY_w)) { gtk_widget_destroy(w); return TRUE; }
    if (ev->keyval == GDK_KEY_F1) { on_guide(NULL, NULL); return TRUE; }
    if (ctrl && k == GDK_KEY_p) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(G->pause), !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(G->pause))); return TRUE; }
    if (ctrl && k == GDK_KEY_i) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(G->infobtn), !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(G->infobtn))); return TRUE; }
    if (ctrl && k >= GDK_KEY_1 && k <= GDK_KEY_9 && (int)(k - GDK_KEY_1) < G->ndev && G->combo) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(G->combo), (int)(k - GDK_KEY_1));
        return TRUE;
    }
    return FALSE;
}

static void on_destroy(GtkWidget *w, gpointer d)
{
    int i;
    (void)w; (void)d;
    stop_all();
    perf_prefs_unlisten(on_prefs, NULL);
    for (i = 0; i < G->ndev; i++) { g_free(G->cur[i]); g_free(G->h[i]); }
    g_free(G->xdna);
    g_free(G);
    G = NULL;
}

/* ---- tables ---- */
static void cell_pct(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
    int v;
    char b[16];
    (void)c;
    gtk_tree_model_get(m, it, GPOINTER_TO_INT(d), &v, -1);
    if (v == GT_NA) b[0] = 0; else snprintf(b, sizeof b, "%d %%", v);
    g_object_set(r, "text", b, NULL);
}

static void cell_mib(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
    gint64 v;
    char b[32];
    (void)c;
    gtk_tree_model_get(m, it, GPOINTER_TO_INT(d), &v, -1);
    if (v == GT_NA) b[0] = 0; else snprintf(b, sizeof b, "%lld MiB", (long long)(v >> 10));
    g_object_set(r, "text", b, NULL);
}

static void cell_kfd(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
    int v;
    (void)c; (void)d;
    gtk_tree_model_get(m, it, P_KFD, &v, -1);
    g_object_set(r, "text", v > 0 ? "Y" : "", NULL);
}

static void cell_int(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
    int v;
    char b[16];
    (void)c;
    gtk_tree_model_get(m, it, GPOINTER_TO_INT(d), &v, -1);
    snprintf(b, sizeof b, "%d", v);
    g_object_set(r, "text", b, NULL);
}

static GtkTreeViewColumn *add_col(GtkWidget *v, const char *title, int col, int right, GtkTreeCellDataFunc f, const char *tip)
{
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c;
    GtkWidget *h = gtk_label_new(title);
    if (right) g_object_set(r, "xalign", 1.0, NULL);
    g_object_set(r, "ypad", 0, NULL);
    if (f) {
        c = gtk_tree_view_column_new();
        gtk_tree_view_column_pack_start(c, r, TRUE);
        gtk_tree_view_column_set_cell_data_func(c, r, f, GINT_TO_POINTER(col), NULL);
    } else c = gtk_tree_view_column_new_with_attributes(title, r, "text", col, NULL);
    gtk_tree_view_column_set_widget(c, h);
    gtk_widget_show(h);
    if (tip) gtk_widget_set_tooltip_text(h, tip);
    if (right) gtk_tree_view_column_set_alignment(c, 1.0);
    gtk_tree_view_column_set_sort_column_id(c, col);
    gtk_tree_view_column_set_resizable(c, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(v), c);
    return c;
}

static GtkWidget *proc_view(GtkListStore *st, int with_gpu, GtkTreeViewColumn **cols)
{
    GtkWidget *v = gtk_tree_view_new_with_model(GTK_TREE_MODEL(st));
    GtkTreeViewColumn *c;
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(v), GTK_TREE_VIEW_GRID_LINES_VERTICAL);
    gtk_style_context_add_class(gtk_widget_get_style_context(v), "gt-table");
    c = add_col(v, "GPU", P_GPU, 1, cell_int, "The card's number (#) in the SMI table");
    gtk_tree_view_column_set_visible(c, with_gpu);
    c = add_col(v, "Name", P_NAME, 0, NULL, "Program name");
    gtk_tree_view_column_set_expand(c, TRUE);
    gtk_tree_view_column_set_min_width(c, 150);
    if (cols) cols[P_NAME] = c;
    c = add_col(v, "PID", P_PID, 1, cell_int, "Process ID");
    c = add_col(v, "KFD", P_KFD, 0, cell_kfd, "Y = the program uses ROCm (it has /dev/kfd open)");
    if (cols) cols[P_KFD] = c;
    c = add_col(v, "VRAM", P_VRAM, 1, cell_mib, "Video memory the program uses");
    if (cols) cols[P_VRAM] = c;
    c = add_col(v, "GTT", P_GTT, 1, cell_mib, "System memory the program has mapped for the GPU (GTT)");
    if (cols) cols[P_GTT] = c;
    c = add_col(v, "CPU", P_CPU, 1, cell_pct, "CPU time of the program, % of one CPU");
    if (cols) cols[P_CPU] = c;
    c = add_col(v, "GFX", P_GFX, 1, cell_pct, "Graphics engine busy with this program");
    if (cols) cols[P_GFX] = c;
    c = add_col(v, "Compute", P_COMP, 1, cell_pct, "Compute queues busy with this program");
    if (cols) cols[P_COMP] = c;
    c = add_col(v, "DMA", P_DMA, 1, cell_pct, "Copy (SDMA) engine busy with this program");
    if (cols) cols[P_DMA] = c;
    c = add_col(v, "VCN Unified", P_VCNU, 1, cell_pct, "Video engine (decode and encode in one) busy with this program");
    if (cols) cols[P_VCNU] = c;
    c = add_col(v, "Decode", P_DEC, 1, cell_pct, "Video decoder busy with this program");
    if (cols) cols[P_DEC] = c;
    c = add_col(v, "Encode", P_ENC, 1, cell_pct, "Video encoder busy with this program");
    if (cols) cols[P_ENC] = c;
    c = add_col(v, "VPE", P_VPE, 1, cell_pct, "Video Processing Engine busy with this program");
    if (cols) cols[P_VPE] = c;
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(st), P_VRAM, GTK_SORT_DESCENDING);
    return v;
}

static GtkWidget *text_tab(GtkWidget **view)
{
    GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL), *v = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(v), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(v), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(v), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(v), 6);
    gtk_container_add(GTK_CONTAINER(sc), v);
    *view = v;
    return sc;
}

static void load_css(void)
{
    static GtkCssProvider *c;
    if (c) return;
    c = gtk_css_provider_new();
    gtk_css_provider_load_from_data(c,
        ".gputop .gt-h { font-weight: bold; }"
        ".gputop .gt-sub { font-weight: bold; }"
        ".gputop .gt-num, .gputop .gt-mono, .gputop progressbar text { font-family: monospace; }"
        ".gputop .gt-bar { padding: 4px 6px; border-bottom: 1px solid @borders; }"
        ".gputop .gt-bar button { padding: 1px 8px; min-height: 0; }"
        ".gputop .gt-bar combobox button { padding: 0 6px; }"
        ".gputop .gt-status { font-size: smaller; }"
        ".gputop progressbar trough, .gputop progressbar progress { min-height: 14px; }"
        ".gputop expander title { padding: 1px 0; }"
        ".gputop treeview.gt-table { font-family: monospace; }"
        ".gputop treeview.gt-smi { font-size: 90%; }"
        ".gputop flowboxchild { padding: 0; }", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(c), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static GtkWidget *flow(void)
{
    GtkWidget *f = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(f), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(f), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(f), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(f), 2);   /* two per row, as amdgpu_top's grids */
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(f), 16);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(f), 8);
    gtk_widget_set_halign(f, GTK_ALIGN_START);
    return f;
}

static void build_right(void)
{
    GtkWidget *r, *body, *g, *x;
    plot *p;
    int i, row;
    static const char *vname[3] = { "VRAM", "CPU-Visible VRAM", "GTT" };
    /* a line for "Reading the card..." or why it cannot be read; the panels show from the first sample on */
    G->msg = lbl("", 0);
    gtk_label_set_line_wrap(GTK_LABEL(G->msg), TRUE);
    gtk_box_pack_start(GTK_BOX(G->right), G->msg, FALSE, FALSE, 4);
    r = G->panels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(G->right), r, FALSE, FALSE, 0);
    /* GRBM, GRBM2 */
    G->x_grbm = panel(r, "GRBM", 1, &body);
    G->grbm_na = lbl("", 0);
    gtk_box_pack_start(GTK_BOX(body), G->grbm_na, FALSE, FALSE, 0);
    G->grbm_box = flow();
    build_pc(G->grbm_box, G->grbm_lbl, 0);
    gtk_box_pack_start(GTK_BOX(body), G->grbm_box, FALSE, FALSE, 0);
    G->x_grbm2 = panel(r, "GRBM2", 1, &body);
    G->grbm2_na = lbl("", 0);
    gtk_box_pack_start(GTK_BOX(body), G->grbm2_na, FALSE, FALSE, 0);
    G->grbm2_box = flow();
    build_pc(G->grbm2_box, G->grbm2_lbl, 1);
    gtk_box_pack_start(GTK_BOX(body), G->grbm2_box, FALSE, FALSE, 0);
    gtk_widget_set_tooltip_text(G->x_grbm, "Graphics Register Bus Management: how often each block of the graphics engine was busy "
                                           "(100 reads of the GPU's performance counters per second)");
    gtk_widget_set_tooltip_text(G->x_grbm2, "More GRBM blocks: caches, command processors, DMA");
    /* VRAM */
    G->x_vclk = NULL;
    panel(r, "VRAM", 1, &body);
    panel(body, "VRAM Plot", 1, &x);
    G->vram_plot = G->nplots;
    p = new_plot(600, 150, 0, 1, 1, 0, 1024, 1, "MiB");
    plot_add(p, HOFF(vram), "VRAM");
    plot_add(p, HOFF(gtt), "GTT");
    gtk_widget_set_hexpand(p->da, TRUE);
    gtk_box_pack_start(GTK_BOX(x), p->da, FALSE, FALSE, 0);
    g = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g), 12);
    gtk_grid_set_row_spacing(GTK_GRID(g), 3);
    for (i = 0; i < 3; i++) {
        G->vram_lbl[i] = lbl(vname[i], 0);
        G->vram_bar[i] = gtk_progress_bar_new();
        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(G->vram_bar[i]), TRUE);
        gtk_widget_set_size_request(G->vram_bar[i], 380, -1);
        gtk_widget_set_valign(G->vram_bar[i], GTK_ALIGN_CENTER);
        gtk_grid_attach(GTK_GRID(g), G->vram_lbl[i], 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(g), G->vram_bar[i], 1, i, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(body), g, FALSE, FALSE, 0);
    /* Activity */
    panel(r, "Activity", 1, &body);
    G->act_lbl = lbl("", 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(G->act_lbl), "gt-num");
    gtk_box_pack_start(GTK_BOX(body), G->act_lbl, FALSE, FALSE, 0);
    p = new_plot(600, 150, 0, 1, 1, 0, 100, 0, "%");
    plot_add(p, HOFF(gfx), "GFX");
    plot_add(p, HOFF(umc), "Memory");
    plot_add(p, HOFF(media), "Media");
    gtk_box_pack_start(GTK_BOX(body), p->da, FALSE, FALSE, 0);
    /* Processes (fdinfo) */
    panel(r, "Processes (fdinfo)", 1, &body);
    panel(body, "fdinfo Plot", 1, &x);
    p = new_plot(600, 150, 0, 1, 1, 0, 100, 0, "%");
    plot_add(p, HOFF(fd_gfx), "GFX");
    plot_add(p, HOFF(fd_compute), "Compute");
    plot_add(p, HOFF(fd_dma), "DMA");
    plot_add(p, HOFF(fd_dec), "Decode");
    plot_add(p, HOFF(fd_enc), "Encode");
    plot_add(p, HOFF(fd_vcnu), "VCN Unified");
    plot_add(p, HOFF(fd_vpe), "VPE");
    gtk_box_pack_start(GTK_BOX(x), p->da, FALSE, FALSE, 0);
    G->fd_full = gtk_check_button_new_with_mnemonic("Full _List");
    gtk_widget_set_tooltip_text(G->fd_full, "Show every process without a scroll bar");
    g_signal_connect(G->fd_full, "toggled", G_CALLBACK(on_full_list), NULL);
    gtk_widget_set_halign(G->fd_full, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(body), G->fd_full, FALSE, FALSE, 0);
    G->fd_store = gtk_list_store_new(P_N, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT64, G_TYPE_INT64, G_TYPE_INT, G_TYPE_INT,
                                     G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);
    G->fd_view = proc_view(G->fd_store, 0, G->fd_cols);
    G->fd_sc = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(G->fd_sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(G->fd_sc), 208);   /* amdgpu_top: FDINFO_LIST_HEIGHT */
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(G->fd_sc), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(G->fd_sc), G->fd_view);
    gtk_box_pack_start(GTK_BOX(body), G->fd_sc, FALSE, FALSE, 0);
    /* Sensors */
    G->x_sens = panel(r, "Sensors", 1, &body);
    G->sens_flow = flow();
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(G->sens_flow), 2);
    for (i = 0; i < NSENS + NTEMP; i++) {
        static const size_t off[NSENS + NTEMP] = { HOFF(sclk), HOFF(mclk), HOFF(fclk), HOFF(vddgfx), HOFF(pavg), HOFF(pin), HOFF(fan),
                                                    HOFF(edge), HOFF(junction), HOFF(memt), HOFF(tctl) };
        GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
        G->sens_plot[i] = G->nplots;
        p = new_plot(340, 64, 1, 0, 0, 0, 100, i >= NSENS, "");
        plot_add(p, off[i], "");
        G->sens_lbl[i] = lbl("", 0);
        gtk_style_context_add_class(gtk_widget_get_style_context(G->sens_lbl[i]), "gt-num");
        gtk_box_pack_start(GTK_BOX(v), G->sens_lbl[i], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(v), p->da, FALSE, FALSE, 0);
        G->sens_cell[i] = v;
        gtk_flow_box_insert(GTK_FLOW_BOX(G->sens_flow), v, -1);
    }
    gtk_box_pack_start(GTK_BOX(body), G->sens_flow, FALSE, FALSE, 0);
    G->link_lbl = lbl("", 0);
    G->pstate_lbl = lbl("", 0);
    G->profile_lbl = lbl("", 0);
    G->thr_lbl = lbl("", 0);
    G->dpm_lbl = mono_text("");
    gtk_widget_set_tooltip_text(G->dpm_lbl, "Clock levels (DPM) of the card in MHz; * = the level in use now");
    gtk_box_pack_start(GTK_BOX(body), G->link_lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(body), G->pstate_lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(body), G->profile_lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(body), G->thr_lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(body), G->dpm_lbl, FALSE, FALSE, 4);
    /* PCIe Bandwidth */
    G->x_pcie = panel(r, "PCIe Bandwidth", 1, &body);
    p = new_plot(600, 150, 0, 1, 1, 0, 100, 1, "MiB/s");
    plot_add(p, HOFF(tx), "Sent");
    plot_add(p, HOFF(rx), "Received");
    gtk_box_pack_start(GTK_BOX(body), p->da, FALSE, FALSE, 0);
    G->pcie_lbl = lbl("", 0);
    gtk_box_pack_start(GTK_BOX(body), G->pcie_lbl, FALSE, FALSE, 0);
    /* ECC Memory Error Count */
    G->x_ecc = panel(r, "ECC Memory Error Count", 1, &body);
    G->ecc_lbl = lbl("", 0);
    gtk_box_pack_start(GTK_BOX(body), G->ecc_lbl, FALSE, FALSE, 0);
    /* GPU Metrics */
    G->x_metrics = panel(r, "GPU Metrics", 1, &body);
    G->metrics_na = lbl("", 0);
    gtk_box_pack_start(GTK_BOX(body), G->metrics_na, FALSE, FALSE, 0);
    G->metrics_grid = kv_grid();
    row = 0;
    for (i = 0; i < GT_M_COUNT; i++) {
        G->m_name[i] = lbl(gt_metric_name(i), 0);
        gtk_style_context_add_class(gtk_widget_get_style_context(G->m_name[i]), "dim-label");
        G->m_val[i] = lbl("", 1);
        gtk_style_context_add_class(gtk_widget_get_style_context(G->m_val[i]), "gt-num");
        gtk_grid_attach(GTK_GRID(G->metrics_grid), G->m_name[i], 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(G->metrics_grid), G->m_val[i], 1, row, 1, 1);
        G->m_unit[i] = lbl(unit_disp(gt_metric_unit(i)), 0);
        gtk_style_context_add_class(gtk_widget_get_style_context(G->m_unit[i]), "dim-label");
        gtk_grid_attach(GTK_GRID(G->metrics_grid), G->m_unit[i], 2, row, 1, 1);
        row++;
    }
    G->metrics_agrid = kv_grid();   /* the per-core arrays: their own columns, so the numbers above stay narrow */
    gtk_widget_set_margin_top(G->metrics_agrid, 4);
    for (i = 0; i < GT_MA_COUNT; i++) {
        G->ma_name[i] = lbl(gt_metric_array_name(i), 0);
        gtk_style_context_add_class(gtk_widget_get_style_context(G->ma_name[i]), "dim-label");
        G->ma_val[i] = lbl("", 0);
        gtk_label_set_line_wrap(GTK_LABEL(G->ma_val[i]), TRUE);   /* 16 cores: wrap rather than widen the panel */
        gtk_label_set_max_width_chars(GTK_LABEL(G->ma_val[i]), 64);
        gtk_style_context_add_class(gtk_widget_get_style_context(G->ma_val[i]), "gt-num");
        gtk_grid_attach(GTK_GRID(G->metrics_agrid), G->ma_name[i], 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(G->metrics_agrid), G->ma_val[i], 1, i, 1, 1);
    }
    gtk_widget_set_no_show_all(G->metrics_grid, FALSE);
    gtk_box_pack_start(GTK_BOX(body), G->metrics_grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(body), G->metrics_agrid, FALSE, FALSE, 0);
    G->x_vclk = panel(body, "VCLK/DCLK Plot", 1, &x);
    p = new_plot(600, 150, 0, 1, 1, 0, 100, 1, "MHz");
    plot_add(p, HOFF(avclk), "VCLK (Avg.)");
    plot_add(p, HOFF(adclk), "DCLK (Avg.)");
    plot_add(p, HOFF(avclk1), "VCLK1 (Avg.)");
    plot_add(p, HOFF(adclk1), "DCLK1 (Avg.)");
    plot_add(p, HOFF(cvclk), "VCLK (Cur.)");
    plot_add(p, HOFF(cdclk), "DCLK (Cur.)");
    plot_add(p, HOFF(cvclk1), "VCLK1 (Cur.)");
    plot_add(p, HOFF(cdclk1), "DCLK1 (Cur.)");
    gtk_box_pack_start(GTK_BOX(x), p->da, FALSE, FALSE, 0);
    /* Throttling Log (closed, as in amdgpu_top) */
    panel(r, "Throttling Log", 0, &body);
    G->thrlog_lbl = mono_text("");
    gtk_box_pack_start(GTK_BOX(body), G->thrlog_lbl, FALSE, FALSE, 0);
}

static void build_smi(GtkWidget *nb)
{
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4), *sc, *v;
    static const struct { const char *t; int right; const char *tip; } c[S_N] = {
        { "#", 1, "Card number (Ctrl+1 ... Ctrl+9 picks it on the GPU tab)" }, { "Name", 0, NULL }, { "Target", 0, "gfx target (ISA)" },
        { "PCI Bus", 0, NULL }, { "VRAM Usage", 1, NULL }, { "GTT (RAM) Usage", 1, NULL }, { "SCLK", 1, "Graphics clock" },
        { "MCLK", 1, "Memory clock" }, { "VDDGFX", 1, "Graphics core voltage" }, { "Power", 1, "Power draw / power cap" },
        { "GFX", 1, "Graphics engine activity" }, { "UMC", 1, "Memory controller activity" }, { "Media", 1, "Video engine activity" },
        { "GPU/MEM Temp", 1, "Junction (else edge) / memory temperature" }, { "Fan", 1, NULL },
        { "Throttle Status", 0, "Throttle reasons that are on now (gpu_metrics or NVML)" } };
    int i;
    G->smi_store = gtk_list_store_new(S_N, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                      G_TYPE_STRING, G_TYPE_STRING);
    v = gtk_tree_view_new_with_model(GTK_TREE_MODEL(G->smi_store));
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(v), GTK_TREE_VIEW_GRID_LINES_VERTICAL);
    gtk_style_context_add_class(gtk_widget_get_style_context(v), "gt-table");
    gtk_style_context_add_class(gtk_widget_get_style_context(v), "gt-smi");
    for (i = 0; i < S_N; i++) {
        GtkTreeViewColumn *col = add_col(v, c[i].t, i, c[i].right, i == S_IDX ? cell_int : NULL, c[i].tip);
        gtk_tree_view_column_set_sort_column_id(col, -1);   /* the SMI table keeps the card order */
    }
    G->smi_view = v;
    gtk_widget_set_margin_start(vb, 6);
    gtk_widget_set_margin_end(vb, 6);
    gtk_widget_set_margin_top(vb, 6);
    sc = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sc), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sc), v);
    gtk_box_pack_start(GTK_BOX(vb), sc, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), lbl_cls("Processes", "gt-h"), FALSE, FALSE, 6);
    G->smi_proc_store = gtk_list_store_new(P_N, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT64, G_TYPE_INT64, G_TYPE_INT, G_TYPE_INT,
                                           G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);
    G->smi_proc_view = proc_view(G->smi_proc_store, 1, NULL);
    sc = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sc), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sc), G->smi_proc_view);
    gtk_box_pack_start(GTK_BOX(vb), sc, TRUE, TRUE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), vb, gtk_label_new_with_mnemonic("_SMI"));
    G->page[GPU_TOP_PAGE_SMI] = vb;
}

void gpu_top_window_open(GtkWindow *parent)
{
    GtkWidget *vb, *bar, *sc, *nb, *w, *msg;
    int i;
    if (G) { gtk_window_present(GTK_WINDOW(G->win)); return; }
    load_css();
    G = g_new0(gtw, 1);
    G->ndev = gt_device_list(G->dev, GT_MAX_DEV);
    if (G->ndev < 0) G->ndev = 0;
    G->no_pc = 1;   /* 2.5.0: GRBM counters off by default (opt-in); two KWin GPU resets on 10-31 happened with the window open */
    G->interval_ms = perf_prefs_gpu_top_ms();
    perf_prefs_listen(on_prefs, NULL);
    G->t0 = g_get_monotonic_time();
    G->xdna = g_malloc0(TXT_LEN);
    G->xdna_ok = gt_xdna_text(G->xdna, TXT_LEN) == 0;
    for (i = 0; i < G->ndev; i++) { G->cur[i] = g_new0(gt_stat, 1); G->h[i] = g_new0(hist, 1); }
    for (i = 0; i < G->ndev && !G->dev[i].active; i++) ;   /* start on the first awake card: opening a runtime-suspended one */
    G->sel = i < G->ndev ? i : 0;                            /* would wake it (a laptop's dGPU often sorts first) */
    w = G->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(w), "SRS - The Big Top");
    gtk_window_set_icon_name(GTK_WINDOW(w), "srs");
    gtk_window_set_default_size(GTK_WINDOW(w), 1280, 860);
    if (parent) gtk_window_set_transient_for(GTK_WINDOW(w), NULL);   /* its own window, not kept above the main one */
    gtk_style_context_add_class(gtk_widget_get_style_context(w), "gputop");
    gtk_style_context_add_class(gtk_widget_get_style_context(w), "srs-main");
    vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(w), vb);

    /* top row: device, Info, Pause, performance counters; like amdgpu_top's menu bar */
    bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(bar), "gt-bar");
    if (G->ndev > 1) {
        GtkWidget *l = G->devlbl = gtk_label_new_with_mnemonic("_Device:");
        G->combo = gtk_combo_box_text_new();
        G->building = 1;
        for (i = 0; i < G->ndev; i++) {
            char s[220];
            snprintf(s, sizeof s, "#%d %s (%s)%s", i, G->dev[i].name, G->dev[i].pci, G->dev[i].active ? "" : " - Suspended");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G->combo), s);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(G->combo), G->sel);
        G->building = 0;
        gtk_label_set_mnemonic_widget(GTK_LABEL(l), G->combo);
        gtk_widget_set_tooltip_text(G->combo, "The graphics card to show (Ctrl+1 ... Ctrl+9). A suspended card is woken up only when you pick it.");
        g_signal_connect(G->combo, "changed", G_CALLBACK(on_combo), NULL);
        gtk_box_pack_start(GTK_BOX(bar), l, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bar), G->combo, FALSE, FALSE, 0);
    } else if (G->ndev == 1) {
        char s[220];
        snprintf(s, sizeof s, "%s (%s)", G->dev[0].name, G->dev[0].pci);
        G->devlbl = lbl_cls(s, "gt-h");
        gtk_box_pack_start(GTK_BOX(bar), G->devlbl, FALSE, FALSE, 0);
    }
    G->infobtn = gtk_toggle_button_new_with_mnemonic("_Info");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(G->infobtn), TRUE);
    gtk_widget_set_tooltip_text(G->infobtn, "Show or hide the side panel with the card's details (Ctrl+I)");
    g_signal_connect(G->infobtn, "toggled", G_CALLBACK(on_info_toggle), NULL);
    gtk_box_pack_start(GTK_BOX(bar), G->infobtn, FALSE, FALSE, 0);
    G->pause = gtk_toggle_button_new_with_mnemonic("P_ause");
    gtk_widget_set_tooltip_text(G->pause, "Freeze the numbers and graphs; the cards are still read in the background (Ctrl+P)");
    gtk_box_pack_start(GTK_BOX(bar), G->pause, FALSE, FALSE, 0);
    G->pcbtn = gtk_check_button_new_with_mnemonic("Performance _Counters (GRBM)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(G->pcbtn), FALSE);
    gtk_widget_set_tooltip_text(G->pcbtn, "Read the GPU's GRBM / GRBM2 performance counters (100 register reads a second) for the GRBM panels. "
                                          "Reading them can keep the GPU from dropping into its power-saving states, so it may draw a little "
                                          "more power while this window is open. Off by default (amdgpu_top --no-pc); turn it on only when you need the GRBM panels. AMD cards only.");
    g_signal_connect(G->pcbtn, "toggled", G_CALLBACK(on_pc_toggle), NULL);
    gtk_box_pack_start(GTK_BOX(bar), G->pcbtn, FALSE, FALSE, 0);
    {   /* how often the cards are read: the same choices as Task Manager's Update Speed */
        int i;
        G->updlbl = gtk_label_new_with_mnemonic("_Update:");
        G->upd = gtk_combo_box_text_new();
        for (i = 0; i < PT_SPEED_CHOICES; i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(G->upd), perf_speed_choice_label(i));
        G->building = 1;
        gtk_combo_box_set_active(GTK_COMBO_BOX(G->upd), perf_speed_choice_of_ms(G->interval_ms));
        G->building = 0;
        gtk_label_set_mnemonic_widget(GTK_LABEL(G->updlbl), G->upd);
        gtk_widget_set_tooltip_text(G->upd, "How often The Big Top reads the cards for the GPU and SMI tabs; the graphs always show the last 30 s. "
                                            "1 s is amdgpu_top's default. Faster costs a little more CPU time. The Performance tab and the "
                                            "tray meters have their own Update Speed (Performance > Summary > Settings), with the same choices.");
        g_signal_connect(G->upd, "changed", G_CALLBACK(on_update), NULL);
        gtk_box_pack_start(GTK_BOX(bar), G->updlbl, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bar), G->upd, FALSE, FALSE, 0);
        G->setbtn = gtk_button_new_with_mnemonic("S_ettings...");
        gtk_widget_set_tooltip_text(G->setbtn, "Every live-update setting on one page: The Big Top's update speed, the Performance tab's "
                                               "update speed, start page and CPU units, and the tray meters (the same page as "
                                               "Performance > Summary > Settings)");
        g_signal_connect(G->setbtn, "clicked", G_CALLBACK(on_settings), NULL);
        gtk_box_pack_start(GTK_BOX(bar), G->setbtn, FALSE, FALSE, 0);
    }
    G->status = lbl_cls("", "dim-label");
    gtk_style_context_add_class(gtk_widget_get_style_context(G->status), "gt-status");
    gtk_label_set_ellipsize(GTK_LABEL(G->status), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(G->status), 1);
    gtk_box_pack_end(GTK_BOX(bar), G->status, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vb), bar, FALSE, FALSE, 0);

    if (!G->ndev) {   /* no GPU: the Performance tab still works; the GPU tab says why it is empty */
        GtkWidget *pv = perf_view_new();
        nb = G->nb = gtk_notebook_new();
        gtk_notebook_set_show_border(GTK_NOTEBOOK(nb), FALSE);
        add_guide_btn(nb);
        gtk_box_pack_start(GTK_BOX(vb), nb, TRUE, TRUE, 0);
        gtk_notebook_append_page(GTK_NOTEBOOK(nb), pv, gtk_label_new_with_mnemonic("_Performance"));
        G->page[GPU_TOP_PAGE_PERFORMANCE] = pv;
        msg = lbl("No graphics card was found (no /sys/class/drm card with a PCI device).", 0.5);
        gtk_notebook_append_page(GTK_NOTEBOOK(nb), msg, gtk_label_new_with_mnemonic("_GPU"));
        G->page[GPU_TOP_PAGE_GPU] = msg;
        g_signal_connect(nb, "switch-page", G_CALLBACK(on_switch_page), NULL);
        g_signal_connect(w, "destroy", G_CALLBACK(on_destroy), NULL);
        g_signal_connect(w, "key-press-event", G_CALLBACK(on_key), NULL);
        gtk_widget_show_all(w);
        on_switch_page(GTK_NOTEBOOK(nb), pv, 0, NULL);
        gtk_widget_hide(G->pause);   /* nothing to pause or count without a card */
        gtk_widget_hide(G->pcbtn);
        gtk_widget_hide(G->infobtn);
        return;
    }

    nb = G->nb = gtk_notebook_new();
    gtk_notebook_set_show_border(GTK_NOTEBOOK(nb), FALSE);
    add_guide_btn(nb);
    gtk_box_pack_start(GTK_BOX(vb), nb, TRUE, TRUE, 0);
    /* GPU tab: side panel | central panel */
    G->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    G->left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    G->right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(G->left), 8);
    gtk_container_set_border_width(GTK_CONTAINER(G->right), 8);
    sc = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sc), G->left);
    gtk_widget_set_size_request(sc, 440, -1);
    gtk_paned_pack1(GTK_PANED(G->paned), sc, FALSE, FALSE);
    sc = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sc), G->right);
    gtk_paned_pack2(GTK_PANED(G->paned), sc, TRUE, FALSE);
    gtk_paned_set_position(GTK_PANED(G->paned), 520);   /* Device Info values mostly on one line */
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), G->paned, gtk_label_new_with_mnemonic("_GPU"));
    G->page[GPU_TOP_PAGE_GPU] = G->paned;
    build_right();
    build_smi(nb);
    G->page[GPU_TOP_PAGE_DRM] = text_tab(&G->drm_view);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), G->page[GPU_TOP_PAGE_DRM], gtk_label_new_with_mnemonic("D_RM Info"));
    G->page[GPU_TOP_PAGE_PPTABLE] = text_tab(&G->pp_view);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), G->page[GPU_TOP_PAGE_PPTABLE], gtk_label_new_with_mnemonic("Po_werPlay Table"));
    G->page[GPU_TOP_PAGE_NPU] = text_tab(&G->xdna_view);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), G->page[GPU_TOP_PAGE_NPU], gtk_label_new_with_mnemonic("_NPU (XDNA)"));
    text_set(G->xdna_view, *G->xdna ? G->xdna : "No XDNA NPU (Ryzen AI) was found.");

    g_signal_connect(w, "map", G_CALLBACK(on_map), NULL);
    g_signal_connect(w, "unmap", G_CALLBACK(on_unmap), NULL);
    g_signal_connect(w, "destroy", G_CALLBACK(on_destroy), NULL);
    g_signal_connect(w, "key-press-event", G_CALLBACK(on_key), NULL);
    {   /* the Performance tab (perf_view.c, the whole PC like a task manager) is the first tab and the one shown at open */
        GtkWidget *pv = perf_view_new();
        gtk_notebook_insert_page(GTK_NOTEBOOK(nb), pv, gtk_label_new_with_mnemonic("_Performance"), 0);
        G->page[GPU_TOP_PAGE_PERFORMANCE] = pv;
    }
    g_signal_connect(nb, "switch-page", G_CALLBACK(on_switch_page), NULL);
    gtk_widget_show_all(w);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(nb), 0);
    on_switch_page(GTK_NOTEBOOK(nb), gtk_notebook_get_nth_page(GTK_NOTEBOOK(nb), 0), 0, NULL);
    /* panels that only some cards have start hidden until the first sample says so */
    gtk_widget_hide(G->x_pcie);
    gtk_widget_hide(G->grbm_box);
    gtk_widget_hide(G->grbm2_box);
    gtk_widget_hide(G->grbm_na);
    gtk_widget_hide(G->grbm2_na);
    gtk_widget_hide(G->x_ecc);
    gtk_widget_hide(G->metrics_grid);
    gtk_widget_hide(G->metrics_agrid);
    gtk_widget_hide(G->x_vclk);
    for (i = 0; i < 3; i++) { gtk_widget_hide(G->vram_bar[i]); gtk_widget_hide(G->vram_lbl[i]); }
    build_left();
    fill_text_tabs();
    update_sel();
    set_text(G->status, "Reading the cards...");
}

void gpu_top_window_open_page(GtkWindow *parent, int page)
{
    int n;
    gpu_top_window_open(parent);
    if (!G || !G->nb || page < 0 || page > GPU_TOP_PAGE_NPU) return;
    if (!G->page[page]) page = GPU_TOP_PAGE_GPU;   /* no Performance tab in this build: the GPU tab */
    if ((n = gtk_notebook_page_num(GTK_NOTEBOOK(G->nb), G->page[page])) >= 0) gtk_notebook_set_current_page(GTK_NOTEBOOK(G->nb), n);
    gtk_window_present(GTK_WINDOW(G->win));
}

GtkWidget *gpu_top_window_get(void) { return G ? G->win : NULL; }
