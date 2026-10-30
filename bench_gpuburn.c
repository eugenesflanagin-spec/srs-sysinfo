/* bench_gpuburn.c - GPU Burnin (see bench_gpuburn.h). Written for SRS, inspired by gpu-burn.
 * One worker thread per GPU: it opens the GPU through the chosen back end (gpu_backend.h), runs a matrix
 * multiplication over and over in short pieces and counts results that differ from the first one. The
 * calling thread reads the sensors, applies the temperature limit, calls the live callback and handles Stop.
 * Everything a back end opens is closed by the worker that opened it. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "bench.h"
#include "bench_gpu.h"
#include "bench_gpuburn.h"
#include "burner.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __linux__
#include <glob.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

#define PIECE_S 0.020        /* GPU time per piece of work */
#define PIECE_MAX_S 2.0      /* a piece this slow ends the run */
#define HANG_WAIT_S 15.0     /* after the end: how long a worker stuck in a driver call is waited for */

static const char *volatile g_stage = "";
static char g_stage_buf[2][160];   /* two buffers: the GUI timer reads one while the next is written */
static int g_stage_i;

/* the stage text for the GUI timer, written into the buffer the reader is not on */
static void set_stage_text(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_stage_text(const char *fmt, ...)
{
    va_list ap;
    char *b = g_stage_buf[g_stage_i ^= 1];
    va_start(ap, fmt);
    vsnprintf(b, sizeof g_stage_buf[0], fmt, ap);
    va_end(ap);
    g_stage = b;
}
const char *gpuburn_stage(void) { return g_stage; }

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

void gpuburn_cfg_default(gpuburn_cfg *c)
{
    memset(c, 0, sizeof *c);
    c->backend = GPUBURN_AUTO;
    c->device = -1;
    c->prec = GP_FP32;
    c->seconds = 60;
    c->limit_c = 100;
    c->n_max = 4096;
}

/* ---------------------------------------------------------------- sensors */
struct gpu_hw {
    char dir[320];
    int t_edge, t_junc, t_mem;     /* tempN number, 0 = none */
    char pow[16];                  /* power1_average / power1_input / "" */
    int freq, fan;
};

static int hw_long(const char *dir, const char *f, long *v)
{
    char p[400], b[64];
    FILE *fp;
    snprintf(p, sizeof p, "%s/%s", dir, f);
    if (!(fp = fopen(p, "r"))) return 0;
    if (!fgets(b, sizeof b, fp)) { fclose(fp); return 0; }
    fclose(fp);
    *v = atol(b);
    return 1;
}

static void hw_str(const char *dir, const char *f, char *out, size_t n)
{
    char p[400];
    FILE *fp;
    out[0] = 0;
    snprintf(p, sizeof p, "%s/%s", dir, f);
    if (!(fp = fopen(p, "r"))) return;
    if (fgets(out, (int)n, fp)) out[strcspn(out, "\n")] = 0;
    fclose(fp);
}

gpu_hw *gpu_hw_open(const char *pci)
{
    glob_t g;
    char pat[300], dir[320] = "", nm[64];
    gpu_hw *h;
    long v;
    int i;
    if (pci && pci[0] && strlen(pci) < 16) {
        snprintf(pat, sizeof pat, "/sys/bus/pci/devices/%s/hwmon/hwmon*", pci);
        if (!glob(pat, 0, NULL, &g) && g.gl_pathc) snprintf(dir, sizeof dir, "%s", g.gl_pathv[0]);
        globfree(&g);
    } else {   /* no PCI address from the back end: use the only GPU hwmon there is */
        int found = 0;
        if (!glob("/sys/class/drm/card?/device/hwmon/hwmon*", 0, NULL, &g)) {
            size_t k;
            for (k = 0; k < g.gl_pathc; k++) {
                hw_str(g.gl_pathv[k], "name", nm, sizeof nm);
                if (!strcmp(nm, "amdgpu") || !strcmp(nm, "i915") || !strcmp(nm, "xe") || !strcmp(nm, "nouveau")) { found++; snprintf(dir, sizeof dir, "%s", g.gl_pathv[k]); }
            }
        }
        globfree(&g);
        if (found != 1) dir[0] = 0;
    }
    if (!dir[0] || !(h = calloc(1, sizeof *h))) return NULL;
    snprintf(h->dir, sizeof h->dir, "%s", dir);
    for (i = 1; i <= 8; i++) {
        char f[24], lb[32];
        snprintf(f, sizeof f, "temp%d_input", i);
        if (!hw_long(h->dir, f, &v)) continue;
        snprintf(f, sizeof f, "temp%d_label", i);
        hw_str(h->dir, f, lb, sizeof lb);
        if (!strcmp(lb, "edge")) h->t_edge = i;
        else if (!strcmp(lb, "junction")) h->t_junc = i;
        else if (!strcmp(lb, "mem")) h->t_mem = i;
        else if (!lb[0] && !h->t_edge) h->t_edge = i;
    }
    if (hw_long(h->dir, "power1_average", &v)) snprintf(h->pow, sizeof h->pow, "power1_average");
    else if (hw_long(h->dir, "power1_input", &v)) snprintf(h->pow, sizeof h->pow, "power1_input");
    h->freq = hw_long(h->dir, "freq1_input", &v);
    h->fan = hw_long(h->dir, "fan1_input", &v);
    return h;
}

void gpu_hw_read(gpu_hw *h, gpu_sensors *s)
{
    long v;
    char f[24];
    s->edge_c = s->junction_c = s->mem_c = s->power_w = s->clock_mhz = s->fan_rpm = -1;
    if (!h) return;
    if (h->t_edge) { snprintf(f, sizeof f, "temp%d_input", h->t_edge); if (hw_long(h->dir, f, &v)) s->edge_c = (double)v / 1000.0; }
    if (h->t_junc) { snprintf(f, sizeof f, "temp%d_input", h->t_junc); if (hw_long(h->dir, f, &v)) s->junction_c = (double)v / 1000.0; }
    if (h->t_mem) { snprintf(f, sizeof f, "temp%d_input", h->t_mem); if (hw_long(h->dir, f, &v)) s->mem_c = (double)v / 1000.0; }
    if (h->pow[0] && hw_long(h->dir, h->pow, &v)) s->power_w = (double)v / 1e6;
    if (h->freq && hw_long(h->dir, "freq1_input", &v)) s->clock_mhz = (double)v / 1e6;
    if (h->fan && hw_long(h->dir, "fan1_input", &v)) s->fan_rpm = (double)v;
}

void gpu_hw_close(gpu_hw *h) { free(h); }

static void smax(gpu_sensors *m, const gpu_sensors *s)
{
    if (s->edge_c > m->edge_c) m->edge_c = s->edge_c;
    if (s->junction_c > m->junction_c) m->junction_c = s->junction_c;
    if (s->mem_c > m->mem_c) m->mem_c = s->mem_c;
    if (s->power_w > m->power_w) m->power_w = s->power_w;
    if (s->clock_mhz > m->clock_mhz) m->clock_mhz = s->clock_mhz;
    if (s->fan_rpm > m->fan_rpm) m->fan_rpm = s->fan_rpm;
}

/* ---------------------------------------------------------------- listing */
int gpuburn_list(int backend, gb_info *out, int max, char *why, size_t wn)
{
    char hint[200];
    if (why && wn) why[0] = 0;
    if (backend < 0 || backend >= GB_N) return -1;
    if (!gb_table[backend]->available(hint, sizeof hint)) { if (why) snprintf(why, wn, "%s", hint); return -1; }
    return gb_table[backend]->list(out, max, why, wn);
}

/* ---------------------------------------------------------------- workers */
typedef struct {
    gpuburn_dev d;
    pthread_mutex_t mu;
    gb_info info;
    const gb_ops *ops;
    const gpuburn_cfg *c;
    struct run *run;
    pthread_t t;
    gpu_hw *hw;
    double wsum, tsum;               /* all work (flop) and GPU seconds, for the average */
    _Atomic int done;                /* written by the worker, read by the controller */
    int started, abandoned;          /* the thread runs / it never came back from a driver call */
    int many;                        /* one of several devices (All GPUs): a device that cannot start does not end the others */
} worker;

typedef struct run {
    _Atomic int stop;                 /* anything: user Stop, a safety rule, a failed GPU */
    _Atomic int user;                 /* stop was the user's */
    char abort_msg[240];              /* first safety message */
    pthread_mutex_t mu;
    double t0, deadline;
    gpuburn_cfg c;                    /* the settings the workers read; on the heap, so a worker left behind never reads freed stack */
} run_t;

static void abort_run(run_t *r, const char *msg)
{
    pthread_mutex_lock(&r->mu);
    if (!r->abort_msg[0]) snprintf(r->abort_msg, sizeof r->abort_msg, "%s", msg);
    pthread_mutex_unlock(&r->mu);
    atomic_store(&r->stop, 1);
}

static void w_note(worker *w, int state, const char *fmt, const char *a)
{
    pthread_mutex_lock(&w->mu);
    w->d.state = state;
    if (fmt) snprintf(w->d.note, sizeof w->d.note, fmt, a ? a : "");
    pthread_mutex_unlock(&w->mu);
}

static const char *rc_text(int rc)
{
    return rc == GBP_HUNG ? "the GPU did not answer in time" : rc == GBP_LOST ? "the GPU stopped answering (device lost)" : "the GPU runtime reported an error";
}

static void *worker_main(void *arg)
{
    worker *w = arg;
    const gpuburn_cfg *c = w->c;
    run_t *r = w->run;
    gb_ctx *ctx = NULL;
    char why[240] = "";
    double work = 0, secs = 0;
    unsigned maxreps = 1, reps = 1;
    int n = 128, variant = GV_OWN, rc;
    long wrong = 0;
    if (n > c->n_max) n = c->n_max;
    if (w->ops->open(&ctx, &w->info, c->software_only, why, sizeof why)) {
        w_note(w, GBS_ABORTED, "could not open the GPU: %s", why);
        if (!w->many) abort_run(r, why);   /* with All GPUs the others go on */
        else { pthread_mutex_lock(&w->mu); w->d.not_started = 1; pthread_mutex_unlock(&w->mu); }
        goto out;
    }
    /* every back end: the register-blocked kernel first (it loads the GPU far harder than gemm_r; HIP and
       ROCm's OpenCL reach only about half of Vulkan's speed with gemm_r), then gemm_r, then the vendor library */
    if ((w->info.backend == GB_HIP || w->info.backend == GB_OPENCL || w->info.backend == GB_VULKAN ||
         w->info.backend == GB_CUDA) && !getenv("SRS_GPUBURN_SIMPLE")) {
        rc = w->ops->prep(ctx, GK_GEMM, c->prec, GV_FAST, (size_t)n, &work, &maxreps);
        if (rc == GBP_OK) variant = GV_FAST;
        else if (rc == GBP_UNSUPPORTED) rc = w->ops->prep(ctx, GK_GEMM, c->prec, variant, (size_t)n, &work, &maxreps);
    } else rc = w->ops->prep(ctx, GK_GEMM, c->prec, variant, (size_t)n, &work, &maxreps);
    if (rc == GBP_UNSUPPORTED) { variant = GV_LIB; rc = w->ops->prep(ctx, GK_GEMM, c->prec, variant, (size_t)n, &work, &maxreps); }
    if (rc) {
        snprintf(why, sizeof why, "%s", rc == GBP_UNSUPPORTED && !w->ops->error(ctx)[0] ? "this GPU cannot run that precision here" : w->ops->error(ctx));
        w_note(w, GBS_ABORTED, "%s", why);
        if (!w->many || rc == GBP_HUNG || rc == GBP_LOST) abort_run(r, why);
        else { pthread_mutex_lock(&w->mu); w->d.not_started = 1; pthread_mutex_unlock(&w->mu); }
        goto out;
    }
    /* grow the matrix until one multiplication takes about a piece; every result counts, also these */
    for (;;) {
        double per;
        if (atomic_load(&r->stop)) break;
        rc = w->ops->run(ctx, 1, &secs, &wrong);
        if (rc) break;
        pthread_mutex_lock(&w->mu);
        w->d.errors += wrong;
        w->d.iterations++;
        w->d.n = n;
        pthread_mutex_unlock(&w->mu);
        per = secs;
        if (per < PIECE_S / 2 && n * 2 <= c->n_max) {
            rc = w->ops->prep(ctx, GK_GEMM, c->prec, variant, (size_t)n * 2, &work, &maxreps);
            if (!rc) { n *= 2; continue; }
            if (rc == GBP_HUNG || rc == GBP_LOST) break;
            /* the bigger matrix does not fit (video memory, buffer size limit): go on at the size that worked */
            rc = w->ops->prep(ctx, GK_GEMM, c->prec, variant, (size_t)n, &work, &maxreps);
            if (rc) break;
            secs = per;
        }
        reps = per > 0 ? (unsigned)(PIECE_S / per) : 1;
        if (reps < 1) reps = 1;
        if (reps > maxreps) reps = maxreps;
        break;
    }
    pthread_mutex_lock(&w->mu);
    w->d.n = n;
    w->d.variant = variant;
    pthread_mutex_unlock(&w->mu);
    while (!rc && !atomic_load(&r->stop) && now_s() < r->deadline) {
        double t = now_s(), gf;
        rc = w->ops->run(ctx, reps, &secs, &wrong);
        if (rc) break;
        if (secs > PIECE_MAX_S) {
            char m[160];
            snprintf(m, sizeof m, "one piece of GPU work took %.1f s (expected about %.0f ms); ended to keep the desktop and the GPU safe", secs, PIECE_S * 1000);
            w_note(w, GBS_ABORTED, "%s", m);
            abort_run(r, m);
            break;
        }
        gf = secs > 0 ? work * reps / secs / 1e9 : 0;
        pthread_mutex_lock(&w->mu);
        w->d.errors += wrong;
        w->d.iterations += reps;
        w->d.gflops = gf;
        w->wsum += work * reps;
        w->tsum += secs;
        w->d.elapsed = t - r->t0;
        pthread_mutex_unlock(&w->mu);
        if (secs > PIECE_S * 2 && reps > 1) reps = (unsigned)((double)reps * PIECE_S / secs) + 1;
    }
    if (rc) {
        snprintf(why, sizeof why, "%s: %s", rc_text(rc), w->ops->error(ctx));
        w_note(w, GBS_ABORTED, "%s", why);
        abort_run(r, why);
    }
out:
    pthread_mutex_lock(&w->mu);
    if (w->d.state == GBS_RUNNING) w->d.state = r->user ? GBS_STOPPED : atomic_load(&r->stop) ? GBS_ABORTED : GBS_DONE;
    if (w->d.state == GBS_ABORTED && !w->d.note[0]) snprintf(w->d.note, sizeof w->d.note, "%s", r->abort_msg);
    pthread_mutex_unlock(&w->mu);
    if (ctx) w->ops->close(ctx);
    atomic_store(&w->done, 1);
    return NULL;
}

/* ---------------------------------------------------------------- result */
typedef struct {
    gpuburn_snap s;
    gpuburn_cfg c;
    char when[32], abort_msg[240];
    int rc;
} bx;

static void fmt_temp(char *b, size_t n, double t) { if (t < 0) snprintf(b, n, "not available"); else snprintf(b, n, "%.0f °C", t); }

static void gburn_render(srs_out *o, void *ctx)
{
    static const int tabs[] = { 200, 330, 450, 580 }, tabs2[] = { 210 };
    bx *x = ctx;
    int i;
    so_siv_header(o, "GPU Burnin (matrix multiplication stress test with a compute-error count)");
    so_tabs(o, tabs, 4);
    so_row(o, 5, (const char *const[]){ "GPU", "Result", "Errors found", "Multiplications", "Average speed" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < x->s.ndev; i++) {
        const gpuburn_dev *d = &x->s.dev[i];
        char res[48], er[24], it[24], sp[32], nm[64];
        int col = d->state == GBS_FAILED ? C_RED : d->state == GBS_ABORTED ? C_ORANGE : d->state == GBS_STOPPED ? C_ORANGE : C_GREEN;
        snprintf(res, sizeof res, "%s", d->state == GBS_FAILED ? "FAIL" : d->not_started ? "COULD NOT START" : d->state == GBS_ABORTED ? "ENDED EARLY" : d->state == GBS_STOPPED ? (d->errors ? "FAIL (stopped)" : "stopped, 0 errors") : "PASS");
        snprintf(er, sizeof er, "%ld", d->errors);
        snprintf(it, sizeof it, "%ld", d->iterations);
        snprintf(sp, sizeof sp, "%.0f GFLOPS", d->gflops_avg);
        snprintf(nm, sizeof nm, "%.60s", d->device);
        so_row(o, 5, (const char *const[]){ nm, res, er, it, sp }, (const int[]){ C_TEAL, col, d->errors ? C_RED : C_GREEN, C_BLACK, C_BLACK });
    }
    so_par(o);
    so_tabs(o, tabs2, 1);
    for (i = 0; i < x->s.ndev; i++) {
        const gpuburn_dev *d = &x->s.dev[i];
        char a[32], b[32], m[32], pw[32] = "not available", ck[32] = "not available", fn[32] = "not available";
        fmt_temp(a, sizeof a, d->max.edge_c);
        fmt_temp(b, sizeof b, d->max.junction_c);
        fmt_temp(m, sizeof m, d->max.mem_c);
        if (d->max.power_w >= 0) snprintf(pw, sizeof pw, "%.0f W", d->max.power_w);
        if (d->max.clock_mhz >= 0) snprintf(ck, sizeof ck, "%.0f MHz", d->max.clock_mhz);
        if (d->max.fan_rpm >= 0) snprintf(fn, sizeof fn, "%.0f rpm", d->max.fan_rpm);
        so_head(o, d->device);
        so_kv(o, "Back end", C_VALUE, d->label);
        so_kv(o, "Software", C_VALUE, d->runtime);
        so_kvf(o, "Matrix size", C_VALUE, "%d x %d, %s", d->n, d->n, x->c.prec == GP_FP64 ? "double precision (FP64)" : "single precision (FP32)");
        so_kv(o, "Kernel", C_VALUE, d->variant == GV_FAST ? "SRS register-blocked (64 x 64 tiles, 16 results per thread)"
                                   : d->variant == GV_LIB ? "the vendor GEMM library" : "SRS tiled (16 x 16 tiles)");
        so_kvf(o, "Time", C_VALUE, "%.0f s of %.0f s planned", d->elapsed, x->s.total);
        so_kvf(o, "Compute errors", d->errors ? C_RED : C_GREEN, "%ld wrong result%s found in %ld multiplications", d->errors, d->errors == 1 ? "" : "s", d->iterations);
        so_kvf(o, "Speed", C_VALUE, "%.0f GFLOPS average, %.0f GFLOPS in the last piece of work", d->gflops_avg, d->gflops);
        so_kvf(o, "Highest temperatures", C_VALUE, "edge %s, junction (hotspot) %s, memory %s", a, b, m);
        so_kvf(o, "Highest power, clock, fan", C_VALUE, "%s, %s, %s", pw, ck, fn);
        if (d->no_temp) so_kv(o, "Temperature limit", C_ORANGE, "not active: this GPU reports no temperature to SRS, so the run could not stop at the limit");
        if (d->note[0]) so_kv(o, d->not_started ? "Could not start" : "Ended early", C_ORANGE, d->note);
    }
    so_par(o);
    so_kvf(o, "Settings", C_VALUE, "temperature limit %.0f °C; pieces of about %.0f ms of GPU time", x->c.limit_c, PIECE_S * 1000);
    so_kv(o, "Run", C_GRAY, x->when);
    so_kv(o, "Build", C_GRAY, bench_build_tag());
    so_note(o, "How it works: the first matrix multiplication is the reference; every later one is compared with it on the GPU. "
               "Any difference is a compute error. 0 errors = PASS. Errors usually mean too much overclock or undervolt, too high a "
               "temperature, a weak power supply or a faulty card or memory.");
    so_note(o, "GFLOPS = billions of floating-point operations per second. Compare speeds only between alike SRS builds and the same "
               "GPU driver version.");
    so_board_footer(o);
}

/* ---------------------------------------------------------------- the run */
/* Auto: the first back end whose list has a real GPU (a list with only a software renderer such as llvmpipe does not
   count, unless the run is software_only) */
static int pick_backend(int want, int software_only, char *why, size_t wn)
{
    static const int order[GB_N] = { GB_VULKAN, GB_HIP, GB_CUDA, GB_OPENCL };
    int i, k, n;
    if (want >= 0 && want < GB_N) return want;
    for (i = 0; i < GB_N; i++) {
        gb_info l[GPUBURN_MAXDEV];
        char w[200];
        if (!gb_table[order[i]]->available(w, sizeof w)) continue;
        n = gb_table[order[i]]->list(l, GPUBURN_MAXDEV, w, sizeof w);
        for (k = 0; k < n; k++) if (software_only || !l[k].is_cpu) return order[i];
    }
    snprintf(why, wn, "no GPU compute back end found (Vulkan, ROCm / HIP, CUDA or OpenCL with a GPU)");
    return -1;
}

int bench_gpuburn(const gpuburn_cfg *cfg, srs_out *o, volatile int *cancel, char *why, size_t wn)
{
    const char *who = NULL;
    gb_info list[GPUBURN_MAXDEV];
    worker *w;
    run_t *run;
    bx *x;
    int backend, n, nw, i, rc = 0, abandoned = 0;
    char nostartmsg[160] = "";
    int pick[GPUBURN_MAXDEV];
    gpuburn_cfg c = *cfg;
    char lw[240];
    double end_at = 0;
    time_t now = time(NULL);
    if (why && wn) why[0] = 0;
    if (burn_active() || burn_gpu_pid_set()) { if (why) snprintf(why, wn, "Stress & Burnin is running (or its GPU part is still ending); start the GPU Burnin when it has finished"); return -1; }
    if (c.seconds < 1) c.seconds = 1;
    if (c.seconds > 86400) c.seconds = 86400;
    if (c.limit_c < 40 || c.limit_c > 130) c.limit_c = 100;
    if (c.prec != GP_FP64) c.prec = GP_FP32;
    if (c.n_max < 32) c.n_max = 32;
    if (c.n_max > 16384) c.n_max = 16384;
    if (!gpu_claim("GPU Burnin", &who)) { if (why) gpu_claim_text(who, why, wn); return -1; }
    if (burn_active()) { gpu_release(); if (why) snprintf(why, wn, "Stress & Burnin is running; start the GPU test when it has finished"); return -1; }   /* it started at the same moment */
    if ((backend = pick_backend(c.backend, c.software_only, lw, sizeof lw)) < 0) { if (why) snprintf(why, wn, "%s", lw); gpu_release(); return -1; }
    if (!gb_table[backend]->available(lw, sizeof lw)) { if (why) snprintf(why, wn, "%s: %s", gb_name[backend], lw); gpu_release(); return -1; }
    n = gb_table[backend]->list(list, GPUBURN_MAXDEV, lw, sizeof lw);
    if (n <= 0) { if (why) snprintf(why, wn, "%s: %s", gb_name[backend], lw[0] ? lw : "no device found"); gpu_release(); return -1; }
    nw = 0;
    if (c.device == GPUBURN_ALL) {   /* every GPU; a CPU / software device only in the test mode */
        for (i = 0; i < n && nw < GPUBURN_MAXDEV; i++) if (c.software_only || !list[i].is_cpu) pick[nw++] = i;
        if (!nw) { if (why) snprintf(why, wn, "%s lists no GPU (only a software renderer, which GPU Burnin does not burn)", gb_name[backend]); gpu_release(); return -1; }
    } else {
        int d = c.device;
        if (d < 0 && c.device_pci[0]) {   /* the GPU the user picked, found in this back end's own list by its PCI address */
            if ((d = gb_find_by_pci(list, n, c.device_pci)) < 0) {
                if (why) snprintf(why, wn, "%s does not list the GPU you picked (PCI %s); choose \"The first GPU\" or the Vulkan back end", gb_name[backend], c.device_pci);
                gpu_release();
                return -1;
            }
        } else if (d < 0) {
            for (i = 0; i < n; i++) if (!list[i].is_cpu) { d = i; break; }
            if (d < 0 && c.software_only) d = 0;
            if (d < 0) { if (why) snprintf(why, wn, "%s lists no GPU (only a software renderer, which GPU Burnin does not burn)", gb_name[backend]); gpu_release(); return -1; }
        }
        if (d >= n) { if (why) snprintf(why, wn, "%s has no device %d (found %d)", gb_name[backend], d, n); gpu_release(); return -1; }
        if (list[d].is_cpu && !c.software_only) {
            if (why) snprintf(why, wn, "%s is a CPU / software renderer, not a GPU; GPU Burnin does not burn it", list[d].device);
            gpu_release();
            return -1;
        }
        pick[nw++] = d;
    }
    w = calloc((size_t)nw, sizeof *w);
    run = calloc(1, sizeof *run);
    x = calloc(1, sizeof *x);
    if (!w || !run || !x) { free(w); free(run); free(x); if (why) snprintf(why, wn, "out of memory"); gpu_release(); return -1; }
    pthread_mutex_init(&run->mu, NULL);
    run->c = c;
    run->t0 = now_s();
    run->deadline = run->t0 + c.seconds;
    for (i = 0; i < nw; i++) {
        worker *k = &w[i];
        pthread_mutex_init(&k->mu, NULL);
        k->info = list[pick[i]];
        k->ops = gb_table[backend];
        k->c = &run->c;
        k->run = run;
        k->many = nw > 1;
        k->hw = k->info.is_cpu ? NULL : gpu_hw_open(k->info.pci);   /* a software renderer has no sensors */
        k->d.state = GBS_RUNNING;
        snprintf(k->d.label, sizeof k->d.label, "%s", k->info.label);
        snprintf(k->d.device, sizeof k->d.device, "%s", k->info.device);
        snprintf(k->d.runtime, sizeof k->d.runtime, "%s", k->info.runtime);
        gpu_hw_read(k->hw, &k->d.now);
        k->d.max = k->d.now;
        k->d.no_temp = !k->info.is_cpu && k->d.now.edge_c < 0 && k->d.now.junction_c < 0;
        if (pthread_create(&k->t, NULL, worker_main, k)) {   /* never join a thread that did not start */
            k->d.state = GBS_ABORTED;
            snprintf(k->d.note, sizeof k->d.note, "could not start a worker thread");
            if (!k->many) abort_run(run, k->d.note);
            else k->d.not_started = 1;
            atomic_store(&k->done, 1);
        } else k->started = 1;
    }
    /* controller: sensors, temperature limit, live readout, Stop */
    for (;;) {
        int alldone = 1;
        struct timespec ts = { 0, 250000000L };
        gpuburn_snap *s = &x->s;
        double tnow;
        nanosleep(&ts, NULL);
        if (cancel && *cancel && !atomic_load(&run->stop)) { atomic_store(&run->user, 1); atomic_store(&run->stop, 1); }
        tnow = now_s();
        if (!end_at && (atomic_load(&run->stop) || tnow >= run->deadline)) end_at = tnow;
        s->ndev = nw;
        s->total = c.seconds;
        s->elapsed = now_s() - run->t0;
        s->running = 1;
        for (i = 0; i < nw; i++) {
            worker *k = &w[i];
            gpu_sensors sn;
            double hot;
            gpu_hw_read(k->hw, &sn);
            hot = sn.junction_c >= 0 ? sn.junction_c : sn.edge_c;
            pthread_mutex_lock(&k->mu);
            k->d.now = sn;
            smax(&k->d.max, &sn);
            k->d.gflops_avg = k->tsum > 0 ? k->wsum / k->tsum / 1e9 : 0;
            k->d.elapsed = s->elapsed;
            k->d.iter_per_s = s->elapsed > 0 ? (double)k->d.iterations / s->elapsed : 0;   /* the live readout showed 0.0 */
            s->dev[i] = k->d;
            pthread_mutex_unlock(&k->mu);
            if (hot >= c.limit_c) {
                char m[200];
                snprintf(m, sizeof m, "the GPU reached %.0f °C (limit %.0f °C); ended to protect it", hot, c.limit_c);
                w_note(k, GBS_ABORTED, "%s", m);
                abort_run(run, m);
            }
            if (!atomic_load(&k->done)) alldone = 0;
        }
        set_stage_text("GPU Burnin: %.0f s of %.0f s", s->elapsed, c.seconds);
        if (c.cb) c.cb(s, c.ud);
        if (alldone) break;
        if (end_at && tnow - end_at > HANG_WAIT_S) {   /* a worker is stuck inside a GPU driver call: do not wait forever */
            atomic_store(&run->stop, 1);
            for (i = 0; i < nw; i++) {
                worker *k = &w[i];
                if (atomic_load(&k->done)) continue;
                k->abandoned = 1;
                abandoned = 1;
                pthread_mutex_lock(&k->mu);
                k->d.state = GBS_ABORTED;
                snprintf(k->d.note, sizeof k->d.note, "the GPU driver did not come back (no answer %.0f s after the end); the GPU was left alone - restart SRS (or the PC) before the next GPU test", HANG_WAIT_S);
                pthread_mutex_unlock(&k->mu);
                abort_run(run, k->d.note);
            }
            break;
        }
    }
    for (i = 0; i < nw; i++) {
        if (!w[i].started) continue;
        if (w[i].abandoned) pthread_detach(w[i].t);   /* it may still come back; it then only touches w and run, which are kept */
        else pthread_join(w[i].t, NULL);
    }
    x->s.running = 0;
    x->s.elapsed = now_s() - run->t0;
    for (i = 0; i < nw; i++) {
        worker *k = &w[i];
        pthread_mutex_lock(&k->mu);   /* an abandoned worker may still write */
        x->s.dev[i] = k->d;
        x->s.dev[i].gflops_avg = k->tsum > 0 ? k->wsum / k->tsum / 1e9 : 0;
        pthread_mutex_unlock(&k->mu);
        x->s.dev[i].elapsed = x->s.elapsed;
        x->s.dev[i].iter_per_s = x->s.elapsed > 0 ? (double)x->s.dev[i].iterations / x->s.elapsed : 0;
        gpu_hw_close(k->hw);
        k->hw = NULL;
    }
    x->c = c;
    strftime(x->when, sizeof x->when, "%Y-%m-%d %H:%M:%S", localtime(&now));
    snprintf(x->abort_msg, sizeof x->abort_msg, "%s", run->abort_msg);
    g_stage = "";
    {
        int nostart = 0;
        for (i = 0; i < nw; i++) {
            gpuburn_dev *d = &x->s.dev[i];
            if (d->not_started) {   /* All GPUs: this one never ran; it is marked, and the verdict comes from the ones that did */
                nostart++;
                continue;
            }
            if (d->errors && d->state != GBS_ABORTED) d->state = GBS_FAILED;
            if (d->errors) rc = rc == -2 ? rc : 2;
            if (d->state == GBS_ABORTED && rc != 2) rc = -2;
        }
        if (nostart == nw) {   /* none of them could start: that is a failed start, with the first reason */
            rc = -2;
            if (!x->abort_msg[0]) snprintf(x->abort_msg, sizeof x->abort_msg, "no GPU could start (%.200s)", x->s.dev[0].note);
        } else if (nostart) {
            snprintf(nostartmsg, sizeof nostartmsg, "%d of %d GPUs could not start (marked in the table); the verdict is from the other%s",
                     nostart, nw, nw - nostart == 1 ? "" : "s");
        }
    }
    if (!rc && atomic_load(&run->user)) rc = 1;
    x->rc = rc;
    bench_save_render("gpuburn", gburn_render, x, o);
    {
        char params[400], res[500];
        long errs = 0, its = 0;
        double gf = 0;
        for (i = 0; i < nw; i++) { errs += x->s.dev[i].errors; its += x->s.dev[i].iterations; gf += x->s.dev[i].gflops_avg; }
        snprintf(params, sizeof params, "backend=%s device=%s%s precision=%s seconds=%.0f limit=%.0fC n=%d", gb_name[backend], x->s.dev[0].device,
                 nw > 1 ? " (+more)" : "", c.prec == GP_FP64 ? "FP64" : "FP32", c.seconds, c.limit_c, x->s.dev[0].n);
        snprintf(res, sizeof res, "%s %ld errors, %ld multiplications, %.0f GFLOPS%s%.200s", rc == 0 ? "PASS" : rc == 1 ? "STOPPED" : rc == 2 ? "FAIL" : "ENDED EARLY",
                 errs, its, gf, x->abort_msg[0] ? "; " : "", x->abort_msg);
        if (nostartmsg[0]) { size_t k = strlen(res); snprintf(res + k, sizeof res - k, "; %.150s", nostartmsg); }
        bench_history_add("gpuburn", params, res);
    }
    if (rc == -2 && why) snprintf(why, wn, "%s", x->abort_msg[0] ? x->abort_msg : "the run ended early");
    else if (rc >= 0 && nostartmsg[0] && why) snprintf(why, wn, "%s", nostartmsg);   /* the GUI and CLI show it next to the verdict */
    if (abandoned) {   /* a worker is still inside the driver: keep what it can touch and keep the GPU claimed */
        gpu_claim_hung();   /* the next GPU test then says what is wrong instead of "GPU Burnin is running" */
        free(x);
        return rc;
    }
    for (i = 0; i < nw; i++) pthread_mutex_destroy(&w[i].mu);
    pthread_mutex_destroy(&run->mu);
    free(w); free(run); free(x);
    gpu_release();
    return rc;
}

#else   /* not Linux */
const char *gpuburn_stage(void) { return ""; }
void gpuburn_cfg_default(gpuburn_cfg *c) { memset(c, 0, sizeof *c); }
int gpuburn_list(int b, gb_info *o, int m, char *why, size_t wn) { (void)b; (void)o; (void)m; if (why) snprintf(why, wn, "Linux only"); return -1; }
int bench_gpuburn(const gpuburn_cfg *c, srs_out *o, volatile int *cancel, char *why, size_t wn)
{
    (void)c; (void)o; (void)cancel;
    if (why) snprintf(why, wn, "Linux only");
    return -1;
}
gpu_hw *gpu_hw_open(const char *pci) { (void)pci; return NULL; }
void gpu_hw_read(gpu_hw *h, gpu_sensors *s) { (void)h; (void)s; }
void gpu_hw_close(gpu_hw *h) { (void)h; }
#endif
