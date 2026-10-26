/* gpuburn_cl.c - OpenCL back end of GPU Burnin and the GPU Compute comparison (see gpu_backend.h).
 * libOpenCL.so.1 (the ICD loader) is opened with dlopen; every platform's every device is its own entry,
 * so "AMD ROCm OpenCL" and "Rusticl" on one GPU show up as two columns. Own kernels are OpenCL C source
 * built at run time (gpu_kernels.h); the "library" variant is CLBlast (libclblast) when it is installed.
 * Not tested on real hardware by the author: the binding is checked with dlsym only. All calls for one
 * context must come from the thread that opened it. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "gpu_backend.h"
#include "gpu_kernels.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) && UINTPTR_MAX > 0xffffffffu
#include <dlfcn.h>
#include <time.h>

#define MEM_SEED 0x5EED1234u
#define WATCHDOG_S 5.0
#define CL_MAXDEV 32

typedef void *cl_h;
typedef struct {
    int tried, ok;
    char miss[96];
    void *lib, *blast;
    int have_blast;
    int (*GetPlatformIDs)(unsigned, cl_h *, unsigned *);
    int (*GetPlatformInfo)(cl_h, unsigned, size_t, void *, size_t *);
    int (*GetDeviceIDs)(cl_h, uint64_t, unsigned, cl_h *, unsigned *);
    int (*GetDeviceInfo)(cl_h, unsigned, size_t, void *, size_t *);
    cl_h (*CreateContext)(const void *, unsigned, const cl_h *, void *, void *, int *);
    int (*ReleaseContext)(cl_h);
    cl_h (*CreateQueue)(cl_h, cl_h, uint64_t, int *);
    int (*ReleaseQueue)(cl_h);
    cl_h (*CreateBuffer)(cl_h, uint64_t, size_t, void *, int *);
    int (*ReleaseMem)(cl_h);
    int (*WriteBuffer)(cl_h, cl_h, unsigned, size_t, size_t, const void *, unsigned, const cl_h *, cl_h *);
    int (*ReadBuffer)(cl_h, cl_h, unsigned, size_t, size_t, void *, unsigned, const cl_h *, cl_h *);
    cl_h (*CreateProgram)(cl_h, unsigned, const char **, const size_t *, int *);
    int (*BuildProgram)(cl_h, unsigned, const cl_h *, const char *, void *, void *);
    int (*GetBuildInfo)(cl_h, cl_h, unsigned, size_t, void *, size_t *);
    int (*ReleaseProgram)(cl_h);
    cl_h (*CreateKernel)(cl_h, const char *, int *);
    int (*ReleaseKernel)(cl_h);
    int (*SetKernelArg)(cl_h, unsigned, size_t, const void *);
    int (*NDRange)(cl_h, cl_h, unsigned, const size_t *, const size_t *, const size_t *, unsigned, const cl_h *, cl_h *);
    int (*GetEventInfo)(cl_h, unsigned, size_t, void *, size_t *);
    int (*GetEventProfiling)(cl_h, unsigned, size_t, void *, size_t *);
    int (*ReleaseEvent)(cl_h);
    int (*Finish)(cl_h);
    int (*Flush)(cl_h);
    int (*BlastS)(int, int, int, size_t, size_t, size_t, float, cl_h, size_t, size_t, cl_h, size_t, size_t, float, cl_h, size_t, size_t, cl_h *, cl_h *);
    int (*BlastD)(int, int, int, size_t, size_t, size_t, double, cl_h, size_t, size_t, cl_h, size_t, size_t, double, cl_h, size_t, size_t, cl_h *, cl_h *);
    int (*BlastH)(int, int, int, size_t, size_t, size_t, uint16_t, cl_h, size_t, size_t, cl_h, size_t, size_t, uint16_t, cl_h, size_t, size_t, cl_h *, cl_h *);
} cl_api;

static cl_api g_cl;

typedef struct { cl_h plat, dev; } cl_slot;
static cl_slot g_slots[CL_MAXDEV];
static int g_nslots;

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

#define CSYM(lib, field, name) \
    do { if (!(*(void **)&a->field = dlsym(lib, name)) && !a->miss[0]) snprintf(a->miss, sizeof a->miss, "%s", name); } while (0)

static int cl_bind(void)
{
    cl_api *a = &g_cl;
    const char *env = getenv("SRS_OPENCL_LIB");
    const char *const names[] = { "libOpenCL.so.1", "libOpenCL.so", NULL };
    const char *const bl[] = { "libclblast.so.1", "libclblast.so", NULL };
    int i;
    if (a->tried) return a->ok;
    a->tried = 1;
    if (env && *env) a->lib = dlopen(env, RTLD_NOW | RTLD_LOCAL);
    else for (i = 0; names[i] && !a->lib; i++) a->lib = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
    if (!a->lib) return 0;
    CSYM(a->lib, GetPlatformIDs, "clGetPlatformIDs");
    CSYM(a->lib, GetPlatformInfo, "clGetPlatformInfo");
    CSYM(a->lib, GetDeviceIDs, "clGetDeviceIDs");
    CSYM(a->lib, GetDeviceInfo, "clGetDeviceInfo");
    CSYM(a->lib, CreateContext, "clCreateContext");
    CSYM(a->lib, ReleaseContext, "clReleaseContext");
    CSYM(a->lib, CreateQueue, "clCreateCommandQueue");
    CSYM(a->lib, ReleaseQueue, "clReleaseCommandQueue");
    CSYM(a->lib, CreateBuffer, "clCreateBuffer");
    CSYM(a->lib, ReleaseMem, "clReleaseMemObject");
    CSYM(a->lib, WriteBuffer, "clEnqueueWriteBuffer");
    CSYM(a->lib, ReadBuffer, "clEnqueueReadBuffer");
    CSYM(a->lib, CreateProgram, "clCreateProgramWithSource");
    CSYM(a->lib, BuildProgram, "clBuildProgram");
    CSYM(a->lib, GetBuildInfo, "clGetProgramBuildInfo");
    CSYM(a->lib, ReleaseProgram, "clReleaseProgram");
    CSYM(a->lib, CreateKernel, "clCreateKernel");
    CSYM(a->lib, ReleaseKernel, "clReleaseKernel");
    CSYM(a->lib, SetKernelArg, "clSetKernelArg");
    CSYM(a->lib, NDRange, "clEnqueueNDRangeKernel");
    CSYM(a->lib, GetEventInfo, "clGetEventInfo");
    CSYM(a->lib, GetEventProfiling, "clGetEventProfilingInfo");
    CSYM(a->lib, ReleaseEvent, "clReleaseEvent");
    CSYM(a->lib, Finish, "clFinish");
    CSYM(a->lib, Flush, "clFlush");
    if (a->miss[0]) return 0;
    for (i = 0; bl[i] && !a->blast; i++) a->blast = dlopen(bl[i], RTLD_NOW | RTLD_LOCAL);
    if (a->blast) {
        char save[96];
        snprintf(save, sizeof save, "%.90s", a->miss);
        CSYM(a->blast, BlastS, "CLBlastSgemm");
        CSYM(a->blast, BlastD, "CLBlastDgemm");
        CSYM(a->blast, BlastH, "CLBlastHgemm");
        a->have_blast = !a->miss[0];
        snprintf(a->miss, sizeof a->miss, "%.90s", save);
    }
    a->ok = 1;
    return 1;
}

int gb_cl_bind_check(char *detail, size_t dn)
{
    if (!cl_bind()) {
        if (g_cl.lib) { snprintf(detail, dn, "libOpenCL loaded, but %s is missing", g_cl.miss); return 1; }
        snprintf(detail, dn, "not installed");
        return -1;
    }
    snprintf(detail, dn, "libOpenCL: all symbols found; CLBlast %s", g_cl.have_blast ? "found" : "not installed");
    return 0;
}

/* ---------------------------------------------------------------- listing */
static int has(const char *s, const char *w)
{
    char a[200], b[64];
    size_t i;
    for (i = 0; s[i] && i < sizeof a - 1; i++) a[i] = (char)((s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i]);
    a[i] = 0;
    for (i = 0; w[i] && i < sizeof b - 1; i++) b[i] = (char)((w[i] >= 'A' && w[i] <= 'Z') ? w[i] + 32 : w[i]);
    b[i] = 0;
    return strstr(a, b) != NULL;
}

static int cl_available(char *hint, size_t hn)
{
    if (!cl_bind()) {
        if (hint) snprintf(hint, hn, "%s", g_cl.lib ? "the OpenCL loader is too old or damaged" : gb_hint[GB_OPENCL]);
        return 0;
    }
    if (hint) hint[0] = 0;
    return 1;
}

static int cl_list(gb_info *out, int max, char *why, size_t wn)
{
    cl_api *a = &g_cl;
    cl_h plats[16];
    unsigned np = 0, p;
    int n = 0, e;
    if (!cl_bind()) { snprintf(why, wn, "%s", gb_hint[GB_OPENCL]); return -1; }
    g_nslots = 0;
    e = a->GetPlatformIDs(16, plats, &np);
    if (e == -1001 || (!e && !np)) { snprintf(why, wn, "OpenCL is installed but no OpenCL platform (driver) is registered"); return 0; }
    if (e) { snprintf(why, wn, "OpenCL could not list its platforms (error %d)", e); return -1; }
    for (p = 0; p < np; p++) {
        char pname[128] = "", pver[128] = "", pvend[128] = "";
        cl_h devs[16];
        unsigned nd = 0, d;
        a->GetPlatformInfo(plats[p], 0x0902, sizeof pname, pname, NULL);
        a->GetPlatformInfo(plats[p], 0x0901, sizeof pver, pver, NULL);
        a->GetPlatformInfo(plats[p], 0x0903, sizeof pvend, pvend, NULL);
        if (a->GetDeviceIDs(plats[p], 0xFFFFFFFFull, 16, devs, &nd) || !nd) continue;
        for (d = 0; d < nd && n < max && g_nslots < CL_MAXDEV; d++) {
            gb_info *o = &out[n];
            char dname[200] = "", dver[128] = "", ext[2048] = "";
            uint64_t type = 0, mem = 0, fp = 0;
            unsigned pci[4] = { 0 };
            unsigned char topo[32];
            memset(o, 0, sizeof *o);
            a->GetDeviceInfo(devs[d], 0x102B, sizeof dname, dname, NULL);
            a->GetDeviceInfo(devs[d], 0x102D, sizeof dver, dver, NULL);
            a->GetDeviceInfo(devs[d], 0x1000, sizeof type, &type, NULL);
            a->GetDeviceInfo(devs[d], 0x101F, sizeof mem, &mem, NULL);
            a->GetDeviceInfo(devs[d], 0x1032, sizeof fp, &fp, NULL);
            a->GetDeviceInfo(devs[d], 0x1030, sizeof ext - 1, ext, NULL);
            o->backend = GB_OPENCL;
            o->index = g_nslots;
            g_slots[g_nslots].plat = plats[p];
            g_slots[g_nslots].dev = devs[d];
            g_nslots++;
            if (has(pname, "rusticl")) snprintf(o->label, sizeof o->label, "OpenCL (Rusticl)");
            else if (has(pname, "amd") || has(pname, "rocm")) snprintf(o->label, sizeof o->label, "OpenCL (ROCm)");
            else if (has(pname, "nvidia")) snprintf(o->label, sizeof o->label, "OpenCL (NVIDIA)");
            else if (has(pname, "portable") || has(pname, "pocl")) snprintf(o->label, sizeof o->label, "OpenCL (PoCL)");
            else if (has(pname, "intel")) snprintf(o->label, sizeof o->label, "OpenCL (Intel)");
            else snprintf(o->label, sizeof o->label, "OpenCL (%.40s)", pname[0] ? pname : pvend);
            snprintf(o->device, sizeof o->device, "%s", dname);
            snprintf(o->runtime, sizeof o->runtime, "%.60s; driver %.60s%s", pver, dver, g_cl.have_blast ? "; CLBlast found" : "");
            o->is_cpu = (type & 2) != 0;
            snprintf(o->kind, sizeof o->kind, "%s", o->is_cpu ? "CPU" : "GPU");
            o->mem_bytes = (double)mem;
            o->has_lib = g_cl.have_blast;
            o->fp64 = fp != 0;
            o->fp16 = strstr(ext, "cl_khr_fp16") != NULL;
            if (!a->GetDeviceInfo(devs[d], 0x410F, sizeof pci, pci, NULL))   /* cl_khr_pci_bus_info */
                snprintf(o->pci, sizeof o->pci, "%04x:%02x:%02x.%x", pci[0] & 0xffff, pci[1] & 0xff, pci[2] & 0x1f, pci[3] & 7);
            else if (!a->GetDeviceInfo(devs[d], 0x4037, sizeof topo, topo, NULL) && *(unsigned *)topo == 1)   /* AMD topology */
                snprintf(o->pci, sizeof o->pci, "0000:%02x:%02x.%x", topo[21], topo[22] & 0x1f, topo[23] & 7);
            n++;
        }
    }
    if (!n) snprintf(why, wn, "OpenCL found no device");
    return n;
}

/* ---------------------------------------------------------------- context */
struct gb_ctx {
    cl_api *a;
    cl_h plat, dev, ctx, q, prog[2];
    cl_h k_gemm, k_fma, k_copy, k_cmp, k_tick;
    cl_h ba, bb, bc, bref, bcnt;
    int dead, kind, prec, variant, ready, built_fp64;
    size_t n, es, bytes, nthreads;
    long prep_wrong, ticks;
    unsigned seed, gx;
    void *host, *ha, *hb;
    int verified;
    char err[240];
};

static void seterr(gb_ctx *c, int e, const char *what) { snprintf(c->err, sizeof c->err, "%s failed (OpenCL error %d)", what, e); }

static int cl_open(gb_ctx **cp, const gb_info *d, int so, char *why, size_t wn)
{
    cl_api *a = &g_cl;
    gb_ctx *c;
    int e = 0;
    (void)so;
    if (!cl_bind()) { snprintf(why, wn, "%s", gb_hint[GB_OPENCL]); return -1; }
    if (d->index < 0 || d->index >= g_nslots) { snprintf(why, wn, "OpenCL device list is out of date"); return -1; }
    if (!(c = calloc(1, sizeof *c))) { snprintf(why, wn, "out of memory"); return -1; }
    c->a = a;
    c->plat = g_slots[d->index].plat;
    c->dev = g_slots[d->index].dev;
    c->ctx = a->CreateContext(NULL, 1, &c->dev, NULL, NULL, &e);
    if (!c->ctx || e) { snprintf(why, wn, "could not open the OpenCL device (error %d)", e); free(c); return -1; }
    c->q = a->CreateQueue(c->ctx, c->dev, 2, &e);   /* profiling on */
    if (!c->q || e) { snprintf(why, wn, "could not create an OpenCL queue (error %d)", e); a->ReleaseContext(c->ctx); free(c); return -1; }
    *cp = c;
    return 0;
}

static void cl_release(gb_ctx *c)
{
    cl_api *a = c->a;
    cl_h *m[] = { &c->ba, &c->bb, &c->bc, &c->bref, &c->bcnt };
    size_t i;
    if (!c->ready) return;
    if (!c->dead) for (i = 0; i < sizeof m / sizeof *m; i++) if (*m[i]) a->ReleaseMem(*m[i]);
    for (i = 0; i < sizeof m / sizeof *m; i++) *m[i] = NULL;
    free(c->host); free(c->ha); free(c->hb);
    c->host = c->ha = c->hb = NULL;
    c->ready = 0;
}

static void cl_close(gb_ctx *c)
{
    int i;
    if (!c) return;
    cl_release(c);
    if (!c->dead) {   /* a hung GPU may still be using these: leave them */
        cl_h *k[] = { &c->k_gemm, &c->k_fma, &c->k_copy, &c->k_cmp, &c->k_tick };
        for (i = 0; i < 5; i++) if (*k[i]) c->a->ReleaseKernel(*k[i]);
        for (i = 0; i < 2; i++) if (c->prog[i]) c->a->ReleaseProgram(c->prog[i]);
        c->a->ReleaseQueue(c->q);
        c->a->ReleaseContext(c->ctx);
        free(c);
    }
}

static const char *cl_error(gb_ctx *c) { return c->err; }

static int build(gb_ctx *c, int fp64)
{
    cl_api *a = c->a;
    char *src = NULL;
    const char *sp;
    int e = 0, rc = GBP_ERROR;
    cl_h *k[] = { &c->k_gemm, &c->k_fma, &c->k_copy, &c->k_cmp, &c->k_tick };
    static const char *const kn[] = { "gemm_r", "fma_r", "copy_u4", "cmp_u32", "tick" };
    int i;
    if (c->prog[fp64] && c->built_fp64 == fp64 + 1) return GBP_OK;
    for (i = 0; i < 5; i++) if (*k[i]) { a->ReleaseKernel(*k[i]); *k[i] = NULL; }
    if (!c->prog[fp64]) {
        if (!(src = gk_source(GKD_OPENCL, fp64))) { snprintf(c->err, sizeof c->err, "out of memory"); return GBP_ERROR; }
        sp = src;
        c->prog[fp64] = a->CreateProgram(c->ctx, 1, &sp, NULL, &e);
        if (!c->prog[fp64] || e) { seterr(c, e, "creating the program"); goto out; }
        if ((e = a->BuildProgram(c->prog[fp64], 1, &c->dev, "", NULL, NULL))) {
            char log[400] = "";
            a->GetBuildInfo(c->prog[fp64], c->dev, 0x1183, sizeof log - 1, log, NULL);
            snprintf(c->err, sizeof c->err, "the OpenCL kernel did not build: %.180s", log);
            a->ReleaseProgram(c->prog[fp64]);
            c->prog[fp64] = NULL;
            goto out;
        }
    }
    for (i = 0; i < 5; i++) {
        *k[i] = a->CreateKernel(c->prog[fp64], kn[i], &e);
        if (!*k[i] || e) { seterr(c, e, "finding a kernel"); goto out; }
    }
    c->built_fp64 = fp64 + 1;
    rc = GBP_OK;
out:
    free(src);
    return rc;
}

static int mk(gb_ctx *c, cl_h *m, size_t n)
{
    int e = 0;
    *m = c->a->CreateBuffer(c->ctx, 1, n, NULL, &e);   /* read-write */
    if (!*m || e) { char b[64]; snprintf(b, sizeof b, "getting %.0f MB of device memory", (double)n / 1e6); seterr(c, e, b); *m = NULL; return GBP_ERROR; }
    return GBP_OK;
}

static int wr(gb_ctx *c, cl_h m, const void *p, size_t n)
{
    int e = c->a->WriteBuffer(c->q, m, 1, 0, n, p, 0, NULL, NULL);
    if (e) { seterr(c, e, "copying to the device"); return GBP_ERROR; }
    return GBP_OK;
}

static int rd(gb_ctx *c, void *p, cl_h m, size_t n)
{
    int e = c->a->ReadBuffer(c->q, m, 1, 0, n, p, 0, NULL, NULL);
    if (e) { seterr(c, e, "copying from the device"); return GBP_ERROR; }
    return GBP_OK;
}

static int setarg(gb_ctx *c, cl_h k, unsigned i, size_t sz, const void *v)
{
    int e = c->a->SetKernelArg(k, i, sz, v);
    if (e) { seterr(c, e, "setting a kernel argument"); return GBP_ERROR; }
    return GBP_OK;
}

static int enq(gb_ctx *c, cl_h k, size_t gx, size_t gy, size_t lx, size_t ly, cl_h *ev)
{
    size_t g[2] = { gx, gy }, l[2] = { lx, ly };
    int e = c->a->NDRange(c->q, k, gy > 1 ? 2 : 1, NULL, g, lx ? l : NULL, 0, NULL, ev);
    if (e) { seterr(c, e, "starting a kernel"); return GBP_ERROR; }
    return GBP_OK;
}

/* wait for the last event; the GPU gets WATCHDOG_S seconds, then the run ends and nothing is freed */
static int wait_ev(gb_ctx *c, cl_h ev)
{
    double t0 = now_s();
    c->a->Flush(c->q);
    for (;;) {
        int st = 1, e = c->a->GetEventInfo(ev, 0x11D3, sizeof st, &st, NULL);
        struct timespec d = { 0, 100000 };
        if (e) { seterr(c, e, "the GPU"); return GBP_LOST; }
        if (st == 0) return GBP_OK;
        if (st < 0) { seterr(c, st, "the GPU work"); return GBP_LOST; }
        if (now_s() - t0 > WATCHDOG_S) {
            c->dead = 1;
            snprintf(c->err, sizeof c->err, "the GPU did not finish its work within %.0f s; the run was ended and the GPU left alone", WATCHDOG_S);
            return GBP_HUNG;
        }
        nanosleep(&d, NULL);
    }
}

static int span(gb_ctx *c, cl_h first, cl_h last, double *secs)
{
    uint64_t t0 = 0, t1 = 0;
    int rc = wait_ev(c, last), e;
    if (rc) return rc;
    e = c->a->GetEventProfiling(first, 0x1282, sizeof t0, &t0, NULL);
    e |= c->a->GetEventProfiling(last, 0x1283, sizeof t1, &t1, NULL);
    if (e || t1 < t0) { seterr(c, e ? e : -1, "timing"); return GBP_ERROR; }
    *secs = (double)(t1 - t0) * 1e-9;
    return GBP_OK;
}

static double half_to_d(uint16_t h)
{
    int e = (h >> 10) & 31, m = h & 1023;
    double v = e == 0 ? m / 1024.0 / 16384.0 : (1.0 + m / 1024.0) * (e >= 15 ? (double)(1u << (e - 15)) : 1.0 / (double)(1u << (15 - e)));
    return (h & 0x8000) ? -v : v;
}

static double mget(const void *m, size_t i, size_t es)
{
    if (es == 8) return ((const double *)m)[i];
    if (es == 2) return half_to_d(((const uint16_t *)m)[i]);
    return (double)((const float *)m)[i];
}

static void fill_mat(void *m, size_t n, size_t es, int which)
{
    size_t i, k;
    for (i = 0; i < n; i++)
        for (k = 0; k < n; k++) {
            int v = ((i * 5 + k * 3 + (size_t)which) % 7) < 3;
            if (es == 8) ((double *)m)[i * n + k] = v;
            else if (es == 2) ((uint16_t *)m)[i * n + k] = v ? 0x3C00 : 0;
            else ((float *)m)[i * n + k] = (float)v;
        }
}

/* one GEMM into dst; ev (may be NULL) receives its event */
static int gemm_once(gb_ctx *c, cl_h dst, cl_h *ev)
{
    int nn = (int)c->n;
    if (c->variant == GV_LIB) {
        cl_api *a = c->a;
        int s;
        size_t n = c->n;
        if (c->prec == GP_FP64) s = a->BlastD(101, 111, 111, n, n, n, 1.0, c->ba, 0, n, c->bb, 0, n, 0.0, dst, 0, n, &c->q, ev);
        else if (c->prec == GP_FP16) s = a->BlastH(101, 111, 111, n, n, n, 0x3C00, c->ba, 0, n, c->bb, 0, n, 0, dst, 0, n, &c->q, ev);
        else s = a->BlastS(101, 111, 111, n, n, n, 1.0f, c->ba, 0, n, c->bb, 0, n, 0.0f, dst, 0, n, &c->q, ev);
        if (s) { snprintf(c->err, sizeof c->err, "CLBlast failed (status %d)", s); return GBP_ERROR; }
        return GBP_OK;
    }
    if (setarg(c, c->k_gemm, 0, sizeof(cl_h), &c->ba) || setarg(c, c->k_gemm, 1, sizeof(cl_h), &c->bb) ||
        setarg(c, c->k_gemm, 2, sizeof(cl_h), &dst) || setarg(c, c->k_gemm, 3, sizeof nn, &nn)) return GBP_ERROR;
    return enq(c, c->k_gemm, c->n, c->n, 16, 16, ev);
}

static int cmp_once(gb_ctx *c, cl_h x, cl_h y, unsigned words, cl_h *ev)
{
    size_t g = (size_t)(words / 256 + 1);
    if (g > 1024) g = 1024;
    if (setarg(c, c->k_cmp, 0, sizeof(cl_h), &x) || setarg(c, c->k_cmp, 1, sizeof(cl_h), &y) ||
        setarg(c, c->k_cmp, 2, sizeof(cl_h), &c->bcnt) || setarg(c, c->k_cmp, 3, sizeof words, &words)) return GBP_ERROR;
    return enq(c, c->k_cmp, g * 256, 1, 256, 1, ev);
}

static int cl_prep(gb_ctx *c, int kind, int prec, int variant, size_t n, double *work, unsigned *maxreps)
{
    int rc = GBP_ERROR;
    uint32_t zero[16] = { 0 };
    cl_release(c);
    c->kind = kind; c->prec = prec; c->variant = variant; c->prep_wrong = 0; c->verified = 0; c->seed = 0; c->err[0] = 0;
    *work = 0; *maxreps = 1;
    if (c->dead) { snprintf(c->err, sizeof c->err, "the GPU stopped answering earlier in this run"); return GBP_LOST; }
    c->ready = 1;
    switch (kind) {
    case GK_GEMM: {
        size_t es = prec == GP_FP64 ? 8 : prec == GP_FP16 ? 2 : 4, bytes, k;
        int t;
        double s;
        cl_h ev = NULL;
        if (prec == GP_INT32) { rc = GBP_UNSUPPORTED; break; }
        if (variant == GV_OWN && prec == GP_FP16) { snprintf(c->err, sizeof c->err, "FP16 is measured through CLBlast only"); rc = GBP_UNSUPPORTED; break; }
        if (variant == GV_LIB && !c->a->have_blast) { snprintf(c->err, sizeof c->err, "CLBlast is not installed"); rc = GBP_UNSUPPORTED; break; }
        if ((rc = build(c, prec == GP_FP64))) break;
        n = n / 16 * 16;
        if (n < 16) n = 16;
        if (prec == GP_FP16 && n > 2048) n = 2048;
        bytes = n * n * es;
        c->n = n; c->es = es; c->bytes = bytes;
        if ((rc = mk(c, &c->ba, bytes)) || (rc = mk(c, &c->bb, bytes)) || (rc = mk(c, &c->bc, bytes)) || (rc = mk(c, &c->bref, bytes)) || (rc = mk(c, &c->bcnt, 64))) break;
        c->ha = malloc(bytes); c->hb = malloc(bytes); c->host = malloc(bytes);
        if (!c->ha || !c->hb || !c->host) { snprintf(c->err, sizeof c->err, "out of memory"); rc = GBP_ERROR; break; }
        fill_mat(c->ha, n, es, 0);
        fill_mat(c->hb, n, es, 1);
        if ((rc = wr(c, c->ba, c->ha, bytes)) || (rc = wr(c, c->bb, c->hb, bytes))) break;
        if ((rc = gemm_once(c, c->bref, &ev))) break;
        if ((rc = span(c, ev, ev, &s))) break;
        c->a->ReleaseEvent(ev);
        if ((rc = rd(c, c->host, c->bref, bytes))) break;
        for (t = 0; t < 64; t++) {   /* the first result is the reference: check a spread of its entries on the host */
            size_t row = (size_t)t * 2654435761u % n, q = (size_t)t * 40503u % n;
            double ex = 0;
            for (k = 0; k < n; k++) ex += mget(c->ha, row * n + k, es) * mget(c->hb, k * n + q, es);
            if (mget(c->host, row * n + q, es) != ex) c->prep_wrong++;
        }
        *work = 2.0 * (double)n * (double)n * (double)n;
        *maxreps = 4096;
        rc = GBP_OK;
        break;
    }
    case GK_FMA: {
        int fp64 = prec == GP_FP64;
        if (prec != GP_FP32 && prec != GP_FP64) { rc = GBP_UNSUPPORTED; break; }
        if (variant == GV_LIB) { rc = GBP_UNSUPPORTED; break; }
        if ((rc = build(c, fp64))) break;
        c->gx = (unsigned)(n ? n : 1024);
        c->nthreads = (size_t)c->gx * 64;
        if ((rc = mk(c, &c->ba, c->nthreads * (fp64 ? 8 : 4)))) break;
        c->host = malloc(c->nthreads * (fp64 ? 8 : 4));
        if (!c->host) { snprintf(c->err, sizeof c->err, "out of memory"); rc = GBP_ERROR; break; }
        *work = 32.0 * (double)c->nthreads;
        *maxreps = 1u << 22;
        rc = GBP_OK;
        break;
    }
    case GK_COPY: {
        size_t i, bytes = n / 16384 * 16384;
        uint32_t *w;
        if (bytes < 16384) bytes = 16384;
        if (variant == GV_LIB) { rc = GBP_UNSUPPORTED; break; }
        if ((rc = build(c, 0))) break;
        c->bytes = bytes;
        if ((rc = mk(c, &c->ba, bytes)) || (rc = mk(c, &c->bb, bytes)) || (rc = mk(c, &c->bcnt, 64))) break;
        c->host = malloc(bytes);
        if (!c->host) { snprintf(c->err, sizeof c->err, "out of memory"); rc = GBP_ERROR; break; }
        w = c->host;
        for (i = 0; i < bytes / 4; i++) w[i] = (uint32_t)i * 2654435761u + MEM_SEED;
        if ((rc = wr(c, c->ba, c->host, bytes))) break;
        *work = 2.0 * (double)bytes;
        *maxreps = 256;
        rc = GBP_OK;
        break;
    }
    case GK_UPLOAD: case GK_DOWNLOAD: {
        size_t i, bytes = n / 16384 * 16384;
        uint32_t *w;
        if (bytes < 16384) bytes = 16384;
        c->bytes = bytes;
        if ((rc = mk(c, &c->ba, bytes))) break;
        c->host = malloc(bytes);
        c->ha = malloc(bytes);
        if (!c->host || !c->ha) { snprintf(c->err, sizeof c->err, "out of memory"); rc = GBP_ERROR; break; }
        w = c->host;
        for (i = 0; i < bytes / 4; i++) w[i] = (uint32_t)i * 2654435761u + MEM_SEED;
        if (kind == GK_DOWNLOAD && (rc = wr(c, c->ba, c->host, bytes))) break;
        *work = (double)bytes;
        *maxreps = 256;
        rc = GBP_OK;
        break;
    }
    case GK_LATENCY:
        if ((rc = build(c, 0)) || (rc = mk(c, &c->ba, 64)) || (rc = wr(c, c->ba, zero, 64))) break;
        c->ticks = 0;
        *maxreps = 20000;
        rc = GBP_OK;
        break;
    default: rc = GBP_UNSUPPORTED;
    }
    if (rc) { if (c->dead) c->ready = 0; else cl_release(c); }
    return rc;
}

static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

static int cl_run(gb_ctx *c, unsigned reps, double *secs, long *wrong)
{
    cl_api *a = c->a;
    unsigned i;
    int rc;
    uint32_t zero[16] = { 0 };
    *wrong = 0;
    if (!c->ready) return GBP_ERROR;
    if (c->dead) return GBP_LOST;
    switch (c->kind) {
    case GK_GEMM: {
        unsigned words = (unsigned)(c->bytes / 4);
        cl_h first = NULL, last = NULL;
        uint32_t v = 0;
        if ((rc = wr(c, c->bcnt, zero, 64))) return rc;
        for (i = 0; i < reps; i++) {
            if ((rc = gemm_once(c, c->bc, i == 0 ? &first : NULL)) || (rc = cmp_once(c, c->bc, c->bref, words, i + 1 == reps ? &last : NULL))) return rc;
        }
        if (!reps) return GBP_OK;
        if (reps == 1) { /* first is the GEMM, last is the compare */ }
        if ((rc = span(c, first, last, secs))) return rc;
        a->ReleaseEvent(first); a->ReleaseEvent(last);
        if ((rc = rd(c, &v, c->bcnt, 4))) return rc;
        *wrong = (long)v + c->prep_wrong;
        c->prep_wrong = 0;
        return GBP_OK;
    }
    case GK_FMA: {
        int fp64 = c->prec == GP_FP64;
        unsigned seed = ++c->seed, nn = (unsigned)c->nthreads, g, k;
        float mf[4] = { 2.0f, 1.0f, 0.5f, -0.5f };
        double md[4] = { 2.0, 1.0, 0.5, -0.5 };
        cl_h ev = NULL;
        size_t as = fp64 ? 8 : 4;
        int e = 0;
        e |= setarg(c, c->k_fma, 0, sizeof(cl_h), &c->ba);
        for (i = 0; i < 4; i++) e |= setarg(c, c->k_fma, 1 + i, as, fp64 ? (const void *)&md[i] : (const void *)&mf[i]);
        e |= setarg(c, c->k_fma, 5, sizeof reps, &reps) | setarg(c, c->k_fma, 6, sizeof seed, &seed) | setarg(c, c->k_fma, 7, sizeof nn, &nn);
        if (e) return GBP_ERROR;
        if ((rc = enq(c, c->k_fma, (size_t)c->gx * 64, 1, 64, 1, &ev)) || (rc = span(c, ev, ev, secs))) return rc;
        a->ReleaseEvent(ev);
        if ((rc = rd(c, c->host, c->ba, c->nthreads * as))) return rc;
        for (g = 0; g < nn; g++) {
            uint32_t sum = 0;
            static const uint32_t km[8] = { 1, 3, 5, 7, 11, 13, 17, 19 };
            double got = fp64 ? ((const double *)c->host)[g] : (double)((const float *)c->host)[g];
            for (k = 0; k < 8; k++) sum += (k ? g * km[k] + k : g + seed) & 255u;
            if (got != (double)sum) (*wrong)++;
        }
        return GBP_OK;
    }
    case GK_COPY: {
        unsigned n4 = (unsigned)(c->bytes / 16), words = (unsigned)(c->bytes / 4);
        size_t g = n4 / 256 + 1;
        cl_h first = NULL, last = NULL;
        uint32_t v = 0;
        if (g > 4096) g = 4096;
        if ((rc = wr(c, c->bcnt, zero, 64))) return rc;
        if (setarg(c, c->k_copy, 0, sizeof(cl_h), &c->ba) || setarg(c, c->k_copy, 1, sizeof(cl_h), &c->bb) || setarg(c, c->k_copy, 2, sizeof n4, &n4)) return GBP_ERROR;
        for (i = 0; i < reps; i++) if ((rc = enq(c, c->k_copy, g * 256, 1, 256, 1, i == 0 ? &first : NULL))) return rc;
        if ((rc = cmp_once(c, c->ba, c->bb, words, &last))) return rc;
        if (!first) first = last;
        if ((rc = span(c, first, last, secs))) return rc;
        if ((rc = rd(c, &v, c->bcnt, 4))) return rc;
        *wrong = (long)v;
        return GBP_OK;
    }
    case GK_UPLOAD: case GK_DOWNLOAD: {
        double t0 = now_s();
        for (i = 0; i < reps; i++) if ((rc = c->kind == GK_UPLOAD ? wr(c, c->ba, c->host, c->bytes) : rd(c, c->ha, c->ba, c->bytes))) return rc;
        *secs = now_s() - t0;
        if (!c->verified) {
            c->verified = 1;
            if (c->kind == GK_UPLOAD && (rc = rd(c, c->ha, c->ba, c->bytes))) return rc;
            if (memcmp(c->ha, c->host, c->bytes)) *wrong = 1;
        }
        return GBP_OK;
    }
    case GK_LATENCY: {
        double *lat = malloc((reps ? reps : 1) * sizeof *lat);
        uint32_t v = 0;
        cl_h ev = NULL;
        if (!lat) { snprintf(c->err, sizeof c->err, "out of memory"); return GBP_ERROR; }
        if (setarg(c, c->k_tick, 0, sizeof(cl_h), &c->ba)) { free(lat); return GBP_ERROR; }
        for (i = 0; i < reps; i++) {
            double t0 = now_s();
            if ((rc = enq(c, c->k_tick, 1, 1, 0, 0, &ev)) || (rc = wait_ev(c, ev))) { free(lat); return rc; }
            a->ReleaseEvent(ev);
            lat[i] = now_s() - t0;
            c->ticks++;
        }
        if (reps) { qsort(lat, reps, sizeof *lat, cmp_d); *secs = lat[reps / 2]; }
        free(lat);
        if ((rc = rd(c, &v, c->ba, 4))) return rc;
        if (v != (uint32_t)c->ticks) *wrong = 1;
        return GBP_OK;
    }
    }
    return GBP_ERROR;
}

const gb_ops gb_opencl = { GB_OPENCL, cl_available, cl_list, cl_open, cl_close, cl_prep, cl_run, cl_release, cl_error };

#else   /* not 64-bit Linux */
int gb_cl_bind_check(char *d, size_t n) { snprintf(d, n, "64-bit Linux only"); return -1; }
static int na_av(char *h, size_t n) { if (h) snprintf(h, n, "64-bit Linux only"); return 0; }
static int na_list(gb_info *o, int m, char *w, size_t n) { (void)o; (void)m; if (w) snprintf(w, n, "64-bit Linux only"); return -1; }
const gb_ops gb_opencl = { GB_OPENCL, na_av, na_list, NULL, NULL, NULL, NULL, NULL, NULL };
#endif
