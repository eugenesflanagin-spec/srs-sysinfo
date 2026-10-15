/* sec_sensors.c - SIV sensor sections on Linux:
 * [sensors] [hw-status] [cooling] [power] [kwh-cost] [battery] [thermal-zones] [synthetic]
 * [formula-setup] [sen-export] [pwm-export] [ups-export] [asus-info] [asus-wmi] [atk-debug]
 * [ldn-info] [sio-dbg] [ipmi-*] [link-*] [aio-export] [lcd-panel]
 * Sources: every /sys/class/hwmon chip, /sys/class/powercap (RAPL), zenergy, /sys/class/power_supply,
 * /sys/class/thermal, /etc/sensors3.conf + /etc/sensors.d files (lm-sensors label/compute, read only),
 * /sys/bus/wmi, /sys/bus/acpi, modinfo aliases.
 * Strictly read-only: pwm / pwm_enable are only read; no i2c, hidraw, /dev/port or IPMI access. */
#define _GNU_SOURCE
#include "sec.h"

#include <ctype.h>
#include <fnmatch.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* GUI hooks for SIV's Sensors panel buttons (the coordinator adds these prototypes to sec.h):
 *   sensors_zero()               - SIV "Zero": reset the tracked minimum / maximum columns
 *   sensors_set_hide_unused(on)  - SIV "Hide": hide sensors reading 0 or not connected (floating) */
void sensors_zero(void);
void sensors_set_hide_unused(int on);

#define HWMON "/sys/class/hwmon"

static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static int g_hide;
static time_t g_zero_t;

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int rd_dbl(const char *path, double *v)
{
    char b[96], *e;
    if (!rd_str(path, b, sizeof b) || !*b) return 0;
    *v = strtod(b, &e);
    return e != b;
}
static int hw_dbl(const char *dir, const char *attr, double *v)
{
    char p[600];
    snprintf(p, sizeof p, "%s/%s", dir, attr);
    return rd_dbl(p, v);
}
static int hw_str(const char *dir, const char *attr, char *b, size_t sz)
{
    char p[600];
    snprintf(p, sizeof p, "%s/%s", dir, attr);
    return rd_str(p, b, sz);
}
static int hw_has(const char *dir, const char *attr)
{
    char p[600];
    snprintf(p, sizeof p, "%s/%s", dir, attr);
    return path_exists(p);
}

/* ======================================================================
 * lm-sensors configuration (label / compute / set / ignore), read only
 * ====================================================================== */
typedef struct { char pat[8][64]; int np; char src[96]; } cfg_chip;
typedef struct { int chip; char kw[10]; char feat[32]; char a[200]; char b[200]; char src[96]; } cfg_st;
static cfg_chip g_cc[256];
static cfg_st g_cs[1500];
static int g_ncc, g_ncs;
static char g_cfg_files[8][160];
static int g_ncfg_files;

static const char *cfg_tok(const char *p, char *out, size_t sz)
{
    size_t n = 0;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) { *out = 0; return NULL; }
    if (*p == '"') {
        p++;
        while (*p && *p != '"') { if (n + 1 < sz) out[n++] = *p; p++; }
        if (*p == '"') p++;
    } else {
        while (*p && !isspace((unsigned char)*p)) { if (n + 1 < sz) out[n++] = *p; p++; }
    }
    out[n] = 0;
    return p;
}

static void cfg_parse_file(const char *path)
{
    char *txt, *p, *line, *save = NULL, shortname[64];
    int ln = 0, cur = -1;
    if (rd_all(path, &txt) < 0) return;
    if (g_ncfg_files < 8) snprintf(g_cfg_files[g_ncfg_files++], sizeof g_cfg_files[0], "%s", path);
    snprintf(shortname, sizeof shortname, "%s", strrchr(path, '/') ? strrchr(path, '/') + 1 : path);
    /* join backslash continuations */
    for (p = txt; *p; p++)
        if (*p == '\\' && p[1] == '\n') { p[0] = ' '; p[1] = ' '; }
    for (line = strtok_r(txt, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char kw[32], feat[64], *h;
        const char *r;
        int inq = 0;
        ln++;
        for (h = line; *h; h++) { if (*h == '"') inq = !inq; if (*h == '#' && !inq) { *h = 0; break; } }
        r = cfg_tok(line, kw, sizeof kw);
        if (!r || !*kw) continue;
        if (!strcmp(kw, "chip")) {
            if (g_ncc >= 256) continue;
            cur = g_ncc++;
            g_cc[cur].np = 0;
            snprintf(g_cc[cur].src, sizeof g_cc[cur].src, "%s:%d", shortname, ln);
            while (r && g_cc[cur].np < 8) {
                r = cfg_tok(r, g_cc[cur].pat[g_cc[cur].np], sizeof g_cc[cur].pat[0]);
                if (*g_cc[cur].pat[g_cc[cur].np]) g_cc[cur].np++;
            }
            continue;
        }
        if (strcmp(kw, "label") && strcmp(kw, "compute") && strcmp(kw, "set") && strcmp(kw, "ignore")) continue;
        if (g_ncs >= (int)(sizeof g_cs / sizeof g_cs[0])) continue;
        r = cfg_tok(r, feat, sizeof feat);
        {
            cfg_st *s = &g_cs[g_ncs++];
            memset(s, 0, sizeof *s);
            s->chip = cur;
            snprintf(s->kw, sizeof s->kw, "%s", kw);
            snprintf(s->feat, sizeof s->feat, "%s", feat);
            snprintf(s->src, sizeof s->src, "%s:%d", shortname, ln);
            if (!r) continue;
            while (*r && isspace((unsigned char)*r)) r++;
            if (!strcmp(kw, "label")) cfg_tok(r, s->a, sizeof s->a);
            else if (!strcmp(kw, "compute")) {
                const char *c = strchr(r, ',');
                snprintf(s->a, sizeof s->a, "%.*s", c ? (int)(c - r) : (int)strlen(r), r);
                if (c) snprintf(s->b, sizeof s->b, "%s", c + 1);
            } else snprintf(s->a, sizeof s->a, "%s", r);
            /* trim */
            {
                char *x[2] = { s->a, s->b };
                int i;
                for (i = 0; i < 2; i++) {
                    char *q = x[i];
                    size_t n;
                    while (*q && isspace((unsigned char)*q)) memmove(q, q + 1, strlen(q));
                    n = strlen(q);
                    while (n && isspace((unsigned char)q[n - 1])) q[--n] = 0;
                }
            }
        }
    }
    free(txt);
}

static void cfg_load(void)
{
    char **names;
    int n, i;
    g_ncc = g_ncs = g_ncfg_files = 0;
    if (path_exists("/etc/sensors3.conf")) cfg_parse_file("/etc/sensors3.conf");
    else if (path_exists("/etc/sensors.conf")) cfg_parse_file("/etc/sensors.conf");
    n = dir_list("/etc/sensors.d", &names);
    for (i = 0; i < n; i++) {
        char p[400];
        if (names[i][0] == '.') continue;
        snprintf(p, sizeof p, "/etc/sensors.d/%s", names[i]);
        cfg_parse_file(p);
    }
    if (n > 0) dir_free(names, n);
}

static int cfg_chip_match(int ci, const char *cname)
{
    int i;
    if (ci < 0 || ci >= g_ncc) return 0;
    for (i = 0; i < g_cc[ci].np; i++)
        if (!fnmatch(g_cc[ci].pat[i], cname, 0)) return 1;
    return 0;
}

/* last matching statement wins (libsensors semantics) */
static const cfg_st *cfg_find(const char *cname, const char *kw, const char *feat)
{
    int i;
    const cfg_st *r = NULL;
    for (i = 0; i < g_ncs; i++)
        if (!strcmp(g_cs[i].kw, kw) && !strcmp(g_cs[i].feat, feat) && cfg_chip_match(g_cs[i].chip, cname)) r = &g_cs[i];
    return r;
}

/* compute expression evaluator: + - * / ( ) @ numbers, ^ = exp, ` = ln (lm-sensors syntax) */
typedef struct { const char *p; double at; int bad; } ev;
static double ev_expr(ev *e);
static void ev_ws(ev *e) { while (*e->p && isspace((unsigned char)*e->p)) e->p++; }
static double ev_fac(ev *e)
{
    char c;
    ev_ws(e);
    c = *e->p;
    if (c == '-') { e->p++; return -ev_fac(e); }
    if (c == '(') {
        double v;
        e->p++;
        v = ev_expr(e);
        ev_ws(e);
        if (*e->p == ')') e->p++; else e->bad = 1;
        return v;
    }
    if (c == '@') { e->p++; return e->at; }
    if (c == '^') { e->p++; return exp(ev_fac(e)); }
    if (c == '`') { e->p++; return log(ev_fac(e)); }
    if (isdigit((unsigned char)c) || c == '.') {
        char *end;
        double v = strtod(e->p, &end);
        e->p = end;
        return v;
    }
    e->bad = 1;   /* references to other features are not evaluated */
    while (*e->p && (isalnum((unsigned char)*e->p) || *e->p == '_')) e->p++;
    if (*e->p && !strchr("+-*/()", *e->p)) e->p++;
    return NAN;
}
static double ev_term(ev *e)
{
    double v = ev_fac(e);
    for (;;) {
        ev_ws(e);
        if (*e->p == '*') { e->p++; v *= ev_fac(e); }
        else if (*e->p == '/') { double d; e->p++; d = ev_fac(e); v = d != 0 ? v / d : NAN; }
        else return v;
    }
}
static double ev_expr(ev *e)
{
    double v = ev_term(e);
    for (;;) {
        ev_ws(e);
        if (*e->p == '+') { e->p++; v += ev_term(e); }
        else if (*e->p == '-') { e->p++; v -= ev_term(e); }
        else return v;
    }
}
static double eval_compute(const char *expr, double at, int *ok)
{
    ev e = { expr, at, 0 };
    double v = ev_expr(&e);
    ev_ws(&e);
    if (*e.p) e.bad = 1;
    *ok = !e.bad && !isnan(v);
    return v;
}

/* ======================================================================
 * energy counters -> watts (keeps the previous reading so live refreshes need no sleep)
 * ====================================================================== */
typedef struct { char path[320]; double e, t; } erec;
static erec g_er[256];
static int g_ner;

static erec *erec_find(const char *path, int add)
{
    int i;
    for (i = 0; i < g_ner; i++) if (!strcmp(g_er[i].path, path)) return &g_er[i];
    if (!add || g_ner >= 256) return NULL;
    snprintf(g_er[g_ner].path, sizeof g_er[0].path, "%s", path);
    g_er[g_ner].t = -1;
    return &g_er[g_ner++];
}

/* paths: energy counters in microjoules. w[] = watts (NAN unknown), j[] = joules now */
static void energy_sample(const char *const *paths, int n, double *w, double *j)
{
    double t0 = now_s(), *e0;
    int i, need = 0;
    if (n <= 0) return;
    e0 = calloc((size_t)n, sizeof *e0);
    if (!e0) return;
    for (i = 0; i < n; i++) {
        erec *r = erec_find(paths[i], 1);
        w[i] = NAN;
        j[i] = NAN;
        if (!rd_dbl(paths[i], &e0[i])) { e0[i] = NAN; continue; }
        j[i] = e0[i] / 1e6;
        if (r && r->t > 0 && t0 - r->t > 0.05 && t0 - r->t < 10 && e0[i] >= r->e)
            w[i] = (e0[i] - r->e) / 1e6 / (t0 - r->t);
        else need = 1;
    }
    if (need) {
        double t1;
        usleep(200000);
        t1 = now_s();
        for (i = 0; i < n; i++) {
            double e1;
            if (!isnan(w[i]) || isnan(e0[i]) || !rd_dbl(paths[i], &e1)) continue;
            if (e1 >= e0[i]) w[i] = (e1 - e0[i]) / 1e6 / (t1 - t0);
            j[i] = e1 / 1e6;
            e0[i] = e1;
        }
        t0 = t1;
    }
    for (i = 0; i < n; i++) {
        erec *r = erec_find(paths[i], 1);
        if (r && !isnan(e0[i])) { r->e = e0[i]; r->t = t0; }
    }
    free(e0);
}

/* ======================================================================
 * hwmon model
 * ====================================================================== */
enum { K_TEMP, K_FAN, K_IN, K_POWER, K_ENERGY, K_CURR, K_HUMID, K_FREQ, K_PWM, K_N };
static const char *const k_pfx[K_N] = { "temp", "fan", "in", "power", "energy", "curr", "humidity", "freq", "pwm" };
static const double k_div[K_N] = { 1000, 1, 1000, 1e6, 1e6, 1000, 1000, 1e6, 2.55 };
static const char *const k_name[K_N] = { "Temperatures", "Fans", "Voltages", "Power", "Energy", "Currents",
                                          "Humidity", "Clocks", "PWM duty" };

typedef struct {
    int kind, idx;
    char base[24];        /* temp3 */
    char in[40];          /* temp3_input */
    char label[72];
    int label_src;        /* 0 driver, 1 sensors.conf, 2 Super-I/O pin name, 3 none */
    double raw;           /* scaled reading (energy: joules) */
    double v;             /* displayed value (energy: watts) */
    double lmin, lmax, lcrit;
    int alarm;
    int ignored;          /* lm-sensors "ignore" */
    const char *nc;       /* not connected reason */
    const char *formula;  /* compute expression applied */
    double tmin, tmax;
} sens;

typedef struct {
    char dir[64];         /* /sys/class/hwmon/hwmon4 */
    char hw[16];          /* hwmon4 */
    char name[64];
    char cname[96];       /* libsensors-style chip name, e.g. nct6799-isa-0290 */
    char dev[320];
    int sio, prio, num;
    sens *s;
    int n;
} chip;

static int is_sio_name(const char *n)
{
    static const char *const p[] = { "nct6", "nct5", "it87", "it86", "it88", "w83", "f71", "f81", "sch5", "pc8736",
                                     "vt1211", "smsc47", NULL };
    int i;
    for (i = 0; p[i]; i++) if (!strncmp(n, p[i], strlen(p[i]))) return 1;
    return 0;
}

static int chip_prio(const char *n)
{
    if (is_sio_name(n)) return 0;
    if (!strcmp(n, "k10temp") || !strcmp(n, "zenergy") || !strcmp(n, "amd_energy") || !strcmp(n, "coretemp")) return 1;
    if (!strcmp(n, "amdgpu") || !strcmp(n, "nouveau") || !strcmp(n, "i915") || !strcmp(n, "xe") || !strcmp(n, "radeon")) return 2;
    if (!strcmp(n, "spd5118") || !strcmp(n, "jc42") || !strncmp(n, "ee1004", 6)) return 3;
    if (!strcmp(n, "nvme") || !strcmp(n, "drivetemp")) return 4;
    return 5;
}

static void chip_cname(chip *c)
{
    const char *d = c->dev, *b = strrchr(d, '/');
    char tok[128], pci[64] = "";
    const char *p = d;
    unsigned dom, bus, dv, fn;
    b = b ? b + 1 : d;
    /* last PCI component of the device path */
    while (*p) {
        size_t l;
        while (*p == '/') p++;
        l = strcspn(p, "/");
        snprintf(tok, sizeof tok, "%.*s", (int)l, p);
        if (sscanf(tok, "%x:%x:%x.%x", &dom, &bus, &dv, &fn) == 4 && strlen(tok) == 12)
            snprintf(pci, sizeof pci, "pci-%04x", (dom << 16) + (bus << 8) + (dv << 3) + fn);
        p += l;
    }
    if (strstr(d, "/i2c-")) {
        int a1, a2;
        if (sscanf(b, "%d-%x", &a1, &a2) == 2) { snprintf(c->cname, sizeof c->cname, "%s-i2c-%d-%x", c->name, a1, a2); return; }
    }
    if (strstr(d, "/mdio_bus/")) { snprintf(c->cname, sizeof c->cname, "%s-mdio-0", c->name); return; }
    if (strstr(d, "/target")) {
        int h, ch, t, l;
        if (sscanf(b, "%d:%d:%d:%d", &h, &ch, &t, &l) == 4) { snprintf(c->cname, sizeof c->cname, "%s-scsi-%d-%d", c->name, h, ch); return; }
    }
    if (!strncmp(d, "/sys/devices/platform/", 22) && !*pci) {
        const char *dot = strrchr(b, '.');
        unsigned long a = dot ? strtoul(dot + 1, NULL, 10) : 0;
        snprintf(c->cname, sizeof c->cname, "%s-isa-%04lx", c->name, a);
        return;
    }
    if (*pci) { snprintf(c->cname, sizeof c->cname, "%s-%s", c->name, pci); return; }
    snprintf(c->cname, sizeof c->cname, "%s-virtual-0", c->name);
}

static int parse_attr(const char *f, int *kind, int *idx, const char **suffix)
{
    int k;
    for (k = 0; k < K_N; k++) {
        size_t l = strlen(k_pfx[k]);
        char *e;
        if (strncmp(f, k_pfx[k], l) || !isdigit((unsigned char)f[l])) continue;
        *idx = (int)strtol(f + l, &e, 10);
        *kind = k;
        if (k == K_PWM) { if (*e) return 0; *suffix = ""; return 1; }
        if (*e != '_') return 0;
        *suffix = e + 1;
        return 1;
    }
    return 0;
}

/* NCT67xx voltage inputs: the driver exposes no labels. Only the pins that are fixed across the nct6775
 * family are named (same names lm-sensors' sensors3.conf uses for nct6775..nct6796); the rest stay inN. */
static const char *nct_in_label(int idx)
{
    switch (idx) {
    case 0: return "Vcore";
    case 2: return "AVCC";
    case 3: return "+3.3V";
    case 7: return "3VSB";
    case 8: return "Vbat";
    default: return NULL;
    }
}

static int board_is(const char *needle) { return strstr(sys_board(), needle) != NULL; }

/* implausible Super-I/O inputs (floating thermistor pins, unwired PCH readings) */
static const char *sio_nc(const chip *c, const sens *s)
{
    if (!c->sio) return NULL;
    if (s->kind == K_TEMP) {
        if (!strncmp(s->label, "PCH_", 4) && s->raw == 0) return "not connected (PCH reading 0)";
        if (!strncmp(c->name, "nct6799", 7) && board_is("TUF GAMING X870-PRO") &&
            (!strcmp(s->label, "AUXTIN0") || !strcmp(s->label, "AUXTIN3") || !strcmp(s->label, "AUXTIN4")))
            return "not connected (floating input)";
        if (s->raw <= 0 || s->raw >= 120) return "not connected (implausible)";
        if (!strncmp(s->label, "AUXTIN", 6) && s->raw < 15) return "not connected (floating low)";
    }
    return NULL;
}

static int sens_cmp(const void *a, const void *b)
{
    const sens *x = a, *y = b;
    if (x->kind != y->kind) return x->kind - y->kind;
    return x->idx - y->idx;
}
static int chip_cmp(const void *a, const void *b)
{
    const chip *x = a, *y = b;
    if (x->prio != y->prio) return x->prio - y->prio;
    return x->num - y->num;
}

/* tracked min / max (SIV's Minimum / Maximum columns), key = hwmonN/base */
typedef struct { char key[48]; double lo, hi; } track;
static track g_tr[1024];
static int g_ntr;

static void track_upd(const char *hw, sens *s)
{
    char key[48];
    int i;
    track *t = NULL;
    snprintf(key, sizeof key, "%s/%s", hw, s->base);
    for (i = 0; i < g_ntr; i++) if (!strcmp(g_tr[i].key, key)) { t = &g_tr[i]; break; }
    if (isnan(s->v)) { s->tmin = t ? t->lo : NAN; s->tmax = t ? t->hi : NAN; return; }
    if (!t) {
        if (g_ntr >= 1024) { s->tmin = s->tmax = s->v; return; }
        t = &g_tr[g_ntr++];
        snprintf(t->key, sizeof t->key, "%s", key);
        t->lo = t->hi = s->v;
        if (!g_zero_t) g_zero_t = time(NULL);
    }
    if (s->v < t->lo || isnan(t->lo)) t->lo = s->v;
    if (s->v > t->hi || isnan(t->hi)) t->hi = s->v;
    s->tmin = t->lo;
    s->tmax = t->hi;
}

void sensors_zero(void)
{
    pthread_mutex_lock(&g_mx);
    g_ntr = 0;
    g_zero_t = time(NULL);
    pthread_mutex_unlock(&g_mx);
}

void sensors_set_hide_unused(int on)
{
    pthread_mutex_lock(&g_mx);
    g_hide = on ? 1 : 0;
    pthread_mutex_unlock(&g_mx);
}

static double lim(const char *dir, const char *base, const char *suf, int kind)
{
    char a[64];
    double v;
    snprintf(a, sizeof a, "%s_%s", base, suf);
    if (!hw_dbl(dir, a, &v)) return NAN;
    v /= k_div[kind];
    if (kind == K_TEMP && (v <= -100 || v >= 500 || v == 0)) return NAN;
    return v;
}

static int any_alarm(const char *dir, const char *base)
{
    static const char *const suf[] = { "alarm", "min_alarm", "max_alarm", "crit_alarm", "lcrit_alarm", "emergency_alarm", NULL };
    int i;
    for (i = 0; suf[i]; i++) {
        char a[64];
        double v;
        snprintf(a, sizeof a, "%s_%s", base, suf[i]);
        if (hw_dbl(dir, a, &v) && v != 0) return 1;
    }
    return 0;
}

static void load_chip(chip *c)
{
    char **names;
    int n, i, cap = 0;
    n = dir_list(c->dir, &names);
    if (n <= 0) return;
    for (i = 0; i < n; i++) {
        int kind, idx;
        const char *suf;
        sens *s;
        char a[64];
        if (!parse_attr(names[i], &kind, &idx, &suf)) continue;
        if (kind == K_PWM) { if (*suf) continue; }
        else if (!strcmp(suf, "average")) {
            if (kind != K_POWER) continue;
            snprintf(a, sizeof a, "power%d_input", idx);
            if (hw_has(c->dir, a)) continue;
        } else if (strcmp(suf, "input")) continue;
        if (c->n >= cap) {
            sens *ns;
            cap = cap ? cap * 2 : 32;
            ns = realloc(c->s, sizeof *ns * (size_t)cap);
            if (!ns) break;
            c->s = ns;
        }
        s = &c->s[c->n];
        memset(s, 0, sizeof *s);
        s->kind = kind;
        s->idx = idx;
        snprintf(s->base, sizeof s->base, "%s%d", k_pfx[kind], idx);
        snprintf(s->in, sizeof s->in, "%s", names[i]);
        if (!hw_dbl(c->dir, names[i], &s->raw)) continue;   /* unreadable (e.g. EIO): skip */
        s->raw /= k_div[kind];
        s->v = s->raw;
        s->lmin = s->lmax = s->lcrit = NAN;
        snprintf(a, sizeof a, "%s_label", s->base);
        if (hw_str(c->dir, a, s->label, sizeof s->label) && *s->label) s->label_src = 0;
        else if (kind == K_IN && !strncmp(c->name, "nct67", 5) && nct_in_label(idx)) {
            snprintf(s->label, sizeof s->label, "%s", nct_in_label(idx));
            s->label_src = 2;
        } else { snprintf(s->label, sizeof s->label, "%s", s->base); s->label_src = 3; }
        {
            const cfg_st *st = cfg_find(c->cname, "label", s->base);
            if (st && *st->a) { snprintf(s->label, sizeof s->label, "%s", st->a); s->label_src = 1; }
            if (cfg_find(c->cname, "ignore", s->base)) s->ignored = 1;
            st = cfg_find(c->cname, "compute", s->base);
            if (st && *st->a && kind != K_ENERGY) {
                int ok;
                double v = eval_compute(st->a, s->raw, &ok);
                if (ok) { s->v = v; s->formula = st->a; }
            }
        }
        switch (kind) {
        case K_TEMP:
            s->lmin = lim(c->dir, s->base, "min", kind);
            s->lmax = lim(c->dir, s->base, "max", kind);
            s->lcrit = lim(c->dir, s->base, "crit", kind);
            break;
        case K_IN: case K_CURR:
            s->lmin = lim(c->dir, s->base, "min", kind);
            s->lmax = lim(c->dir, s->base, "max", kind);
            s->lcrit = lim(c->dir, s->base, "crit", kind);
            if (s->lmin == 0 && s->lmax == 0) s->lmin = s->lmax = NAN;
            break;
        case K_FAN:
            s->lmin = lim(c->dir, s->base, "min", kind);
            s->lmax = lim(c->dir, s->base, "max", kind);
            if (s->lmin == 0) s->lmin = NAN;
            break;
        case K_POWER:
            s->lmax = lim(c->dir, s->base, "cap", kind);
            s->lcrit = lim(c->dir, s->base, "crit", kind);
            break;
        default: break;
        }
        s->alarm = any_alarm(c->dir, s->base);
        /* voltage alarms against unset (0/0) limits are meaningless */
        if ((kind == K_IN || kind == K_FAN) && isnan(s->lmin) && isnan(s->lmax)) s->alarm = 0;
        c->n++;
    }
    dir_free(names, n);
    for (i = 0; i < c->n; i++) c->s[i].nc = sio_nc(c, &c->s[i]);
    qsort(c->s, (size_t)c->n, sizeof *c->s, sens_cmp);
}

/* every hwmon chip; energy inputs get watts; tracked min/max updated. Caller holds g_mx. */
static int collect(chip **out, int energy)
{
    char **names;
    int n, i, nc = 0;
    chip *cs;
    cfg_load();
    *out = NULL;
    n = dir_list(HWMON, &names);
    if (n <= 0) return 0;
    cs = calloc((size_t)n, sizeof *cs);
    if (!cs) { dir_free(names, n); return 0; }
    for (i = 0; i < n; i++) {
        chip *c = &cs[nc];
        char p[400], *rp;
        if (strncmp(names[i], "hwmon", 5)) continue;
        snprintf(c->hw, sizeof c->hw, "%s", names[i]);
        snprintf(c->dir, sizeof c->dir, HWMON "/%s", names[i]);
        c->num = atoi(names[i] + 5);
        if (!hw_str(c->dir, "name", c->name, sizeof c->name)) snprintf(c->name, sizeof c->name, "%s", names[i]);
        snprintf(p, sizeof p, "%s/device", c->dir);
        rp = realpath(p, NULL);
        if (!rp) { snprintf(p, sizeof p, "%s", c->dir); rp = realpath(p, NULL); }
        snprintf(c->dev, sizeof c->dev, "%s", rp ? rp : "");
        free(rp);
        c->sio = is_sio_name(c->name);
        c->prio = chip_prio(c->name);
        chip_cname(c);
        load_chip(c);
        nc++;
    }
    dir_free(names, n);
    qsort(cs, (size_t)nc, sizeof *cs, chip_cmp);
    if (energy) {
        const char **paths = NULL;
        sens **refs = NULL;
        int ne = 0, cap = 0, k;
        for (i = 0; i < nc; i++)
            for (k = 0; k < cs[i].n; k++) {
                char *pp;
                if (cs[i].s[k].kind != K_ENERGY) continue;
                if (ne >= cap) {
                    cap = cap ? cap * 2 : 16;
                    paths = realloc(paths, sizeof *paths * (size_t)cap);
                    refs = realloc(refs, sizeof *refs * (size_t)cap);
                    if (!paths || !refs) { free(paths); free(refs); paths = NULL; refs = NULL; ne = 0; goto done; }
                }
                pp = malloc(600);
                if (!pp) continue;
                snprintf(pp, 600, "%s/%s", cs[i].dir, cs[i].s[k].in);
                paths[ne] = pp;
                refs[ne++] = &cs[i].s[k];
            }
        if (ne) {
            double *w = calloc((size_t)ne, sizeof *w), *j = calloc((size_t)ne, sizeof *j);
            if (w && j) {
                energy_sample(paths, ne, w, j);
                for (k = 0; k < ne; k++) { refs[k]->raw = j[k]; refs[k]->v = w[k]; }
            }
            free(w);
            free(j);
            for (k = 0; k < ne; k++) free((void *)paths[k]);
        }
    done:
        free(paths);
        free(refs);
    } else {
        int k;
        for (i = 0; i < nc; i++)
            for (k = 0; k < cs[i].n; k++) if (cs[i].s[k].kind == K_ENERGY) cs[i].s[k].v = NAN;
    }
    for (i = 0; i < nc; i++) {
        int k;
        for (k = 0; k < cs[i].n; k++) track_upd(cs[i].hw, &cs[i].s[k]);
    }
    *out = cs;
    return nc;
}

static void free_chips(chip *cs, int n)
{
    int i;
    for (i = 0; i < n; i++) free(cs[i].s);
    free(cs);
}

static int arg_match(const srs_out *o, const chip *c)
{
    if (!o->arg || !*o->arg) return 1;
    return !strcmp(o->arg, c->name) || !strcmp(o->arg, c->hw) || !strncmp(c->cname, o->arg, strlen(o->arg));
}

/* value formatting */
static void fmt_v(int kind, double v, char *b, size_t sz)
{
    if (isnan(v)) { snprintf(b, sz, "-"); return; }
    switch (kind) {
    case K_TEMP: snprintf(b, sz, "%.1f°C", v); break;
    case K_FAN: snprintf(b, sz, "%.0f RPM", v); break;
    case K_IN: snprintf(b, sz, "%.3f V", v); break;
    case K_POWER: case K_ENERGY: snprintf(b, sz, "%.2f W", v); break;
    case K_CURR: snprintf(b, sz, "%.3f A", v); break;
    case K_HUMID: snprintf(b, sz, "%.1f%%", v); break;
    case K_FREQ: snprintf(b, sz, "%.0f MHz", v); break;
    case K_PWM: snprintf(b, sz, "%.0f%%", v); break;
    default: snprintf(b, sz, "%g", v);
    }
}
static void fmt_n(int kind, double v, char *b, size_t sz)
{
    if (kind == K_IN || kind == K_CURR) snprintf(b, sz, "%.3f", v);
    else if (kind == K_TEMP) snprintf(b, sz, "%.0f", v);
    else snprintf(b, sz, "%.0f", v);
}
static void fmt_limits(const sens *s, char *b, size_t sz)
{
    char t[32];
    b[0] = 0;
    if (s->kind == K_ENERGY && !isnan(s->raw)) { snprintf(b, sz, "total %.0f J", s->raw); return; }
    if (!isnan(s->lmin)) { fmt_n(s->kind, s->lmin, t, sizeof t); snprintf(b + strlen(b), sz - strlen(b), "min %s  ", t); }
    if (!isnan(s->lmax)) { fmt_n(s->kind, s->lmax, t, sizeof t); snprintf(b + strlen(b), sz - strlen(b), "%s %s  ", s->kind == K_POWER ? "cap" : "max", t); }
    if (!isnan(s->lcrit)) { fmt_n(s->kind, s->lcrit, t, sizeof t); snprintf(b + strlen(b), sz - strlen(b), "crit %s", t); }
}

static int sens_unused(const sens *s)
{
    if (s->nc || s->ignored) return 1;
    if (s->kind == K_ENERGY) return 0;
    return !isnan(s->v) && s->v == 0;
}

/* ======================================================================
 * [sensors] Sensor Status
 * ====================================================================== */
static void s_sensors(srs_out *o)
{
    static const int tabs[] = { 245, 345, 445, 545, 770, 1010 };
    chip *cs;
    int nc, i, k, shown = 0, hidden = 0, ncon = 0, nal = 0, total = 0, hide, pin_note = 0;
    char b[128];
    pthread_mutex_lock(&g_mx);
    nc = collect(&cs, 1);
    hide = g_hide;
    so_siv_header(o, "Sensor Status");
    so_tabs(o, tabs, 6);
    if (!nc) {
        so_t(o, C_TEAL, "No SIO or GPU Sensors to Report");
        so_par(o);
        so_err(o, "No /sys/class/hwmon chips found (hwmon drivers not loaded?)");
        pthread_mutex_unlock(&g_mx);
        return;
    }
    {
        char ts[64] = "";
        struct tm tmv;
        if (g_zero_t && localtime_r(&g_zero_t, &tmv)) strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);
        snprintf(b, sizeof b, "Minimum / Maximum since %s (Zero resets)   Hide unused: %s", *ts ? ts : "now", hide ? "on" : "off");
        so_note(o, b);
        so_par(o);
    }
    {
        static const char *const h[] = { "Sensor", "Current", "Minimum", "Maximum", "Limits", "Status", "Source" };
        static const int hc[] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL };
        so_row(o, 7, h, hc);
    }
    for (i = 0; i < nc; i++) {
        chip *c = &cs[i];
        int lastkind = -1, chip_hdr = 0;
        if (!arg_match(o, c) || !c->n) continue;
        for (k = 0; k < c->n; k++) {
            sens *s = &c->s[k];
            char cur[48], mn[48], mx[48], lm[96], src[64];
            const char *st = "";
            int col = C_VALUE, scol = C_VALUE;
            total++;
            if (s->nc) ncon++;
            if (s->alarm && !s->nc) nal++;
            if (hide && sens_unused(s)) { hidden++; continue; }
            if (!chip_hdr) {
                so_par(o);
                so_b(o, C_TEAL, c->name);
                so_t(o, C_GRAY, "  ");
                so_t(o, C_PURPLE, c->cname);
                so_t(o, C_GRAY, "  ");
                so_t(o, C_GRAY, c->hw);
                so_t(o, C_GRAY, "  ");
                so_t(o, C_GRAY, c->dev);
                so_par(o);
                chip_hdr = 1;
            }
            if (s->kind != lastkind) {
                so_t(o, C_ORANGE, k_name[s->kind]);
                so_par(o);
                lastkind = s->kind;
            }
            fmt_v(s->kind, s->v, cur, sizeof cur);
            fmt_v(s->kind, s->tmin, mn, sizeof mn);
            fmt_v(s->kind, s->tmax, mx, sizeof mx);
            fmt_limits(s, lm, sizeof lm);
            snprintf(src, sizeof src, "%s%s%s", s->in, s->formula ? " (compute)" : "", s->label_src == 2 ? " (pin)" : "");
            if (s->label_src == 2) pin_note = 1;
            if (s->nc) { st = s->nc; col = scol = C_GRAY; }
            else if (s->ignored) { st = "ignored (sensors.conf)"; col = scol = C_GRAY; }
            else if (s->alarm) { st = "ALARM"; col = scol = C_RED; }
            else if (!isnan(s->lcrit) && s->v >= s->lcrit) { st = "CRITICAL"; col = scol = C_RED; }
            else if (s->kind != K_POWER && !isnan(s->lmax) && s->v > s->lmax) { st = "HIGH"; col = scol = C_ORANGE; }
            else if (!isnan(s->lmin) && s->v < s->lmin && s->kind != K_TEMP) { st = "LOW"; col = scol = C_ORANGE; }
            else if (sens_unused(s)) { st = "unused"; col = scol = C_GRAY; }
            else { st = "OK"; scol = C_GREEN; }
            {
                const char *cells[] = { s->label, cur, mn, mx, lm, st, src };
                int g = s->nc || s->ignored ? C_GRAY : C_LABEL;
                int cols[] = { g, col == C_VALUE ? C_BLACK : col, s->nc ? C_GRAY : C_TEAL, s->nc ? C_GRAY : C_PURPLE,
                               C_GRAY, scol, C_GRAY };
                so_row(o, 7, cells, cols);
            }
            shown++;
        }
    }
    so_par(o);
    snprintf(b, sizeof b, "%d chips, %d sensors, %d shown, %d hidden, %d not connected, %d in alarm", nc, total, shown, hidden, ncon, nal);
    so_t(o, C_VALUE, b);
    so_par(o);
    if (ncon) so_note(o, "Greyed: Super-I/O inputs with no thermistor wired (floating) or unwired PCH readings - not real temperatures.");
    if (pin_note) so_note(o, "(pin) = standard NCT67xx pin name (driver has no voltage labels); other inN are board-specific, divider scaling not applied.");
    so_board_footer(o);
    free_chips(cs, nc);
    pthread_mutex_unlock(&g_mx);
}

/* ======================================================================
 * [hw-status] Hardware Status
 * ====================================================================== */
static void s_hw_status(srs_out *o)
{
    static const int tabs[] = { 105, 245, 385, 435, 485, 535, 585, 635, 700, 780 };
    chip *cs;
    int nc, i, k;
    char *mods = NULL;
    pthread_mutex_lock(&g_mx);
    nc = collect(&cs, 0);
    so_siv_header(o, "Hardware Status");
    so_tabs(o, tabs, 10);
    {
        static const char *const h[] = { "Chip", "Sensors name", "Device", "Temp", "Fans", "Volts", "Power", "PWM", "Other", "NC", "Alarms" };
        so_row(o, 11, h, NULL);
    }
    for (i = 0; i < nc; i++) {
        chip *c = &cs[i];
        int cnt[K_N] = { 0 }, nnc = 0, nal = 0, other;
        char nums[6][12], nb[12], ab[12], *dv = strrchr(c->dev, '/');
        for (k = 0; k < c->n; k++) {
            cnt[c->s[k].kind]++;
            if (c->s[k].nc) nnc++;
            else if (c->s[k].alarm) nal++;
        }
        other = cnt[K_ENERGY] + cnt[K_CURR] + cnt[K_HUMID] + cnt[K_FREQ];
        snprintf(nums[0], 12, "%d", cnt[K_TEMP]);
        snprintf(nums[1], 12, "%d", cnt[K_FAN]);
        snprintf(nums[2], 12, "%d", cnt[K_IN]);
        snprintf(nums[3], 12, "%d", cnt[K_POWER]);
        snprintf(nums[4], 12, "%d", cnt[K_PWM]);
        snprintf(nums[5], 12, "%d", other);
        snprintf(nb, sizeof nb, "%d", nnc);
        snprintf(ab, sizeof ab, "%d", nal);
        {
            const char *cells[] = { c->name, c->cname, dv ? dv + 1 : c->dev, nums[0], nums[1], nums[2], nums[3], nums[4], nums[5], nb, ab };
            int cols[] = { C_TEAL, C_PURPLE, C_GRAY, C_VALUE, C_VALUE, C_VALUE, C_VALUE, C_VALUE, C_VALUE,
                           nnc ? C_GRAY : C_VALUE, nal ? C_RED : C_GREEN };
            so_row(o, 11, cells, cols);
        }
    }
    so_par(o);
    {
        static const int kt[] = { 200 };
        so_tabs(o, kt, 1);
    }
    /* chassis intrusion / beep */
    for (i = 0; i < nc; i++) {
        chip *c = &cs[i];
        double v;
        int j;
        for (j = 0; j < 4; j++) {
            char a[40];
            snprintf(a, sizeof a, "intrusion%d_alarm", j);
            if (hw_dbl(c->dir, a, &v)) {
                char k2[96];
                snprintf(k2, sizeof k2, "%s intrusion%d", c->name, j);
                so_kv(o, k2, v ? C_ORANGE : C_GREEN, v ? "Intrusion latched (set since last clear, not live; clearing needs a write - not done here)" : "Closed");
            }
        }
        if (hw_dbl(c->dir, "beep_enable", &v)) {
            char k2[96];
            snprintf(k2, sizeof k2, "%s beep", c->name);
            so_kv(o, k2, C_VALUE, v ? "Enabled" : "Disabled");
        }
    }
    /* loaded hwmon-related modules */
    if (rd_all("/proc/modules", &mods) >= 0) {
        static const char *const want[] = { "nct6775", "nct6775_core", "nct6683", "nct6687", "it87", "k10temp", "zenergy",
                                             "amd_energy", "coretemp", "drivetemp", "spd5118", "jc42", "asus_ec_sensors",
                                             "asus_wmi_sensors", "asus_atk0110", "amdgpu", "acpi_cpufreq", "ryzen_smu",
                                             "corsair_cpro", "corsair_psu", "nzxt_kraken3", "nzxt_smart2", NULL };
        char line[512] = "";
        int w;
        for (w = 0; want[w]; w++) {
            char pat[64];
            snprintf(pat, sizeof pat, "\n%s ", want[w]);
            if (!strncmp(mods, pat + 1, strlen(pat + 1)) || strstr(mods, pat)) {
                if (*line) strncat(line, " ", sizeof line - strlen(line) - 1);
                strncat(line, want[w], sizeof line - strlen(line) - 1);
            }
        }
        so_kv(o, "Monitoring modules", C_PURPLE, line);
        free(mods);
    }
    so_board_footer(o);
    free_chips(cs, nc);
    pthread_mutex_unlock(&g_mx);
}

/* ======================================================================
 * [cooling] fans + PWM duty (pwm / pwm_enable are READ only)
 * ====================================================================== */
static const char *pwm_enable_txt(const chip *c, long en)
{
    if (!strncmp(c->name, "nct6", 4) || !strncmp(c->name, "w83", 3)) {
        switch (en) {
        case 0: return "off (full speed)";
        case 1: return "manual (software)";
        case 2: return "Thermal Cruise";
        case 3: return "Fan Speed Cruise";
        case 4: return "Smart Fan III";
        case 5: return "Smart Fan IV";
        default: break;
        }
    }
    switch (en) {
    case 0: return "full speed / off";
    case 1: return "manual (software)";
    default: return "automatic (hardware)";
    }
}

static void s_cooling(srs_out *o)
{
    static const int tabs[] = { 140, 220, 300, 380, 460, 530, 580, 740, 980 };
    chip *cs;
    int nc, i, k, any = 0;
    char b[256];
    pthread_mutex_lock(&g_mx);
    nc = collect(&cs, 0);
    so_siv_header(o, "Cooling Status");
    so_tabs(o, tabs, 9);
    {
        static const char *const h[] = { "Fan", "Current", "Minimum", "Maximum", "Target", "Duty", "Mode", "Control", "Temperature source", "Curve" };
        so_row(o, 10, h, NULL);
    }
    for (i = 0; i < nc; i++) {
        chip *c = &cs[i];
        int maxidx = 0, idx, hdr = 0;
        if (!arg_match(o, c)) continue;
        for (k = 0; k < c->n; k++)
            if ((c->s[k].kind == K_FAN || c->s[k].kind == K_PWM) && c->s[k].idx > maxidx) maxidx = c->s[k].idx;
        for (idx = 1; idx <= maxidx; idx++) {
            sens *f = NULL, *p = NULL;
            char a[64], rpm[32] = "-", mn[32] = "-", mx[32] = "-", tg[32] = "", duty[32] = "-", mode[16] = "", ctl[48] = "",
                 src[96] = "", curve[160] = "";
            double v;
            long en = -1;
            for (k = 0; k < c->n; k++) {
                if (c->s[k].idx != idx) continue;
                if (c->s[k].kind == K_FAN) f = &c->s[k];
                if (c->s[k].kind == K_PWM) p = &c->s[k];
            }
            if (!f && !p) continue;
            if (g_hide && f && f->v == 0) continue;   /* SIV Hide: fans reading 0 RPM */
            if (!hdr) {
                so_par(o);
                so_b(o, C_TEAL, c->name);
                so_t(o, C_GRAY, "  ");
                so_t(o, C_PURPLE, c->cname);
                so_par(o);
                hdr = 1;
            }
            if (f) {
                fmt_v(K_FAN, f->v, rpm, sizeof rpm);
                fmt_v(K_FAN, f->tmin, mn, sizeof mn);
                fmt_v(K_FAN, f->tmax, mx, sizeof mx);
            }
            if (p) {
                double pmax = 255;
                snprintf(a, sizeof a, "pwm%d_max", idx);
                if (!hw_dbl(c->dir, a, &pmax) || pmax <= 0) pmax = 255;
                snprintf(a, sizeof a, "pwm%d", idx);
                if (hw_dbl(c->dir, a, &v)) snprintf(duty, sizeof duty, "%.0f%% (%.0f)", v * 100.0 / pmax, v);
                snprintf(a, sizeof a, "pwm%d_mode", idx);
                if (hw_dbl(c->dir, a, &v)) snprintf(mode, sizeof mode, "%s", v ? "PWM" : "DC");
                snprintf(a, sizeof a, "pwm%d_enable", idx);
                if (hw_dbl(c->dir, a, &v)) { en = (long)v; snprintf(ctl, sizeof ctl, "%ld %s", en, pwm_enable_txt(c, en)); }
                else snprintf(ctl, sizeof ctl, "firmware / driver");
            }
            /* fan target: Super-I/O chips only honour it in Fan Speed Cruise (mode 3) */
            if (f && (!c->sio || en == 3)) {
                snprintf(a, sizeof a, "fan%d_target", idx);
                if (hw_dbl(c->dir, a, &v) && v > 0 && v < 20000) snprintf(tg, sizeof tg, "%.0f RPM", v);
            }
            if (p) {
                snprintf(a, sizeof a, "pwm%d_temp_sel", idx);
                if (hw_dbl(c->dir, a, &v) && v > 0) {
                    char lb[72] = "";
                    snprintf(a, sizeof a, "temp%d_label", (int)v);
                    if (!hw_str(c->dir, a, lb, sizeof lb)) snprintf(lb, sizeof lb, "temp%d", (int)v);
                    snprintf(src, sizeof src, "%d %s", (int)v, lb);
                }
                if (en >= 2) {
                    int pt;
                    for (pt = 1; pt <= 7; pt++) {
                        double t, d;
                        char a2[64];
                        snprintf(a, sizeof a, "pwm%d_auto_point%d_temp", idx, pt);
                        snprintf(a2, sizeof a2, "pwm%d_auto_point%d_pwm", idx, pt);
                        if (!hw_dbl(c->dir, a, &t) || !hw_dbl(c->dir, a2, &d)) break;
                        snprintf(curve + strlen(curve), sizeof curve - strlen(curve), "%s%.0f°C:%.0f%%", pt > 1 ? " " : "",
                                 t / 1000, d * 100 / 255);
                    }
                }
            }
            {
                char name[80];
                const char *cells[10];
                int cols[] = { C_LABEL, C_BLACK, C_TEAL, C_PURPLE, C_VALUE, C_ORANGE, C_VALUE, C_VALUE, C_GRAY, C_GRAY };
                if (f && f->label_src == 0) snprintf(name, sizeof name, "%s", f->label);
                else snprintf(name, sizeof name, "%s%d", f ? "fan" : "pwm", idx);
                cells[0] = name; cells[1] = rpm; cells[2] = mn; cells[3] = mx; cells[4] = tg; cells[5] = duty;
                cells[6] = mode; cells[7] = ctl; cells[8] = src; cells[9] = curve;
                if (f && f->v == 0) { cols[0] = C_GRAY; cols[1] = C_GRAY; }
                so_row(o, 10, cells, cols);
            }
            any++;
        }
    }
    if (!any) { so_t(o, C_TEAL, "No fans or PWM outputs exposed by hwmon"); so_par(o); }
    so_par(o);
    so_note(o, "PWM values are read only. Fans reading 0 RPM are greyed (header unused or fan stopped at low duty).");
    /* thermal framework cooling devices */
    {
        char **names;
        int n = dir_list("/sys/class/thermal", &names), cnt = 0;
        char last[64] = "";
        int run = 0;
        if (n > 0) {
            so_par(o);
            so_b(o, C_TEAL, "Thermal cooling devices");
            so_par(o);
            for (i = 0; i <= n; i++) {
                char t[64] = "", cur[32] = "", mx[32] = "";
                if (i < n) {
                    if (strncmp(names[i], "cooling_device", 14)) continue;
                    rd_fmt(t, sizeof t, "/sys/class/thermal/%s/type", names[i]);
                    rd_fmt(cur, sizeof cur, "/sys/class/thermal/%s/cur_state", names[i]);
                    rd_fmt(mx, sizeof mx, "/sys/class/thermal/%s/max_state", names[i]);
                    cnt++;
                    snprintf(b, sizeof b, "%s state %s/%s", t, cur, mx);
                }
                if (i < n && !strcmp(b, last)) { run++; continue; }
                if (*last) {
                    char k2[48];
                    snprintf(k2, sizeof k2, "%d device%s", run, run == 1 ? "" : "s");
                    so_kv(o, k2, C_VALUE, last);
                }
                if (i < n) { snprintf(last, sizeof last, "%s", b); run = 1; }
            }
            if (!cnt) so_note(o, "none");
            dir_free(names, n);
        }
    }
    so_board_footer(o);
    free_chips(cs, nc);
    pthread_mutex_unlock(&g_mx);
}

/* ======================================================================
 * [power] Power Status
 * ====================================================================== */
typedef struct { char name[64], path[320], dir[256]; double w, j; } ezone;

static int rapl_zones(ezone *z, int max)
{
    char **names;
    int n = dir_list("/sys/class/powercap", &names), i, nz = 0;
    for (i = 0; i < n && nz < max; i++) {
        char p[320];
        snprintf(p, sizeof p, "/sys/class/powercap/%s/energy_uj", names[i]);
        if (!path_exists(p)) continue;
        snprintf(z[nz].dir, sizeof z[nz].dir, "/sys/class/powercap/%s", names[i]);
        snprintf(z[nz].path, sizeof z[nz].path, "%s", p);
        if (!rd_fmt(z[nz].name, sizeof z[nz].name, "/sys/class/powercap/%s/name", names[i])) snprintf(z[nz].name, sizeof z[nz].name, "%s", names[i]);
        nz++;
    }
    if (n > 0) dir_free(names, n);
    return nz;
}

static void sample_zones(ezone *z, int nz)
{
    const char *p[64] = { NULL };
    double w[64], j[64];
    int i;
    if (nz <= 0) return;
    for (i = 0; i < nz && i < 64; i++) p[i] = z[i].path;
    energy_sample(p, nz < 64 ? nz : 64, w, j);
    for (i = 0; i < nz && i < 64; i++) { z[i].w = w[i]; z[i].j = j[i]; }
}

static void power_supplies(srs_out *o, int batteries_only)
{
    char **names;
    int n = dir_list("/sys/class/power_supply", &names), i, shown = 0;
    for (i = 0; i < n; i++) {
        static const char *const attrs[] = { "type", "scope", "online", "status", "capacity", "capacity_level", "health",
                                             "technology", "manufacturer", "model_name", "serial_number", "cycle_count",
                                             "voltage_now", "current_now", "power_now", "energy_now", "energy_full",
                                             "energy_full_design", "charge_now", "charge_full", "charge_full_design", NULL };
        char d[300], t[64] = "";
        int a;
        snprintf(d, sizeof d, "/sys/class/power_supply/%s", names[i]);
        hw_str(d, "type", t, sizeof t);
        if (batteries_only && strcmp(t, "Battery") && strcmp(t, "UPS")) continue;
        so_par(o);
        so_b(o, C_TEAL, names[i]);
        so_par(o);
        for (a = 0; attrs[a]; a++) {
            char v[128];
            double x;
            if (!hw_str(d, attrs[a], v, sizeof v) || !*v) continue;
            if (!strcmp(attrs[a], "serial_number")) continue;   /* not shown */
            if (strstr(attrs[a], "_now") || strstr(attrs[a], "_full")) {
                x = atof(v);
                if (attrs[a][0] == 'v') snprintf(v, sizeof v, "%.3f V", x / 1e6);
                else if (attrs[a][0] == 'p') snprintf(v, sizeof v, "%.2f W", x / 1e6);
                else if (attrs[a][0] == 'e') snprintf(v, sizeof v, "%.2f Wh", x / 1e6);
                else if (attrs[a][1] == 'u') snprintf(v, sizeof v, "%.3f A", x / 1e6);
                else snprintf(v, sizeof v, "%.0f mAh", x / 1e3);
            }
            so_kv(o, attrs[a], C_VALUE, v);
        }
        shown++;
    }
    if (n > 0) dir_free(names, n);
    if (!shown) {
        so_t(o, C_TEAL, batteries_only ? "No batteries or UPS reported in /sys/class/power_supply" :
                                         "No power supplies reported in /sys/class/power_supply (mains powered, no battery / UPS driver)");
        so_par(o);
    }
}

static void s_power(srs_out *o)
{
    static const int tabs[] = { 210, 330, 470, 620 };
    ezone z[32];
    int nz, nc, i, k, any = 0;
    chip *cs;
    char b[160], c1[48], c2[48];
    pthread_mutex_lock(&g_mx);
    so_siv_header(o, "Power Status");
    so_tabs(o, tabs, 4);
    nz = rapl_zones(z, 32);
    nc = collect(&cs, 1);   /* also samples zenergy / energy*_input */
    sample_zones(z, nz);
    so_b(o, C_TEAL, "RAPL energy (powercap)");
    so_par(o);
    if (!nz) so_note(o, "No powercap energy zones");
    for (i = 0; i < nz; i++) {
        char lims[160] = "", *zn = strrchr(z[i].dir, '/');
        int cn;
        for (cn = 0; cn < 4; cn++) {
            char a[64], nm[48];
            double v;
            snprintf(a, sizeof a, "constraint_%d_name", cn);
            if (!hw_str(z[i].dir, a, nm, sizeof nm)) break;
            snprintf(a, sizeof a, "constraint_%d_power_limit_uw", cn);
            if (hw_dbl(z[i].dir, a, &v))
                snprintf(lims + strlen(lims), sizeof lims - strlen(lims), "%s %.0f W  ", nm, v / 1e6);
        }
        fmt_v(K_POWER, z[i].w, c1, sizeof c1);
        snprintf(c2, sizeof c2, "%.0f J", z[i].j);
        snprintf(b, sizeof b, "%s (%s)", z[i].name, zn ? zn + 1 : "");
        {
            const char *cells[] = { b, c1, c2, lims };
            int cols[] = { C_LABEL, C_BLACK, C_GRAY, C_PURPLE };
            so_row(o, 4, cells, cols);
        }
    }
    so_par(o);
    so_b(o, C_TEAL, "hwmon power and energy");
    so_par(o);
    for (i = 0; i < nc; i++) {
        chip *c = &cs[i];
        for (k = 0; k < c->n; k++) {
            sens *s = &c->s[k];
            char lb[160], lm[96] = "";
            if (s->kind != K_POWER && s->kind != K_ENERGY && s->kind != K_CURR) continue;
            snprintf(lb, sizeof lb, "%s %s", c->name, s->label);
            fmt_v(s->kind, s->v, c1, sizeof c1);
            if (s->kind == K_ENERGY) snprintf(c2, sizeof c2, "%.0f J", s->raw);
            else snprintf(c2, sizeof c2, "%s", s->in);
            if (s->kind == K_POWER) {
                double v;
                char a[64];
                snprintf(a, sizeof a, "%s_cap", s->base);
                if (hw_dbl(c->dir, a, &v)) snprintf(lm + strlen(lm), sizeof lm - strlen(lm), "cap %.0f W  ", v / 1e6);
                snprintf(a, sizeof a, "%s_cap_max", s->base);
                if (hw_dbl(c->dir, a, &v)) snprintf(lm + strlen(lm), sizeof lm - strlen(lm), "max %.0f W  ", v / 1e6);
                snprintf(a, sizeof a, "%s_cap_default", s->base);
                if (hw_dbl(c->dir, a, &v)) snprintf(lm + strlen(lm), sizeof lm - strlen(lm), "default %.0f W", v / 1e6);
            }
            {
                const char *cells[] = { lb, c1, c2, lm };
                int cols[] = { C_LABEL, C_BLACK, C_GRAY, C_PURPLE };
                so_row(o, 4, cells, cols);
            }
            any++;
        }
    }
    if (!any) so_note(o, "No hwmon power / energy / current sensors");
    so_par(o);
    so_b(o, C_TEAL, "Power supplies");
    so_par(o);
    power_supplies(o, 0);
    so_board_footer(o);
    free_chips(cs, nc);
    pthread_mutex_unlock(&g_mx);
}

/* ======================================================================
 * [kwh-cost] electricity cost from measured power
 * ====================================================================== */
static void s_kwh_cost(srs_out *o)
{
    static const int tabs[] = { 260, 360, 480, 600 };
    ezone z[32];
    int nz, nc, i, k, have_cpu = 0;
    chip *cs;
    double rate = 0.15, total = 0;
    const char *cur = getenv("SRS_CURRENCY"), *rs = getenv("SRS_KWH_COST");
    char b[160], c1[48], c2[48], c3[48];
    if (!cur || !*cur) cur = "$";
    if (rs && atof(rs) > 0) rate = atof(rs);
    pthread_mutex_lock(&g_mx);
    so_siv_header(o, "kWh Cost");
    so_tabs(o, tabs, 4);
    nz = rapl_zones(z, 32);
    nc = collect(&cs, 1);
    sample_zones(z, nz);
    snprintf(b, sizeof b, "%s%.4f per kWh (set SRS_KWH_COST / SRS_CURRENCY)", cur, rate);
    so_kv(o, "Rate", C_PURPLE, b);
    so_par(o);
    {
        static const char *const h[] = { "Component", "Power", "per Hour", "per Day", "per Year" };
        so_row(o, 5, h, NULL);
    }
#define KROW(name, w) do { \
        snprintf(c1, sizeof c1, "%.2f W", (w)); \
        snprintf(c2, sizeof c2, "%s%.4f", cur, (w) / 1000 * rate); \
        snprintf(c3, sizeof c3, "%s%.2f", cur, (w) / 1000 * rate * 24); \
        snprintf(b, sizeof b, "%s%.2f", cur, (w) / 1000 * rate * 24 * 365); \
        { const char *cells[] = { (name), c1, c2, c3, b }; \
          int cols[] = { C_LABEL, C_BLACK, C_VALUE, C_VALUE, C_VALUE }; so_row(o, 5, cells, cols); } } while (0)
    for (i = 0; i < nz; i++)
        if (!strncmp(z[i].name, "package", 7) && !isnan(z[i].w)) {
            char nm[96];
            snprintf(nm, sizeof nm, "CPU %s (RAPL)", z[i].name);
            KROW(nm, z[i].w);
            total += z[i].w;
            have_cpu = 1;
        }
    for (i = 0; i < nc; i++) {
        chip *c = &cs[i];
        for (k = 0; k < c->n; k++) {
            sens *s = &c->s[k];
            char nm[160];
            if (isnan(s->v)) continue;
            if (s->kind == K_ENERGY) {
                if (have_cpu || strncasecmp(s->label, "Esocket", 7)) continue;
                snprintf(nm, sizeof nm, "CPU %s (%s)", s->label, c->name);
                have_cpu = 1;
            } else if (s->kind == K_POWER) {
                if (!strcmp(c->name, "zenergy") || !strcmp(c->name, "amd_energy")) continue;
                snprintf(nm, sizeof nm, "%s %s", !strcmp(c->name, "amdgpu") ? "GPU" : c->name, s->label);
            } else continue;
            KROW(nm, s->v);
            total += s->v;
        }
    }
    so_par(o);
    KROW("Total Power (measured)", total);
#undef KROW
    so_par(o);
    so_note(o, "Only components with power telemetry are counted: board, drives, fans, RAM, PSU losses are not measured.");
    so_board_footer(o);
    free_chips(cs, nc);
    pthread_mutex_unlock(&g_mx);
}

/* ======================================================================
 * [battery] [ups-export] [thermal-zones]
 * ====================================================================== */
static void s_battery(srs_out *o)
{
    static const int tabs[] = { 200 };
    so_siv_header(o, "Battery");
    so_tabs(o, tabs, 1);
    power_supplies(o, 1);
    so_par(o);
    so_kv(o, "Checked", C_PURPLE, "/sys/class/power_supply (Battery / UPS types, incl. HID peripheral batteries)");
    so_board_footer(o);
}

static int have_bin(const char *name)
{
    static const char *const dirs[] = { "/usr/bin", "/usr/sbin", "/usr/local/bin", "/bin", "/sbin", NULL };
    int i;
    for (i = 0; dirs[i]; i++) {
        char p[256];
        snprintf(p, sizeof p, "%s/%s", dirs[i], name);
        if (path_exists(p)) return 1;
    }
    return 0;
}

static void s_ups_export(srs_out *o)
{
    static const int tabs[] = { 220 };
    so_siv_header(o, "UPS Export");
    so_tabs(o, tabs, 1);
    so_kv(o, "apcupsd (apcaccess)", C_VALUE, have_bin("apcaccess") ? "installed (not queried)" : "not installed");
    so_kv(o, "Network UPS Tools (upsc)", C_VALUE, have_bin("upsc") ? "installed (not queried)" : "not installed");
    power_supplies(o, 1);
    so_par(o);
    so_t(o, C_TEAL, "No UPS data to export");
    so_par(o);
    so_board_footer(o);
}

static void s_thermal_zones(srs_out *o)
{
    static const int tabs[] = { 160, 330, 430 };
    char **names;
    int n = dir_list("/sys/class/thermal", &names), i, zones = 0, cdev = 0;
    so_siv_header(o, "Thermal Zones");
    so_tabs(o, tabs, 3);
    for (i = 0; i < n; i++) {
        char d[300], t[64] = "", pol[64] = "", tb[32] = "-";
        double v;
        int tp;
        if (strncmp(names[i], "thermal_zone", 12)) { if (!strncmp(names[i], "cooling_device", 14)) cdev++; continue; }
        snprintf(d, sizeof d, "/sys/class/thermal/%s", names[i]);
        hw_str(d, "type", t, sizeof t);
        hw_str(d, "policy", pol, sizeof pol);
        if (hw_dbl(d, "temp", &v)) snprintf(tb, sizeof tb, "%.1f°C", v / 1000);
        {
            const char *cells[] = { names[i], t, tb, pol };
            int cols[] = { C_LABEL, C_TEAL, C_BLACK, C_GRAY };
            so_row(o, 4, cells, cols);
        }
        for (tp = 0; tp < 16; tp++) {
            char a[48], ty[32] = "", tv[32];
            snprintf(a, sizeof a, "trip_point_%d_type", tp);
            if (!hw_str(d, a, ty, sizeof ty)) break;
            snprintf(a, sizeof a, "trip_point_%d_temp", tp);
            if (!hw_dbl(d, a, &v)) continue;
            snprintf(tv, sizeof tv, "%.1f°C", v / 1000);
            {
                const char *cells[] = { "", ty, tv };
                int cols[] = { C_GRAY, C_PURPLE, C_VALUE };
                so_row(o, 3, cells, cols);
            }
        }
        zones++;
    }
    if (n > 0) dir_free(names, n);
    if (!zones) {
        so_t(o, C_TEAL, "No ACPI / thermal_zone* zones exposed by this firmware");
        so_par(o);
    }
    so_par(o);
    so_kvf(o, "Cooling devices", C_VALUE, "%d (see [cooling])", cdev);
    so_kv(o, "Checked", C_PURPLE, "/sys/class/thermal");
    so_board_footer(o);
}

/* ======================================================================
 * [synthetic] [formula-setup] lm-sensors compute / label statements
 * ====================================================================== */
static void s_synthetic(srs_out *o)
{
    static const int tabs[] = { 150, 260, 360, 640, 760 };
    chip *cs;
    int nc, i, j, k, any = 0;
    char b[256];
    pthread_mutex_lock(&g_mx);
    nc = collect(&cs, 0);
    so_siv_header(o, "Synthetic Setup");
    so_tabs(o, tabs, 5);
    so_note(o, "Linux equivalent of SIV's user-defined computed sensors: lm-sensors \"compute\" statements (read only).");
    for (i = 0; i < g_ncfg_files; i++) so_kv(o, i ? "" : "Config files", C_PURPLE, g_cfg_files[i]);
    if (!g_ncfg_files) so_kv(o, "Config files", C_ORANGE, "none (/etc/sensors3.conf, /etc/sensors.d/*)");
    so_par(o);
    {
        static const char *const h[] = { "Source", "Chip match", "Feature", "Formula (read, write)", "Raw", "Computed" };
        so_row(o, 6, h, NULL);
    }
    for (i = 0; i < g_ncs; i++) {
        const cfg_st *s = &g_cs[i];
        char match[96] = "-", raw[32] = "", comp[32] = "", f[256];
        int col = C_GRAY;
        if (strcmp(s->kw, "compute")) continue;
        if (s->chip >= 0 && g_cc[s->chip].np)
            snprintf(match, sizeof match, "%s%s", g_cc[s->chip].pat[0], g_cc[s->chip].np > 1 ? " ..." : "");
        for (j = 0; j < nc; j++)
            if (cfg_chip_match(s->chip, cs[j].cname)) {
                snprintf(match, sizeof match, "%s", cs[j].cname);
                col = C_VALUE;
                for (k = 0; k < cs[j].n; k++)
                    if (!strcmp(cs[j].s[k].base, s->feat)) {
                        int ok;
                        double v = eval_compute(s->a, cs[j].s[k].raw, &ok);
                        fmt_v(cs[j].s[k].kind, cs[j].s[k].raw, raw, sizeof raw);
                        if (ok) fmt_v(cs[j].s[k].kind, v, comp, sizeof comp);
                        else snprintf(comp, sizeof comp, "n/a");
                    }
                break;
            }
        snprintf(f, sizeof f, "%s , %s", s->a, s->b);
        {
            const char *cells[] = { s->src, match, s->feat, f, raw, comp };
            int cols[] = { C_GRAY, col == C_VALUE ? C_TEAL : C_GRAY, col, col, C_VALUE, C_BLACK };
            so_row(o, 6, cells, cols);
        }
        any++;
    }
    so_par(o);
    snprintf(b, sizeof b, "%d compute statements, chips present:", any);
    so_t(o, C_VALUE, b);
    for (j = 0; j < nc; j++) { so_t(o, C_PURPLE, " "); so_t(o, C_PURPLE, cs[j].cname); }
    so_par(o);
    so_note(o, "Greyed rows apply to chips not present on this system. Add your own in /etc/sensors.d/<name>.conf.");
    so_board_footer(o);
    free_chips(cs, nc);
    pthread_mutex_unlock(&g_mx);
}

static void s_formula_setup(srs_out *o)
{
    static const int tabs[] = { 160, 330, 560, 640, 860 };
    chip *cs;
    int nc, i, j, any = 0;
    pthread_mutex_lock(&g_mx);
    nc = collect(&cs, 0);
    so_siv_header(o, "Formula Setup");
    so_tabs(o, tabs, 5);
    {
        static const char *const h[] = { "SIV Device Name", "User Name", "Formula", "Units", "SIV Device ID", "Source" };
        so_row(o, 6, h, NULL);
    }
    for (i = 0; i < g_ncs; i++) {
        const cfg_st *s = &g_cs[i];
        for (j = 0; j < nc; j++) {
            chip *c = &cs[j];
            int k;
            const sens *se = NULL;
            char id[96], f[256] = "", lb[72] = "";
            if (!cfg_chip_match(s->chip, c->cname)) continue;
            for (k = 0; k < c->n; k++) if (!strcmp(c->s[k].base, s->feat)) se = &c->s[k];
            if (!strcmp(s->kw, "label")) snprintf(lb, sizeof lb, "%s", s->a);
            else if (!strcmp(s->kw, "compute")) snprintf(f, sizeof f, "%s , %s", s->a, s->b);
            else if (!strcmp(s->kw, "set")) snprintf(f, sizeof f, "set = %s (applied only by sensors -s)", s->a);
            else snprintf(f, sizeof f, "ignore");
            snprintf(id, sizeof id, "%s/%s", c->hw, s->feat);
            {
                static const char *const units[K_N] = { "°C", "RPM", "V", "W", "J", "A", "%", "Hz", "%" };
                const char *cells[] = { c->name, lb, f, se ? units[se->kind] : "", id, s->src };
                int cols[] = { C_TEAL, C_BLACK, C_PURPLE, C_VALUE, C_GRAY, C_GRAY };
                so_row(o, 6, cells, cols);
            }
            any++;
        }
    }
    if (!any) {
        so_par(o);
        so_t(o, C_TEAL, "No label / compute / set / ignore statements match the chips present:");
        so_par(o);
        for (j = 0; j < nc; j++) so_kv(o, cs[j].name, C_PURPLE, cs[j].cname);
    }
    so_par(o);
    so_note(o, "Read only. Formulas come from /etc/sensors3.conf and /etc/sensors.d/*; [synthetic] lists every compute line.");
    so_board_footer(o);
    free_chips(cs, nc);
    pthread_mutex_unlock(&g_mx);
}

/* ======================================================================
 * [sen-export] [pwm-export] CSV snapshots
 * ====================================================================== */
static void s_sen_export(srs_out *o)
{
    static const char *const units[K_N] = { "C", "RPM", "V", "W", "W", "A", "%", "MHz", "%" };
    chip *cs;
    int nc, i, k;
    char b[512];
    pthread_mutex_lock(&g_mx);
    nc = collect(&cs, 1);
    so_siv_header(o, "Sensor Export");
    so_m(o, C_LABEL, "hwmon,chip,feature,label,value,unit,minimum,maximum,status");
    so_par(o);
    for (i = 0; i < nc; i++)
        for (k = 0; k < cs[i].n; k++) {
            sens *s = &cs[i].s[k];
            snprintf(b, sizeof b, "%s,%s,%s,\"%s\",%.3f,%s,%.3f,%.3f,%s", cs[i].hw, cs[i].cname, s->base, s->label, s->v,
                     units[s->kind], s->tmin, s->tmax, s->nc ? "not-connected" : s->alarm ? "alarm" : "ok");
            so_m(o, s->nc ? C_GRAY : C_VALUE, b);
            so_par(o);
        }
    so_board_footer(o);
    free_chips(cs, nc);
    pthread_mutex_unlock(&g_mx);
}

static void s_pwm_export(srs_out *o)
{
    chip *cs;
    int nc, i, k, any = 0;
    char b[512];
    pthread_mutex_lock(&g_mx);
    nc = collect(&cs, 0);
    so_siv_header(o, "PWM Export");
    so_m(o, C_LABEL, "hwmon,chip,pwm,raw,duty_pct,enable,mode,temp_sel,fan_rpm");
    so_par(o);
    for (i = 0; i < nc; i++)
        for (k = 0; k < cs[i].n; k++) {
            sens *s = &cs[i].s[k];
            double en = -1, md = -1, ts = -1, rpm = -1;
            char a[64];
            if (s->kind != K_PWM) continue;
            snprintf(a, sizeof a, "pwm%d_enable", s->idx); hw_dbl(cs[i].dir, a, &en);
            snprintf(a, sizeof a, "pwm%d_mode", s->idx); hw_dbl(cs[i].dir, a, &md);
            snprintf(a, sizeof a, "pwm%d_temp_sel", s->idx); hw_dbl(cs[i].dir, a, &ts);
            snprintf(a, sizeof a, "fan%d_input", s->idx); hw_dbl(cs[i].dir, a, &rpm);
            snprintf(b, sizeof b, "%s,%s,pwm%d,%.0f,%.1f,%.0f,%.0f,%.0f,%.0f", cs[i].hw, cs[i].cname, s->idx, s->raw * 2.55, s->raw,
                     en, md, ts, rpm);
            so_m(o, C_VALUE, b);
            so_par(o);
            any++;
        }
    if (!any) so_note(o, "No PWM outputs exposed by hwmon");
    so_note(o, "-1 = attribute not provided by the driver. Values are read only.");
    so_board_footer(o);
    free_chips(cs, nc);
    pthread_mutex_unlock(&g_mx);
}

/* ======================================================================
 * ASUS: [asus-info] [asus-wmi] [atk-debug]
 * ====================================================================== */
static const char *wmi_guid_name(const char *g)
{
    static const struct { const char *g, *n; } t[] = {
        { "97845ED0-4E6D-11DE-8A39-0800200C9A66", "ASUS WMI management (ATK / ASUSWMI, asus-wmi)" },
        { "466747A0-70EC-11DE-8A39-0800200C9A66", "ASUS WMI hardware monitor (asus_wmi_sensors)" },
        { "0B3CBB35-E3C2-45ED-91C2-4C5A6D195D1C", "ASUS WMI events (asus-nb-wmi)" },
        { "05901221-D566-11D1-B2F0-00A0C9062910", "WMI binary MOF (wmi-bmof)" },
        { NULL, NULL } };
    int i;
    for (i = 0; t[i].g; i++) if (!strncasecmp(g, t[i].g, 36)) return t[i].n;
    return "";
}

static void wmi_table(srs_out *o, int asus_only)
{
    char **names;
    int n = dir_list("/sys/bus/wmi/devices", &names), i;
    {
        static const char *const h[] = { "GUID", "Obj", "Inst", "ACPI parent", "Driver", "Description" };
        so_row(o, 6, h, NULL);
    }
    for (i = 0; i < n; i++) {
        char d[300], p[400], obj[16] = "", inst[16] = "", drv[64] = "-", parent[96] = "", *rp, *x;
        const char *nm = wmi_guid_name(names[i]);
        snprintf(d, sizeof d, "/sys/bus/wmi/devices/%s", names[i]);
        if (!hw_str(d, "object_id", obj, sizeof obj)) { if (hw_str(d, "notify_id", obj, sizeof obj)) { char t2[24]; snprintf(t2, sizeof t2, "ev %s", obj); snprintf(obj, sizeof obj, "%s", t2); } }
        hw_str(d, "instance_count", inst, sizeof inst);
        snprintf(p, sizeof p, "%s/driver", d);
        rp = realpath(p, NULL);
        if (rp) { snprintf(drv, sizeof drv, "%s", strrchr(rp, '/') + 1); free(rp); }
        rp = realpath(d, NULL);
        if (rp && (x = strstr(rp, "PNP0C14:"))) {
            char uid[64] = "", pn[16];
            snprintf(pn, sizeof pn, "%.10s", x);
            rd_fmt(uid, sizeof uid, "/sys/bus/acpi/devices/%s/uid", pn);
            snprintf(parent, sizeof parent, "%s %s", pn, uid);
        }
        free(rp);
        if (asus_only && !strstr(parent, "Asus") && !strstr(nm, "ASUS")) continue;
        {
            char g[40];
            const char *cells[6];
            int cols[] = { C_PURPLE, C_VALUE, C_VALUE, C_TEAL, strcmp(drv, "-") ? C_GREEN : C_GRAY, C_BLACK };
            snprintf(g, sizeof g, "%.36s", names[i]);
            cells[0] = g; cells[1] = obj; cells[2] = inst; cells[3] = parent; cells[4] = drv; cells[5] = nm;
            so_row(o, 6, cells, cols);
        }
    }
    if (n > 0) dir_free(names, n);
    if (n <= 0) so_note(o, "No WMI devices (/sys/bus/wmi/devices empty)");
}

/* ACPI table copies from the srs-root cache (DSDT/SSDT hold the WMI _WDG / method code) */
static void acpi_tables_kv(srs_out *o)
{
    const char *t = so_root_file("acpi-tables");
    char dir[1100], line[1024] = "", **names;
    int n, i;
    if (!t) { so_kv(o, "ACPI tables", C_ORANGE, "Needs root: Menu > Run as root (srs-root)"); return; }
    snprintf(dir, sizeof dir, "%s", t);
    n = dir_list(dir, &names);
    for (i = 0; i < n; i++)
        if (!strncmp(names[i], "DSDT", 4) || !strncmp(names[i], "SSDT", 4)) {
            if (*line) strncat(line, " ", sizeof line - strlen(line) - 1);
            strncat(line, names[i], sizeof line - strlen(line) - 1);
        }
    if (n > 0) dir_free(names, n);
    so_kv(o, "ACPI tables", C_VALUE, dir);
    so_kv(o, "DSDT / SSDT", C_PURPLE, *line ? line : "(none in cache)");
}

/* is <mod>.ko* shipped for the running kernel (hwmon or platform/x86)? '-' and '_' are equivalent */
static int module_file(const char *mod)
{
    static const char *const sub[] = { "kernel/drivers/hwmon", "kernel/drivers/platform/x86", "kernel/drivers/platform/x86/asus", NULL };
    char rel[128] = "", want[64];
    int s, found = 0;
    size_t i;
    rd_str("/proc/sys/kernel/osrelease", rel, sizeof rel);
    snprintf(want, sizeof want, "%s", mod);
    for (i = 0; want[i]; i++) if (want[i] == '-') want[i] = '_';
    for (s = 0; sub[s] && !found; s++) {
        char d[300], **names;
        int n, k;
        snprintf(d, sizeof d, "/lib/modules/%s/%s", rel, sub[s]);
        n = dir_list(d, &names);
        for (k = 0; k < n && !found; k++) {
            char nm[128];
            snprintf(nm, sizeof nm, "%s", names[k]);
            nm[strcspn(nm, ".")] = 0;
            for (i = 0; nm[i]; i++) if (nm[i] == '-') nm[i] = '_';
            if (!strcmp(nm, want)) found = 1;
        }
        if (n > 0) dir_free(names, n);
    }
    return found;
}

/* does module <mod> claim this board through its dmi modalias list? */
static int module_claims_board(const char *mod, char *how, size_t sz)
{
    const char *argv[] = { "modinfo", "-F", "alias", mod, NULL };
    char *out = NULL, ma[1024] = "", *line, *save = NULL;
    int found = 0, n = 0;
    *how = 0;
    if (!module_file(mod)) { snprintf(how, sz, "module not built for this kernel"); return -1; }
    if (!rd_str("/sys/class/dmi/id/modalias", ma, sizeof ma)) return -1;
    if (run_out(&out, argv) != 0 || !out) { free(out); snprintf(how, sz, "modinfo failed"); return -1; }
    for (line = strtok_r(out, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (strncmp(line, "dmi", 3)) continue;
        n++;
        if (!fnmatch(line, ma, 0)) { found = 1; snprintf(how, sz, "%s", line); break; }
    }
    if (!found && !n) snprintf(how, sz, "binds by ACPI device id, not by board (DMI)");
    else if (!found) snprintf(how, sz, "%d supported boards, this board is not listed", n);
    free(out);
    return found;
}

static int module_loaded(const char *m)
{
    char p[128];
    snprintf(p, sizeof p, "/sys/module/%s", m);
    return path_exists(p);
}

static void s_asus_info(srs_out *o)
{
    static const int tabs[] = { 200, 520, 590, 660, 860, 960 };
    static const char *const mods[] = { "asus_ec_sensors", "asus_wmi_sensors", "asus_atk0110", "asus_wmi", "asus_nb_wmi", NULL };
    char v[128], how[256];
    int i;
    so_siv_header(o, "ASUS ATK/WMI Information");
    so_tabs(o, tabs, 1);
    rd_str("/sys/class/dmi/id/board_vendor", v, sizeof v);
    so_kv(o, "Board vendor", C_VALUE, v);
    rd_str("/sys/class/dmi/id/board_name", v, sizeof v);
    so_kv(o, "Board", C_BLACK, v);
    rd_str("/sys/class/dmi/id/bios_version", v, sizeof v);
    so_kv(o, "BIOS", C_VALUE, v);
    so_kv(o, "ASUS WMI GUID", path_exists("/sys/bus/wmi/devices/97845ED0-4E6D-11DE-8A39-0800200C9A66-4") ||
                              path_exists("/sys/bus/wmi/devices/97845ED0-4E6D-11DE-8A39-0800200C9A66") ? C_GREEN : C_ORANGE,
          "97845ED0-4E6D-11DE-8A39-0800200C9A66 (ATK / ASUSWMI)");
    so_par(o);
    so_b(o, C_TEAL, "ASUS sensor drivers");
    so_par(o);
    for (i = 0; mods[i]; i++) {
        int r = module_claims_board(mods[i], how, sizeof how);
        char line[400];
        snprintf(line, sizeof line, "%s; %s", module_loaded(mods[i]) ? "loaded" : "not loaded",
                 r > 0 ? "supports this board" : how);
        so_kv(o, mods[i], r > 0 ? C_GREEN : C_GRAY, line);
    }
    so_par(o);
    {
        static const int t2[] = { 300, 350, 400, 630, 730 };
        so_tabs(o, t2, 5);
        so_b(o, C_TEAL, "ASUS WMI blocks");
        so_par(o);
        wmi_table(o, 1);
    }
    so_tabs(o, tabs, 1);
    so_par(o);
    so_note(o, "Board sensors on this system come from the Super-I/O (nct6775 driver) - see [sensors].");
    acpi_tables_kv(o);
    so_board_footer(o);
}

static void s_asus_wmi(srs_out *o)
{
    static const int tabs[] = { 300, 350, 400, 630, 730 };
    char **names;
    int n, i;
    so_siv_header(o, "ASUS WMI ACPI Methods");
    so_tabs(o, tabs, 5);
    so_b(o, C_TEAL, "ACPI WMI devices (PNP0C14)");
    so_par(o);
    n = dir_list("/sys/bus/acpi/devices", &names);
    for (i = 0; i < n; i++) {
        char uid[64] = "", path[128] = "";
        if (strncmp(names[i], "PNP0C14", 7)) continue;
        rd_fmt(uid, sizeof uid, "/sys/bus/acpi/devices/%s/uid", names[i]);
        rd_fmt(path, sizeof path, "/sys/bus/acpi/devices/%s/path", names[i]);
        {
            const char *cells[] = { names[i], uid, path };
            int cols[] = { C_LABEL, C_BLACK, C_GRAY };
            static const int t3[] = { 120, 330 };
            so_tabs(o, t3, 2);
            so_row(o, 3, cells, cols);
        }
    }
    if (n > 0) dir_free(names, n);
    so_par(o);
    so_tabs(o, tabs, 5);
    so_b(o, C_TEAL, "WMI GUID blocks");
    so_par(o);
    wmi_table(o, 0);
    so_par(o);
    so_note(o, "WMI methods are not evaluated: no ASUS WMI driver is bound and calling ACPI methods from user space is not done.");
    {
        static const int kt[] = { 200 };
        so_tabs(o, kt, 1);
    }
    acpi_tables_kv(o);
    so_board_footer(o);
}

static void s_atk_debug(srs_out *o)
{
    static const int tabs[] = { 200 };
    char **names;
    int n = dir_list("/sys/bus/acpi/devices", &names), i, found = 0;
    so_siv_header(o, "ASUS ATK0110 Debug");
    so_tabs(o, tabs, 1);
    for (i = 0; i < n; i++)
        if (!strncmp(names[i], "ATK0110", 7)) { so_kv(o, "ACPI device", C_GREEN, names[i]); found++; }
    if (n > 0) dir_free(names, n);
    if (!found) { so_t(o, C_TEAL, "No ATK0110 ACPI device (used by older ASUS boards only)"); so_par(o); }
    so_kv(o, "asus_atk0110 module", C_VALUE, module_loaded("asus_atk0110") ? "loaded" : "not loaded");
    so_kv(o, "Checked", C_PURPLE, "/sys/bus/acpi/devices/ATK0110*");
    so_board_footer(o);
}

/* ======================================================================
 * Super-I/O: [ldn-info] [sio-dbg]
 * ====================================================================== */
static int find_sio(char *dir, size_t dsz, char *name, size_t nsz)
{
    char **names;
    int n = dir_list(HWMON, &names), i, ok = 0;
    for (i = 0; i < n && !ok; i++) {
        char d[128], nm[64];
        snprintf(d, sizeof d, HWMON "/%s", names[i]);
        if (hw_str(d, "name", nm, sizeof nm) && is_sio_name(nm)) {
            snprintf(dir, dsz, "%s", d);
            snprintf(name, nsz, "%s", nm);
            ok = 1;
        }
    }
    if (n > 0) dir_free(names, n);
    return ok;
}

static void grep_file(srs_out *o, const char *path, const char *const *needles, int max)
{
    char *txt, *line, *save = NULL;
    int shown = 0;
    if (rd_all(path, &txt) < 0) return;
    for (line = strtok_r(txt, "\n", &save); line && shown < max; line = strtok_r(NULL, "\n", &save)) {
        int i;
        for (i = 0; needles[i]; i++)
            if (strcasestr(line, needles[i])) { so_m(o, C_VALUE, line); so_par(o); shown++; break; }
    }
    free(txt);
    if (!shown) so_note(o, "(no matching lines)");
}

static void s_ldn_info(srs_out *o)
{
    static const int tabs[] = { 200 };
    static const char *const needles[] = { "nct6", "it87", "w83", "f71", "0290", "Super", "sio", NULL };
    char dir[128], name[64], dev[400] = "", *rp, p[200];
    so_siv_header(o, "Super I/O Logical Devices");
    so_tabs(o, tabs, 1);
    if (!find_sio(dir, sizeof dir, name, sizeof name)) {
        so_t(o, C_TEAL, "No Super-I/O hardware monitor driver bound (nct6775 / it87 / w83627ehf ...)");
        so_par(o);
    } else {
        const char *b;
        snprintf(p, sizeof p, "%s/device", dir);
        rp = realpath(p, NULL);
        if (rp) { snprintf(dev, sizeof dev, "%s", rp); free(rp); }
        b = strrchr(dev, '/');
        so_kv(o, "Chip", C_BLACK, name);
        so_kv(o, "Platform device", C_VALUE, b ? b + 1 : dev);
        if (b && strchr(b, '.')) so_kvf(o, "HWM base (LDN 0x0B)", C_PURPLE, "0x%04lX", strtoul(strchr(b, '.') + 1, NULL, 10));
        so_kv(o, "hwmon", C_GRAY, dir);
        so_kv(o, "Driver", C_VALUE, module_loaded("nct6775") ? "nct6775 + nct6775_core (loaded)" : "-");
    }
    so_par(o);
    so_b(o, C_TEAL, "I/O ports");
    so_par(o);
    {
        const char *rf = so_root_file("ioports.txt");
        grep_file(o, rf ? rf : "/proc/ioports", needles, 20);
        if (!rf) so_note(o, "Unprivileged /proc/ioports hides addresses. Needs root: Menu > Run as root (srs-root)");
    }
    so_par(o);
    so_b(o, C_TEAL, "Kernel log");
    so_par(o);
    {
        const char *df = so_root_file("dmesg.txt");
        static const char *const dn[] = { "nct6775", "nct6683", "it87", "Super-I/O", "w83627", NULL };
        if (df) grep_file(o, df, dn, 20);
        else so_note(o, "Needs root: Menu > Run as root (srs-root)");
    }
    so_par(o);
    so_note(o, "LDN configuration registers (ports 0x2E/0x4E) are not read: raw port I/O is never done by this tool.");
    so_board_footer(o);
}

static void s_sio_dbg(srs_out *o)
{
    static const int tabs[] = { 260 };
    char dir[128], name[64];
    char **names;
    int n, i;
    so_siv_header(o, "SIO Debug");
    so_tabs(o, tabs, 1);
    if (!find_sio(dir, sizeof dir, name, sizeof name)) {
        so_t(o, C_TEAL, "No Super-I/O hardware monitor driver bound");
        so_par(o);
        so_board_footer(o);
        return;
    }
    so_kv(o, "Chip", C_BLACK, name);
    so_kv(o, "Source", C_GRAY, dir);
    so_note(o, "Register-derived driver attributes (read only; raw register dumps would need /dev/port and are not done).");
    so_par(o);
    n = dir_list(dir, &names);
    for (i = 0; i < n; i++) {
        char v[128];
        if (!strcmp(names[i], "uevent") || !strcmp(names[i], "device") || !strcmp(names[i], "subsystem") ||
            !strcmp(names[i], "power"))
            continue;
        if (!hw_str(dir, names[i], v, sizeof v)) continue;
        so_kv(o, names[i], C_VALUE, v);
    }
    if (n > 0) dir_free(names, n);
    so_board_footer(o);
}

/* ======================================================================
 * absent hardware: IPMI, AIO Link, LCD
 * ====================================================================== */
static void ipmi_probe(srs_out *o, const char *what)
{
    static const int tabs[] = { 200 };
    so_siv_header(o, what);
    so_tabs(o, tabs, 1);
    so_t(o, C_TEAL, path_exists("/dev/ipmi0") || path_exists("/dev/ipmi/0") ? "IPMI device present (not opened)" : "No IPMI BMC found on this system");
    so_par(o);
    so_kv(o, "/dev/ipmi0", C_VALUE, path_exists("/dev/ipmi0") ? "present" : "absent");
    so_kv(o, "/sys/class/ipmi", C_VALUE, path_exists("/sys/class/ipmi") ? "present" : "absent");
    so_kv(o, "ipmi_si / ipmi_devintf", C_VALUE, module_loaded("ipmi_si") || module_loaded("ipmi_devintf") ? "loaded" : "not loaded");
    so_kv(o, "ipmitool", C_VALUE, have_bin("ipmitool") ? "installed" : "not installed");
    so_board_footer(o);
}
static void s_ipmi_info(srs_out *o) { ipmi_probe(o, "IPMI Status"); }
static void s_ipmi_dump(srs_out *o) { ipmi_probe(o, "IPMI Dump"); }
static void s_ipmi_time(srs_out *o) { ipmi_probe(o, "IPMI Time"); }
static void s_ipmi_wait(srs_out *o) { ipmi_probe(o, "IPMI Wait"); }

static void s_link(srs_out *o)
{
    static const int tabs[] = { 200 };
    static const char *const cooler[] = { "corsair_cpro", "corsairpsu", "corsair-psu", "nzxt_kraken2", "nzxt_kraken3", "kraken3",
                                          "nzxt_smart2", "nzxt-smart2", "aquacomputer_d5next", "d5next", "quadro", "octo",
                                          "highflownext", "asus_rog_ryujin", "rog_ryujin", "gigabyte_waterforce", NULL };
    char **names;
    int n = dir_list(HWMON, &names), i, found = 0;
    so_siv_header(o, "AIO Link");
    so_tabs(o, tabs, 1);
    for (i = 0; i < n; i++) {
        char d[128], nm[64];
        int j;
        snprintf(d, sizeof d, HWMON "/%s", names[i]);
        if (!hw_str(d, "name", nm, sizeof nm)) continue;
        for (j = 0; cooler[j]; j++)
            if (!strncmp(nm, cooler[j], strlen(cooler[j]))) { so_kv(o, names[i], C_GREEN, nm); found++; break; }
    }
    if (n > 0) dir_free(names, n);
    if (!found) {
        so_t(o, C_TEAL, "No AIO Link Devices found on this system");
        so_par(o);
    } else so_note(o, "Cooler readings above are part of [sensors] / [cooling].");
    so_par(o);
    so_kv(o, "Checked", C_PURPLE, "hwmon cooler drivers (Corsair / NZXT / Aquacomputer / ASUS / Gigabyte)");
    so_note(o, "USB HID cooler devices are owned by CoolerControl / liquidctl; hidraw is never opened here.");
    so_board_footer(o);
}

static void s_lcd_panel(srs_out *o)
{
    static const int tabs[] = { 200 };
    so_siv_header(o, "LCD Panel Setup");
    so_tabs(o, tabs, 1);
    so_t(o, C_TEAL, "No supported external LCD panel (LCDSysInfo / AIO LCD) found");
    so_par(o);
    so_note(o, "Case / AIO displays are driven over USB HID by their own tools and are not probed here.");
    so_board_footer(o);
}

const srs_item sec_sensors[] = {
    { "sensors", "hardware", "Sensor status", B_LINUX, 0, "/sys/class/hwmon (all chips); /etc/sensors3.conf labels", s_sensors, 1000, "sensors" },
    { "hw-status", "hardware", "Hardware status", B_LINUX, 0, "/sys/class/hwmon; /proc/modules", s_hw_status, 2000, "sensors" },
    { "cooling", "hardware", "Cooling status (fans)", B_LINUX, 0, "/sys/class/hwmon fan*/pwm* (read only); /sys/class/thermal/cooling_device*", s_cooling, 1000, "sensors" },
    { "power", "hardware", "Power status", B_LINUX, 0, "/sys/class/powercap (RAPL); hwmon power*/energy*; /sys/class/power_supply", s_power, 1000, "sensors" },
    { "kwh-cost", "hardware", "kWh cost", B_LINUX, 0, "RAPL / zenergy / hwmon power x SRS_KWH_COST", s_kwh_cost, 2000, "sensors" },
    { "battery", "hardware", "Battery", B_ABSENT, 0, "/sys/class/power_supply (Battery / UPS)", s_battery, 0, "sensors" },
    { "thermal-zones", "hardware", "Thermal zones", B_ABSENT, 0, "/sys/class/thermal (thermal_zone*, cooling_device*)", s_thermal_zones, 0, "sensors" },
    { "synthetic", "hardware", "Synthetic sensors setup", B_LINUX, 0, "lm-sensors compute lines: /etc/sensors3.conf, /etc/sensors.d/*", s_synthetic, 0, "sensors" },
    { "formula-setup", "hardware", "Sensor formula setup", B_LINUX, 0, "lm-sensors label/compute/set/ignore for present chips", s_formula_setup, 0, "sensors" },
    { "sen-export", "hardware", "Sensor export", B_LINUX, 0, "/sys/class/hwmon -> CSV", s_sen_export, 0, "sensors" },
    { "pwm-export", "hardware", "PWM export", B_LINUX, 0, "/sys/class/hwmon pwm* -> CSV (read only)", s_pwm_export, 0, "sensors" },
    { "ups-export", "hardware", "UPS export", B_ABSENT, 0, "apcaccess / upsc presence; /sys/class/power_supply UPS", s_ups_export, 0, "sensors" },
    { "asus-info", "hardware", "ASUS ATK/WMI info", B_LINUX, 1, "DMI; /sys/bus/wmi/devices; modinfo dmi aliases; acpi-tables (root cache)", s_asus_info, 0, "sensors" },
    { "asus-wmi", "hardware", "ASUS WMI ACPI methods", B_LINUX, 1, "/sys/bus/acpi/devices/PNP0C14*; /sys/bus/wmi/devices", s_asus_wmi, 0, "sensors" },
    { "atk-debug", "hardware", "ASUS ATK0110 debug", B_ABSENT, 0, "/sys/bus/acpi/devices (probe ATK0110)", s_atk_debug, 0, "sensors" },
    { "ldn-info", "hardware", "Super I/O LDNs", B_LINUX, 1, "nct6775 platform device; /proc/ioports or ioports.txt; dmesg.txt (root cache)", s_ldn_info, 0, "sensors" },
    { "sio-dbg", "hardware", "SIO debug", B_LINUX, 1, "Super-I/O hwmon driver attributes (read only)", s_sio_dbg, 0, "sensors" },
    { "ipmi-info", "hardware", "IPMI status", B_ABSENT, 0, "/dev/ipmi0; /sys/class/ipmi", s_ipmi_info, 0, "sensors" },
    { "ipmi-dump", "hardware", "IPMI dump", B_ABSENT, 0, "/dev/ipmi0; /sys/class/ipmi", s_ipmi_dump, 0, "sensors" },
    { "ipmi-time", "hardware", "IPMI time", B_ABSENT, 0, "/dev/ipmi0; /sys/class/ipmi", s_ipmi_time, 0, "sensors" },
    { "ipmi-wait", "hardware", "IPMI wait", B_ABSENT, 0, "/dev/ipmi0; /sys/class/ipmi", s_ipmi_wait, 0, "sensors" },
    { "aio-export", "hardware", "AIO Link export", B_ABSENT, 0, "hwmon cooler drivers (no hidraw)", s_link, 0, "sensors" },
    { "link-devices", "hardware", "AIO Link devices", B_ABSENT, 0, "hwmon cooler drivers (no hidraw)", s_link, 0, "sensors" },
    { "link-dump", "hardware", "AIO Link dump", B_ABSENT, 0, "hwmon cooler drivers (no hidraw)", s_link, 0, "sensors" },
    { "link-fans", "hardware", "AIO Link fans", B_ABSENT, 0, "hwmon cooler drivers (no hidraw)", s_link, 0, "sensors" },
    { "link-leds", "hardware", "AIO Link LEDs", B_ABSENT, 0, "hwmon cooler drivers (no hidraw)", s_link, 0, "sensors" },
    { "link-limits", "hardware", "AIO Link limits", B_ABSENT, 0, "hwmon cooler drivers (no hidraw)", s_link, 0, "sensors" },
    { "link-power", "hardware", "AIO Link power", B_ABSENT, 0, "hwmon cooler drivers (no hidraw)", s_link, 0, "sensors" },
    { "link-setup", "hardware", "AIO Link setup", B_ABSENT, 0, "hwmon cooler drivers (no hidraw)", s_link, 0, "sensors" },
    { "link-status", "hardware", "AIO Link status", B_ABSENT, 0, "hwmon cooler drivers (no hidraw)", s_link, 0, "sensors" },
    { "link-test", "hardware", "AIO Link test", B_ABSENT, 0, "hwmon cooler drivers (no hidraw)", s_link, 0, "sensors" },
    { "link-update", "hardware", "AIO Link update", B_ABSENT, 0, "hwmon cooler drivers (no hidraw)", s_link, 0, "sensors" },
    { "lcd-panel", "hardware", "LCD panel setup", B_ABSENT, 0, "n/a (no supported LCDSysInfo/AIO LCD)", s_lcd_panel, 0, "sensors" },
    { NULL, NULL, NULL, B_LINUX, 0, NULL, NULL, 0, NULL }
};
