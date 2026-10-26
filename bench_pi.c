/* bench_pi.c - SRS Pi: pi to N decimal places, checked digit for digit (see bench.h).
 *
 * Written for SRS from the published mathematics, not from another program's code:
 *  - Chudnovsky series (D. V. and G. V. Chudnovsky, 1988) summed by binary splitting;
 *  - Gauss-Legendre / Brent-Salamin arithmetic-geometric mean (1976), the method SuperPi uses;
 *  - big numbers in base 10^8 (so the result is already decimal), multiplied by schoolbook for
 *    small sizes and by a number-theoretic transform (NTT) modulo two primes below 2^31, joined
 *    with the Chinese remainder theorem (Montgomery multiplication; only 32/64-bit integers, so
 *    it also works on 32-bit builds);
 *  - Newton iterations for 1/x and 1/sqrt(x).
 * The digits are checked against SHA-256 checksums of the true digits (computed once with mpmath
 * for the listed sizes), so a wrong result - from a bug or from unstable hardware - is reported. */
#define _GNU_SOURCE
#include "bench.h"
#include "burner.h"

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BASE 100000000u   /* 10^8 per limb */
#define BD 8              /* decimal digits per limb */

/* ======================================================================
 * cancel flag / progress (read by the GUI)
 * ====================================================================== */
static volatile int *g_cancel;
static volatile int g_oom;
static const char *volatile g_stage = "";
const char *pi_stage(void) { return g_stage; }
#define CANCELLED() ((g_cancel && *g_cancel) || g_oom)

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) g_oom = 1;
    return p;
}

/* ======================================================================
 * big integers: sign + magnitude, little-endian limbs in base 10^8
 * ====================================================================== */
typedef struct { uint32_t *d; size_t n, cap; int neg; } bn;

static void bn_init(bn *a) { a->d = NULL; a->n = a->cap = 0; a->neg = 0; }
static void bn_free(bn *a) { free(a->d); bn_init(a); }
static int bn_reserve(bn *a, size_t n)
{
    uint32_t *p;
    if (n <= a->cap) return 1;
    p = realloc(a->d, (n ? n : 1) * sizeof *p);
    if (!p) { g_oom = 1; return 0; }
    a->d = p;
    a->cap = n;
    return 1;
}
static void bn_trim(bn *a) { while (a->n && !a->d[a->n - 1]) a->n--; if (!a->n) a->neg = 0; }
static void bn_set_u64(bn *a, uint64_t v)
{
    a->n = 0;
    a->neg = 0;
    if (!bn_reserve(a, 3)) return;
    while (v) { a->d[a->n++] = (uint32_t)(v % BASE); v /= BASE; }
}
static void bn_swap(bn *a, bn *b) { bn t = *a; *a = *b; *b = t; }

/* a *= m (m < 2^32) */
static void bn_mul_u32(bn *a, uint32_t m)
{
    uint64_t c = 0;
    size_t i;
    for (i = 0; i < a->n; i++) { uint64_t t = (uint64_t)a->d[i] * m + c; a->d[i] = (uint32_t)(t % BASE); c = t / BASE; }
    while (c) { if (!bn_reserve(a, a->n + 1)) return; a->d[a->n++] = (uint32_t)(c % BASE); c /= BASE; }
    bn_trim(a);
}
/* |a| /= m, truncating */
static void bn_div_u32(bn *a, uint32_t m)
{
    uint64_t r = 0;
    size_t i = a->n;
    while (i--) { uint64_t t = r * BASE + a->d[i]; a->d[i] = (uint32_t)(t / m); r = t % m; }
    bn_trim(a);
}
/* drop the k lowest limbs (divide |a| by 10^(8k), truncating) */
static void bn_shr(bn *a, size_t k)
{
    if (k >= a->n) { a->n = 0; a->neg = 0; return; }
    memmove(a->d, a->d + k, (a->n - k) * sizeof *a->d);
    a->n -= k;
}
/* multiply by 10^(8k) */
static void bn_shl(bn *a, size_t k)
{
    if (!a->n || !k) return;
    if (!bn_reserve(a, a->n + k)) return;
    memmove(a->d + k, a->d, a->n * sizeof *a->d);
    memset(a->d, 0, k * sizeof *a->d);
    a->n += k;
}
static void bn_copy(bn *r, const bn *a)
{
    if (!bn_reserve(r, a->n)) return;
    if (a->n) memcpy(r->d, a->d, a->n * sizeof *a->d);
    r->n = a->n;
    r->neg = a->neg;
}
static int mag_cmp(const bn *a, const bn *b)
{
    size_t i;
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (i = a->n; i--;) if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}
/* r = |a| + |b| (r may not alias) */
static void mag_add(bn *r, const bn *a, const bn *b)
{
    size_t n = a->n > b->n ? a->n : b->n, i;
    uint32_t c = 0;
    if (!bn_reserve(r, n + 1)) return;
    for (i = 0; i < n; i++) {
        uint32_t t = (i < a->n ? a->d[i] : 0) + (i < b->n ? b->d[i] : 0) + c;
        c = t >= BASE;
        r->d[i] = c ? t - BASE : t;
    }
    r->d[n] = c;
    r->n = n + 1;
    r->neg = 0;
    bn_trim(r);
}
/* r = |a| - |b|, |a| >= |b| */
static void mag_sub(bn *r, const bn *a, const bn *b)
{
    size_t i;
    int64_t c = 0;
    if (!bn_reserve(r, a->n)) return;
    for (i = 0; i < a->n; i++) {
        int64_t t = (int64_t)a->d[i] - (i < b->n ? b->d[i] : 0) - c;
        c = t < 0;
        r->d[i] = (uint32_t)(c ? t + BASE : t);
    }
    r->n = a->n;
    r->neg = 0;
    bn_trim(r);
}
/* r = a + b (signed; r may not alias a or b) */
static void bn_add(bn *r, const bn *a, const bn *b)
{
    if (a->neg == b->neg) { mag_add(r, a, b); r->neg = r->n ? a->neg : 0; return; }
    if (mag_cmp(a, b) >= 0) { mag_sub(r, a, b); r->neg = r->n ? a->neg : 0; }
    else { mag_sub(r, b, a); r->neg = r->n ? b->neg : 0; }
}

/* ---------------------------------------------------------------- NTT multiplication */
#define P1 2013265921u   /* 15 * 2^27 + 1, primitive root 31 */
#define P2 469762049u    /*  7 * 2^26 + 1, primitive root 3  */
#define NTT_MAXLOG 26

static inline uint32_t addm(uint32_t a, uint32_t b, uint32_t p) { uint32_t s = a + b; return s >= p ? s - p : s; }
static inline uint32_t subm(uint32_t a, uint32_t b, uint32_t p) { return a >= b ? a - b : a + p - b; }
static inline uint32_t mulm(uint32_t a, uint32_t b, uint32_t p) { return (uint32_t)((uint64_t)a * b % p); }
static uint32_t powm(uint32_t b, uint64_t e, uint32_t p)
{
    uint32_t r = 1;
    while (e) { if (e & 1) r = mulm(r, b, p); b = mulm(b, b, p); e >>= 1; }
    return r;
}

/* Montgomery multiplication (R = 2^32): mont(a, b) = a b / R mod p, for a, b < p < 2^31.
   The transforms keep the data in normal form and the twiddle factors in Montgomery form
   (w R mod p), so mont(x, w R) = x w; no division in the inner loops on any compiler. */
static inline uint32_t mont(uint32_t a, uint32_t b, uint32_t p, uint32_t pinv)
{
    uint64_t t = (uint64_t)a * b;
    uint32_t m = (uint32_t)t * pinv;
    uint32_t u = (uint32_t)((t + (uint64_t)m * p) >> 32);
    return u >= p ? u - p : u;
}
static uint32_t mont_pinv(uint32_t p)   /* -p^-1 mod 2^32 (Newton) */
{
    uint32_t x = p;
    int i;
    for (i = 0; i < 5; i++) x *= 2 - p * x;
    return (uint32_t)0 - x;
}

/* tw[h + j] = w_{2h}^j R mod p for h = 1, 2, 4 .. n/2 and j < h (one table for every stage, contiguous) */
static inline __attribute__((always_inline)) void make_tw(uint32_t *tw, size_t n, uint32_t root, int inv, uint32_t p)
{
    size_t h, j;
    uint32_t r1 = (uint32_t)(((uint64_t)1 << 32) % p);
    for (h = 1; h < n; h <<= 1) {
        uint32_t w = powm(root, (p - 1) / (2 * h), p), x = r1;
        if (inv) w = powm(w, p - 2, p);
        for (j = 0; j < h; j++) { tw[h + j] = x; x = mulm(x, w, p); }
    }
}
/* forward: natural order in, bit-reversed out (decimation in frequency) */
static inline __attribute__((always_inline)) void ntt_dif(uint32_t *a, size_t n, const uint32_t *tw, uint32_t p, uint32_t pinv)
{
    size_t h, s, j;
    for (h = n >> 1; h; h >>= 1)
        for (s = 0; s < n; s += 2 * h)
            for (j = 0; j < h; j++) {
                uint32_t u = a[s + j], v = a[s + j + h];
                a[s + j] = addm(u, v, p);
                a[s + j + h] = mont(subm(u, v, p), tw[h + j], p, pinv);
            }
}
/* inverse: bit-reversed in, natural order out (decimation in time), without the 1/n factor */
static inline __attribute__((always_inline)) void ntt_dit(uint32_t *a, size_t n, const uint32_t *tw, uint32_t p, uint32_t pinv)
{
    size_t h, s, j;
    for (h = 1; h < n; h <<= 1)
        for (s = 0; s < n; s += 2 * h)
            for (j = 0; j < h; j++) {
                uint32_t u = a[s + j], v = mont(a[s + j + h], tw[h + j], p, pinv);
                a[s + j] = addm(u, v, p);
                a[s + j + h] = subm(u, v, p);
            }
}
/* split base-10^8 limbs into base-10^4 coefficients */
static void to_coef(uint32_t *f, size_t n, const uint32_t *a, size_t na)
{
    size_t i;
    for (i = 0; i < na; i++) { f[2 * i] = a[i] % 10000; f[2 * i + 1] = a[i] / 10000; }
    memset(f + 2 * na, 0, (n - 2 * na) * sizeof *f);
}
/* cyclic convolution of a and b modulo p, result in fa (fb is scratch; NULL fb = squaring) */
static inline __attribute__((always_inline)) void conv_mod(uint32_t *fa, uint32_t *fb, uint32_t *tw, size_t n,
                                                           const bn *a, const bn *b, uint32_t p, uint32_t g)
{
    size_t i;
    uint32_t pinv = mont_pinv(p), r2 = mulm((uint32_t)(((uint64_t)1 << 32) % p), (uint32_t)(((uint64_t)1 << 32) % p), p);
    /* the pointwise product leaves a factor 1/R; scaling by (1/n) R^2 in Montgomery form removes it */
    uint32_t k = mulm(powm((uint32_t)(n % p), p - 2, p), r2, p);
    make_tw(tw, n, g, 0, p);
    to_coef(fa, n, a->d, a->n);
    ntt_dif(fa, n, tw, p, pinv);
    if (fb) {
        to_coef(fb, n, b->d, b->n);
        ntt_dif(fb, n, tw, p, pinv);
        for (i = 0; i < n; i++) fa[i] = mont(fa[i], fb[i], p, pinv);
    } else {
        for (i = 0; i < n; i++) fa[i] = mont(fa[i], fa[i], p, pinv);
    }
    make_tw(tw, n, g, 1, p);
    ntt_dit(fa, n, tw, p, pinv);
    for (i = 0; i < n; i++) fa[i] = mont(fa[i], k, p, pinv);
}
static void conv_p1(uint32_t *fa, uint32_t *fb, uint32_t *tw, size_t n, const bn *a, const bn *b) { conv_mod(fa, fb, tw, n, a, b, P1, 31); }
static void conv_p2(uint32_t *fa, uint32_t *fb, uint32_t *tw, size_t n, const bn *a, const bn *b) { conv_mod(fa, fb, tw, n, a, b, P2, 3); }

static int mul_ntt(bn *r, const bn *a, const bn *b)
{
    size_t cl = 2 * (a->n + b->n), n = 1, i;
    uint32_t *r1, *fa, *fb = NULL, *tw, inv = powm(P1 % P2, P2 - 2, P2);
    uint64_t carry = 0;
    int sq = a == b;
    while (n < cl) n <<= 1;
    if (n > ((size_t)1 << NTT_MAXLOG)) { g_oom = 1; return 0; }
    r1 = xmalloc(n * sizeof *r1);
    fa = xmalloc(n * sizeof *fa);
    tw = xmalloc(n * sizeof *tw);
    if (!sq) fb = xmalloc(n * sizeof *fb);
    if (!r1 || !fa || !tw || (!sq && !fb) || !bn_reserve(r, a->n + b->n + 1)) { free(r1); free(fa); free(fb); free(tw); return 0; }
    conv_p1(r1, fb, tw, n, a, sq ? a : b);
    conv_p2(fa, fb, tw, n, a, sq ? a : b);
    /* CRT: x = r1 + P1 * ((r2 - r1) / P1 mod P2), x < P1 * P2 < 2^60; then carry in base 10^4 */
    for (i = 0; i < cl; i += 2) {
        uint64_t x0, x1, lo, hi;
        uint32_t k;
        k = mulm(subm(fa[i], r1[i] % P2, P2), inv, P2);
        x0 = r1[i] + (uint64_t)P1 * k + carry;
        lo = x0 % 10000;
        carry = x0 / 10000;
        k = mulm(subm(fa[i + 1], r1[i + 1] % P2, P2), inv, P2);
        x1 = r1[i + 1] + (uint64_t)P1 * k + carry;
        hi = x1 % 10000;
        carry = x1 / 10000;
        r->d[i / 2] = (uint32_t)(lo + hi * 10000);
    }
    r->n = cl / 2;
    while (carry) { if (!bn_reserve(r, r->n + 1)) break; r->d[r->n++] = (uint32_t)(carry % BASE); carry /= BASE; }
    free(r1); free(fa); free(fb); free(tw);
    return 1;
}

static void mul_school(bn *r, const bn *a, const bn *b)
{
    size_t i, j;
    if (!bn_reserve(r, a->n + b->n)) return;
    memset(r->d, 0, (a->n + b->n) * sizeof *r->d);
    for (i = 0; i < a->n; i++) {
        uint64_t c = 0, ai = a->d[i];
        if (!ai) continue;
        for (j = 0; j < b->n; j++) {
            uint64_t t = r->d[i + j] + ai * b->d[j] + c;
            r->d[i + j] = (uint32_t)(t % BASE);
            c = t / BASE;
        }
        r->d[i + b->n] = (uint32_t)c;
    }
    r->n = a->n + b->n;
}

/* r = a * b; r may not alias a or b */
static void bn_mul(bn *r, const bn *a, const bn *b)
{
    size_t mn = a->n < b->n ? a->n : b->n;
    if (!a->n || !b->n) { r->n = 0; r->neg = 0; return; }
    if (mn <= 48 || (mn <= 400 && a->n + b->n > 40 * mn)) mul_school(r, a, b);
    else mul_ntt(r, a, b);
    r->neg = a->neg ^ b->neg;
    bn_trim(r);
}
/* a = a * b */
static void bn_mul_to(bn *a, const bn *b)
{
    bn t;
    bn_init(&t);
    bn_mul(&t, a, b);
    bn_swap(a, &t);
    bn_free(&t);
}

/* ======================================================================
 * Chudnovsky by binary splitting
 *   P(a,b), Q(a,b), T(a,b); leaf k: P = -(6k-5)(2k-1)(6k-1), Q = k^3 C^3/24, T = P (A + B k)
 *   pi = 426880 sqrt(10005) Q(0,N) / T(0,N)
 * ====================================================================== */
typedef struct { bn P, Q, T; } pqt;
static void pqt_init(pqt *x) { bn_init(&x->P); bn_init(&x->Q); bn_init(&x->T); }
static void pqt_free(pqt *x) { bn_free(&x->P); bn_free(&x->Q); bn_free(&x->T); }

static void leaf(uint64_t k, pqt *r)
{
    bn t;
    if (!k) { bn_set_u64(&r->P, 1); bn_set_u64(&r->Q, 1); bn_set_u64(&r->T, 13591409); return; }
    bn_set_u64(&r->P, 6 * k - 5);
    bn_mul_u32(&r->P, (uint32_t)(2 * k - 1));
    bn_mul_u32(&r->P, (uint32_t)(6 * k - 1));
    r->P.neg = 1;
    bn_set_u64(&r->Q, 10939058860032000ull);   /* 640320^3 / 24 */
    bn_mul_u32(&r->Q, (uint32_t)k);
    bn_mul_u32(&r->Q, (uint32_t)k);
    bn_mul_u32(&r->Q, (uint32_t)k);
    bn_init(&t);
    bn_set_u64(&t, 13591409ull + 545140134ull * k);
    bn_mul(&r->T, &r->P, &t);
    bn_free(&t);
}

typedef struct { const bn *a, *b; bn *r; } mjob;
static void *mjob_run(void *p) { mjob *j = p; bn_mul(j->r, j->a, j->b); return NULL; }

static void bs(uint64_t a, uint64_t b, pqt *r, int needP, int threads);
typedef struct { uint64_t a, b; pqt *r; int threads; } bsjob;
static void *bs_run(void *p) { bsjob *j = p; bs(j->a, j->b, j->r, 1, j->threads); return NULL; }

static void bs(uint64_t a, uint64_t b, pqt *r, int needP, int threads)
{
    pqt L, R;
    uint64_t m;
    bn x, y;
    if (CANCELLED()) return;
    if (b - a == 1) { leaf(a, r); return; }
    m = (a + b) / 2;
    pqt_init(&L);
    pqt_init(&R);
    if (threads > 1 && b - a > 64) {
        pthread_t t;
        bsjob j = { a, m, &L, threads / 2 };
        if (pthread_create(&t, NULL, bs_run, &j) == 0) {
            bs(m, b, &R, needP, threads - threads / 2);
            pthread_join(t, NULL);
        } else {
            bs(a, m, &L, 1, 1);
            bs(m, b, &R, needP, 1);
        }
    } else {
        bs(a, m, &L, 1, 1);
        bs(m, b, &R, needP, 1);
    }
    if (CANCELLED()) { pqt_free(&L); pqt_free(&R); return; }
    bn_init(&x);
    bn_init(&y);
    if (threads > 1 && b - a > 64) {   /* the four products are independent: run them side by side */
        mjob j[4] = { { &L.Q, &R.Q, &r->Q }, { &R.Q, &L.T, &x }, { &L.P, &R.T, &y }, { &L.P, &R.P, &r->P } };
        pthread_t t[4];
        int k, nj = needP ? 4 : 3, ok[4] = { 0 };
        for (k = 1; k < nj; k++) ok[k] = pthread_create(&t[k], NULL, mjob_run, &j[k]) == 0;
        mjob_run(&j[0]);
        for (k = 1; k < nj; k++) { if (ok[k]) pthread_join(t[k], NULL); else mjob_run(&j[k]); }
    } else {
        bn_mul(&r->Q, &L.Q, &R.Q);
        bn_mul(&x, &R.Q, &L.T);
        bn_mul(&y, &L.P, &R.T);
        if (needP) bn_mul(&r->P, &L.P, &R.P);
    }
    bn_add(&r->T, &x, &y);
    bn_free(&x);
    bn_free(&y);
    pqt_free(&L);
    pqt_free(&R);
}

/* ======================================================================
 * fixed point: value = M / 10^(8 s) (M a bn, s fractional limbs)
 * ====================================================================== */
/* change the scale of M from s to s2 (truncating when s2 < s) */
static void fx_rescale(bn *m, size_t s, size_t s2) { if (s2 < s) bn_shr(m, s - s2); else bn_shl(m, s2 - s); }

/* one = 10^(8 s) */
static void fx_one(bn *m, size_t s)
{
    if (!bn_reserve(m, s + 1)) return;
    memset(m->d, 0, (s + 1) * sizeof *m->d);
    m->d[s] = 1;
    m->n = s + 1;
    m->neg = 0;
}

/* Y / 10^(8 prec) ~= 10^(8 n) / T, where T has n limbs (Newton: y += y (1 - t y)) */
static void fx_recip(bn *Y, const bn *T, size_t prec)
{
    double tv = 0;
    size_t p = 1, i;
    bn tt, E, one, D, C, S;
    for (i = 0; i < 3 && i < T->n; i++) tv += T->d[T->n - 1 - i] / pow(1e8, (double)(i + 1));
    {
        double y0 = 1.0 / tv * 1e8;   /* scale 1 limb */
        bn_set_u64(Y, (uint64_t)y0);
    }
    bn_init(&tt); bn_init(&E); bn_init(&one); bn_init(&D); bn_init(&C); bn_init(&S);
    for (;;) {
        size_t p2 = p * 2 < prec ? p * 2 : prec;
        if (CANCELLED()) break;
        /* tt = t at scale p2+2 */
        bn_copy(&tt, T);
        tt.neg = 0;
        if (T->n > p2 + 2) bn_shr(&tt, T->n - (p2 + 2)); else bn_shl(&tt, p2 + 2 - T->n);
        fx_rescale(Y, p, p2);                         /* Y at scale p2 */
        bn_mul(&E, &tt, Y);                           /* scale 2 p2 + 2 */
        bn_shr(&E, p2 + 1);                           /* scale p2 + 1 */
        fx_one(&one, p2 + 1);
        E.neg = !E.neg;
        bn_add(&D, &one, &E);                         /* 1 - t y */
        bn_mul(&C, Y, &D);                            /* scale 2 p2 + 1 */
        bn_shr(&C, p2 + 1);                           /* scale p2 */
        bn_add(&S, Y, &C);
        bn_swap(Y, &S);
        if (p == prec && p2 == prec) break;           /* one extra step at full precision done */
        p = p2;
    }
    bn_free(&tt); bn_free(&E); bn_free(&one); bn_free(&D); bn_free(&C); bn_free(&S);
}

/* X / 10^(8 prec) ~= 1 / sqrt(a) for a small integer a (Newton: x += x (1 - a x^2) / 2) */
static void fx_invsqrt(bn *X, uint32_t a, size_t prec)
{
    size_t p = 2;
    bn S, one, E, C, N;
    bn_set_u64(X, (uint64_t)(1.0 / sqrt((double)a) * 1e16));
    bn_init(&S); bn_init(&one); bn_init(&E); bn_init(&C); bn_init(&N);
    for (;;) {
        size_t p2 = p * 2 < prec ? p * 2 : prec;
        if (CANCELLED()) break;
        fx_rescale(X, p, p2);
        bn_mul(&S, X, X);                             /* scale 2 p2 */
        bn_shr(&S, p2 - 1);                           /* scale p2 + 1 */
        bn_mul_u32(&S, a);
        fx_one(&one, p2 + 1);
        S.neg = !S.neg;
        bn_add(&E, &one, &S);                         /* 1 - a x^2 */
        bn_mul(&C, X, &E);                            /* scale 2 p2 + 1 */
        bn_shr(&C, p2 + 1);
        bn_div_u32(&C, 2);
        bn_add(&N, X, &C);
        bn_swap(X, &N);
        if (p == prec && p2 == prec) break;
        p = p2;
    }
    bn_free(&S); bn_free(&one); bn_free(&E); bn_free(&C); bn_free(&N);
}

/* the fractional digits of Z / 10^(8 s) (integer part must be 3) -> out[0..digits) */
static int fx_digits(const bn *Z, size_t s, char *out, size_t digits)
{
    size_t i, k = 0;
    if (Z->n != s + 1 || Z->d[s] != 3) return 0;
    for (i = s; i-- && k < digits;) {
        char b[16];
        size_t j;
        snprintf(b, sizeof b, "%08u", Z->d[i]);
        for (j = 0; j < BD && k < digits; j++) out[k++] = b[j];
    }
    out[k] = 0;
    return k == digits;
}

static int pi_chudnovsky(size_t digits, int threads, char *out)
{
    size_t L = (digits + BD - 1) / BD + 3, s;
    uint64_t terms = (uint64_t)(digits / 14.181647462725477) + 2;
    pqt r;
    bn Y, X, Z, t;
    int ok;
    pqt_init(&r);
    g_stage = "Series (binary splitting)";
    bs(0, terms, &r, 0, threads);
    if (CANCELLED()) { pqt_free(&r); return 0; }
    /* only the leading L + 4 limbs of Q and T matter for Q / T */
    if (r.T.n > L + 4) { s = r.T.n - (L + 4); bn_shr(&r.T, s); bn_shr(&r.Q, s); }
    bn_init(&Y); bn_init(&X); bn_init(&Z); bn_init(&t);
    g_stage = "Division (Newton reciprocal)";
    fx_recip(&Y, &r.T, L + 3);                       /* 1/T = Y / 10^(8 (L+3+nT)) */
    g_stage = "Square root of 10005 (Newton)";
    fx_invsqrt(&X, 10005, L + 3);                    /* 1/sqrt(10005) = X / 10^(8 (L+3)) */
    g_stage = "Final products";
    bn_mul(&Z, &r.Q, &Y);                            /* scale L+3 + nT */
    bn_shr(&Z, (L + 3 + r.T.n) - (L + 2));           /* scale L+2 */
    bn_mul(&t, &Z, &X);                              /* scale 2L+5 */
    bn_shr(&t, L + 3);                               /* scale L+2 */
    bn_mul_u32(&t, 426880);
    bn_mul_u32(&t, 10005);                           /* sqrt(10005) = 10005 / sqrt(10005) */
    ok = !CANCELLED() && fx_digits(&t, L + 2, out, digits);
    pqt_free(&r);
    bn_free(&Y); bn_free(&X); bn_free(&Z); bn_free(&t);
    return ok;
}

/* ======================================================================
 * Gauss-Legendre (AGM): a=1, b=1/sqrt2, t=1/4, p=1;
 *   a' = (a+b)/2, b' = sqrt(a b), t' = t - p (a - a')^2, p' = 2p;  pi ~ (a+b)^2 / (4t)
 * all fixed point at scale s; sqrt(x) = x / sqrt(x) via the reciprocal square root of x.
 * ====================================================================== */
/* X / 10^(8 s) ~= 1/sqrt(A / 10^(8 s)) for a fixed-point A, 0 < A < 1 (Newton) */
static void fx_invsqrt_fx(bn *X, const bn *A, size_t s)
{
    size_t p = 2, i;
    double av = 0;
    bn Ap, S, one, E, C, N;
    for (i = 0; i < 3 && i < A->n; i++) av += A->d[A->n - 1 - i] * pow(1e8, (double)A->n - 1 - i - (double)s);
    bn_set_u64(X, (uint64_t)(1.0 / sqrt(av) * 1e16));
    bn_init(&Ap); bn_init(&S); bn_init(&one); bn_init(&E); bn_init(&C); bn_init(&N);
    for (;;) {
        size_t p2 = p * 2 < s ? p * 2 : s;
        if (CANCELLED()) break;
        fx_rescale(X, p, p2);
        bn_copy(&Ap, A);
        fx_rescale(&Ap, s, p2 + 1);                   /* A at scale p2+1 */
        bn_mul(&S, X, X);                             /* scale 2 p2 */
        bn_shr(&S, p2);                               /* scale p2 */
        bn_mul_to(&S, &Ap);                           /* scale 2 p2 + 1 */
        bn_shr(&S, p2);                               /* scale p2 + 1 */
        fx_one(&one, p2 + 1);
        S.neg = !S.neg;
        bn_add(&E, &one, &S);
        bn_mul(&C, X, &E);
        bn_shr(&C, p2 + 1);
        bn_div_u32(&C, 2);
        bn_add(&N, X, &C);
        bn_swap(X, &N);
        if (p == s && p2 == s) break;
        p = p2;
    }
    bn_free(&Ap); bn_free(&S); bn_free(&one); bn_free(&E); bn_free(&C); bn_free(&N);
}

static int pi_agm(size_t digits, char *out)
{
    size_t s = (digits + BD - 1) / BD + 3;
    int it, maxit = 1, ok;
    bn a, b, t, A, B, D, X, U;
    double prec = (double)s * BD;
    uint32_t pw = 1;
    while ((double)(1ull << maxit) < prec * 1.1 && maxit < 40) maxit++;   /* digits double each step */
    bn_init(&a); bn_init(&b); bn_init(&t); bn_init(&A); bn_init(&B); bn_init(&D); bn_init(&X); bn_init(&U);
    fx_one(&a, s);
    fx_invsqrt(&b, 2, s);                            /* 1/sqrt(2) */
    fx_one(&t, s);
    bn_div_u32(&t, 4);
    for (it = 0; it < maxit + 2 && !CANCELLED(); it++) {
        static char st[64];
        size_t dn;
        snprintf(st, sizeof st, "AGM step %d of about %d", it + 1, maxit);
        g_stage = st;
        mag_add(&A, &a, &b);                         /* a + b (both positive) */
        bn_div_u32(&A, 2);                           /* a' */
        bn_mul(&U, &a, &b);                          /* scale 2s */
        bn_shr(&U, s);                               /* ab, scale s */
        fx_invsqrt_fx(&X, &U, s);                    /* 1/sqrt(ab) */
        bn_mul(&B, &U, &X);
        bn_shr(&B, s);                               /* b' = ab / sqrt(ab) */
        if (mag_cmp(&a, &A) >= 0) mag_sub(&D, &a, &A); else mag_sub(&D, &A, &a);
        dn = D.n;
        bn_mul(&U, &D, &D);
        bn_shr(&U, s);                               /* (a - a')^2 */
        bn_mul_u32(&U, (uint32_t)pw);                /* times p = 2^it */
        U.neg = 1;
        bn_add(&D, &t, &U);
        bn_swap(&t, &D);
        bn_swap(&a, &A);
        bn_swap(&b, &B);
        pw <<= 1;
        if (2 * dn + 3 < s) break;                   /* (a - a')^2 p is below the last digit: converged */
    }
    g_stage = "Final division";
    mag_add(&A, &a, &b);
    bn_mul(&U, &A, &A);                              /* (a+b)^2, scale 2s */
    bn_mul_u32(&t, 4);                               /* 4t, scale s */
    {
        bn Y;
        size_t tn = t.n;
        bn_init(&Y);
        fx_recip(&Y, &t, s + 2);                     /* 1/(4t) = Y / 10^(8 (s+2) + 8 (tn - s)) ... see below */
        bn_mul(&B, &U, &Y);                          /* U/10^(16s) * Y / 10^(8(s+2+tn)) * 10^(8 s) */
        /* value = B / 10^(8 (2s + s+2 + tn - s)) = B / 10^(8 (2s + 2 + tn)) -> want scale s */
        bn_shr(&B, (2 * s + 2 + tn) - s);
        bn_free(&Y);
    }
    ok = !CANCELLED() && fx_digits(&B, s, out, digits);
    bn_free(&a); bn_free(&b); bn_free(&t); bn_free(&A); bn_free(&B); bn_free(&D); bn_free(&X); bn_free(&U);
    return ok;
}

/* ======================================================================
 * checking, timing, results
 * ====================================================================== */
/* SHA-256 of the first N decimal places of pi (the digits after "3."), computed with mpmath */
static const struct { size_t n; const char *sha, *tail; } pi_ref[] = {
    { 128000, "5f0cb206f0e1583f888737d556feb3dfdff6dd2302a326a799eded6f2a596e5c", "31618211502789264307" },
    { 1000000, "7806ee47461b49ef1f578e14461b2c83c09c6d7a9a914275da1d71e9cbbf7069", "22090106105779458151" },
    { 2000000, "efcbafbaf15047f32f70d8b1b008bac527e9f76d4d4b995d7f36dfd6dd8dc7ed", "36871065191457297909" },
    { 4000000, "3b447adf831964fd071c07652456f1f4ff709d04af4be9caf81c41b642f566f9", "59674697473349203390" },
    { 8000000, "d553ef1b09a588e5cbbfc8feee121ca8956da15c0d823245cb16134ae5c905c4", "80521868811148966164" },
    { 16000000, "1531a9f5bbbd9050f21b695a815f8f5fa4b9a2879857b72f1ba43566bd7021fd", "72458731614311886858" },
    { 32000000, "2e6d0f509bc9b87eb891a8144e3b51420cd79dd620f8e4b73f74ec9a306a3d8d", "92127277244129012784" },
    /* SuperPi sizes (powers of two); the first 32,000,000 digits of these were checked against a
       second, separate mpmath run */
    { 16384, "7e0fbde2cdea91d86396eb8d585cd07267460ca7b22871d2985dc0568e3cbd9b", "31002797665935838758" },
    { 32768, "ef215fb31d300d51ba7817735f8b614897313d16b7253a242072e39336fbec0b", "53471885372293096161" },
    { 65536, "1654e3aaf15083b22841608f39ce0533724f5afc57b3332cc0f40c6c13274044", "54302210909472721073" },
    { 131072, "adb3261f4cdd37f5c0074920e64a3fc9d1675280c05a11c097eab256ec94d1ed", "27594750178122678581" },
    { 262144, "0fe92c3203bc1a034ae1f1a169ad3585020438d26dd4e5ae4750021eb2dae865", "74841548524687471869" },
    { 524288, "b2f02b62d35d62d05eba866342eab6fff0860abb37467fcc26efd911355a9fe1", "69678446051551761653" },
    { 1048576, "f09ba8137bd6aa789764f666c119b58db5cd7d313ed8be7967af230cb1bd44b1", "33170994591637429204" },
    { 2097152, "255c1ccf090905850d107cd57438f3d1e362b5c5b55043aa54ce45f3fc47c96b", "63207750323664312379" },
    { 4194304, "e346ccbe066c59a70aa0f9d1b1f65e0d1a0b6a896d19e41fa73630c8009175ed", "80258565140638311120" },
    { 8388608, "96fdc3cd30fa04b038b5e30adfd6353c58b7cb19dc771bb0c2656e471ab49534", "17423342216340425119" },
    { 16777216, "65ca4431b92e284febbd187790f0ccb03287ba43bf8c76b3cc0fa736b52424fb", "28229791155273188571" },
    { 33554432, "3ed4c8bbd858967342e3d883e2037165280ec2a62e81395a0f3af666d89c103e", "49255830905226097306" },
};
static const char pi_head[] = "14159265358979323846264338327950288419716939937510"
                              "58209749445923078164062862089986280348253421170679";

/* time on a Ryzen 7 7800X3D, Chudnovsky, 1 thread, package build; slower CPUs take longer */
const pi_size pi_size_tab[] = {
    { 128000, 0, "128 thousand digits (under a second)" }, { 1000000, 0, "1 million digits (about 3 s)" },
    { 2000000, 0, "2 million digits (about 8 s)" }, { 4000000, 0, "4 million digits (about 18 s)" },
    { 8000000, 0, "8 million digits (about 40 s)" }, { 16000000, 0, "16 million digits (about 1.5 minutes)" },
    { 32000000, 0, "32 million digits (about 3-4 minutes, 1-2 GB of memory)" },
    { 16384, 1, "16K = 16,384 digits" }, { 32768, 1, "32K = 32,768 digits" }, { 65536, 1, "64K = 65,536 digits" },
    { 131072, 1, "128K = 131,072 digits (under a second)" }, { 262144, 1, "256K = 262,144 digits" },
    { 524288, 1, "512K = 524,288 digits" }, { 1048576, 1, "1M = 1,048,576 digits (about 4 s)" },
    { 2097152, 1, "2M = 2,097,152 digits (about 10 s)" }, { 4194304, 1, "4M = 4,194,304 digits (about 20 s)" },
    { 8388608, 1, "8M = 8,388,608 digits (about 45 s)" }, { 16777216, 1, "16M = 16,777,216 digits (about 2 minutes)" },
    { 33554432, 1, "32M = 33,554,432 digits (about 4-5 minutes, 1-2 GB of memory)" } };
const int pi_size_count = sizeof pi_size_tab / sizeof *pi_size_tab;

/* 1 = every digit checked and right, 0 = wrong, -1 = only the first 100 digits could be checked */
static int pi_check(const char *d, size_t n, char *how, size_t hn)
{
    size_t i, hl = n < 100 ? n : 100;
    char hex[65];
    if (memcmp(d, pi_head, hl)) { snprintf(how, hn, "wrong - the first 100 digits do not match"); return 0; }
    for (i = 0; i < sizeof pi_ref / sizeof *pi_ref; i++) {
        if (pi_ref[i].n != n || !*pi_ref[i].sha) continue;
        sha256_hex(d, n, hex);
        if (strcmp(hex, pi_ref[i].sha)) { snprintf(how, hn, "wrong - checksum differs (SHA-256 %.16s...)", hex); return 0; }
        snprintf(how, hn, "every digit right (SHA-256 of all %zu digits matches; last digits ...%s)", n, pi_ref[i].tail);
        return 1;
    }
    snprintf(how, hn, "first %zu digits right; no checksum for %zu digits (use a listed size)", hl, n);
    return -1;
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

typedef struct { size_t digits; int threads, algo, check; double secs; char how[160], last[24], when[32]; } pictx;

static void pi_render(srs_out *o, void *c)
{
    static const int tabs[] = { 200 };
    pictx *x = c;
    char t[64];
    so_siv_header(o, "SRS Pi (pi to many decimal places, every digit checked)");
    so_tabs(o, tabs, 1);
    if (x->digits >= 16384 && !(x->digits & (x->digits - 1)))
        snprintf(t, sizeof t, "%zu decimal places (SuperPi size %zu%s)", x->digits,
                 x->digits >= 1048576 ? x->digits >> 20 : x->digits >> 10, x->digits >= 1048576 ? "M" : "K");
    else snprintf(t, sizeof t, "%zu decimal places", x->digits);
    so_kv(o, "Digits", C_VALUE, t);
    so_kv(o, "Method", C_TEAL, x->algo == PI_AGM ? "Gauss-Legendre (AGM), the method SuperPi uses" :
                                                   "Chudnovsky series, binary splitting");
    so_kvf(o, "Threads", C_VALUE, "%d%s", x->threads, x->algo == PI_AGM && x->threads > 1 ? " (AGM runs on one thread)" : "");
    so_kvf(o, "Time", C_GREEN, "%.3f s", x->secs);
    so_kv(o, "Check", x->check > 0 ? C_GREEN : x->check < 0 ? C_ORANGE : C_RED, x->how);
    so_kvf(o, "Last 20 digits", C_VALUE, "...%s", x->last);
    so_kv(o, "Run", C_GRAY, x->when);
    so_kv(o, "Build", C_GRAY, bench_build_tag());
    so_board_footer(o);
}

int bench_pi(size_t digits, int threads, int algo, srs_out *o, volatile int *cancel, char **digits_out)
{
    pictx c;
    char *d, name[48], params[96], res[96];
    double t0;
    int ok;
    time_t now = time(NULL);
    if (digits < 100 || digits > PI_MAXDIGITS) return -1;
    if (threads < 1) threads = 1;
    if (threads > burn_ncpu()) threads = burn_ncpu();
    d = malloc(digits + 1);
    if (!d) return -1;
    g_cancel = cancel;
    g_oom = 0;
    t0 = now_s();
    ok = algo == PI_AGM ? pi_agm(digits, d) : pi_chudnovsky(digits, threads, d);
    memset(&c, 0, sizeof c);
    c.secs = now_s() - t0;
    g_stage = "";
    if (cancel && *cancel) { free(d); g_cancel = NULL; return -2; }
    g_cancel = NULL;
    if (g_oom) { free(d); return -3; }
    c.digits = digits;
    c.threads = algo == PI_AGM ? 1 : threads;
    c.algo = algo;
    c.check = ok ? pi_check(d, digits, c.how, sizeof c.how) : 0;
    if (!ok) snprintf(c.how, sizeof c.how, "wrong - the result is not 3.14...");
    snprintf(c.last, sizeof c.last, "%s", ok ? d + digits - 20 : "-");
    strftime(c.when, sizeof c.when, "%Y-%m-%d %H:%M:%S", localtime(&now));
    snprintf(name, sizeof name, "pi");
    bench_save_render(name, pi_render, &c, o);
    snprintf(params, sizeof params, "digits=%zu method=%s threads=%d", digits, algo == PI_AGM ? "agm" : "chudnovsky", c.threads);
    snprintf(res, sizeof res, "%.3f s %s", c.secs, c.check > 0 ? "checked-ok" : c.check < 0 ? "partly-checked" : "WRONG");
    bench_history_add("pi", params, res);
    if (digits_out) *digits_out = d; else free(d);
    return c.check == 0 ? 1 : 0;
}
