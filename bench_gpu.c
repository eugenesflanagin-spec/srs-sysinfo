/* bench_gpu.c - GPU compute benchmark through Vulkan (see bench_gpu.h). Written for SRS.
 * Shaders: the .comp files in shaders/gpu, embedded as SPIR-V in bench_gpu_spv.inc (shaders/gpu/build.sh).
 * Each test grows its work until one submission takes about 25 ms of GPU time, then repeats
 * submissions until the chosen time is used up; the result is all the work divided by all the GPU
 * time (Vulkan timestamps, or the host clock when the queue has none). Every submission's output
 * is checked against a value worked out here on the host. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "bench.h"
#include "bench_gpu.h"
#include "burner.h"
#include "gpu_vk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *const gpu_test_id[GT_N] = { "fp32", "fp16", "fp64", "int32", "read", "write", "copy", "upload", "download", "latency" };
const char *const gpu_test_name[GT_N] = {
    "FP32 multiply-add", "FP16 multiply-add", "FP64 multiply-add", "INT32 multiply-add",
    "Video memory read", "Video memory write", "Video memory copy",
    "Upload (system to video memory)", "Download (video to system memory)", "Dispatch latency (round trip)"
};

void gpu_cfg_default(gpu_cfg *c)
{
    memset(c, 0, sizeof *c);
    c->device = -1;
    c->seconds = 1.0;
}

#ifdef __linux__
#include <sys/sysinfo.h>
#include "bench_gpu_spv.inc"

static const char *volatile g_stage = "";
const char *gpu_stage(void) { return g_stage; }

enum { R_NOTRUN, R_OK, R_WRONG, R_UNSUPPORTED, R_STOPPED, R_FAILED };
typedef struct { double v; int state; char note[256]; } gres;

#define LAT_MAX 20000
#define TARGET_S 0.025       /* GPU time per submission */
#define MEM_SEED 0x5EED1234u
#define LCG_A 1664525u       /* Numerical Recipes LCG */
#define LCG_C 1013904223u

typedef struct {
    gvk_dev g;
    gpu_cfg c;
    gres r[GT_N];
    double lat_med, lat_best, lat_p95;
    long lat_n;
    double mem_bytes;
    int shared_mem;           /* video memory is system memory (integrated / software) */
    int mem_capped;           /* the size the user asked for was lowered to fit video memory / RAM */
    int cancelled, failed, wrong;
    char fail[240];
    char when[32];
    volatile int *cancel;
    double *lat;
} gx;

static int stopped(gx *x) { return x->cancel && *x->cancel; }

/* ---------------------------------------------------------------- the timing loop */
typedef void (*rec_fn)(gx *x, void *u, uint32_t reps);
typedef int (*chk_fn)(gx *x, void *u, uint32_t reps);   /* 1 right, 0 wrong, -1 GPU failure */

/* grow reps until one submission takes ~25 ms (not counted), then count submissions until c.seconds
   of GPU time. rate = work per second. 0 ok, 1 stopped, 2 wrong result, -2 GPU failure */
static int timed(gx *x, rec_fn rec, chk_fn chk, void *u, double work_per_rep, uint32_t rep_max, double *rate)
{
    uint32_t reps = 1;
    double tot_s = 0, tot_w = 0, wall_end = gvk_now() + x->c.seconds * 4 + 5;
    int counted = 0;
    *rate = 0;
    for (;;) {
        double s = 0;
        int rc;
        if (stopped(x)) return 1;
        if (gvk_begin(&x->g)) { snprintf(x->fail, sizeof x->fail, "%s", x->g.err); return -2; }
        rec(x, u, reps);
        if ((rc = gvk_submit(&x->g, &s))) { snprintf(x->fail, sizeof x->fail, "%s", x->g.err); return -2; }
        if ((rc = chk(x, u, reps)) < 0) { if (!x->fail[0]) snprintf(x->fail, sizeof x->fail, "%s", x->g.err); return -2; }
        if (!rc) return 2;
        if (s <= 0) s = 1e-9;
        if (!counted && s < TARGET_S * 0.8 && reps < rep_max) {
            double f = TARGET_S / s;
            if (f > 64) f = 64;
            if (f < 2) f = 2;
            reps = (uint32_t)((double)reps * f) > rep_max ? rep_max : (uint32_t)((double)reps * f);
            if (gvk_now() < wall_end) continue;
        }
        counted = 1;
        tot_s += s;
        tot_w += work_per_rep * reps;
        if (s > TARGET_S * 2 && reps > 1) reps /= 2;
        if (tot_s >= x->c.seconds || gvk_now() > wall_end) break;
    }
    *rate = tot_w / tot_s;
    return 0;
}

/* ---------------------------------------------------------------- arithmetic */
typedef struct { vki_Pipeline p; vki_DescriptorSet s; gvk_buf *out; uint32_t groups, n, seed; int t; } fma_u;
typedef struct { float m1, c1, m2, c2; uint32_t iters, seed, n, pad; } pc_f;
typedef struct { uint32_t A, C, u2, u3, iters, seed, n, pad; } pc_i;

static void fma_rec(gx *x, void *v, uint32_t reps)
{
    fma_u *u = v;
    u->seed++;
    if (u->t == GT_INT32) {
        pc_i pc = { LCG_A, LCG_C, 0, 0, reps, u->seed, u->n, 0 };
        gvk_dispatch(&x->g, u->p, u->s, &pc, sizeof pc, u->groups);
    } else {
        pc_f pc = { 2.0f, 1.0f, 0.5f, -0.5f, reps, u->seed, u->n, 0 };
        gvk_dispatch(&x->g, u->p, u->s, &pc, sizeof pc, u->groups);
    }
}

static const uint32_t k_mul[16] = { 1, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53 };

/* x -> A^n x + C (A^n - 1) / (A - 1) mod 2^32, by squaring the affine map */
static void lcg_jump(uint32_t n, uint32_t *an, uint32_t *cn)
{
    uint32_t a = LCG_A, c = LCG_C, ra = 1, rc = 0;
    while (n) {
        if (n & 1) { rc = a * rc + c; ra = a * ra; }
        c = a * c + c;
        a = a * a;
        n >>= 1;
    }
    *an = ra;
    *cn = rc;
}

static int fma_chk(gx *x, void *v, uint32_t reps)
{
    fma_u *u = v;
    uint32_t g, k, an = 0, cn = 0;
    (void)x;
    if (u->t == GT_INT32) lcg_jump(reps, &an, &cn);
    for (g = 0; g < u->n; g++) {
        if (u->t == GT_INT32) {
            uint32_t e = 0, a;
            for (k = 0; k < 8; k++) {
                a = k ? g * k_mul[k] + k : g ^ u->seed;
                e ^= an * a + cn;
            }
            if (((const uint32_t *)u->out->map)[g] != e) return 0;
        } else {
            uint32_t sum = 0, nv = u->t == GT_FP16 ? 16 : 8, mask = u->t == GT_FP16 ? 63 : 255;
            for (k = 0; k < nv; k++) sum += (k ? g * k_mul[k] + k : g + u->seed) & mask;
            if (((const float *)u->out->map)[g] != (float)sum) return 0;
        }
    }
    return 1;
}

static void run_arith(gx *x, int t)
{
    static const struct { const uint32_t *spv; size_t n; double ops; } sh[4] = {
        { spv_fma32, sizeof spv_fma32, 32 }, { spv_fma16, sizeof spv_fma16, 64 },
        { spv_fma64, sizeof spv_fma64, 32 }, { spv_imad32, sizeof spv_imad32, 16 } };
    gres *r = &x->r[t];
    fma_u u;
    gvk_buf out, *bp = &out;
    double rate = 0;
    int rc;
    if (t == GT_FP16 && !x->g.fp16) { r->state = R_UNSUPPORTED; snprintf(r->note, sizeof r->note, "this device has no FP16 shader arithmetic"); return; }
    if (t == GT_FP64 && !x->g.fp64) { r->state = R_UNSUPPORTED; snprintf(r->note, sizeof r->note, "this device has no FP64 shader arithmetic"); return; }
    memset(&u, 0, sizeof u);
    u.t = t;
    u.groups = x->g.info.dtype == GPU_CPU ? 16 : 4096;
    u.n = u.groups * 64;
    if (gvk_buf_new(&x->g, &out, (size_t)u.n * 4, GVK_READBACK)) { r->state = R_FAILED; snprintf(r->note, sizeof r->note, "%s", x->g.err); return; }
    u.out = &out;
    if (!(u.p = gvk_pipeline(&x->g, sh[t].spv, sh[t].n)) || !(u.s = gvk_set(&x->g, &bp, 1))) {
        r->state = R_FAILED;
        snprintf(r->note, sizeof r->note, "%s", x->g.err);
    } else {
        rc = timed(x, fma_rec, fma_chk, &u, sh[t].ops * u.n, 1u << 22, &rate);
        if (rc == 0) { r->state = R_OK; r->v = rate / 1e9; }
        else if (rc == 1) r->state = R_STOPPED;
        else if (rc == 2) { r->state = R_WRONG; snprintf(r->note, sizeof r->note, "the GPU returned a wrong result"); }
        else { r->state = R_FAILED; snprintf(r->note, sizeof r->note, "%s", x->fail); }
    }
    gvk_pipeline_free(&x->g, u.p);
    gvk_buf_free(&x->g, &out);
}

/* ---------------------------------------------------------------- memory and transfers */
typedef struct {
    gvk_buf a, b, up, down, part;
    vki_Pipeline pw, pr, pc;
    vki_DescriptorSet sw, sr, srb, scp;
    uint32_t n4, groups;
    int t;
} mem_u;
typedef struct { uint32_t n4, seed, u2, u3, u4, u5, u6, u7; } pc_m;

static uint32_t pattern_sum(uint32_t nwords)
{
    uint64_t n = nwords, s = n * (n - 1) / 2;
    return (uint32_t)s * 2654435761u + nwords * MEM_SEED;
}

static void mem_dispatch(gx *x, mem_u *u, vki_Pipeline p, vki_DescriptorSet s)
{
    pc_m pc = { u->n4, MEM_SEED, 0, 0, 0, 0, 0, 0 };
    gvk_dispatch(&x->g, p, s, &pc, sizeof pc, u->groups);
}

static void mem_rec(gx *x, void *v, uint32_t reps)
{
    mem_u *u = v;
    uint32_t i;
    for (i = 0; i < reps; i++) {
        switch (u->t) {
        case GT_WRITE: mem_dispatch(x, u, u->pw, u->sw); break;
        case GT_READ: mem_dispatch(x, u, u->pr, u->sr); break;
        case GT_COPY: mem_dispatch(x, u, u->pc, u->scp); break;
        case GT_UPLOAD: gvk_copy(&x->g, &u->up, &u->a, u->a.size); break;
        case GT_DOWNLOAD: gvk_copy(&x->g, &u->a, &u->down, u->a.size); break;
        }
        if (i + 1 < reps) gvk_barrier(&x->g);
    }
}

/* 1 = the data is right; write, copy and upload are read back by an extra (untimed) submission */
static int mem_chk(gx *x, void *v, uint32_t reps)
{
    mem_u *u = v;
    uint32_t i, s = 0, n = u->n4 * 4;
    (void)reps;
    if (u->t == GT_DOWNLOAD) {
        const uint32_t *w = u->down.map;
        for (i = 0; i < n; i++) s += w[i];
        return s == pattern_sum(n);
    }
    if (u->t != GT_READ) {
        double sec;
        if (gvk_begin(&x->g)) return -1;
        mem_dispatch(x, u, u->pr, u->t == GT_COPY ? u->srb : u->sr);
        if (gvk_submit(&x->g, &sec)) { snprintf(x->fail, sizeof x->fail, "%s", x->g.err); return -1; }
    }
    for (i = 0; i < u->groups * 64; i++) s += ((const uint32_t *)u->part.map)[i];
    return s == pattern_sum(n);
}

static void run_memory(gx *x, unsigned want)
{
    mem_u u;
    gvk_buf *b1[2], *b2[2], *b3[2], *b4[2];
    size_t bytes;
    int t;
    double mib = x->c.mem_mib;
    memset(&u, 0, sizeof u);
    if (mib <= 0) {
        mib = x->g.info.dtype == GPU_CPU ? 8 : 256;
        if (x->g.info.vram_bytes > 0 && mib * 1048576.0 * 4 > x->g.info.vram_bytes) mib = x->g.info.vram_bytes / 4 / 1048576.0;
    }
    else {   /* the user's size: at most a quarter of the video memory and of the PC's memory (2 buffers on the GPU, 2 in RAM) */
        struct sysinfo si;
        double ram = sysinfo(&si) == 0 ? (double)si.totalram * (double)si.mem_unit : 0;
        if (x->g.info.vram_bytes > 0 && mib * 1048576.0 * 4 > x->g.info.vram_bytes) { mib = x->g.info.vram_bytes / 4 / 1048576.0; x->mem_capped = 1; }
        if (ram > 0 && mib * 1048576.0 * 4 > ram) { mib = ram / 4 / 1048576.0; x->mem_capped = 1; }
    }
    if (mib < 1) mib = 1;
    bytes = (size_t)(mib * 1048576.0) / 16384 * 16384;
    if (bytes > x->g.max_storage) bytes = (size_t)x->g.max_storage / 16384 * 16384;
    x->mem_bytes = (double)bytes;
    u.n4 = (uint32_t)(bytes / 16);
    u.groups = x->g.info.dtype == GPU_CPU ? 16 : 8192;
    if (u.groups > u.n4 / 64) u.groups = u.n4 / 64;
    if (u.groups > x->g.max_groups) u.groups = x->g.max_groups;
    if (gvk_buf_new(&x->g, &u.a, bytes, GVK_DEVICE) || gvk_buf_new(&x->g, &u.b, bytes, GVK_DEVICE) ||
        gvk_buf_new(&x->g, &u.part, (size_t)u.groups * 64 * 4, GVK_READBACK) ||
        ((want & (1u << GT_UPLOAD)) && gvk_buf_new(&x->g, &u.up, bytes, GVK_UPLOAD)) ||
        ((want & (1u << GT_DOWNLOAD)) && gvk_buf_new(&x->g, &u.down, bytes, GVK_READBACK))) {
        for (t = GT_READ; t <= GT_DOWNLOAD; t++)
            if (want & (1u << t)) { x->r[t].state = R_FAILED; snprintf(x->r[t].note, sizeof x->r[t].note, "%s", x->g.err); }
        goto out;
    }
    x->shared_mem = !u.a.device_local || (u.up.buf && u.up.device_local) || x->g.info.dtype == GPU_CPU || x->g.info.dtype == GPU_INTEGRATED;
    b1[0] = &u.a; b2[0] = &u.a; b2[1] = &u.part; b3[0] = &u.b; b3[1] = &u.part; b4[0] = &u.a; b4[1] = &u.b;
    if (!(u.pw = gvk_pipeline(&x->g, spv_mem_write, sizeof spv_mem_write)) || !(u.pr = gvk_pipeline(&x->g, spv_mem_read, sizeof spv_mem_read)) ||
        !(u.pc = gvk_pipeline(&x->g, spv_mem_copy, sizeof spv_mem_copy)) || !(u.sw = gvk_set(&x->g, b1, 1)) ||
        !(u.sr = gvk_set(&x->g, b2, 2)) || !(u.srb = gvk_set(&x->g, b3, 2)) || !(u.scp = gvk_set(&x->g, b4, 2))) {
        for (t = GT_READ; t <= GT_DOWNLOAD; t++)
            if (want & (1u << t)) { x->r[t].state = R_FAILED; snprintf(x->r[t].note, sizeof x->r[t].note, "%s", x->g.err); }
        goto out;
    }
    if (u.up.map) {   /* the upload source holds the same pattern the write shader stores */
        uint32_t *w = u.up.map, i;
        for (i = 0; i < u.n4 * 4; i++) w[i] = i * 2654435761u + MEM_SEED;
    }
    /* the pattern in A for the read / copy / download tests, whatever is selected */
    if (gvk_begin(&x->g)) { x->failed = 1; snprintf(x->fail, sizeof x->fail, "%s", x->g.err); goto out; }   /* not a wrong result: the GPU failed */
    {
        double s;
        mem_dispatch(x, &u, u.pw, u.sw);
        if (gvk_submit(&x->g, &s)) { x->failed = 1; snprintf(x->fail, sizeof x->fail, "%s", x->g.err); goto out; }
    }
    {
        static const int order[5] = { GT_READ, GT_WRITE, GT_COPY, GT_UPLOAD, GT_DOWNLOAD };
        int k;
        for (k = 0; k < 5 && !x->failed; k++) {
            double rate = 0;
            int rc;
            t = order[k];
            if (!(want & (1u << t))) continue;
            if (stopped(x)) { x->r[t].state = R_STOPPED; continue; }
            g_stage = gpu_test_name[t];
            u.t = t;
            rc = timed(x, mem_rec, mem_chk, &u, t == GT_COPY ? 2.0 * (double)bytes : (double)bytes, 256, &rate);
            if (rc == 0) { x->r[t].state = R_OK; x->r[t].v = rate / 1e9; }
            else if (rc == 1) x->r[t].state = R_STOPPED;
            else if (rc == 2) { x->r[t].state = R_WRONG; snprintf(x->r[t].note, sizeof x->r[t].note, "the data read back was not what was written"); }
            else { x->r[t].state = R_FAILED; x->failed = 1; snprintf(x->r[t].note, sizeof x->r[t].note, "%s", x->fail); }
        }
    }
out:
    gvk_pipeline_free(&x->g, u.pw);
    gvk_pipeline_free(&x->g, u.pr);
    gvk_pipeline_free(&x->g, u.pc);
    gvk_buf_free(&x->g, &u.a); gvk_buf_free(&x->g, &u.b); gvk_buf_free(&x->g, &u.part);
    gvk_buf_free(&x->g, &u.up); gvk_buf_free(&x->g, &u.down);
}

/* ---------------------------------------------------------------- dispatch latency */
static int cmp_d(const void *a, const void *b) { double p = *(const double *)a, q = *(const double *)b; return p < q ? -1 : p > q; }

static void run_latency(gx *x)
{
    gres *r = &x->r[GT_LATENCY];
    gvk_buf cnt, *bp = &cnt;
    vki_Pipeline p = 0;
    vki_DescriptorSet s;
    double t_end;
    long n = 0, i;
    if (gvk_buf_new(&x->g, &cnt, 64, GVK_READBACK)) { r->state = R_FAILED; snprintf(r->note, sizeof r->note, "%s", x->g.err); return; }
    memset(cnt.map, 0, 64);
    if (!(p = gvk_pipeline(&x->g, spv_tick, sizeof spv_tick)) || !(s = gvk_set(&x->g, &bp, 1))) {
        r->state = R_FAILED;
        snprintf(r->note, sizeof r->note, "%s", x->g.err);
        goto out;
    }
    for (i = 0; i < 20 && !x->failed; i++) {   /* warm-up: first submissions pay for lazy driver set-up */
        double sec;
        if (gvk_begin(&x->g)) break;
        gvk_dispatch(&x->g, p, s, NULL, 0, 1);
        if (gvk_submit(&x->g, &sec)) { x->failed = 1; snprintf(x->fail, sizeof x->fail, "%s", x->g.err); }
    }
    t_end = gvk_now() + x->c.seconds;
    while (!x->failed && n < LAT_MAX && gvk_now() < t_end) {
        double sec, t0;
        if (stopped(x)) { r->state = R_STOPPED; goto out; }
        if (gvk_begin(&x->g)) break;
        gvk_dispatch(&x->g, p, s, NULL, 0, 1);
        t0 = gvk_now();
        if (gvk_submit(&x->g, &sec)) { x->failed = 1; snprintf(x->fail, sizeof x->fail, "%s", x->g.err); break; }
        x->lat[n++] = (gvk_now() - t0) * 1e6;
    }
    if (x->failed) { r->state = R_FAILED; snprintf(r->note, sizeof r->note, "%s", x->fail); goto out; }
    if (!n) { r->state = R_FAILED; snprintf(r->note, sizeof r->note, "no round trip finished"); goto out; }
    if (((const uint32_t *)cnt.map)[0] != (uint32_t)(n + i)) {
        r->state = R_WRONG;
        snprintf(r->note, sizeof r->note, "only %u of %ld dispatches ran", ((const uint32_t *)cnt.map)[0], n + i);
        goto out;
    }
    qsort(x->lat, (size_t)n, sizeof *x->lat, cmp_d);
    x->lat_n = n;
    x->lat_best = x->lat[0];
    x->lat_med = x->lat[n / 2];
    x->lat_p95 = x->lat[(long)((double)n * 0.95) < n ? (long)((double)n * 0.95) : n - 1];
    r->v = x->lat_med;
    r->state = R_OK;
out:
    gvk_pipeline_free(&x->g, p);
    gvk_buf_free(&x->g, &cnt);
}

/* ---------------------------------------------------------------- result */
static void fmt_res(const gx *x, int t, char *b, size_t n, int *col)
{
    const gres *r = &x->r[t];
    *col = C_GREEN;
    switch (r->state) {
    case R_OK:
        if (t == GT_LATENCY) snprintf(b, n, "%.1f µs", r->v);
        else if (t >= GT_READ) snprintf(b, n, r->v >= 100 ? "%.0f GB/s" : "%.2f GB/s", r->v);
        else snprintf(b, n, r->v >= 100 ? "%.0f %s" : "%.2f %s", r->v, t == GT_INT32 ? "GIOPS" : "GFLOPS");
        return;
    case R_WRONG: *col = C_RED; snprintf(b, n, "WRONG RESULT"); return;
    case R_UNSUPPORTED: *col = C_GRAY; snprintf(b, n, "not supported"); return;
    case R_STOPPED: *col = C_ORANGE; snprintf(b, n, "stopped"); return;
    case R_FAILED: *col = C_RED; snprintf(b, n, "failed"); return;
    default: *col = C_GRAY; snprintf(b, n, "-"); return;
    }
}

static void gpu_render(srs_out *o, void *c)
{
    static const int tabs[] = { 300, 460 }, tabs2[] = { 210 };
    gx *x = c;
    const gpu_dev *d = &x->g.info;
    int t;
    so_siv_header(o, "GPU compute benchmark (Vulkan)");
    so_tabs(o, tabs, 2);
    so_row(o, 3, (const char *const[]){ "Test", "Result", "" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    for (t = 0; t < GT_N; t++) {
        char v[48], note[160];
        int col;
        if (x->r[t].state == R_NOTRUN) continue;
        fmt_res(x, t, v, sizeof v, &col);
        note[0] = 0;
        if (t == GT_LATENCY && x->r[t].state == R_OK)
            snprintf(note, sizeof note, "median of %ld; best %.1f µs, 95%% under %.1f µs", x->lat_n, x->lat_best, x->lat_p95);
        else if (x->r[t].note[0]) snprintf(note, sizeof note, "%s", x->r[t].note);
        so_row(o, 3, (const char *const[]){ gpu_test_name[t], v, note }, (const int[]){ C_TEAL, col, C_GRAY });
    }
    so_par(o);
    so_tabs(o, tabs2, 1);
    if (x->cancelled) so_kv(o, "Status", C_ORANGE, "Stopped before the end - the rows marked stopped were not measured");
    if (x->failed) so_kvf(o, "Error", C_RED, "%s", x->fail);
    if (x->wrong) so_kv(o, "Check", C_RED, "A result was wrong: the GPU, its driver or its memory did not compute what was asked");
    so_kvf(o, "Device", C_VALUE, "%s (%s)", d->name, d->type ? d->type : "unknown");
    so_kv(o, "Driver", C_VALUE, d->driver[0] ? d->driver : "not reported");
    so_kvf(o, "Vulkan", C_VALUE, "%s", d->api);
    if (d->vram_bytes > 0) so_kvf(o, "Device memory", C_VALUE, "%.1f GB%s", d->vram_bytes / 1e9, x->shared_mem ? " (shared with the system)" : "");
    if (d->cus) so_kvf(o, "Shader cores", C_VALUE, "%d %s, subgroup size %d", d->cus, d->cu_word, d->subgroup);
    else if (d->subgroup) so_kvf(o, "Subgroup size", C_VALUE, "%d", d->subgroup);
    if (x->mem_bytes > 0) so_kvf(o, "Memory tests", C_VALUE, "%.0f MiB buffers%s", x->mem_bytes / 1048576.0,
                                 x->mem_capped ? " (lowered from the size asked for, to a quarter of the video memory or of the PC's memory)" : "");
    so_kvf(o, "Settings", C_VALUE, "%.1f s of GPU time per test, timed with %s", x->c.seconds,
           x->g.timestamps ? "GPU timestamps" : "the host clock (this queue has no timestamps)");
    so_kv(o, "Run", C_GRAY, x->when);
    so_kv(o, "Build", C_GRAY, bench_build_tag());
    so_note(o, "1 GB = 1,000,000,000 bytes. GFLOPS = billions of floating-point operations per second (a fused multiply-add counts as 2); "
               "GIOPS the same for 32-bit integer operations. Copy counts the bytes read plus the bytes written.");
    if (x->shared_mem) so_note(o, "This device uses system memory: the memory and transfer rows measure system RAM, not separate video memory.");
    so_note(o, "Compare results only between alike SRS builds and the same GPU driver version: a driver update alone can change them.");
    so_board_footer(o);
}

int bench_gpu(const gpu_cfg *cfg, srs_out *o, volatile int *cancel, char *why, size_t wn)
{
    gx *x;
    const char *who = NULL;
    unsigned want;
    int t, rc;
    time_t now = time(NULL);
    char params[600], res[700];
    size_t rl;
    if (why && wn) why[0] = 0;
    if (burn_active()) { if (why) snprintf(why, wn, "CPU Burnin / Stress is running; start the GPU benchmark when it has finished"); return -1; }
    if (!gpu_claim("GPU benchmark", &who)) { if (why) snprintf(why, wn, "%s is running; wait for it to finish", who ? who : "another GPU test"); return -1; }
    if (burn_active()) { gpu_release(); if (why) snprintf(why, wn, "CPU Burnin / Stress is running; start the GPU test when it has finished"); return -1; }   /* it started at the same moment */
    if (!(x = calloc(1, sizeof *x)) || !(x->lat = malloc(LAT_MAX * sizeof *x->lat))) {
        if (x) free(x);
        gpu_release();
        if (why) snprintf(why, wn, "out of memory");
        return -1;
    }
    x->c = *cfg;
    if (x->c.seconds < 0.05) x->c.seconds = 0.05;
    if (x->c.seconds > 10) x->c.seconds = 10;
    x->cancel = cancel;
    want = cfg->tests ? cfg->tests : (1u << GT_N) - 1;
    g_stage = "Opening the GPU";
    if (gvk_open(&x->g, cfg->device, cfg->software_only, why, wn)) {
        g_stage = "";
        free(x->lat);
        free(x);
        gpu_release();
        return -1;
    }
    for (t = GT_FP32; t <= GT_INT32 && !x->failed; t++) {
        if (!(want & (1u << t))) continue;
        if (stopped(x)) { x->r[t].state = R_STOPPED; continue; }
        g_stage = gpu_test_name[t];
        run_arith(x, t);
        if (x->r[t].state == R_FAILED && (x->g.lost || x->g.abandoned)) { x->failed = 1; if (!x->fail[0]) snprintf(x->fail, sizeof x->fail, "%s", x->g.err); }
    }
    if (!x->failed && (want & ((1u << GT_READ) | (1u << GT_WRITE) | (1u << GT_COPY) | (1u << GT_UPLOAD) | (1u << GT_DOWNLOAD))))
        run_memory(x, want);
    if (!x->failed && (want & (1u << GT_LATENCY))) {
        if (stopped(x)) x->r[GT_LATENCY].state = R_STOPPED;
        else { g_stage = gpu_test_name[GT_LATENCY]; run_latency(x); }
    }
    for (t = 0; t < GT_N; t++) {
        if (x->r[t].state == R_STOPPED) x->cancelled = 1;
        if (x->r[t].state == R_WRONG) x->wrong = 1;
        if (x->failed && (want & (1u << t)) && x->r[t].state == R_NOTRUN) x->r[t].state = R_FAILED;
    }
    if (stopped(x)) x->cancelled = 1;
    g_stage = "";
    strftime(x->when, sizeof x->when, "%Y-%m-%d %H:%M:%S", localtime(&now));
    bench_save_render("gpu", gpu_render, x, o);
    snprintf(params, sizeof params, "device=%s driver=%s vulkan=%s seconds=%.2f mem=%.0fMiB timing=%s", x->g.info.name,
             x->g.info.driver[0] ? x->g.info.driver : "-", x->g.info.api, x->c.seconds, x->mem_bytes / 1048576.0, x->g.timestamps ? "gpu" : "host");
    res[0] = 0;
    for (t = 0; t < GT_N; t++) {
        char v[48];
        int col;
        if (x->r[t].state == R_NOTRUN) continue;
        fmt_res(x, t, v, sizeof v, &col);
        rl = strlen(res);
        snprintf(res + rl, sizeof res - rl, "%s%s %s", rl ? ", " : "", gpu_test_id[t], v);
    }
    rl = strlen(res);
    snprintf(res + rl, sizeof res - rl, "%s", x->failed ? " FAILED" : x->cancelled ? " STOPPED" : x->wrong ? " WRONG" : "");
    bench_history_add("gpu", params, res);
    rc = x->failed ? -2 : x->cancelled ? 1 : x->wrong ? 2 : 0;
    if (x->failed && why) snprintf(why, wn, "%s", x->fail);
    gvk_close(&x->g);
    free(x->lat);
    free(x);
    gpu_release();
    return rc;
}

#else   /* not Linux */
const char *gpu_stage(void) { return ""; }
int bench_gpu(const gpu_cfg *cfg, srs_out *o, volatile int *cancel, char *why, size_t wn)
{
    (void)cfg; (void)o; (void)cancel;
    if (why) snprintf(why, wn, "Linux only");
    return -1;
}
#endif
