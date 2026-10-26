/* bench_compute.c - CPU compute benchmark: matrix multiply, peak multiply-add, FFT, N-body,
 * Mandelbrot and a small ray tracer, 1 thread and N threads (see bench_compute.h).
 * Written for SRS from textbook methods: register-blocked matrix multiply (6 rows x 2 vectors),
 * iterative radix-2 Cooley-Tukey FFT (1965), direct-sum N-body gravity with softening, the
 * escape-time Mandelbrot algorithm and a Whitted-style ray tracer (1980). Vector code uses the
 * Intel intrinsics; each vector path is compiled with a target attribute and picked at run time.
 * Every result is checked: matrix multiply and multiply-add exactly (the inputs are chosen so
 * every rounding is exact), the rest against reference values computed another way, and every
 * repeat of a timed loop must give the same bits as the first. */
#define _GNU_SOURCE
#include "bench_compute.h"
#include "bench.h"
#include "burner.h"

#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__x86_64__) || defined(__i386__)
#define SRS_X86 1
#include <immintrin.h>
#endif

/* "plain C" means scalar code: keep the compiler from turning it into vector code */
#if defined(__GNUC__) && !defined(__clang__)
#define NOVEC __attribute__((optimize("no-tree-vectorize")))
#define LOOP_NOVEC
#elif defined(__clang__)
#define NOVEC
#define LOOP_NOVEC _Pragma("clang loop vectorize(disable) interleave(disable)")
#else
#define NOVEC
#define LOOP_NOVEC
#endif

#define GEMM_N 288          /* multiple of 6 (rows) and 32 (2 AVX-512 float vectors) */
#define GEMM_N_SMALL 96
#define FFT_N 4096
#define FFT_N_SMALL 256
#define FFT_BATCH 16
#define NB_N 1024
#define NB_N_SMALL 64
#define NB_EPS2 0.01
#define MB_W 640
#define MB_H 480
#define MB_MAXIT 255
#define RT_W 320
#define RT_H 240
#define FMA_ACC 12
#define FMA_ITERS 100000L

const char *const cp_test_name[CP_NTEST] = {
    "Matrix multiply, single precision (SGEMM)", "Matrix multiply, double precision (DGEMM)",
    "Peak multiply-add, single precision", "Peak multiply-add, double precision",
    "FFT 4096 points, single precision", "FFT 4096 points, double precision",
    "N-body gravity, 1,024 bodies (double)", "Mandelbrot set, 640 x 480 (double)", "Ray tracer, 320 x 240 (single)" };
const char *const cp_test_id[CP_NTEST] = { "sgemm", "dgemm", "fma-sp", "fma-dp", "fft-sp", "fft-dp", "nbody", "mandelbrot", "raytrace" };
const char *const cp_unit[CP_NTEST] = { "GFLOPS", "GFLOPS", "GFLOPS", "GFLOPS", "GFLOPS", "GFLOPS", "GFLOPS", "Mpixels/s", "Mpixels/s" };

const char *cp_isa_name(int isa)
{
    static const char *const n[CP_NISA] = { "best available", "plain C", "SSE2", "AVX2 + FMA", "AVX-512" };
    return isa >= 0 && isa < CP_NISA ? n[isa] : "?";
}
const char *cp_isa_id(int isa)
{
    static const char *const n[CP_NISA] = { "auto", "c", "sse2", "avx2", "avx512" };
    return isa >= 0 && isa < CP_NISA ? n[isa] : "?";
}

int cp_isa_ok(int isa)
{
    if (isa == CP_ISA_C) return 1;
#ifdef SRS_X86
    __builtin_cpu_init();
    if (isa == CP_ISA_SSE2) return __builtin_cpu_supports("sse2");
    if (isa == CP_ISA_AVX2) return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
    if (isa == CP_ISA_AVX512) return __builtin_cpu_supports("avx512f");
#endif
    return 0;
}

int cp_isa_best(void)
{
    int i;
    for (i = CP_ISA_AVX512; i > CP_ISA_C; i--) if (cp_isa_ok(i)) return i;
    return CP_ISA_C;
}

#ifdef __linux__
static const double cp_scale[CP_NTEST] = { 1e9, 1e9, 1e9, 1e9, 1e9, 1e9, 1e9, 1e6, 1e6 };
static int has_vector(int test) { return test != CP_FFT_SP && test != CP_FFT_DP && test != CP_RAY; }

static int isa_for(int test, int want)
{
    if (!has_vector(test)) return CP_ISA_C;
    return want > CP_ISA_AUTO && want < CP_NISA && cp_isa_ok(want) ? want : cp_isa_best();
}

static void *xalloc(size_t n)
{
    void *p = NULL;
    n = (n + 63) & ~(size_t)63;
    if (posix_memalign(&p, 64, n ? n : 64)) return NULL;
    return p;
}

static uint32_t lcg_u(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }
static double lcg(uint32_t *s) { return lcg_u(s) / 16777216.0 * 2 - 1; }   /* [-1, 1) */

static uint64_t hash_bytes(const void *p, size_t n)
{
    const unsigned char *b = p;
    uint64_t h = 1469598103934665603ULL, w;
    size_t i;
    for (i = 0; i + 8 <= n; i += 8) { memcpy(&w, b + i, 8); h = (h ^ w) * 1099511628211ULL; }
    for (; i < n; i++) h = (h ^ b[i]) * 1099511628211ULL;
    return h;
}

/* ======================================================================
 * matrix multiply C = A B (n x n, row-major)
 * ====================================================================== */
#define GEMM_C(NAME, T) \
NOVEC static void NAME(const T *a, const T *b, T *c, int n) \
{ \
    int i, j, k; \
    for (i = 0; i < n; i++) { \
        T *cr = c + (size_t)i * n; \
        LOOP_NOVEC for (j = 0; j < n; j++) cr[j] = 0; \
        for (k = 0; k < n; k++) { \
            T v = a[(size_t)i * n + k]; \
            const T *br = b + (size_t)k * n; \
            LOOP_NOVEC for (j = 0; j < n; j++) cr[j] += v * br[j]; \
        } \
    } \
}
GEMM_C(gemm_c_f, float)
GEMM_C(gemm_c_d, double)

/* 6 rows x 2 vectors of C stay in 12 registers for the whole k loop */
#define GEMM_V(NAME, T, VT, W, LD, ST, BC, MA, ZR) \
static void NAME(const T *a, const T *b, T *c, int n) \
{ \
    int i0, j0, k; \
    for (j0 = 0; j0 < n; j0 += 2 * (W)) \
        for (i0 = 0; i0 < n; i0 += 6) { \
            const T *a0 = a + (size_t)i0 * n, *bp = b + j0; \
            T *cp = c + (size_t)i0 * n + j0; \
            VT x00 = ZR, x01 = ZR, x10 = ZR, x11 = ZR, x20 = ZR, x21 = ZR; \
            VT x30 = ZR, x31 = ZR, x40 = ZR, x41 = ZR, x50 = ZR, x51 = ZR; \
            for (k = 0; k < n; k++, bp += n) { \
                VT b0 = LD(bp), b1 = LD(bp + (W)), v; \
                v = BC(a0[k]);         x00 = MA(v, b0, x00); x01 = MA(v, b1, x01); \
                v = BC(a0[n + k]);     x10 = MA(v, b0, x10); x11 = MA(v, b1, x11); \
                v = BC(a0[2 * n + k]); x20 = MA(v, b0, x20); x21 = MA(v, b1, x21); \
                v = BC(a0[3 * n + k]); x30 = MA(v, b0, x30); x31 = MA(v, b1, x31); \
                v = BC(a0[4 * n + k]); x40 = MA(v, b0, x40); x41 = MA(v, b1, x41); \
                v = BC(a0[5 * n + k]); x50 = MA(v, b0, x50); x51 = MA(v, b1, x51); \
            } \
            ST(cp, x00); ST(cp + (W), x01); cp += n; \
            ST(cp, x10); ST(cp + (W), x11); cp += n; \
            ST(cp, x20); ST(cp + (W), x21); cp += n; \
            ST(cp, x30); ST(cp + (W), x31); cp += n; \
            ST(cp, x40); ST(cp + (W), x41); cp += n; \
            ST(cp, x50); ST(cp + (W), x51); \
        } \
}

#ifdef SRS_X86
#define SSE_MA_PS(a, b, c) _mm_add_ps(_mm_mul_ps(a, b), c)
#define SSE_MA_PD(a, b, c) _mm_add_pd(_mm_mul_pd(a, b), c)
__attribute__((target("sse2"))) GEMM_V(gemm_sse_f, float, __m128, 4, _mm_loadu_ps, _mm_storeu_ps, _mm_set1_ps, SSE_MA_PS, _mm_setzero_ps())
__attribute__((target("sse2"))) GEMM_V(gemm_sse_d, double, __m128d, 2, _mm_loadu_pd, _mm_storeu_pd, _mm_set1_pd, SSE_MA_PD, _mm_setzero_pd())
__attribute__((target("avx2,fma"))) GEMM_V(gemm_avx2_f, float, __m256, 8, _mm256_loadu_ps, _mm256_storeu_ps, _mm256_set1_ps, _mm256_fmadd_ps, _mm256_setzero_ps())
__attribute__((target("avx2,fma"))) GEMM_V(gemm_avx2_d, double, __m256d, 4, _mm256_loadu_pd, _mm256_storeu_pd, _mm256_set1_pd, _mm256_fmadd_pd, _mm256_setzero_pd())
__attribute__((target("avx512f"))) GEMM_V(gemm_512_f, float, __m512, 16, _mm512_loadu_ps, _mm512_storeu_ps, _mm512_set1_ps, _mm512_fmadd_ps, _mm512_setzero_ps())
__attribute__((target("avx512f"))) GEMM_V(gemm_512_d, double, __m512d, 8, _mm512_loadu_pd, _mm512_storeu_pd, _mm512_set1_pd, _mm512_fmadd_pd, _mm512_setzero_pd())
#endif

static void gemm_f(int isa, const float *a, const float *b, float *c, int n)
{
    switch (isa) {
#ifdef SRS_X86
    case CP_ISA_AVX512: gemm_512_f(a, b, c, n); return;
    case CP_ISA_AVX2: gemm_avx2_f(a, b, c, n); return;
    case CP_ISA_SSE2: gemm_sse_f(a, b, c, n); return;
#endif
    default: gemm_c_f(a, b, c, n);
    }
}

static void gemm_d(int isa, const double *a, const double *b, double *c, int n)
{
    switch (isa) {
#ifdef SRS_X86
    case CP_ISA_AVX512: gemm_512_d(a, b, c, n); return;
    case CP_ISA_AVX2: gemm_avx2_d(a, b, c, n); return;
    case CP_ISA_SSE2: gemm_sse_d(a, b, c, n); return;
#endif
    default: gemm_c_d(a, b, c, n);
    }
}

/* inputs are small multiples of 1/4 and 1/2, so every product and partial sum is exact in
   single precision and any summation order gives the same answer: C * 8 must equal the integer
   reference exactly */
static int gemm_ai(size_t i) { return (int)((i * 7 + 3) % 17) - 8; }
static int gemm_bi(size_t i) { return (int)((i * 5 + 1) % 13) - 6; }

/* ======================================================================
 * peak multiply-add: 12 independent chains of x = x*2 + 1, x = x*0.5 - 0.5, which gives back
 * x exactly, so every lane must end where it started
 * ====================================================================== */
#define FMA_LOOP(NAME, T, VT, W, LD, ST, SET1, MA) \
static void NAME(T *x, long it) \
{ \
    VT a0 = LD(x), a1 = LD(x + (W)), a2 = LD(x + 2 * (W)), a3 = LD(x + 3 * (W)), a4 = LD(x + 4 * (W)), a5 = LD(x + 5 * (W)); \
    VT a6 = LD(x + 6 * (W)), a7 = LD(x + 7 * (W)), a8 = LD(x + 8 * (W)), a9 = LD(x + 9 * (W)), a10 = LD(x + 10 * (W)), a11 = LD(x + 11 * (W)); \
    const VT two = SET1(2.0), one = SET1(1.0), half = SET1(0.5), mhalf = SET1(-0.5); \
    long i; \
    for (i = 0; i < it; i++) { \
        a0 = MA(a0, two, one); a1 = MA(a1, two, one); a2 = MA(a2, two, one); a3 = MA(a3, two, one); \
        a4 = MA(a4, two, one); a5 = MA(a5, two, one); a6 = MA(a6, two, one); a7 = MA(a7, two, one); \
        a8 = MA(a8, two, one); a9 = MA(a9, two, one); a10 = MA(a10, two, one); a11 = MA(a11, two, one); \
        a0 = MA(a0, half, mhalf); a1 = MA(a1, half, mhalf); a2 = MA(a2, half, mhalf); a3 = MA(a3, half, mhalf); \
        a4 = MA(a4, half, mhalf); a5 = MA(a5, half, mhalf); a6 = MA(a6, half, mhalf); a7 = MA(a7, half, mhalf); \
        a8 = MA(a8, half, mhalf); a9 = MA(a9, half, mhalf); a10 = MA(a10, half, mhalf); a11 = MA(a11, half, mhalf); \
    } \
    ST(x, a0); ST(x + (W), a1); ST(x + 2 * (W), a2); ST(x + 3 * (W), a3); ST(x + 4 * (W), a4); ST(x + 5 * (W), a5); \
    ST(x + 6 * (W), a6); ST(x + 7 * (W), a7); ST(x + 8 * (W), a8); ST(x + 9 * (W), a9); ST(x + 10 * (W), a10); ST(x + 11 * (W), a11); \
}
#define C_LD(p) (*(p))
#define C_ST(p, v) (*(p) = (v))
#define C_SET1F(v) ((float)(v))
#define C_SET1D(v) ((double)(v))
#define C_MA(a, b, c) ((a) * (b) + (c))
NOVEC FMA_LOOP(fma_c_f, float, float, 1, C_LD, C_ST, C_SET1F, C_MA)
NOVEC FMA_LOOP(fma_c_d, double, double, 1, C_LD, C_ST, C_SET1D, C_MA)
#ifdef SRS_X86
__attribute__((target("sse2"))) FMA_LOOP(fma_sse_f, float, __m128, 4, _mm_loadu_ps, _mm_storeu_ps, _mm_set1_ps, SSE_MA_PS)
__attribute__((target("sse2"))) FMA_LOOP(fma_sse_d, double, __m128d, 2, _mm_loadu_pd, _mm_storeu_pd, _mm_set1_pd, SSE_MA_PD)
__attribute__((target("avx2,fma"))) FMA_LOOP(fma_avx2_f, float, __m256, 8, _mm256_loadu_ps, _mm256_storeu_ps, _mm256_set1_ps, _mm256_fmadd_ps)
__attribute__((target("avx2,fma"))) FMA_LOOP(fma_avx2_d, double, __m256d, 4, _mm256_loadu_pd, _mm256_storeu_pd, _mm256_set1_pd, _mm256_fmadd_pd)
__attribute__((target("avx512f"))) FMA_LOOP(fma_512_f, float, __m512, 16, _mm512_loadu_ps, _mm512_storeu_ps, _mm512_set1_ps, _mm512_fmadd_ps)
__attribute__((target("avx512f"))) FMA_LOOP(fma_512_d, double, __m512d, 8, _mm512_loadu_pd, _mm512_storeu_pd, _mm512_set1_pd, _mm512_fmadd_pd)
#endif

static int isa_lanes(int isa, int dbl)
{
    int w = isa == CP_ISA_AVX512 ? 16 : isa == CP_ISA_AVX2 ? 8 : isa == CP_ISA_SSE2 ? 4 : 1;
    return dbl && w > 1 ? w / 2 : w;
}

/* returns floating-point operations done (each multiply-add counts as 2) */
static double fma_run(int isa, int dbl, void *x, long it)
{
    switch (isa * 2 + dbl) {
#ifdef SRS_X86
    case CP_ISA_AVX512 * 2: fma_512_f(x, it); break;
    case CP_ISA_AVX512 * 2 + 1: fma_512_d(x, it); break;
    case CP_ISA_AVX2 * 2: fma_avx2_f(x, it); break;
    case CP_ISA_AVX2 * 2 + 1: fma_avx2_d(x, it); break;
    case CP_ISA_SSE2 * 2: fma_sse_f(x, it); break;
    case CP_ISA_SSE2 * 2 + 1: fma_sse_d(x, it); break;
#endif
    default: if (dbl) fma_c_d(x, it); else fma_c_f(x, it);
    }
    return (double)it * 2 * FMA_ACC * isa_lanes(isa, dbl) * 2;
}

static double fma_start(int i) { return (double)(i % 29 - 14); }

/* ======================================================================
 * FFT: out-of-place, iterative radix-2 decimation in time, interleaved complex (re, im)
 * ====================================================================== */
#define FFT_FN(NAME, T) \
static void NAME(const T *in, T *out, const T *tw, const int *rev, int n) \
{ \
    int i, j, len; \
    for (i = 0; i < n; i++) { out[2 * rev[i]] = in[2 * i]; out[2 * rev[i] + 1] = in[2 * i + 1]; } \
    for (len = 2; len <= n; len <<= 1) { \
        int half = len >> 1, step = n / len; \
        for (i = 0; i < n; i += len) { \
            T *p = out + 2 * i, *q = p + 2 * half; \
            for (j = 0; j < half; j++) { \
                T wr = tw[2 * j * step], wi = tw[2 * j * step + 1]; \
                T vr = q[2 * j] * wr - q[2 * j + 1] * wi, vi = q[2 * j] * wi + q[2 * j + 1] * wr; \
                T ur = p[2 * j], ui = p[2 * j + 1]; \
                p[2 * j] = ur + vr; p[2 * j + 1] = ui + vi; \
                q[2 * j] = ur - vr; q[2 * j + 1] = ui - vi; \
            } \
        } \
    } \
}
FFT_FN(fft_f, float)
FFT_FN(fft_d, double)

static int ilog2(int n) { int l = 0; while ((1 << l) < n) l++; return l; }

/* ======================================================================
 * N-body: acceleration of every body from all bodies (softened), structure of arrays
 * ====================================================================== */
NOVEC static void nbody_c(const double *x, const double *y, const double *z, const double *m,
                          double *ax, double *ay, double *az, int n)
{
    int i, j;
    for (i = 0; i < n; i++) {
        double xi = x[i], yi = y[i], zi = z[i], sx = 0, sy = 0, sz = 0;
        LOOP_NOVEC for (j = 0; j < n; j++) {
            double dx = x[j] - xi, dy = y[j] - yi, dz = z[j] - zi;
            double r2 = dx * dx + dy * dy + dz * dz + NB_EPS2, inv = 1.0 / sqrt(r2), s = m[j] * inv * inv * inv;
            sx += dx * s; sy += dy * s; sz += dz * s;
        }
        ax[i] = sx; ay[i] = sy; az[i] = sz;
    }
}

#define NBODY_V(NAME, VT, W, LD, ST, SET1, ZR, ADD, SUB, MUL, DIV, SQRT, MA) \
static void NAME(const double *x, const double *y, const double *z, const double *m, \
                 double *ax, double *ay, double *az, int n) \
{ \
    int i, j, l; \
    double t[3][W]; \
    for (i = 0; i < n; i++) { \
        VT xi = SET1(x[i]), yi = SET1(y[i]), zi = SET1(z[i]), sx = ZR, sy = ZR, sz = ZR; \
        const VT e2 = SET1(NB_EPS2), one = SET1(1.0); \
        for (j = 0; j < n; j += (W)) { \
            VT dx = SUB(LD(x + j), xi), dy = SUB(LD(y + j), yi), dz = SUB(LD(z + j), zi); \
            VT r2 = MA(dx, dx, MA(dy, dy, MA(dz, dz, e2))); \
            VT inv = DIV(one, SQRT(r2)); \
            VT s = MUL(LD(m + j), MUL(inv, MUL(inv, inv))); \
            sx = MA(dx, s, sx); sy = MA(dy, s, sy); sz = MA(dz, s, sz); \
        } \
        ST(t[0], sx); ST(t[1], sy); ST(t[2], sz); \
        ax[i] = ay[i] = az[i] = 0; \
        for (l = 0; l < (W); l++) { ax[i] += t[0][l]; ay[i] += t[1][l]; az[i] += t[2][l]; } \
    } \
}
#ifdef SRS_X86
__attribute__((target("sse2"))) NBODY_V(nbody_sse, __m128d, 2, _mm_loadu_pd, _mm_storeu_pd, _mm_set1_pd, _mm_setzero_pd(),
    _mm_add_pd, _mm_sub_pd, _mm_mul_pd, _mm_div_pd, _mm_sqrt_pd, SSE_MA_PD)
__attribute__((target("avx2,fma"))) NBODY_V(nbody_avx2, __m256d, 4, _mm256_loadu_pd, _mm256_storeu_pd, _mm256_set1_pd, _mm256_setzero_pd(),
    _mm256_add_pd, _mm256_sub_pd, _mm256_mul_pd, _mm256_div_pd, _mm256_sqrt_pd, _mm256_fmadd_pd)
__attribute__((target("avx512f"))) NBODY_V(nbody_512, __m512d, 8, _mm512_loadu_pd, _mm512_storeu_pd, _mm512_set1_pd, _mm512_setzero_pd(),
    _mm512_add_pd, _mm512_sub_pd, _mm512_mul_pd, _mm512_div_pd, _mm512_sqrt_pd, _mm512_fmadd_pd)
#endif

static void nbody(int isa, double *const *p, int n)   /* p: x y z m ax ay az */
{
    switch (isa) {
#ifdef SRS_X86
    case CP_ISA_AVX512: nbody_512(p[0], p[1], p[2], p[3], p[4], p[5], p[6], n); return;
    case CP_ISA_AVX2: nbody_avx2(p[0], p[1], p[2], p[3], p[4], p[5], p[6], n); return;
    case CP_ISA_SSE2: nbody_sse(p[0], p[1], p[2], p[3], p[4], p[5], p[6], n); return;
#endif
    default: nbody_c(p[0], p[1], p[2], p[3], p[4], p[5], p[6], n);
    }
}

/* reference: every pair once, Newton's third law - another summation order than the kernels */
static void nbody_ref(double *const *p, int n)
{
    int i, j;
    for (i = 0; i < n; i++) p[4][i] = p[5][i] = p[6][i] = 0;
    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++) {
            double dx = p[0][j] - p[0][i], dy = p[1][j] - p[1][i], dz = p[2][j] - p[2][i];
            double r2 = dx * dx + dy * dy + dz * dz + NB_EPS2, inv = 1.0 / sqrt(r2), f = inv * inv * inv;
            p[4][i] += dx * f * p[3][j]; p[5][i] += dy * f * p[3][j]; p[6][i] += dz * f * p[3][j];
            p[4][j] -= dx * f * p[3][i]; p[5][j] -= dy * f * p[3][i]; p[6][j] -= dz * f * p[3][i];
        }
}

/* ======================================================================
 * Mandelbrot: escape time, at most MB_MAXIT steps; the vector paths do the same operations in
 * the same order as the plain C code (no fused multiply-add), so the counts match exactly
 * ====================================================================== */
#if defined(__i386__) && !defined(__SSE2_MATH__)
#define CP_X87 1          /* 32-bit build doing scalar maths on the x87 unit (80-bit registers) */
#endif
#define MB_X0 (-2.2)
#define MB_Y0 (-1.125)

NOVEC static void mandel_c(unsigned char *out, int w, int h)
{
    double dx = 3.0 / w, dy = 2.25 / h;
    int px, py, n;
    for (py = 0; py < h; py++) {
        double ci = (double)py * dy, cr;   /* separate statements: no fused multiply-add */
        ci += MB_Y0;
        for (px = 0; px < w; px++) {
            double zr = 0, zi = 0, zr2, zi2, t;
            cr = (double)px * dx;
            cr += MB_X0;
            for (n = 0; n < MB_MAXIT; n++) {
                zr2 = zr * zr; zi2 = zi * zi;
                if (!(zr2 + zi2 <= 4.0)) break;
                t = zr * zi; zi = t + t + ci; zr = zr2 - zi2 + cr;
            }
            out[(size_t)py * w + px] = (unsigned char)n;
        }
    }
}

#ifdef SRS_X86
__attribute__((target("sse2"))) static void mandel_sse(unsigned char *out, int w, int h)
{
    double dx = 3.0 / w, dy = 2.25 / h, t[2];
    const __m128d four = _mm_set1_pd(4.0), one = _mm_set1_pd(1.0), lane = _mm_set_pd(1, 0), vx0 = _mm_set1_pd(MB_X0), vdx = _mm_set1_pd(dx);
    int px, py, n, l;
    for (py = 0; py < h; py++) {
        __m128d ci = _mm_set1_pd(MB_Y0 + (double)py * dy);
        for (px = 0; px < w; px += 2) {
            __m128d cr = _mm_add_pd(vx0, _mm_mul_pd(_mm_add_pd(_mm_set1_pd((double)px), lane), vdx));
            __m128d zr = _mm_setzero_pd(), zi = _mm_setzero_pd(), cnt = _mm_setzero_pd(), act = _mm_cmpeq_pd(zr, zr);
            for (n = 0; n < MB_MAXIT; n++) {
                __m128d zr2 = _mm_mul_pd(zr, zr), zi2 = _mm_mul_pd(zi, zi), tt;
                act = _mm_and_pd(act, _mm_cmple_pd(_mm_add_pd(zr2, zi2), four));
                if (!_mm_movemask_pd(act)) break;
                cnt = _mm_add_pd(cnt, _mm_and_pd(act, one));
                tt = _mm_mul_pd(zr, zi);
                zi = _mm_add_pd(_mm_add_pd(tt, tt), ci);
                zr = _mm_add_pd(_mm_sub_pd(zr2, zi2), cr);
            }
            _mm_storeu_pd(t, cnt);
            for (l = 0; l < 2; l++) out[(size_t)py * w + px + l] = (unsigned char)t[l];
        }
    }
}

__attribute__((target("avx2"))) static void mandel_avx2(unsigned char *out, int w, int h)
{
    double dx = 3.0 / w, dy = 2.25 / h, t[4];
    const __m256d four = _mm256_set1_pd(4.0), one = _mm256_set1_pd(1.0), lane = _mm256_set_pd(3, 2, 1, 0);
    const __m256d vx0 = _mm256_set1_pd(MB_X0), vdx = _mm256_set1_pd(dx);
    int px, py, n, l;
    for (py = 0; py < h; py++) {
        __m256d ci = _mm256_set1_pd(MB_Y0 + (double)py * dy);
        for (px = 0; px < w; px += 4) {
            __m256d cr = _mm256_add_pd(vx0, _mm256_mul_pd(_mm256_add_pd(_mm256_set1_pd((double)px), lane), vdx));
            __m256d zr = _mm256_setzero_pd(), zi = _mm256_setzero_pd(), cnt = _mm256_setzero_pd();
            __m256d act = _mm256_cmp_pd(zr, zr, _CMP_EQ_OQ);
            for (n = 0; n < MB_MAXIT; n++) {
                __m256d zr2 = _mm256_mul_pd(zr, zr), zi2 = _mm256_mul_pd(zi, zi), tt;
                act = _mm256_and_pd(act, _mm256_cmp_pd(_mm256_add_pd(zr2, zi2), four, _CMP_LE_OQ));
                if (!_mm256_movemask_pd(act)) break;
                cnt = _mm256_add_pd(cnt, _mm256_and_pd(act, one));
                tt = _mm256_mul_pd(zr, zi);
                zi = _mm256_add_pd(_mm256_add_pd(tt, tt), ci);
                zr = _mm256_add_pd(_mm256_sub_pd(zr2, zi2), cr);
            }
            _mm256_storeu_pd(t, cnt);
            for (l = 0; l < 4; l++) out[(size_t)py * w + px + l] = (unsigned char)t[l];
        }
    }
}

__attribute__((target("avx512f"))) static void mandel_512(unsigned char *out, int w, int h)
{
    double dx = 3.0 / w, dy = 2.25 / h, t[8];
    const __m512d four = _mm512_set1_pd(4.0), one = _mm512_set1_pd(1.0), lane = _mm512_set_pd(7, 6, 5, 4, 3, 2, 1, 0);
    const __m512d vx0 = _mm512_set1_pd(MB_X0), vdx = _mm512_set1_pd(dx);
    int px, py, n, l;
    for (py = 0; py < h; py++) {
        __m512d ci = _mm512_set1_pd(MB_Y0 + (double)py * dy);
        for (px = 0; px < w; px += 8) {
            __m512d cr = _mm512_add_pd(vx0, _mm512_mul_pd(_mm512_add_pd(_mm512_set1_pd((double)px), lane), vdx));
            __m512d zr = _mm512_setzero_pd(), zi = _mm512_setzero_pd(), cnt = _mm512_setzero_pd();
            __mmask8 act = 0xFF;
            for (n = 0; n < MB_MAXIT; n++) {
                __m512d zr2 = _mm512_mul_pd(zr, zr), zi2 = _mm512_mul_pd(zi, zi), tt;
                act = _mm512_mask_cmp_pd_mask(act, _mm512_add_pd(zr2, zi2), four, _CMP_LE_OQ);
                if (!act) break;
                cnt = _mm512_mask_add_pd(cnt, act, cnt, one);
                tt = _mm512_mul_pd(zr, zi);
                zi = _mm512_add_pd(_mm512_add_pd(tt, tt), ci);
                zr = _mm512_add_pd(_mm512_sub_pd(zr2, zi2), cr);
            }
            _mm512_storeu_pd(t, cnt);
            for (l = 0; l < 8; l++) out[(size_t)py * w + px + l] = (unsigned char)t[l];
        }
    }
}
#endif

static void mandel(int isa, unsigned char *out, int w, int h)
{
    switch (isa) {
#ifdef SRS_X86
    case CP_ISA_AVX512: mandel_512(out, w, h); return;
    case CP_ISA_AVX2: mandel_avx2(out, w, h); return;
    case CP_ISA_SSE2: mandel_sse(out, w, h); return;
#endif
    default: mandel_c(out, w, h);
    }
}

/* ======================================================================
 * ray tracer: four spheres on a checkered floor, one point light, hard shadows and up to two
 * reflections. Timed in single precision; the double-precision copy is the reference.
 * ====================================================================== */
#define RT_NS 4
static const double rt_scene[RT_NS][8] = {   /* centre x y z, radius, colour r g b, reflectivity */
    { 0.0, 1.0, 3.0, 1.0, 0.90, 0.15, 0.10, 0.30 },
    { -2.2, 0.7, 4.0, 0.7, 0.15, 0.80, 0.20, 0.20 },
    { 2.0, 0.8, 2.8, 0.8, 0.15, 0.25, 0.90, 0.50 },
    { 0.6, 0.3, 1.6, 0.3, 0.90, 0.90, 0.90, 0.60 } };

#define RT_FN(NAME, T, SQRT, FLOOR) \
static void NAME(unsigned char *rgb, int w, int h) \
{ \
    T s[RT_NS][8]; \
    const T lx = (T)-3, ly = (T)6, lz = (T)-2, fov = (T)0.57735, eps = (T)1e-3; \
    int px, py, i, k, d; \
    for (i = 0; i < RT_NS; i++) for (k = 0; k < 8; k++) s[i][k] = (T)rt_scene[i][k]; \
    for (py = 0; py < h; py++) \
        for (px = 0; px < w; px++) { \
            T dx = ((T)2 * ((T)px + (T)0.5) / (T)w - (T)1) * fov * (T)w / (T)h; \
            T dy = ((T)1 - (T)2 * ((T)py + (T)0.5) / (T)h) * fov, dz = (T)1, len; \
            T ox = 0, oy = (T)1.2, oz = (T)-3, col[3] = { 0, 0, 0 }, wgt = 1; \
            unsigned char *o = rgb + 3 * ((size_t)py * w + px); \
            len = SQRT(dx * dx + dy * dy + dz * dz); dx /= len; dy /= len; dz /= len; \
            for (d = 0; d < 3; d++) { \
                T t = (T)60, hx, hy, hz, nx, ny, nz, base[3], refl, Lx, Ly, Lz, Ld, diff, dn; \
                int id = -1, shadow = 0; \
                for (i = 0; i < RT_NS; i++) { \
                    T cx = ox - s[i][0], cy = oy - s[i][1], cz = oz - s[i][2]; \
                    T b = cx * dx + cy * dy + cz * dz, c = cx * cx + cy * cy + cz * cz - s[i][3] * s[i][3], disc = b * b - c; \
                    if (disc > 0) { T t0 = -b - SQRT(disc); if (t0 > eps && t0 < t) { t = t0; id = i; } } \
                } \
                if (dy < (T)-1e-6) { T tp = -oy / dy; if (tp > eps && tp < t) { t = tp; id = RT_NS; } } \
                if (id < 0) { \
                    T sky = (T)0.5 + (T)0.5 * dy; \
                    col[0] += wgt * (T)0.5 * sky; col[1] += wgt * (T)0.7 * sky; col[2] += wgt * sky; \
                    break; \
                } \
                hx = ox + t * dx; hy = oy + t * dy; hz = oz + t * dz; \
                if (id < RT_NS) { \
                    nx = (hx - s[id][0]) / s[id][3]; ny = (hy - s[id][1]) / s[id][3]; nz = (hz - s[id][2]) / s[id][3]; \
                    base[0] = s[id][4]; base[1] = s[id][5]; base[2] = s[id][6]; refl = s[id][7]; \
                } else { \
                    T g = (((long)FLOOR(hx) + (long)FLOOR(hz)) & 1) ? (T)0.9 : (T)0.2; \
                    nx = 0; ny = 1; nz = 0; base[0] = base[1] = base[2] = g; refl = (T)0.25; \
                } \
                Lx = lx - hx; Ly = ly - hy; Lz = lz - hz; Ld = SQRT(Lx * Lx + Ly * Ly + Lz * Lz); \
                Lx /= Ld; Ly /= Ld; Lz /= Ld; \
                for (i = 0; i < RT_NS && !shadow; i++) { \
                    T cx = hx - s[i][0], cy = hy - s[i][1], cz = hz - s[i][2]; \
                    T b = cx * Lx + cy * Ly + cz * Lz, c = cx * cx + cy * cy + cz * cz - s[i][3] * s[i][3], disc = b * b - c; \
                    if (disc > 0) { T t0 = -b - SQRT(disc); if (t0 > eps && t0 < Ld) shadow = 1; } \
                } \
                diff = nx * Lx + ny * Ly + nz * Lz; \
                if (diff < 0 || shadow) diff = 0; \
                for (k = 0; k < 3; k++) col[k] += wgt * ((T)1 - refl) * base[k] * ((T)0.1 + (T)0.9 * diff); \
                wgt *= refl; \
                if (wgt < (T)0.01) break; \
                dn = dx * nx + dy * ny + dz * nz; \
                dx -= (T)2 * dn * nx; dy -= (T)2 * dn * ny; dz -= (T)2 * dn * nz; \
                ox = hx; oy = hy; oz = hz; \
            } \
            for (k = 0; k < 3; k++) o[k] = (unsigned char)((col[k] > 1 ? (T)1 : col[k]) * (T)255 + (T)0.5); \
        } \
}
RT_FN(ray_f, float, sqrtf, floorf)
RT_FN(ray_d, double, sqrt, floor)

/* ======================================================================
 * one test's inputs and reference values (shared by all threads), and one thread's own copy
 * ====================================================================== */
typedef struct {
    int test, n, h;        /* matrix size / FFT points / bodies / image width; image height */
    void *in[4];           /* inputs */
    void *ref;             /* reference output */
    int *rev;              /* FFT bit-reversal table */
    double norm2;          /* FFT: sum of |x|^2 of the input */
    int bins[16];          /* FFT: checked output bins */
    double bin_re[16], bin_im[16];
} cp_ref;

typedef struct {
    int test, isa, n, h;
    void *in[4], *out[3];
    const cp_ref *r;
} cp_work;

static void ref_free(cp_ref *r)
{
    int i;
    for (i = 0; i < 4; i++) free(r->in[i]);
    free(r->ref);
    free(r->rev);
    memset(r, 0, sizeof *r);
}

/* 0 ok, -1 out of memory */
static int ref_make(cp_ref *r, int test, int small)
{
    uint32_t seed = 12345u + (uint32_t)test;
    size_t i, j, k, nn;
    memset(r, 0, sizeof *r);
    r->test = test;
    switch (test) {
    case CP_SGEMM: case CP_DGEMM: {
        int n = small ? GEMM_N_SMALL : GEMM_N, dbl = test == CP_DGEMM, *ai, *bi;
        int64_t *rc;
        r->n = n;
        nn = (size_t)n * n;
        r->in[0] = xalloc(nn * (dbl ? 8 : 4));
        r->in[1] = xalloc(nn * (dbl ? 8 : 4));
        r->ref = xalloc(nn * sizeof(int64_t));
        ai = malloc(nn * sizeof *ai);
        bi = malloc(nn * sizeof *bi);
        if (!r->in[0] || !r->in[1] || !r->ref || !ai || !bi) { free(ai); free(bi); ref_free(r); return -1; }
        for (i = 0; i < nn; i++) {
            ai[i] = gemm_ai(i);
            bi[i] = gemm_bi(i);
            if (dbl) { ((double *)r->in[0])[i] = ai[i] * 0.25; ((double *)r->in[1])[i] = bi[i] * 0.5; }
            else { ((float *)r->in[0])[i] = (float)(ai[i] * 0.25); ((float *)r->in[1])[i] = (float)(bi[i] * 0.5); }
        }
        rc = r->ref;   /* C * 8 as integers */
        memset(rc, 0, nn * sizeof *rc);
        for (i = 0; i < (size_t)n; i++)
            for (k = 0; k < (size_t)n; k++) {
                int64_t v = ai[i * n + k];
                for (j = 0; j < (size_t)n; j++) rc[i * n + j] += v * bi[k * n + j];
            }
        free(ai);
        free(bi);
        return 0;
    }
    case CP_FMA_SP: case CP_FMA_DP:
        r->n = small ? 1000 : (int)FMA_ITERS;
        return 0;
    case CP_FFT_SP: case CP_FFT_DP: {
        int n = small ? FFT_N_SMALL : FFT_N, dbl = test == CP_FFT_DP, lg = ilog2(n), b;
        double *cs;
        r->n = n;
        r->in[0] = xalloc((size_t)n * 2 * (dbl ? 8 : 4));     /* input */
        r->in[1] = xalloc((size_t)n * (dbl ? 8 : 4));         /* twiddles, n/2 complex */
        r->rev = malloc((size_t)n * sizeof *r->rev);
        cs = malloc((size_t)n * 2 * sizeof *cs);              /* exp(-2 pi i k / n), all k */
        if (!r->in[0] || !r->in[1] || !r->rev || !cs) { free(cs); ref_free(r); return -1; }
        for (i = 0; i < (size_t)n; i++) {
            double a = 2 * M_PI * (double)i / n;
            cs[2 * i] = cos(a);
            cs[2 * i + 1] = -sin(a);
        }
        for (i = 0; i < (size_t)n; i++) {
            unsigned v = 0;
            for (b = 0; b < lg; b++) if (i >> b & 1) v |= 1u << (lg - 1 - b);
            r->rev[i] = (int)v;
        }
        for (i = 0; i < (size_t)n / 2; i++) {
            if (dbl) { ((double *)r->in[1])[2 * i] = cs[2 * i]; ((double *)r->in[1])[2 * i + 1] = cs[2 * i + 1]; }
            else { ((float *)r->in[1])[2 * i] = (float)cs[2 * i]; ((float *)r->in[1])[2 * i + 1] = (float)cs[2 * i + 1]; }
        }
        r->norm2 = 0;
        for (i = 0; i < (size_t)n * 2; i++) {
            double v = lcg(&seed);
            if (dbl) ((double *)r->in[0])[i] = v;
            else { ((float *)r->in[0])[i] = (float)v; v = (float)v; }
            r->norm2 += v * v;
        }
        /* reference bins by direct DFT in double: 0, 1, n/2, n-1 and 12 more */
        for (k = 0; k < 16; k++) {
            size_t bin = k == 0 ? 0 : k == 1 ? 1 : k == 2 ? (size_t)n / 2 : k == 3 ? (size_t)n - 1 : lcg_u(&seed) % (uint32_t)n;
            double sr = 0, si = 0;
            r->bins[k] = (int)bin;
            for (j = 0; j < (size_t)n; j++) {
                size_t m = bin * j % (size_t)n;
                double xr = dbl ? ((double *)r->in[0])[2 * j] : ((float *)r->in[0])[2 * j];
                double xi = dbl ? ((double *)r->in[0])[2 * j + 1] : ((float *)r->in[0])[2 * j + 1];
                sr += xr * cs[2 * m] - xi * cs[2 * m + 1];
                si += xr * cs[2 * m + 1] + xi * cs[2 * m];
            }
            r->bin_re[k] = sr;
            r->bin_im[k] = si;
        }
        free(cs);
        return 0;
    }
    case CP_NBODY: {
        int n = small ? NB_N_SMALL : NB_N;
        double *p[7];
        r->n = n;
        r->in[0] = xalloc((size_t)n * 4 * sizeof(double));    /* x y z m, one after the other */
        r->ref = xalloc((size_t)n * 3 * sizeof(double));      /* ax ay az */
        if (!r->in[0] || !r->ref) { ref_free(r); return -1; }
        for (i = 0; i < 4; i++) p[i] = (double *)r->in[0] + i * n;
        for (i = 0; i < 3; i++) p[4 + i] = (double *)r->ref + i * n;
        for (i = 0; i < (size_t)n; i++) {
            p[0][i] = lcg(&seed); p[1][i] = lcg(&seed); p[2][i] = lcg(&seed);
            p[3][i] = 1.0 + 0.5 * lcg(&seed);
        }
        nbody_ref(p, n);
        return 0;
    }
    case CP_MANDEL:
        r->n = small ? 160 : MB_W;
        r->h = small ? 120 : MB_H;
        r->ref = xalloc((size_t)r->n * r->h);
        if (!r->ref) return -1;
#ifdef CP_X87
        mandel(cp_isa_ok(CP_ISA_SSE2) ? CP_ISA_SSE2 : CP_ISA_C, r->ref, r->n, r->h);
#else
        mandel_c(r->ref, r->n, r->h);
#endif
        return 0;
    case CP_RAY:
        r->n = small ? 80 : RT_W;
        r->h = small ? 60 : RT_H;
        r->ref = xalloc((size_t)r->n * r->h * 3);
        if (!r->ref) return -1;
        ray_d(r->ref, r->n, r->h);
        return 0;
    }
    return -1;
}

static void work_free(cp_work *w)
{
    int i;
    for (i = 0; i < 4; i++) free(w->in[i]);
    for (i = 0; i < 3; i++) free(w->out[i]);
    memset(w, 0, sizeof *w);
}

static void *dup_or_null(const void *p, size_t n, int *bad)
{
    void *q;
    if (!p) return NULL;
    q = xalloc(n);
    if (!q) { *bad = 1; return NULL; }
    memcpy(q, p, n);
    return q;
}

/* the thread's own copy of the inputs, and its output buffers. 0 ok, -1 out of memory */
static int work_make(cp_work *w, const cp_ref *r, int isa)
{
    int bad = 0, i;
    size_t nn = (size_t)r->n * r->n, es;
    memset(w, 0, sizeof *w);
    w->test = r->test; w->isa = isa; w->n = r->n; w->h = r->h; w->r = r;
    switch (r->test) {
    case CP_SGEMM: case CP_DGEMM:
        es = r->test == CP_DGEMM ? 8 : 4;
        w->in[0] = dup_or_null(r->in[0], nn * es, &bad);
        w->in[1] = dup_or_null(r->in[1], nn * es, &bad);
        w->out[0] = xalloc(nn * es);
        break;
    case CP_FMA_SP: case CP_FMA_DP:
        w->out[0] = xalloc(FMA_ACC * 16 * sizeof(double));
        if (w->out[0])
            for (i = 0; i < FMA_ACC * 16; i++) {
                if (r->test == CP_FMA_DP) ((double *)w->out[0])[i] = fma_start(i);
                else ((float *)w->out[0])[i] = (float)fma_start(i);
            }
        break;
    case CP_FFT_SP: case CP_FFT_DP:
        es = r->test == CP_FFT_DP ? 8 : 4;
        w->in[0] = dup_or_null(r->in[0], (size_t)r->n * 2 * es, &bad);
        w->in[1] = dup_or_null(r->in[1], (size_t)r->n * es, &bad);
        w->in[2] = dup_or_null(r->rev, (size_t)r->n * sizeof(int), &bad);
        w->out[0] = xalloc((size_t)r->n * 2 * es);
        break;
    case CP_NBODY:
        w->in[0] = dup_or_null(r->in[0], (size_t)r->n * 4 * sizeof(double), &bad);
        w->out[0] = xalloc((size_t)r->n * 3 * sizeof(double));
        break;
    case CP_MANDEL:
        w->out[0] = xalloc((size_t)r->n * r->h);
        break;
    case CP_RAY:
        w->out[0] = xalloc((size_t)r->n * r->h * 3);
        break;
    }
    if (bad || !w->out[0]) { work_free(w); return -1; }
    return 0;
}

static size_t work_out_bytes(const cp_work *w)
{
    switch (w->test) {
    case CP_SGEMM: return (size_t)w->n * w->n * 4;
    case CP_DGEMM: return (size_t)w->n * w->n * 8;
    case CP_FMA_SP: return FMA_ACC * 16 * 4;
    case CP_FMA_DP: return FMA_ACC * 16 * 8;
    case CP_FFT_SP: return (size_t)w->n * 2 * 4;
    case CP_FFT_DP: return (size_t)w->n * 2 * 8;
    case CP_NBODY: return (size_t)w->n * 3 * 8;
    case CP_MANDEL: return (size_t)w->n * w->h;
    case CP_RAY: return (size_t)w->n * w->h * 3;
    }
    return 0;
}

/* one round of work; returns the operations done (floating-point operations or pixels) */
static double work_run(cp_work *w)
{
    int b, n = w->n;
    switch (w->test) {
    case CP_SGEMM: gemm_f(w->isa, w->in[0], w->in[1], w->out[0], n); return 2.0 * n * n * n;
    case CP_DGEMM: gemm_d(w->isa, w->in[0], w->in[1], w->out[0], n); return 2.0 * n * n * n;
    case CP_FMA_SP: return fma_run(w->isa, 0, w->out[0], n);
    case CP_FMA_DP: return fma_run(w->isa, 1, w->out[0], n);
    case CP_FFT_SP:
        for (b = 0; b < FFT_BATCH; b++) fft_f(w->in[0], w->out[0], w->in[1], w->in[2], n);
        return 5.0 * n * ilog2(n) * FFT_BATCH;
    case CP_FFT_DP:
        for (b = 0; b < FFT_BATCH; b++) fft_d(w->in[0], w->out[0], w->in[1], w->in[2], n);
        return 5.0 * n * ilog2(n) * FFT_BATCH;
    case CP_NBODY: {
        double *p[7];
        int i;
        for (i = 0; i < 4; i++) p[i] = (double *)w->in[0] + (size_t)i * n;
        for (i = 0; i < 3; i++) p[4 + i] = (double *)w->out[0] + (size_t)i * n;
        nbody(w->isa, p, n);
        return 20.0 * n * n;
    }
    case CP_MANDEL: mandel(w->isa, w->out[0], n, w->h); return (double)n * w->h;
    case CP_RAY: ray_f(w->out[0], n, w->h); return (double)n * w->h;
    }
    return 0;
}

/* check the output against the reference; 1 = right, 0 = wrong (why says how) */
static int work_check(const cp_work *w, char *why, size_t wn)
{
    const cp_ref *r = w->r;
    size_t i, nn = (size_t)w->n * w->n, bad = 0;
    switch (w->test) {
    case CP_SGEMM: case CP_DGEMM: {
        const int64_t *rc = r->ref;
        for (i = 0; i < nn; i++) {
            double v = w->test == CP_DGEMM ? ((double *)w->out[0])[i] : ((float *)w->out[0])[i];
            if (v * 8 != (double)rc[i]) bad++;
        }
        if (bad) snprintf(why, wn, "wrong: %zu of %zu matrix elements differ", bad, nn);
        return !bad;
    }
    case CP_FMA_SP: case CP_FMA_DP:
        for (i = 0; i < FMA_ACC * 16; i++) {
            double v = w->test == CP_FMA_DP ? ((double *)w->out[0])[i] : ((float *)w->out[0])[i];
            if (v != fma_start((int)i)) bad++;
        }
        if (bad) snprintf(why, wn, "wrong: %zu of %d lanes did not return to their start value", bad, FMA_ACC * 16);
        return !bad;
    case CP_FFT_SP: case CP_FFT_DP: {
        int dbl = w->test == CP_FFT_DP, n = w->n;
        double tol = (dbl ? 1e-12 : 2e-5) * sqrt((double)n) * ilog2(n), e2 = 0, worst = 0;
        for (i = 0; i < (size_t)n * 2; i++) {
            double v = dbl ? ((double *)w->out[0])[i] : ((float *)w->out[0])[i];
            e2 += v * v;
        }
        for (i = 0; i < 16; i++) {
            size_t b = (size_t)r->bins[i];
            double vr = dbl ? ((double *)w->out[0])[2 * b] : ((float *)w->out[0])[2 * b];
            double vi = dbl ? ((double *)w->out[0])[2 * b + 1] : ((float *)w->out[0])[2 * b + 1];
            double e = hypot(vr - r->bin_re[i], vi - r->bin_im[i]);
            if (!(e <= worst)) worst = e;
        }
        if (!(worst <= tol)) { snprintf(why, wn, "wrong: an output bin is off by %.3g (allowed %.3g)", worst, tol); return 0; }
        /* Parseval: sum |X|^2 = n sum |x|^2 */
        if (!(fabs(e2 / (n * r->norm2) - 1) <= (dbl ? 1e-10 : 1e-4))) {
            snprintf(why, wn, "wrong: output energy is off by a factor %.6g", e2 / (n * r->norm2));
            return 0;
        }
        return 1;
    }
    case CP_NBODY: {
        const double *a = w->out[0], *b = r->ref;
        double big = 0, worst = 0;
        for (i = 0; i < (size_t)w->n * 3; i++) {
            if (fabs(b[i]) > big) big = fabs(b[i]);
            if (!(fabs(a[i] - b[i]) <= worst)) worst = fabs(a[i] - b[i]);
        }
        if (!(worst <= 1e-9 * big)) { snprintf(why, wn, "wrong: an acceleration is off by %.3g (allowed %.3g)", worst, 1e-9 * big); return 0; }
        return 1;
    }
    case CP_MANDEL: {
        const unsigned char *a = w->out[0], *b = r->ref;
        size_t np = (size_t)w->n * w->h;
        for (i = 0; i < np; i++) bad += a[i] != b[i];
#ifdef CP_X87
        if (w->isa == CP_ISA_C && bad <= np / 100) bad = 0;   /* x87 keeps extra precision: edge pixels may differ */
#endif
        if (bad) { snprintf(why, wn, "wrong: %zu of %zu pixels differ from the reference", bad, np); return 0; }
        return 1;
    }
    case CP_RAY: {
        const unsigned char *a = w->out[0], *b = r->ref;
        size_t np = (size_t)w->n * w->h;
        for (i = 0; i < np; i++) {
            int k, d = 0;
            for (k = 0; k < 3; k++) { int e = abs((int)a[3 * i + k] - (int)b[3 * i + k]); if (e > d) d = e; }
            bad += d > 3;
        }
        /* single vs double precision: only pixels right on an edge may flip */
        if (bad > np / 100) { snprintf(why, wn, "wrong: %zu of %zu pixels differ from the double-precision image", bad, np); return 0; }
        return 1;
    }
    }
    snprintf(why, wn, "unknown test");
    return 0;
}

/* ======================================================================
 * self-test: small sizes, every code path this CPU has
 * ====================================================================== */
int cp_selftest(char *msg, size_t mn)
{
    int t, isa, paths = 0;
    char why[128];
    for (t = 0; t < CP_NTEST; t++) {
        cp_ref r;
        if (ref_make(&r, t, 1)) { snprintf(msg, mn, "out of memory"); return 0; }
        if (t == CP_RAY) {   /* the reference image itself: sky at the top, the red ball in the middle */
            const unsigned char *p = r.ref, *c = p + 3 * ((size_t)(r.h / 2) * r.n + r.n / 2);
            if (!(p[2] > p[0] && c[0] > c[1] && c[0] > c[2])) {
                snprintf(msg, mn, "Ray tracer: the reference picture is wrong");
                ref_free(&r);
                return 0;
            }
        }
        for (isa = CP_ISA_C; isa < CP_NISA; isa++) {
            cp_work w;
            int ok;
            if (!cp_isa_ok(isa) || (!has_vector(t) && isa != CP_ISA_C)) continue;
            if (work_make(&w, &r, isa)) { ref_free(&r); snprintf(msg, mn, "out of memory"); return 0; }
            work_run(&w);
            ok = work_check(&w, why, sizeof why);
            work_free(&w);
            if (!ok) {
                snprintf(msg, mn, "%s, %s: %s", cp_test_name[t], cp_isa_name(isa), why);
                ref_free(&r);
                return 0;
            }
            paths++;
        }
        ref_free(&r);
    }
    snprintf(msg, mn, "all %d kernel and code-path combinations give the right answer (best path: %s)", paths, cp_isa_name(cp_isa_best()));
    return 1;
}

/* ======================================================================
 * timed runs
 * ====================================================================== */
static const char *volatile g_stage = "";
static char g_stage_buf[2][128];
static int g_stage_i;
const char *cp_stage(void) { return g_stage; }

static void set_stage(int test, int threads)
{
    char *b = g_stage_buf[g_stage_i ^= 1];
    snprintf(b, sizeof g_stage_buf[0], "%s: %d thread%s", cp_test_name[test], threads, threads == 1 ? "" : "s");
    g_stage = b;
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* start gate: every thread prepares its data, then all start timing together */
typedef struct { pthread_mutex_t mu; pthread_cond_t cv; int ready, go; } cp_gate;

typedef struct {
    const cp_ref *r;
    int isa, cpu, ok;
    double secs, rate;
    volatile int *cancel;
    cp_gate *g;
    char why[96];
    pthread_t t;
} cjob;

static void gate_wait(cp_gate *g)
{
    pthread_mutex_lock(&g->mu);
    g->ready++;
    pthread_cond_broadcast(&g->cv);
    while (!g->go) pthread_cond_wait(&g->cv, &g->mu);
    pthread_mutex_unlock(&g->mu);
}

static void *cjob_run(void *arg)
{
    cjob *j = arg;
    cp_work w;
    cpu_set_t s;
    double t0, t1, start, ops = 0, kt = 0;
    uint64_t sum0 = 0;
    long rep = 0;
    CPU_ZERO(&s);
    CPU_SET(j->cpu, &s);
    pthread_setaffinity_np(pthread_self(), sizeof s, &s);
    if (work_make(&w, j->r, j->isa)) {
        j->ok = 0;
        snprintf(j->why, sizeof j->why, "out of memory");
        gate_wait(j->g);
        return NULL;
    }
    gate_wait(j->g);
    start = now_s();
    do {   /* only the kernel is timed; the checks run between rounds */
        t0 = now_s();
        ops += work_run(&w);
        t1 = now_s();
        kt += t1 - t0;
        if (!rep) {
            if (!work_check(&w, j->why, sizeof j->why)) { j->ok = 0; break; }
            sum0 = hash_bytes(w.out[0], work_out_bytes(&w));
        } else if (hash_bytes(w.out[0], work_out_bytes(&w)) != sum0) {
            j->ok = 0;
            snprintf(j->why, sizeof j->why, "wrong: round %ld gave a different result than round 1", rep + 1);
            break;
        }
        rep++;
    } while (t1 - start < j->secs && !(j->cancel && *j->cancel));
    j->rate = kt > 0 ? ops / kt : 0;
    work_free(&w);
    return NULL;
}

/* total rate over nthreads pinned threads (ops/s); *ok = 0 when a check failed (why says why) */
static double cp_measure(const cp_ref *r, int isa, int nthreads, double secs, volatile int *cancel, int *ok, char *why, size_t wn)
{
    cjob *js = calloc(BURN_MAXCPU, sizeof *js);
    cp_gate g = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0 };
    double sum = 0;
    int i, n = 0, started = 0;
    cpu_set_t saved;
    *ok = 1;
    if (!js) { *ok = 0; snprintf(why, wn, "out of memory"); return 0; }
    CPU_ZERO(&saved);
    sched_getaffinity(0, sizeof saved, &saved);
    for (i = 0; i < BURN_MAXCPU && n < nthreads; i++) if (CPU_ISSET(i, &saved)) js[n++].cpu = i;
    if (!n) js[n++].cpu = 0;
    for (i = 0; i < n; i++) {
        js[i].r = r; js[i].isa = isa; js[i].secs = secs; js[i].cancel = cancel; js[i].g = &g; js[i].ok = 1;
        if (pthread_create(&js[i].t, NULL, cjob_run, &js[i])) {
            js[i].ok = 0;
            snprintf(js[i].why, sizeof js[i].why, "could not start thread %d", i + 1);
            break;
        }
        started++;
    }
    pthread_mutex_lock(&g.mu);
    while (g.ready < started) pthread_cond_wait(&g.cv, &g.mu);
    g.go = 1;
    pthread_cond_broadcast(&g.cv);
    pthread_mutex_unlock(&g.mu);
    for (i = 0; i < started; i++) pthread_join(js[i].t, NULL);
    for (i = 0; i < n; i++) {
        if (!js[i].ok) { if (*ok) snprintf(why, wn, "%s", js[i].why); *ok = 0; }
        sum += js[i].rate;
    }
    pthread_mutex_destroy(&g.mu);
    pthread_cond_destroy(&g.cv);
    free(js);
    return sum;
}

typedef struct { cp_result r; double secs_total; } cpctx;
static cp_result g_last;
static int g_have_last;
const cp_result *cp_last(void) { return g_have_last ? &g_last : NULL; }

static void cp_render(srs_out *o, void *c)
{
    static const int tabs[] = { 320, 450, 580 };
    cpctx *x = c;
    const cp_result *r = &x->r;
    char h2[32];
    int t;
    so_siv_header(o, "CPU compute benchmark (floating-point work, GFLOPS and Mpixels/s)");
    so_tabs(o, tabs, 3);
    snprintf(h2, sizeof h2, "%d threads", r->threads);
    so_row(o, 4, (const char *const[]){ "Test", "1 thread", r->single_only ? "" : h2, "Code" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (t = 0; t < CP_NTEST; t++) {
        const cp_cell *e = &r->t[t];
        char a[40], b[40];
        int col = C_GREEN;
        if (r->run_mask && !(r->run_mask >> t & 1)) continue;
        if (e->state == CP_ST_OK || (e->state == CP_ST_NOTRUN && e->v[0] > 0)) {
            snprintf(a, sizeof a, e->v[0] > 0 ? "%.1f %s" : "-", e->v[0], cp_unit[t]);
            snprintf(b, sizeof b, e->v[1] > 0 ? "%.1f %s" : "-", e->v[1], cp_unit[t]);
        } else if (e->state == CP_ST_FAIL) {
            snprintf(a, sizeof a, "WRONG result");
            snprintf(b, sizeof b, " ");
            col = C_RED;
        } else {
            snprintf(a, sizeof a, "%s", e->state == CP_ST_SKIP ? "skipped" : "not measured");
            snprintf(b, sizeof b, " ");
            col = C_GRAY;
        }
        so_row(o, 4, (const char *const[]){ cp_test_name[t], a, r->single_only ? "" : b, cp_isa_name(e->isa ? e->isa : CP_ISA_C) },
               (const int[]){ C_LABEL, col, col, C_TEAL });
        if (*e->note) { so_t(o, e->state == CP_ST_FAIL ? C_RED : C_GRAY, e->note); so_par(o); }
    }
    so_par(o);
    so_tabs(o, tabs, 1);
    if (r->cancelled) { so_err(o, "Stopped before the end - the rows marked \"not measured\" were not run."); so_par(o); }
    so_kv(o, "Check", r->self_ok && !r->failed ? C_GREEN : C_RED, r->self_ok && r->failed ? "a timed result was wrong (see the red rows)" : r->self);
    so_kvf(o, "Run", C_GRAY, "%s, %.1f s per measurement, %.0f s in all", r->when, r->seconds, x->secs_total);
    so_kv(o, "Build", C_GRAY, bench_build_tag());
    so_note(o, "GFLOPS = billion floating-point operations per second (a multiply-add counts as 2). Matrix multiply: "
               "288 x 288 matrices, 2 n^3 operations. FFT: 5 n log2 n operations per transform. N-body: 20 operations "
               "per pair of bodies. Mandelbrot: up to 255 steps per pixel. Ray tracer: 4 balls on a floor, shadows and "
               "two reflections. Mpixels/s = million pixels per second.");
    so_note(o, "Every result is checked; each thread works on its own copy. \"Code\" is the instruction set used: the "
               "best one this CPU has is picked when the test runs.");
    so_board_footer(o);
}

void cp_cfg_default(cp_cfg *c)
{
    memset(c, 0, sizeof *c);
    c->seconds = 0.5;
    c->isa = CP_ISA_AUTO;
}

int cp_available(void) { return 1; }

double cp_estimate_seconds(const cp_cfg *c)
{
    int t, n = 0;
    double s = c->seconds < 0.1 ? 0.1 : c->seconds > 60 ? 60 : c->seconds;
    for (t = 0; t < CP_NTEST; t++) if (!c->run_mask || (c->run_mask >> t & 1)) n++;
    return 0.5 + n * (s + 0.1) * (c->single_only ? 1 : 2);
}

int bench_compute(const cp_cfg *cfg, srs_out *o, volatile int *cancel)
{
    static cpctx x;
    cp_cfg c;
    cp_result *r = &x.r;
    char params[128], res[600];
    double t_all = now_s();
    time_t now = time(NULL);
    int t, k, len;
    if (cfg) c = *cfg; else cp_cfg_default(&c);
    if (burn_active()) return -1;
    if (c.threads < 1 || c.threads > burn_ncpu()) c.threads = burn_ncpu();
    if (!(c.seconds >= 0.1)) c.seconds = 0.1;
    if (c.seconds > 60) c.seconds = 60;
    c.run_mask &= (1u << CP_NTEST) - 1;
    memset(&x, 0, sizeof x);
    r->threads = c.threads;
    r->single_only = c.single_only;
    r->seconds = c.seconds;
    r->run_mask = c.run_mask;
    strftime(r->when, sizeof r->when, "%Y-%m-%d %H:%M:%S", localtime(&now));
    g_stage = "Self-test";
    r->self_ok = cp_selftest(r->self, sizeof r->self);
    for (t = 0; t < CP_NTEST && r->self_ok; t++) {
        cp_cell *e = &r->t[t];
        cp_ref ref;
        if (c.run_mask && !(c.run_mask >> t & 1)) continue;
        e->isa = isa_for(t, c.isa);
        if (cancel && *cancel) { r->cancelled = 1; break; }
        if (c.cb) c.cb(r, 0, c.ud);
        if (ref_make(&ref, t, 0)) { e->state = CP_ST_SKIP; snprintf(e->note, sizeof e->note, "not enough memory"); continue; }
        for (k = 0; k < (c.single_only ? 1 : 2); k++) {
            int ok, n = k ? c.threads : 1;
            set_stage(t, n);
            e->v[k] = cp_measure(&ref, e->isa, n, c.seconds, cancel, &ok, e->note, sizeof e->note) / cp_scale[t];
            if (!ok) { e->state = CP_ST_FAIL; e->v[0] = e->v[1] = 0; r->failed++; break; }
            if (cancel && *cancel) { r->cancelled = 1; e->v[k] = 0; break; }
        }
        ref_free(&ref);
        if (e->state != CP_ST_FAIL && !r->cancelled) e->state = CP_ST_OK;
        if (c.cb) c.cb(r, 1, c.ud);
        if (r->cancelled) break;
    }
    g_stage = "";
    x.secs_total = now_s() - t_all;
    bench_save_render("compute", cp_render, &x, o);
    snprintf(params, sizeof params, "threads=%d seconds=%.1f code=%s%s", c.threads, c.seconds, cp_isa_id(c.isa), c.single_only ? " single" : "");
    len = 0;
    res[0] = 0;
    for (t = 0; t < CP_NTEST && len < (int)sizeof res - 64; t++) {
        const cp_cell *e = &r->t[t];
        if (e->state == CP_ST_OK) len += snprintf(res + len, sizeof res - len, "%s%s %.1f/%.1f", len ? " " : "", cp_test_id[t], e->v[0], e->v[1]);
        else if (e->state == CP_ST_FAIL) len += snprintf(res + len, sizeof res - len, "%s%s WRONG", len ? " " : "", cp_test_id[t]);
    }
    if (!r->self_ok) snprintf(res + len, sizeof res - len, "%sSELFTEST-FAILED", len ? " " : "");
    else if (r->cancelled) snprintf(res + len, sizeof res - len, "%sSTOPPED", len ? " " : "");
    bench_history_add("compute", params, res);
    g_last = *r;
    g_have_last = 1;
    return r->cancelled ? -2 : (!r->self_ok || r->failed) ? 1 : 0;
}

#else   /* not Linux: the Windows images do not run it; these stubs only keep the API linkable */
const char *cp_stage(void) { return ""; }
int cp_available(void) { return 0; }
const cp_result *cp_last(void) { return NULL; }
void cp_cfg_default(cp_cfg *c) { memset(c, 0, sizeof *c); c->seconds = 0.5; c->isa = CP_ISA_AUTO; }
double cp_estimate_seconds(const cp_cfg *c) { (void)c; return 0; }
int cp_selftest(char *msg, size_t mn) { snprintf(msg, mn, "Linux only"); return 0; }
int bench_compute(const cp_cfg *c, srs_out *o, volatile int *cancel)
{
    (void)c; (void)cancel;
    if (o) so_err(o, "CPU Compute: not available on this system (Linux only)");
    return -1;
}
#endif
