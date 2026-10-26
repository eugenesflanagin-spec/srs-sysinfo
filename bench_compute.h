/* bench_compute.h - CPU compute benchmark: floating-point number crunching.
 *  - matrix multiply, single and double precision (SGEMM / DGEMM, 288 x 288), GFLOPS
 *  - peak multiply-add throughput, single and double precision, GFLOPS
 *  - FFT: 4096-point complex radix-2, single and double precision, GFLOPS (5 N log2 N per transform)
 *  - N-body gravity: 1,024 bodies, double precision, GFLOPS (20 operations per pair)
 *  - Mandelbrot set: 640 x 480, up to 255 steps per pixel, double precision, Mpixels/s
 *  - ray tracer: 320 x 240, spheres on a floor, shadows and up to two reflections, single precision, Mpixels/s
 * Matrix multiply, multiply-add, N-body and Mandelbrot have plain C, SSE2, AVX2 (+FMA) and AVX-512
 * code; the best one the CPU has is picked at run time, so a generic x86-64 package still uses
 * AVX-512. FFT and the ray tracer are plain C. Every measurement runs on 1 thread and on N threads
 * (one pinned thread per logical CPU, each with its own copy of the work), and every result is
 * checked against reference values: a wrong-but-fast result is reported as wrong, not as a score.
 * Linux only (the Linux images build it). Written for SRS. */
#ifndef BENCH_COMPUTE_H
#define BENCH_COMPUTE_H

#include "sec.h"
#include <stddef.h>

enum { CP_SGEMM, CP_DGEMM, CP_FMA_SP, CP_FMA_DP, CP_FFT_SP, CP_FFT_DP, CP_NBODY, CP_MANDEL, CP_RAY, CP_NTEST };
/* code paths: CP_ISA_AUTO = best available; a forced path the CPU lacks falls back to the best */
enum { CP_ISA_AUTO, CP_ISA_C, CP_ISA_SSE2, CP_ISA_AVX2, CP_ISA_AVX512, CP_NISA };
/* state of one result row */
enum { CP_ST_NOTRUN, CP_ST_OK, CP_ST_FAIL, CP_ST_SKIP };

typedef struct {
    int state;          /* CP_ST_* */
    double v[2];        /* 1 thread, N threads, in cp_unit[test] (0 = not measured) */
    int isa;            /* code path used (CP_ISA_C .. CP_ISA_AVX512) */
    char note[96];      /* what failed ("" = nothing to say) */
} cp_cell;

typedef struct {
    cp_cell t[CP_NTEST];
    int threads;        /* N asked for */
    int single_only;    /* 1 = only the 1-thread column was measured */
    double seconds;     /* timed length of each measurement */
    unsigned run_mask;
    int self_ok;        /* self-test result (1 = pass) */
    char self[256];
    int cancelled;
    int failed;         /* rows whose check failed */
    char when[32];
} cp_result;

/* called after every finished row (and once with done == 0 when one starts); runs on the
   benchmark's own thread, so a GUI should copy what it needs and post it to its main loop */
typedef void (*cp_cb)(const cp_result *r, int done, void *ud);

typedef struct {
    int threads;        /* N for the "N threads" column; 0 = all logical CPUs */
    double seconds;     /* timed length of each measurement (default 0.5, 0.1 .. 60) */
    unsigned run_mask;  /* bits 1u << CP_*; 0 = every test */
    int isa;            /* CP_ISA_*; default CP_ISA_AUTO */
    int single_only;    /* 1 = skip the N-thread column */
    cp_cb cb;
    void *ud;
} cp_cfg;

extern const char *const cp_test_name[CP_NTEST];  /* "Matrix multiply, single precision (SGEMM 288 x 288)" */
extern const char *const cp_test_id[CP_NTEST];    /* "sgemm", "dgemm", "fma-sp", ... (CLI words) */
extern const char *const cp_unit[CP_NTEST];       /* "GFLOPS", "Mpixels/s" */
const char *cp_isa_name(int isa);                 /* "AVX-512", "AVX2 + FMA", "SSE2", "plain C" */
const char *cp_isa_id(int isa);                   /* "auto", "c", "sse2", "avx2", "avx512" (CLI words) */
int  cp_isa_best(void);                           /* best path this CPU and this build can run */
int  cp_isa_ok(int isa);                          /* 1 when this CPU can run that path */

void cp_cfg_default(cp_cfg *c);
int  cp_available(void);                 /* 1 on Linux builds */
const char *cp_stage(void);              /* "Matrix multiply, double precision: 16 threads" while running, "" otherwise */
/* every kernel on every code path this CPU has, on small sizes, against reference values.
   One thread, well under a second. 1 = all pass; msg says what passed or what failed. */
int  cp_selftest(char *msg, size_t mn);
double cp_estimate_seconds(const cp_cfg *c);   /* about how long a run takes */
/* run, render into o (may be NULL) and ~/.cache/srs/bench-compute.txt, add a history line.
   0 ok, 1 a check failed (results shown), -1 not available / out of memory / burn-in running,
   -2 stopped (partial results shown) */
int  bench_compute(const cp_cfg *c, srs_out *o, volatile int *cancel);
const cp_result *cp_last(void);          /* result of the last bench_compute() in this process, NULL = none */

#endif
