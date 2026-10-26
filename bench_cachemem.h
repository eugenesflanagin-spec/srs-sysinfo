/* bench_cachemem.h - Cache & Memory benchmark engine (from Linux System Audit 0.2.0, the same author's
 * project, merged into SRS 2.2).
 * Written independently from the published description of a cache & memory benchmark
 * (Read / Write / Copy bandwidth and Latency of Memory, L1, L2, L3), not from any other
 * program's code. Bandwidth tests run one pinned thread per logical CPU; Latency is a
 * single-thread random pointer chase on 2 MB (transparent huge) pages. */
#ifndef BENCH_CACHEMEM_H
#define BENCH_CACHEMEM_H

enum { CM_MEM, CM_L1, CM_L2, CM_L3, CM_NLEVEL };
enum { CM_READ, CM_WRITE, CM_COPY, CM_LAT, CM_NTEST };
enum { CM_MW_AUTO, CM_MW_STREAM, CM_MW_CLZERO };
enum { CM_ISA_AUTO, CM_ISA_SSE2, CM_ISA_AVX2, CM_ISA_AVX512 };

typedef struct {
    int threads;          /* 0 = all logical CPUs */
    double seconds;       /* measuring time per test (default 1.0) */
    int isa;              /* CM_ISA_* */
    long mem_mib;         /* memory block, MiB, all threads together (0 = auto: 1024, capped by MemAvailable/4) */
    unsigned run_mask;    /* bit (level * CM_NTEST + test); 0 = everything */
    double mb;            /* bytes per "MB" in results (default 1e6) */
    int mem_write;        /* CM_MW_AUTO: CLZERO on AMD CPUs that have it, else streaming stores */
} cm_cfg;

typedef struct {
    double v[CM_NLEVEL][CM_NTEST];   /* MB/s for Read/Write/Copy, ns for Latency; 0 = not run */
    int threads, isa;
    long cache_kib[4];               /* L1d, L2, L3 per instance (index 1..3), from sysfs */
    double clock_mhz;                /* measured core clock during the run */
    int mem_write;                   /* method used for Memory Write (CM_MW_STREAM / CM_MW_CLZERO) */
} cm_result;

/* called after every finished (or starting, when done == 0) cell; may run on a worker thread */
typedef void (*cm_cb)(int level, int test, int done, const cm_result *r, void *ud);


void cm_cfg_default(cm_cfg *c);
int  cm_run(const cm_cfg *c, cm_result *r, cm_cb cb, void *ud, volatile int *cancel);  /* 0 ok, 1 cancelled, -1 error */
double cm_measure_mhz(void);
/* "65369 MB/s" for Memory; caches in GB/s: "2520.3 GB/s", "695.57 GB/s" (v is in MB/s) */
void cm_format_bw(char *b, unsigned long n, int level, double mbs, int mib);
const char *cm_isa_name(int isa);
/* "DRAM:FSB 30:1" (whole ratio) or "DRAM:FSB 28.6:1" (memory clock / FSB, one decimal) */
void cm_dram_ratio(char *b, unsigned long n, double mem_mhz, double fsb_mhz);
int  cm_isa_best(void);
int  cm_have_clzero(void);
int  cm_large_pages(void);   /* 1 when 2 MB pages are available for the latency test */
extern const char *const cm_level_name[CM_NLEVEL], *const cm_test_name[CM_NTEST];


/* ---- SRS side (bench_cachemem.c) ---- */
#include "sec.h"
int  cm_available(void);                 /* 1 on x86-64 (the engine uses x86-64 vector code) */
const char *cm_stage(void);              /* "L2 cache Copy" while a run is going on */
/* run, render into o (may be NULL) and ~/.cache/srs/bench-cachemem.txt, add a history line.
   0 ok, 1 cancelled, -1 could not allocate / not available */
int  bench_cachemem(const cm_cfg *c, srs_out *o, volatile int *cancel);

#endif
