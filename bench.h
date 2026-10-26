/* bench.h - SIV-style CPU benchmarks and micro-measurements (run only on explicit request).
 *  - Dhrystone 2.1 (DMIPS) and Whetstone (MWIPS) per CPU, in SIV's modes
 *  - IPI / core-to-core latency matrix (cache-line ping-pong between pinned threads)
 *  - memory latency by block size (dependent pointer chase)
 * Results are saved to ~/.cache/srs/bench-<name>.txt so -save and the GUI can show the
 * last run without re-running anything. */
#ifndef BENCH_H
#define BENCH_H

#include "sec.h"
#include <stddef.h>
#include <stdint.h>

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
int bench_ecores(unsigned char *is_e, int ncpu);                 /* hybrid CPUs: E-core map, returns count */
int bench_show_last(const char *name, srs_out *o);             /* render cached result, 0 if found */
/* last measured data for the GUI plots (valid after a run in this process) */
const double *bench_ipi_last(int *n);                           /* n x n matrix, ns */
int bench_latency_last(const long **sizes, const double **ns);  /* returns count */


/* ---- shared by the benchmarks ---- */
/* render through fn into ~/.cache/srs/bench-<name>.txt, and into o when o is not NULL */
void bench_save_render(const char *name, void (*fn)(srs_out *, void *), void *ctx, srs_out *o);
/* one line per run in ~/.cache/srs/bench-history.tsv: time, test, settings, result, build */
void bench_history_add(const char *test, const char *params, const char *result);
/* "gcc 15.2.1, x86-64-v3 (AVX2), optimised": results only compare between alike builds */
const char *bench_build_tag(void);

/* ---- SRS Pi (bench_pi.c): pi to N decimal places, every digit checked ---- */
enum { PI_CHUDNOVSKY, PI_AGM };
#define PI_NSIZES 7
#define PI_MAXDIGITS 32000000
extern const size_t pi_sizes[PI_NSIZES];          /* the sizes with a full checksum: 128K .. 32M */
/* 0 = right, 1 = wrong digits, -1 bad size, -2 cancelled, -3 out of memory.
   digits_out (optional) receives the malloc'd digit string (decimal places only). */
int bench_pi(size_t digits, int threads, int algo, srs_out *o, volatile int *cancel, char **digits_out);
const char *pi_stage(void);                         /* what the running calculation is doing */

/* ---- Crypto (bench_crypto.c): AES-128-CTR and SHA-256, plain C and AES-NI / SHA-NI ---- */
int  bench_crypto(int threads, double secs, srs_out *o);   /* 0 ok, 1 self-test failed */
int  crypto_selftest(char *msg, size_t mn);                /* 1 ok */
int  crypto_have_aesni(void);
int  crypto_have_shani(void);
void sha256_hex(const void *data, size_t len, char hex[65]);

/* ---- Prime sieve (bench_prime.c): count primes <= n, checked against pi(10^k) ---- */
int bench_prime(uint64_t n, int threads, srs_out *o, volatile int *cancel);   /* 0 ok, 1 wrong, -1 bad, -2 cancelled */

#endif
