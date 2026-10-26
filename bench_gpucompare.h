/* bench_gpucompare.h - "GPU Compute: Vulkan vs HIP vs CUDA vs OpenCL" (bench_gpucompare.c), new in SRS 2.2.
 * Runs the SAME workloads, with the same sizes, through every compute back end that is installed and has the
 * chosen GPU, and shows them side by side in one table: rows = tests, columns = back ends, with the fastest
 * marked per row and every column's speed as a percentage of Vulkan.
 *
 * Tests: matrix multiplication (GEMM) in FP32, FP16 and FP64, each with the back end's OWN kernel and with the
 * vendor LIBRARY (hipBLAS, cuBLAS, CLBlast) where there is one; a plain multiply-add chain (FP32, FP64);
 * video-memory copy; upload and download over PCIe; kernel launch round trip. Every result is checked against
 * the same reference as GPU Burnin: a wrong result is shown as an error, never as a speed.
 * OpenCL: every platform is its own column ("OpenCL (ROCm)", "OpenCL (Rusticl)" ...); CUDA that turns out to be
 * ZLUDA is labelled "CUDA (ZLUDA)". Back ends that are not installed say "not installed" with a one-line hint. */
#ifndef BENCH_GPUCOMPARE_H
#define BENCH_GPUCOMPARE_H

#include "gpu_backend.h"
#include "sec.h"

#define GPUCMP_MAXCOL 16

enum { GC_GEMM32_OWN, GC_GEMM32_LIB, GC_GEMM16_OWN, GC_GEMM16_LIB, GC_GEMM64_OWN, GC_GEMM64_LIB,
       GC_FMA32, GC_FMA64, GC_COPY, GC_UPLOAD, GC_DOWNLOAD, GC_LATENCY, GC_N };
extern const char *const gpucmp_row_name[GC_N];     /* "FP32 matrix multiply, own kernel" ... */
extern const char *const gpucmp_row_unit[GC_N];     /* "GFLOPS", "GB/s", "µs" */

enum { CS_NOTRUN, CS_OK, CS_WRONG, CS_NA, CS_FAILED, CS_STOPPED, CS_SKIPPED };   /* skipped: not run because the GPU failed earlier */

typedef struct {
    unsigned backends;         /* bit (1u << GB_*); 0 = all four */
    double seconds;            /* GPU time per test (default 1.0; 0.1 .. 10) */
    int device;                /* Vulkan device index that fixes "the GPU" (other back ends are matched by PCI address); -1 = default */
    int software_only;         /* tests: only software renderers */
    int tiny;                  /* tests: tiny sizes */
} gpucmp_cfg;

typedef struct {
    int shown;                 /* 1 = a real column; 0 = back end not installed / no device (hint says why) */
    int backend;
    gb_info info;
    char hint[200];
    int state[GC_N];
    double v[GC_N];
    char note[GC_N][160];
    char aborted[200];         /* the GPU failed during this column */
} gpucmp_col;

typedef struct {
    gpucmp_col col[GPUCMP_MAXCOL];
    int ncol;
} gpucmp_result;

void gpucmp_cfg_default(gpucmp_cfg *c);
const char *gpucmp_stage(void);      /* "ROCm / HIP: FP32 matrix multiply, library" while running, "" when idle */
/* run, render into o (may be NULL) and ~/.cache/srs/bench-gpucompare.txt, add a history line. r (may be NULL)
   receives the table. 0 ok, 1 stopped, 2 a result was wrong (shown), -1 not started (why says why),
   -2 a GPU failed during the run (table so far shown) */
int  bench_gpucompare(const gpucmp_cfg *c, gpucmp_result *r, srs_out *o, volatile int *cancel, char *why, size_t wn);

#endif
