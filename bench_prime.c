/* bench_prime.c - Prime sieve: counts the primes up to N with a segmented sieve of Eratosthenes
 * (odd numbers only, cache-sized segments), on one or more threads, and checks the count against
 * the known value of pi(N) (see bench.h). Written for SRS. */
#define _GNU_SOURCE
#include "bench.h"
#include "burner.h"

#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SEG (128 * 1024)   /* odd numbers per segment = bytes: fits in L2 on current CPUs */

/* known prime counts pi(10^k) */
static const struct { uint64_t n, count; } known[] = {
    { 10000000ull, 664579ull }, { 100000000ull, 5761455ull }, { 1000000000ull, 50847534ull },
    { 10000000000ull, 455052511ull }, { 100000000000ull, 4118054813ull } };

typedef struct {
    uint64_t n;                 /* count primes <= n */
    const uint32_t *bp;         /* odd base primes <= sqrt(n) */
    size_t nbp;
    _Atomic uint64_t next;      /* next segment index */
    uint64_t nseg;
    volatile int *cancel;
} sieve;

typedef struct { sieve *s; int cpu; uint64_t count; pthread_t t; } sjob;

static void *sieve_worker(void *arg)
{
    sjob *j = arg;
    sieve *s = j->s;
    uint8_t *seg = malloc(SEG);
    cpu_set_t cs;
    if (!seg) return NULL;
    CPU_ZERO(&cs);
    CPU_SET(j->cpu, &cs);
    pthread_setaffinity_np(pthread_self(), sizeof cs, &cs);
    for (;;) {
        uint64_t k = atomic_fetch_add(&s->next, 1), lo, hi, cnt = 0;
        size_t i, len;
        if (k >= s->nseg || (s->cancel && *s->cancel)) break;
        lo = k * (uint64_t)SEG * 2 + 1;              /* segment covers odd numbers lo, lo+2, ... */
        hi = lo + (uint64_t)SEG * 2;                 /* exclusive */
        if (hi > s->n + 1) hi = s->n + 1;
        len = (size_t)((hi - lo + 1) / 2);
        memset(seg, 1, len);
        for (i = 0; i < s->nbp; i++) {
            uint64_t p = s->bp[i], st = p * p, m;
            if (st >= hi) break;
            if (st < lo) { st = (lo + p - 1) / p * p; if (!(st & 1)) st += p; }
            for (m = (st - lo) / 2; m < len; m += p) seg[m] = 0;
        }
        if (k == 0) seg[0] = 0;                      /* 1 is not prime */
        for (i = 0; i < len; i++) cnt += seg[i];
        j->count += cnt;
    }
    free(seg);
    return NULL;
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

typedef struct { uint64_t n, count, want; int threads; double secs; char when[32]; } prctx;

static void prime_render(srs_out *o, void *c)
{
    static const int tabs[] = { 200 };
    prctx *x = c;
    so_siv_header(o, "Prime sieve (count the prime numbers up to a limit)");
    so_tabs(o, tabs, 1);
    so_kvf(o, "Limit", C_VALUE, "%llu", (unsigned long long)x->n);
    so_kvf(o, "Threads", C_VALUE, "%d", x->threads);
    so_kvf(o, "Time", C_GREEN, "%.3f s", x->secs);
    so_kvf(o, "Speed", C_GREEN, "%.0f million numbers per second", x->secs > 0 ? x->n / x->secs / 1e6 : 0);
    so_kvf(o, "Primes found", C_VALUE, "%llu", (unsigned long long)x->count);
    if (x->want) so_kv(o, "Check", x->want == x->count ? C_GREEN : C_RED,
                       x->want == x->count ? "right (matches the known count)" : "WRONG - does not match the known count");
    else so_kv(o, "Check", C_ORANGE, "no known count for this limit (use 10^7 .. 10^11)");
    so_kv(o, "Method", C_TEAL, "segmented sieve of Eratosthenes, odd numbers only, 128 KB segments");
    so_kv(o, "Run", C_GRAY, x->when);
    so_kv(o, "Build", C_GRAY, bench_build_tag());
    so_board_footer(o);
}

int bench_prime(uint64_t n, int threads, srs_out *o, volatile int *cancel)
{
    sieve s;
    sjob js[BURN_MAXCPU];
    prctx c;
    uint32_t *bp;
    uint8_t *small;
    uint64_t r = (uint64_t)sqrt((double)n) + 2, i, m;
    size_t nbp = 0;
    double t0;
    int k, nj = 0;
    cpu_set_t saved;
    time_t now = time(NULL);
    char params[64], res[96];
    if (n < 100 || n > 100000000000ull) return -1;
    if (threads < 1 || threads > burn_ncpu()) threads = burn_ncpu();
    memset(&c, 0, sizeof c);
    t0 = now_s();
    /* base primes up to sqrt(n): plain sieve */
    small = calloc((size_t)r + 1, 1);
    bp = malloc(((size_t)r / 2 + 1) * sizeof *bp);
    if (!small || !bp) { free(small); free(bp); return -1; }
    for (i = 3; i <= r; i += 2) {
        if (small[i]) continue;
        if (i * i <= n) bp[nbp++] = (uint32_t)i;
        for (m = i * i; m <= r; m += 2 * i) small[m] = 1;
    }
    free(small);
    s.n = n; s.bp = bp; s.nbp = nbp; s.cancel = cancel;
    s.nseg = ((n + 1) / 2 + SEG - 1) / SEG;
    atomic_init(&s.next, 0);
    sched_getaffinity(0, sizeof saved, &saved);
    for (k = 0; k < BURN_MAXCPU && nj < threads; k++)
        if (CPU_ISSET(k, &saved)) { js[nj].s = &s; js[nj].cpu = k; js[nj].count = 0; nj++; }
    for (k = 0; k < nj; k++) pthread_create(&js[k].t, NULL, sieve_worker, &js[k]);
    for (k = 0; k < nj; k++) { pthread_join(js[k].t, NULL); c.count += js[k].count; }
    sched_setaffinity(0, sizeof saved, &saved);
    free(bp);
    if (cancel && *cancel) return -2;
    c.count += n >= 2;                                /* the even prime 2 */
    c.secs = now_s() - t0;
    c.n = n;
    c.threads = nj;
    for (k = 0; k < (int)(sizeof known / sizeof *known); k++) if (known[k].n == n) c.want = known[k].count;
    strftime(c.when, sizeof c.when, "%Y-%m-%d %H:%M:%S", localtime(&now));
    bench_save_render("prime", prime_render, &c, o);
    snprintf(params, sizeof params, "limit=%llu threads=%d", (unsigned long long)n, nj);
    snprintf(res, sizeof res, "%.3f s count=%llu %s", c.secs, (unsigned long long)c.count,
             !c.want ? "unchecked" : c.want == c.count ? "checked-ok" : "WRONG");
    bench_history_add("prime", params, res);
    return c.want && c.want != c.count ? 1 : 0;
}
