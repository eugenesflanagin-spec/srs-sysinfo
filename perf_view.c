/* perf_view.c - the Performance tab of the GPU Top window (see perf_view.h).
 *
 * Layout: a narrow navigation list on the left (Summary, Performance, Processes, Startup Apps, Users,
 * Services) and the pages in a GtkStack. Window chrome keeps the GTK theme's colours; the meters and
 * graphs are dark "instrument" panels with a fixed colour per resource (CPU green, kernel time red,
 * memory magenta, CPU power yellow, thermals orange, disk green, network and GPU cyan), so their text
 * reads the same in light and dark themes. LED-segment bars and history lines are drawn with Cairo;
 * a lit segment / a line gets a wider, faint stroke under it for the glow.
 *
 * Sampling: the shared sampler of perf_tray.c ("hub": one perf_data.c worker for the tray meters and
 * this tab, at the Task Manager Update Speed), subscribed only while the tab is mapped and the window
 * is not minimised. The GUI copies each snapshot and redraws only the visible page.
 * The layout follows the general idea of classic task-manager designs; the code, drawing and
 * wording are SRS's own.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "perf_view.h"
#include "perf_data.h"
#include "perf_admin.h"
#include "perf_tray.h"
#include "perf_procs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define EM "—"

/* ------------------------------------------------------------------ colours and fonts */

typedef struct { double r, g, b; } rgb;
static const rgb C_CPU = { 0.25, 1.00, 0.35 }, C_KER = { 1.00, 0.30, 0.30 }, C_TEMP = { 1.00, 0.62, 0.15 },
                 C_MEM = { 1.00, 0.38, 1.00 }, C_PWR = { 1.00, 0.88, 0.20 }, C_DISK = { 0.30, 0.95, 0.45 },
                 C_NET = { 0.25, 0.85, 1.00 }, C_GPU = { 0.25, 0.85, 1.00 }, C_TXT = { 0.88, 0.91, 0.95 },
                 C_DIM = { 0.62, 0.68, 0.74 }, C_WARN = { 1.00, 0.25, 0.20 };
static const rgb C_SERIES[8] = { { 1.00, 0.62, 0.15 }, { 0.25, 0.85, 1.00 }, { 1.00, 0.38, 1.00 }, { 0.30, 0.95, 0.45 },
                                 { 1.00, 0.88, 0.20 }, { 0.60, 0.55, 1.00 }, { 1.00, 0.45, 0.45 }, { 0.75, 0.90, 0.60 } };

static int g_ncpu = 1;               /* for the Per Core Units display of process CPU */
static PangoFontDescription *F_SMALL, *F_NORM, *F_BOLD, *F_HEAD, *F_BIG;
static PangoAttrList *TNUM;

static void fonts_init(void)
{
    if (F_SMALL) return;
    F_SMALL = pango_font_description_from_string("Sans 7.5");
    F_NORM = pango_font_description_from_string("Sans 8.5");
    F_BOLD = pango_font_description_from_string("Sans Bold 8.5");
    F_HEAD = pango_font_description_from_string("Sans Bold 10.5");
    F_BIG = pango_font_description_from_string("Sans Bold 13");
    TNUM = pango_attr_list_new();
    pango_attr_list_insert(TNUM, pango_attr_font_features_new("tnum"));
}

/* one PangoLayout per drawing area, made once */
static PangoLayout *lay(GtkWidget *w, cairo_t *cr)
{
    PangoLayout *l = g_object_get_data(G_OBJECT(w), "pv-layout");
    if (!l) {
        l = gtk_widget_create_pango_layout(w, NULL);
        pango_layout_set_attributes(l, TNUM);
        g_object_set_data_full(G_OBJECT(w), "pv-layout", l, g_object_unref);
    }
    pango_cairo_update_layout(cr, l);
    return l;
}

/* draws s at x (align 0 left, 0.5 centre, 1 right of x), y = top; returns the width */
static double text(cairo_t *cr, PangoLayout *l, double x, double y, double align, const PangoFontDescription *f,
                   rgb c, double a, const char *s)
{
    int w, h;
    pango_layout_set_font_description(l, f);
    pango_layout_set_text(l, s, -1);
    pango_layout_get_pixel_size(l, &w, &h);
    cairo_set_source_rgba(cr, c.r, c.g, c.b, a);
    cairo_move_to(cr, x - align * w, y);
    pango_cairo_show_layout(cr, l);
    return w;
}

/* like text(), cut with an ellipsis at maxw pixels */
static void text_clip(cairo_t *cr, PangoLayout *l, double x, double y, double maxw, const PangoFontDescription *f, rgb c, const char *s)
{
    pango_layout_set_width(l, (int)(maxw * PANGO_SCALE));
    pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
    text(cr, l, x, y, 0, f, c, 1, s);
    pango_layout_set_width(l, -1);
    pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_NONE);
}

static void rrect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
    cairo_close_path(cr);
}

static void panel(cairo_t *cr, double w, double h, rgb edge)
{
    rrect(cr, 0.5, 0.5, w - 1, h - 1, 5);
    cairo_set_source_rgb(cr, 0.067, 0.082, 0.102);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, edge.r, edge.g, edge.b, 0.35);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
}

static rgb mix(rgb a, rgb b, double t) { rgb r = { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; return r; }

/* ------------------------------------------------------------------ LED-segment bars */

static void led_seg(cairo_t *cr, double x, double y, double w, double h, rgb c, int lit)
{
    if (lit) {
        cairo_set_source_rgba(cr, c.r, c.g, c.b, 0.16);
        rrect(cr, x - 2, y - 2, w + 4, h + 4, 2.5);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, c.r, c.g, c.b, 0.95);
        rrect(cr, x, y, w, h, 1.2);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
        cairo_rectangle(cr, x + 1, y + 1, w - 2, h > 3 ? 1 : 0.5);
        cairo_fill(cr);
    } else {
        cairo_set_source_rgba(cr, c.r, c.g, c.b, 0.11);
        rrect(cr, x, y, w, h, 1.2);
        cairo_fill(cr);
    }
}

/* vertical bar, filled from the bottom; warn = lit segments above 80 % turn towards red */
static void led_v(cairo_t *cr, double x, double y, double w, double h, double frac, rgb c, int warn)
{
    const double seg = 4, gap = 2;
    int n = (int)((h + gap) / (seg + gap)), lit;
    if (n < 1) return;
    if (isnan(frac) || frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    lit = (int)(frac * n + 0.5);
    if (frac > 0.005 && lit == 0) lit = 1;
    for (int i = 0; i < n; i++) {
        double yy = y + h - (i + 1) * (seg + gap) + gap;
        rgb cc = warn && i >= n * 0.8 ? mix(c, C_WARN, (i - n * 0.8) / (n * 0.2) * 0.8 + 0.2) : c;
        led_seg(cr, x, yy, w, seg, cc, i < lit);
    }
}

static void led_h(cairo_t *cr, double x, double y, double w, double h, double frac, rgb c, int warn)
{
    const double seg = 5, gap = 2;
    int n = (int)((w + gap) / (seg + gap)), lit;
    if (n < 1) return;
    if (isnan(frac) || frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    lit = (int)(frac * n + 0.5);
    if (frac > 0.005 && lit == 0) lit = 1;
    for (int i = 0; i < n; i++) {
        rgb cc = warn && i >= n * 0.8 ? mix(c, C_WARN, (i - n * 0.8) / (n * 0.2) * 0.8 + 0.2) : c;
        led_seg(cr, x + i * (seg + gap), y, seg, h, cc, i < lit);
    }
}

/* ------------------------------------------------------------------ history graphs */

typedef struct { const pd_ring *r; rgb c; const char *name; int fill; } gser;
#define GMAX 12
typedef struct {
    int n;
    gser s[GMAX];
    double ymin, ymax;
    int unit;                      /* U_* for axis labels */
    rgb c;                         /* panel / grid colour */
} gspec;
enum { U_PCT, U_C, U_W, U_RATE, U_MHZ };

static double nice_ceil(double v)
{
    double e, m;
    if (!(v > 0)) return 1;
    e = pow(10, floor(log10(v)));
    m = v / e;
    return (m <= 1 ? 1 : m <= 2 ? 2 : m <= 5 ? 5 : 10) * e;
}

static void fmt_axis(double v, int unit, char *o, int n)
{
    switch (unit) {
    case U_PCT: snprintf(o, (size_t)n, "%.0f %%", v); break;
    case U_C: snprintf(o, (size_t)n, "%.0f °C", v); break;
    case U_W: snprintf(o, (size_t)n, "%.0f W", v); break;
    case U_RATE: pd_fmt_rate(v, o, n); break;
    default: snprintf(o, (size_t)n, "%.0f", v); break;
    }
}

static void series_path(cairo_t *cr, const pd_ring *r, double x, double y, double w, double h, double ymin, double ymax, int close)
{
    int started = 0;
    double lastx = x, firstx = x;
    for (int i = 0; i < r->n; i++) {
        double v = pd_ring_at(r, i), xx, yy;
        if (isnan(v)) {
            /* a missing sample ends the line, and the filled area too (no bridge across a pause) */
            if (started && close) { cairo_line_to(cr, lastx, y + h); cairo_line_to(cr, firstx, y + h); cairo_close_path(cr); }
            started = 0;
            continue;
        }
        xx = x + w * (double)(PD_HIST - r->n + i) / (PD_HIST - 1);
        if (v < ymin) v = ymin;
        if (v > ymax) v = ymax;
        yy = y + h - (v - ymin) / (ymax - ymin) * h;
        if (!started) { if (close) { cairo_move_to(cr, xx, y + h); cairo_line_to(cr, xx, yy); firstx = xx; } else cairo_move_to(cr, xx, yy); started = 1; }
        else cairo_line_to(cr, xx, yy);
        lastx = xx;
    }
    if (close && started) { cairo_line_to(cr, lastx, y + h); cairo_line_to(cr, firstx, y + h); cairo_close_path(cr); }
}

/* grid + glowing lines; gx/gy grid divisions (0 = none) */
static void graph(cairo_t *cr, double x, double y, double w, double h, const gspec *g, int gx, int gy, int glow)
{
    rgb gc = g->c;
    double range = g->ymax - g->ymin;
    if (!(range > 0)) range = 1;
    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_set_source_rgba(cr, gc.r, gc.g, gc.b, 0.045);
    cairo_fill(cr);
    cairo_set_line_width(cr, 1);
    cairo_set_source_rgba(cr, gc.r, gc.g, gc.b, 0.16);
    for (int i = 1; i < gx; i++) {
        double xx = floor(x + w * i / gx) + 0.5;
        cairo_move_to(cr, xx, y); cairo_line_to(cr, xx, y + h);
    }
    for (int i = 1; i < gy; i++) {
        double yy = floor(y + h * i / gy) + 0.5;
        cairo_move_to(cr, x, yy); cairo_line_to(cr, x + w, yy);
    }
    cairo_stroke(cr);
    cairo_set_source_rgba(cr, gc.r, gc.g, gc.b, 0.45);
    cairo_rectangle(cr, floor(x) + 0.5, floor(y) + 0.5, floor(w), floor(h));
    cairo_stroke(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (int k = 0; k < g->n; k++) {
        const gser *s = &g->s[k];
        if (!s->r || s->r->n == 0) continue;
        if (s->fill) {
            series_path(cr, s->r, x, y, w, h, g->ymin, g->ymin + range, 1);
            cairo_set_source_rgba(cr, s->c.r, s->c.g, s->c.b, 0.13);
            cairo_fill(cr);
        }
        if (glow) {
            series_path(cr, s->r, x, y, w, h, g->ymin, g->ymin + range, 0);
            cairo_set_source_rgba(cr, s->c.r, s->c.g, s->c.b, 0.10);
            cairo_set_line_width(cr, 7);
            cairo_stroke(cr);
            series_path(cr, s->r, x, y, w, h, g->ymin, g->ymin + range, 0);
            cairo_set_source_rgba(cr, s->c.r, s->c.g, s->c.b, 0.25);
            cairo_set_line_width(cr, 3.5);
            cairo_stroke(cr);
        }
        series_path(cr, s->r, x, y, w, h, g->ymin, g->ymin + range, 0);
        cairo_set_source_rgba(cr, s->c.r, s->c.g, s->c.b, 1);
        cairo_set_line_width(cr, glow ? 1.6 : 1.2);
        cairo_stroke(cr);
    }
    /* a pause (the tab was hidden, nothing sampled): a run of missing samples between real ones gets a shaded
       band and a dashed line, so the graph says "not measured here" instead of drawing a jump */
    if (g->n) {
        const pd_ring *r = g->s[g->n - 1].r;
        int i = 0;
        while (r && i < r->n) {
            int a0, a1;
            while (i < r->n && !isnan(pd_ring_at(r, i))) i++;
            a0 = i;
            while (i < r->n && isnan(pd_ring_at(r, i))) i++;
            a1 = i;
            if (a0 > 0 && a1 < r->n && a1 > a0) {
                double x0 = x + w * (double)(PD_HIST - r->n + a0 - 1) / (PD_HIST - 1);
                double x1 = x + w * (double)(PD_HIST - r->n + a1) / (PD_HIST - 1), xm = floor((x0 + x1) / 2) + 0.5;
                static const double dash[] = { 3, 3 };
                cairo_set_source_rgba(cr, gc.r, gc.g, gc.b, 0.07);
                cairo_rectangle(cr, x0, y, x1 - x0, h);
                cairo_fill(cr);
                cairo_set_dash(cr, dash, 2, 0);
                cairo_set_line_width(cr, 1);
                cairo_set_source_rgba(cr, gc.r, gc.g, gc.b, 0.6);
                cairo_move_to(cr, xm, y);
                cairo_line_to(cr, xm, y + h);
                cairo_stroke(cr);
                cairo_set_dash(cr, NULL, 0, 0);
            }
        }
    }
    cairo_restore(cr);
}

/* ------------------------------------------------------------------ the view */

enum { PG_SUMMARY, PG_PERF, PG_PROCS, PG_STARTUP, PG_USERS, PG_SERVICES, PG_N };
enum { K_CPU, K_MEM, K_PWR, K_THERM, K_DISK, K_NET, K_GPU };

#define MAX_RES (4 + PD_MAX_DISK + PD_MAX_NET + PD_MAX_GPU)
#define MAX_STAT 16

typedef struct pv pv;

typedef struct {
    pv *v;
    int kind, idx;
    GtkWidget *row, *name, *l1, *l2, *spark;
} resrow;

/* processes tree row bookkeeping */
typedef struct {
    GtkTreeIter it;
    unsigned gen;
    int parent;                    /* pid of the row's parent process row, 0 = top of its group */
    int group;                     /* 1 apps, 2 background */
    char state;
    int cpu10, gpu, thr, nice;
    long long mem;
    int rd, wr;                    /* rounded KB/s, -1 */
    unsigned long long start;      /* with the pid, the process's identity (a reused PID is a new row) */
    char name[48], cmd[240];       /* an exec() changes them: the row is made again */
} prow;

enum { PC_NAME, PC_PID, PC_STATUS, PC_USER, PC_CPU, PC_MEM, PC_RD, PC_WR, PC_GPU, PC_THR, PC_NICE, PC_CMD, PC_KIND, PC_N };

struct pv {
    GtkWidget *root, *nav, *stack, *pages[PG_N];
    /* sampling (the shared hub) */
    guint sub, tick;
    int mapped;
    pd_snap v;                     /* the GUI's copy of the latest snapshot (its rings replaced by hist's) */
    pd_snap *hist;                 /* the view's own history rings: they live as long as the window, so the graphs go on
                                      after the tab was hidden (the hub may free its sampler meanwhile) */
    unsigned long long last_seq;   /* the hub sample seen last; a lower one = a new sampler */
    gint64 paused_at;              /* monotonic time the view unsubscribed, 0 = running */
    int page;
    /* summary */
    GtkWidget *s_meters, *s_overview, *s_top, *s_mem, *s_tiles[4], *s_status;
    /* performance */
    GtkWidget *p_list, *p_title, *p_sub, *p_bar, *p_cap1, *p_cap1r, *p_g1, *p_cpugrid, *p_bodystack, *p_cap2, *p_cap2r,
              *p_g2, *p_g2box, *p_bigk[6], *p_bigv[6], *p_smk[MAX_STAT], *p_smv[MAX_STAT];
    GtkWidget **p_cpu;             /* per logical CPU drawing areas */
    int p_ncpu;
    resrow res[MAX_RES];
    int nres;
    char res_sig[512];
    int sel_kind, sel_idx;
    const char *want_res;
    /* processes */
    GtkTreeStore *t_store;
    GtkTreeModel *t_filter, *t_sort;
    GtkWidget *t_view, *t_entry, *t_detail[24], *t_detail_cmd, *t_detail_box, *t_hint;
    GtkTreeIter t_apps, t_bg;
    GHashTable *t_rows;            /* pid -> prow */
    unsigned t_gen;
    int t_sel_pid, t_filled, t_want_pid;
    pp_ui *pp, *pp_top;            /* process actions: Processes page (its selection), Summary (the right-clicked row) */
    int top_pid[64], ntop, top_menu_pid;
    double top_y0, top_rh;
    char t_filter_txt[128];
    /* admin pages */
    GtkWidget *users_page;
};

static void ensure_css(void)
{
    static int done;
    GtkCssProvider *p;
    if (done) return;
    done = 1;
    p = gtk_css_provider_new();
    gtk_css_provider_load_from_data(p,
        ".srs-perf .pv-title { font-size: 16pt; font-weight: bold; }\n"
        ".srs-perf .pv-sub { font-size: 10pt; opacity: 0.75; }\n"
        ".srs-perf .pv-dim { opacity: 0.72; font-size: 8.5pt; }\n"
        ".srs-perf .pv-big { font-size: 13pt; font-feature-settings: \"tnum\"; }\n"
        ".srs-perf .pv-num { font-feature-settings: \"tnum\"; }\n"
        ".srs-perf .pv-rowname { font-weight: bold; }\n"
        ".srs-perf .pv-nav row { padding: 5px 10px; }\n"
        ".srs-perf .pv-res row { padding: 3px 4px; }\n", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(p),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

static GtkWidget *label(const char *t, float xa, const char *cls)
{
    GtkWidget *l = gtk_label_new(t);
    gtk_label_set_xalign(GTK_LABEL(l), xa);
    if (cls) gtk_style_context_add_class(gtk_widget_get_style_context(l), cls);
    return l;
}

static void set_label(GtkWidget *l, const char *t)
{
    if (strcmp(gtk_label_get_text(GTK_LABEL(l)), t)) gtk_label_set_text(GTK_LABEL(l), t);
}

static GtkWidget *drawing(int w, int h, GCallback draw, gpointer d)
{
    GtkWidget *a = gtk_drawing_area_new();
    gtk_widget_set_size_request(a, w, h);
    g_signal_connect(a, "draw", draw, d);
    return a;
}

/* the time the history graphs cover: PD_HIST samples at the Update Speed */
static const char *span(void)
{
    static char b[32];
    int sec = PD_HIST * perf_prefs_interval_ms() / 1000;
    if (sec >= 120 && sec % 60 == 0) snprintf(b, sizeof b, "%d minutes", sec / 60);
    else snprintf(b, sizeof b, "%d seconds", sec);
    return b;
}

/* ------------------------------------------------------------------ helpers on the snapshot */

static double cpu_frac_mhz(const pd_snap *s)
{
    double mx = s->max_mhz > 0 ? s->max_mhz : 0;
    if (s->mhz_peak > mx) mx = s->mhz_peak;
    return mx > 0 ? s->mhz / mx : 0;
}

static const char *pstate_name(char c)
{
    switch (c) {
    case 'R': return "Running";
    case 'S': return "Sleeping";
    case 'D': return "Disk Sleep";
    case 'Z': return "Zombie";
    case 'T': return "Suspended";
    case 't': return "Tracing Stop";
    case 'I': return "Idle";
    case 'X': case 'x': return "Dead";
    default: return "Unknown";
    }
}


/* ------------------------------------------------------------------ Summary page */

static gboolean draw_meters(GtkWidget *w, cairo_t *cr, gpointer d)
{
    pv *v = d;
    const pd_snap *s = &v->v;
    PangoLayout *l = lay(w, cr);
    double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
    char b[64], c[64];
    static const char *const names[4] = { "CPU", "Clock", "Temp", "GPU" };
    const rgb cols[4] = { C_CPU, C_KER, C_TEMP, C_GPU };
    double fr[4], bw, top = 50, bot = 22;
    panel(cr, W, H, C_CPU);
    pd_fmt_mhz(s->mhz, c, sizeof c);
    snprintf(b, sizeof b, "System");
    text(cr, l, 10, 8, 0, F_HEAD, C_CPU, 1, b);
    text(cr, l, W - 10, 8, 1, F_HEAD, C_CPU, 1, c);
    fr[0] = s->cpu / 100;
    fr[1] = cpu_frac_mhz(s);
    fr[2] = isnan(s->cpu_temp) ? 0 : s->cpu_temp / 100;
    fr[3] = s->gpu_busy >= 0 ? s->gpu_busy / 100.0 : 0;
    bw = (W - 20) / 4;
    for (int i = 0; i < 4; i++) {
        double x = 10 + i * bw;
        text(cr, l, x + bw / 2, 30, 0.5, F_SMALL, cols[i], 1, names[i]);
        led_v(cr, x + bw * 0.22, top, bw * 0.56, H - top - bot, fr[i], cols[i], i == 0 || i == 2);
        switch (i) {
        case 0: snprintf(b, sizeof b, "%.0f %%", perf_prefs_cpu_value(s->cpu, s->ncpu)); break;
        case 1: if (s->mhz > 0) snprintf(b, sizeof b, "%.1fG", s->mhz / 1000); else snprintf(b, sizeof b, EM); break;
        case 2: if (!isnan(s->cpu_temp)) snprintf(b, sizeof b, "%.0f°", s->cpu_temp); else snprintf(b, sizeof b, EM); break;
        default: if (s->gpu_busy >= 0) snprintf(b, sizeof b, "%d %%", s->gpu_busy); else snprintf(b, sizeof b, EM); break;
        }
        text(cr, l, x + bw / 2, H - bot + 3, 0.5, F_BOLD, cols[i], 1, b);
    }
    return TRUE;
}

static void legend(cairo_t *cr, PangoLayout *l, double *x, double y, rgb c, const char *name)
{
    cairo_set_source_rgba(cr, c.r, c.g, c.b, 1);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, *x, y + 7); cairo_line_to(cr, *x + 14, y + 7);
    cairo_stroke(cr);
    *x += 18 + text(cr, l, *x + 18, y, 0, F_SMALL, c, 1, name) + 14;
}

static gboolean draw_overview(GtkWidget *w, cairo_t *cr, gpointer d)
{
    pv *v = d;
    const pd_snap *s = &v->v;
    PangoLayout *l = lay(w, cr);
    double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
    double gx = 46, gy = 48, gw = W - 46 - 46, gh = H - 48 - 38, lx = 12;
    char b[64], c[64];
    gspec g;
    panel(cr, W, H, C_CPU);
    text(cr, l, 12, 8, 0, F_HEAD, C_CPU, 1, "CPU Overview");
    snprintf(b, sizeof b, "%.0f %%", perf_prefs_cpu_value(s->cpu, s->ncpu));
    text(cr, l, W - 12, 6, 1, F_BIG, C_CPU, 1, b);
    legend(cr, l, &lx, 28, C_CPU, "Utilization");
    legend(cr, l, &lx, 28, C_TEMP, "Temperature (right axis)");
    legend(cr, l, &lx, 28, C_KER, "Kernel time");
    memset(&g, 0, sizeof g);
    g.c = C_CPU;
    g.ymin = 0; g.ymax = 100;
    g.n = 3;
    g.s[0].r = &s->h_ctemp; g.s[0].c = C_TEMP;
    g.s[1].r = &s->h_kernel; g.s[1].c = C_KER;
    g.s[2].r = &s->h_cpu; g.s[2].c = C_CPU; g.s[2].fill = 1;
    if (gw > 40 && gh > 30) {
        graph(cr, gx, gy, gw, gh, &g, 12, 10, 1);
        text(cr, l, gx - 5, gy - 6, 1, F_SMALL, C_CPU, 1, "100 %");
        text(cr, l, gx - 5, gy + gh / 2 - 6, 1, F_SMALL, C_CPU, 1, "50 %");
        text(cr, l, gx - 5, gy + gh - 6, 1, F_SMALL, C_CPU, 1, "0 %");
        text(cr, l, gx + gw + 5, gy - 6, 0, F_SMALL, C_TEMP, 1, "100°");
        text(cr, l, gx + gw + 5, gy + gh / 2 - 6, 0, F_SMALL, C_TEMP, 1, "50°");
        text(cr, l, gx + gw + 5, gy + gh - 6, 0, F_SMALL, C_TEMP, 1, "0°");
    }
    snprintf(b, sizeof b, "%d logical processors · %s", s->ncpu, span());
    text(cr, l, gx, H - 30, 0, F_SMALL, C_DIM, 1, b);
    pd_fmt_mhz(s->mhz, c, sizeof c);
    snprintf(b, sizeof b, "Speed %s", c);
    if (!isnan(s->cpu_temp)) snprintf(b + strlen(b), sizeof b - strlen(b), " · %.0f °C", s->cpu_temp);
    text(cr, l, gx + gw, H - 30, 1, F_SMALL, C_DIM, 1, b);
    snprintf(b, sizeof b, "Kernel %.1f %% · Load %.2f %.2f %.2f", s->cpu_kernel, s->load1, s->load5, s->load15);
    text(cr, l, gx, H - 17, 0, F_SMALL, C_DIM, 1, b);
    return TRUE;
}

/* the n busiest processes (CPU, then memory) without allocating: insertion into a small array */
static int top_procs(const pd_snap *s, int *idx, int max)
{
    int n = 0;
    for (int i = 0; i < s->nproc; i++) {
        const pd_proc *p = &s->proc[i];
        int j;
        if (p->kthread && p->cpu < 0.05) continue;
        for (j = n; j > 0; j--) {
            const pd_proc *q = &s->proc[idx[j - 1]];
            if (q->cpu > p->cpu || (q->cpu == p->cpu && q->rss >= p->rss)) break;
        }
        if (j >= max) continue;
        if (n < max) n++;
        memmove(idx + j + 1, idx + j, (size_t)(n - 1 - j) * sizeof *idx);
        idx[j] = i;
    }
    return n;
}

static gboolean draw_top(GtkWidget *w, cairo_t *cr, gpointer d)
{
    pv *v = d;
    const pd_snap *s = &v->v;
    PangoLayout *l = lay(w, cr);
    double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
    double rh = 17, y0 = 52, xpid = 10 + 52, xname = 70, xcpu = W - 132, xgpu = W - 92, xmem = W - 10;
    int idx[64], n, rows = (int)((H - y0 - 8) / rh);
    char b[64];
    if (rows > 64) rows = 64;
    if (rows < 0) rows = 0;
    n = top_procs(s, idx, rows);
    v->ntop = n;
    v->top_y0 = y0;
    v->top_rh = rh;
    for (int i = 0; i < n; i++) v->top_pid[i] = s->proc[idx[i]].pid;
    panel(cr, W, H, C_NET);
    snprintf(b, sizeof b, "Top %d CPU Processes", n);
    text(cr, l, 12, 8, 0, F_HEAD, C_NET, 1, b);
    text(cr, l, xpid, 32, 1, F_BOLD, C_DIM, 1, "PID");
    text(cr, l, xname, 32, 0, F_BOLD, C_DIM, 1, "Name");
    text(cr, l, xcpu, 32, 1, F_BOLD, C_DIM, 1, "CPU");
    text(cr, l, xgpu, 32, 1, F_BOLD, C_DIM, 1, "GPU");
    text(cr, l, xmem, 32, 1, F_BOLD, C_DIM, 1, "Memory");
    for (int i = 0; i < n; i++) {
        const pd_proc *p = &s->proc[idx[i]];
        double y = y0 + i * rh;
        if (p->cpu >= 0.05 || p->gpu > 0) {
            double a = 0.18 + 0.5 * fmin(1.0, p->cpu / 25.0);
            cairo_set_source_rgba(cr, 0.10, 0.55, 0.20, a);
            cairo_rectangle(cr, 6, y - 1, W - 12, rh - 1);
            cairo_fill(cr);
        }
        snprintf(b, sizeof b, "%d", p->pid);
        text(cr, l, xpid, y, 1, F_NORM, C_TXT, 1, b);
        text_clip(cr, l, xname, y, xcpu - 44 - xname, F_NORM, C_TXT, p->name);
        snprintf(b, sizeof b, "%.1f %%", perf_prefs_cpu_value(p->cpu, s->ncpu));
        text(cr, l, xcpu, y, 1, F_NORM, C_TXT, 1, b);
        if (p->gpu >= 0) snprintf(b, sizeof b, "%d %%", p->gpu);
        else snprintf(b, sizeof b, EM);
        text(cr, l, xgpu, y, 1, F_NORM, C_TXT, 1, b);
        pd_fmt_bytes((double)p->rss, b, sizeof b);
        text(cr, l, xmem, y, 1, F_NORM, C_TXT, 1, b);
    }
    return TRUE;
}

static gboolean draw_mem(GtkWidget *w, cairo_t *cr, gpointer d)
{
    pv *v = d;
    const pd_snap *s = &v->v;
    PangoLayout *l = lay(w, cr);
    double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
    double gx = 60, gy = 30, gw = W - 70, gh = H - 30 - 36, lo = 100, hi = 0, pct = 0;
    char b[96], c[48], e[48];
    gspec g;
    panel(cr, W, H, C_MEM);
    if (s->mem_total > 0) pct = 100.0 * (double)s->mem_used / (double)s->mem_total;
    led_v(cr, 18, 30, 24, H - 30 - 26, pct / 100, C_MEM, 1);
    snprintf(b, sizeof b, "%.0f %%", pct);
    text(cr, l, 30, H - 22, 0.5, F_BOLD, C_MEM, 1, b);
    text(cr, l, gx, 8, 0, F_HEAD, C_MEM, 1, "Memory Utilization");
    pd_fmt_bytes((double)s->mem_used, c, sizeof c);
    pd_fmt_bytes((double)s->mem_total, e, sizeof e);
    snprintf(b, sizeof b, "%s / %s", c, e);
    text(cr, l, W - 10, 6, 1, F_BIG, C_MEM, 1, b);
    /* zoom to the history's range (a flat line at 35 % says little) */
    for (int i = 0; i < s->h_mem.n; i++) {
        double x = pd_ring_at(&s->h_mem, i);
        if (isnan(x)) continue;
        if (x < lo) lo = x;
        if (x > hi) hi = x;
    }
    if (hi < lo) { lo = 0; hi = 100; }
    {
        double span = fmax(hi - lo, 2.0);
        lo = fmax(0, lo - span * 0.6);
        hi = fmin(100, hi + span * 0.6);
    }
    memset(&g, 0, sizeof g);
    g.c = C_MEM; g.ymin = lo; g.ymax = hi; g.n = 1;
    g.s[0].r = &s->h_mem; g.s[0].c = C_MEM; g.s[0].fill = 1;
    if (gw > 40 && gh > 20) {
        graph(cr, gx, gy, gw, gh, &g, 24, 4, 1);
        pd_fmt_bytes(hi / 100 * (double)s->mem_total, c, sizeof c);
        text(cr, l, gx + 4, gy + 2, 0, F_SMALL, C_MEM, 0.9, c);
        pd_fmt_bytes(lo / 100 * (double)s->mem_total, c, sizeof c);
        text(cr, l, gx + 4, gy + gh - 15, 0, F_SMALL, C_MEM, 0.9, c);
    }
    pd_fmt_bytes((double)s->mem_avail, c, sizeof c);
    snprintf(b, sizeof b, "Available %s", c);
    text(cr, l, gx, H - 30, 0, F_SMALL, C_DIM, 1, b);
    pd_fmt_bytes((double)s->mem_cached, c, sizeof c);
    snprintf(b, sizeof b, "Cached %s", c);
    text(cr, l, gx + gw / 2, H - 30, 0.5, F_SMALL, C_DIM, 1, b);
    pd_fmt_bytes((double)s->swap_used, c, sizeof c);
    pd_fmt_bytes((double)s->swap_total, e, sizeof e);
    snprintf(b, sizeof b, "Swap %s / %s", c, e);
    text(cr, l, gx + gw, H - 30, 1, F_SMALL, C_DIM, 1, b);
    { char m[64]; snprintf(m, sizeof m, "Physical memory in use, %s", span()); text(cr, l, gx, H - 17, 0, F_SMALL, C_DIM, 0.8, m); }
    return TRUE;
}

static double net_scale(const pd_snap *s)
{
    double sc = 0;
    for (int i = 0; i < s->nnet; i++)
        if (s->net[i].up && s->net[i].speed_mbps > 0 && s->net[i].speed_mbps * 125000.0 > sc) sc = s->net[i].speed_mbps * 125000.0;
    return sc;
}

static gboolean draw_tile(GtkWidget *w, cairo_t *cr, gpointer d)
{
    pv *v = d;
    const pd_snap *s = &v->v;
    int which = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "tile"));
    PangoLayout *l = lay(w, cr);
    double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w), frac = 0;
    char line[160], val[48], a[48], b[48];
    const char *title;
    rgb c;
    int warn = 0;
    switch (which) {
    case 0: {
        double mx = fmax(s->psi_cpu, fmax(s->psi_mem, s->psi_io));
        title = "System Pressure";
        c = C_CPU;
        if (!s->psi_ok) { snprintf(line, sizeof line, "Not reported (kernel without PSI)"); snprintf(val, sizeof val, EM); break; }
        snprintf(line, sizeof line, "%s · CPU %.1f %% · Memory %.1f %% · I/O %.1f %%",
                 mx < 10 ? "Low" : mx < 30 ? "Moderate" : "High", s->psi_cpu, s->psi_mem, s->psi_io);
        snprintf(val, sizeof val, "%.1f %%", mx);
        frac = mx / 100;
        warn = 1;
        break;
    }
    case 1: {
        double sc = net_scale(s);
        title = "Network";
        c = C_NET;
        pd_fmt_rate(s->net_rx, a, sizeof a);
        pd_fmt_rate(s->net_tx, b, sizeof b);
        snprintf(line, sizeof line, "Receive %s · Send %s", a, b);
        if (sc <= 0) sc = nice_ceil(fmax(pd_ring_max(&s->h_net), 1e5));
        pd_fmt_bits(s->net_rx + s->net_tx, val, sizeof val);
        frac = (s->net_rx + s->net_tx) / sc;
        if (frac > 0 && frac < 0.02) frac = 0.02;
        break;
    }
    case 2:
        title = "Disk";
        c = C_DISK;
        pd_fmt_rate(s->disk_rd, a, sizeof a);
        pd_fmt_rate(s->disk_wr, b, sizeof b);
        snprintf(line, sizeof line, "Read %s · Write %s", a, b);
        snprintf(val, sizeof val, "%.1f %%", s->disk_busy);
        frac = s->disk_busy / 100;
        warn = 1;
        break;
    default: {
        int cap = 0;
        title = s->ngpu > 1 ? "GPUs" : "GPU";
        c = C_GPU;
        for (int i = 0; i < s->ngpu; i++) if (s->gpu[i].power_cap > 0) cap += s->gpu[i].power_cap;
        if (s->gpu_w >= 0) {
            snprintf(line, sizeof line, "Power %.1f W", s->gpu_w);
            if (cap) snprintf(line + strlen(line), sizeof line - strlen(line), " of %d W", cap);
            if (s->gpu_busy >= 0) snprintf(line + strlen(line), sizeof line - strlen(line), " · Busy %d %%", s->gpu_busy);
            frac = cap ? s->gpu_w / cap : s->gpu_busy / 100.0;
        } else snprintf(line, sizeof line, s->ngpu ? "Not sampled (asleep or unreadable)" : "No graphics card found");
        if (s->gpu_temp >= 0) snprintf(val, sizeof val, "%d °C", s->gpu_temp);
        else snprintf(val, sizeof val, EM);
        break;
    }
    }
    panel(cr, W, H, c);
    text(cr, l, 12, 7, 0, F_HEAD, c, 1, title);
    text_clip(cr, l, 12, 26, W - 24, F_NORM, C_TXT, line);
    led_h(cr, 12, H - 22, W - 12 - 78, 12, frac, c, warn);
    text(cr, l, W - 12, H - 25, 1, F_BOLD, c, 1, val);
    return TRUE;
}

static GtkWidget *page_header(const char *title, GtkWidget **right_box)
{
    GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8), *t = label(title, 0, "pv-title");
    gtk_box_pack_start(GTK_BOX(h), t, FALSE, FALSE, 0);
    if (right_box) {
        *right_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_end(GTK_BOX(h), *right_box, FALSE, FALSE, 0);
    }
    return h;
}

static void on_settings(GtkButton *b, gpointer d)
{
    GtkWidget *t = gtk_widget_get_toplevel(GTK_WIDGET(b));
    (void)d;
    perf_tray_settings_dialog(GTK_IS_WINDOW(t) ? GTK_WINDOW(t) : NULL);
}

static gboolean on_top_press(GtkWidget *w, GdkEventButton *ev, gpointer d);
static int top_sel(void *user, int *out, int max);
static const pd_snap *proc_snap(void *user);

static GtkWidget *build_summary(pv *v)
{
    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6), *g = gtk_grid_new(), *tiles;
    gtk_container_set_border_width(GTK_CONTAINER(b), 8);
    {
        GtkWidget *rb, *sb;
        gtk_box_pack_start(GTK_BOX(b), page_header("Summary", &rb), FALSE, FALSE, 0);
        sb = gtk_button_new_with_label("Settings\u2026");   /* E is the top row's Settings */
        gtk_widget_set_tooltip_text(sb, "Task Manager settings: update speed, start page, per-core CPU units, tray meters");
        g_signal_connect(sb, "clicked", G_CALLBACK(on_settings), NULL);
        gtk_box_pack_end(GTK_BOX(rb), sb, FALSE, FALSE, 0);
    }
    gtk_grid_set_row_spacing(GTK_GRID(g), 6);
    gtk_grid_set_column_spacing(GTK_GRID(g), 6);
    v->s_meters = drawing(176, 300, G_CALLBACK(draw_meters), v);
    v->s_overview = drawing(360, 300, G_CALLBACK(draw_overview), v);
    v->s_top = drawing(340, 300, G_CALLBACK(draw_top), v);
    gtk_widget_add_events(v->s_top, GDK_BUTTON_PRESS_MASK);
    gtk_widget_set_tooltip_text(v->s_top, "Right-click a process for End Task, Force Kill, Suspend, Priority and more");
    g_signal_connect(v->s_top, "button-press-event", G_CALLBACK(on_top_press), v);
    gtk_widget_set_hexpand(v->s_overview, TRUE);
    gtk_widget_set_vexpand(v->s_overview, TRUE);
    gtk_widget_set_vexpand(v->s_top, TRUE);
    gtk_grid_attach(GTK_GRID(g), v->s_meters, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), v->s_overview, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), v->s_top, 2, 0, 1, 1);
    v->s_mem = drawing(400, 150, G_CALLBACK(draw_mem), v);
    gtk_grid_attach(GTK_GRID(g), v->s_mem, 0, 1, 3, 1);
    tiles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_set_homogeneous(GTK_BOX(tiles), TRUE);
    for (int i = 0; i < 4; i++) {
        v->s_tiles[i] = drawing(200, 72, G_CALLBACK(draw_tile), v);
        g_object_set_data(G_OBJECT(v->s_tiles[i]), "tile", GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(tiles), v->s_tiles[i], TRUE, TRUE, 0);
    }
    gtk_grid_attach(GTK_GRID(g), tiles, 0, 2, 3, 1);
    v->pp_top = pp_ui_new(top_sel, proc_snap, v);
    g_object_ref_sink(pp_toolbar(v->pp_top));   /* not shown: the Summary page offers the actions as a menu only */
    gtk_box_pack_start(GTK_BOX(b), pp_infobar(v->pp_top), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(b), g, TRUE, TRUE, 0);
    v->s_status = label("Starting…", 0, "pv-dim");
    gtk_box_pack_start(GTK_BOX(b), v->s_status, FALSE, FALSE, 0);
    return b;
}

static void update_summary(pv *v)
{
    const pd_snap *s = &v->v;
    char b[200], u[32];
    pd_fmt_uptime(s->uptime, u, sizeof u);
    snprintf(b, sizeof b, "%d processes \u00b7 %d threads \u00b7 %d applications \u00b7 up %s \u00b7 refresh %g s",
             s->nproc, s->nthreads, s->napps, u, perf_prefs_interval_ms() / 1000.0);
    set_label(v->s_status, b);
    gtk_widget_queue_draw(v->s_meters);
    gtk_widget_queue_draw(v->s_overview);
    gtk_widget_queue_draw(v->s_top);
    gtk_widget_queue_draw(v->s_mem);
    for (int i = 0; i < 4; i++) gtk_widget_queue_draw(v->s_tiles[i]);
}

/* ------------------------------------------------------------------ Performance page */

static rgb kind_colour(int k)
{
    switch (k) {
    case K_CPU: return C_CPU;
    case K_MEM: return C_MEM;
    case K_PWR: return C_PWR;
    case K_THERM: return C_TEMP;
    case K_DISK: return C_DISK;
    default: return C_NET;
    }
}

/* the graph of resource (kind, idx); which = 0 main, 1 second graph */
static void spec_of(const pd_snap *s, int kind, int idx, int which, gspec *g)
{
    memset(g, 0, sizeof *g);
    g->c = kind_colour(kind);
    g->ymin = 0;
    g->ymax = 100;
    g->unit = U_PCT;
    switch (kind) {
    case K_CPU:
        g->n = 2;
        g->s[0].r = &s->h_kernel; g->s[0].c = C_KER; g->s[0].name = "Kernel";
        g->s[1].r = &s->h_cpu; g->s[1].c = C_CPU; g->s[1].fill = 1; g->s[1].name = "Total";
        break;
    case K_MEM:
        g->n = 1;
        if (which) { g->s[0].r = &s->h_swap; g->s[0].c = C_MEM; g->s[0].name = "Swap"; }
        else { g->s[0].r = &s->h_mem; g->s[0].c = C_MEM; g->s[0].fill = 1; g->s[0].name = "In use"; }
        break;
    case K_PWR: {
        double mx = pd_ring_max(&s->h_cpu_w);
        g->unit = U_W;
        g->s[g->n].r = &s->h_cpu_w; g->s[g->n].c = C_PWR; g->s[g->n].fill = 1; g->s[g->n++].name = "CPU package";
        for (int i = 0; i < s->ngpu && g->n < GMAX; i++) {
            double m = pd_ring_max(&s->gpu[i].h_power);
            if (!s->gpu[i].open) continue;
            g->s[g->n].r = &s->gpu[i].h_power; g->s[g->n].c = C_GPU; g->s[g->n++].name = s->gpu[i].dev.name;
            if (!isnan(m) && (isnan(mx) || m > mx)) mx = m;
        }
        g->ymax = nice_ceil(isnan(mx) ? 50 : fmax(mx * 1.15, 20));
        break;
    }
    case K_THERM: {
        double mx = 0;
        g->unit = U_C;
        for (int i = 0; i < s->ntemp && g->n < GMAX; i++) {
            double m = pd_ring_max(&s->temp[i].h);
            g->s[g->n].r = &s->temp[i].h;
            g->s[g->n].c = C_SERIES[i % 8];
            g->s[g->n++].name = s->temp[i].label;
            if (!isnan(m) && m > mx) mx = m;
        }
        g->ymax = mx > 90 ? 110 : 100;
        break;
    }
    case K_DISK:
        if (idx < 0 || idx >= s->ndisk) break;
        if (!which) { g->n = 1; g->s[0].r = &s->disk[idx].h_busy; g->s[0].c = C_DISK; g->s[0].fill = 1; g->s[0].name = "Active time"; }
        else {
            double m = fmax(pd_ring_max(&s->disk[idx].h_rd), pd_ring_max(&s->disk[idx].h_wr));
            g->unit = U_RATE;
            g->n = 2;
            g->s[0].r = &s->disk[idx].h_rd; g->s[0].c = C_DISK; g->s[0].fill = 1; g->s[0].name = "Read";
            g->s[1].r = &s->disk[idx].h_wr; g->s[1].c = C_PWR; g->s[1].name = "Write";
            g->ymax = nice_ceil(isnan(m) ? 1 << 20 : fmax(m * 1.1, 1 << 20));
        }
        break;
    case K_NET: {
        double m;
        if (idx < 0 || idx >= s->nnet) break;
        m = fmax(pd_ring_max(&s->net[idx].h_rx), pd_ring_max(&s->net[idx].h_tx));
        g->unit = U_RATE;
        g->n = 2;
        g->s[0].r = &s->net[idx].h_rx; g->s[0].c = C_NET; g->s[0].fill = 1; g->s[0].name = "Receive";
        g->s[1].r = &s->net[idx].h_tx; g->s[1].c = C_TEMP; g->s[1].name = "Send";
        g->ymax = nice_ceil(isnan(m) ? 1e5 : fmax(m * 1.1, 1e5));
        break;
    }
    case K_GPU:
        if (idx < 0 || idx >= s->ngpu) break;
        g->n = 1;
        if (!which) { g->s[0].r = &s->gpu[idx].h_busy; g->s[0].c = C_GPU; g->s[0].fill = 1; g->s[0].name = "Utilization"; }
        else { g->s[0].r = &s->gpu[idx].h_vram; g->s[0].c = C_MEM; g->s[0].fill = 1; g->s[0].name = "VRAM"; }
        break;
    }
}

static gboolean draw_spark(GtkWidget *w, cairo_t *cr, gpointer d)
{
    resrow *r = d;
    gspec g;
    double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
    spec_of(&r->v->v, r->kind, r->idx, 0, &g);
    panel(cr, W, H, g.c);
    graph(cr, 2, 2, W - 4, H - 4, &g, 6, 4, 0);
    return TRUE;
}

static gboolean draw_bigbar(GtkWidget *w, cairo_t *cr, gpointer d)
{
    pv *v = d;
    const pd_snap *s = &v->v;
    PangoLayout *l = lay(w, cr);
    double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w), frac = 0;
    char b[64] = EM;
    rgb c = kind_colour(v->sel_kind);
    int k = v->sel_kind, i = v->sel_idx;
    switch (k) {
    case K_CPU: frac = s->cpu / 100; snprintf(b, sizeof b, "%.1f %%", perf_prefs_cpu_value(s->cpu, s->ncpu)); break;
    case K_MEM: if (s->mem_total > 0) { frac = (double)s->mem_used / (double)s->mem_total; snprintf(b, sizeof b, "%.1f %%", frac * 100); } break;
    case K_PWR: if (!isnan(s->cpu_w)) { frac = s->cpu_w / fmax(pd_ring_max(&s->h_cpu_w) * 1.2, 50); snprintf(b, sizeof b, "%.1f W", s->cpu_w); } break;
    case K_THERM: {
        double mx = NAN;
        for (int t = 0; t < s->ntemp; t++) if (!isnan(s->temp[t].c) && (isnan(mx) || s->temp[t].c > mx)) mx = s->temp[t].c;
        if (!isnan(mx)) { frac = mx / 100; snprintf(b, sizeof b, "%.0f °C", mx); }
        break;
    }
    case K_DISK: if (i < s->ndisk) { frac = s->disk[i].busy / 100; snprintf(b, sizeof b, "%.1f %%", s->disk[i].busy); } break;
    case K_NET: if (i < s->nnet) {
            double sc = s->net[i].speed_mbps > 0 ? s->net[i].speed_mbps * 125000.0 : nice_ceil(fmax(pd_ring_max(&s->net[i].h_rx), 1e5));
            frac = (s->net[i].rx + s->net[i].tx) / sc;
            pd_fmt_bits(s->net[i].rx + s->net[i].tx, b, sizeof b);
        }
        break;
    case K_GPU: if (i < s->ngpu && s->gpu[i].busy >= 0) { frac = s->gpu[i].busy / 100.0; snprintf(b, sizeof b, "%d %%", s->gpu[i].busy); } break;
    }
    panel(cr, W, H, c);
    led_h(cr, 10, 8, W - 20 - 96, H - 16, frac, c, k == K_CPU || k == K_THERM || k == K_MEM);
    text(cr, l, W - 12, (H - 20) / 2, 1, F_BIG, c, 1, b);
    return TRUE;
}

static gboolean draw_detail_graph(GtkWidget *w, cairo_t *cr, gpointer d)
{
    pv *v = d;
    int which = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "which"));
    PangoLayout *l = lay(w, cr);
    double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w), lx = 10;
    gspec g;
    char b[48];
    spec_of(&v->v, v->sel_kind, v->sel_idx, which, &g);
    panel(cr, W, H, g.c);
    if (g.n > 1 || v->sel_kind == K_THERM || v->sel_kind == K_PWR)
        for (int i = 0; i < g.n; i++) legend(cr, l, &lx, 6, g.s[i].c, g.s[i].name ? g.s[i].name : "");
    graph(cr, 8, 26, W - 16, H - 34, &g, 12, 10, 1);
    fmt_axis(g.ymax, g.unit, b, sizeof b);
    text(cr, l, W - 12, 6, 1, F_SMALL, g.c, 1, b);
    return TRUE;
}

static gboolean draw_lcpu(GtkWidget *w, cairo_t *cr, gpointer d)
{
    pv *v = d;
    int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "cpu"));
    PangoLayout *l = lay(w, cr);
    double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
    gspec g;
    char b[48];
    const pd_snap *s = &v->v;
    if (i >= s->ncpu) return TRUE;
    memset(&g, 0, sizeof g);
    g.c = C_NET; g.ymin = 0; g.ymax = 100; g.n = 2;
    g.s[0].r = &s->hu[i]; g.s[0].c = C_CPU; g.s[0].fill = 1;
    g.s[1].r = &s->hk[i]; g.s[1].c = C_KER;
    panel(cr, W, H, C_NET);
    graph(cr, 3, 17, W - 6, H - 20, &g, 6, 5, 1);
    snprintf(b, sizeof b, "CPU %d", s->cpu_id[i]);
    text(cr, l, 6, 2, 0, F_SMALL, C_NET, 1, b);
    snprintf(b, sizeof b, "%.0f %%", s->cu[i] + s->ck[i]);
    text(cr, l, W - 6, 2, 1, F_SMALL, C_CPU, 1, b);
    return TRUE;
}

static void build_cpu_grid(pv *v)
{
    int n = v->v.ncpu, cols;
    GList *ch = gtk_container_get_children(GTK_CONTAINER(v->p_cpugrid));
    for (GList *c = ch; c; c = c->next) gtk_widget_destroy(c->data);
    g_list_free(ch);
    g_free(v->p_cpu);
    v->p_cpu = g_new0(GtkWidget *, n > 0 ? n : 1);
    v->p_ncpu = n;
    cols = (int)ceil(sqrt(n * 1.6));
    if (cols < 1) cols = 1;
    for (int i = 0; i < n; i++) {
        GtkWidget *a = drawing(90, 64, G_CALLBACK(draw_lcpu), v);
        g_object_set_data(G_OBJECT(a), "cpu", GINT_TO_POINTER(i));
        gtk_widget_set_hexpand(a, TRUE);
        gtk_widget_set_vexpand(a, TRUE);
        gtk_grid_attach(GTK_GRID(v->p_cpugrid), a, i % cols, i / cols, 1, 1);
        v->p_cpu[i] = a;
    }
    gtk_widget_show_all(v->p_cpugrid);
}

static const char *res_icons[][3] = {
    { "cpu-symbolic", "computer-symbolic", "computer" },
    { "memory-symbolic", "media-flash-symbolic", "drive-removable-media" },
    { "battery-full-charging-symbolic", "ac-adapter-symbolic", "utilities-system-monitor" },
    { "temperature-symbolic", "sensors-temperature-symbolic", "weather-clear-symbolic" },
    { "drive-harddisk-symbolic", "drive-harddisk", "media-floppy" },
    { "network-wired-symbolic", "network-wired", "network-workgroup" },
    { "video-display-symbolic", "video-display", "computer" },
};

static GtkWidget *themed_image(const char *const *names, int n, int px)
{
    GIcon *ic = g_themed_icon_new_from_names((char **)names, n);
    GtkWidget *im = gtk_image_new_from_gicon(ic, GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_image_set_pixel_size(GTK_IMAGE(im), px);
    g_object_unref(ic);
    return im;
}

static void res_add(pv *v, int kind, int idx)
{
    resrow *r;
    GtkWidget *h, *tb;
    if (v->nres >= MAX_RES) return;
    r = &v->res[v->nres++];
    r->v = v; r->kind = kind; r->idx = idx;
    r->row = gtk_list_box_row_new();
    h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(h), themed_image(res_icons[kind], 3, 20), FALSE, FALSE, 2);
    tb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    r->name = label("", 0, "pv-rowname");
    r->l1 = label("", 0, "pv-dim");
    r->l2 = label("", 0, "pv-dim");
    gtk_label_set_ellipsize(GTK_LABEL(r->name), PANGO_ELLIPSIZE_END);
    gtk_label_set_ellipsize(GTK_LABEL(r->l1), PANGO_ELLIPSIZE_END);
    gtk_label_set_ellipsize(GTK_LABEL(r->l2), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(r->name), 22);
    gtk_label_set_max_width_chars(GTK_LABEL(r->l1), 26);
    gtk_label_set_max_width_chars(GTK_LABEL(r->l2), 26);
    gtk_box_pack_start(GTK_BOX(tb), r->name, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb), r->l1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tb), r->l2, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(h), tb, TRUE, TRUE, 0);
    r->spark = drawing(76, 44, G_CALLBACK(draw_spark), r);
    gtk_widget_set_valign(r->spark, GTK_ALIGN_CENTER);
    gtk_box_pack_end(GTK_BOX(h), r->spark, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(r->row), h);
    g_object_set_data(G_OBJECT(r->row), "res", r);
    gtk_list_box_insert(GTK_LIST_BOX(v->p_list), r->row, -1);
    gtk_widget_show_all(r->row);
}

static void res_text(pv *v, resrow *r)
{
    const pd_snap *s = &v->v;
    char n[128] = "", a[128] = "", b[128] = "", x[48], y[48];
    int i = r->idx;
    switch (r->kind) {
    case K_CPU:
        snprintf(n, sizeof n, "CPU");
        pd_fmt_mhz(s->mhz, x, sizeof x);
        snprintf(a, sizeof a, "%.0f %%  %s", perf_prefs_cpu_value(s->cpu, s->ncpu), x);
        snprintf(b, sizeof b, "%d logical processors", s->ncpu);
        break;
    case K_MEM:
        snprintf(n, sizeof n, "Memory");
        pd_fmt_bytes((double)s->mem_used, x, sizeof x);
        pd_fmt_bytes((double)s->mem_total, y, sizeof y);
        snprintf(a, sizeof a, "%s / %s", x, y);
        snprintf(b, sizeof b, "%.0f %%", s->mem_total > 0 ? 100.0 * (double)s->mem_used / (double)s->mem_total : 0);
        break;
    case K_PWR:
        snprintf(n, sizeof n, "CPU Power");
        snprintf(a, sizeof a, "CPU package");
        if (!isnan(s->cpu_w)) snprintf(b, sizeof b, "%.1f W", s->cpu_w);
        else snprintf(b, sizeof b, s->cpu_w_state == PD_W_ROOT ? "n/a (needs root)" : "n/a");
        break;
    case K_THERM: {
        int h = -1;
        snprintf(n, sizeof n, "Thermals");
        for (int t = 0; t < s->ntemp; t++) if (!isnan(s->temp[t].c) && (h < 0 || s->temp[t].c > s->temp[h].c)) h = t;
        snprintf(a, sizeof a, "%d sensors", s->ntemp);
        if (h >= 0) snprintf(b, sizeof b, "Hottest %.0f °C (%s)", s->temp[h].c, s->temp[h].label);
        else snprintf(b, sizeof b, EM);
        break;
    }
    case K_DISK:
        if (i >= s->ndisk) break;
        snprintf(n, sizeof n, "Disk %d (%s)", i, s->disk[i].name);
        snprintf(a, sizeof a, "%s", s->disk[i].model[0] ? s->disk[i].model : s->disk[i].rotational ? "Hard disk" : "Disk");
        snprintf(b, sizeof b, "%s · %.0f %%", s->disk[i].removable ? "Removable" : s->disk[i].rotational ? "HDD" : "SSD", s->disk[i].busy);
        break;
    case K_NET:
        if (i >= s->nnet) break;
        snprintf(n, sizeof n, "%s (%s)", s->net[i].kind, s->net[i].name);
        pd_fmt_rate(s->net[i].tx, x, sizeof x);
        pd_fmt_rate(s->net[i].rx, y, sizeof y);
        snprintf(a, sizeof a, "S: %s  R: %s", x, y);
        snprintf(b, sizeof b, "%s", s->net[i].up ? (s->net[i].ipv4[0] ? s->net[i].ipv4 : "Connected") : "Not connected");
        break;
    case K_GPU:
        if (i >= s->ngpu) break;
        snprintf(n, sizeof n, "GPU %d", i);
        snprintf(a, sizeof a, "%s", s->gpu[i].dev.name[0] ? s->gpu[i].dev.name : "Graphics card");
        if (!s->gpu[i].open) snprintf(b, sizeof b, "%s", s->gpu[i].why[0] ? s->gpu[i].why : "Not sampled");
        else if (!s->gpu[i].have) snprintf(b, sizeof b, "Reading…");
        else {
            snprintf(b, sizeof b, "%d %%", s->gpu[i].busy < 0 ? 0 : s->gpu[i].busy);
            if (s->gpu[i].edge >= 0) snprintf(b + strlen(b), sizeof b - strlen(b), " · %d °C", s->gpu[i].edge);
        }
        break;
    }
    set_label(r->name, n);
    set_label(r->l1, a);
    set_label(r->l2, b);
}

/* (re)build the resource list when the set of disks / adapters / GPUs changes */
static void res_sync(pv *v)
{
    const pd_snap *s = &v->v;
    char sig[512];
    int o = 0;
    o += snprintf(sig + o, sizeof sig - (size_t)o, "%d|", s->ncpu);
    for (int i = 0; i < s->ndisk && o < (int)sizeof sig - 40; i++) o += snprintf(sig + o, sizeof sig - (size_t)o, "%s,", s->disk[i].name);
    for (int i = 0; i < s->nnet && o < (int)sizeof sig - 40; i++) o += snprintf(sig + o, sizeof sig - (size_t)o, "%s,", s->net[i].name);
    o += snprintf(sig + o, sizeof sig - (size_t)o, "%d", s->ngpu);
    if (!strcmp(sig, v->res_sig)) return;
    snprintf(v->res_sig, sizeof v->res_sig, "%s", sig);
    {
        int keep_kind = v->sel_kind, keep_idx = v->sel_idx;
        GList *ch = gtk_container_get_children(GTK_CONTAINER(v->p_list));
        for (GList *c = ch; c; c = c->next) gtk_widget_destroy(c->data);
        g_list_free(ch);
        v->nres = 0;
        res_add(v, K_CPU, 0);
        res_add(v, K_MEM, 0);
        res_add(v, K_PWR, 0);
        res_add(v, K_THERM, 0);
        for (int i = 0; i < s->ndisk; i++) res_add(v, K_DISK, i);
        for (int i = 0; i < s->nnet; i++) res_add(v, K_NET, i);
        for (int i = 0; i < s->ngpu; i++) res_add(v, K_GPU, i);
        if (v->want_res) {
            static const char *const nm[] = { "cpu", "memory", "power", "thermals", "disk", "network", "gpu" };
            for (int k = 0; k < 7; k++) if (!strcmp(v->want_res, nm[k])) { keep_kind = k; keep_idx = 0; }
            v->want_res = NULL;
        }
        for (int i = 0; i < v->nres; i++)
            if (v->res[i].kind == keep_kind && v->res[i].idx == keep_idx) {
                gtk_list_box_select_row(GTK_LIST_BOX(v->p_list), GTK_LIST_BOX_ROW(v->res[i].row));
                break;
            }
        if (!gtk_list_box_get_selected_row(GTK_LIST_BOX(v->p_list)) && v->nres)
            gtk_list_box_select_row(GTK_LIST_BOX(v->p_list), GTK_LIST_BOX_ROW(v->res[0].row));
    }
}

static void stat_set(pv *v, int *nb, int *ns, int big, const char *k, const char *val)
{
    if (big) {
        if (*nb >= 6) return;
        set_label(v->p_bigk[*nb], k);
        set_label(v->p_bigv[*nb], val);
        gtk_widget_show(v->p_bigk[*nb]);
        gtk_widget_show(v->p_bigv[*nb]);
        (*nb)++;
    } else {
        if (*ns >= MAX_STAT) return;
        set_label(v->p_smk[*ns], k);
        set_label(v->p_smv[*ns], val);
        gtk_widget_show(v->p_smk[*ns]);
        gtk_widget_show(v->p_smv[*ns]);
        (*ns)++;
    }
}

static void update_detail(pv *v)
{
    const pd_snap *s = &v->v;
    int nb = 0, ns = 0, i = v->sel_idx;
    char t[128] = "", sub[160] = "", c1[96] = "", c2[96] = "", b[160], x[48], y[48];
    int two = 0;
#define BIG(k, ...) do { snprintf(b, sizeof b, __VA_ARGS__); stat_set(v, &nb, &ns, 1, k, b); } while (0)
#define SM(k, ...) do { snprintf(b, sizeof b, __VA_ARGS__); stat_set(v, &nb, &ns, 0, k, b); } while (0)
    switch (v->sel_kind) {
    case K_CPU: {
        char up[32];
        snprintf(t, sizeof t, "CPU");
        snprintf(sub, sizeof sub, "%s", s->cpu_model);
        snprintf(c1, sizeof c1, "%% Utilization of each logical processor (user, kernel in red) over %s", span());
        BIG("Utilization", "%.1f %%", perf_prefs_cpu_value(s->cpu, s->ncpu));
        pd_fmt_mhz(s->mhz, x, sizeof x);
        BIG("Speed", "%s", x);
        BIG("Processes", "%d", s->nproc);
        BIG("Threads", "%d", s->nthreads);
        pd_fmt_uptime(s->uptime, up, sizeof up);
        BIG("Up Time", "%s", up);
        pd_fmt_mhz(s->base_mhz, x, sizeof x);
        SM("Base Speed:", "%s", s->base_mhz > 0 ? x : "Not reported");
        pd_fmt_mhz(s->max_mhz, x, sizeof x);
        SM("Maximum Speed:", "%s", x);
        SM("Sockets:", "%d", s->sockets);
        SM("Cores:", "%d", s->cores);
        SM("Logical Processors:", "%d", s->ncpu);
        SM("Virtualization:", "%s", s->virt ? "Supported (AMD-V / VT-x)" : "Not reported");
        SM("Governor:", "%s", s->governor[0] ? s->governor : EM);
        SM("Frequency Driver:", "%s", s->freq_driver[0] ? s->freq_driver : EM);
        if (!isnan(s->cpu_temp)) SM("Temperature:", "%.1f °C (%s)", s->cpu_temp, s->cpu_temp_src);
        else SM("Temperature:", "Not reported");
        if (!isnan(s->cpu_w)) SM("Package Power:", "%.1f W", s->cpu_w);
        else SM("Package Power:", "%s", s->cpu_w_state == PD_W_ROOT ? "n/a (needs root)" : "n/a");
        SM("Kernel Time:", "%.1f %%", s->cpu_kernel);
        SM("Load Average:", "%.2f  %.2f  %.2f", s->load1, s->load5, s->load15);
        break;
    }
    case K_MEM:
        snprintf(t, sizeof t, "Memory");
        pd_fmt_bytes((double)s->mem_total, x, sizeof x);
        snprintf(sub, sizeof sub, "%s installed memory visible to Linux", x);
        snprintf(c1, sizeof c1, "Memory in use over %s", span());
        snprintf(c2, sizeof c2, "Swap in use");
        two = 1;
        pd_fmt_bytes((double)s->mem_used, x, sizeof x);
        BIG("In Use", "%s", x);
        pd_fmt_bytes((double)s->mem_avail, x, sizeof x);
        BIG("Available", "%s", x);
        pd_fmt_bytes((double)s->commit, x, sizeof x);
        pd_fmt_bytes((double)s->commit_limit, y, sizeof y);
        BIG("Committed", "%s / %s", x, y);
        pd_fmt_bytes((double)s->mem_cached, x, sizeof x);
        BIG("Cached", "%s", x);
        pd_fmt_bytes((double)s->swap_used, x, sizeof x);
        pd_fmt_bytes((double)s->swap_total, y, sizeof y);
        BIG("Swap", "%s / %s", x, y);
        pd_fmt_bytes((double)s->mem_total, x, sizeof x); SM("Total:", "%s", x);
        pd_fmt_bytes((double)s->mem_free, x, sizeof x); SM("Free:", "%s", x);
        pd_fmt_bytes((double)s->mem_buffers, x, sizeof x); SM("Buffers:", "%s", x);
        pd_fmt_bytes((double)s->mem_shmem, x, sizeof x); SM("Shared:", "%s", x);
        pd_fmt_bytes((double)s->mem_slab, x, sizeof x); SM("Kernel Slab:", "%s", x);
        pd_fmt_bytes((double)s->mem_dirty, x, sizeof x); SM("Dirty:", "%s", x);
        if (s->psi_ok) SM("Memory Stalls:", "%.1f %% some, %.1f %% full (avg10)", s->psi_mem, s->psi_mem_full);
        break;
    case K_PWR: {
        double tot = 0;
        int any = 0;
        snprintf(t, sizeof t, "CPU Power");
        snprintf(sub, sizeof sub, "%s", s->cpu_w_src[0] ? s->cpu_w_src : "");
        snprintf(c1, sizeof c1, "Measured power over %s (CPU package and graphics cards)", span());
        if (!isnan(s->cpu_w)) { BIG("CPU Package", "%.1f W", s->cpu_w); tot += s->cpu_w; any = 1; }
        else BIG("CPU Package", "%s", s->cpu_w_state == PD_W_ROOT ? "n/a (needs root)" : "n/a");
        if (s->gpu_w >= 0) { BIG("Graphics Cards", "%.1f W", s->gpu_w); tot += s->gpu_w; any = 1; }
        else BIG("Graphics Cards", EM);
        if (any) BIG("Measured Total", "%.1f W", tot);
        SM("CPU Source:", "%s", s->cpu_w_src[0] ? s->cpu_w_src : EM);
        for (int g = 0; g < s->ngpu; g++) {
            char k[48];
            snprintf(k, sizeof k, "GPU %d:", g);
            if (s->gpu[g].open && s->gpu[g].power >= 0) {
                if (s->gpu[g].power_cap > 0) SM(k, "%.1f W of %d W cap", s->gpu[g].power, s->gpu[g].power_cap);
                else SM(k, "%.1f W", s->gpu[g].power);
            } else SM(k, "%s", s->gpu[g].open ? EM : s->gpu[g].why);
        }
        SM("Note:", "The rest of the PC (board, drives, fans, PSU losses) is not measured here");
        break;
    }
    case K_THERM:
        snprintf(t, sizeof t, "Thermals");
        snprintf(sub, sizeof sub, "CPU, graphics card and drive sensors");
        snprintf(c1, sizeof c1, "Temperatures over %s", span());
        for (int k = 0; k < s->ntemp; k++) {
            char kk[48];
            snprintf(kk, sizeof kk, "%s", s->temp[k].label);
            if (k < 5) { if (!isnan(s->temp[k].c)) BIG(kk, "%.0f °C", s->temp[k].c); else BIG(kk, EM); }
            else { strncat(kk, ":", sizeof kk - strlen(kk) - 1); if (!isnan(s->temp[k].c)) SM(kk, "%.1f °C", s->temp[k].c); else SM(kk, EM); }
        }
        break;
    case K_DISK:
        if (i >= s->ndisk) break;
        snprintf(t, sizeof t, "Disk %d (%s)", i, s->disk[i].name);
        snprintf(sub, sizeof sub, "%s", s->disk[i].model);
        snprintf(c1, sizeof c1, "Active time over %s", span());
        snprintf(c2, sizeof c2, "Disk transfer rate (read, write in yellow)");
        two = 1;
        BIG("Active Time", "%.1f %%", s->disk[i].busy);
        BIG("Average Response", "%.2f ms", s->disk[i].resp_ms);
        pd_fmt_rate(s->disk[i].rd, x, sizeof x); BIG("Read Speed", "%s", x);
        pd_fmt_rate(s->disk[i].wr, x, sizeof x); BIG("Write Speed", "%s", x);
        pd_fmt_bytes((double)s->disk[i].size, x, sizeof x); SM("Capacity:", "%s", x);
        SM("Type:", "%s%s", s->disk[i].rotational ? "Hard disk (HDD)" : "Solid state (SSD / flash)", s->disk[i].removable ? ", removable" : "");
        SM("Device:", "/dev/%s", s->disk[i].name);
        pd_fmt_bytes((double)s->disk[i].rd_total, x, sizeof x); SM("Read Since Boot:", "%s", x);
        pd_fmt_bytes((double)s->disk[i].wr_total, x, sizeof x); SM("Written Since Boot:", "%s", x);
        break;
    case K_NET:
        if (i >= s->nnet) break;
        snprintf(t, sizeof t, "%s", s->net[i].kind);
        snprintf(sub, sizeof sub, "%s", s->net[i].name);
        snprintf(c1, sizeof c1, "Throughput over %s (receive, send in orange)", span());
        pd_fmt_bits(s->net[i].rx, x, sizeof x); BIG("Receive", "%s", x);
        pd_fmt_bits(s->net[i].tx, x, sizeof x); BIG("Send", "%s", x);
        SM("Adapter:", "%s", s->net[i].name);
        SM("Connection:", "%s", s->net[i].up ? "Connected" : "Not connected");
        if (s->net[i].speed_mbps > 0) SM("Link Speed:", "%d Mbps", s->net[i].speed_mbps);
        else SM("Link Speed:", "Not reported");
        SM("IPv4 Address:", "%s", s->net[i].ipv4[0] ? s->net[i].ipv4 : EM);
        SM("IPv6 Address:", "%s", s->net[i].ipv6[0] ? s->net[i].ipv6 : EM);
        SM("Hardware Address:", "%s", s->net[i].mac[0] ? s->net[i].mac : EM);
        SM("Driver:", "%s", s->net[i].driver[0] ? s->net[i].driver : EM);
        pd_fmt_bytes((double)s->net[i].rx_total, x, sizeof x); SM("Received Since Boot:", "%s", x);
        pd_fmt_bytes((double)s->net[i].tx_total, x, sizeof x); SM("Sent Since Boot:", "%s", x);
        break;
    case K_GPU: {
        const pd_gpu *g;
        if (i >= s->ngpu) break;
        g = &s->gpu[i];
        snprintf(t, sizeof t, "GPU %d", i);
        snprintf(sub, sizeof sub, "%s", g->dev.name);
        snprintf(c1, sizeof c1, "Utilization over %s (GFX activity, no performance counters)", span());
        snprintf(c2, sizeof c2, "VRAM in use (%% of the card's memory)");
        two = 1;
        if (!g->open) { BIG("Status", "%s", g->why[0] ? g->why : "Not sampled"); SM("PCI Bus:", "%s", g->dev.pci); SM("Driver:", "%s", g->dev.driver); break; }
        if (g->busy >= 0) BIG("Utilization", "%d %%", g->busy); else BIG("Utilization", EM);
        pd_fmt_bytes((double)g->vram_used, x, sizeof x);
        pd_fmt_bytes((double)g->vram_total, y, sizeof y);
        BIG("Dedicated Memory", "%s / %s", x, y);
        if (g->power >= 0) BIG("Power", "%.1f W", g->power); else BIG("Power", EM);
        if (g->edge >= 0) BIG("Temperature", "%d °C", g->edge); else BIG("Temperature", EM);
        if (g->junction >= 0) BIG("Hotspot", "%d °C", g->junction);
        if (g->sclk >= 0) SM("Shader Clock:", "%d MHz%s", g->sclk, ""); else SM("Shader Clock:", EM);
        if (g->mclk >= 0) SM("Memory Clock:", "%d MHz", g->mclk); else SM("Memory Clock:", EM);
        if (g->memtemp >= 0) SM("Memory Temperature:", "%d °C", g->memtemp);
        if (g->power_cap > 0) SM("Power Cap:", "%d W", g->power_cap);
        if (g->fan_rpm >= 0) SM("Fan:", "%d RPM%s", g->fan_rpm, g->fan_pct >= 0 ? "" : "");
        pd_fmt_bytes((double)g->gtt_used, x, sizeof x);
        pd_fmt_bytes((double)g->gtt_total, y, sizeof y);
        SM("Shared Memory (GTT):", "%s / %s", x, y);
        if (g->vram_type[0]) SM("VRAM Type:", "%s", g->vram_type);
        if (g->pcie_gen > 0) SM("PCIe Link:", "Gen %d x%d", g->pcie_gen, g->pcie_width);
        SM("Power State:", "%s%s", g->pstate[0] ? g->pstate : EM, g->idle_d3 ? " (left idle to sleep)" : "");
        SM("PCI Bus:", "%s", g->dev.pci);
        SM("Driver:", "%s %s", g->dev.driver, g->driver_version);
        break;
    }
    }
#undef BIG
#undef SM
    for (int k = nb; k < 6; k++) { gtk_widget_hide(v->p_bigk[k]); gtk_widget_hide(v->p_bigv[k]); }
    for (int k = ns; k < MAX_STAT; k++) { gtk_widget_hide(v->p_smk[k]); gtk_widget_hide(v->p_smv[k]); }
    set_label(v->p_title, t);
    set_label(v->p_sub, sub);
    set_label(v->p_cap1, c1);
    set_label(v->p_cap2, c2);
    gtk_widget_set_visible(v->p_g2box, two);
    if (v->sel_kind == K_CPU) {
        if (v->p_ncpu != s->ncpu) build_cpu_grid(v);
        gtk_stack_set_visible_child_name(GTK_STACK(v->p_bodystack), "cpus");
        for (int k = 0; k < v->p_ncpu; k++) gtk_widget_queue_draw(v->p_cpu[k]);
    } else {
        gtk_stack_set_visible_child_name(GTK_STACK(v->p_bodystack), "graph");
        gtk_widget_queue_draw(v->p_g1);
    }
    if (two) gtk_widget_queue_draw(v->p_g2);
    gtk_widget_queue_draw(v->p_bar);
}

static void update_perf(pv *v)
{
    res_sync(v);
    for (int i = 0; i < v->nres; i++) {
        res_text(v, &v->res[i]);
        gtk_widget_queue_draw(v->res[i].spark);
    }
    update_detail(v);
}

static void on_res_selected(GtkListBox *lb, GtkListBoxRow *row, gpointer d)
{
    pv *v = d;
    resrow *r;
    (void)lb;
    if (!row || !(r = g_object_get_data(G_OBJECT(row), "res"))) return;
    v->sel_kind = r->kind;
    v->sel_idx = r->idx;
    if (v->v.seq) update_detail(v);
}

static GtkWidget *build_perf(pv *v)
{
    GtkWidget *pan = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL), *sw, *right, *hdr, *g, *gs, *cap, *rs;
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(sw, 270, -1);
    v->p_list = gtk_list_box_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(v->p_list), "pv-res");
    g_signal_connect(v->p_list, "row-selected", G_CALLBACK(on_res_selected), v);
    gtk_container_add(GTK_CONTAINER(sw), v->p_list);
    gtk_paned_pack1(GTK_PANED(pan), sw, FALSE, FALSE);

    right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(right), 8);
    hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    v->p_title = label("CPU", 0, "pv-title");
    v->p_sub = label("", 1, "pv-sub");
    gtk_label_set_ellipsize(GTK_LABEL(v->p_sub), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(hdr), v->p_title, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hdr), v->p_sub, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(right), hdr, FALSE, FALSE, 0);
    v->p_bar = drawing(300, 40, G_CALLBACK(draw_bigbar), v);
    gtk_box_pack_start(GTK_BOX(right), v->p_bar, FALSE, FALSE, 0);
    cap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    v->p_cap1 = label("", 0, "pv-dim");
    gtk_label_set_ellipsize(GTK_LABEL(v->p_cap1), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(cap), v->p_cap1, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(right), cap, FALSE, FALSE, 0);
    v->p_bodystack = gtk_stack_new();
    v->p_g1 = drawing(300, 160, G_CALLBACK(draw_detail_graph), v);
    g_object_set_data(G_OBJECT(v->p_g1), "which", GINT_TO_POINTER(0));
    v->p_cpugrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(v->p_cpugrid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(v->p_cpugrid), 4);
    gtk_grid_set_row_homogeneous(GTK_GRID(v->p_cpugrid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(v->p_cpugrid), TRUE);
    gtk_stack_add_named(GTK_STACK(v->p_bodystack), v->p_g1, "graph");
    gtk_stack_add_named(GTK_STACK(v->p_bodystack), v->p_cpugrid, "cpus");
    gtk_widget_set_vexpand(v->p_bodystack, TRUE);
    gtk_box_pack_start(GTK_BOX(right), v->p_bodystack, TRUE, TRUE, 0);
    v->p_g2box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    v->p_cap2 = label("", 0, "pv-dim");
    v->p_g2 = drawing(300, 110, G_CALLBACK(draw_detail_graph), v);
    g_object_set_data(G_OBJECT(v->p_g2), "which", GINT_TO_POINTER(1));
    gtk_box_pack_start(GTK_BOX(v->p_g2box), v->p_cap2, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(v->p_g2box), v->p_g2, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), v->p_g2box, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(v->p_g2box, TRUE);
    gtk_widget_show(v->p_cap2);
    gtk_widget_show(v->p_g2);

    g = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g), 28);
    gtk_grid_set_row_spacing(GTK_GRID(g), 0);
    for (int i = 0; i < 6; i++) {
        v->p_bigk[i] = label("", 0, "pv-dim");
        v->p_bigv[i] = label("", 0, "pv-big");
        gtk_label_set_selectable(GTK_LABEL(v->p_bigv[i]), TRUE);
        gtk_widget_set_can_focus(v->p_bigv[i], FALSE);
        gtk_grid_attach(GTK_GRID(g), v->p_bigk[i], i, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(g), v->p_bigv[i], i, 1, 1, 1);
        gtk_widget_set_no_show_all(v->p_bigk[i], TRUE);
        gtk_widget_set_no_show_all(v->p_bigv[i], TRUE);
    }
    gtk_box_pack_start(GTK_BOX(right), g, FALSE, FALSE, 4);
    gs = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(gs), 10);
    gtk_grid_set_row_spacing(GTK_GRID(gs), 1);
    for (int i = 0; i < MAX_STAT; i++) {
        int col = i < MAX_STAT / 2 ? 0 : 2, row = i % (MAX_STAT / 2);
        v->p_smk[i] = label("", 0, "pv-dim");
        v->p_smv[i] = label("", 0, "pv-num");
        gtk_label_set_selectable(GTK_LABEL(v->p_smv[i]), TRUE);
        gtk_widget_set_can_focus(v->p_smv[i], FALSE);
        gtk_label_set_ellipsize(GTK_LABEL(v->p_smv[i]), PANGO_ELLIPSIZE_END);
        gtk_widget_set_hexpand(v->p_smv[i], TRUE);
        gtk_grid_attach(GTK_GRID(gs), v->p_smk[i], col, row, 1, 1);
        gtk_grid_attach(GTK_GRID(gs), v->p_smv[i], col + 1, row, 1, 1);
        gtk_widget_set_no_show_all(v->p_smk[i], TRUE);
        gtk_widget_set_no_show_all(v->p_smv[i], TRUE);
    }
    gtk_box_pack_start(GTK_BOX(right), gs, FALSE, FALSE, 0);
    rs = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(rs), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(rs), TRUE);
    gtk_container_add(GTK_CONTAINER(rs), right);
    gtk_paned_pack2(GTK_PANED(pan), rs, TRUE, FALSE);
    return pan;
}

/* ------------------------------------------------------------------ Processes page */

static int row_kind(GtkTreeModel *m, GtkTreeIter *it)
{
    int k;
    gtk_tree_model_get(m, it, PC_KIND, &k, -1);
    return k;
}

static void cell_text_num(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
    int col = GPOINTER_TO_INT(d), k = row_kind(m, it);
    char b[48] = "";
    (void)c;
    if (k) { g_object_set(r, "text", "", "cell-background-set", FALSE, NULL); return; }
    switch (col) {
    case PC_PID: case PC_THR: case PC_NICE: {
        int x;
        gtk_tree_model_get(m, it, col, &x, -1);
        snprintf(b, sizeof b, "%d", x);
        break;
    }
    case PC_CPU: {
        double x;
        gtk_tree_model_get(m, it, col, &x, -1);
        snprintf(b, sizeof b, "%.1f %%", perf_prefs_cpu_value(x, g_ncpu));
        if (x >= 0.05) {
            GdkRGBA bg = { 0.15, 0.62, 0.25, 0.22 + 0.40 * fmin(1.0, x / 25.0) };
            g_object_set(r, "cell-background-rgba", &bg, NULL);
        } else g_object_set(r, "cell-background-set", FALSE, NULL);
        break;
    }
    case PC_MEM: {
        gint64 x;
        gtk_tree_model_get(m, it, col, &x, -1);
        pd_fmt_bytes((double)x, b, sizeof b);
        break;
    }
    case PC_RD: case PC_WR: {
        double x;
        gtk_tree_model_get(m, it, col, &x, -1);
        pd_fmt_rate(x, b, sizeof b);
        break;
    }
    case PC_GPU: {
        int x;
        gtk_tree_model_get(m, it, col, &x, -1);
        if (x < 0) snprintf(b, sizeof b, EM);
        else snprintf(b, sizeof b, "%d %%", x);
        break;
    }
    }
    g_object_set(r, "text", b, NULL);
}

static void cell_name(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
    int k = row_kind(m, it);
    (void)c; (void)d;
    g_object_set(r, "weight", k ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL, NULL);
}

static int cmp_num(double a, double b) { return a < b ? -1 : a > b ? 1 : 0; }

static gint sort_func(GtkTreeModel *m, GtkTreeIter *a, GtkTreeIter *b, gpointer d)
{
    pv *v = d;
    int col = 0, ka = row_kind(m, a), kb = row_kind(m, b), r = 0;
    GtkSortType ord = GTK_SORT_ASCENDING;
    gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(v->t_sort), &col, &ord);
    if (ka || kb) return (ka - kb) * (ord == GTK_SORT_DESCENDING ? -1 : 1);   /* groups keep their order */
    switch (col) {
    case PC_NAME: case PC_STATUS: case PC_USER: case PC_CMD: {
        gchar *x, *y;
        gtk_tree_model_get(m, a, col, &x, -1);
        gtk_tree_model_get(m, b, col, &y, -1);
        r = g_ascii_strcasecmp(x ? x : "", y ? y : "");
        g_free(x);
        g_free(y);
        break;
    }
    case PC_PID: case PC_GPU: case PC_THR: case PC_NICE: {
        int x, y;
        gtk_tree_model_get(m, a, col, &x, -1);
        gtk_tree_model_get(m, b, col, &y, -1);
        r = x - y;
        break;
    }
    case PC_MEM: {
        gint64 x, y;
        gtk_tree_model_get(m, a, col, &x, -1);
        gtk_tree_model_get(m, b, col, &y, -1);
        r = x < y ? -1 : x > y;
        break;
    }
    default: {
        double x, y;
        gtk_tree_model_get(m, a, col, &x, -1);
        gtk_tree_model_get(m, b, col, &y, -1);
        r = cmp_num(x, y);
        break;
    }
    }
    if (!r) {
        int x, y;
        gtk_tree_model_get(m, a, PC_PID, &x, -1);
        gtk_tree_model_get(m, b, PC_PID, &y, -1);
        r = x - y;
    }
    return r;
}

static gboolean visible_func(GtkTreeModel *m, GtkTreeIter *it, gpointer d)
{
    pv *v = d;
    gchar *name = NULL, *user = NULL, *cmd = NULL;
    int pid, k, ok;
    char ps[16];
    if (!v->t_filter_txt[0]) return TRUE;
    gtk_tree_model_get(m, it, PC_KIND, &k, PC_NAME, &name, PC_USER, &user, PC_CMD, &cmd, PC_PID, &pid, -1);
    if (k) { g_free(name); g_free(user); g_free(cmd); return TRUE; }
    snprintf(ps, sizeof ps, "%d", pid);
    {
        gchar *ln = name ? g_ascii_strdown(name, -1) : NULL, *lu = user ? g_ascii_strdown(user, -1) : NULL,
              *lc = cmd ? g_ascii_strdown(cmd, -1) : NULL;
        ok = (ln && strstr(ln, v->t_filter_txt)) || (lu && strstr(lu, v->t_filter_txt)) ||
             (lc && strstr(lc, v->t_filter_txt)) || !strcmp(ps, v->t_filter_txt);
        g_free(ln); g_free(lu); g_free(lc);
    }
    g_free(name); g_free(user); g_free(cmd);
    return ok;
}

static void on_filter(GtkEditable *e, gpointer d)
{
    pv *v = d;
    gchar *t = g_ascii_strdown(gtk_entry_get_text(GTK_ENTRY(e)), -1);
    g_strstrip(t);
    snprintf(v->t_filter_txt, sizeof v->t_filter_txt, "%s", t);
    g_free(t);
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(v->t_filter));
    gtk_tree_view_expand_all(GTK_TREE_VIEW(v->t_view));
}

static const char *detail_keys[] = {
    "Name:", "PID:", "Parent PID:", "User:", "Status:", "Started:", "CPU Time:", "Threads:",
    "Priority / Nice:", "Memory (Resident):", "Shared Memory:", "Virtual Size:", "Disk Read Total:", "Disk Written Total:",
    "GPU:", "VRAM:", "Unit:", "Session:", "Executable:", "Kind:", NULL
};
enum { DK_N = 20 };

static void update_proc_detail(pv *v)
{
    const pd_snap *s = &v->v;
    int i = v->t_sel_pid > 0 ? pd_find(s, v->t_sel_pid) : -1;
    char val[DK_N][200], b[64];
    const pd_proc *p;
    if (i < 0) {
        gtk_widget_hide(v->t_detail_box);
        gtk_widget_show(v->t_hint);
        return;
    }
    p = &s->proc[i];
    snprintf(val[0], sizeof val[0], "%s", p->name);
    snprintf(val[1], sizeof val[1], "%d", p->pid);
    snprintf(val[2], sizeof val[2], "%d", p->ppid);
    snprintf(val[3], sizeof val[3], "%s (%d)", p->user, p->uid);
    snprintf(val[4], sizeof val[4], "%s", pstate_name(p->state));
    {
        double ago = s->uptime - (double)p->start / (double)sysconf(_SC_CLK_TCK);
        time_t t = time(NULL) - (time_t)ago;
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(val[5], sizeof val[5], "%Y-%m-%d %H:%M:%S", &tm);
    }
    pd_fmt_uptime(p->cpu_s, b, sizeof b);
    snprintf(val[6], sizeof val[6], "%s (%.1f %% now)", b, perf_prefs_cpu_value(p->cpu, s->ncpu));
    snprintf(val[7], sizeof val[7], "%d", p->threads);
    snprintf(val[8], sizeof val[8], "%d / %d", p->prio, p->nice);
    pd_fmt_bytes((double)p->rss, val[9], sizeof val[9]);
    pd_fmt_bytes((double)p->shared, val[10], sizeof val[10]);
    pd_fmt_bytes((double)p->vsize, val[11], sizeof val[11]);
    if (p->rd_total >= 0) pd_fmt_bytes((double)p->rd_total, val[12], sizeof val[12]); else snprintf(val[12], sizeof val[12], EM " (another user's process)");
    if (p->wr_total >= 0) pd_fmt_bytes((double)p->wr_total, val[13], sizeof val[13]); else snprintf(val[13], sizeof val[13], EM);
    if (p->gpu >= 0) snprintf(val[14], sizeof val[14], "%d %%", p->gpu); else snprintf(val[14], sizeof val[14], "Not using a GPU");
    if (p->vram >= 0) pd_fmt_bytes((double)p->vram, val[15], sizeof val[15]); else snprintf(val[15], sizeof val[15], EM);
    snprintf(val[16], sizeof val[16], "%s", p->unit[0] ? p->unit : EM);
    if (p->session >= 0) snprintf(val[17], sizeof val[17], "%d", p->session); else snprintf(val[17], sizeof val[17], EM);
    {
        char path[64], tgt[180];
        ssize_t n;
        snprintf(path, sizeof path, "/proc/%d/exe", p->pid);
        n = readlink(path, tgt, sizeof tgt - 1);
        if (n > 0) { tgt[n] = 0; snprintf(val[18], sizeof val[18], "%s", tgt); }
        else snprintf(val[18], sizeof val[18], EM);
    }
    snprintf(val[19], sizeof val[19], "%s", p->kthread ? "Kernel thread" : p->is_app ? "Application" : "Background process");
    for (int k = 0; k < DK_N; k++) set_label(v->t_detail[k], val[k]);
    set_label(v->t_detail_cmd, p->cmd);
    gtk_widget_hide(v->t_hint);
    gtk_widget_show(v->t_detail_box);
}

/* the selected processes (group rows left out), for the actions; the first one also fills the detail pane */
static int proc_sel(void *user, int *out, int max)
{
    pv *v = user;
    GtkTreeModel *m;
    GList *rows;
    int n = 0;
    if (!v->t_view) return 0;   /* the actions are made before the list */
    rows = gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->t_view)), &m);
    for (GList *r = rows; r && n < max; r = r->next) {
        GtkTreeIter it;
        int pid;
        if (!gtk_tree_model_get_iter(m, &it, r->data) || row_kind(m, &it)) continue;
        gtk_tree_model_get(m, &it, PC_PID, &pid, -1);
        out[n++] = pid;
    }
    g_list_free_full(rows, (GDestroyNotify)gtk_tree_path_free);
    return n;
}

static const pd_snap *proc_snap(void *user) { return &((pv *)user)->v; }
static int top_sel(void *user, int *out, int max) { pv *v = user; if (max > 0 && v->top_menu_pid > 0) { out[0] = v->top_menu_pid; return 1; } return 0; }

static void on_proc_sel(GtkTreeSelection *sel, gpointer d)
{
    pv *v = d;
    int p[1];
    (void)sel;
    v->t_sel_pid = proc_sel(v, p, 1) ? p[0] : 0;
    if (v->v.seq) update_proc_detail(v);
    if (v->pp) pp_update(v->pp);
}

/* right click: the row under the pointer joins / becomes the selection, then the actions menu */
static gboolean on_proc_press(GtkWidget *w, GdkEventButton *ev, gpointer d)
{
    pv *v = d;
    GtkTreePath *p = NULL;
    if (ev->type != GDK_BUTTON_PRESS || ev->button != 3) return FALSE;
    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(w), (gint)ev->x, (gint)ev->y, &p, NULL, NULL, NULL)) {
        GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(w));
        if (!gtk_tree_selection_path_is_selected(sel, p)) { gtk_tree_selection_unselect_all(sel); gtk_tree_selection_select_path(sel, p); }
        gtk_tree_path_free(p);
    }
    pp_popup(v->pp, w, (GdkEvent *)ev);
    return TRUE;
}

static gboolean on_proc_menu_key(GtkWidget *w, gpointer d) { pp_popup(((pv *)d)->pp, w, NULL); return TRUE; }
static gboolean on_proc_key(GtkWidget *w, GdkEventKey *ev, gpointer d) { (void)w; return pp_key(((pv *)d)->pp, ev); }

/* Summary: right click on a Top CPU Processes row = the same actions for that process */
static gboolean on_top_press(GtkWidget *w, GdkEventButton *ev, gpointer d)
{
    pv *v = d;
    int row;
    if (ev->type != GDK_BUTTON_PRESS || ev->button != 3 || !v->pp_top || v->top_rh <= 0) return FALSE;
    row = (int)((ev->y - v->top_y0 + 1) / v->top_rh);
    if (ev->y < v->top_y0 - 1 || row < 0 || row >= v->ntop) return FALSE;
    v->top_menu_pid = v->top_pid[row];
    pp_update(v->pp_top);
    pp_popup(v->pp_top, w, (GdkEvent *)ev);
    return TRUE;
}

static GtkTreeViewColumn *tcol(pv *v, const char *t, int col, int num, int width, const char *tip)
{
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c;
    if (num) {
        g_object_set(r, "xalign", 1.0, "attributes", TNUM, NULL);
        c = gtk_tree_view_column_new_with_attributes(t, r, NULL);
        gtk_tree_view_column_set_cell_data_func(c, r, cell_text_num, GINT_TO_POINTER(col), NULL);
        gtk_tree_view_column_set_alignment(c, 1.0);
    } else {
        g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
        c = gtk_tree_view_column_new_with_attributes(t, r, "text", col, NULL);
        if (col == PC_NAME) gtk_tree_view_column_set_cell_data_func(c, r, cell_name, NULL, NULL);
    }
    gtk_tree_view_column_set_sort_column_id(c, col);
    gtk_tree_view_column_set_resizable(c, TRUE);
    if (width > 0) { gtk_tree_view_column_set_sizing(c, GTK_TREE_VIEW_COLUMN_FIXED); gtk_tree_view_column_set_fixed_width(c, width); }
    gtk_tree_view_append_column(GTK_TREE_VIEW(v->t_view), c);
    if (tip) {
        GtkWidget *l = gtk_label_new(t);
        gtk_widget_set_tooltip_text(l, tip);
        gtk_widget_show(l);
        gtk_tree_view_column_set_widget(c, l);
    }
    return c;
}

static GtkWidget *build_procs(pv *v)
{
    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6), *rb, *sw, *pan, *dg, *ds;
    GtkTreeIter it;
    gtk_container_set_border_width(GTK_CONTAINER(b), 8);
    gtk_box_pack_start(GTK_BOX(b), page_header("Processes", &rb), FALSE, FALSE, 0);
    v->t_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(v->t_entry), "Filter by name, user, or PID");
    gtk_entry_set_width_chars(GTK_ENTRY(v->t_entry), 32);
    g_signal_connect(v->t_entry, "search-changed", G_CALLBACK(on_filter), v);
    gtk_box_pack_end(GTK_BOX(rb), v->t_entry, FALSE, FALSE, 0);
    v->pp = pp_ui_new(proc_sel, proc_snap, v);
    gtk_box_pack_start(GTK_BOX(b), pp_toolbar(v->pp), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(b), pp_infobar(v->pp), FALSE, FALSE, 0);

    v->t_store = gtk_tree_store_new(PC_N, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_DOUBLE, G_TYPE_INT64,
                                    G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT);
    gtk_tree_store_insert_with_values(v->t_store, &it, NULL, -1, PC_NAME, "Applications (0)", PC_KIND, 1, PC_PID, 0, -1);
    v->t_apps = it;
    gtk_tree_store_insert_with_values(v->t_store, &it, NULL, -1, PC_NAME, "Background Processes (0)", PC_KIND, 2, PC_PID, 0, -1);
    v->t_bg = it;
    v->t_filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(v->t_store), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(v->t_filter), visible_func, v, NULL);
    v->t_sort = gtk_tree_model_sort_new_with_model(v->t_filter);
    for (int c = 0; c < PC_KIND; c++) gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(v->t_sort), c, sort_func, v, NULL);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(v->t_sort), PC_CPU, GTK_SORT_DESCENDING);
    v->t_view = gtk_tree_view_new_with_model(v->t_sort);
    g_object_unref(v->t_sort);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(v->t_view), FALSE);
    tcol(v, "Name", PC_NAME, 0, 260, NULL);
    tcol(v, "PID", PC_PID, 1, 0, NULL);
    tcol(v, "Status", PC_STATUS, 0, 90, NULL);
    tcol(v, "User", PC_USER, 0, 90, NULL);
    tcol(v, "CPU", PC_CPU, 1, 0, "Share of the CPU: the whole machine = 100 %, or one logical processor = 100 % with Per Core Units (Settings)");
    tcol(v, "Memory", PC_MEM, 1, 0, "Resident memory (RSS)");
    tcol(v, "Disk Read", PC_RD, 1, 0, "Bytes per second read from storage; — = another user's process (root-only)");
    tcol(v, "Disk Write", PC_WR, 1, 0, "Bytes per second written to storage; — = another user's process (root-only)");
    tcol(v, "GPU", PC_GPU, 1, 0, "Busiest GPU engine used by the process (DRM fdinfo); — = no GPU use");
    tcol(v, "Threads", PC_THR, 1, 0, NULL);
    tcol(v, "Nice", PC_NICE, 1, 0, NULL);
    gtk_tree_view_column_set_expand(tcol(v, "Command", PC_CMD, 0, 0, NULL), TRUE);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->t_view)), GTK_SELECTION_MULTIPLE);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->t_view)), "changed", G_CALLBACK(on_proc_sel), v);
    g_signal_connect(v->t_view, "button-press-event", G_CALLBACK(on_proc_press), v);
    g_signal_connect(v->t_view, "popup-menu", G_CALLBACK(on_proc_menu_key), v);
    g_signal_connect(v->t_view, "key-press-event", G_CALLBACK(on_proc_key), v);
    {
        const char *w = getenv("SRS_PERF_SELECT_PID");   /* test hook: select this process once it is listed */
        v->t_want_pid = w ? atoi(w) : 0;
    }
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(sw), v->t_view);
    pan = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_pack1(GTK_PANED(pan), sw, TRUE, FALSE);

    ds = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(ds), 6);
    v->t_hint = label("Select a process for its details.", 0.5, "pv-dim");
    gtk_box_pack_start(GTK_BOX(ds), v->t_hint, TRUE, TRUE, 10);
    v->t_detail_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    dg = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(dg), 10);
    gtk_grid_set_row_spacing(GTK_GRID(dg), 1);
    for (int k = 0; k < DK_N; k++) {
        int col = (k / 5) * 2, row = k % 5;
        GtkWidget *kl = label(detail_keys[k], 0, "pv-dim");
        v->t_detail[k] = label("", 0, "pv-num");
        gtk_label_set_selectable(GTK_LABEL(v->t_detail[k]), TRUE);
        gtk_widget_set_can_focus(v->t_detail[k], FALSE);
        gtk_label_set_ellipsize(GTK_LABEL(v->t_detail[k]), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_max_width_chars(GTK_LABEL(v->t_detail[k]), 34);
        gtk_widget_set_hexpand(v->t_detail[k], TRUE);
        gtk_grid_attach(GTK_GRID(dg), kl, col, row, 1, 1);
        gtk_grid_attach(GTK_GRID(dg), v->t_detail[k], col + 1, row, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(v->t_detail_box), dg, FALSE, FALSE, 0);
    {
        GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_pack_start(GTK_BOX(h), label("Command Line:", 0, "pv-dim"), FALSE, FALSE, 0);
        v->t_detail_cmd = label("", 0, NULL);
        gtk_label_set_selectable(GTK_LABEL(v->t_detail_cmd), TRUE);
        gtk_widget_set_can_focus(v->t_detail_cmd, FALSE);
        gtk_label_set_line_wrap(GTK_LABEL(v->t_detail_cmd), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(v->t_detail_cmd), PANGO_WRAP_CHAR);
        gtk_label_set_lines(GTK_LABEL(v->t_detail_cmd), 2);
        gtk_label_set_ellipsize(GTK_LABEL(v->t_detail_cmd), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(h), v->t_detail_cmd, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(v->t_detail_box), h, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(ds), v->t_detail_box, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(v->t_detail_box, TRUE);
    gtk_widget_show_all(v->t_detail_box);
    gtk_widget_hide(v->t_detail_box);
    gtk_paned_pack2(GTK_PANED(pan), ds, FALSE, FALSE);
    gtk_box_pack_start(GTK_BOX(b), pan, TRUE, TRUE, 0);
    v->t_rows = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    return b;
}

static int kb_round(double r) { return r < 0 ? -1 : (int)(r / 1024 + 0.5); }

/* places one process row (re-adding it when its group or parent changed) and sets changed values */
/* rows nested under this pid went out of the store with it: forget them too */
static gboolean child_of(gpointer k, gpointer val, gpointer pid) { (void)k; return ((prow *)val)->parent == GPOINTER_TO_INT(pid); }

static void drop_row(pv *v, int pid, prow *r)
{
    g_hash_table_foreach_remove(v->t_rows, child_of, GINT_TO_POINTER(pid));
    gtk_tree_store_remove(v->t_store, &r->it);
    g_hash_table_remove(v->t_rows, GINT_TO_POINTER(pid));
}

static void proc_row(pv *v, const pd_proc *p, int group, int parent)
{
    prow *r = g_hash_table_lookup(v->t_rows, GINT_TO_POINTER(p->pid));
    int cpu10 = (int)(p->cpu * 10 + 0.5), rd = kb_round(p->rd), wr = kb_round(p->wr);
    if (r && (r->group != group || r->parent != parent || r->start != p->start || strcmp(r->name, p->name) || strcmp(r->cmd, p->cmd))) {
        drop_row(v, p->pid, r);   /* moved, or another process now (reused PID, exec): a new row */
        r = NULL;
    }
    if (!r) {
        GtkTreeIter *par = group == 1 ? &v->t_apps : &v->t_bg;
        prow *pr = parent ? g_hash_table_lookup(v->t_rows, GINT_TO_POINTER(parent)) : NULL;
        if (pr) par = &pr->it;
        r = g_new0(prow, 1);
        r->group = group;
        r->parent = parent;
        r->start = p->start;
        snprintf(r->name, sizeof r->name, "%s", p->name);
        snprintf(r->cmd, sizeof r->cmd, "%s", p->cmd);
        gtk_tree_store_insert_with_values(v->t_store, &r->it, par, -1,
            PC_NAME, p->name, PC_PID, p->pid, PC_STATUS, pstate_name(p->state), PC_USER, p->user, PC_CPU, p->cpu,
            PC_MEM, (gint64)p->rss, PC_RD, p->rd, PC_WR, p->wr, PC_GPU, p->gpu, PC_THR, p->threads, PC_NICE, p->nice,
            PC_CMD, p->cmd, PC_KIND, 0, -1);
        g_hash_table_insert(v->t_rows, GINT_TO_POINTER(p->pid), r);
    } else if (r->state != p->state || r->cpu10 != cpu10 || r->gpu != p->gpu || r->thr != p->threads || r->nice != p->nice ||
               r->mem / 65536 != p->rss / 65536 || r->rd != rd || r->wr != wr) {
        gtk_tree_store_set(v->t_store, &r->it, PC_STATUS, pstate_name(p->state), PC_CPU, p->cpu, PC_MEM, (gint64)p->rss,
                           PC_RD, p->rd, PC_WR, p->wr, PC_GPU, p->gpu, PC_THR, p->threads, PC_NICE, p->nice, -1);
    }
    r->gen = v->t_gen;
    r->state = p->state; r->cpu10 = cpu10; r->gpu = p->gpu; r->thr = p->threads; r->nice = p->nice;
    r->mem = p->rss; r->rd = rd; r->wr = wr;
}

static gboolean dead_nested(gpointer k, gpointer val, gpointer d)
{
    pv *v = d;
    prow *r = val;
    (void)k;
    if (r->gen == v->t_gen || !r->parent) return FALSE;
    gtk_tree_store_remove(v->t_store, &r->it);
    return TRUE;
}

static gboolean dead_top(gpointer k, gpointer val, gpointer d)
{
    pv *v = d;
    prow *r = val;
    (void)k;
    if (r->gen == v->t_gen) return FALSE;
    gtk_tree_store_remove(v->t_store, &r->it);
    return TRUE;
}

static void update_procs(pv *v)
{
    const pd_snap *s = &v->v;
    char b[64];
    int first = !v->t_filled;
    v->t_gen++;
    /* pass 1: rows at the top of their group; pass 2: applications started by another application */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < s->nproc; i++) {
            const pd_proc *p = &s->proc[i];
            int parent = 0, group = p->is_app ? 1 : 2;
            if (p->is_app) {
                int j = pd_find(s, p->ppid);
                if (j >= 0 && s->proc[j].is_app) {
                    int jj = pd_find(s, s->proc[j].ppid);
                    if (jj < 0 || !s->proc[jj].is_app) parent = p->ppid;   /* one level deep */
                }
            }
            if ((pass == 0) != (parent == 0)) continue;
            proc_row(v, p, group, parent);
        }
    }
    g_hash_table_foreach_remove(v->t_rows, dead_nested, v);
    g_hash_table_foreach_remove(v->t_rows, dead_top, v);
    snprintf(b, sizeof b, "Applications (%d)", s->napps);
    gtk_tree_store_set(v->t_store, &v->t_apps, PC_NAME, b, -1);
    snprintf(b, sizeof b, "Background Processes (%d)", s->nproc - s->napps);
    gtk_tree_store_set(v->t_store, &v->t_bg, PC_NAME, b, -1);
    v->t_filled = 1;
    if (first) {
        GtkTreePath *pa;
        pa = gtk_tree_path_new_from_indices(0, -1);
        gtk_tree_view_expand_row(GTK_TREE_VIEW(v->t_view), pa, TRUE);
        gtk_tree_path_free(pa);
        pa = gtk_tree_path_new_from_indices(1, -1);
        gtk_tree_view_expand_row(GTK_TREE_VIEW(v->t_view), pa, FALSE);
        gtk_tree_path_free(pa);
    }
    if (v->t_want_pid) {
        prow *r = g_hash_table_lookup(v->t_rows, GINT_TO_POINTER(v->t_want_pid));
        GtkTreeIter fi, si;
        if (r && gtk_tree_model_filter_convert_child_iter_to_iter(GTK_TREE_MODEL_FILTER(v->t_filter), &fi, &r->it) &&
            gtk_tree_model_sort_convert_child_iter_to_iter(GTK_TREE_MODEL_SORT(v->t_sort), &si, &fi)) {
            GtkTreePath *pa = gtk_tree_model_get_path(v->t_sort, &si);
            gtk_tree_view_expand_to_path(GTK_TREE_VIEW(v->t_view), pa);
            gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->t_view)));
            gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->t_view)), pa);
            gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(v->t_view), pa, NULL, TRUE, 0.3, 0);
            gtk_widget_grab_focus(v->t_view);
            gtk_tree_path_free(pa);
            v->t_want_pid = 0;
        }
    }
    update_proc_detail(v);
    pp_update(v->pp);
}

/* ------------------------------------------------------------------ navigation, timer, lifetime */

static void refresh_page(pv *v)
{
    if (!v->v.seq) return;
    switch (v->page) {
    case PG_SUMMARY: update_summary(v); break;
    case PG_PERF: update_perf(v); break;
    case PG_PROCS: update_procs(v); break;
    case PG_USERS: pa_users_update(v->users_page, &v->v); break;
    default: break;
    }
}

/* one ring of the view's history: either the samples the hub took meanwhile (same sampler) or a gap of NAN samples
   (paused: nothing was sampled), then the new value; the view's copy then shows the history ring */
static void ring_step(pd_ring *h, pd_ring *view, const pd_ring *src, int missed, int gap)
{
    for (int g = 0; g < gap; g++) pd_ring_push(h, NAN);
    if (missed > src->n) missed = src->n;
    for (int k = missed; k >= 1; k--) pd_ring_push(h, pd_ring_at(src, src->n - k));
    *view = *h;
}

static void hist_merge(pv *v, const pd_snap *s)
{
    pd_snap *H = v->hist, *V = &v->v;
    int missed = v->last_seq ? 1 : PD_HIST, gap = 0;   /* first time: take whatever history the hub already has */
    if (!H) H = v->hist = g_new0(pd_snap, 1);
    if (v->last_seq && s->seq > v->last_seq) missed = (int)MIN(s->seq - v->last_seq, (unsigned long long)PD_HIST);
    else if (v->last_seq) {   /* the hub started a new sampler while the tab was hidden: mark the pause */
        double secs = v->paused_at ? (g_get_monotonic_time() - v->paused_at) / 1e6 : 1;
        gap = (int)(secs * 1000 / perf_prefs_interval_ms() + 0.5);
        if (gap < 1) gap = 1;
        if (gap > 8) gap = 8;
    }
    v->last_seq = s->seq;
    v->paused_at = 0;
#define R(f) ring_step(&H->f, &V->f, &s->f, missed, gap)
    R(h_cpu); R(h_kernel); R(h_mhz); R(h_ctemp); R(h_cpu_w); R(h_mem); R(h_swap); R(h_psi); R(h_disk); R(h_net); R(h_gpu);
    for (int i = 0; i < s->ncpu && i < PD_MAX_CPU; i++) {
        if (H->cpu_id[i] != s->cpu_id[i]) { memset(&H->hu[i], 0, sizeof H->hu[i]); memset(&H->hk[i], 0, sizeof H->hk[i]); H->cpu_id[i] = s->cpu_id[i]; }
        R(hu[i]); R(hk[i]);
    }
    for (int i = 0; i < s->ndisk; i++) {
        if (strcmp(H->disk[i].name, s->disk[i].name)) { memset(&H->disk[i], 0, sizeof H->disk[i]); snprintf(H->disk[i].name, sizeof H->disk[i].name, "%s", s->disk[i].name); }
        R(disk[i].h_busy); R(disk[i].h_rd); R(disk[i].h_wr);
    }
    for (int i = 0; i < s->nnet; i++) {
        if (strcmp(H->net[i].name, s->net[i].name)) { memset(&H->net[i], 0, sizeof H->net[i]); snprintf(H->net[i].name, sizeof H->net[i].name, "%s", s->net[i].name); }
        R(net[i].h_rx); R(net[i].h_tx);
    }
    for (int i = 0; i < s->ngpu; i++) {
        if (strcmp(H->gpu[i].dev.pci, s->gpu[i].dev.pci)) { memset(&H->gpu[i], 0, sizeof H->gpu[i]); snprintf(H->gpu[i].dev.pci, sizeof H->gpu[i].dev.pci, "%s", s->gpu[i].dev.pci); }
        R(gpu[i].h_busy); R(gpu[i].h_vram); R(gpu[i].h_power); R(gpu[i].h_temp);
    }
    for (int i = 0; i < s->ntemp; i++) {
        if (strcmp(H->temp[i].label, s->temp[i].label)) { memset(&H->temp[i], 0, sizeof H->temp[i]); snprintf(H->temp[i].label, sizeof H->temp[i].label, "%s", s->temp[i].label); }
        R(temp[i].h);
    }
#undef R
}

static void on_snap(const pd_snap *s, void *user)
{
    pv *v = user;
    pd_snap_copy(&v->v, s);
    hist_merge(v, s);
    if (s->ncpu > 0) g_ncpu = s->ncpu;
    refresh_page(v);
}

/* subscribe while the tab is mapped and its window is not minimised */
static void sync_sub(pv *v)
{
    GtkWidget *top = gtk_widget_get_toplevel(v->root);
    GdkWindow *gw = gtk_widget_get_window(top);
    int iconic = gw && (gdk_window_get_state(gw) & GDK_WINDOW_STATE_ICONIFIED);
    int want = v->mapped && !iconic;
    if (want && !v->sub) v->sub = perf_hub_subscribe(on_snap, v, 1);
    else if (!want && v->sub) { perf_hub_unsubscribe(v->sub); v->sub = 0; v->paused_at = g_get_monotonic_time(); }
}

static gboolean tick(gpointer d) { sync_sub(d); return G_SOURCE_CONTINUE; }

static void on_prefs(void *user)
{
    pv *v = user;
    if (v->v.seq) refresh_page(v);
}

static void on_map(GtkWidget *w, gpointer d)
{
    pv *v = d;
    (void)w;
    v->mapped = 1;
    sync_sub(v);
    if (!v->tick) v->tick = g_timeout_add_seconds(1, tick, v);
}

static void on_unmap(GtkWidget *w, gpointer d)
{
    pv *v = d;
    (void)w;
    v->mapped = 0;
    if (v->tick) { g_source_remove(v->tick); v->tick = 0; }
    sync_sub(v);
}

/* open views (one per Big Top window) and a page asked for before any exists */
static GList *g_views;
static char g_want_page[24];

static void on_destroy(GtkWidget *w, gpointer d)
{
    pv *v = d;
    (void)w;
    g_views = g_list_remove(g_views, v);
    on_unmap(NULL, v);
    /* the children are destroyed after this handler: nothing of theirs may call back into the freed view */
    if (v->t_view) {
        g_signal_handlers_disconnect_by_data(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->t_view)), v);
        g_signal_handlers_disconnect_by_data(v->t_view, v);
    }
    if (v->s_top) g_signal_handlers_disconnect_by_data(v->s_top, v);
    if (v->nav) g_signal_handlers_disconnect_by_data(v->nav, v);
    if (v->p_list) g_signal_handlers_disconnect_by_data(v->p_list, v);
    perf_prefs_unlisten(on_prefs, v);
    if (v->pp_top) { GtkWidget *t = pp_toolbar(v->pp_top); gtk_widget_destroy(t); g_object_unref(t); v->pp_top = NULL; }
    pd_snap_free(&v->v);
    g_free(v->hist);   /* only rings: no process buffer */
    if (v->t_rows) g_hash_table_destroy(v->t_rows);
    if (v->t_store) g_object_unref(v->t_store);
    if (v->t_filter) g_object_unref(v->t_filter);
    g_free(v->p_cpu);
    g_free(v);
}

static void on_nav(GtkListBox *lb, GtkListBoxRow *row, gpointer d)
{
    pv *v = d;
    static const char *const names[PG_N] = { "summary", "performance", "processes", "startup", "users", "services" };
    (void)lb;
    if (!row) return;
    v->page = gtk_list_box_row_get_index(row);
    if (v->page < 0 || v->page >= PG_N) v->page = 0;
    {   /* the user guide section of this page, for the GPU Top window's Guide button / F1 */
        static const char *const guide[PG_N] = { "perf-summary", "perf-performance", "perf-processes", "perf-startup-apps",
                                                 "perf-users", "perf-services" };
        g_object_set_data(G_OBJECT(v->root), "srs-guide-anchor", (gpointer)guide[v->page]);
    }
    gtk_stack_set_visible_child_name(GTK_STACK(v->stack), names[v->page]);
    refresh_page(v);
}

static void nav_add(pv *v, const char *const *icons, const char *text_)
{
    GtkWidget *row = gtk_list_box_row_new(), *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(h), themed_image(icons, 3, 16), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(h), label(text_, 0, NULL), TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(row), h);
    gtk_list_box_insert(GTK_LIST_BOX(v->nav), row, -1);
}

/* (g_views, g_want_page: see perf_view_request_page) */

static int page_index(const char *name)
{
    static const char *const names[PG_N] = { "summary", "performance", "processes", "startup", "users", "services" };
    for (int i = 0; name && i < PG_N; i++) if (!strcmp(name, names[i])) return i;
    return -1;
}

void perf_view_request_page(const char *name)
{
    int i = page_index(name);
    if (i < 0) return;
    if (!g_views) { snprintf(g_want_page, sizeof g_want_page, "%s", name); return; }
    for (GList *l = g_views; l; l = l->next) {
        pv *v = l->data;
        gtk_list_box_select_row(GTK_LIST_BOX(v->nav), gtk_list_box_get_row_at_index(GTK_LIST_BOX(v->nav), i));
    }
}

GtkWidget *perf_view_new(void)
{
    pv *v = g_new0(pv, 1);
    static const char *const ic[PG_N][3] = {
        { "go-home-symbolic", "user-home-symbolic", "go-home" },
        { "utilities-system-monitor-symbolic", "org.gnome.SystemMonitor-symbolic", "utilities-system-monitor" },
        { "view-list-symbolic", "view-list-details-symbolic", "view-list" },
        { "system-run-symbolic", "media-playback-start-symbolic", "system-run" },
        { "system-users-symbolic", "avatar-default-symbolic", "system-users" },
        { "preferences-system-symbolic", "applications-system-symbolic", "preferences-system" },
    };
    static const char *const titles[PG_N] = { "Summary", "Performance", "Processes", "Startup Apps", "Users", "Services" };
    static const char *const names[PG_N] = { "summary", "performance", "processes", "startup", "users", "services" };
    const char *want = getenv("SRS_PERF_PAGE");
    GtkWidget *sw, *sep;
    int first = 0;
    fonts_init();
    ensure_css();
    v->want_res = getenv("SRS_PERF_RES");
    v->root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(v->root), "srs-perf");
    v->nav = gtk_list_box_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(v->nav), "pv-nav");
    for (int i = 0; i < PG_N; i++) nav_add(v, ic[i], titles[i]);
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw), v->nav);
    gtk_widget_set_size_request(sw, 150, -1);
    gtk_box_pack_start(GTK_BOX(v->root), sw, FALSE, FALSE, 0);
    sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(v->root), sep, FALSE, FALSE, 0);
    v->stack = gtk_stack_new();
    v->pages[PG_SUMMARY] = build_summary(v);
    v->pages[PG_PERF] = build_perf(v);
    v->pages[PG_PROCS] = build_procs(v);
    v->pages[PG_STARTUP] = pa_startup_new();
    v->pages[PG_USERS] = v->users_page = pa_users_new();
    v->pages[PG_SERVICES] = pa_services_new();
    for (int i = 0; i < PG_N; i++) gtk_stack_add_named(GTK_STACK(v->stack), v->pages[i], names[i]);
    gtk_box_pack_start(GTK_BOX(v->root), v->stack, TRUE, TRUE, 0);
    first = perf_prefs_get()->start_page;   /* Task Manager setting: Start Page (same order as here) */
    if (first < 0 || first >= PG_N) first = 0;
    if (want) for (int i = 0; i < PG_N; i++) if (!strcmp(want, names[i])) first = i;
    if (g_want_page[0] && page_index(g_want_page) >= 0) { first = page_index(g_want_page); g_want_page[0] = 0; }
    g_views = g_list_prepend(g_views, v);
    perf_prefs_listen(on_prefs, v);
    g_signal_connect(v->nav, "row-selected", G_CALLBACK(on_nav), v);
    g_signal_connect(v->root, "map", G_CALLBACK(on_map), v);
    g_signal_connect(v->root, "unmap", G_CALLBACK(on_unmap), v);
    g_signal_connect(v->root, "destroy", G_CALLBACK(on_destroy), v);
    gtk_widget_show_all(v->root);
    gtk_list_box_select_row(GTK_LIST_BOX(v->nav), gtk_list_box_get_row_at_index(GTK_LIST_BOX(v->nav), first));
    return v->root;
}
