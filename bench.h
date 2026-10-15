/* bench.h - SIV-style CPU benchmarks and micro-measurements (run only on explicit request).
 *  - Dhrystone 2.1 (DMIPS) and Whetstone (MWIPS) per CPU, in SIV's modes
 *  - IPI / core-to-core latency matrix (cache-line ping-pong between pinned threads)
 *  - memory latency by block size (dependent pointer chase)
 * Results are saved to ~/.cache/srs/bench-<name>.txt so -save and the GUI can show the
 * last run without re-running anything. */
#ifndef BENCH_H
#define BENCH_H

#include "sec.h"

typedef enum {
    BM_AC, BM_AS, BM_DD, BM_DW, BM_EC, BM_EP, BM_LC, BM_LP, BM_PC, BM_PP, BM_COUNT
} bench_mode;

const char *bench_mode_id(bench_mode m);     /* "benchmark-ac" ... */
const char *bench_mode_desc(bench_mode m);
double dhrystone_dmips(long runs, double *secs);

/* run a benchmark mode for ~secs_per_cpu seconds per measurement; writes the result file and
   renders it into o (o may be NULL). Returns 0 ok, -1 nothing to run (e.g. no E-cores). */
int bench_run(bench_mode m, double secs_per_cpu, srs_out *o);
int bench_ipi(int max_cpus, int rounds, srs_out *o);          /* core-to-core latency matrix */
int bench_latency(long max_bytes, srs_out *o);                 /* memory latency by block size */
int bench_show_last(const char *name, srs_out *o);             /* render cached result, 0 if found */
/* last measured data for the GUI plots (valid after a run in this process) */
const double *bench_ipi_last(int *n);                           /* n x n matrix, ns */
int bench_latency_last(const long **sizes, const double **ns);  /* returns count */

#endif
