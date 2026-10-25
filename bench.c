/* bench.c - Dhrystone 2.1, SIV benchmark modes, IPI latency, memory latency (see bench.h). */
#define _GNU_SOURCE
#include "bench.h"
#include "burner.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void pin_self(int cpu)
{
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(cpu, &s);
    pthread_setaffinity_np(pthread_self(), sizeof s, &s);
}

/* ======================================================================
 * Dhrystone 2.1 (R. P. Weicker, 1988) - globals are thread-local so every CPU runs its own copy
 * ====================================================================== */
typedef enum { Ident_1, Ident_2, Ident_3, Ident_4, Ident_5 } Enumeration;
typedef struct record {
    struct record *Ptr_Comp;
    Enumeration Discr;
    union {
        struct { Enumeration Enum_Comp; int Int_Comp; char Str_Comp[31]; } var_1;
        struct { Enumeration E_Comp_2; char Str_2_Comp[31]; } var_2;
        struct { char Ch_1_Comp; char Ch_2_Comp; } var_3;
    } variant;
} Rec_Type, *Rec_Pointer;

static __thread Rec_Pointer Ptr_Glob, Next_Ptr_Glob;
static __thread int Int_Glob, Bool_Glob;
static __thread char Ch_1_Glob, Ch_2_Glob;
static __thread int Arr_1_Glob[50];
static __thread int Arr_2_Glob[50][50];

static void Proc_7(int a, int b, int *r) { int l = a + 2; *r = b + l; }
static int Func_3(Enumeration e) { return e == Ident_3; }

static void Proc_6(Enumeration v, Enumeration *r)
{
    *r = v;
    if (!Func_3(v)) *r = Ident_4;
    switch (v) {
    case Ident_1: *r = Ident_1; break;
    case Ident_2: *r = Int_Glob > 100 ? Ident_1 : Ident_4; break;
    case Ident_3: *r = Ident_2; break;
    case Ident_4: break;
    case Ident_5: *r = Ident_3; break;
    }
}

static void Proc_3(Rec_Pointer *r)
{
    if (Ptr_Glob) *r = Ptr_Glob->Ptr_Comp;
    Proc_7(10, Int_Glob, &Ptr_Glob->variant.var_1.Int_Comp);
}

static void Proc_1(Rec_Pointer p)
{
    Rec_Pointer next = p->Ptr_Comp;
    *p->Ptr_Comp = *Ptr_Glob;
    p->variant.var_1.Int_Comp = 5;
    next->variant.var_1.Int_Comp = p->variant.var_1.Int_Comp;
    next->Ptr_Comp = p->Ptr_Comp;
    Proc_3(&next->Ptr_Comp);
    if (next->Discr == Ident_1) {
        next->variant.var_1.Int_Comp = 6;
        Proc_6(p->variant.var_1.Enum_Comp, &next->variant.var_1.Enum_Comp);
        next->Ptr_Comp = Ptr_Glob->Ptr_Comp;
        Proc_7(next->variant.var_1.Int_Comp, 10, &next->variant.var_1.Int_Comp);
    } else {
        *p = *p->Ptr_Comp;
    }
}

static void Proc_2(int *r)
{
    int l = *r + 10;
    Enumeration e = Ident_2;
    do
        if (Ch_1_Glob == 'A') { l -= 1; *r = l - Int_Glob; e = Ident_1; }
    while (e != Ident_1);
}

static void Proc_4(void) { int b = Ch_1_Glob == 'A'; Bool_Glob = b | Bool_Glob; Ch_2_Glob = 'B'; }
static void Proc_5(void) { Ch_1_Glob = 'A'; Bool_Glob = 0; }

static void Proc_8(int a1[50], int a2[50][50], int v1, int v2)
{
    int i, l = v1 + 5;
    a1[l] = v2;
    a1[l + 1] = a1[l];
    a1[l + 30] = l;
    for (i = l; i <= l + 1; ++i) a2[l][i] = l;
    a2[l][l - 1] += 1;
    a2[l + 20][l] = a1[l];
    Int_Glob = 5;
}

static Enumeration Func_1(char c1, char c2)
{
    char l1 = c1, l2 = l1;
    if (l2 != c2) return Ident_1;
    Ch_1_Glob = l1;
    return Ident_2;
}

static int Func_2(const char *s1, const char *s2)
{
    int l = 2;
    char c = 'A';
    while (l <= 2)
        if (Func_1(s1[l], s2[l + 1]) == Ident_1) { c = 'A'; l += 1; }
    if (c >= 'W' && c < 'Z') l = 7;
    if (c == 'R') return 1;
    if (strcmp(s1, s2) > 0) { l += 7; Int_Glob = l; return 1; }
    return 0;
}

double dhrystone_dmips(long runs, double *secs)
{
    Rec_Type a, b;
    char s1[31], s2[31];
    int i1 = 0, i2 = 0, i3 = 0;
    Enumeration e;
    long run;
    char ci;
    double t0, dt;
    volatile int sink;

    Next_Ptr_Glob = &a;
    Ptr_Glob = &b;
    Ptr_Glob->Ptr_Comp = Next_Ptr_Glob;
    Ptr_Glob->Discr = Ident_1;
    Ptr_Glob->variant.var_1.Enum_Comp = Ident_3;
    Ptr_Glob->variant.var_1.Int_Comp = 40;
    strcpy(Ptr_Glob->variant.var_1.Str_Comp, "DHRYSTONE PROGRAM, SOME STRING");
    strcpy(s1, "DHRYSTONE PROGRAM, 1'ST STRING");
    Arr_2_Glob[8][7] = 10;
    t0 = now_s();
    for (run = 1; run <= runs; ++run) {
        Proc_5();
        Proc_4();
        i1 = 2;
        i2 = 3;
        strcpy(s2, "DHRYSTONE PROGRAM, 2'ND STRING");
        e = Ident_2;
        Bool_Glob = !Func_2(s1, s2);
        while (i1 < i2) { i3 = 5 * i1 - i2; Proc_7(i1, i2, &i3); i1 += 1; }
        Proc_8(Arr_1_Glob, Arr_2_Glob, i1, i3);
        Proc_1(Ptr_Glob);
        for (ci = 'A'; ci <= Ch_2_Glob; ++ci)
            if (e == Func_1(ci, 'C')) {
                Proc_6(Ident_1, &e);
                strcpy(s2, "DHRYSTONE PROGRAM, 3'RD STRING");
                i2 = (int)run;
                Int_Glob = (int)run;
            }
        i2 = i2 * i1;
        i1 = i2 / i3;
        i2 = 7 * (i2 - i3) - i1;
        Proc_2(&i1);
    }
    dt = now_s() - t0;
    sink = i1 + i2 + i3 + Int_Glob + (int)e;
    (void)sink;
    if (secs) *secs = dt;
    return dt > 0 ? (double)runs / dt / 1757.0 : 0;   /* VAX 11/780 = 1757 Dhrystones/s = 1 DMIPS */
}

/* ======================================================================
 * benchmark modes
 * ====================================================================== */
static const char *const mode_ids[BM_COUNT] = { "benchmark-ac", "benchmark-as", "benchmark-dd", "benchmark-dw", "benchmark-ec",
                                                "benchmark-ep", "benchmark-lc", "benchmark-lp", "benchmark-pc", "benchmark-pp" };
static const char *const mode_desc[BM_COUNT] = {
    "All CPUs concurrently: Dhrystone then Whetstone on every logical CPU at once",
    "All CPUs sequentially: Dhrystone then Whetstone, one logical CPU at a time",
    "Per core D+D then W+W: both SMT threads of a core run Dhrystone, then both run Whetstone",
    "Per core D+W then W+D: one SMT thread runs Dhrystone while its sibling runs Whetstone, then swap",
    "Efficiency cores concurrently", "Efficiency cores one at a time",
    "Logical (SMT sibling) threads concurrently", "Logical (SMT sibling) threads one at a time",
    "Performance (physical) cores concurrently - one thread per core", "Performance (physical) cores one at a time" };

const char *bench_mode_id(bench_mode m) { return m < BM_COUNT ? mode_ids[m] : "benchmark"; }
const char *bench_mode_desc(bench_mode m) { return m < BM_COUNT ? mode_desc[m] : ""; }

typedef struct { int cpu; double secs; char kind; double dmips, mwips; pthread_t t; } job;

static long calib_d, calib_w;
static double calib_secs;

static void calibrate(double secs)
{
    double t;
    if (calib_d && calib_secs == secs) return;   /* re-calibrate when the measurement time changes */
    calib_secs = secs;
    dhrystone_dmips(200000, &t);
    calib_d = (long)(200000 * (secs / (t > 0 ? t : 1e-3)));
    whetstone_mwips(500, &t);
    calib_w = (long)(500 * (secs / (t > 0 ? t : 1e-3)));
    if (calib_d < 1000) calib_d = 1000;
    if (calib_w < 10) calib_w = 10;
}

static void *job_main(void *arg)
{
    job *j = arg;
    pin_self(j->cpu);
    if (j->kind == 'D' || j->kind == 'B') j->dmips = dhrystone_dmips(calib_d, NULL);
    if (j->kind == 'W' || j->kind == 'B') j->mwips = whetstone_mwips(calib_w, NULL);
    return NULL;
}

static void run_set(job *js, int n)   /* concurrently */
{
    int i;
    for (i = 0; i < n; i++) pthread_create(&js[i].t, NULL, job_main, &js[i]);
    for (i = 0; i < n; i++) pthread_join(js[i].t, NULL);
}

static int sibling_of(int cpu)
{
    char p[128], b[64];
    int a, c;
    snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu);
    if (!rd_str(p, b, sizeof b)) return -1;
    if (sscanf(b, "%d%*[,-]%d", &a, &c) == 2) return a == cpu ? c : a;
    return -1;
}

static const char *cache_dir(void)
{
    static char d[1024];
    const char *h = getenv("HOME");
    snprintf(d, sizeof d, "%s/.cache", h ? h : "/tmp");
    mkdir(d, 0755);   /* recursive: ~/.cache may not exist yet */
    snprintf(d, sizeof d, "%s/.cache/srs", h ? h : "/tmp");
    mkdir(d, 0755);
    return d;
}

/* Hybrid CPUs: Linux lists the efficiency cores of Intel hybrid parts in /sys/devices/cpu_atom/cpus
   (and the performance cores in cpu_core/cpus). Fills is_e[cpu]; returns the number of E-cores. */
int bench_ecores(unsigned char *is_e, int ncpu)
{
    char b[512], *t, *save = NULL;
    int n = 0;
    memset(is_e, 0, (size_t)ncpu);
    if (!rd_str("/sys/devices/cpu_atom/cpus", b, sizeof b) || !*b) return 0;
    for (t = strtok_r(b, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
        int a, z, i;
        if (sscanf(t, "%d-%d", &a, &z) != 2) { if (sscanf(t, "%d", &a) != 1) continue; z = a; }
        for (i = a; i <= z && i < ncpu; i++) if (i >= 0 && !is_e[i]) { is_e[i] = 1; n++; }
    }
    return n;
}

typedef struct { int cpu; double d, w; } brow;

static void render_bench(srs_out *o, bench_mode m, const brow *r, int n, double secs, const char *note)
{
    static const int tabs[] = { 80, 200, 320 };
    double td = 0, tw = 0;
    int i;
    char t[160];
    time_t now = time(NULL);
    snprintf(t, sizeof t, "CPU Benchmark [%s]", mode_ids[m] + 10);
    so_siv_header(o, t);
    so_tabs(o, tabs, 3);
    so_kv(o, "Mode", C_TEAL, mode_desc[m]);
    strftime(t, sizeof t, "%Y-%m-%d %H:%M:%S", localtime(&now));
    so_kvf(o, "Run", C_GRAY, "%s, %.1f s per measurement", t, secs);
    if (note) so_kv(o, "Note", C_ORANGE, note);
    so_par(o);
    so_row(o, 3, (const char *const[]){ "CPU", "Dhrystone", "Whetstone" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < n; i++) {
        char c[3][32];
        snprintf(c[0], 32, "CPU-%d", r[i].cpu);
        snprintf(c[1], 32, r[i].d > 0 ? "%.0f DMIPS" : "-", r[i].d);
        snprintf(c[2], 32, r[i].w > 0 ? "%.0f MWIPS" : "-", r[i].w);
        so_row(o, 3, (const char *const[]){ c[0], c[1], c[2] }, (const int[]){ C_LABEL, C_BLACK, C_BLACK });
        td += r[i].d;
        tw += r[i].w;
    }
    so_par(o);
    so_f(o, C_GREEN, "Total"); so_tab(o); so_f(o, C_GREEN, "%.0f DMIPS", td); so_tab(o); so_f(o, C_GREEN, "%.0f MWIPS", tw); so_par(o);
    so_board_footer(o);
}

static void save_render(const char *name, void (*fn)(srs_out *, void *), void *ctx, srs_out *o)
{
    char path[1100];
    FILE *f;
    srs_out to;
    snprintf(path, sizeof path, "%s/bench-%s.txt", cache_dir(), name);
    if ((f = fopen(path, "w"))) { so_text_init(&to, f); fn(&to, ctx); fclose(f); }
    if (o) fn(o, ctx);
}

typedef struct { bench_mode m; brow *r; int n; double secs; const char *note; } bctx;
static void bench_fn(srs_out *o, void *c) { bctx *b = c; render_bench(o, b->m, b->r, b->n, b->secs, b->note); }

int bench_run(bench_mode m, double secs, srs_out *o)
{
    int ncpu = burn_ncpu(), i, n = 0;
    brow r[BURN_MAXCPU];
    job js[BURN_MAXCPU];
    bctx c;
    unsigned char is_e[BURN_MAXCPU];
    int ne;
    if (ncpu > BURN_MAXCPU) ncpu = BURN_MAXCPU;
    ne = bench_ecores(is_e, ncpu);
    if ((m == BM_EC || m == BM_EP) && !ne) {
        if (o) so_err(o, "No efficiency cores detected (Linux lists them in /sys/devices/cpu_atom/cpus on hybrid CPUs)");
        return -1;
    }
    calibrate(secs);
    memset(r, 0, sizeof r);
    memset(js, 0, sizeof js);
    switch (m) {
    case BM_AC: case BM_PC: case BM_LC: case BM_EC:
        for (i = 0; i < ncpu; i++) {
            int sib = sibling_of(i), primary = sib < 0 || i < sib;
            if ((m == BM_PC && (!primary || is_e[i])) || (m == BM_LC && primary)) continue;
            if (m == BM_EC && !is_e[i]) continue;
            js[n].cpu = i; js[n].kind = 'D'; n++;
        }
        run_set(js, n);
        for (i = 0; i < n; i++) { js[i].kind = 'W'; r[i].cpu = js[i].cpu; r[i].d = js[i].dmips; }
        run_set(js, n);
        for (i = 0; i < n; i++) r[i].w = js[i].mwips;
        break;
    case BM_AS: case BM_PP: case BM_LP: case BM_EP:
        for (i = 0; i < ncpu; i++) {
            int sib = sibling_of(i), primary = sib < 0 || i < sib;
            if ((m == BM_PP && (!primary || is_e[i])) || (m == BM_LP && primary)) continue;
            if (m == BM_EP && !is_e[i]) continue;
            js[0].cpu = i; js[0].kind = 'B';
            run_set(js, 1);
            r[n].cpu = i; r[n].d = js[0].dmips; r[n].w = js[0].mwips;
            n++;
        }
        break;
    case BM_DD: case BM_DW:
        for (i = 0; i < ncpu; i++) {
            int sib = sibling_of(i);
            if (sib >= 0 && sib < i) continue;
            js[0].cpu = i; js[1].cpu = sib < 0 ? i : sib;
            if (m == BM_DD) {
                js[0].kind = js[1].kind = 'D'; run_set(js, sib < 0 ? 1 : 2);
                r[n].cpu = i; r[n].d = js[0].dmips; r[n + 1].cpu = js[1].cpu; r[n + 1].d = js[1].dmips;
                js[0].kind = js[1].kind = 'W'; run_set(js, sib < 0 ? 1 : 2);
                r[n].w = js[0].mwips; r[n + 1].w = js[1].mwips;
            } else {
                js[0].kind = 'D'; js[1].kind = 'W'; run_set(js, sib < 0 ? 1 : 2);
                r[n].cpu = i; r[n].d = js[0].dmips; r[n + 1].cpu = js[1].cpu; r[n + 1].w = js[1].mwips;
                js[0].kind = 'W'; js[1].kind = 'D'; run_set(js, sib < 0 ? 1 : 2);
                r[n].w = js[0].mwips; r[n + 1].d = js[1].dmips;
            }
            n += sib < 0 ? 1 : 2;
        }
        break;
    default: break;
    }
    c.m = m; c.r = r; c.n = n; c.secs = secs; c.note = NULL;
    save_render(mode_ids[m], bench_fn, &c, o);
    return 0;
}

/* ======================================================================
 * IPI / core-to-core latency: two pinned threads bounce a cache line
 * ====================================================================== */
typedef struct { int a, b, rounds; _Atomic int flag; double ns; } pp;

static void *pong(void *arg)
{
    pp *p = arg;
    int i;
    pin_self(p->b);
    for (i = 0; i < p->rounds; i++) {
        while (atomic_load_explicit(&p->flag, memory_order_acquire) != 1) ;
        atomic_store_explicit(&p->flag, 0, memory_order_release);
    }
    return NULL;
}

static double pingpong(int a, int b, int rounds)
{
    pp p;
    pthread_t t;
    double t0;
    int i;
    memset(&p, 0, sizeof p);
    p.a = a; p.b = b; p.rounds = rounds;
    atomic_store(&p.flag, 0);
    p.rounds = rounds + rounds / 2;   /* first third is untimed warm-up (thread start, pinning, clocks) */
    pin_self(a);
    pthread_create(&t, NULL, pong, &p);
    t0 = 0;
    for (i = 0; i < p.rounds; i++) {
        if (i == rounds / 2) t0 = now_s();
        atomic_store_explicit(&p.flag, 1, memory_order_release);
        while (atomic_load_explicit(&p.flag, memory_order_acquire) != 0) ;
    }
    p.ns = (now_s() - t0) * 1e9 / (2.0 * rounds);
    pthread_join(t, NULL);
    return p.ns;
}

typedef struct { int n; double *m; int rounds; } ictx;
static double *g_ipi_m;
static int g_ipi_n;
const double *bench_ipi_last(int *n) { *n = g_ipi_n; return g_ipi_m; }
static void ipi_fn(srs_out *o, void *c)
{
    ictx *x = c;
    int i, j, tabs[64], nt = x->n < 63 ? x->n : 63;
    for (i = 0; i < nt; i++) tabs[i] = 60 + i * 50;
    so_siv_header(o, "CPU IPI Delay (core-to-core cache-line round trip / 2, ns)");
    so_kvf(o, "Rounds per pair", C_GRAY, "%d", x->rounds);
    so_par(o);
    so_tabs(o, tabs, nt);
    so_t(o, C_LABEL, "From\\To");
    for (j = 0; j < x->n; j++) { so_tab(o); so_f(o, C_LABEL, "%d", j); }
    so_par(o);
    for (i = 0; i < x->n; i++) {
        so_f(o, C_LABEL, "CPU-%d", i);
        for (j = 0; j < x->n; j++) {
            double v = x->m[i * x->n + j];
            so_tab(o);
            if (i == j) so_t(o, C_GRAY, "-");
            else so_f(o, v < 40 ? C_GREEN : v < 100 ? C_BLACK : C_ORANGE, "%.0f", v);
        }
        so_par(o);
    }
    so_board_footer(o);
}

int bench_ipi(int max_cpus, int rounds, srs_out *o)
{
    int n = burn_ncpu(), i, j;
    ictx c;
    cpu_set_t saved;
    if (max_cpus > 0 && max_cpus < n) n = max_cpus;
    if (rounds <= 0) rounds = 20000;
    c.n = n; c.rounds = rounds;
    c.m = calloc((size_t)(n * n), sizeof(double));
    if (!c.m) return -1;
    sched_getaffinity(0, sizeof saved, &saved);
    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++) c.m[i * n + j] = c.m[j * n + i] = pingpong(i, j, rounds);
    sched_setaffinity(0, sizeof saved, &saved);
    save_render("ipi-delay", ipi_fn, &c, o);
    free(g_ipi_m);
    g_ipi_m = c.m;   /* kept for the GUI heat map */
    g_ipi_n = n;
    return 0;
}

/* ======================================================================
 * memory latency: dependent random pointer chase over growing block sizes
 * ====================================================================== */
/* data / unified cache size per level (index 1..3, bytes; 0 = none) from cpu0's sysfs cache info */
static void cache_sizes(long sz[4])
{
    int i;
    memset(sz, 0, sizeof(long) * 4);
    for (i = 0; i < 16; i++) {
        char b[64], t[32];
        long lv, kb;
        char unit = 0;
        if (!rd_fmt(b, sizeof b, "/sys/devices/system/cpu/cpu0/cache/index%d/level", i)) break;
        lv = atol(b);
        rd_fmt(t, sizeof t, "/sys/devices/system/cpu/cpu0/cache/index%d/type", i);
        if (!strcmp(t, "Instruction") || lv < 1 || lv > 3) continue;
        if (!rd_fmt(b, sizeof b, "/sys/devices/system/cpu/cpu0/cache/index%d/size", i)) continue;
        if (sscanf(b, "%ld%c", &kb, &unit) < 1) continue;
        sz[lv] = unit == 'M' ? kb * 1024 * 1024 : unit == 'G' ? kb << 30 : kb * 1024;
    }
}

/* level a block of this size mostly lives in (first level whose cache holds it) */
static const char *lat_level(long size, const long csz[4])
{
    static const char *const nm[] = { "", "L1", "L2", "L3" };
    int l;
    for (l = 1; l <= 3; l++) if (csz[l] && size <= csz[l]) return nm[l];
    return csz[1] ? "DRAM" : "?";
}

static volatile size_t g_lat_sink;
typedef struct { int n; long size[40]; double ns[40]; } lctx;
static lctx g_lat;
int bench_latency_last(const long **sizes, const double **ns) { *sizes = g_lat.size; *ns = g_lat.ns; return g_lat.n; }
static void lat_fn(srs_out *o, void *c)
{
    static const int tabs[] = { 120, 240 };
    lctx *x = c;
    int i;
    long csz[4];
    cache_sizes(csz);
    so_siv_header(o, "Memory Latency (random dependent loads, 64-byte lines)");
    so_tabs(o, tabs, 2);
    so_row(o, 3, (const char *const[]){ "Block", "Latency", "Level" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < x->n; i++) {
        char sz[32], ns[32];
        const char *lvl = lat_level(x->size[i], csz);
        fmt_bytes((double)x->size[i], sz, sizeof sz);
        snprintf(ns, sizeof ns, "%.2f ns", x->ns[i]);
        so_row(o, 3, (const char *const[]){ sz, ns, lvl }, (const int[]){ C_VALUE, C_BLACK, C_TEAL });
    }
    so_par(o);
    {
        char a[32], b2[32], c3[32], line[160];
        fmt_bytes((double)csz[1], a, sizeof a); fmt_bytes((double)csz[2], b2, sizeof b2); fmt_bytes((double)csz[3], c3, sizeof c3);
        snprintf(line, sizeof line, "Level = smallest cache the block fits in (L1d %s, L2 %s, L3 %s from /sys/devices/system/cpu/cpu0/cache); indicative.",
                 csz[1] ? a : "-", csz[2] ? b2 : "-", csz[3] ? c3 : "-");
        so_note(o, line);
    }
}

int bench_latency(long max_bytes, srs_out *o)
{
    lctx c;
    long sz;
    size_t *buf;
    if (max_bytes <= 0) max_bytes = 256L * 1024 * 1024;
    buf = malloc((size_t)max_bytes);
    if (!buf) return -1;
    memset(&c, 0, sizeof c);
    /* Spin ~200 ms first so the core is at full clock before the small blocks
     * (which only take a few ms each) are timed. */
    { double t = now_s(); while (now_s() - t < 0.2) g_lat_sink++; }
    for (sz = 4096; sz <= max_bytes && c.n < 40; sz *= 2) {
        size_t lines = (size_t)sz / 64, i, idx = 0, stride = 64 / sizeof(size_t);
        size_t *perm = malloc(lines * sizeof(size_t));
        long hops = 2000000, h;
        double t0;
        if (!perm) break;
        for (i = 0; i < lines; i++) perm[i] = i;
        srand(12345);
        for (i = lines - 1; i > 0; i--) { size_t j = (size_t)rand() % (i + 1), t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
        for (i = 0; i < lines; i++) buf[perm[i] * stride] = perm[(i + 1) % lines] * stride;
        free(perm);
        idx = 0;
        for (h = 0; h < 10000 || (size_t)h < lines; h++) idx = buf[idx];   /* warm: touch every line once */
        /* Best of 5: an interrupt or clock dip only ever makes a pass slower,
         * so the fastest pass is the closest to the true load-to-use latency. */
        c.ns[c.n] = 0;
        for (int r = 0; r < 5; r++) {
            double ns;
            t0 = now_s();
            for (h = 0; h < hops; h++) idx = buf[idx];
            ns = (now_s() - t0) * 1e9 / (double)hops;
            if (r == 0 || ns < c.ns[c.n]) c.ns[c.n] = ns;
        }
        c.size[c.n] = sz;
        g_lat_sink = idx;   /* keep the chase observable so it is not optimised away */
        c.n++;
    }
    free(buf);
    save_render("latency", lat_fn, &c, o);
    g_lat = c;
    return 0;
}

int bench_show_last(const char *name, srs_out *o)
{
    char path[1100], *txt;
    snprintf(path, sizeof path, "%s/bench-%s.txt", cache_dir(), name);
    if (rd_all(path, &txt) < 0) return 0;
    so_lines(o, C_BLACK, txt);
    free(txt);
    return 1;
}
