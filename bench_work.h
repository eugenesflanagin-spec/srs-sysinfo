/* bench_work.h - "Everyday work" CPU benchmarks: the kind of work a PC does all day long.
 *  - compression and decompression: zlib (-1/-6/-9), zstd (-1/-3/-19), lz4, xz (-1/-6), MB/s and
 *    ratio on a fixed generated corpus (text-like, record-like binary, random); the libraries are
 *    the system's own (libz.so.1, libzstd.so.1, liblz4.so.1, liblzma.so.5), loaded at run time,
 *    so SRS needs no new build dependency and a missing library only skips its rows
 *  - sorting: 64-bit integers (C library qsort vs SRS's own radix sort) and strings (qsort vs
 *    SRS's own multikey quicksort), million keys per second
 *  - hashing / checksums: CRC32 (byte table and slicing-by-8), CRC32C (table and the CPU's CRC32
 *    instruction: SSE4.2, in the 64-bit x86 build only; the i686 and aarch64 builds skip that row),
 *    xxHash64 and FNV-1a, GB/s
 *  - text processing: substring search (C library memmem vs SRS's own Boyer-Moore-Horspool),
 *    JSON tokenise + structure check, UTF-8 validation, GB/s
 *  - a tiny bytecode interpreter (branchy integer work), switch and threaded dispatch, Mops/s
 * Every measurement runs on 1 thread and on N threads (one pinned thread per logical CPU, each
 * with its own copy of the work) and every result is checked. The corpus comes from a fixed seed,
 * so two runs (and two PCs) do exactly the same work.
 * Linux only (threads pinned with sched_setaffinity, libraries with dlopen); the Windows build
 * gets stubs that say so. Written for SRS. */
#ifndef BENCH_WORK_H
#define BENCH_WORK_H

#include "sec.h"
#include <stddef.h>

/* test groups (bits of wk_cfg.run_mask) */
enum { WK_COMPRESS, WK_SORT, WK_HASH, WK_TEXT, WK_INTERP, WK_NGROUP };
/* compression libraries (bits of wk_cfg.codec_mask) */
enum { WK_LIB_ZLIB, WK_LIB_ZSTD, WK_LIB_LZ4, WK_LIB_XZ, WK_NLIB };
/* codec rows: library + level */
enum { WC_ZLIB1, WC_ZLIB6, WC_ZLIB9, WC_ZSTD1, WC_ZSTD3, WC_ZSTD19, WC_LZ4, WC_XZ1, WC_XZ6, WK_NCODEC };
/* the other rows */
enum {
    WT_SORT_QSORT_U64, WT_SORT_RADIX_U64, WT_SORT_QSORT_STR, WT_SORT_MKQS_STR,
    WT_CRC32_BYTE, WT_CRC32_SLICE8, WT_CRC32C_TABLE, WT_CRC32C_HW, WT_XXH64, WT_FNV1A,
    WT_FIND_MEMMEM, WT_FIND_BMH, WT_JSON, WT_UTF8,
    WT_VM_SWITCH, WT_VM_THREADED, WT_N
};
/* corpus parts; ratio[..][WK_PART_MIX] is all three together */
enum { WK_PART_TEXT, WK_PART_BIN, WK_PART_RAND, WK_PART_MIX, WK_NPART };
/* state of one result cell */
enum { WK_ST_NOTRUN, WK_ST_OK, WK_ST_FAIL, WK_ST_SKIP };

typedef struct {
    int state;          /* WK_ST_* */
    double v[2];        /* 1 thread, N threads, in the group's unit (0 = not measured) */
    int threads;        /* threads in the N column (lower than asked when memory-capped) */
    char note[96];      /* why it was skipped, or what failed ("" = nothing to say) */
} wk_cell;

typedef struct {
    wk_cell t[WT_N];                    /* sort / hash / text / interpreter rows */
    wk_cell comp[WK_NCODEC], dec[WK_NCODEC];   /* compression rows: compress / decompress MB/s */
    double ratio[WK_NCODEC][WK_NPART];  /* original : compressed (2.5 = 2.5 times smaller), 0 = not run */
    char libver[WK_NLIB][64];           /* "zlib 1.3.1.zlib-ng (libz.so.1)", "" = not loaded */
    char libwhy[WK_NLIB][128];          /* why a library was not loaded */
    int threads;                        /* N asked for */
    int single_only;                    /* 1 = only the 1-thread column was measured */
    double seconds;                     /* timed length of each measurement */
    long part_kib;                      /* size of each corpus part */
    unsigned run_mask, codec_mask;      /* what was asked for */
    int self_ok;                        /* known-answer self-test result (1 = pass) */
    char self[256];
    int cancelled;                      /* stopped before the end */
    int failed;                         /* number of cells whose check failed */
    char when[32];
} wk_result;

/* called after every finished cell (and once with done == 0 when one starts); runs on the
   benchmark's own thread, so a GUI should copy what it needs and post it to its main loop */
typedef void (*wk_cb)(const wk_result *r, int done, void *ud);

typedef struct {
    int threads;        /* N for the "N threads" column; 0 = all logical CPUs */
    double seconds;     /* timed length of each measurement (default 0.5, 0.1 .. 60) */
    unsigned run_mask;  /* bits 1u << WK_*; 0 = every group */
    unsigned codec_mask;/* bits 1u << WK_LIB_*; 0 = every library */
    int single_only;    /* 1 = skip the N-thread column (about half the time) */
    long part_kib;      /* each corpus part, KiB (default 1024, 64 .. 16384) */
    wk_cb cb;           /* optional progress callback */
    void *ud;
} wk_cfg;

extern const char *const wk_group_name[WK_NGROUP];   /* "Compression", "Sorting", ... */
extern const char *const wk_group_id[WK_NGROUP];     /* "compress", "sort", "hash", "text", "interp" (CLI words) */
extern const char *const wk_lib_id[WK_NLIB];         /* "zlib", "zstd", "lz4", "xz" */
extern const char *const wk_codec_name[WK_NCODEC];   /* "zlib -6", "zstd -19", "lz4", ... */
extern const char *const wk_test_name[WT_N];         /* "64-bit integers, qsort", ... */
extern const char *const wk_unit[WK_NGROUP];         /* "MB/s", "Mkeys/s", "GB/s", "GB/s", "Mops/s" */
int  wk_test_group(int test);                         /* WK_* group of a WT_* row */
int  wk_codec_lib(int codec);                         /* WK_LIB_* of a WC_* row */

void wk_cfg_default(wk_cfg *c);
int  wk_available(void);                 /* 1 on Linux builds */
const char *wk_stage(void);              /* "Compression: zstd -19 compress, 16 threads" while running, "" otherwise */
/* known-answer tests (CRC32, CRC32C, xxHash64, FNV-1a), small checks of every sort, search, parser
   and interpreter against simple reference code, and a 4 KB round trip through every library that
   loads. One thread, well under a second. 1 = all pass; msg says what passed or what failed. */
int  wk_selftest(char *msg, size_t mn);
/* about how long a run with these settings takes, seconds (for the "Run asks first" question) */
double wk_estimate_seconds(const wk_cfg *c);
/* run, render into o (may be NULL) and ~/.cache/srs/bench-work.txt, add history lines
   (work-compress, work-sort, ...). 0 ok, 1 a check failed (results shown), -1 not available /
   out of memory / another CPU run (burn-in) is active, -2 stopped (partial results shown) */
int  bench_work(const wk_cfg *c, srs_out *o, volatile int *cancel);
const wk_result *wk_last(void);          /* result of the last bench_work() in this process, NULL = none */

#endif
