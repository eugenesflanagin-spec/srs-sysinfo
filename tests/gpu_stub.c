/* tests/gpu_stub.c - a FAKE GPU runtime for tests/gpu-stub-test.sh. It stands in for libcuda / libamdhip64, libnvrtc /
 * libhiprtc, libcublas / libhipblas, libOpenCL and libclblast all at once (one file, every symbol), so the HIP, CUDA and
 * OpenCL back ends of GPU Burnin and the GPU Compute comparison (gpuburn_rt.c, gpuburn_cl.c) can be driven end to end
 * WITHOUT a GPU, a driver or a compiler.
 *
 * "Device memory" is ordinary host memory. The kernels the back end asks for (gemm_r, fma_r, copy_u4, cmp_u32, tick) are
 * carried out on the host, so every result checks out and the run ends with exit 0. What matters for the test:
 *   - a kernel launch with a NULL function handle prints a message and ABORTS (what a real driver does: SIGSEGV),
 *   - every launch and library call is counted; the totals go to the file named by STUB_LOG when the program ends.
 * Time is fake and constant (STUB_MS, default 30 ms per timed piece), so the run lengths do not depend on the PC.
 *
 * cuModuleLoadData also takes PTX text (SRS's built-in kernels): it checks that all five .entry names are there, counts it
 * as "ptxload" and remembers whether it was the FP64 module.
 *
 * Switches (environment): STUB_RTC_FAIL=1   the "compiler" rejects the kernels (the own kernels cannot be built)
 *                         STUB_PTX_FAIL=1   cuModuleLoadData refuses PTX text (error 218, CUDA_ERROR_INVALID_PTX)
 *                         STUB_NO_FN=name   cuModuleGetFunction says OK but returns a NULL function for that kernel
 *                         STUB_CL_FP64=1    the fake OpenCL device has FP64 (default: it has none, like Rusticl often)
 *                         STUB_CL_BUILD_FAIL=1  clBuildProgram fails (error -11) and gives an EMPTY build log
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

static long n_launch[8], n_blas, n_null, n_ptx, n_rtc, n_clbuild[2];
/* gemm_b: GPU Burnin's register-blocked HIP kernel (in the hipRTC source only, not in the PTX or OpenCL text) */
static const char *const kname[6] = { "gemm_r", "fma_r", "copy_u4", "cmp_u32", "tick", "gemm_b" };
static stub_fn g_fn[2][6];

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
    for (i = 0; i < 6; i++) fprintf(o, "launch %s %ld\n", kname[i], n_launch[i]);
    fprintf(o, "blas %ld\n", n_blas);
    fprintf(o, "ptxload %ld\nrtccompile %ld\nclbuild32 %ld\nclbuild64 %ld\n", n_ptx, n_rtc, n_clbuild[0], n_clbuild[1]);
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
    const char *t = code;
    stub_mod *s;
    if (strstr(t, ".entry")) {   /* PTX text */
        static const char *const want[5] = { ".entry gemm_r(", ".entry fma_r(", ".entry copy_u4(", ".entry cmp_u32(", ".entry tick(" };
        int i;
        if (getenv("STUB_PTX_FAIL")) return 218;
        if (strncmp(t, "//", 2) || !strstr(t, "\n.version ") || !strstr(t, "\n.target ") || !strstr(t, "\n.address_size 64")) return 218;
        for (i = 0; i < 5; i++) if (!strstr(t, want[i])) return 218;
        n_ptx++;
    }
    s = malloc(sizeof *s);
    s->fp64 = strstr(t, "FP64") != NULL;
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
    for (i = 0; i < 6; i++)
        if (!strcmp(name, kname[i])) {
            g_fn[fp][i].name = kname[i];
            g_fn[fp][i].fp64 = fp;
            *f = no && !strcmp(no, name) ? NULL : &g_fn[fp][i];
            return 0;
        }
    return 500;   /* not found */
}
EXPORT int hipModuleGetFunction(void **f, void *m, const char *name) { return cuModuleGetFunction(f, m, name); }

/* gemm_b carried out the way the GPU does it: every 16 x 16 block, both __syncthreads() phases, every thread's 4 x 4
   accumulators and the same shared-memory index arithmetic as gk_body_hip_fast in gpuburn_rt.c (keep the two in step),
   so a wrong index there gives wrong results here. n must be a multiple of 64 (anything else = the back end's fault). */
static void gemm_b_exec(const void *A, const void *B, void *C, int n, int fp64)
{
    static double sa[16][65], sb[16][64], acc[256][4][4];
    int bx, by, t, k, i, j, l, tid;
    if (n <= 0 || n % 64) { fprintf(stderr, "gpu_stub: gemm_b with n = %d (not a multiple of 64)\n", n); abort(); }
#define LD(M, x) (fp64 ? ((const double *)(M))[x] : (double)((const float *)(M))[x])
    for (by = 0; by < n / 64; by++)
        for (bx = 0; bx < n / 64; bx++) {
            int row0 = by * 64, col0 = bx * 64;
            memset(acc, 0, sizeof acc);
            for (t = 0; t < n; t += 16) {
                for (tid = 0; tid < 256; tid++)
                    for (l = 0; l < 4; l++) {
                        int x = tid + 256 * l;
                        sa[x & 15][x >> 4] = LD(A, (long)(row0 + (x >> 4)) * n + t + (x & 15));
                        sb[x >> 6][x & 63] = LD(B, (long)(t + (x >> 6)) * n + col0 + (x & 63));
                    }
                for (tid = 0; tid < 256; tid++) {
                    int tx = tid % 16, ty = tid / 16;
                    for (k = 0; k < 16; k++)
                        for (i = 0; i < 4; i++)
                            for (j = 0; j < 4; j++) acc[tid][i][j] = fma(sa[k][ty + 16 * i], sb[k][tx + 16 * j], acc[tid][i][j]);
                }
            }
            for (tid = 0; tid < 256; tid++) {
                int tx = tid % 16, ty = tid / 16;
                for (i = 0; i < 4; i++)
                    for (j = 0; j < 4; j++) {
                        long o = (long)(row0 + ty + 16 * i) * n + col0 + tx + 16 * j;
                        if (fp64) ((double *)C)[o] = acc[tid][i][j]; else ((float *)C)[o] = (float)acc[tid][i][j];
                    }
            }
        }
#undef LD
}

/* carry out one kernel on the host; a[i] points to argument i (a buffer argument = a pointer to the data pointer) */
static void stub_exec(const stub_fn *f, void **a)
{
    int i;
    for (i = 0; i < 6; i++) if (!strcmp(f->name, kname[i])) n_launch[i]++;
    if (!strcmp(f->name, "gemm_r")) {
        mat_mul(*(void **)a[0], *(void **)a[1], *(void **)a[2], *(int *)a[3], f->fp64 ? 8 : 4);
    } else if (!strcmp(f->name, "gemm_b")) {
        /* checked in full against the plain multiply: SRS's own spot check (64 entries of very regular matrices) can miss
           a wrong index, and the stub's cmp_u32 counts nothing */
        int nn = *(int *)a[3], es = f->fp64 ? 8 : 4;
        void *ref = malloc((size_t)nn * nn * es);
        gemm_b_exec(*(void **)a[0], *(void **)a[1], *(void **)a[2], nn, f->fp64);
        if (ref) {
            mat_mul(*(void **)a[0], *(void **)a[1], ref, nn, es);
            if (memcmp(ref, *(void **)a[2], (size_t)nn * nn * es)) { fprintf(stderr, "gpu_stub: gemm_b RESULT DIFFERS from the plain multiply (n = %d)\n", nn); abort(); }
            free(ref);
        }
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
}

static int stub_launch(void *fn, unsigned gx, unsigned gy, void **a)
{
    const stub_fn *f = fn;
    if (!f) {
        n_null++;
        fprintf(stderr, "gpu_stub: LAUNCH WITH A NULL FUNCTION HANDLE (grid %ux%u) - a real driver crashes here\n", gx, gy);
        fflush(stderr);
        abort();
    }
    stub_exec(f, a);
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
static int rtc_compile(void) { n_rtc++; return getenv("STUB_RTC_FAIL") ? 6 : 0; }
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
/* STUB_DGEMM_UNSUP: the library has no FP64 GEMM, as ZLUDA's cuBLAS (cuBLAS 15 / hipBLAS 7 = NOT_SUPPORTED) */
/* STUB_DGEMM_UNSUP_AFTER=N: the first N FP64 calls work, then NOT_SUPPORTED (a library that takes prep's size, not the run's) */
static int dgemm_unsup(void)
{
    static int calls;
    const char *a = getenv("STUB_DGEMM_UNSUP_AFTER");
    if (getenv("STUB_DGEMM_UNSUP")) return 1;
    return a && *a && calls++ >= atoi(a);
}
GEMM_SIG(cublasDgemm_v2, double) { if (dgemm_unsup()) return 15; GEMM_BODY(8) }
GEMM_SIG(hipblasDgemm, double) { if (dgemm_unsup()) return 7; GEMM_BODY(8) }
GEMM_SIG(cublasHgemm, uint16_t) GEMM_BODY(2)
GEMM_SIG(hipblasHgemm, uint16_t) GEMM_BODY(2)

/* ---------------------------------------------------------------- OpenCL (one platform, one GPU device) + CLBlast */
typedef struct { int magic; void *p; size_t n; } cl_buf;
typedef struct { int fp64; } cl_prog;
typedef struct { stub_fn fn; unsigned char val[8][16]; } cl_kern;
typedef struct { uint64_t t0, t1; } cl_ev;
#define BUF_MAGIC 0x5742
static cl_buf *g_bufs[64];
static uint64_t g_clock = 1000000;
static int g_plat, g_dev, g_ctx, g_q;

static int is_buf(const void *h) { int i; for (i = 0; i < 64; i++) if (g_bufs[i] && g_bufs[i] == h) return 1; return 0; }
static void new_ev(void **ev)
{
    cl_ev *e;
    uint64_t d = (uint64_t)(fake_ms() * 1e6);
    if (!ev) return;
    e = malloc(sizeof *e);
    e->t0 = g_clock; g_clock += d; e->t1 = g_clock;
    *ev = e;
}
static int put_str(const char *s, size_t n, void *v, size_t *r)
{
    size_t l = strlen(s) + 1;
    if (r) *r = l;
    if (v) { if (n < l) return -30; memcpy(v, s, l); }
    return 0;
}
static int put_u64(uint64_t x, size_t n, void *v, size_t *r) { if (r) *r = 8; if (v) { if (n < 8) return -30; memcpy(v, &x, 8); } return 0; }

EXPORT int clGetPlatformIDs(unsigned n, void **p, unsigned *np) { if (np) *np = 1; if (p && n) p[0] = &g_plat; return 0; }
EXPORT int clGetPlatformInfo(void *p, unsigned w, size_t n, void *v, size_t *r)
{
    (void)p;
    return put_str(w == 0x0902 ? "SRS stub OpenCL" : w == 0x0901 ? "OpenCL 3.0 stub" : "SRS test", n, v, r);
}
EXPORT int clGetDeviceIDs(void *p, uint64_t t, unsigned n, void **d, unsigned *nd) { (void)p; (void)t; if (nd) *nd = 1; if (d && n) d[0] = &g_dev; return 0; }
EXPORT int clGetDeviceInfo(void *d, unsigned w, size_t n, void *v, size_t *r)
{
    int fp64 = getenv("STUB_CL_FP64") && atoi(getenv("STUB_CL_FP64"));
    (void)d;
    switch (w) {
    case 0x102B: return put_str("SRS test stub OpenCL GPU", n, v, r);
    case 0x102D: return put_str("stub 1.0", n, v, r);
    case 0x1000: return put_u64(4, n, v, r);   /* CL_DEVICE_TYPE_GPU */
    case 0x101F: return put_u64((uint64_t)512 << 20, n, v, r);
    case 0x1032: return put_u64(fp64 ? 0x3f : 0, n, v, r);   /* CL_DEVICE_DOUBLE_FP_CONFIG */
    case 0x1030: return put_str(fp64 ? "cl_khr_byte_addressable_store cl_khr_fp64" : "cl_khr_byte_addressable_store", n, v, r);
    default: return -30;   /* CL_INVALID_VALUE: no PCI address etc. */
    }
}
EXPORT void *clCreateContext(const void *pr, unsigned n, void *const *d, void *cb, void *u, int *e) { (void)pr; (void)n; (void)d; (void)cb; (void)u; if (e) *e = 0; return &g_ctx; }
EXPORT int clReleaseContext(void *c) { (void)c; return 0; }
EXPORT void *clCreateCommandQueue(void *c, void *d, uint64_t p, int *e) { (void)c; (void)d; (void)p; if (e) *e = 0; return &g_q; }
EXPORT int clReleaseCommandQueue(void *q) { (void)q; return 0; }
EXPORT void *clCreateBuffer(void *c, uint64_t f, size_t n, void *h, int *e)
{
    cl_buf *b = calloc(1, sizeof *b);
    int i;
    (void)c; (void)f; (void)h;
    b->magic = BUF_MAGIC; b->n = n; b->p = calloc(1, n ? n : 1);
    for (i = 0; i < 64 && g_bufs[i]; i++) {}
    if (i == 64 || !b->p) { if (e) *e = -4; free(b->p); free(b); return NULL; }
    g_bufs[i] = b;
    if (e) *e = 0;
    return b;
}
EXPORT int clReleaseMemObject(void *m)
{
    int i;
    for (i = 0; i < 64; i++) if (g_bufs[i] == m) { g_bufs[i] = NULL; free(((cl_buf *)m)->p); free(m); return 0; }
    return -38;
}
EXPORT int clEnqueueWriteBuffer(void *q, void *m, unsigned bl, size_t off, size_t n, const void *p, unsigned ne, void *const *wl, void **ev)
{
    (void)q; (void)bl; (void)ne; (void)wl;
    memcpy((char *)((cl_buf *)m)->p + off, p, n); new_ev(ev); return 0;
}
EXPORT int clEnqueueReadBuffer(void *q, void *m, unsigned bl, size_t off, size_t n, void *p, unsigned ne, void *const *wl, void **ev)
{
    (void)q; (void)bl; (void)ne; (void)wl;
    memcpy(p, (char *)((cl_buf *)m)->p + off, n); new_ev(ev); return 0;
}
EXPORT void *clCreateProgramWithSource(void *c, unsigned n, const char **src, const size_t *l, int *e)
{
    cl_prog *p = malloc(sizeof *p);
    (void)c; (void)n; (void)l;
    p->fp64 = strstr(src[0], "#define REAL double") != NULL;
    if (e) *e = 0;
    return p;
}
EXPORT int clBuildProgram(void *p, unsigned n, void *const *d, const char *o, void *cb, void *u)
{
    (void)n; (void)d; (void)o; (void)cb; (void)u;
    n_clbuild[((cl_prog *)p)->fp64]++;
    return getenv("STUB_CL_BUILD_FAIL") ? -11 : 0;
}
EXPORT int clGetProgramBuildInfo(void *p, void *d, unsigned w, size_t n, void *v, size_t *r) { (void)p; (void)d; (void)w; return put_str("", n, v, r); }
EXPORT int clReleaseProgram(void *p) { free(p); return 0; }
EXPORT void *clCreateKernel(void *p, const char *name, int *e)
{
    int i;
    for (i = 0; i < 5; i++)
        if (!strcmp(name, kname[i])) {
            cl_kern *k = calloc(1, sizeof *k);
            k->fn.name = kname[i];
            k->fn.fp64 = ((cl_prog *)p)->fp64;
            if (e) *e = 0;
            return k;
        }
    if (e) *e = -46;
    return NULL;
}
EXPORT int clReleaseKernel(void *k) { free(k); return 0; }
EXPORT int clSetKernelArg(void *k, unsigned i, size_t sz, const void *v)
{
    cl_kern *kk = k;
    if (i >= 8 || sz > 16) return -51;
    if (sz == sizeof(void *) && is_buf(*(void *const *)v)) { void *data = (*(cl_buf *const *)v)->p; memcpy(kk->val[i], &data, sizeof data); }
    else memcpy(kk->val[i], v, sz);
    return 0;
}
EXPORT int clEnqueueNDRangeKernel(void *q, void *k, unsigned dim, const size_t *off, const size_t *g, const size_t *l, unsigned ne, void *const *wl, void **ev)
{
    cl_kern *kk = k;
    void *a[8];
    int i;
    (void)q; (void)dim; (void)off; (void)g; (void)l; (void)ne; (void)wl;
    for (i = 0; i < 8; i++) a[i] = kk->val[i];
    stub_exec(&kk->fn, a);
    new_ev(ev);
    return 0;
}
EXPORT int clEnqueueMarkerWithWaitList(void *q, unsigned ne, void *const *wl, void **ev) { (void)q; (void)ne; (void)wl; new_ev(ev); return 0; }
EXPORT int clGetEventInfo(void *ev, unsigned w, size_t n, void *v, size_t *r) { (void)ev; (void)w; if (r) *r = 4; if (v && n >= 4) *(int *)v = 0; return 0; }
EXPORT int clGetEventProfilingInfo(void *ev, unsigned w, size_t n, void *v, size_t *r) { const cl_ev *e = ev; return put_u64(w == 0x1282 ? e->t0 : e->t1, n, v, r); }
EXPORT int clReleaseEvent(void *ev) { free(ev); return 0; }
EXPORT int clFinish(void *q) { (void)q; return 0; }
EXPORT int clFlush(void *q) { (void)q; return 0; }

/* CLBlast, row-major, no transpose: C = A * B */
#define BLAST_SIG(name, T) \
    EXPORT int name(int lay, int ta, int tb, size_t m, size_t n, size_t k, T al, void *a, size_t ao, size_t lda, void *b, size_t bo, size_t ldb, T be, void *c, size_t co, size_t ldc, void **q, void **ev)
#define BLAST_BODY(es) { (void)lay; (void)ta; (void)tb; (void)m; (void)k; (void)al; (void)ao; (void)lda; (void)bo; (void)ldb; (void)be; (void)co; (void)ldc; (void)q; \
    n_blas++; mat_mul(((cl_buf *)a)->p, ((cl_buf *)b)->p, ((cl_buf *)c)->p, (int)n, es); new_ev(ev); return 0; }
BLAST_SIG(CLBlastSgemm, float) BLAST_BODY(4)
BLAST_SIG(CLBlastDgemm, double) BLAST_BODY(8)
BLAST_SIG(CLBlastHgemm, uint16_t) BLAST_BODY(2)
