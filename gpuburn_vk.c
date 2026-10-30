/* gpuburn_vk.c - Vulkan back end of GPU Burnin and the GPU Compute comparison (see gpu_backend.h).
 * Own kernels only (the .comp files in shaders/gpu, SPIR-V in bench_gpu_spv.inc). Uses the shared Vulkan layer gpu_vk.c.
 * Errors are counted on the GPU: every GEMM result is compared with the first one by cmp.comp. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "gpu_backend.h"
#include "gpu_vk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include "bench_gpu_spv.inc"

#define MEM_SEED 0x5EED1234u
#define MAXLAT 20000

struct gb_ctx {
    gvk_dev g;
    int kind, prec, ready;
    size_t n, es, bytes;
    gvk_buf a, b, c, ref, cnt, cntrb, sa, sb, rb;
    vki_Pipeline p1, p2, p3;
    vki_DescriptorSet s1, s2, s3, s4;
    uint32_t groups, seed, words;
    long prep_wrong, ticks;
    int verified;
    char err[240];
};

static int vk_available(char *hint, size_t hn) { return gvk_load(hint, hn); }

static int vk_list(gb_info *out, int max, char *why, size_t wn)
{
    gpu_dev d[GPU_MAXDEV];
    int n = gpu_list(d, max < GPU_MAXDEV ? max : GPU_MAXDEV, why, wn), i;
    for (i = 0; i < n; i++) {
        gb_info *o = &out[i];
        memset(o, 0, sizeof *o);
        o->backend = GB_VULKAN;
        o->index = i;
        snprintf(o->label, sizeof o->label, "Vulkan");
        snprintf(o->device, sizeof o->device, "%.250s", d[i].name);
        snprintf(o->runtime, sizeof o->runtime, "Vulkan %s, driver %.120s", d[i].api, d[i].driver[0] ? d[i].driver : "not reported");
        snprintf(o->pci, sizeof o->pci, "%s", d[i].pci);
        snprintf(o->kind, sizeof o->kind, "%s", d[i].dtype == GPU_CPU ? "software renderer" : "GPU");
        o->mem_bytes = d[i].vram_bytes;
        o->is_cpu = d[i].dtype == GPU_CPU;
        o->fp16 = d[i].fp16;
        o->fp64 = d[i].fp64;
    }
    return n;
}

static int vk_open(gb_ctx **cp, const gb_info *d, int software_only, char *why, size_t wn)
{
    gb_ctx *c = calloc(1, sizeof *c);
    if (!c) { snprintf(why, wn, "out of memory"); return -1; }
    if (gvk_open(&c->g, d->index, software_only, why, wn)) { free(c); return -1; }
    *cp = c;
    return 0;
}

static void vk_release(gb_ctx *c)
{
    gvk_dev *g = &c->g;
    if (!c->ready) return;
    gvk_pipeline_free(g, c->p1); gvk_pipeline_free(g, c->p2); gvk_pipeline_free(g, c->p3);
    gvk_buf_free(g, &c->a); gvk_buf_free(g, &c->b); gvk_buf_free(g, &c->c); gvk_buf_free(g, &c->ref);
    gvk_buf_free(g, &c->cnt); gvk_buf_free(g, &c->cntrb); gvk_buf_free(g, &c->sa); gvk_buf_free(g, &c->sb); gvk_buf_free(g, &c->rb);
    c->p1 = c->p2 = c->p3 = 0;
    c->ready = 0;
}

static void vk_close(gb_ctx *c)
{
    if (!c) return;
    vk_release(c);
    gvk_close(&c->g);
    if (!c->g.abandoned) free(c);   /* an abandoned device may still be in use by the GPU: its memory stays */
}

static const char *vk_error(gb_ctx *c) { return c->err[0] ? c->err : c->g.err; }

static int map_rc(gb_ctx *c, int rc)
{
    if (!rc) return GBP_OK;
    snprintf(c->err, sizeof c->err, "%s", c->g.err);
    return rc == -2 ? GBP_HUNG : rc == -3 ? GBP_LOST : GBP_ERROR;
}

static uint32_t clamp_groups(gb_ctx *c, uint32_t want)
{
    if (want > c->g.max_groups) want = c->g.max_groups;
    return want ? want : 1;
}

/* ---- helpers shared by the kinds ---- */
static int new_buf(gb_ctx *c, gvk_buf *b, size_t bytes, int home)
{
    if (gvk_buf_new(&c->g, b, bytes, home)) { snprintf(c->err, sizeof c->err, "%s", c->g.err); return GBP_ERROR; }
    return GBP_OK;
}

static int new_pipe(gb_ctx *c, vki_Pipeline *p, const uint32_t *spv, size_t bytes)
{
    if (!(*p = gvk_pipeline(&c->g, spv, bytes))) { snprintf(c->err, sizeof c->err, "%s", c->g.err); return GBP_ERROR; }
    return GBP_OK;
}

static int new_set(gb_ctx *c, vki_DescriptorSet *s, gvk_buf *const *b, int n)
{
    if (!(*s = gvk_set(&c->g, b, n))) { snprintf(c->err, sizeof c->err, "%s", c->g.err); return GBP_ERROR; }
    return GBP_OK;
}

static int shot(gb_ctx *c, double *secs)
{
    return map_rc(c, gvk_submit(&c->g, secs));
}

static int begin(gb_ctx *c)
{
    if (gvk_begin(&c->g)) return map_rc(c, -1);
    return GBP_OK;
}

static void fill_mat(void *m, size_t n, size_t es, int which)
{
    size_t i, k;
    for (i = 0; i < n; i++)
        for (k = 0; k < n; k++) {
            int v = ((i * 5 + k * 3 + (size_t)which) % 7) < 3;
            if (es == 8) ((double *)m)[i * n + k] = v; else ((float *)m)[i * n + k] = (float)v;
        }
}

static double mget(const void *m, size_t idx, size_t es) { return es == 8 ? ((const double *)m)[idx] : (double)((const float *)m)[idx]; }

static const uint32_t k_mul[16] = { 1, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53 };

/* ---------------------------------------------------------------- prep */
static int prep_gemm(gb_ctx *c, int prec, size_t n, double *work, unsigned *maxreps)
{
    static const int tests = 64;
    size_t es = prec == GP_FP64 ? 8 : 4, bytes;
    gvk_buf *abc[3], *cr[3], *ab_ref[3];
    const uint32_t *spv;
    size_t sb;
    double s;
    int rc, t;
    uint32_t pc[8];
    if (prec == GP_FP16 && !c->g.fp16) { snprintf(c->err, sizeof c->err, "this device has no FP16 shader arithmetic"); return GBP_UNSUPPORTED; }
    if (prec == GP_FP64 && !c->g.fp64) { snprintf(c->err, sizeof c->err, "this device has no FP64 shader arithmetic"); return GBP_UNSUPPORTED; }
    if (prec == GP_INT32) return GBP_UNSUPPORTED;
    n = n / 16 * 16;
    if (n < 16) n = 16;
    if (prec == GP_FP16 && n > 2048) n = 2048;   /* whole numbers up to 2048 are exact in half precision */
    bytes = n * n * es;
    if (bytes > c->g.max_storage) return GBP_UNSUPPORTED;
    c->n = n; c->es = es; c->bytes = bytes; c->words = (uint32_t)(bytes / 4);
    spv = prec == GP_FP64 ? spv_gemm64 : prec == GP_FP16 ? spv_gemm16 : spv_gemm32;
    sb = prec == GP_FP64 ? sizeof spv_gemm64 : prec == GP_FP16 ? sizeof spv_gemm16 : sizeof spv_gemm32;
    c->ready = 1;
    if ((rc = new_buf(c, &c->sa, bytes, GVK_UPLOAD)) || (rc = new_buf(c, &c->sb, bytes, GVK_UPLOAD)) ||
        (rc = new_buf(c, &c->a, bytes, GVK_DEVICE)) || (rc = new_buf(c, &c->b, bytes, GVK_DEVICE)) ||
        (rc = new_buf(c, &c->c, bytes, GVK_DEVICE)) || (rc = new_buf(c, &c->ref, bytes, GVK_DEVICE)) ||
        (rc = new_buf(c, &c->cnt, 64, GVK_DEVICE)) || (rc = new_buf(c, &c->cntrb, 64, GVK_READBACK)) ||
        (rc = new_buf(c, &c->rb, bytes, GVK_READBACK)) ||
        (rc = new_pipe(c, &c->p1, spv, sb)) || (rc = new_pipe(c, &c->p2, spv_cmp, sizeof spv_cmp))) return rc;
    abc[0] = &c->a; abc[1] = &c->b; abc[2] = &c->c;
    cr[0] = &c->c; cr[1] = &c->ref; cr[2] = &c->cnt;
    ab_ref[0] = &c->a; ab_ref[1] = &c->b; ab_ref[2] = &c->ref;
    if ((rc = new_set(c, &c->s1, abc, 3)) || (rc = new_set(c, &c->s2, cr, 3)) || (rc = new_set(c, &c->s3, ab_ref, 3))) return rc;
    fill_mat(c->sa.map, n, es, 0);
    fill_mat(c->sb.map, n, es, 1);
    pc[0] = (uint32_t)n;
    if ((rc = begin(c))) return rc;
    gvk_copy(&c->g, &c->sa, &c->a, bytes);
    gvk_copy(&c->g, &c->sb, &c->b, bytes);
    gvk_barrier(&c->g);
    gvk_dispatch2(&c->g, c->p1, c->s3, pc, sizeof pc, (uint32_t)(n / 16), (uint32_t)(n / 16));
    gvk_barrier(&c->g);
    gvk_copy(&c->g, &c->ref, &c->rb, bytes);
    if ((rc = shot(c, &s))) return rc;
    /* the first result is the reference: check a spread of its entries against the host's own sums */
    for (t = 0; t < tests; t++) {
        size_t r = (size_t)t * 2654435761u % n, q = (size_t)t * 40503u % n, k;
        double e = 0;
        for (k = 0; k < n; k++) e += mget(c->sa.map, r * n + k, es) * mget(c->sb.map, k * n + q, es);
        if (mget(c->rb.map, r * n + q, es) != e) c->prep_wrong++;
    }
    *work = 2.0 * (double)n * (double)n * (double)n;
    *maxreps = 4096;
    return GBP_OK;
}

static int prep_fma(gb_ctx *c, int prec, size_t n, double *work, unsigned *maxreps)
{
    const uint32_t *spv;
    size_t sb;
    gvk_buf *o[1];
    int rc;
    double ops;
    if (prec == GP_INT32) { spv = spv_imad32; sb = sizeof spv_imad32; ops = 16; }
    else if (prec == GP_FP16) { spv = spv_fma16; sb = sizeof spv_fma16; ops = 64; }
    else if (prec == GP_FP64) { spv = spv_fma64; sb = sizeof spv_fma64; ops = 32; }
    else { spv = spv_fma32; sb = sizeof spv_fma32; ops = 32; }
    if (prec == GP_FP16 && !c->g.fp16) return GBP_UNSUPPORTED;
    if (prec == GP_FP64 && !c->g.fp64) return GBP_UNSUPPORTED;
    c->ready = 1;
    c->groups = clamp_groups(c, (uint32_t)n);
    c->words = c->groups * 64;
    if ((rc = new_buf(c, &c->rb, (size_t)c->words * 4, GVK_READBACK)) || (rc = new_pipe(c, &c->p1, spv, sb))) return rc;
    o[0] = &c->rb;
    if ((rc = new_set(c, &c->s1, o, 1))) return rc;
    *work = ops * c->words;
    *maxreps = 1u << 22;
    return GBP_OK;
}

static int prep_copy(gb_ctx *c, size_t bytes, double *work, unsigned *maxreps)
{
    gvk_buf *w[1], *cp[2], *cm[3];
    uint32_t pc[8] = { 0 };
    double s;
    int rc;
    bytes = bytes / 16384 * 16384;
    if (bytes < 16384) bytes = 16384;
    if (bytes > c->g.max_storage) bytes = (size_t)c->g.max_storage / 16384 * 16384;
    c->bytes = bytes;
    c->words = (uint32_t)(bytes / 4);
    c->groups = clamp_groups(c, (uint32_t)(bytes / 16 / 64 < 8192 ? bytes / 16 / 64 : 8192));
    if (c->g.info.dtype == GPU_CPU && c->groups > 16) c->groups = 16;
    c->ready = 1;
    if ((rc = new_buf(c, &c->a, bytes, GVK_DEVICE)) || (rc = new_buf(c, &c->b, bytes, GVK_DEVICE)) ||
        (rc = new_buf(c, &c->cnt, 64, GVK_DEVICE)) || (rc = new_buf(c, &c->cntrb, 64, GVK_READBACK)) ||
        (rc = new_pipe(c, &c->p1, spv_mem_write, sizeof spv_mem_write)) || (rc = new_pipe(c, &c->p2, spv_mem_copy, sizeof spv_mem_copy)) ||
        (rc = new_pipe(c, &c->p3, spv_cmp, sizeof spv_cmp))) return rc;
    w[0] = &c->a; cp[0] = &c->a; cp[1] = &c->b; cm[0] = &c->a; cm[1] = &c->b; cm[2] = &c->cnt;
    if ((rc = new_set(c, &c->s1, w, 1)) || (rc = new_set(c, &c->s2, cp, 2)) || (rc = new_set(c, &c->s3, cm, 3))) return rc;
    pc[0] = (uint32_t)(bytes / 16);
    pc[1] = MEM_SEED;
    if ((rc = begin(c))) return rc;
    gvk_dispatch(&c->g, c->p1, c->s1, pc, sizeof pc, c->groups);   /* the pattern into the source */
    if ((rc = shot(c, &s))) return rc;
    *work = 2.0 * (double)bytes;
    *maxreps = 256;
    return GBP_OK;
}

static int prep_xfer(gb_ctx *c, int kind, size_t bytes, double *work, unsigned *maxreps)
{
    uint32_t *w;
    size_t i;
    double s;
    int rc;
    bytes = bytes / 16384 * 16384;
    if (bytes < 16384) bytes = 16384;
    c->bytes = bytes;
    c->ready = 1;
    if ((rc = new_buf(c, &c->sa, bytes, GVK_UPLOAD)) || (rc = new_buf(c, &c->a, bytes, GVK_DEVICE)) ||
        (rc = new_buf(c, &c->rb, bytes, GVK_READBACK))) return rc;
    w = c->sa.map;
    for (i = 0; i < bytes / 4; i++) w[i] = (uint32_t)i * 2654435761u + MEM_SEED;
    if (kind == GK_DOWNLOAD) {   /* the data to bring back has to be in video memory first */
        if ((rc = begin(c))) return rc;
        gvk_copy(&c->g, &c->sa, &c->a, bytes);
        if ((rc = shot(c, &s))) return rc;
    }
    *work = (double)bytes;
    *maxreps = 256;
    return GBP_OK;
}

static int prep_lat(gb_ctx *c, double *work, unsigned *maxreps)
{
    gvk_buf *o[1];
    double s;
    int rc;
    c->ready = 1;
    if ((rc = new_buf(c, &c->a, 64, GVK_DEVICE)) || (rc = new_buf(c, &c->cntrb, 64, GVK_READBACK)) ||
        (rc = new_pipe(c, &c->p1, spv_tick, sizeof spv_tick))) return rc;
    o[0] = &c->a;
    if ((rc = new_set(c, &c->s1, o, 1))) return rc;
    if ((rc = begin(c))) return rc;
    gvk_fill(&c->g, &c->a, 0);
    if ((rc = shot(c, &s))) return rc;
    c->ticks = 0;
    *work = 0;
    *maxreps = MAXLAT;
    return GBP_OK;
}

static int vk_prep(gb_ctx *c, int kind, int prec, int variant, size_t n, double *work, unsigned *maxreps)
{
    int rc;
    vk_release(c);
    c->kind = kind; c->prec = prec; c->prep_wrong = 0; c->verified = 0; c->seed = 0; c->err[0] = 0;
    *work = 0; *maxreps = 1;
    if (variant == GV_LIB) { snprintf(c->err, sizeof c->err, "Vulkan has no vendor library, only its own kernels"); return GBP_UNSUPPORTED; }
    if (variant != GV_OWN) { snprintf(c->err, sizeof c->err, "the register-blocked kernel is for ROCm / HIP only"); return GBP_UNSUPPORTED; }
    if (c->g.abandoned || c->g.lost) { snprintf(c->err, sizeof c->err, "%s", c->g.err); return GBP_LOST; }
    switch (kind) {
    case GK_GEMM: rc = prep_gemm(c, prec, n, work, maxreps); break;
    case GK_FMA: rc = prep_fma(c, prec, n, work, maxreps); break;
    case GK_COPY: rc = prep_copy(c, n, work, maxreps); break;
    case GK_UPLOAD: case GK_DOWNLOAD: rc = prep_xfer(c, kind, n, work, maxreps); break;
    case GK_LATENCY: rc = prep_lat(c, work, maxreps); break;
    default: rc = GBP_UNSUPPORTED;
    }
    if (rc) { if (!c->g.abandoned) vk_release(c); else c->ready = 0; }
    return rc;
}

/* ---------------------------------------------------------------- run */
static void cmp_dispatch(gb_ctx *c, vki_DescriptorSet s, uint32_t words)
{
    uint32_t pc[8] = { words };
    uint32_t g = words / 64 + 1;
    if (g > 1024) g = 1024;
    gvk_dispatch(&c->g, c->p2 && c->kind == GK_GEMM ? c->p2 : c->p3, s, pc, sizeof pc, clamp_groups(c, g));
}

static int read_count(gb_ctx *c, long *wrong)
{
    double s;
    int rc;
    if ((rc = begin(c))) return rc;
    gvk_copy(&c->g, &c->cnt, &c->cntrb, 64);
    if ((rc = shot(c, &s))) return rc;
    *wrong += ((const uint32_t *)c->cntrb.map)[0];
    return GBP_OK;
}

static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

static int vk_run(gb_ctx *c, unsigned reps, double *secs, long *wrong)
{
    unsigned i;
    int rc;
    *wrong = 0;
    if (!c->ready) return GBP_ERROR;
    if (c->g.abandoned || c->g.lost) return GBP_LOST;
    switch (c->kind) {
    case GK_GEMM: {
        uint32_t pc[8] = { (uint32_t)c->n };
        if ((rc = begin(c))) return rc;
        gvk_fill(&c->g, &c->cnt, 0);
        gvk_barrier(&c->g);
        for (i = 0; i < reps; i++) {
            gvk_dispatch2(&c->g, c->p1, c->s1, pc, sizeof pc, (uint32_t)(c->n / 16), (uint32_t)(c->n / 16));
            gvk_barrier(&c->g);
            cmp_dispatch(c, c->s2, c->words);
            gvk_barrier(&c->g);
        }
        if ((rc = shot(c, secs))) return rc;
        if ((rc = read_count(c, wrong))) return rc;
        *wrong += c->prep_wrong;
        c->prep_wrong = 0;
        return GBP_OK;
    }
    case GK_FMA: {
        const uint32_t *o = c->rb.map;
        uint32_t g, k, nv = c->prec == GP_FP16 ? 16 : 8, mask = c->prec == GP_FP16 ? 63 : 255, seed = ++c->seed;
        struct { float m1, c1, m2, c2; uint32_t iters, seed, n, pad; } pf = { 2.0f, 1.0f, 0.5f, -0.5f, reps, seed, c->words, 0 };
        struct { uint32_t A, C, u2, u3, iters, seed, n, pad; } pi = { 1664525u, 1013904223u, 0, 0, reps, seed, c->words, 0 };
        if ((rc = begin(c))) return rc;
        gvk_dispatch(&c->g, c->p1, c->s1, c->prec == GP_INT32 ? (void *)&pi : (void *)&pf, 32, c->groups);
        if ((rc = shot(c, secs))) return rc;
        for (g = 0; g < c->words; g++) {
            if (c->prec == GP_INT32) {
                uint32_t an = 1, cn = 0, a = 1664525u, cc = 1013904223u, n = reps, e = 0, x;
                while (n) { if (n & 1) { cn = a * cn + cc; an = a * an; } cc = a * cc + cc; a = a * a; n >>= 1; }
                for (k = 0; k < 8; k++) { x = k ? g * k_mul[k] + k : g ^ seed; e ^= an * x + cn; }
                if (o[g] != e) (*wrong)++;
            } else {
                uint32_t sum = 0;
                for (k = 0; k < nv; k++) sum += (k ? g * k_mul[k] + k : g + seed) & mask;
                if (((const float *)o)[g] != (float)sum) (*wrong)++;
            }
        }
        return GBP_OK;
    }
    case GK_COPY: {
        uint32_t pc[8] = { (uint32_t)(c->bytes / 16) };
        if ((rc = begin(c))) return rc;
        gvk_fill(&c->g, &c->cnt, 0);
        gvk_barrier(&c->g);
        for (i = 0; i < reps; i++) {
            gvk_dispatch(&c->g, c->p2, c->s2, pc, sizeof pc, c->groups);
            gvk_barrier(&c->g);
        }
        cmp_dispatch(c, c->s3, c->words);   /* source against destination, not timed apart: tiny next to reps copies */
        if ((rc = shot(c, secs))) return rc;
        return read_count(c, wrong);
    }
    case GK_UPLOAD: case GK_DOWNLOAD: {
        if ((rc = begin(c))) return rc;
        for (i = 0; i < reps; i++) {
            if (c->kind == GK_UPLOAD) gvk_copy(&c->g, &c->sa, &c->a, c->bytes); else gvk_copy(&c->g, &c->a, &c->rb, c->bytes);
            if (i + 1 < reps) gvk_barrier(&c->g);
        }
        if ((rc = shot(c, secs))) return rc;
        if (!c->verified) {   /* once per workload: what arrived is what was sent */
            double s;
            c->verified = 1;
            if (c->kind == GK_UPLOAD) {
                if ((rc = begin(c))) return rc;
                gvk_copy(&c->g, &c->a, &c->rb, c->bytes);
                if ((rc = shot(c, &s))) return rc;
            }
            if (memcmp(c->rb.map, c->sa.map, c->bytes)) *wrong = 1;
        }
        return GBP_OK;
    }
    case GK_LATENCY: {
        double *lat = malloc((reps ? reps : 1) * sizeof *lat);
        unsigned n = 0;
        if (!lat) { snprintf(c->err, sizeof c->err, "out of memory"); return GBP_ERROR; }
        for (i = 0; i < reps; i++) {
            double s, t0;
            if ((rc = begin(c))) { free(lat); return rc; }
            gvk_dispatch(&c->g, c->p1, c->s1, NULL, 0, 1);
            t0 = gvk_now();
            if ((rc = shot(c, &s))) { free(lat); return rc; }
            lat[n++] = gvk_now() - t0;
            c->ticks++;
        }
        if (n) { qsort(lat, n, sizeof *lat, cmp_d); *secs = lat[n / 2]; }
        free(lat);
        if ((rc = begin(c))) return rc;
        gvk_copy(&c->g, &c->a, &c->cntrb, 64);
        {
            double s;
            if ((rc = shot(c, &s))) return rc;
        }
        if (((const uint32_t *)c->cntrb.map)[0] != (uint32_t)c->ticks) *wrong = 1;
        return GBP_OK;
    }
    }
    return GBP_ERROR;
}

const gb_ops gb_vulkan = { GB_VULKAN, vk_available, vk_list, vk_open, vk_close, vk_prep, vk_run, vk_release, vk_error };

#else
static int na_av(char *h, size_t n) { if (h) snprintf(h, n, "Linux only"); return 0; }
static int na_list(gb_info *o, int m, char *w, size_t n) { (void)o; (void)m; if (w) snprintf(w, n, "Linux only"); return -1; }
const gb_ops gb_vulkan = { GB_VULKAN, na_av, na_list, NULL, NULL, NULL, NULL, NULL, NULL };
#endif
