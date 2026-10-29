/* gpuburn_rt.c - ROCm / HIP and NVIDIA CUDA back ends of GPU Burnin and the GPU Compute comparison
 * (see gpu_backend.h). One file because the two driver APIs are twins: the same calls with a hip or cu
 * prefix. Everything is opened with dlopen / dlsym at run time (no build dependency):
 *   HIP : libamdhip64 (runtime), libhiprtc (own kernels, compiled at run time), libhipblas (library GEMM)
 *   CUDA: libcuda (driver), libnvrtc (own kernels, only as a fallback), libcublas (library GEMM)
 * CUDA's own kernels come precompiled as PTX text (gpu_kernels_ptx.inc, made by clang from gpu_kernels.h):
 * cuModuleLoadData takes PTX directly and the driver (or ZLUDA) finishes it for the GPU, so CUDA needs no
 * runtime compiler. The built-in PTX is always tried first (the same kernels on every PC); NVRTC builds the
 * same source only when the driver refuses the PTX. HIP has no such portable form: hipModuleLoadData wants
 * a code object for the exact GPU (gfx1201, gfx1100, ...), one per GPU generation, megabytes each and
 * changing with every ROCm release, so HIP keeps compiling with hipRTC.
 * If libcuda turns out to be ZLUDA (CUDA calls translated to an AMD GPU), the column says so.
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
#include <pthread.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include "gpu_kernels_ptx.inc"

#define MEM_SEED 0x5EED1234u
#define WATCHDOG_S 5.0

typedef struct {
    int cuda, tried, ok;
    char miss[96], libpath[PATH_MAX];
    void *lib, *rtc, *blas;
    int zluda, have_rtc, have_blas;
    /* runtime */
    int (*Init)(unsigned);
    int (*DeviceGetCount)(int *);
    int (*DeviceGet)(int *, int);
    int (*DeviceGetName)(char *, int, int);
    int (*DeviceTotalMem)(size_t *, int);
    int (*DeviceGetPCIBusId)(char *, int, int);
    int (*DriverGetVersion)(int *);
    int (*RuntimeGetVersion)(int *);
    int (*SetDevice)(int);
    int (*CtxCreate)(void **, unsigned, int);
    int (*CtxDestroy)(void *);
    int (*Malloc)(void **, size_t);
    int (*Free)(void *);
    int (*HostAlloc)(void **, size_t);
    int (*HostFree)(void *);
    int (*HtoD)(void *, const void *, size_t);
    int (*DtoH)(void *, void *, size_t);
    int (*MemsetD32)(void *, unsigned, size_t);
    int (*ModuleLoadData)(void **, const void *);
    int (*ModuleUnload)(void *);
    int (*ModuleGetFunction)(void **, void *, const char *);
    int (*Launch)(void *, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, void *, void **, void **);
    int (*EventCreate)(void **, unsigned);
    int (*EventRecord)(void *, void *);
    int (*EventQuery)(void *);
    int (*EventElapsed)(float *, void *, void *);
    int (*EventDestroy)(void *);
    const char *(*HipErrStr)(int);
    int (*CuErrStr)(int, const char **);
    /* runtime compiler */
    int (*RtcCreate)(void **, const char *, const char *, int, const char **, const char **);
    int (*RtcCompile)(void *, int, const char **);
    int (*RtcSize)(void *, size_t *);
    int (*RtcCode)(void *, char *);
    int (*RtcLogSize)(void *, size_t *);
    int (*RtcLog)(void *, char *);
    int (*RtcDestroy)(void **);
    /* BLAS */
    int (*BlasCreate)(void **);
    int (*BlasDestroy)(void *);
    int (*Sgemm)(void *, int, int, int, int, int, const float *, const void *, int, const void *, int, const float *, void *, int);
    int (*Dgemm)(void *, int, int, int, int, int, const double *, const void *, int, const void *, int, const double *, void *, int);
    int (*Hgemm)(void *, int, int, int, int, int, const uint16_t *, const void *, int, const void *, int, const uint16_t *, void *, int);
} rt_api;

static rt_api g_rt[2];   /* [0] HIP, [1] CUDA */

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static void *open_one(const char *const *names, char *path, size_t pn)
{
    int i;
    for (i = 0; names[i]; i++) {
        void *h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
        if (h) { if (path) snprintf(path, pn, "%s", names[i]); return h; }
    }
    return NULL;
}

/* does this library file carry ZLUDA's marker? (the real NVIDIA driver library does not) */
static int file_has(const char *path, const char *needle)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat st;
    void *m;
    int hit = 0;
    if (fd < 0) return 0;
    if (fstat(fd, &st) == 0 && st.st_size > 0 && (m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) != MAP_FAILED) {
        hit = memmem(m, (size_t)st.st_size, needle, strlen(needle)) != NULL;
        munmap(m, (size_t)st.st_size);
    }
    close(fd);
    return hit;
}

#define SYM(lib, field, hipn, cun) \
    do { const char *nm_ = r->cuda ? (cun) : (hipn); \
         if (nm_ && !(*(void **)&r->field = dlsym(lib, nm_)) && !r->miss[0]) snprintf(r->miss, sizeof r->miss, "%s", nm_); } while (0)

/* dlopen + dlsym only; never starts a GPU runtime */
static int rt_bind_once(int cuda)
{
    rt_api *r = &g_rt[cuda];
    const char *env = getenv(cuda ? "SRS_CUDA_LIB" : "SRS_HIP_LIB");
    const char *const hip_rt[] = { "libamdhip64.so.7", "libamdhip64.so.6", "libamdhip64.so", "/opt/rocm/lib/libamdhip64.so", NULL };
    const char *const hip_rtc[] = { "libhiprtc.so.7", "libhiprtc.so.6", "libhiprtc.so", "/opt/rocm/lib/libhiprtc.so", NULL };
    const char *const hip_blas[] = { "libhipblas.so.3", "libhipblas.so.2", "libhipblas.so", "/opt/rocm/lib/libhipblas.so", NULL };
    const char *const cu_rt[] = { "libcuda.so.1", "libcuda.so", NULL };
    const char *const cu_rtc[] = { "libnvrtc.so.13", "libnvrtc.so.12", "libnvrtc.so.11.2", "libnvrtc.so", NULL };
    const char *const cu_blas[] = { "libcublas.so.13", "libcublas.so.12", "libcublas.so.11", "libcublas.so", NULL };
    const char *only[2] = { NULL, NULL }, *envc;
    if (r->tried) return r->ok;
    r->tried = 1;
    r->cuda = cuda;
    if (env && *env) { only[0] = env; r->lib = open_one(only, r->libpath, sizeof r->libpath); }
    else r->lib = open_one(cuda ? cu_rt : hip_rt, r->libpath, sizeof r->libpath);
    if (!r->lib) return 0;
    SYM(r->lib, Init, "hipInit", "cuInit");
    SYM(r->lib, DeviceGetCount, "hipGetDeviceCount", "cuDeviceGetCount");
    SYM(r->lib, DeviceGet, NULL, "cuDeviceGet");
    SYM(r->lib, DeviceGetName, "hipDeviceGetName", "cuDeviceGetName");
    SYM(r->lib, DeviceTotalMem, "hipDeviceTotalMem", "cuDeviceTotalMem_v2");
    SYM(r->lib, DeviceGetPCIBusId, "hipDeviceGetPCIBusId", "cuDeviceGetPCIBusId");
    SYM(r->lib, DriverGetVersion, "hipDriverGetVersion", "cuDriverGetVersion");
    SYM(r->lib, RuntimeGetVersion, "hipRuntimeGetVersion", NULL);
    SYM(r->lib, SetDevice, "hipSetDevice", NULL);
    SYM(r->lib, CtxCreate, NULL, "cuCtxCreate_v2");
    SYM(r->lib, CtxDestroy, NULL, "cuCtxDestroy_v2");
    SYM(r->lib, Malloc, "hipMalloc", "cuMemAlloc_v2");
    SYM(r->lib, Free, "hipFree", "cuMemFree_v2");
    SYM(r->lib, HtoD, "hipMemcpyHtoD", "cuMemcpyHtoD_v2");
    SYM(r->lib, DtoH, "hipMemcpyDtoH", "cuMemcpyDtoH_v2");
    SYM(r->lib, MemsetD32, "hipMemsetD32", "cuMemsetD32_v2");
    SYM(r->lib, ModuleLoadData, "hipModuleLoadData", "cuModuleLoadData");
    SYM(r->lib, ModuleUnload, "hipModuleUnload", "cuModuleUnload");
    SYM(r->lib, ModuleGetFunction, "hipModuleGetFunction", "cuModuleGetFunction");
    SYM(r->lib, Launch, "hipModuleLaunchKernel", "cuLaunchKernel");
    SYM(r->lib, EventCreate, "hipEventCreateWithFlags", "cuEventCreate");
    SYM(r->lib, EventRecord, "hipEventRecord", "cuEventRecord");
    SYM(r->lib, EventQuery, "hipEventQuery", "cuEventQuery");
    SYM(r->lib, EventElapsed, "hipEventElapsedTime", "cuEventElapsedTime");
    SYM(r->lib, EventDestroy, "hipEventDestroy", "cuEventDestroy_v2");
    SYM(r->lib, HipErrStr, "hipGetErrorString", NULL);
    SYM(r->lib, CuErrStr, NULL, "cuGetErrorString");
    if (r->miss[0]) return 0;
    if (cuda) {
        Dl_info di;
        if (dladdr((void *)r->Init, &di) && di.dli_fname) {
            char rp[PATH_MAX];
            const char *p = realpath(di.dli_fname, rp) ? rp : di.dli_fname;
            r->zluda = file_has(p, "ZLUDA_") || strstr(p, "zluda") != NULL;
        }
    }
    only[0] = NULL;
    /* tests: SRS_HIP_RTC / SRS_CUDA_RTC and SRS_HIP_BLAS / SRS_CUDA_BLAS open exactly that file instead of the compiler and the
       GEMM library (a name that does not exist = "not installed"), so a test never loads the real ones */
    envc = getenv(cuda ? "SRS_CUDA_RTC" : "SRS_HIP_RTC");
    if (envc && *envc) { only[0] = envc; r->rtc = open_one(only, NULL, 0); }
    else r->rtc = open_one(cuda ? cu_rtc : hip_rtc, NULL, 0);
    if (r->rtc) {
        char save[96];
        snprintf(save, sizeof save, "%.90s", r->miss);
        SYM(r->rtc, RtcCreate, "hiprtcCreateProgram", "nvrtcCreateProgram");
        SYM(r->rtc, RtcCompile, "hiprtcCompileProgram", "nvrtcCompileProgram");
        SYM(r->rtc, RtcSize, "hiprtcGetCodeSize", "nvrtcGetPTXSize");
        SYM(r->rtc, RtcCode, "hiprtcGetCode", "nvrtcGetPTX");
        SYM(r->rtc, RtcLogSize, "hiprtcGetProgramLogSize", "nvrtcGetProgramLogSize");
        SYM(r->rtc, RtcLog, "hiprtcGetProgramLog", "nvrtcGetProgramLog");
        SYM(r->rtc, RtcDestroy, "hiprtcDestroyProgram", "nvrtcDestroyProgram");
        r->have_rtc = !r->miss[0];
        snprintf(r->miss, sizeof r->miss, "%.90s", save);
    }
    envc = getenv(cuda ? "SRS_CUDA_BLAS" : "SRS_HIP_BLAS");
    if (envc && *envc) { only[0] = envc; r->blas = open_one(only, NULL, 0); }
    else r->blas = open_one(cuda ? cu_blas : hip_blas, NULL, 0);
    if (r->blas) {
        char save[96];
        snprintf(save, sizeof save, "%.90s", r->miss);
        SYM(r->blas, BlasCreate, "hipblasCreate", "cublasCreate_v2");
        SYM(r->blas, BlasDestroy, "hipblasDestroy", "cublasDestroy_v2");
        SYM(r->blas, Sgemm, "hipblasSgemm", "cublasSgemm_v2");
        SYM(r->blas, Dgemm, "hipblasDgemm", "cublasDgemm_v2");
        SYM(r->blas, Hgemm, "hipblasHgemm", "cublasHgemm");
        r->have_blas = !r->miss[0];
        snprintf(r->miss, sizeof r->miss, "%.90s", save);
    }
    r->ok = 1;
    return 1;
}

/* the GUI thread (available()) and a worker thread (open) may bind at the same time: one at a time */
static pthread_mutex_t g_bind_mu = PTHREAD_MUTEX_INITIALIZER;
static int rt_bind(int cuda)
{
    int ok;
    pthread_mutex_lock(&g_bind_mu);
    ok = rt_bind_once(cuda);
    pthread_mutex_unlock(&g_bind_mu);
    return ok;
}

int gb_rt_bind_check(int cuda, char *detail, size_t dn)
{
    rt_api *r = &g_rt[cuda ? 1 : 0];
    if (!rt_bind(cuda ? 1 : 0)) {
        if (r->lib) { snprintf(detail, dn, "%s loaded, but %s is missing", r->libpath, r->miss); return 1; }
        snprintf(detail, dn, "not installed");
        return -1;
    }
    snprintf(detail, dn, "%s: all runtime symbols found; %scompiler %s, GEMM library %s%s", r->libpath, cuda ? "own kernels built in (PTX), " : "",
             r->have_rtc ? "found" : "missing", r->have_blas ? "found" : "missing", r->zluda ? "; this is ZLUDA" : "");
    return 0;
}

/* the NVIDIA CUDA toolkit question (cuda_toolkit.c): is libcuda there, is it ZLUDA, is NVRTC there and which version.
   dlopen / dlsym and nvrtcVersion only (a plain function of the compiler library): never starts a GPU runtime.
   recheck = 1 looks for libnvrtc again (after an install) when it was missing. */
int gb_cuda_probe(gb_cuda_state *st, int recheck)
{
    rt_api *r = &g_rt[1];
    memset(st, 0, sizeof *st);
    pthread_mutex_lock(&g_bind_mu);
    rt_bind_once(1);
    if (recheck && r->ok && !r->have_rtc) {
        const char *const cu_rtc[] = { "libnvrtc.so.13", "libnvrtc.so.12", "libnvrtc.so.11.2", "libnvrtc.so", NULL };
        const char *envc = getenv("SRS_CUDA_RTC"), *only[2] = { envc, NULL };
        char save[96];
        if (r->rtc) { dlclose(r->rtc); r->rtc = NULL; }
        r->rtc = envc && *envc ? open_one(only, NULL, 0) : open_one(cu_rtc, NULL, 0);
        if (r->rtc) {
            snprintf(save, sizeof save, "%.90s", r->miss);
            r->miss[0] = 0;
            SYM(r->rtc, RtcCreate, "hiprtcCreateProgram", "nvrtcCreateProgram");
            SYM(r->rtc, RtcCompile, "hiprtcCompileProgram", "nvrtcCompileProgram");
            SYM(r->rtc, RtcSize, "hiprtcGetCodeSize", "nvrtcGetPTXSize");
            SYM(r->rtc, RtcCode, "hiprtcGetCode", "nvrtcGetPTX");
            SYM(r->rtc, RtcLogSize, "hiprtcGetProgramLogSize", "nvrtcGetProgramLogSize");
            SYM(r->rtc, RtcLog, "hiprtcGetProgramLog", "nvrtcGetProgramLog");
            SYM(r->rtc, RtcDestroy, "hiprtcDestroyProgram", "nvrtcDestroyProgram");
            r->have_rtc = !r->miss[0];
            snprintf(r->miss, sizeof r->miss, "%.90s", save);
        }
    }
    st->libcuda = r->lib != NULL;
    st->runtime_ok = r->ok;
    st->zluda = r->zluda;
    st->nvrtc = r->ok && r->have_rtc;
    snprintf(st->cuda_lib, sizeof st->cuda_lib, "%s", r->lib ? r->libpath : "");
    if (st->nvrtc) {
        int (*ver)(int *, int *) = (int (*)(int *, int *))dlsym(r->rtc, "nvrtcVersion");
        Dl_info di;
        if (ver && ver(&st->nvrtc_major, &st->nvrtc_minor)) st->nvrtc_major = st->nvrtc_minor = 0;
        if (dladdr((void *)r->RtcCreate, &di) && di.dli_fname) snprintf(st->nvrtc_lib, sizeof st->nvrtc_lib, "%s", di.dli_fname);
    }
    pthread_mutex_unlock(&g_bind_mu);
    return st->libcuda;
}

/* ---------------------------------------------------------------- listing */
static const char *err_text(rt_api *r, int e, char *b, size_t n)
{
    const char *s = NULL;
    if (r->cuda) { if (r->CuErrStr) r->CuErrStr(e, &s); } else if (r->HipErrStr) s = r->HipErrStr(e);
    snprintf(b, n, "%s (error %d)", s ? s : "GPU runtime error", e);
    return b;
}

static void version_text(int cuda, int v, char *b, size_t n)
{
    if (cuda) snprintf(b, n, "%d.%d", v / 1000, (v % 1000) / 10);
    else snprintf(b, n, "%d.%d.%d", v / 10000000, (v / 100000) % 100, v % 100000);
}

static int rt_available_x(int cuda, char *hint, size_t hn)
{
    rt_api *r = &g_rt[cuda];
    if (!rt_bind(cuda)) {
        if (hint) snprintf(hint, hn, "%s", r->lib ? "the runtime library is too old or damaged" : gb_hint[cuda ? GB_CUDA : GB_HIP]);
        return 0;
    }
    if (hint) hint[0] = 0;
    return 1;
}

static int rt_list_x(int cuda, gb_info *out, int max, char *why, size_t wn)
{
    rt_api *r = &g_rt[cuda];
    int n = 0, i, e, ver = 0, dver = 0;
    char eb[160], vr[48], vd[48];
    if (!rt_bind(cuda)) { snprintf(why, wn, "%s", gb_hint[cuda ? GB_CUDA : GB_HIP]); return -1; }
    if ((e = r->Init(0))) { snprintf(why, wn, "%s could not start: %s", cuda ? "CUDA" : "HIP", err_text(r, e, eb, sizeof eb)); return -1; }
    if ((e = r->DeviceGetCount(&n))) { snprintf(why, wn, "%s", err_text(r, e, eb, sizeof eb)); return -1; }
    if (n <= 0) { snprintf(why, wn, "no %s device found", cuda ? "CUDA" : "ROCm / HIP"); return 0; }
    if (r->DriverGetVersion) r->DriverGetVersion(&dver);
    if (r->RuntimeGetVersion) r->RuntimeGetVersion(&ver);
    version_text(cuda, dver, vd, sizeof vd);
    version_text(cuda, ver ? ver : dver, vr, sizeof vr);
    for (i = 0; i < n && i < max; i++) {
        gb_info *o = &out[i];
        size_t mem = 0;
        int dev = i;
        memset(o, 0, sizeof *o);
        o->backend = cuda ? GB_CUDA : GB_HIP;
        o->index = i;
        if (cuda && r->DeviceGet) r->DeviceGet(&dev, i);
        snprintf(o->label, sizeof o->label, "%s", cuda ? (r->zluda ? "CUDA (ZLUDA)" : "CUDA") : "ROCm / HIP");
        r->DeviceGetName(o->device, (int)sizeof o->device, dev);
        if (r->DeviceTotalMem && !r->DeviceTotalMem(&mem, dev)) o->mem_bytes = (double)mem;
        if (r->DeviceGetPCIBusId) {
            char b[32] = "";
            if (!r->DeviceGetPCIBusId(b, (int)sizeof b, dev)) { size_t k; for (k = 0; b[k]; k++) if (b[k] >= 'A' && b[k] <= 'F') b[k] += 32; snprintf(o->pci, sizeof o->pci, "%s", b); }
        }
        snprintf(o->kind, sizeof o->kind, "GPU");
        o->zluda = r->zluda;
        o->has_lib = r->have_blas;
        o->fp16 = o->fp64 = 1;
        snprintf(o->runtime, sizeof o->runtime, "%s %s (driver %s)%s%s", cuda ? "CUDA" : "HIP", vr, vd,
                 r->zluda ? ", translated to AMD by ZLUDA" : "",
                 cuda ? (r->have_rtc ? "; own kernels built in (PTX), NVRTC as fallback" : "; own kernels built in (PTX), no NVRTC")
                      : (r->have_rtc ? "" : "; no runtime compiler (libhiprtc), own kernels unavailable"));
    }
    return n < max ? n : max;
}

static int hip_available(char *h, size_t n) { return rt_available_x(0, h, n); }
static int cuda_available(char *h, size_t n) { return rt_available_x(1, h, n); }
static int hip_list(gb_info *o, int m, char *w, size_t n) { return rt_list_x(0, o, m, w, n); }
static int cuda_list(gb_info *o, int m, char *w, size_t n) { return rt_list_x(1, o, m, w, n); }

/* ---------------------------------------------------------------- context */
struct gb_ctx {
    rt_api *r;
    int dev, dead, kind, prec, variant;
    void *cuctx, *mod[2], *ev0, *ev1, *blas;
    void *fn_gemm[2], *fn_fma[2], *fn_copy, *fn_cmp, *fn_tick;
    void *a, *b, *c, *ref, *cnt;
    void *ha, *hb, *host;       /* host copies (matrices / transfer buffers) */
    size_t n, es, bytes, nthreads;
    long prep_wrong, ticks;
    unsigned seed;
    int verified, ready, gx;
    char err[240];
};

static void seterr(gb_ctx *c, int e, const char *what)
{
    char eb[160];
    snprintf(c->err, sizeof c->err, "%s: %s", what, err_text(c->r, e, eb, sizeof eb));
}

static int rt_open(rt_api *r, gb_ctx **cp, const gb_info *d, char *why, size_t wn)
{
    gb_ctx *c;
    int e, dev = d->index;
    char eb[160];
    if (!rt_bind(r->cuda)) { snprintf(why, wn, "%s", gb_hint[r->cuda ? GB_CUDA : GB_HIP]); return -1; }
    if ((e = r->Init(0))) { snprintf(why, wn, "%s", err_text(r, e, eb, sizeof eb)); return -1; }
    if (!(c = calloc(1, sizeof *c))) { snprintf(why, wn, "out of memory"); return -1; }
    c->r = r;
    c->dev = dev;
    if (r->cuda) {
        if ((e = r->DeviceGet(&dev, d->index)) || (e = r->CtxCreate(&c->cuctx, 0, dev))) { snprintf(why, wn, "%s", err_text(r, e, eb, sizeof eb)); free(c); return -1; }
    } else if ((e = r->SetDevice(dev))) { snprintf(why, wn, "%s", err_text(r, e, eb, sizeof eb)); free(c); return -1; }
    if ((e = r->EventCreate(&c->ev0, 0)) || (e = r->EventCreate(&c->ev1, 0))) { snprintf(why, wn, "%s", err_text(r, e, eb, sizeof eb)); free(c); return -1; }
    *cp = c;
    return 0;
}

/* software_only (tests): HIP and CUDA only drive real GPUs, so they are never started then */
static int hip_open(gb_ctx **cp, const gb_info *d, int so, char *w, size_t n)
{
    if (gb_software_only(so)) { snprintf(w, n, "test mode: ROCm / HIP drives a real GPU, refused"); return -1; }
    return rt_open(&g_rt[0], cp, d, w, n);
}
static int cuda_open(gb_ctx **cp, const gb_info *d, int so, char *w, size_t n)
{
    if (gb_software_only(so)) { snprintf(w, n, "test mode: CUDA drives a real GPU, refused"); return -1; }
    return rt_open(&g_rt[1], cp, d, w, n);
}

static void rt_release(gb_ctx *c)
{
    rt_api *r = c->r;
    if (!c->ready) return;
    if (!c->dead) {
        void **p[] = { &c->a, &c->b, &c->c, &c->ref, &c->cnt };
        size_t i;
        for (i = 0; i < sizeof p / sizeof *p; i++) if (*p[i]) r->Free(*p[i]);
        if (c->blas) r->BlasDestroy(c->blas);
    }
    c->a = c->b = c->c = c->ref = c->cnt = NULL;
    c->blas = NULL;
    free(c->ha); free(c->hb); free(c->host);
    c->ha = c->hb = c->host = NULL;
    c->ready = 0;
}

static void rt_close(gb_ctx *c)
{
    int i;
    if (!c) return;
    rt_release(c);
    if (!c->dead) {   /* a hung GPU may still be using these: leave them */
        for (i = 0; i < 2; i++) if (c->mod[i]) c->r->ModuleUnload(c->mod[i]);
        if (c->ev0) c->r->EventDestroy(c->ev0);
        if (c->ev1) c->r->EventDestroy(c->ev1);
        if (c->cuctx) c->r->CtxDestroy(c->cuctx);
        free(c);
    }
}

static const char *rt_error(gb_ctx *c) { return c->err; }

/* ---------------------------------------------------------------- own kernels */
static int build_module(gb_ctx *c, int fp64)
{
    rt_api *r = c->r;
    char *src = NULL, *code = NULL, *log = NULL, ptxerr[180] = "";
    void *prog = NULL, *mod = NULL, *f[5] = { NULL, NULL, NULL, NULL, NULL };
    size_t sz = 0, ls = 0;
    int e, rc = GBP_ERROR;
    if (c->mod[fp64]) return GBP_OK;
    if (r->cuda) {   /* CUDA: the built-in PTX first; NVRTC only when the driver refuses it */
        char eb[160];
        if (!(e = r->ModuleLoadData(&mod, fp64 ? gk_ptx_fp64 : gk_ptx_fp32))) goto loaded;
        mod = NULL;
        snprintf(ptxerr, sizeof ptxerr, "%.150s", err_text(r, e, eb, sizeof eb));
        if (!r->have_rtc) {
            snprintf(c->err, sizeof c->err, "the CUDA driver refused the built-in kernels (%.50s) and libnvrtc is not installed: "
                     "see Options > NVIDIA CUDA toolkit", ptxerr);
            return GBP_UNSUPPORTED;
        }
    } else if (!r->have_rtc) { snprintf(c->err, sizeof c->err, "the runtime compiler (libhiprtc) is not installed, so the own kernels cannot be built"); return GBP_UNSUPPORTED; }
    if (!(src = gk_source(GKD_CUDA_HIP, fp64))) { snprintf(c->err, sizeof c->err, "out of memory"); return GBP_ERROR; }
    if ((e = r->RtcCreate(&prog, src, "srs_kernels.cu", 0, NULL, NULL))) { seterr(c, e, "kernel compiler"); goto out; }
    if ((e = r->RtcCompile(prog, 0, NULL))) {
        r->RtcLogSize(prog, &ls);
        if (ls > 1 && (log = malloc(ls + 1)) && !r->RtcLog(prog, log)) { log[ls] = 0; snprintf(c->err, sizeof c->err, "the kernel did not compile: %.180s", log); }
        else seterr(c, e, "the kernel did not compile");
        goto out;
    }
    if ((e = r->RtcSize(prog, &sz)) || !(code = malloc(sz + 1)) || (e = r->RtcCode(prog, code))) { seterr(c, e, "kernel compiler"); goto out; }
    if ((e = r->ModuleLoadData(&mod, code))) {
        char what[220];
        snprintf(what, sizeof what, "%s%s%s", ptxerr[0] ? "loading the kernels (the built-in ones were refused too: " : "loading the kernels", ptxerr, ptxerr[0] ? ")" : "");
        seterr(c, e, what); mod = NULL; goto out;
    }
loaded:
    /* all five kernels or none: a half-loaded module must never be remembered as built (a NULL function crashes the driver) */
    if ((e = r->ModuleGetFunction(&f[0], mod, "gemm_r")) || (e = r->ModuleGetFunction(&f[1], mod, "fma_r")) ||
        (e = r->ModuleGetFunction(&f[2], mod, "copy_u4")) || (e = r->ModuleGetFunction(&f[3], mod, "cmp_u32")) ||
        (e = r->ModuleGetFunction(&f[4], mod, "tick"))) { seterr(c, e, "finding a kernel"); goto out; }
    if (!f[0] || !f[1] || !f[2] || !f[3] || !f[4]) { snprintf(c->err, sizeof c->err, "the GPU runtime returned no kernel (internal error)"); goto out; }
    c->mod[fp64] = mod; mod = NULL;
    c->fn_gemm[fp64] = f[0]; c->fn_fma[fp64] = f[1]; c->fn_copy = f[2]; c->fn_cmp = f[3]; c->fn_tick = f[4];
    rc = GBP_OK;
out:
    if (rc && ptxerr[0] && !strstr(c->err, "built-in")) {   /* say that the built-in kernels were tried first */
        size_t l = strlen(c->err);
        snprintf(c->err + l, sizeof c->err - l, " (after the built-in kernels were refused: %.60s)", ptxerr);
    }
    if (mod) r->ModuleUnload(mod);
    if (prog) r->RtcDestroy(&prog);
    free(src); free(code); free(log);
    return rc;
}

static int launch(gb_ctx *c, void *fn, unsigned gx, unsigned gy, unsigned bx, unsigned by, void **args)
{
    if (!fn) { snprintf(c->err, sizeof c->err, "a kernel was not loaded (internal error); the run was ended"); return GBP_ERROR; }
    int e = c->r->Launch(fn, gx, gy, 1, bx, by, 1, 0, NULL, args, NULL);
    if (e) { seterr(c, e, "starting a kernel"); return GBP_ERROR; }
    return GBP_OK;
}

/* wait for an event; the GPU gets WATCHDOG_S seconds, then the run ends and nothing is freed */
static int wait_ev(gb_ctx *c, void *ev)
{
    double t0 = now_s();
    for (;;) {
        int e = c->r->EventQuery(ev);
        struct timespec d = { 0, 100000 };
        if (!e) return GBP_OK;
        if (e != 600) { seterr(c, e, "the GPU"); return GBP_LOST; }
        if (now_s() - t0 > WATCHDOG_S) {
            c->dead = 1;
            snprintf(c->err, sizeof c->err, "the GPU did not finish its work within %.0f s; the run was ended and the GPU left alone", WATCHDOG_S);
            return GBP_HUNG;
        }
        nanosleep(&d, NULL);
    }
}

static int timed_end(gb_ctx *c, double *secs)
{
    float ms = 0;
    int e, rc;
    if ((e = c->r->EventRecord(c->ev1, NULL))) { seterr(c, e, "timing"); return GBP_ERROR; }
    if ((rc = wait_ev(c, c->ev1))) return rc;
    if ((e = c->r->EventElapsed(&ms, c->ev0, c->ev1))) { seterr(c, e, "timing"); return GBP_ERROR; }
    *secs = (double)ms / 1000.0;
    return GBP_OK;
}

static int timed_begin(gb_ctx *c)
{
    int e = c->r->EventRecord(c->ev0, NULL);
    if (e) { seterr(c, e, "timing"); return GBP_ERROR; }
    return GBP_OK;
}

static int dmalloc(gb_ctx *c, void **p, size_t n)
{
    int e = c->r->Malloc(p, n);
    if (e) { char b[64]; snprintf(b, sizeof b, "getting %.0f MB of video memory", (double)n / 1e6); seterr(c, e, b); return GBP_ERROR; }
    return GBP_OK;
}

static double half_to_d(uint16_t h)
{
    int e = (h >> 10) & 31, m = h & 1023;
    double v = e == 0 ? m / 1024.0 / 16384.0 : e == 31 ? 65504.0 * 2 : (1.0 + m / 1024.0) * (e >= 15 ? (double)(1u << (e - 15)) : 1.0 / (double)(1u << (15 - e)));
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

/* one GEMM of the prepared matrices into dst */
static int gemm_once(gb_ctx *c, void *dst)
{
    rt_api *r = c->r;
    int n = (int)c->n, e;
    if (c->variant == GV_LIB) {
        const float f1 = 1.0f, f0 = 0.0f;
        const double d1 = 1.0, d0 = 0.0;
        const uint16_t h1 = 0x3C00, h0 = 0;
        int op = r->cuda ? 0 : 111;   /* no transpose; row-major C = A * B is column-major C' = B' * A' */
        if (c->prec == GP_FP64) e = r->Dgemm(c->blas, op, op, n, n, n, &d1, c->b, n, c->a, n, &d0, dst, n);
        else if (c->prec == GP_FP16) e = r->Hgemm(c->blas, op, op, n, n, n, &h1, c->b, n, c->a, n, &h0, dst, n);
        else e = r->Sgemm(c->blas, op, op, n, n, n, &f1, c->b, n, c->a, n, &f0, dst, n);
        /* cuBLAS 15 / hipBLAS 7 = NOT_SUPPORTED (e.g. ZLUDA's cuBLAS has no FP64 GEMM): a missing feature, not a fault */
        if (e == (r->cuda ? 15 : 7)) {
            snprintf(c->err, sizeof c->err, "%s on this driver does not support %s matrix multiply", r->cuda ? (r->zluda ? "cuBLAS (ZLUDA)" : "cuBLAS") : "hipBLAS",
                     c->prec == GP_FP64 ? "FP64" : c->prec == GP_FP16 ? "FP16" : "FP32");
            return GBP_UNSUPPORTED;
        }
        if (e) { seterr(c, e, "the GEMM library"); return GBP_ERROR; }
        return GBP_OK;
    } else {
        void *args[4] = { &c->a, &c->b, &dst, &n };
        return launch(c, c->fn_gemm[c->prec == GP_FP64], (unsigned)(n / 16), (unsigned)(n / 16), 16, 16, args);
    }
}

static int cmp_once(gb_ctx *c, void *x, void *y, unsigned words)
{
    unsigned g = words / 256 + 1;
    void *args[4] = { &x, &y, &c->cnt, &words };
    if (g > 1024) g = 1024;
    return launch(c, c->fn_cmp, g, 1, 256, 1, args);
}

static int rt_prep(gb_ctx *c, int kind, int prec, int variant, size_t n, double *work, unsigned *maxreps)
{
    rt_api *r = c->r;
    int rc, e;
    rt_release(c);
    c->kind = kind; c->prec = prec; c->variant = variant; c->prep_wrong = 0; c->verified = 0; c->seed = 0; c->err[0] = 0;
    *work = 0; *maxreps = 1;
    if (c->dead) { snprintf(c->err, sizeof c->err, "the GPU stopped answering earlier in this run"); return GBP_LOST; }
    c->ready = 1;
    switch (kind) {
    case GK_GEMM: {
        size_t es = prec == GP_FP64 ? 8 : prec == GP_FP16 ? 2 : 4, bytes, k;
        int t;
        if (prec == GP_INT32) { rc = GBP_UNSUPPORTED; break; }
        if (variant == GV_OWN && prec == GP_FP16) { snprintf(c->err, sizeof c->err, "FP16 is measured through the GEMM library only"); rc = GBP_UNSUPPORTED; break; }
        if (variant == GV_LIB && !r->have_blas) { snprintf(c->err, sizeof c->err, "%s is not installed", r->cuda ? "cuBLAS" : "hipBLAS"); rc = GBP_UNSUPPORTED; break; }
        /* the error check (cmp_u32) is one of our own kernels, so the library variant needs the module too
           (CUDA has it built in; HIP has to compile it) */
        if (variant == GV_LIB && !r->cuda && !r->have_rtc) {
            snprintf(c->err, sizeof c->err, "%s needs the runtime compiler (%s) for the error check, and it is not installed", r->cuda ? "cuBLAS" : "hipBLAS", r->cuda ? "libnvrtc" : "libhiprtc");
            rc = GBP_UNSUPPORTED; break;
        }
        if ((rc = build_module(c, variant == GV_OWN && prec == GP_FP64))) break;
        n = n / 16 * 16;
        if (n < 16) n = 16;
        if (prec == GP_FP16 && n > 2048) n = 2048;
        bytes = n * n * es;
        c->n = n; c->es = es; c->bytes = bytes;
        if ((rc = dmalloc(c, &c->a, bytes)) || (rc = dmalloc(c, &c->b, bytes)) || (rc = dmalloc(c, &c->c, bytes)) ||
            (rc = dmalloc(c, &c->ref, bytes)) || (rc = dmalloc(c, &c->cnt, 64))) break;
        if (variant == GV_LIB && (e = r->BlasCreate(&c->blas))) { seterr(c, e, "starting the GEMM library"); rc = GBP_ERROR; break; }
        c->ha = malloc(bytes); c->hb = malloc(bytes); c->host = malloc(bytes);
        if (!c->ha || !c->hb || !c->host) { snprintf(c->err, sizeof c->err, "out of memory"); rc = GBP_ERROR; break; }
        fill_mat(c->ha, n, es, 0);
        fill_mat(c->hb, n, es, 1);
        if ((e = r->HtoD(c->a, c->ha, bytes)) || (e = r->HtoD(c->b, c->hb, bytes))) { seterr(c, e, "copying the matrices"); rc = GBP_ERROR; break; }
        if ((rc = gemm_once(c, c->ref)) || (rc = timed_begin(c))) break;
        {
            double s;
            if ((rc = timed_end(c, &s))) break;
        }
        if ((e = r->DtoH(c->host, c->ref, bytes))) { seterr(c, e, "reading the first result"); rc = GBP_ERROR; break; }
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
        if (variant == GV_LIB) { snprintf(c->err, sizeof c->err, "no library variant for plain multiply-add"); rc = GBP_UNSUPPORTED; break; }
        if ((rc = build_module(c, fp64))) break;
        c->gx = (int)(n ? n : 1024);
        c->nthreads = (size_t)c->gx * 64;
        if ((rc = dmalloc(c, &c->a, c->nthreads * (fp64 ? 8 : 4)))) break;
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
        if ((rc = build_module(c, 0))) break;
        c->bytes = bytes;
        if ((rc = dmalloc(c, &c->a, bytes)) || (rc = dmalloc(c, &c->b, bytes)) || (rc = dmalloc(c, &c->cnt, 64))) break;
        c->host = malloc(bytes);
        if (!c->host) { snprintf(c->err, sizeof c->err, "out of memory"); rc = GBP_ERROR; break; }
        w = c->host;
        for (i = 0; i < bytes / 4; i++) w[i] = (uint32_t)i * 2654435761u + MEM_SEED;
        if ((e = r->HtoD(c->a, c->host, bytes))) { seterr(c, e, "filling video memory"); rc = GBP_ERROR; break; }
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
        if ((rc = dmalloc(c, &c->a, bytes))) break;
        c->host = malloc(bytes);
        c->ha = malloc(bytes);
        if (!c->host || !c->ha) { snprintf(c->err, sizeof c->err, "out of memory"); rc = GBP_ERROR; break; }
        w = c->host;
        for (i = 0; i < bytes / 4; i++) w[i] = (uint32_t)i * 2654435761u + MEM_SEED;
        if (kind == GK_DOWNLOAD && (e = r->HtoD(c->a, c->host, bytes))) { seterr(c, e, "filling video memory"); rc = GBP_ERROR; break; }
        *work = (double)bytes;
        *maxreps = 256;
        rc = GBP_OK;
        break;
    }
    case GK_LATENCY: {
        if ((rc = build_module(c, 0)) || (rc = dmalloc(c, &c->a, 64))) break;
        if ((e = r->MemsetD32(c->a, 0, 16))) { seterr(c, e, "preparing the counter"); rc = GBP_ERROR; break; }
        c->ticks = 0;
        *maxreps = 20000;
        rc = GBP_OK;
        break;
    }
    default: rc = GBP_UNSUPPORTED;
    }
    if (rc) { if (c->dead) c->ready = 0; else rt_release(c); }
    return rc;
}

static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

static int rt_run(gb_ctx *c, unsigned reps, double *secs, long *wrong)
{
    rt_api *r = c->r;
    unsigned i;
    int rc, e;
    *wrong = 0;
    if (!c->ready) return GBP_ERROR;
    if (c->dead) return GBP_LOST;
    switch (c->kind) {
    case GK_GEMM: {
        unsigned words = (unsigned)(c->bytes / 4);
        long cnt = 0;
        if ((e = r->MemsetD32(c->cnt, 0, 16))) { seterr(c, e, "clearing the error counter"); return GBP_ERROR; }
        if ((rc = timed_begin(c))) return rc;
        for (i = 0; i < reps; i++) if ((rc = gemm_once(c, c->c)) || (rc = cmp_once(c, c->c, c->ref, words))) return rc;
        if ((rc = timed_end(c, secs))) return rc;
        {
            uint32_t v = 0;
            if ((e = r->DtoH(&v, c->cnt, 4))) { seterr(c, e, "reading the error counter"); return GBP_ERROR; }
            cnt = v;
        }
        *wrong = cnt + c->prep_wrong;
        c->prep_wrong = 0;
        return GBP_OK;
    }
    case GK_FMA: {
        int fp64 = c->prec == GP_FP64;
        unsigned seed = ++c->seed, nn = (unsigned)c->nthreads, g, k;
        float mf[4] = { 2.0f, 1.0f, 0.5f, -0.5f };
        double md[4] = { 2.0, 1.0, 0.5, -0.5 };
        void *args[8] = { &c->a, fp64 ? (void *)&md[0] : (void *)&mf[0], fp64 ? (void *)&md[1] : (void *)&mf[1], fp64 ? (void *)&md[2] : (void *)&mf[2],
                          fp64 ? (void *)&md[3] : (void *)&mf[3], &reps, &seed, &nn };
        if ((rc = timed_begin(c)) || (rc = launch(c, c->fn_fma[fp64], (unsigned)c->gx, 1, 64, 1, args)) || (rc = timed_end(c, secs))) return rc;
        if ((e = r->DtoH(c->host, c->a, c->nthreads * (fp64 ? 8 : 4)))) { seterr(c, e, "reading the results"); return GBP_ERROR; }
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
        unsigned n4 = (unsigned)(c->bytes / 16), words = (unsigned)(c->bytes / 4), g = n4 / 256 + 1;
        void *args[3] = { &c->a, &c->b, &n4 };
        uint32_t v = 0;
        if (g > 4096) g = 4096;
        if ((e = r->MemsetD32(c->cnt, 0, 16))) { seterr(c, e, "clearing the error counter"); return GBP_ERROR; }
        if ((rc = timed_begin(c))) return rc;
        for (i = 0; i < reps; i++) if ((rc = launch(c, c->fn_copy, g, 1, 256, 1, args))) return rc;
        if ((rc = cmp_once(c, c->a, c->b, words)) || (rc = timed_end(c, secs))) return rc;
        if ((e = r->DtoH(&v, c->cnt, 4))) { seterr(c, e, "reading the error counter"); return GBP_ERROR; }
        *wrong = v;
        return GBP_OK;
    }
    case GK_UPLOAD: case GK_DOWNLOAD: {
        double t0 = now_s();
        for (i = 0; i < reps; i++) {
            e = c->kind == GK_UPLOAD ? r->HtoD(c->a, c->host, c->bytes) : r->DtoH(c->ha, c->a, c->bytes);
            if (e) { seterr(c, e, "copying"); return GBP_ERROR; }
        }
        *secs = now_s() - t0;
        if (!c->verified) {
            c->verified = 1;
            if (c->kind == GK_UPLOAD) { if ((e = r->DtoH(c->ha, c->a, c->bytes))) { seterr(c, e, "checking"); return GBP_ERROR; } }
            if (memcmp(c->ha, c->host, c->bytes)) *wrong = 1;
        }
        return GBP_OK;
    }
    case GK_LATENCY: {
        double *lat = malloc((reps ? reps : 1) * sizeof *lat);
        void *args[1] = { &c->a };
        uint32_t v = 0;
        if (!lat) { snprintf(c->err, sizeof c->err, "out of memory"); return GBP_ERROR; }
        for (i = 0; i < reps; i++) {
            double t0 = now_s();
            if ((rc = timed_begin(c)) || (rc = launch(c, c->fn_tick, 1, 1, 1, 1, args)) || (rc = timed_end(c, secs))) { free(lat); return rc; }
            lat[i] = now_s() - t0;
            c->ticks++;
        }
        if (reps) { qsort(lat, reps, sizeof *lat, cmp_d); *secs = lat[reps / 2]; }
        free(lat);
        if ((e = r->DtoH(&v, c->a, 4))) { seterr(c, e, "reading the counter"); return GBP_ERROR; }
        if (v != (uint32_t)c->ticks) *wrong = 1;
        return GBP_OK;
    }
    }
    return GBP_ERROR;
}

const gb_ops gb_hip = { GB_HIP, hip_available, hip_list, hip_open, rt_close, rt_prep, rt_run, rt_release, rt_error };
const gb_ops gb_cuda = { GB_CUDA, cuda_available, cuda_list, cuda_open, rt_close, rt_prep, rt_run, rt_release, rt_error };

#else   /* not 64-bit Linux */
int gb_rt_bind_check(int cuda, char *d, size_t n) { (void)cuda; snprintf(d, n, "64-bit Linux only"); return -1; }
int gb_cuda_probe(gb_cuda_state *st, int recheck) { (void)recheck; memset(st, 0, sizeof *st); return 0; }
static int na_av(char *h, size_t n) { if (h) snprintf(h, n, "64-bit Linux only"); return 0; }
static int na_list(gb_info *o, int m, char *w, size_t n) { (void)o; (void)m; if (w) snprintf(w, n, "64-bit Linux only"); return -1; }
const gb_ops gb_hip = { GB_HIP, na_av, na_list, NULL, NULL, NULL, NULL, NULL, NULL };
const gb_ops gb_cuda = { GB_CUDA, na_av, na_list, NULL, NULL, NULL, NULL, NULL, NULL };
#endif
