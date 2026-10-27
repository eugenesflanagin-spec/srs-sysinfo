/* tests/gpu_stub.c - a FAKE GPU runtime for tests/gpu-stub-test.sh. It stands in for libcuda / libamdhip64, libnvrtc /
 * libhiprtc and libcublas / libhipblas all at once (one file, every symbol), so the HIP and CUDA back ends of GPU Burnin and
 * the GPU Compute comparison (gpuburn_rt.c) can be driven end to end WITHOUT a GPU, a driver or a compiler.
 *
 * "Device memory" is ordinary host memory. The kernels the back end asks for (gemm_r, fma_r, copy_u4, cmp_u32, tick) are
 * carried out on the host, so every result checks out and the run ends with exit 0. What matters for the test:
 *   - a kernel launch with a NULL function handle prints a message and ABORTS (what a real driver does: SIGSEGV),
 *   - every launch and library call is counted; the totals go to the file named by STUB_LOG when the program ends.
 * Time is fake and constant (STUB_MS, default 30 ms per timed piece), so the run lengths do not depend on the PC.
 *
 * Switches (environment): STUB_RTC_FAIL=1   the "compiler" rejects the kernels (the own kernels cannot be built)
 *                         STUB_NO_FN=name   cuModuleGetFunction says OK but returns a NULL function for that kernel
 *                         STUB_MS=n         fake milliseconds per timed piece
 *                         STUB_LOG=file     write the launch counts here at exit
 * Built by the test as a shared object; nothing here is part of SRS itself. Written for SRS. */
#define _GNU_SOURCE
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPORT __attribute__((visibility("default")))

typedef struct { const char *name; int fp64; } stub_fn;
typedef struct { int fp64; } stub_mod;
typedef struct { int fp64; } stub_prog;

static long n_launch[8], n_blas, n_null;
static const char *const kname[5] = { "gemm_r", "fma_r", "copy_u4", "cmp_u32", "tick" };
static stub_fn g_fn[2][5];

static double fake_ms(void)
{
    const char *e = getenv("STUB_MS");
    return e && *e ? atof(e) : 30.0;
}

__attribute__((destructor)) static void stub_report(void)
{
    const char *f = getenv("STUB_LOG");
    FILE *o;
    int i;
    if (!f || !*f || !(o = fopen(f, "a"))) return;
    for (i = 0; i < 5; i++) fprintf(o, "launch %s %ld\n", kname[i], n_launch[i]);
    fprintf(o, "blas %ld\n", n_blas);
    fclose(o);
}

/* ---------------------------------------------------------------- half precision (whole numbers only) */
static float h2f(uint16_t h)
{
    int e = (h >> 10) & 31, m = h & 1023;
    float v = e == 0 ? m / 1024.0f / 16384.0f : ldexpf(1.0f + m / 1024.0f, e - 15);
    return (h & 0x8000) ? -v : v;
}

static uint16_t f2h(float f)
{
    int e;
    float m;
    if (f == 0) return 0;
    m = frexpf(f < 0 ? -f : f, &e);   /* f = m * 2^e, m in [0.5, 1) */
    return (uint16_t)((f < 0 ? 0x8000 : 0) | ((e - 1 + 15) << 10) | ((int)((m * 2 - 1) * 1024.0f + 0.5f) & 1023));
}

/* ---------------------------------------------------------------- matrix product, remembered per element size */
static float *rd_f(const void *p, size_t n2, int es)
{
    float *f = malloc(n2 * sizeof *f);
    size_t i;
    for (i = 0; i < n2; i++) f[i] = es == 8 ? (float)((const double *)p)[i] : es == 2 ? h2f(((const uint16_t *)p)[i]) : ((const float *)p)[i];
    return f;
}

static void wr_f(void *p, const float *f, size_t n2, int es)
{
    size_t i;
    for (i = 0; i < n2; i++) {
        if (es == 8) ((double *)p)[i] = f[i];
        else if (es == 2) ((uint16_t *)p)[i] = f2h(f[i]);
        else ((float *)p)[i] = f[i];
    }
}

/* dst(row-major n x n) = a * b for 0/1-ish whole numbers. The same inputs come back all the time, so the last product
   of each element size is remembered and copied: the back end launches thousands of identical multiplications. */
static struct { int n; void *a, *b, *res; size_t bytes; } g_memo[9];

static void mat_mul(const void *a, const void *b, void *dst, int n, int es)
{
    size_t n2 = (size_t)n * n, bytes = n2 * es;
    int slot = es;   /* 2, 4 or 8 */
    if (g_memo[slot].n == n && !memcmp(g_memo[slot].a, a, bytes) && !memcmp(g_memo[slot].b, b, bytes)) { memcpy(dst, g_memo[slot].res, bytes); return; }
    {
        float *fa = rd_f(a, n2, es), *fb = rd_f(b, n2, es), *fc = calloc(n2, sizeof *fc);
        int i, k, j;
        for (i = 0; i < n; i++)
            for (k = 0; k < n; k++) {
                float x = fa[(size_t)i * n + k];
                const float *rb = fb + (size_t)k * n;
                float *rc = fc + (size_t)i * n;
                if (x != 0) for (j = 0; j < n; j++) rc[j] += x * rb[j];
            }
        wr_f(dst, fc, n2, es);
        free(g_memo[slot].a); free(g_memo[slot].b); free(g_memo[slot].res);
        g_memo[slot].n = n; g_memo[slot].bytes = bytes;
        g_memo[slot].a = malloc(bytes); memcpy(g_memo[slot].a, a, bytes);
        g_memo[slot].b = malloc(bytes); memcpy(g_memo[slot].b, b, bytes);
        g_memo[slot].res = malloc(bytes); memcpy(g_memo[slot].res, dst, bytes);
        free(fa); free(fb); free(fc);
    }
}

/* ---------------------------------------------------------------- the runtime (CUDA driver + HIP) */
EXPORT int cuInit(unsigned f) { (void)f; return 0; }
EXPORT int hipInit(unsigned f) { (void)f; return 0; }
EXPORT int cuDeviceGetCount(int *n) { *n = 1; return 0; }
EXPORT int hipGetDeviceCount(int *n) { *n = 1; return 0; }
EXPORT int cuDeviceGet(int *d, int i) { *d = i; return 0; }
EXPORT int cuDeviceGetName(char *b, int n, int d) { (void)d; snprintf(b, (size_t)n, "SRS test stub GPU"); return 0; }
EXPORT int hipDeviceGetName(char *b, int n, int d) { (void)d; snprintf(b, (size_t)n, "SRS test stub GPU"); return 0; }
EXPORT int cuDeviceTotalMem_v2(size_t *m, int d) { (void)d; *m = (size_t)512 << 20; return 0; }
EXPORT int hipDeviceTotalMem(size_t *m, int d) { (void)d; *m = (size_t)512 << 20; return 0; }
EXPORT int cuDeviceGetPCIBusId(char *b, int n, int d) { (void)d; snprintf(b, (size_t)n, "ffff:ff:1f.7"); return 0; }
EXPORT int hipDeviceGetPCIBusId(char *b, int n, int d) { (void)d; snprintf(b, (size_t)n, "ffff:ff:1f.7"); return 0; }
EXPORT int cuDriverGetVersion(int *v) { *v = 12040; return 0; }
EXPORT int hipDriverGetVersion(int *v) { *v = 60300000; return 0; }
EXPORT int hipRuntimeGetVersion(int *v) { *v = 60300000; return 0; }
EXPORT int hipSetDevice(int d) { (void)d; return 0; }
EXPORT int cuCtxCreate_v2(void **c, unsigned f, int d) { (void)f; (void)d; *c = malloc(8); return 0; }
EXPORT int cuCtxDestroy_v2(void *c) { free(c); return 0; }

static int dmalloc(void **p, size_t n) { *p = calloc(1, n ? n : 1); return *p ? 0 : 2; }
EXPORT int cuMemAlloc_v2(void **p, size_t n) { return dmalloc(p, n); }
EXPORT int hipMalloc(void **p, size_t n) { return dmalloc(p, n); }
EXPORT int cuMemFree_v2(void *p) { free(p); return 0; }
EXPORT int hipFree(void *p) { free(p); return 0; }
EXPORT int cuMemcpyHtoD_v2(void *d, const void *s, size_t n) { memcpy(d, s, n); return 0; }
EXPORT int hipMemcpyHtoD(void *d, const void *s, size_t n) { memcpy(d, s, n); return 0; }
EXPORT int cuMemcpyDtoH_v2(void *d, void *s, size_t n) { memcpy(d, s, n); return 0; }
EXPORT int hipMemcpyDtoH(void *d, void *s, size_t n) { memcpy(d, s, n); return 0; }
static int memset32(void *p, unsigned v, size_t n) { size_t i; for (i = 0; i < n; i++) ((unsigned *)p)[i] = v; return 0; }
EXPORT int cuMemsetD32_v2(void *p, unsigned v, size_t n) { return memset32(p, v, n); }
EXPORT int hipMemsetD32(void *p, unsigned v, size_t n) { return memset32(p, v, n); }

EXPORT int cuModuleLoadData(void **m, const void *code)
{
    stub_mod *s = malloc(sizeof *s);
    s->fp64 = strstr((const char *)code, "FP64") != NULL;
    *m = s;
    return 0;
}
EXPORT int hipModuleLoadData(void **m, const void *code) { return cuModuleLoadData(m, code); }
EXPORT int cuModuleUnload(void *m) { free(m); return 0; }
EXPORT int hipModuleUnload(void *m) { return cuModuleUnload(m); }

EXPORT int cuModuleGetFunction(void **f, void *m, const char *name)
{
    int fp = ((stub_mod *)m)->fp64, i;
    const char *no = getenv("STUB_NO_FN");
    for (i = 0; i < 5; i++)
        if (!strcmp(name, kname[i])) {
            g_fn[fp][i].name = kname[i];
            g_fn[fp][i].fp64 = fp;
            *f = no && !strcmp(no, name) ? NULL : &g_fn[fp][i];
            return 0;
        }
    return 500;   /* not found */
}
EXPORT int hipModuleGetFunction(void **f, void *m, const char *name) { return cuModuleGetFunction(f, m, name); }

static int stub_launch(void *fn, unsigned gx, unsigned gy, void **a)
{
    const stub_fn *f = fn;
    int i;
    (void)gx; (void)gy;
    if (!f) {
        n_null++;
        fprintf(stderr, "gpu_stub: LAUNCH WITH A NULL FUNCTION HANDLE (grid %ux%u) - a real driver crashes here\n", gx, gy);
        fflush(stderr);
        abort();
    }
    for (i = 0; i < 5; i++) if (f->name == kname[i]) n_launch[i]++;
    if (!strcmp(f->name, "gemm_r")) {
        mat_mul(*(void **)a[0], *(void **)a[1], *(void **)a[2], *(int *)a[3], f->fp64 ? 8 : 4);
    } else if (!strcmp(f->name, "fma_r")) {
        static const unsigned km[8] = { 1, 3, 5, 7, 11, 13, 17, 19 };
        unsigned seed = *(unsigned *)a[6], nn = *(unsigned *)a[7], g, k;
        void *o = *(void **)a[0];
        for (g = 0; g < nn; g++) {
            unsigned sum = 0;
            for (k = 0; k < 8; k++) sum += (k ? g * km[k] + k : g + seed) & 255u;
            if (f->fp64) ((double *)o)[g] = sum; else ((float *)o)[g] = (float)sum;
        }
    } else if (!strcmp(f->name, "copy_u4")) {
        memcpy(*(void **)a[1], *(void **)a[0], (size_t)*(unsigned *)a[2] * 16);
    } else if (!strcmp(f->name, "tick")) {
        (*(unsigned **)a[0])[0] += 1;
    }   /* cmp_u32: the data is equal, the error counter stays 0 */
    return 0;
}
EXPORT int cuLaunchKernel(void *f, unsigned gx, unsigned gy, unsigned gz, unsigned bx, unsigned by, unsigned bz, unsigned sh, void *st, void **args, void **extra)
{
    (void)gz; (void)bx; (void)by; (void)bz; (void)sh; (void)st; (void)extra;
    return stub_launch(f, gx, gy, args);
}
EXPORT int hipModuleLaunchKernel(void *f, unsigned gx, unsigned gy, unsigned gz, unsigned bx, unsigned by, unsigned bz, unsigned sh, void *st, void **args, void **extra)
{
    return cuLaunchKernel(f, gx, gy, gz, bx, by, bz, sh, st, args, extra);
}

EXPORT int cuEventCreate(void **e, unsigned f) { (void)f; *e = malloc(8); return 0; }
EXPORT int hipEventCreateWithFlags(void **e, unsigned f) { return cuEventCreate(e, f); }
EXPORT int cuEventRecord(void *e, void *s) { (void)e; (void)s; return 0; }
EXPORT int hipEventRecord(void *e, void *s) { (void)e; (void)s; return 0; }
EXPORT int cuEventQuery(void *e) { (void)e; return 0; }
EXPORT int hipEventQuery(void *e) { (void)e; return 0; }
EXPORT int cuEventElapsedTime(float *ms, void *a, void *b) { (void)a; (void)b; *ms = (float)fake_ms(); return 0; }
EXPORT int hipEventElapsedTime(float *ms, void *a, void *b) { return cuEventElapsedTime(ms, a, b); }
EXPORT int cuEventDestroy_v2(void *e) { free(e); return 0; }
EXPORT int hipEventDestroy(void *e) { free(e); return 0; }
EXPORT const char *hipGetErrorString(int e) { (void)e; return "stub error"; }
EXPORT int cuGetErrorString(int e, const char **s) { (void)e; *s = "stub error"; return 0; }

/* ---------------------------------------------------------------- the runtime compiler (NVRTC / hipRTC) */
static int rtc_create(void **p, const char *src)
{
    stub_prog *s = malloc(sizeof *s);
    s->fp64 = strstr(src, "#define REAL double") != NULL;
    *p = s;
    return 0;
}
static int rtc_compile(void) { return getenv("STUB_RTC_FAIL") ? 6 : 0; }
static int rtc_size(void *p, size_t *n) { (void)p; *n = 16; return 0; }
static int rtc_code(void *p, char *b) { strcpy(b, ((stub_prog *)p)->fp64 ? "STUBCODE FP64" : "STUBCODE FP32"); return 0; }
static int rtc_logsize(void *p, size_t *n) { (void)p; *n = 40; return 0; }
static int rtc_log(void *p, char *b) { (void)p; strcpy(b, "stub: the test compiler said no"); return 0; }
static int rtc_destroy(void **p) { free(*p); *p = NULL; return 0; }
EXPORT int nvrtcCreateProgram(void **p, const char *src, const char *n, int nh, const char **h, const char **i) { (void)n; (void)nh; (void)h; (void)i; return rtc_create(p, src); }
EXPORT int hiprtcCreateProgram(void **p, const char *src, const char *n, int nh, const char **h, const char **i) { (void)n; (void)nh; (void)h; (void)i; return rtc_create(p, src); }
EXPORT int nvrtcCompileProgram(void *p, int n, const char **o) { (void)p; (void)n; (void)o; return rtc_compile(); }
EXPORT int hiprtcCompileProgram(void *p, int n, const char **o) { (void)p; (void)n; (void)o; return rtc_compile(); }
EXPORT int nvrtcGetPTXSize(void *p, size_t *n) { return rtc_size(p, n); }
EXPORT int hiprtcGetCodeSize(void *p, size_t *n) { return rtc_size(p, n); }
EXPORT int nvrtcGetPTX(void *p, char *b) { return rtc_code(p, b); }
EXPORT int hiprtcGetCode(void *p, char *b) { return rtc_code(p, b); }
EXPORT int nvrtcGetProgramLogSize(void *p, size_t *n) { return rtc_logsize(p, n); }
EXPORT int hiprtcGetProgramLogSize(void *p, size_t *n) { return rtc_logsize(p, n); }
EXPORT int nvrtcGetProgramLog(void *p, char *b) { return rtc_log(p, b); }
EXPORT int hiprtcGetProgramLog(void *p, char *b) { return rtc_log(p, b); }
EXPORT int nvrtcDestroyProgram(void **p) { return rtc_destroy(p); }
EXPORT int hiprtcDestroyProgram(void **p) { return rtc_destroy(p); }

/* ---------------------------------------------------------------- the GEMM library (cuBLAS / hipBLAS) */
static int blas_create(void **h) { *h = malloc(8); return 0; }
static int blas_destroy(void *h) { free(h); return 0; }
/* column-major C = F * S (F = first matrix argument, S = second) on buffers that hold row-major matrices: the row-major
   result in memory is S * F, which is how the back end gets C = A * B by passing B first */
static int blas_gemm(int n, const void *f, const void *s, void *c, int es) { n_blas++; mat_mul(s, f, c, n, es); return 0; }
EXPORT int cublasCreate_v2(void **h) { return blas_create(h); }
EXPORT int hipblasCreate(void **h) { return blas_create(h); }
EXPORT int cublasDestroy_v2(void *h) { return blas_destroy(h); }
EXPORT int hipblasDestroy(void *h) { return blas_destroy(h); }
#define GEMM_SIG(name, T) \
    EXPORT int name(void *h, int oa, int ob, int m, int n, int k, const T *al, const void *a, int lda, const void *b, int ldb, const T *be, void *c, int ldc)
#define GEMM_BODY(es) { (void)h; (void)oa; (void)ob; (void)m; (void)k; (void)al; (void)lda; (void)ldb; (void)be; (void)ldc; return blas_gemm(n, a, b, c, es); }
GEMM_SIG(cublasSgemm_v2, float) GEMM_BODY(4)
GEMM_SIG(hipblasSgemm, float) GEMM_BODY(4)
GEMM_SIG(cublasDgemm_v2, double) GEMM_BODY(8)
GEMM_SIG(hipblasDgemm, double) GEMM_BODY(8)
GEMM_SIG(cublasHgemm, uint16_t) GEMM_BODY(2)
GEMM_SIG(hipblasHgemm, uint16_t) GEMM_BODY(2)
