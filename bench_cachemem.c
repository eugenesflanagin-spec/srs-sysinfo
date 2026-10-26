/* bench_cachemem.c - Cache & Memory benchmark: Read / Write / Copy bandwidth and Latency of the RAM
 * and the L1, L2 and L3 caches (see bench_cachemem.h).
 * The measuring engine comes unchanged from Linux System Audit (LSA) 0.2.0, the same author's
 * GPL-2.0-or-later project, so its results match LSA's. Bandwidth: one pinned thread per logical CPU,
 * AVX-512 / AVX2 / SSE2 chosen at run time; RAM write on AMD with CLZERO. Latency: one thread, random
 * pointer chase (4 KB pages for caches, 2 MB pages over 8 x L3 for the RAM).
 * x86-64 only (vector and CLZERO code); other builds get a stub that says so. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "bench_cachemem.h"
#include "bench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__x86_64__)
#include <cpuid.h>
#include <immintrin.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

const char *const cm_level_name[CM_NLEVEL] = { "Memory", "L1 Cache", "L2 Cache", "L3 Cache" };
const char *const cm_test_name[CM_NTEST] = { "Read", "Write", "Copy", "Latency" };

/* ---------------------------------------------------------------- Linux platform layer */
static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}


typedef cpu_set_t aff_t;
static aff_t aff_save(void) { cpu_set_t s; sched_getaffinity(0, sizeof s, &s); return s; }
static void aff_restore(aff_t a) { sched_setaffinity(0, sizeof a, &a); }

static long cache_kib(int level)
{
    char p[128], buf[64];
    int idx;
    for (idx = 0; idx < 8; idx++) {
        FILE *f;
        int lv = 0;
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu0/cache/index%d/level", idx);
        if (!(f = fopen(p, "r"))) break;
        if (fscanf(f, "%d", &lv) != 1) lv = 0;
        fclose(f);
        if (lv != level) continue;
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu0/cache/index%d/type", idx);
        if ((f = fopen(p, "r"))) { if (!fgets(buf, sizeof buf, f)) buf[0] = 0; fclose(f); if (!strncmp(buf, "Instruction", 11)) continue; }
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu0/cache/index%d/size", idx);
        if ((f = fopen(p, "r"))) {
            long v = 0;
            char u = 'K';
            if (fscanf(f, "%ld%c", &v, &u) < 1) v = 0;
            fclose(f);
            return u == 'M' ? v * 1024 : v;
        }
    }
    return level == 1 ? 32 : level == 2 ? 1024 : 8192;
}

/* logical CPUs sharing one instance of this cache level (cpu0's) */
static int cache_sharers(int level)
{
    char p[128], buf[256];
    int idx, n = 1;
    for (idx = 0; idx < 8; idx++) {
        FILE *f;
        int lv = 0;
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu0/cache/index%d/level", idx);
        if (!(f = fopen(p, "r"))) break;
        if (fscanf(f, "%d", &lv) != 1) lv = 0;
        fclose(f);
        if (lv != level) continue;
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu0/cache/index%d/shared_cpu_list", idx);
        if ((f = fopen(p, "r"))) {
            if (fgets(buf, sizeof buf, f)) {   /* "0,8" / "0-15" / "0-3,16-19" */
                char *t = buf, *e;
                n = 0;
                while (*t && *t != '\n') {
                    long a = strtol(t, &e, 10), b = a;
                    if (e == t) break;
                    if (*e == '-') b = strtol(e + 1, &e, 10);
                    n += (int)(b - a + 1);
                    t = *e == ',' ? e + 1 : e;
                }
                if (n < 1) n = 1;
            }
            fclose(f);
        }
        break;
    }
    return n;
}

static int cpu_list(int *cpus, int max)
{
    cpu_set_t s;
    int i, n = 0;
    if (sched_getaffinity(0, sizeof s, &s)) { long c = sysconf(_SC_NPROCESSORS_ONLN); for (i = 0; i < c && i < max; i++) cpus[n++] = i; return n; }
    for (i = 0; i < CPU_SETSIZE && n < max; i++) if (CPU_ISSET(i, &s)) cpus[n++] = i;
    return n;
}

static void pin(int cpu)
{
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(cpu, &s);
    sched_setaffinity(0, sizeof s, &s);
}

static void *alloc_pages(size_t bytes, int huge)
{
    size_t al = 2u << 20, sz = (bytes + al - 1) / al * al;
    void *p = NULL;
    if (posix_memalign(&p, al, sz)) return NULL;
    madvise(p, sz, huge ? MADV_HUGEPAGE : MADV_NOHUGEPAGE);
    return p;
}
static void *alloc_huge(size_t bytes) { return alloc_pages(bytes, 1); }

/* first logical CPU of every physical core among `cpus` */
static int core_list(const int *cpus, int n, int *out)
{
    int i, k = 0;
    for (i = 0; i < n; i++) {
        char p[128];
        FILE *f;
        long first = cpus[i];
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpus[i]);
        if ((f = fopen(p, "r"))) { if (fscanf(f, "%ld", &first) != 1) first = cpus[i]; fclose(f); }
        if (first == cpus[i]) out[k++] = cpus[i];
    }
    return k ? k : n;
}

static long mem_available_mib(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char l[256];
    long kb = 0;
    if (!f) return 0;
    while (fgets(l, sizeof l, f)) if (sscanf(l, "MemAvailable: %ld kB", &kb) == 1) break;
    fclose(f);
    return kb / 1024;
}

static void page_free(void *p) { free(p); }
int cm_large_pages(void) { return 1; }   /* transparent huge pages (madvise) */

/* ---------------------------------------------------------------- kernels
 * n is a multiple of 256 bytes, buffers 64-byte aligned. Read ORs every loaded vector into
 * four accumulators (so no load can be optimised away) and stores the result to `sink`. */
typedef void (*kfn)(char *dst, const char *src, size_t n, char *sink);

/* Read: plain vector loads into registers (asm, so they cannot be optimised away and add no ALU work) */
__attribute__((target("avx512f"))) static void rd_512(char *d, const char *s, size_t n, char *sink)
{
    size_t i;
    (void)d; (void)sink;
    for (i = 0; i < n; i += 256)
        __asm__ volatile("vmovdqa64 (%0), %%zmm0\n\tvmovdqa64 64(%0), %%zmm1\n\tvmovdqa64 128(%0), %%zmm2\n\tvmovdqa64 192(%0), %%zmm3"
                         : : "r"(s + i) : "xmm0", "xmm1", "xmm2", "xmm3");
}
__attribute__((target("avx512f"))) static void wr_512(char *d, const char *s, size_t n, char *sink)
{
    __m512i v = _mm512_set1_epi32(0x5A5A5A5A);
    size_t i;
    (void)s; (void)sink;
    for (i = 0; i < n; i += 256) {
        _mm512_store_si512((void *)(d + i), v); _mm512_store_si512((void *)(d + i + 64), v);
        _mm512_store_si512((void *)(d + i + 128), v); _mm512_store_si512((void *)(d + i + 192), v);
    }
}
__attribute__((target("avx512f"))) static void wrnt_512(char *d, const char *s, size_t n, char *sink)
{
    __m512i v = _mm512_set1_epi32(0x5A5A5A5A);
    size_t i;
    (void)s; (void)sink;
    for (i = 0; i < n; i += 256) {
        _mm512_stream_si512((void *)(d + i), v); _mm512_stream_si512((void *)(d + i + 64), v);
        _mm512_stream_si512((void *)(d + i + 128), v); _mm512_stream_si512((void *)(d + i + 192), v);
    }
    _mm_sfence();
}
__attribute__((target("avx512f"))) static void cp_512(char *d, const char *s, size_t n, char *sink)
{
    size_t i;
    (void)sink;
    for (i = 0; i < n; i += 256) {
        __m512i a = _mm512_load_si512((const void *)(s + i)), b = _mm512_load_si512((const void *)(s + i + 64));
        __m512i c = _mm512_load_si512((const void *)(s + i + 128)), e = _mm512_load_si512((const void *)(s + i + 192));
        _mm512_store_si512((void *)(d + i), a); _mm512_store_si512((void *)(d + i + 64), b);
        _mm512_store_si512((void *)(d + i + 128), c); _mm512_store_si512((void *)(d + i + 192), e);
    }
}
__attribute__((target("avx512f"))) static void cpnt_512(char *d, const char *s, size_t n, char *sink)
{
    size_t i;
    (void)sink;
    for (i = 0; i < n; i += 256) {
        __m512i a = _mm512_load_si512((const void *)(s + i)), b = _mm512_load_si512((const void *)(s + i + 64));
        __m512i c = _mm512_load_si512((const void *)(s + i + 128)), e = _mm512_load_si512((const void *)(s + i + 192));
        _mm512_stream_si512((void *)(d + i), a); _mm512_stream_si512((void *)(d + i + 64), b);
        _mm512_stream_si512((void *)(d + i + 128), c); _mm512_stream_si512((void *)(d + i + 192), e);
    }
    _mm_sfence();
}

#define AVX2_LOOP(BODY) for (i = 0; i < n; i += 256) { BODY(0) BODY(32) BODY(64) BODY(96) BODY(128) BODY(160) BODY(192) BODY(224) }
__attribute__((target("avx2"))) static void rd_256(char *d, const char *s, size_t n, char *sink)
{
    size_t i;
    (void)d; (void)sink;
    for (i = 0; i < n; i += 256)
        __asm__ volatile("vmovdqa (%0), %%ymm0\n\tvmovdqa 32(%0), %%ymm1\n\tvmovdqa 64(%0), %%ymm2\n\tvmovdqa 96(%0), %%ymm3\n\t"
                         "vmovdqa 128(%0), %%ymm4\n\tvmovdqa 160(%0), %%ymm5\n\tvmovdqa 192(%0), %%ymm6\n\tvmovdqa 224(%0), %%ymm7"
                         : : "r"(s + i) : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7");
}
#define W256(o) _mm256_store_si256((void *)(d + i + (o)), v);
#define WNT256(o) _mm256_stream_si256((void *)(d + i + (o)), v);
#define C256(o) _mm256_store_si256((void *)(d + i + (o)), _mm256_load_si256((const void *)(s + i + (o))));
#define CNT256(o) _mm256_stream_si256((void *)(d + i + (o)), _mm256_load_si256((const void *)(s + i + (o))));
__attribute__((target("avx2"))) static void wr_256(char *d, const char *s, size_t n, char *sink)
{ __m256i v = _mm256_set1_epi32(0x5A5A5A5A); size_t i; (void)s; (void)sink; AVX2_LOOP(W256) }
__attribute__((target("avx2"))) static void wrnt_256(char *d, const char *s, size_t n, char *sink)
{ __m256i v = _mm256_set1_epi32(0x5A5A5A5A); size_t i; (void)s; (void)sink; AVX2_LOOP(WNT256) _mm_sfence(); }
__attribute__((target("avx2"))) static void cp_256(char *d, const char *s, size_t n, char *sink)
{ size_t i; (void)sink; AVX2_LOOP(C256) }
__attribute__((target("avx2"))) static void cpnt_256(char *d, const char *s, size_t n, char *sink)
{ size_t i; (void)sink; AVX2_LOOP(CNT256) _mm_sfence(); }

#define SSE_LOOP(BODY) for (i = 0; i < n; i += 256) { int k; for (k = 0; k < 256; k += 64) { BODY(k) BODY(k + 16) BODY(k + 32) BODY(k + 48) } }
static void rd_128(char *d, const char *s, size_t n, char *sink)
{
    __m128i a = _mm_setzero_si128(), b = a, c = a, e = a;
    size_t i;
    (void)d;
    for (i = 0; i < n; i += 64) {
        a = _mm_or_si128(a, _mm_load_si128((const void *)(s + i)));      b = _mm_or_si128(b, _mm_load_si128((const void *)(s + i + 16)));
        c = _mm_or_si128(c, _mm_load_si128((const void *)(s + i + 32))); e = _mm_or_si128(e, _mm_load_si128((const void *)(s + i + 48)));
    }
    _mm_storeu_si128((void *)sink, _mm_or_si128(_mm_or_si128(a, b), _mm_or_si128(c, e)));
}
#define W128(o) _mm_store_si128((void *)(d + i + (o)), v);
#define WNT128(o) _mm_stream_si128((void *)(d + i + (o)), v);
#define C128(o) _mm_store_si128((void *)(d + i + (o)), _mm_load_si128((const void *)(s + i + (o))));
#define CNT128(o) _mm_stream_si128((void *)(d + i + (o)), _mm_load_si128((const void *)(s + i + (o))));
static void wr_128(char *d, const char *s, size_t n, char *sink) { __m128i v = _mm_set1_epi32(0x5A5A5A5A); size_t i; (void)s; (void)sink; SSE_LOOP(W128) }
static void wrnt_128(char *d, const char *s, size_t n, char *sink) { __m128i v = _mm_set1_epi32(0x5A5A5A5A); size_t i; (void)s; (void)sink; SSE_LOOP(WNT128) _mm_sfence(); }
static void cp_128(char *d, const char *s, size_t n, char *sink) { size_t i; (void)sink; SSE_LOOP(C128) }
static void cpnt_128(char *d, const char *s, size_t n, char *sink) { size_t i; (void)sink; SSE_LOOP(CNT128) _mm_sfence(); }

/* AMD CLZERO: CPUID 8000_0008h EBX bit 0 */
int cm_have_clzero(void)
{
    unsigned a, b, c, d;
    if (!__get_cpuid(0x80000000u, &a, &b, &c, &d) || a < 0x80000008u) return 0;
    __get_cpuid(0x80000008u, &a, &b, &c, &d);
    return (int)(b & 1u);
}

/* Memory Write on AMD: CLZERO writes whole zeroed cache lines to DRAM without carrying the data
   over the core-to-memory link, so it measures what the memory itself can absorb (streaming
   stores stop at the link's write limit, ~16 B per fabric clock on Zen) */
static void clz_range(char *d, size_t n)
{
    size_t i;
    for (i = 0; i < n; i += 256)
        __asm__ volatile("clzero\n\tlea 64(%%rax), %%rax\n\tclzero\n\tlea 64(%%rax), %%rax\n\tclzero\n\tlea 64(%%rax), %%rax\n\tclzero"
                         : : "a"(d + i) : "memory");
}

int cm_isa_best(void)
{
    __builtin_cpu_init();
    if (__builtin_cpu_supports("avx512f")) return CM_ISA_AVX512;
    if (__builtin_cpu_supports("avx2")) return CM_ISA_AVX2;
    return CM_ISA_SSE2;
}

void cm_format_bw(char *b, unsigned long n, int level, double v, int mib)
{
    if (v <= 0) { snprintf(b, n, "-"); return; }
    double k = mib ? 1024.0 : 1000.0;   /* v is in MiB/s with mib, else MB/s */
    if (level == CM_MEM || v < k) snprintf(b, n, "%.0f %s", v, mib ? "MiB/s" : "MB/s");
    else if (v < k * 1000) snprintf(b, n, "%.2f %s", v / k, mib ? "GiB/s" : "GB/s");
    else snprintf(b, n, "%.1f %s", v / k, mib ? "GiB/s" : "GB/s");
}

void cm_dram_ratio(char *b, unsigned long n, double mem_mhz, double fsb_mhz)
{
    double r = fsb_mhz > 0 ? mem_mhz / fsb_mhz : 0, w = (double)(long)(r + 0.5);
    if (r - w < 0.05 && w - r < 0.05) snprintf(b, n, "DRAM:FSB %.0f:1", w);
    else snprintf(b, n, "DRAM:FSB %.1f:1", r);
}

const char *cm_isa_name(int isa)
{
    return isa == CM_ISA_AVX512 ? "AVX-512" : isa == CM_ISA_AVX2 ? "AVX2" : isa == CM_ISA_SSE2 ? "SSE2" : "auto";
}

/* memory: non-temporal (streaming) stores bypass the caches; caches: ordinary stores */
static kfn pick(int isa, int test, int mem)
{
    static const kfn t[3][5] = {
        { rd_128, wr_128, wrnt_128, cp_128, cpnt_128 },
        { rd_256, wr_256, wrnt_256, cp_256, cpnt_256 },
        { rd_512, wr_512, wrnt_512, cp_512, cpnt_512 } };
    int r = isa == CM_ISA_AVX512 ? 2 : isa == CM_ISA_AVX2 ? 1 : 0;
    if (test == CM_READ) return t[r][0];
    if (test == CM_WRITE) return t[r][mem ? 2 : 1];
    return t[r][mem ? 4 : 3];
}

/* ---------------------------------------------------------------- topology */
/* ---------------------------------------------------------------- bandwidth */
#define MAXT 512
typedef struct {
    int cpu, idx;
    char *buf;
    size_t bytes;           /* this thread's block */
    kfn fn;
    int test;
    long reps;
    double t0, t1;          /* this thread's own start / end of the last trial */
    char sink[64] __attribute__((aligned(64)));
} worker;

typedef struct {
    pthread_barrier_t start, done;
    worker w[MAXT];
    int n, quit;
} pool;

static void work_once(worker *w)
{
    size_t half = w->bytes / 2 / 256 * 256;
    long r;
    for (r = 0; r < w->reps; r++) {
        if (w->test == CM_COPY) w->fn(w->buf + half, w->buf, half, w->sink);
        else w->fn(w->buf, w->buf, w->bytes, w->sink);
    }
}

static void *worker_main(void *arg)
{
    worker *w = arg;
    pool *p = (pool *)((char *)(w - w->idx) - offsetof(pool, w));
    pin(w->cpu);
    for (;;) {
        pthread_barrier_wait(&p->start);
        if (p->quit) break;
        w->t0 = now_s();
        work_once(w);
        w->t1 = now_s();
        pthread_barrier_wait(&p->done);
    }
    return NULL;
}

/* one timed trial on every thread; returns seconds from the first thread's start to the last
   thread's end (each thread stamps its own times, so wake-up order cannot skew the result) */
static double trial(pool *p)
{
    double t0 = 1e300, t1 = 0;
    int i;
    pthread_barrier_wait(&p->start);
    pthread_barrier_wait(&p->done);
    for (i = 0; i < p->n; i++) {
        if (p->w[i].t0 < t0) t0 = p->w[i].t0;
        if (p->w[i].t1 > t1) t1 = p->w[i].t1;
    }
    return t1 - t0;
}

static double bandwidth(pool *p, int level, int test, int isa, size_t per_thread, double secs, volatile int *cancel)
{
    int i;
    double best = 0, t, spent = 0, bytes_per_rep;
    size_t useful;
    for (i = 0; i < p->n; i++) {
        worker *w = &p->w[i];
        w->bytes = per_thread / 256 * 256;
        w->fn = pick(isa, test, level == CM_MEM);
        w->test = test;
        w->reps = 1;
    }
    /* Copy is reported as bytes read + bytes written (2 x the bytes copied), the usual convention for
       this benchmark - it makes Copy comparable with Read and Write */
    useful = test == CM_COPY ? p->w[0].bytes / 2 / 256 * 256 * 2 : p->w[0].bytes;
    bytes_per_rep = (double)useful * p->n;
    t = trial(p);   /* warm-up */
    for (;;) {      /* calibrate: grow the work until one trial lasts >= 40 ms */
        long reps = p->w[0].reps;
        if (cancel && *cancel) return 0;
        t = trial(p);
        if (t >= 0.04 || reps > (1L << 30)) break;
        reps = t > 0.004 ? (long)(reps * 0.06 / t) + 1 : reps * 8;
        for (i = 0; i < p->n; i++) p->w[i].reps = reps;
    }
    while (spent < secs || best == 0) {
        double bw;
        if (cancel && *cancel) return 0;
        t = trial(p);
        spent += t;
        bw = bytes_per_rep * p->w[0].reps / t;
        if (bw > best) best = bw;
        if (spent > secs * 4) break;
    }
    return best;
}

/* ---------------------------------------------------------------- memory write (CLZERO) */
typedef struct {
    int cpu, idx, n, quit;
    long reps;
    char *buf;
    size_t total;
    double t0, t1;
    pthread_barrier_t *start, *done;
} czw;

#define CLZ_CHUNK 4096   /* threads take turns in 4 KB chunks, so DRAM rows stay open */

static void *clz_main(void *arg)
{
    czw *w = arg;
    pin(w->cpu);
    for (;;) {
        long r;
        pthread_barrier_wait(w->start);
        if (w->quit) break;
        w->t0 = now_s();
        for (r = 0; r < w->reps; r++) {
            size_t c;
            for (c = (size_t)w->idx * CLZ_CHUNK; c < w->total; c += (size_t)w->n * CLZ_CHUNK) clz_range(w->buf + c, CLZ_CHUNK);
        }
        _mm_sfence();
        w->t1 = now_s();
        pthread_barrier_wait(w->done);
    }
    return NULL;
}

static double mem_write_clzero(const int *cpus, int ncpu, size_t total, double secs, volatile int *cancel)
{
    static int cores[MAXT];
    czw *w;
    pthread_t tid[MAXT];
    pthread_barrier_t start, done;
    char *buf;
    int n = core_list(cpus, ncpu, cores), i;
    double best = 0, spent = 0;
    if (n > ncpu) n = ncpu;
    total = total / ((size_t)n * CLZ_CHUNK) * ((size_t)n * CLZ_CHUNK);
    if (!total || !(buf = alloc_huge(total))) return 0;
    memset(buf, 1, total);
    w = calloc((size_t)n, sizeof *w);
    if (!w) { page_free(buf); return 0; }
    pthread_barrier_init(&start, NULL, (unsigned)n + 1);
    pthread_barrier_init(&done, NULL, (unsigned)n + 1);
    for (i = 0; i < n; i++) {
        w[i] = (czw){ .cpu = cores[i], .idx = i, .n = n, .reps = 1, .buf = buf, .total = total, .start = &start, .done = &done };
        pthread_create(&tid[i], NULL, clz_main, &w[i]);
    }
    for (i = -1;; i = 0) {   /* one warm-up pass, then each trial = one complete pass over the block; best pass */
        double t0 = 1e300, t1 = 0, t, bw;
        int k, warm = i < 0;
        if (cancel && *cancel) break;
        pthread_barrier_wait(&start);
        pthread_barrier_wait(&done);
        for (k = 0; k < n; k++) { if (w[k].t0 < t0) t0 = w[k].t0; if (w[k].t1 > t1) t1 = w[k].t1; }
        t = t1 - t0;
        if (warm) continue;
        spent += t;
        bw = (double)total * w[0].reps / t;
        if (bw > best) best = bw;
        if (spent >= secs) break;
    }
    for (i = 0; i < n; i++) w[i].quit = 1;
    pthread_barrier_wait(&start);
    for (i = 0; i < n; i++) pthread_join(tid[i], NULL);
    pthread_barrier_destroy(&start);
    pthread_barrier_destroy(&done);
    free(w);
    page_free(buf);
    return best;
}

/* ---------------------------------------------------------------- latency */
static uint64_t xs(uint64_t *s) { *s ^= *s << 13; *s ^= *s >> 7; *s ^= *s << 17; return *s; }

/* random cyclic chain through every 64-byte line of `bytes` (Sattolo), ns per dependent load */
static double latency(char *buf, size_t bytes, double secs, volatile int *cancel)
{
    size_t lines = bytes / 64, i;
    uint32_t *perm = malloc(lines * sizeof *perm);
    uint64_t seed = 0x9E3779B97F4A7C15ull;
    void **p;
    double best = 1e9, spent = 0;
    long steps;
    if (!perm || lines < 2) { free(perm); return 0; }
    for (i = 0; i < lines; i++) perm[i] = (uint32_t)i;
    for (i = lines - 1; i > 0; i--) { size_t j = xs(&seed) % i; uint32_t t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
    for (i = 0; i < lines; i++) *(void **)(buf + (size_t)perm[i] * 64) = buf + (size_t)perm[(i + 1) % lines] * 64;
    free(perm);
    p = (void **)buf;
    for (i = 0; i < lines; i++) p = *p;   /* warm up: TLB + caches */
    steps = 1 << 16;
    for (;;) {   /* calibrate to ~50 ms */
        double t0 = now_s(), t;
        long k;
        for (k = 0; k < steps; k += 16) {
            p = *p; p = *p; p = *p; p = *p; p = *p; p = *p; p = *p; p = *p;
            p = *p; p = *p; p = *p; p = *p; p = *p; p = *p; p = *p; p = *p;
        }
        t = now_s() - t0;
        if (t > 0.05 || steps > (1L << 30)) break;
        steps *= 2;
    }
    while (spent < secs) {
        double t0 = now_s(), t;
        long k;
        if (cancel && *cancel) break;
        for (k = 0; k < steps; k += 16) {
            p = *p; p = *p; p = *p; p = *p; p = *p; p = *p; p = *p; p = *p;
            p = *p; p = *p; p = *p; p = *p; p = *p; p = *p; p = *p; p = *p;
        }
        t = now_s() - t0;
        spent += t;
        if (t / steps * 1e9 < best) best = t / steps * 1e9;
    }
    __asm__ volatile("" : : "r"(p));
    return best;
}

/* ---------------------------------------------------------------- clock */
double cm_measure_mhz(void)
{
    /* 20 dependent 1-cycle adds per loop: cycles = 20 x iterations, whatever the turbo state */
    long it = 20000000, x = 0;
    double t0, t, best = 0;
    int r;
    for (r = 0; r < 3; r++) {
        long n = it;
        t0 = now_s();
        __asm__ volatile(
            "1:\n\t"
            "add $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\t"
            "add $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\t"
            "add $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\t"
            "add $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\tadd $1, %0\n\t"
            "dec %1\n\tjnz 1b" : "+r"(x), "+r"(n));
        t = now_s() - t0;
        if (t > 0 && 20.0 * it / t / 1e6 > best) best = 20.0 * it / t / 1e6;
    }
    return best;
}

/* ---------------------------------------------------------------- driver */
void cm_cfg_default(cm_cfg *c)
{
    memset(c, 0, sizeof *c);
    c->seconds = 1.0;
    c->isa = CM_ISA_AUTO;
    c->mb = 1e6;
}

int cm_run(const cm_cfg *cfg, cm_result *r, cm_cb cb, void *ud, volatile int *cancel)
{
    static int cpus[MAXT];
    pool *p;
    int ncpu = cpu_list(cpus, MAXT), n, i, lv, te, rc = 0, isa = cfg->isa ? cfg->isa : cm_isa_best();
    long mem_mib = cfg->mem_mib;
    double mb = cfg->mb > 0 ? cfg->mb : 1e6;
    size_t per[CM_NLEVEL];
    char **bufs;
    pthread_t tid[MAXT];
    memset(r, 0, sizeof *r);
    n = cfg->threads > 0 && cfg->threads < ncpu ? cfg->threads : ncpu;
    if (n < 1) return -1;
    if (isa == CM_ISA_AVX512 && cm_isa_best() != CM_ISA_AVX512) isa = cm_isa_best();
    if (isa == CM_ISA_AVX2 && cm_isa_best() == CM_ISA_SSE2) isa = CM_ISA_SSE2;
    r->threads = n;
    r->isa = isa;
    for (i = 1; i <= 3; i++) r->cache_kib[i] = cache_kib(i);
    if (mem_mib <= 0) {
        long av = mem_available_mib() / 4;
        mem_mib = 1024;
        if (av > 0 && mem_mib > av) mem_mib = av;
        if (mem_mib < 64) mem_mib = 64;
    }
    {   /* the memory block must not fit in the caches (X3D parts have 96+ MB of L3) */
        long min_mib = r->cache_kib[3] * 4 / 1024;
        if (min_mib < 256) min_mib = 256;
        if (mem_mib < min_mib) mem_mib = min_mib;
    }
    /* per-thread block for each level: half of this thread's share of the cache, so it stays resident */
    per[CM_MEM] = (size_t)mem_mib * 1048576 / (size_t)n;
    per[CM_L1] = (size_t)r->cache_kib[1] * 1024 / (size_t)cache_sharers(1) / 2;
    per[CM_L2] = (size_t)r->cache_kib[2] * 1024 / (size_t)cache_sharers(2) / 2;
    {
        int sh3 = cache_sharers(3), per_inst = n < sh3 ? n : sh3;
        per[CM_L3] = (size_t)r->cache_kib[3] * 1024 / (size_t)(per_inst > 0 ? per_inst : 1) / 2;
        if (per[CM_L3] < per[CM_L2] * 4) per[CM_L3] = per[CM_L2] * 4;   /* must spill out of L2 */
    }
    for (lv = 0; lv < CM_NLEVEL; lv++) if (per[lv] < 4096) per[lv] = 4096;

    p = calloc(1, sizeof *p);
    bufs = calloc((size_t)n, sizeof *bufs);
    if (!p || !bufs) { free(p); free(bufs); return -1; }
    for (i = 0; i < n; i++) {
        bufs[i] = alloc_huge(per[CM_MEM] > per[CM_L3] ? per[CM_MEM] : per[CM_L3]);
        if (!bufs[i]) { rc = -1; n = i; break; }
    }
    p->n = n;
    if (rc == 0) {
        pthread_barrier_init(&p->start, NULL, (unsigned)n + 1);
        pthread_barrier_init(&p->done, NULL, (unsigned)n + 1);
        for (i = 0; i < n; i++) {
            p->w[i].idx = i;
            p->w[i].cpu = cpus[i];
            p->w[i].buf = bufs[i];
            /* first touch by the owning thread happens in the warm-up trial; pre-fault here once */
            memset(bufs[i], 1, per[CM_MEM] > per[CM_L3] ? per[CM_MEM] : per[CM_L3]);
            pthread_create(&tid[i], NULL, worker_main, &p->w[i]);
        }
        r->clock_mhz = cm_measure_mhz();
        for (lv = 0; lv < CM_NLEVEL && rc == 0; lv++)
            for (te = 0; te < CM_NTEST && rc == 0; te++) {
                unsigned bit = 1u << (lv * CM_NTEST + te);
                if (cfg->run_mask && !(cfg->run_mask & bit)) continue;
                if (cancel && *cancel) { rc = 1; break; }
                if (cb) cb(lv, te, 0, r, ud);
                if (te == CM_LAT) {
                    /* single thread on the first CPU; chain over the level's block size */
                    /* block per level: L1 = half of L1d; L2 = half of L2 (few L1 hits); L3 = 8 x L2 (few L2
                       hits); Memory = 8 x L3, at least 256 MB, so almost none of it is cache-resident */
                    size_t l3 = (size_t)r->cache_kib[3] * 1024, bytes;
                    char *lb = bufs[0], *own = NULL;
                    aff_t old;
                    if (lv == CM_L1) bytes = (size_t)r->cache_kib[1] * 1024 / 2;
                    else if (lv == CM_L2) bytes = (size_t)r->cache_kib[2] * 1024 / 2;
                    else if (lv == CM_L3) {   /* 8 x L2 (mostly L3 hits, still inside the second-level TLB's reach) */
                        bytes = (size_t)r->cache_kib[2] * 1024 * 8;
                        if (bytes > l3 / 2) bytes = l3 / 2;
                    }
                    else {   /* 8 x L3 (at least 256 MB): with 4 x L3 a quarter of the chain still hit L3 and
                                read ~7% too fast; 8 x L3 leaves ~1/8 L3 hits */
                        long av = mem_available_mib();
                        bytes = l3 * 8 > (256u << 20) ? l3 * 8 : (256u << 20);
                        if (av > 0 && bytes > (size_t)av / 2 * 1048576) bytes = (size_t)av / 2 * 1048576;
                    }
                    /* cache latency on ordinary 4 KB pages (includes first-level TLB misses, as on a normal
                       desktop); memory latency on 2 MB pages so page walks do not dominate it */
                    own = alloc_pages(bytes, lv == CM_MEM);
                    if (own) { memset(own, 1, bytes); lb = own; }
                    else if (bytes > (per[CM_MEM] > per[CM_L3] ? per[CM_MEM] : per[CM_L3])) bytes = per[CM_MEM] > per[CM_L3] ? per[CM_MEM] : per[CM_L3];
                    old = aff_save();
                    pin(cpus[0]);
                    r->v[lv][te] = latency(lb, bytes, cfg->seconds, cancel);
                    aff_restore(old);
                    page_free(own);
                } else if (lv == CM_MEM && te == CM_WRITE && cfg->mem_write != CM_MW_STREAM && cm_have_clzero()) {
                    r->mem_write = CM_MW_CLZERO;
                    /* block: 256 MB or 2.5 x L3, whichever is larger (well beyond any cache, and the size at which
                       this method was checked on the author's machine) */
                    size_t l3b = (size_t)r->cache_kib[3] * 1024, blk = l3b * 5 / 2 > (256u << 20) ? l3b * 5 / 2 : (256u << 20);
                    r->v[lv][te] = mem_write_clzero(cpus, n, blk, cfg->seconds, cancel) / mb;
                } else {
                    if (lv == CM_MEM && te == CM_WRITE) r->mem_write = CM_MW_STREAM;
                    r->v[lv][te] = bandwidth(p, lv, te, isa, per[lv], cfg->seconds, cancel) / mb;
                }
                if (cancel && *cancel) { r->v[lv][te] = 0; rc = 1; }
                if (cb) cb(lv, te, 1, r, ud);
            }
        p->quit = 1;
        pthread_barrier_wait(&p->start);
        for (i = 0; i < n; i++) pthread_join(tid[i], NULL);
        pthread_barrier_destroy(&p->start);
        pthread_barrier_destroy(&p->done);
    }
    for (i = 0; i < n; i++) page_free(bufs[i]);
    free(bufs);
    free(p);
    return rc;
}

/* ======================================================================
 * SRS presentation: the result table and the system lines, in SRS's words
 * ====================================================================== */
#include <limits.h>
#include <x86intrin.h>

static const char *volatile g_cm_stage = "";
const char *cm_stage(void) { return g_cm_stage; }
int cm_available(void) { return 1; }

static void rd1(const char *path, char *out, size_t n)
{
    FILE *f = fopen(path, "r");
    out[0] = 0;
    if (!f) return;
    if (fgets(out, (int)n, f)) out[strcspn(out, "\n")] = 0;
    fclose(f);
}

static double mono_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

/* TSC rate in MHz, timed against CLOCK_MONOTONIC_RAW over ~100 ms */
static double tsc_mhz(void)
{
    struct timespec d = { 0, 100000000L };
    double t0, t1;
    unsigned long long c0, c1;
    t0 = mono_s(); c0 = __rdtsc();
    nanosleep(&d, NULL);
    t1 = mono_s(); c1 = __rdtsc();
    return t1 > t0 ? (double)(c1 - c0) / (t1 - t0) / 1e6 : 0;
}

/* base (reference) clock: the invariant TSC runs at the nominal clock, and ACPI CPPC nominal_freq
   is that clock in MHz at a 100 MHz base, so base = TSC x 100 / nominal_freq (105 with eCLK 105) */
static double base_clock_mhz(void)
{
    char b[32];
    double nom, f;
    FILE *c = fopen("/proc/cpuinfo", "r");
    int ctsc = 0;
    if (c) {
        char l[4096];
        while (fgets(l, sizeof l, c)) if (!strncmp(l, "flags", 5)) { ctsc = strstr(l, " constant_tsc") != NULL; break; }
        fclose(c);
    }
    if (!ctsc) return 0;
    rd1("/sys/devices/system/cpu/cpu0/acpi_cppc/nominal_freq", b, sizeof b);
    if ((nom = atof(b)) <= 0) return 0;
    f = tsc_mhz() * 100.0 / nom;
    return f >= 50 && f <= 200 ? f : 0;
}

/* Infinity Fabric clock (FCLK) from the ryzen_smu driver's PM table (read-only). Only the table
   layout 0x540104 (Ryzen 7000 desktop) is known: FCLK is float 70. SRS_RSMU_DIR overrides the
   directory (tests). */
static void fclk(char *out, size_t n)
{
    const char *dir = getenv("SRS_RSMU_DIR");
    char p[PATH_MAX];
    unsigned char raw[4];
    float t[2048];
    uint32_t ver;
    size_t got;
    FILE *f;
    if (!dir || !*dir) dir = "/sys/kernel/ryzen_smu_drv";
    snprintf(p, sizeof p, "%s/pm_table_version", dir);
    if (!(f = fopen(p, "rb"))) { snprintf(out, n, "not shown (the ryzen_smu driver is not loaded)"); return; }
    got = fread(raw, 1, 4, f);
    fclose(f);
    if (got != 4) { snprintf(out, n, "not shown (ryzen_smu table version not readable)"); return; }
    memcpy(&ver, raw, 4);
    if (ver != 0x540104u) { snprintf(out, n, "not shown (ryzen_smu table version 0x%06X is not known)", (unsigned)ver); return; }
    snprintf(p, sizeof p, "%s/pm_table", dir);
    if (!(f = fopen(p, "rb"))) { snprintf(out, n, "not shown (ryzen_smu table not readable)"); return; }
    got = fread(t, 1, sizeof t, f);
    fclose(f);
    if (got < 79 * sizeof(float) || !(t[70] >= 400 && t[70] <= 4000)) { snprintf(out, n, "not shown (no FCLK in the ryzen_smu table)"); return; }
    snprintf(out, n, "%.0f MHz", t[70]);
}

/* memory sticks from the srs-root dmidecode cache (dmidecode needs root): count, speed, type, channels */
static void dimms(int *count, int *mts, char *type, size_t tn, int *channels)
{
    const char *path = so_root_file("dmidecode.txt");
    char l[512], chan[8][32];
    FILE *f;
    int in17 = 0, size_ok = 0, nch = 0, i;
    *count = 0; *mts = 0; type[0] = 0; *channels = 0;
    if (!path || !(f = fopen(path, "r"))) return;
    while (fgets(l, sizeof l, f)) {
        char *t = l;
        int v;
        while (*t == ' ' || *t == '\t') t++;
        if (!strncmp(l, "Handle ", 7)) { in17 = 0; continue; }
        if (!strncmp(t, "Memory Device", 13) && l[0] != '\t') { in17 = 1; size_ok = 0; continue; }
        if (!in17) continue;
        if (!strncmp(t, "Size:", 5)) size_ok = !strstr(t, "No Module") && !strstr(t, "Not Installed");
        else if (size_ok && sscanf(t, "Configured Memory Speed: %d MT/s", &v) == 1) { (*count)++; if (v > *mts) *mts = v; }
        else if (size_ok && !strncmp(t, "Type: ", 6) && strncmp(t + 6, "Unknown", 7)) {
            snprintf(type, tn, "%s", t + 6);
            type[strcspn(type, " \t\n")] = 0;
        } else if (size_ok && !strncmp(t, "Bank Locator:", 13)) {
            char *c = strstr(t, "CHANNEL "), cb[32];
            if (c) {
                snprintf(cb, sizeof cb, "%s", c + 8);
                cb[strcspn(cb, " \t\n")] = 0;
                for (i = 0; i < nch; i++) if (!strcmp(chan[i], cb)) break;
                if (i == nch && nch < 8) snprintf(chan[nch++], sizeof chan[0], "%s", cb);
            }
        }
    }
    fclose(f);
    *channels = nch;
}

static const char *const cm_row_name[CM_NLEVEL] = { "Memory (RAM)", "L1 cache", "L2 cache", "L3 cache" };

typedef struct { cm_result r; double base; int mib, cancelled; char when[32], fclk[96]; } cmctx;

static void cm_render(srs_out *o, void *c)
{
    static const int tabs[] = { 150, 290, 430, 570 }, tabs2[] = { 230 };
    cmctx *x = c;
    int lv, te, mts = 0, n = 0, nch = 0;
    char type[32];
    so_siv_header(o, "Cache & Memory benchmark (bandwidth and latency of the RAM and each cache level)");
    so_tabs(o, tabs, 4);
    so_row(o, 5, (const char *const[]){ "", "Read", "Write", "Copy", "Latency" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (lv = 0; lv < CM_NLEVEL; lv++) {
        char cell[CM_NTEST][32];
        const char *cp[5];
        for (te = 0; te < CM_NTEST; te++) {
            double v = x->r.v[lv][te];
            if (v <= 0) snprintf(cell[te], 32, "-");
            else if (te == CM_LAT) snprintf(cell[te], 32, "%.1f ns", v);
            else cm_format_bw(cell[te], 32, lv, v, x->mib);
        }
        cp[0] = cm_row_name[lv]; cp[1] = cell[0]; cp[2] = cell[1]; cp[3] = cell[2]; cp[4] = cell[3];
        so_row(o, 5, cp, (const int[]){ C_LABEL, C_BLACK, C_BLACK, C_BLACK, C_BLACK });
    }
    so_par(o);
    so_tabs(o, tabs2, 1);
    if (x->cancelled) so_kv(o, "Status", C_ORANGE, "Stopped before the end - the empty cells were not measured");
    so_kvf(o, "Threads / code", C_VALUE, "%d threads, %s vector code", x->r.threads, cm_isa_name(x->r.isa));
    if (x->r.mem_write) so_kv(o, "RAM write method", C_VALUE, x->r.mem_write == CM_MW_CLZERO ?
                              "CLZERO (whole zeroed cache lines), one thread per core" : "streaming stores");
    so_kv(o, "Processor", C_VALUE, sys_cpu_name());
    if (x->r.clock_mhz > 0) so_kvf(o, "Measured clock", C_VALUE, "%.0f MHz (during the run)", x->r.clock_mhz);
    if (x->base > 0) so_kvf(o, "Base clock", C_VALUE, "%.1f MHz (measured: TSC x 100 / CPPC nominal frequency)", x->base);
    else so_kv(o, "Base clock", C_VALUE, "100 MHz (nominal; could not be measured)");
    if (x->r.clock_mhz > 0) so_kvf(o, "Multiplier", C_VALUE, "%.2f x", x->r.clock_mhz / (x->base > 0 ? x->base : 100.0));
    so_kv(o, "Infinity Fabric clock", C_VALUE, x->fclk);
    dimms(&n, &mts, type, sizeof type, &nch);
    if (mts > 0) {
        so_kvf(o, "Memory", C_VALUE, "%s-%d, %d stick%s%s%.0d%s", type[0] ? type : "DRAM", mts, n, n == 1 ? "" : "s",
               nch > 0 ? ", " : "", nch, nch > 1 ? " channels" : nch == 1 ? " channel" : "");
        so_kvf(o, "Memory clock", C_VALUE, "%.0f MHz (= %.1f x base clock)", mts / 2.0, mts / 2.0 / (x->base > 0 ? x->base : 100.0));
    } else so_kv(o, "Memory", C_GRAY, "speed and type need root data: Tools > Collect root data (srs-root)");
    {
        char c[3][24];
        int i;
        for (i = 1; i <= 3; i++)
            if (x->r.cache_kib[i] >= 4096 && x->r.cache_kib[i] % 1024 == 0) snprintf(c[i - 1], sizeof c[0], "%ld MB", x->r.cache_kib[i] / 1024);
            else snprintf(c[i - 1], sizeof c[0], "%ld KB", x->r.cache_kib[i]);
        so_kvf(o, "Cache sizes", C_VALUE, "L1 data %s, L2 %s, L3 %s (per core / per L3 group)", c[0], c[1], c[2]);
    }
    so_kv(o, "Run", C_GRAY, x->when);
    so_kv(o, "Build", C_GRAY, bench_build_tag());
    so_note(o, x->mib ? "1 MiB = 1,048,576 bytes. Copy counts the bytes read plus the bytes written." :
                        "1 MB = 1,000,000 bytes; caches in GB/s. Copy counts the bytes read plus the bytes written.");
    so_board_footer(o);
}

static void cm_cb_stage(int level, int test, int done, const cm_result *r, void *ud)
{
    static char st[64];
    (void)r; (void)ud;
    if (done) return;
    snprintf(st, sizeof st, "%s %s", cm_row_name[level], cm_test_name[test]);
    g_cm_stage = st;
}

int bench_cachemem(const cm_cfg *c, srs_out *o, volatile int *cancel)
{
    cmctx *x = calloc(1, sizeof *x);
    time_t now = time(NULL);
    char params[96], res[200];
    int rc, lv, te;
    if (!x) return -1;
    x->mib = c->mb > 1e6;
    rc = cm_run(c, &x->r, cm_cb_stage, NULL, cancel);
    g_cm_stage = "";
    if (rc < 0) { free(x); return -1; }
    x->cancelled = rc == 1;
    x->base = base_clock_mhz();
    fclk(x->fclk, sizeof x->fclk);
    strftime(x->when, sizeof x->when, "%Y-%m-%d %H:%M:%S", localtime(&now));
    bench_save_render("cachemem", cm_render, x, o);
    snprintf(params, sizeof params, "threads=%d seconds=%.1f code=%s%s", x->r.threads, c->seconds, cm_isa_name(x->r.isa),
             c->run_mask ? " partial" : "");
    res[0] = 0;
    for (lv = 0; lv < CM_NLEVEL; lv++) {
        size_t l = strlen(res);
        snprintf(res + l, sizeof res - l, "%s%s", lv ? " | " : "", lv == CM_MEM ? "mem" : lv == CM_L1 ? "L1" : lv == CM_L2 ? "L2" : "L3");
        for (te = 0; te < CM_NTEST; te++) {
            l = strlen(res);
            snprintf(res + l, sizeof res - l, te == CM_LAT ? " %.1fns" : " %.0f", x->r.v[lv][te]);
        }
    }
    if (x->cancelled) strncat(res, " STOPPED", sizeof res - strlen(res) - 1);
    bench_history_add("cachemem", params, res);
    free(x);
    return rc;
}

#else   /* not x86-64 */
const char *const cm_level_name[CM_NLEVEL] = { "Memory", "L1 Cache", "L2 Cache", "L3 Cache" };
const char *const cm_test_name[CM_NTEST] = { "Read", "Write", "Copy", "Latency" };
int cm_available(void) { return 0; }
const char *cm_stage(void) { return ""; }
void cm_cfg_default(cm_cfg *c) { memset(c, 0, sizeof *c); c->seconds = 1.0; c->mb = 1e6; }
const char *cm_isa_name(int isa) { (void)isa; return "-"; }
int bench_cachemem(const cm_cfg *c, srs_out *o, volatile int *cancel)
{
    (void)c; (void)cancel;
    if (o) so_err(o, "The Cache & Memory benchmark needs an x86-64 CPU (it uses x86-64 vector code).");
    return -1;
}
#endif
