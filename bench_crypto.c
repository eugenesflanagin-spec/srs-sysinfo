/* bench_crypto.c - Crypto benchmark: AES-128 (CTR mode) and SHA-256, plain C and with the CPU's
 * crypto instructions (AES-NI, SHA-NI on x86), one thread and all threads (see bench.h).
 * Written for SRS from the standards: FIPS 197 (AES) and FIPS 180-4 (SHA-256); the hardware
 * paths follow the instruction descriptions in the Intel 64 and IA-32 Architectures Software
 * Developer's Manual. Checked against the FIPS test vectors before every run. */
#define _GNU_SOURCE
#include "bench.h"
#include "burner.h"

#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__x86_64__) || defined(__i386__)
#define SRS_X86 1
#include <cpuid.h>
#include <immintrin.h>
#endif

/* ======================================================================
 * SHA-256 (FIPS 180-4)
 * ====================================================================== */
static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_blocks_c(uint32_t h[8], const unsigned char *p, size_t nb)
{
    uint32_t w[64], a, b, c, d, e, f, g, k, t1, t2;
    int i;
    while (nb--) {
        for (i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
        for (i = 16; i < 64; i++) {
            uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; k = h[7];
        for (i = 0; i < 64; i++) {
            t1 = k + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
            t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
        p += 64;
    }
}

#ifdef SRS_X86
__attribute__((target("sha,sse4.1,ssse3")))
static void sha256_blocks_ni(uint32_t h[8], const unsigned char *p, size_t nb)
{
    const __m128i bswap = _mm_set_epi64x(0x0c0d0e0f08090a0bULL, 0x0405060700010203ULL);
    __m128i t = _mm_loadu_si128((const __m128i *)&h[0]), s1 = _mm_loadu_si128((const __m128i *)&h[4]), s0;
    t = _mm_shuffle_epi32(t, 0xB1);               /* CDAB */
    s1 = _mm_shuffle_epi32(s1, 0x1B);             /* EFGH */
    s0 = _mm_alignr_epi8(t, s1, 8);               /* ABEF */
    s1 = _mm_blend_epi16(s1, t, 0xF0);            /* CDGH */
    while (nb--) {
        __m128i save0 = s0, save1 = s1, m[4], k;
        int i;
        for (i = 0; i < 16; i++) {
            if (i < 4) m[i] = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(p + 16 * i)), bswap);
            else {   /* W[4i..4i+3] from the previous 16 words */
                __m128i x = _mm_sha256msg1_epu32(m[i & 3], m[(i + 1) & 3]);
                x = _mm_add_epi32(x, _mm_alignr_epi8(m[(i + 3) & 3], m[(i + 2) & 3], 4));
                m[i & 3] = _mm_sha256msg2_epu32(x, m[(i + 3) & 3]);
            }
            k = _mm_add_epi32(m[i & 3], _mm_loadu_si128((const __m128i *)&K256[4 * i]));
            s1 = _mm_sha256rnds2_epu32(s1, s0, k);    /* rounds 4i, 4i+1: new ABEF in s1 */
            k = _mm_shuffle_epi32(k, 0x0E);
            s0 = _mm_sha256rnds2_epu32(s0, s1, k);    /* rounds 4i+2, 4i+3: new ABEF in s0 */
        }
        s0 = _mm_add_epi32(s0, save0);
        s1 = _mm_add_epi32(s1, save1);
        p += 64;
    }
    t = _mm_shuffle_epi32(s0, 0x1B);              /* FEBA */
    s1 = _mm_shuffle_epi32(s1, 0xB1);             /* DCHG */
    s0 = _mm_blend_epi16(t, s1, 0xF0);            /* DCBA */
    s1 = _mm_alignr_epi8(s1, t, 8);               /* HGFE */
    _mm_storeu_si128((__m128i *)&h[0], s0);
    _mm_storeu_si128((__m128i *)&h[4], s1);
}
#endif

int crypto_have_shani(void)
{
#ifdef SRS_X86
    unsigned a, b, c, d;
    __builtin_cpu_init();
    if (!__builtin_cpu_supports("sse4.1") || !__builtin_cpu_supports("ssse3")) return 0;
    if (!__get_cpuid_count(7, 0, &a, &b, &c, &d)) return 0;
    return (b >> 29) & 1;   /* CPUID leaf 7 EBX bit 29 = SHA extensions */
#else
    return 0;
#endif
}

int crypto_have_aesni(void)
{
#ifdef SRS_X86
    __builtin_cpu_init();
    return __builtin_cpu_supports("aes") && __builtin_cpu_supports("sse4.1");
#else
    return 0;
#endif
}

typedef void (*sha_fn)(uint32_t h[8], const unsigned char *p, size_t nb);

static void sha256_with(sha_fn fn, const void *data, size_t len, unsigned char out[32])
{
    static const uint32_t iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    uint32_t h[8];
    unsigned char tail[128];
    size_t full = len / 64, rest = len % 64, tl;
    uint64_t bits = (uint64_t)len * 8;
    int i;
    memcpy(h, iv, sizeof h);
    if (full) fn(h, data, full);
    memset(tail, 0, sizeof tail);
    memcpy(tail, (const unsigned char *)data + full * 64, rest);
    tail[rest] = 0x80;
    tl = rest + 9 > 64 ? 128 : 64;
    for (i = 0; i < 8; i++) tail[tl - 1 - i] = (unsigned char)(bits >> (8 * i));
    fn(h, tail, tl / 64);
    for (i = 0; i < 8; i++) { out[4 * i] = h[i] >> 24; out[4 * i + 1] = h[i] >> 16; out[4 * i + 2] = h[i] >> 8; out[4 * i + 3] = (unsigned char)h[i]; }
}

void sha256_hex(const void *data, size_t len, char hex[65])
{
    unsigned char d[32];
    int i;
    sha256_with(crypto_have_shani() ?
#ifdef SRS_X86
                sha256_blocks_ni
#else
                sha256_blocks_c
#endif
                : sha256_blocks_c, data, len, d);
    for (i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", d[i]);
}

/* ======================================================================
 * AES-128 (FIPS 197): T-table implementation in plain C, AES-NI on x86
 * ====================================================================== */
static uint8_t sbox[256];
static uint32_t te[4][256];
static pthread_once_t aes_once = PTHREAD_ONCE_INIT;

static uint8_t xt(uint8_t x) { return (uint8_t)((x << 1) ^ (x & 0x80 ? 0x1b : 0)); }
static uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void aes_tables(void)
{
    uint8_t p = 1, q = 1;
    int i;
    /* S-box: multiplicative inverse in GF(2^8) followed by the affine transform (FIPS 197 5.1.1).
       p runs over the powers of 3, q over the powers of 3^-1, so q = p^-1 at every step. */
    do {
        uint8_t x;
        p = p ^ xt(p);                                    /* p *= 3 */
        q ^= q << 1; q ^= q << 2; q ^= q << 4;            /* q /= 3 */
        if (q & 0x80) q ^= 0x09;
        x = q ^ (uint8_t)(q << 1 | q >> 7) ^ (uint8_t)(q << 2 | q >> 6) ^ (uint8_t)(q << 3 | q >> 5) ^ (uint8_t)(q << 4 | q >> 4);
        sbox[p] = x ^ 0x63;
    } while (p != 1);
    sbox[0] = 0x63;
    for (i = 0; i < 256; i++) {
        uint8_t s = sbox[i], s2 = xt(s), s3 = s2 ^ s;
        /* column word, row 0 in the low byte: MixColumns column (2s, s, s, 3s) */
        te[0][i] = (uint32_t)s2 | (uint32_t)s << 8 | (uint32_t)s << 16 | (uint32_t)s3 << 24;
        te[1][i] = rotl(te[0][i], 8);
        te[2][i] = rotl(te[0][i], 16);
        te[3][i] = rotl(te[0][i], 24);
    }
}

typedef struct { uint32_t rk[44]; } aes_key;

static void aes_expand(aes_key *k, const uint8_t key[16])
{
    static const uint8_t rcon[10] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36 };
    int i;
    pthread_once(&aes_once, aes_tables);
    for (i = 0; i < 4; i++) k->rk[i] = (uint32_t)key[4 * i] | (uint32_t)key[4 * i + 1] << 8 | (uint32_t)key[4 * i + 2] << 16 | (uint32_t)key[4 * i + 3] << 24;
    for (i = 4; i < 44; i++) {
        uint32_t t = k->rk[i - 1];
        if (i % 4 == 0) {
            t = (t >> 8) | (t << 24);   /* RotWord: bytes a1 a2 a3 a0 */
            t = (uint32_t)sbox[t & 0xff] | (uint32_t)sbox[(t >> 8) & 0xff] << 8 | (uint32_t)sbox[(t >> 16) & 0xff] << 16 | (uint32_t)sbox[t >> 24] << 24;
            t ^= rcon[i / 4 - 1];
        }
        k->rk[i] = k->rk[i - 4] ^ t;
    }
}

static void aes_block_c(const aes_key *k, const uint8_t in[16], uint8_t out[16])
{
    uint32_t s[4], t[4];
    int r, c;
    for (c = 0; c < 4; c++)
        s[c] = ((uint32_t)in[4 * c] | (uint32_t)in[4 * c + 1] << 8 | (uint32_t)in[4 * c + 2] << 16 | (uint32_t)in[4 * c + 3] << 24) ^ k->rk[c];
    for (r = 1; r < 10; r++) {
        for (c = 0; c < 4; c++)
            t[c] = te[0][s[c] & 0xff] ^ te[1][(s[(c + 1) & 3] >> 8) & 0xff] ^ te[2][(s[(c + 2) & 3] >> 16) & 0xff] ^
                   te[3][s[(c + 3) & 3] >> 24] ^ k->rk[4 * r + c];
        memcpy(s, t, sizeof s);
    }
    for (c = 0; c < 4; c++) {
        uint32_t v = ((uint32_t)sbox[s[c] & 0xff] | (uint32_t)sbox[(s[(c + 1) & 3] >> 8) & 0xff] << 8 |
                      (uint32_t)sbox[(s[(c + 2) & 3] >> 16) & 0xff] << 16 | (uint32_t)sbox[s[(c + 3) & 3] >> 24] << 24) ^ k->rk[40 + c];
        out[4 * c] = (uint8_t)v; out[4 * c + 1] = (uint8_t)(v >> 8); out[4 * c + 2] = (uint8_t)(v >> 16); out[4 * c + 3] = (uint8_t)(v >> 24);
    }
}

/* CTR: counter block = iv with the last 8 bytes as a big-endian counter */
static void ctr_inc(uint8_t c[16]) { int i; for (i = 15; i >= 8; i--) if (++c[i]) break; }

static void aes_ctr_c(const aes_key *k, uint8_t ctr[16], const uint8_t *in, uint8_t *out, size_t n)
{
    uint8_t ks[16];
    size_t i, j;
    for (i = 0; i < n; i += 16) {
        aes_block_c(k, ctr, ks);
        ctr_inc(ctr);
        for (j = 0; j < 16 && i + j < n; j++) out[i + j] = in[i + j] ^ ks[j];
    }
}

#ifdef SRS_X86
/* counter blocks are built in registers (prefix | big-endian 64-bit counter): writing the counter
   byte by byte and loading it as 16 bytes would stall on store forwarding for every block */
__attribute__((target("aes,sse4.1")))
static void aes_ctr_ni(const aes_key *k, uint8_t ctr[16], const uint8_t *in, uint8_t *out, size_t n)
{
    __m128i rk[11], x0, x1, x2, x3, x4, x5, x6, x7;
    uint64_t hi, lo = 0;
    size_t i;
    int r;
    memcpy(&hi, ctr, 8);
    for (r = 8; r < 16; r++) lo = lo << 8 | ctr[r];
    for (r = 0; r < 11; r++) rk[r] = _mm_loadu_si128((const __m128i *)&k->rk[4 * r]);   /* FIPS byte order */
#define CB(c) _mm_xor_si128(_mm_set_epi64x((long long)__builtin_bswap64(c), (long long)hi), rk[0])
    for (i = 0; i + 128 <= n; i += 128, lo += 8) {   /* eight blocks at once: keeps the AES units busy */
        x0 = CB(lo); x1 = CB(lo + 1); x2 = CB(lo + 2); x3 = CB(lo + 3); x4 = CB(lo + 4); x5 = CB(lo + 5); x6 = CB(lo + 6); x7 = CB(lo + 7);
        for (r = 1; r < 10; r++) {
            __m128i kk = rk[r];
            x0 = _mm_aesenc_si128(x0, kk); x1 = _mm_aesenc_si128(x1, kk); x2 = _mm_aesenc_si128(x2, kk); x3 = _mm_aesenc_si128(x3, kk);
            x4 = _mm_aesenc_si128(x4, kk); x5 = _mm_aesenc_si128(x5, kk); x6 = _mm_aesenc_si128(x6, kk); x7 = _mm_aesenc_si128(x7, kk);
        }
#define FIN(x, j) _mm_storeu_si128((__m128i *)(out + i + 16 * (j)), \
                  _mm_xor_si128(_mm_aesenclast_si128(x, rk[10]), _mm_loadu_si128((const __m128i *)(in + i + 16 * (j)))))
        FIN(x0, 0); FIN(x1, 1); FIN(x2, 2); FIN(x3, 3); FIN(x4, 4); FIN(x5, 5); FIN(x6, 6); FIN(x7, 7);
#undef FIN
    }
    for (; i < n; i += 16, lo++) {
        uint8_t ks[16];
        size_t b;
        __m128i v = CB(lo);
        for (r = 1; r < 10; r++) v = _mm_aesenc_si128(v, rk[r]);
        _mm_storeu_si128((__m128i *)ks, _mm_aesenclast_si128(v, rk[10]));
        for (b = 0; b < 16 && i + b < n; b++) out[i + b] = in[i + b] ^ ks[b];
    }
#undef CB
    for (r = 15; r >= 8; r--) { ctr[r] = (uint8_t)lo; lo >>= 8; }
}
#endif

/* ======================================================================
 * self-test: FIPS test vectors, and plain C vs hardware on the same data
 * ====================================================================== */
static int unhex(const char *h, uint8_t *o, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) { unsigned v; if (sscanf(h + 2 * i, "%2x", &v) != 1) return 0; o[i] = (uint8_t)v; }
    return 1;
}

int crypto_selftest(char *msg, size_t mn)
{
    static const struct { const char *in, *hex; } sv[] = {
        { "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" },
        { "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
        { "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" } };
    uint8_t key[16], pt[16], ct[16], want[16], *a, *b, *c;
    unsigned char d1[32];
    char hex[65];
    aes_key k;
    size_t i;
    int ok = 1;
    for (i = 0; i < sizeof sv / sizeof *sv; i++) {
        int j;
        sha256_with(sha256_blocks_c, sv[i].in, strlen(sv[i].in), d1);
        for (j = 0; j < 32; j++) snprintf(hex + 2 * j, 3, "%02x", d1[j]);
        if (strcmp(hex, sv[i].hex)) { snprintf(msg, mn, "SHA-256 (plain C) fails FIPS 180-4 test vector %zu", i + 1); return 0; }
    }
    /* FIPS 197 appendix C.1 */
    unhex("000102030405060708090a0b0c0d0e0f", key, 16);
    unhex("00112233445566778899aabbccddeeff", pt, 16);
    unhex("69c4e0d86a7b0430d8cdb78070b4c55a", want, 16);
    aes_expand(&k, key);
    aes_block_c(&k, pt, ct);
    if (memcmp(ct, want, 16)) { snprintf(msg, mn, "AES-128 (plain C) fails the FIPS 197 C.1 test vector"); return 0; }
    a = malloc(4096); b = malloc(4096); c = malloc(4096);
    if (!a || !b || !c) { free(a); free(b); free(c); snprintf(msg, mn, "out of memory"); return 0; }
    for (i = 0; i < 4096; i++) a[i] = (uint8_t)(i * 131 + 7);
    (void)b; (void)c;
#ifdef SRS_X86
    if (crypto_have_shani()) {
        unsigned char d2[32];
        sha256_with(sha256_blocks_c, a, 4000, d1);
        sha256_with(sha256_blocks_ni, a, 4000, d2);
        if (memcmp(d1, d2, 32)) ok = 0, snprintf(msg, mn, "SHA-256 with SHA-NI differs from plain C");
    }
    if (ok && crypto_have_aesni()) {
        uint8_t ctr1[16] = { 0 }, ctr2[16] = { 0 };
        aes_ctr_c(&k, ctr1, a, b, 4000);
        aes_ctr_ni(&k, ctr2, a, c, 4000);
        if (memcmp(b, c, 4000)) ok = 0, snprintf(msg, mn, "AES-128 with AES-NI differs from plain C");
    }
#endif
    free(a); free(b); free(c);
    if (ok) snprintf(msg, mn, "FIPS 180-4 / FIPS 197 test vectors pass%s%s", crypto_have_aesni() ? "; AES-NI matches plain C" : "",
                     crypto_have_shani() ? "; SHA-NI matches plain C" : "");
    return ok;
}

/* ======================================================================
 * the benchmark
 * ====================================================================== */
enum { CT_AES_HW, CT_AES_C, CT_SHA_HW, CT_SHA_C, CT_N };
static const char *const ct_name[CT_N] = { "AES-128-CTR, AES-NI", "AES-128-CTR, plain C", "SHA-256, SHA-NI", "SHA-256, plain C" };

#define CBUF (64 * 1024)
static volatile uint32_t g_sink;   /* results are written here so the compiler cannot drop the work */
typedef struct { int cpu, test; double secs, mbs; pthread_barrier_t *bar; pthread_t t; } cjob;

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void *cjob_run(void *arg)
{
    cjob *j = arg;
    uint8_t *in = malloc(CBUF), *out = malloc(CBUF), key[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 }, ctr[16] = { 0 };
    uint32_t h[8] = { 0 };
    aes_key k;
    double t0, el;
    long n = 0;
    size_t i;
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(j->cpu, &s);
    pthread_setaffinity_np(pthread_self(), sizeof s, &s);
    if (!in || !out) { free(in); free(out); if (j->bar) pthread_barrier_wait(j->bar); return NULL; }
    for (i = 0; i < CBUF; i++) in[i] = (uint8_t)(i * 7 + j->cpu);
    aes_expand(&k, key);
    if (j->bar) pthread_barrier_wait(j->bar);
    t0 = now_s();
    do {
        switch (j->test) {
#ifdef SRS_X86
        case CT_AES_HW: aes_ctr_ni(&k, ctr, in, out, CBUF); break;
        case CT_SHA_HW: sha256_blocks_ni(h, in, CBUF / 64); break;
#endif
        case CT_AES_C: aes_ctr_c(&k, ctr, in, out, CBUF); break;
        case CT_SHA_C: sha256_blocks_c(h, in, CBUF / 64); break;
        default: break;
        }
        g_sink ^= h[0] ^ out[n & (CBUF - 1)];
        n++;
        el = now_s() - t0;
    } while (el < j->secs);
    j->mbs = (double)n * CBUF / el / 1e6;
    free(in);
    free(out);
    return NULL;
}

static double crypto_measure(int test, int nthreads, double secs)
{
    cjob js[BURN_MAXCPU];
    pthread_barrier_t bar;
    double sum = 0;
    int i, n = 0;
    cpu_set_t saved;
    sched_getaffinity(0, sizeof saved, &saved);
    for (i = 0; i < BURN_MAXCPU && n < nthreads; i++) if (CPU_ISSET(i, &saved)) { js[n].cpu = i; n++; }
    pthread_barrier_init(&bar, NULL, (unsigned)n);
    for (i = 0; i < n; i++) { js[i].test = test; js[i].secs = secs; js[i].mbs = 0; js[i].bar = &bar; }
    for (i = 0; i < n; i++) pthread_create(&js[i].t, NULL, cjob_run, &js[i]);
    for (i = 0; i < n; i++) { pthread_join(js[i].t, NULL); sum += js[i].mbs; }
    pthread_barrier_destroy(&bar);
    return sum;
}

typedef struct { double v[CT_N][2]; int threads, hw_aes, hw_sha, ok; double secs; char self[200], when[32]; } cctx;

static void crypto_render(srs_out *o, void *c)
{
    static const int tabs[] = { 220, 380 };
    cctx *x = c;
    int t;
    char h2[32];
    so_siv_header(o, "Crypto benchmark (AES-128 encryption and SHA-256 hashing, MB/s)");
    so_tabs(o, tabs, 2);
    snprintf(h2, sizeof h2, "%d threads", x->threads);
    so_row(o, 3, (const char *const[]){ "Test", "1 thread", h2 }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    for (t = 0; t < CT_N; t++) {
        char a[32], b[32];
        int hw = t == CT_AES_HW || t == CT_SHA_HW, have = t == CT_AES_HW ? x->hw_aes : t == CT_SHA_HW ? x->hw_sha : 1;
        if (!have) {
            so_row(o, 3, (const char *const[]){ ct_name[t], "not available on this CPU", "" }, (const int[]){ C_LABEL, C_GRAY, C_GRAY });
            continue;
        }
        snprintf(a, sizeof a, x->v[t][0] > 0 ? "%.0f MB/s" : "-", x->v[t][0]);
        snprintf(b, sizeof b, x->v[t][1] > 0 ? "%.0f MB/s" : "-", x->v[t][1]);
        so_row(o, 3, (const char *const[]){ ct_name[t], a, b }, (const int[]){ C_LABEL, hw ? C_GREEN : C_BLACK, hw ? C_GREEN : C_BLACK });
    }
    so_par(o);
    so_tabs(o, tabs, 1);
    so_kv(o, "Check", x->ok ? C_GREEN : C_RED, x->self);
    so_kvf(o, "Run", C_GRAY, "%s, %.1f s per measurement, 64 KB buffer per thread", x->when, x->secs);
    so_kv(o, "Build", C_GRAY, bench_build_tag());
    so_note(o, "1 MB = 1,000,000 bytes. The plain C rows show what the same work costs without the CPU's crypto instructions.");
    so_board_footer(o);
}

int bench_crypto(int threads, double secs, srs_out *o)
{
    cctx c;
    int t, k;
    time_t now = time(NULL);
    char params[64], res[200];
    memset(&c, 0, sizeof c);
    if (threads < 1 || threads > burn_ncpu()) threads = burn_ncpu();
    if (secs < 0.1) secs = 0.1;
    c.threads = threads;
    c.secs = secs;
    c.hw_aes = crypto_have_aesni();
    c.hw_sha = crypto_have_shani();
    pthread_once(&aes_once, aes_tables);
    c.ok = crypto_selftest(c.self, sizeof c.self);
    strftime(c.when, sizeof c.when, "%Y-%m-%d %H:%M:%S", localtime(&now));
    if (c.ok)
        for (t = 0; t < CT_N; t++) {
            if ((t == CT_AES_HW && !c.hw_aes) || (t == CT_SHA_HW && !c.hw_sha)) continue;
            for (k = 0; k < 2; k++) c.v[t][k] = crypto_measure(t, k ? threads : 1, secs);
        }
    bench_save_render("crypto", crypto_render, &c, o);
    snprintf(params, sizeof params, "threads=%d seconds=%.1f", threads, secs);
    snprintf(res, sizeof res, "aes-hw %.0f/%.0f aes-c %.0f/%.0f sha-hw %.0f/%.0f sha-c %.0f/%.0f MB/s%s",
             c.v[0][0], c.v[0][1], c.v[1][0], c.v[1][1], c.v[2][0], c.v[2][1], c.v[3][0], c.v[3][1], c.ok ? "" : " SELFTEST-FAILED");
    bench_history_add("crypto", params, res);
    return c.ok ? 0 : 1;
}
