/* bench_work.c - "Everyday work" CPU benchmark: compression, sorting, checksums, text processing
 * and a bytecode interpreter, 1 thread and N threads, every result checked (see bench_work.h).
 * Written for SRS. The compression libraries are the system's own, opened with dlopen at run
 * time; everything else is SRS's own code. Algorithms: xxHash64 follows Yann Collet's published
 * xxHash specification (BSD-2-Clause reference), CRC32 / CRC32C the IEEE 802.3 and Castagnoli
 * polynomials (slicing-by-8 as described by Intel's Kounavis and Berry), FNV-1a by Fowler, Noll
 * and Vo, Boyer-Moore-Horspool (Horspool 1980), multikey quicksort (Bentley and Sedgewick 1997). */
#define _GNU_SOURCE
#include "bench.h"
#include "bench_work.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *const wk_group_name[WK_NGROUP] = { "Compression", "Sorting", "Checksums and hashes", "Text processing", "Bytecode interpreter" };
const char *const wk_group_id[WK_NGROUP] = { "compress", "sort", "hash", "text", "interp" };
const char *const wk_lib_id[WK_NLIB] = { "zlib", "zstd", "lz4", "xz" };
const char *const wk_codec_name[WK_NCODEC] = { "zlib -1", "zlib -6", "zlib -9", "zstd -1", "zstd -3", "zstd -19", "lz4", "xz -1", "xz -6" };
const char *const wk_test_name[WT_N] = {
    "64-bit integers, C library qsort", "64-bit integers, radix sort", "Strings, C library qsort", "Strings, multikey quicksort",
    "CRC32, byte table", "CRC32, slicing-by-8", "CRC32C, table", "CRC32C, CPU instruction", "xxHash64", "FNV-1a 64-bit",
    "Find text, C library memmem", "Find text, Boyer-Moore-Horspool", "JSON check (tokenise + structure)", "UTF-8 check",
    "Interpreter, switch dispatch", "Interpreter, threaded dispatch" };
const char *const wk_unit[WK_NGROUP] = { "MB/s", "Mkeys/s", "GB/s", "GB/s", "Mops/s" };
#ifdef __linux__
static const char *const codec_id[WK_NCODEC] = { "zlib-1", "zlib-6", "zlib-9", "zstd-1", "zstd-3", "zstd-19", "lz4", "xz-1", "xz-6" };
static const char *const test_id[WT_N] = { "qsort-u64", "radix-u64", "qsort-str", "mkqs-str", "crc32", "crc32-s8", "crc32c", "crc32c-hw",
                                           "xxh64", "fnv1a", "memmem", "bmh", "json", "utf8", "vm-switch", "vm-threaded" };
#endif

int wk_test_group(int t)
{
    if (t <= WT_SORT_MKQS_STR) return WK_SORT;
    if (t <= WT_FNV1A) return WK_HASH;
    if (t <= WT_UTF8) return WK_TEXT;
    return WK_INTERP;
}

int wk_codec_lib(int c) { return c <= WC_ZLIB9 ? WK_LIB_ZLIB : c <= WC_ZSTD19 ? WK_LIB_ZSTD : c == WC_LZ4 ? WK_LIB_LZ4 : WK_LIB_XZ; }

void wk_cfg_default(wk_cfg *c)
{
    memset(c, 0, sizeof *c);
    c->seconds = 0.5;
    c->part_kib = 1024;
}

#ifdef __linux__
#include "burner.h"

#include <ctype.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>

#if defined(__x86_64__)
#include <immintrin.h>
#endif

static const char *volatile g_stage = "";
static char g_stage_buf[128];
const char *wk_stage(void) { return g_stage; }
int wk_available(void) { return 1; }

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static uint64_t sm64(uint64_t *s)   /* splitmix64 */
{
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static uint64_t rd64(const uint8_t *p)
{
    uint64_t v;
    memcpy(&v, p, 8);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap64(v);
#endif
    return v;
}

static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap32(v);
#endif
    return v;
}

/* ======================================================================
 * checksums and hashes
 * ====================================================================== */
static uint32_t crc_t[8][256], crcc_t[256];
static pthread_once_t crc_once = PTHREAD_ONCE_INIT;

static void crc_tables(void)
{
    uint32_t i, k, c, d;
    for (i = 0; i < 256; i++) {
        c = d = i;
        for (k = 0; k < 8; k++) {
            c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
            d = d & 1 ? (d >> 1) ^ 0x82F63B78u : d >> 1;
        }
        crc_t[0][i] = c;
        crcc_t[i] = d;
    }
    for (i = 0; i < 256; i++)
        for (k = 1; k < 8; k++) crc_t[k][i] = (crc_t[k - 1][i] >> 8) ^ crc_t[0][crc_t[k - 1][i] & 0xff];
}

static uint32_t crc32_byte(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = (c >> 8) ^ crc_t[0][(c ^ *p++) & 0xff];
    return ~c;
}

static uint32_t crc32_slice8(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (; n >= 8; n -= 8, p += 8) {
        uint32_t a = rd32(p) ^ c, b = rd32(p + 4);
        c = crc_t[7][a & 0xff] ^ crc_t[6][(a >> 8) & 0xff] ^ crc_t[5][(a >> 16) & 0xff] ^ crc_t[4][a >> 24] ^
            crc_t[3][b & 0xff] ^ crc_t[2][(b >> 8) & 0xff] ^ crc_t[1][(b >> 16) & 0xff] ^ crc_t[0][b >> 24];
    }
    while (n--) c = (c >> 8) ^ crc_t[0][(c ^ *p++) & 0xff];
    return ~c;
}

static uint32_t crc32c_table(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = (c >> 8) ^ crcc_t[(c ^ *p++) & 0xff];
    return ~c;
}

#if defined(__x86_64__)
__attribute__((target("sse4.2")))
static uint32_t crc32c_hw(const uint8_t *p, size_t n)
{
    uint64_t c = 0xFFFFFFFFu, v;
    for (; n >= 8; n -= 8, p += 8) { memcpy(&v, p, 8); c = _mm_crc32_u64(c, v); }
    while (n--) c = _mm_crc32_u8((uint32_t)c, *p++);
    return ~(uint32_t)c;
}
static int have_crc32c_hw(void) { __builtin_cpu_init(); return __builtin_cpu_supports("sse4.2"); }
#else
static uint32_t crc32c_hw(const uint8_t *p, size_t n) { return crc32c_table(p, n); }
static int have_crc32c_hw(void) { return 0; }
#endif

#define XP1 0x9E3779B185EBCA87ull
#define XP2 0xC2B2AE3D27D4EB4Full
#define XP3 0x165667B19E3779F9ull
#define XP4 0x85EBCA77C2B2AE63ull
#define XP5 0x27D4EB2F165667C5ull
static uint64_t rotl64(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
static uint64_t xround(uint64_t acc, uint64_t in) { return rotl64(acc + in * XP2, 31) * XP1; }
static uint64_t xmerge(uint64_t h, uint64_t v) { return (h ^ xround(0, v)) * XP1 + XP4; }

static uint64_t xxh64(const uint8_t *p, size_t n, uint64_t seed)
{
    const uint8_t *e = p + n;
    uint64_t h;
    if (n >= 32) {
        uint64_t v1 = seed + XP1 + XP2, v2 = seed + XP2, v3 = seed, v4 = seed - XP1;
        do {
            v1 = xround(v1, rd64(p)); v2 = xround(v2, rd64(p + 8)); v3 = xround(v3, rd64(p + 16)); v4 = xround(v4, rd64(p + 24));
            p += 32;
        } while (p + 32 <= e);
        h = rotl64(v1, 1) + rotl64(v2, 7) + rotl64(v3, 12) + rotl64(v4, 18);
        h = xmerge(h, v1); h = xmerge(h, v2); h = xmerge(h, v3); h = xmerge(h, v4);
    } else h = seed + XP5;
    h += (uint64_t)n;
    for (; p + 8 <= e; p += 8) h = rotl64(h ^ xround(0, rd64(p)), 27) * XP1 + XP4;
    if (p + 4 <= e) { h = rotl64(h ^ (uint64_t)rd32(p) * XP1, 23) * XP2 + XP3; p += 4; }
    while (p < e) h = rotl64(h ^ *p++ * XP5, 11) * XP1;
    h ^= h >> 33; h *= XP2; h ^= h >> 29; h *= XP3; h ^= h >> 32;
    return h;
}

static uint64_t fnv1a(const uint8_t *p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    while (n--) { h ^= *p++; h *= 0x100000001b3ull; }
    return h;
}

/* ======================================================================
 * sorting
 * ====================================================================== */
static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static void radix_u64(uint64_t *a, uint64_t *t, size_t n)
{
    size_t cnt[8][256];
    uint64_t *src = a, *dst = t, *sw;
    size_t i, k, sum;
    int b;
    if (n < 2) return;
    memset(cnt, 0, sizeof cnt);
    for (i = 0; i < n; i++) for (b = 0; b < 8; b++) cnt[b][(a[i] >> (8 * b)) & 0xff]++;
    for (b = 0; b < 8; b++) {
        size_t *c = cnt[b];
        if (c[(src[0] >> (8 * b)) & 0xff] == n) continue;   /* every key has the same byte here */
        for (k = 0, sum = 0; k < 256; k++) { size_t v = c[k]; c[k] = sum; sum += v; }
        for (i = 0; i < n; i++) dst[c[(src[i] >> (8 * b)) & 0xff]++] = src[i];
        sw = src; src = dst; dst = sw;
    }
    if (src != a) memcpy(a, src, n * sizeof *a);
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int ch_at(const char *s, size_t d) { return (unsigned char)s[d]; }

/* Bentley-Sedgewick multikey quicksort: 3-way partition on the character at depth d */
static void mkqs(char **a, size_t n, size_t d)
{
    while (n > 1) {
        size_t lt = 0, i = 0, gt = n;
        int v, x, y, z;
        if (n < 12) {
            for (i = 1; i < n; i++) {
                char *k = a[i];
                size_t j = i;
                while (j > 0 && strcmp(a[j - 1] + d, k + d) > 0) { a[j] = a[j - 1]; j--; }
                a[j] = k;
            }
            return;
        }
        x = ch_at(a[0], d); y = ch_at(a[n / 2], d); z = ch_at(a[n - 1], d);
        v = x < y ? (y < z ? y : x < z ? z : x) : (x < z ? x : y < z ? z : y);   /* median of three */
        while (i < gt) {
            int c = ch_at(a[i], d);
            char *s;
            if (c < v) { s = a[lt]; a[lt++] = a[i]; a[i++] = s; }
            else if (c > v) { s = a[--gt]; a[gt] = a[i]; a[i] = s; }
            else i++;
        }
        mkqs(a, lt, d);
        mkqs(a + gt, n - gt, d);
        if (!v) return;   /* the middle part ends here: equal strings */
        a += lt; n = gt - lt; d++;
    }
}

static uint64_t mix64(uint64_t x) { uint64_t s = x; return sm64(&s); }

/* ======================================================================
 * text: substring search, UTF-8 check, JSON check
 * ====================================================================== */
static long count_memmem(const uint8_t *h, size_t n, const char *nd)
{
    size_t m = strlen(nd);
    const uint8_t *p = h, *e = h + n, *q;
    long c = 0;
    while (p < e && (q = memmem(p, (size_t)(e - p), nd, m))) { c++; p = q + 1; }
    return c;
}

static long count_bmh(const uint8_t *h, size_t n, const char *ndc)
{
    const uint8_t *nd = (const uint8_t *)ndc;
    size_t m = strlen(ndc), sh[256], i, pos = 0;
    uint8_t last;
    long c = 0;
    if (!m || m > n) return 0;
    for (i = 0; i < 256; i++) sh[i] = m;
    for (i = 0; i + 1 < m; i++) sh[nd[i]] = m - 1 - i;
    last = nd[m - 1];
    while (pos <= n - m) {
        uint8_t b = h[pos + m - 1];
        if (b == last && !memcmp(h + pos, nd, m - 1)) c++;
        pos += sh[b];
    }
    return c;
}

static long count_naive(const uint8_t *h, size_t n, const char *nd)
{
    size_t m = strlen(nd), i;
    long c = 0;
    for (i = 0; i + m <= n; i++) if (h[i] == (uint8_t)nd[0] && !memcmp(h + i, nd, m)) c++;
    return c;
}

/* code points, or -1 when not valid UTF-8 (overlong forms, surrogates and > U+10FFFF rejected) */
static long utf8_count(const uint8_t *s, size_t n)
{
    size_t i = 0, len, k;
    long cp = 0;
    while (i < n) {
        uint8_t c, lo = 0x80, hi = 0xBF;
        if (i + 8 <= n) {
            uint64_t w;
            memcpy(&w, s + i, 8);
            if (!(w & 0x8080808080808080ull)) { i += 8; cp += 8; continue; }
        }
        c = s[i];
        if (c < 0x80) { i++; cp++; continue; }
        if (c >= 0xC2 && c <= 0xDF) len = 2;
        else if (c >= 0xE0 && c <= 0xEF) { len = 3; if (c == 0xE0) lo = 0xA0; if (c == 0xED) hi = 0x9F; }
        else if (c >= 0xF0 && c <= 0xF4) { len = 4; if (c == 0xF0) lo = 0x90; if (c == 0xF4) hi = 0x8F; }
        else return -1;
        if (i + len > n || s[i + 1] < lo || s[i + 1] > hi) return -1;
        for (k = 2; k < len; k++) if ((s[i + k] & 0xC0) != 0x80) return -1;
        i += len;
        cp++;
    }
    return cp;
}

typedef struct { long obj, arr, key, str, num, lit; } jcount;
typedef struct { const uint8_t *p, *e; jcount n; int depth; } jparse;

static void j_ws(jparse *j) { while (j->p < j->e && (*j->p == ' ' || *j->p == '\n' || *j->p == '\r' || *j->p == '\t')) j->p++; }
static int j_at(jparse *j, int c) { return j->p < j->e && *j->p == c; }

static int j_str(jparse *j)
{
    j->p++;
    while (j->p < j->e) {
        uint8_t c = *j->p++;
        if (c == '"') return 1;
        if (c < 0x20) return 0;
        if (c != '\\') continue;
        if (j->p >= j->e) return 0;
        c = *j->p++;
        if (c == 'u') {
            int k;
            for (k = 0; k < 4; k++, j->p++) if (j->p >= j->e || !isxdigit(*j->p)) return 0;
        } else if (!c || !strchr("\"\\/bfnrt", c)) return 0;
    }
    return 0;
}

static int j_digits(jparse *j)
{
    const uint8_t *s = j->p;
    while (j->p < j->e && *j->p >= '0' && *j->p <= '9') j->p++;
    return j->p > s;
}

static int j_num(jparse *j)
{
    if (j_at(j, '-')) j->p++;
    if (j_at(j, '0')) j->p++;
    else if (!j_digits(j)) return 0;
    if (j_at(j, '.')) { j->p++; if (!j_digits(j)) return 0; }
    if (j_at(j, 'e') || j_at(j, 'E')) {
        j->p++;
        if (j_at(j, '+') || j_at(j, '-')) j->p++;
        if (!j_digits(j)) return 0;
    }
    return 1;
}

static int j_lit(jparse *j, const char *w)
{
    size_t l = strlen(w);
    if ((size_t)(j->e - j->p) < l || memcmp(j->p, w, l)) return 0;
    j->p += l;
    j->n.lit++;
    return 1;
}

static int j_val(jparse *j)
{
    j_ws(j);
    if (j->p >= j->e) return 0;
    switch (*j->p) {
    case '{': case '[': {
        int obj = *j->p == '{', close = obj ? '}' : ']';
        if (++j->depth > 64) return 0;
        j->p++;
        if (obj) j->n.obj++; else j->n.arr++;
        j_ws(j);
        if (j_at(j, close)) { j->p++; j->depth--; return 1; }
        for (;;) {
            if (obj) {
                j_ws(j);
                if (!j_at(j, '"') || !j_str(j)) return 0;
                j->n.key++;
                j_ws(j);
                if (!j_at(j, ':')) return 0;
                j->p++;
            }
            if (!j_val(j)) return 0;
            j_ws(j);
            if (j_at(j, ',')) { j->p++; continue; }
            if (j_at(j, close)) { j->p++; j->depth--; return 1; }
            return 0;
        }
    }
    case '"': if (!j_str(j)) return 0; j->n.str++; return 1;
    case 't': return j_lit(j, "true");
    case 'f': return j_lit(j, "false");
    case 'n': return j_lit(j, "null");
    default:
        if ((*j->p == '-' || (*j->p >= '0' && *j->p <= '9')) && j_num(j)) { j->n.num++; return 1; }
        return 0;
    }
}

static int json_check(const uint8_t *s, size_t n, jcount *c)
{
    jparse j;
    int ok;
    memset(&j, 0, sizeof j);
    j.p = s; j.e = s + n;
    ok = j_val(&j);
    j_ws(&j);
    if (j.p != j.e) ok = 0;
    if (c) *c = j.n;
    return ok;
}

/* ======================================================================
 * bytecode interpreter: count primes below a limit by trial division and hash them
 * ====================================================================== */
enum { OP_LI, OP_ADD, OP_ADDI, OP_MUL, OP_MOD, OP_XOR, OP_SHR, OP_JLT, OP_JZ, OP_JMP, OP_HALT };
typedef struct { uint8_t op, a, b, c; int32_t imm; } vins;
#define VM_LEN 21
#define VM_LIMIT 20000

static void vm_program(vins *p, int32_t limit)
{
    static const vins tpl[VM_LEN] = {
        { OP_LI, 0, 0, 0, 2 }, { OP_LI, 5, 0, 0, 0 }, { OP_LI, 6, 0, 0, 0 }, { OP_LI, 7, 0, 0, 0 }, { OP_LI, 4, 0, 0, 31 },
        { OP_JLT, 0, 6, 0, 7 }, { OP_HALT, 0, 0, 0, 0 },                                 /* 5: while n < limit */
        { OP_LI, 1, 0, 0, 2 },                                                           /* d = 2 */
        { OP_MUL, 2, 1, 1, 0 }, { OP_JLT, 0, 2, 0, 14 },                                 /* 8: n < d*d: prime */
        { OP_MOD, 3, 0, 1, 0 }, { OP_JZ, 3, 0, 0, 19 }, { OP_ADDI, 1, 1, 0, 1 }, { OP_JMP, 0, 0, 0, 8 },
        { OP_ADDI, 5, 5, 0, 1 }, { OP_MUL, 7, 7, 4, 0 }, { OP_ADD, 7, 7, 0, 0 },         /* 14: count, h = h*31 + n */
        { OP_SHR, 2, 7, 0, 13 }, { OP_XOR, 7, 7, 2, 0 },                                 /* h ^= h >> 13 */
        { OP_ADDI, 0, 0, 0, 1 }, { OP_JMP, 0, 0, 0, 5 } };                               /* 19: n++ */
    memcpy(p, tpl, sizeof tpl);
    p[2].imm = limit;
}

/* plain C version of the same program: the answer the interpreters must give */
static void vm_ref(int32_t limit, uint64_t *count, uint64_t *hash)
{
    uint64_t n, d, c = 0, h = 0;
    for (n = 2; n < (uint64_t)limit; n++) {
        for (d = 2; d * d <= n; d++) if (n % d == 0) break;
        if (d * d > n) { c++; h = h * 31 + n; h ^= h >> 13; }
    }
    *count = c;
    *hash = h;
}

#define VM_OPS(X) \
    X(OP_LI, r[ip->a] = (uint64_t)(int64_t)ip->imm; ip++) \
    X(OP_ADD, r[ip->a] = r[ip->b] + r[ip->c]; ip++) \
    X(OP_ADDI, r[ip->a] = r[ip->b] + (uint64_t)(int64_t)ip->imm; ip++) \
    X(OP_MUL, r[ip->a] = r[ip->b] * r[ip->c]; ip++) \
    X(OP_MOD, r[ip->a] = r[ip->b] % r[ip->c]; ip++) \
    X(OP_XOR, r[ip->a] = r[ip->b] ^ r[ip->c]; ip++) \
    X(OP_SHR, r[ip->a] = r[ip->b] >> (ip->imm & 63); ip++) \
    X(OP_JLT, ip = r[ip->a] < r[ip->b] ? code + ip->imm : ip + 1) \
    X(OP_JZ, ip = !r[ip->a] ? code + ip->imm : ip + 1) \
    X(OP_JMP, ip = code + ip->imm)

/* returns instructions executed when counting (only the reference run counts; inlined, so the
   measured calls carry no counter) */
#if defined(__GNUC__)
__attribute__((always_inline))
#endif
static inline uint64_t vm_switch(const vins *code, uint64_t r[8], int counting)
{
    const vins *ip = code;
    uint64_t ops = 0;
    memset(r, 0, 8 * sizeof *r);
    for (;;) {
        if (counting) ops++;
        switch (ip->op) {
#define CASE(o, body) case o: body; break;
        VM_OPS(CASE)
#undef CASE
        case OP_HALT: return ops;
        default: return 0;
        }
    }
}

#if defined(__GNUC__)
#define HAVE_THREADED 1
static void vm_threaded(const vins *code, uint64_t r[8])
{
    static void *const lab[] = { &&L_OP_LI, &&L_OP_ADD, &&L_OP_ADDI, &&L_OP_MUL, &&L_OP_MOD, &&L_OP_XOR, &&L_OP_SHR,
                                 &&L_OP_JLT, &&L_OP_JZ, &&L_OP_JMP, &&L_OP_HALT };
    const vins *ip = code;
    memset(r, 0, 8 * sizeof *r);
    goto *lab[ip->op];
#define LBL(o, body) L_##o: body; goto *lab[ip->op];
    VM_OPS(LBL)
#undef LBL
L_OP_HALT:
    return;
}
#else
#define HAVE_THREADED 0
static void vm_threaded(const vins *code, uint64_t r[8]) { vm_switch(code, r, 0); }
#endif

/* ======================================================================
 * compression libraries, opened at run time
 * ====================================================================== */
static struct {
    void *h[WK_NLIB];
    int (*z_compress2)(unsigned char *, unsigned long *, const unsigned char *, unsigned long, int);
    int (*z_uncompress)(unsigned char *, unsigned long *, const unsigned char *, unsigned long);
    unsigned long (*z_bound)(unsigned long);
    const char *(*z_version)(void);
    void *(*zs_cctx)(void);
    size_t (*zs_freec)(void *);
    void *(*zs_dctx)(void);
    size_t (*zs_freed)(void *);
    size_t (*zs_compress)(void *, void *, size_t, const void *, size_t, int);
    size_t (*zs_decompress)(void *, void *, size_t, const void *, size_t);
    unsigned (*zs_iserr)(size_t);
    size_t (*zs_bound)(size_t);
    const char *(*zs_version)(void);
    int (*l4_compress)(const char *, char *, int, int);
    int (*l4_decompress)(const char *, char *, int, int);
    int (*l4_bound)(int);
    const char *(*l4_version)(void);
    int (*xz_encode)(uint32_t, int, const void *, const uint8_t *, size_t, uint8_t *, size_t *, size_t);
    int (*xz_decode)(uint64_t *, uint32_t, const void *, const uint8_t *, size_t *, size_t, uint8_t *, size_t *, size_t);
    size_t (*xz_bound)(size_t);
    const char *(*xz_version)(void);
    char ver[WK_NLIB][64], why[WK_NLIB][128];
} L;
static pthread_once_t lib_once = PTHREAD_ONCE_INIT;

static int sym(int lib, void *fp, const char *name)
{
    void *s = dlsym(L.h[lib], name);
    if (!s) { snprintf(L.why[lib], sizeof L.why[lib], "%s has no %s", wk_lib_id[lib], name); return 0; }
    memcpy(fp, &s, sizeof s);
    return 1;
}

/* the handles stay open for the life of the process */
static void lib_load(void)
{
    static const char *const so[WK_NLIB] = { "libz.so.1", "libzstd.so.1", "liblz4.so.1", "liblzma.so.5" };
    const char *off = getenv("SRS_WORK_NOLIB");   /* test setting: act as if these were not installed */
    int i, ok;
    for (i = 0; i < WK_NLIB; i++) {
        const char *v = NULL;
        if (off && strstr(off, wk_lib_id[i])) { snprintf(L.why[i], sizeof L.why[i], "turned off by SRS_WORK_NOLIB"); continue; }
        if (!(L.h[i] = dlopen(so[i], RTLD_NOW | RTLD_LOCAL))) {
            const char *e = dlerror();
            snprintf(L.why[i], sizeof L.why[i], "%.120s", e ? e : so[i]);
            continue;
        }
        switch (i) {
        case WK_LIB_ZLIB:
            ok = sym(i, &L.z_compress2, "compress2") && sym(i, &L.z_uncompress, "uncompress") && sym(i, &L.z_bound, "compressBound") &&
                 sym(i, &L.z_version, "zlibVersion");
            if (ok) v = L.z_version();
            break;
        case WK_LIB_ZSTD:
            ok = sym(i, &L.zs_cctx, "ZSTD_createCCtx") && sym(i, &L.zs_freec, "ZSTD_freeCCtx") && sym(i, &L.zs_dctx, "ZSTD_createDCtx") &&
                 sym(i, &L.zs_freed, "ZSTD_freeDCtx") && sym(i, &L.zs_compress, "ZSTD_compressCCtx") &&
                 sym(i, &L.zs_decompress, "ZSTD_decompressDCtx") && sym(i, &L.zs_iserr, "ZSTD_isError") &&
                 sym(i, &L.zs_bound, "ZSTD_compressBound") && sym(i, &L.zs_version, "ZSTD_versionString");
            if (ok) v = L.zs_version();
            break;
        case WK_LIB_LZ4:
            ok = sym(i, &L.l4_compress, "LZ4_compress_default") && sym(i, &L.l4_decompress, "LZ4_decompress_safe") &&
                 sym(i, &L.l4_bound, "LZ4_compressBound") && sym(i, &L.l4_version, "LZ4_versionString");
            if (ok) v = L.l4_version();
            break;
        default:
            ok = sym(i, &L.xz_encode, "lzma_easy_buffer_encode") && sym(i, &L.xz_decode, "lzma_stream_buffer_decode") &&
                 sym(i, &L.xz_bound, "lzma_stream_buffer_bound") && sym(i, &L.xz_version, "lzma_version_string");
            if (ok) v = L.xz_version();
            break;
        }
        if (!ok) { L.h[i] = NULL; continue; }   /* not closed: harmless, and closing could unload a library someone else uses */
        snprintf(L.ver[i], sizeof L.ver[i], "%s %.24s (%s)", wk_lib_id[i], v ? v : "?", so[i]);
    }
}

static const int clevel[WK_NCODEC] = { 1, 6, 9, 1, 3, 19, 0, 1, 6 };
#define XZ_CHECK_CRC64 4
typedef struct { void *cctx, *dctx; } cx_ctx;

static int cx_open(int c, cx_ctx *x)
{
    memset(x, 0, sizeof *x);
    if (wk_codec_lib(c) != WK_LIB_ZSTD) return 1;
    x->cctx = L.zs_cctx();
    x->dctx = L.zs_dctx();
    return x->cctx && x->dctx;
}

static void cx_close(int c, cx_ctx *x)
{
    if (wk_codec_lib(c) != WK_LIB_ZSTD) return;
    if (x->cctx) L.zs_freec(x->cctx);
    if (x->dctx) L.zs_freed(x->dctx);
    x->cctx = x->dctx = NULL;
}

static size_t cx_bound(int c, size_t n)
{
    switch (wk_codec_lib(c)) {
    case WK_LIB_ZLIB: return L.z_bound(n);
    case WK_LIB_ZSTD: return L.zs_bound(n);
    case WK_LIB_LZ4: return (size_t)L.l4_bound((int)n);
    default: return L.xz_bound(n);
    }
}

/* 0 = ok; *on = bytes written */
static int cx_comp(int c, cx_ctx *x, const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *on)
{
    switch (wk_codec_lib(c)) {
    case WK_LIB_ZLIB: { unsigned long dl = cap; int rc = L.z_compress2(out, &dl, in, n, clevel[c]); *on = dl; return rc != 0; }
    case WK_LIB_ZSTD: { size_t r = L.zs_compress(x->cctx, out, cap, in, n, clevel[c]); *on = r; return L.zs_iserr(r) != 0; }
    case WK_LIB_LZ4: { int r = L.l4_compress((const char *)in, (char *)out, (int)n, (int)cap); *on = r > 0 ? (size_t)r : 0; return r <= 0; }
    default: { size_t pos = 0; int rc = L.xz_encode((uint32_t)clevel[c], XZ_CHECK_CRC64, NULL, in, n, out, &pos, cap); *on = pos; return rc != 0; }
    }
}

static int cx_dec(int c, cx_ctx *x, const uint8_t *in, size_t n, uint8_t *out, size_t cap, size_t *on)
{
    switch (wk_codec_lib(c)) {
    case WK_LIB_ZLIB: { unsigned long dl = cap; int rc = L.z_uncompress(out, &dl, in, n); *on = dl; return rc != 0; }
    case WK_LIB_ZSTD: { size_t r = L.zs_decompress(x->dctx, out, cap, in, n); *on = r; return L.zs_iserr(r) != 0; }
    case WK_LIB_LZ4: { int r = L.l4_decompress((const char *)in, (char *)out, (int)n, (int)cap); *on = r >= 0 ? (size_t)r : 0; return r < 0; }
    default: {
        uint64_t ml = UINT64_MAX;
        size_t ip = 0, op = 0;
        int rc = L.xz_decode(&ml, 0, NULL, in, &ip, n, out, &op, cap);
        *on = op;
        return rc != 0 || ip != n;
    }
    }
}

/* round trip of n bytes through codec c; 1 = output identical */
static int cx_roundtrip(int c, const uint8_t *in, size_t n, size_t *clen)
{
    size_t cap = cx_bound(c, n), cl = 0, dl = 0;
    uint8_t *cb = malloc(cap), *db = malloc(n + 1);
    cx_ctx x;
    int ok = cb && db && cx_open(c, &x);
    if (ok) {
        ok = !cx_comp(c, &x, in, n, cb, cap, &cl) && !cx_dec(c, &x, cb, cl, db, n + 1, &dl) && dl == n && !memcmp(in, db, n);
        cx_close(c, &x);
    }
    if (clen) *clen = cl;
    free(cb);
    free(db);
    return ok;
}

/* ======================================================================
 * generated test data (fixed seed: every run and every PC does the same work)
 * ====================================================================== */
static const char *const vocab[] = {
    "the", "of", "and", "to", "in", "a", "is", "that", "for", "it", "as", "was", "with", "be", "by", "on", "not", "he", "this",
    "are", "or", "his", "from", "at", "which", "but", "have", "an", "had", "they", "you", "were", "their", "one", "all", "we",
    "can", "her", "has", "there", "been", "if", "more", "when", "will", "would", "who", "so", "no", "system", "report", "search",
    "memory", "drive", "file", "folder", "window", "program", "number", "value", "result", "time", "speed", "network", "driver",
    "kernel", "package", "update", "error", "screen", "sound", "power", "fan", "clock", "board", "cable", "temperature",
    "café", "naïve", "Größe", "résumé", "日本語", "テスト", "Ελληνικά", "русский", "😀" };
#define NVOCAB (sizeof vocab / sizeof *vocab)
#define NEEDLE "#needle#"
static const char *const needles[3] = { NEEDLE, "the ", "zqxjv" };

static const char *pick_word(uint64_t *s)
{
    uint64_t r = sm64(s);
    return vocab[(size_t)((r & 0xffff) % NVOCAB * ((r >> 16 & 0xffff) % NVOCAB) / NVOCAB)];   /* common words more often */
}

static long cps(const char *w) { long n = 0; for (; *w; w++) n += ((unsigned char)*w & 0xC0) != 0x80; return n; }

/* prose with a few non-English words; returns the planted needles, *cp = code points */
static long gen_text(uint8_t *b, size_t n, long *cp)
{
    uint64_t s = 0x5253532D74657874ull;
    size_t len = 0;
    long planted = 0;
    char w[64];
    *cp = 0;
    for (;;) {
        int words = 6 + (int)(sm64(&s) % 12), i;
        for (i = 0; i < words; i++) {
            uint64_t r = sm64(&s);
            if (r % 23 == 0) snprintf(w, sizeof w, "%u", (unsigned)(r >> 40) % 10000);
            else snprintf(w, sizeof w, "%s", pick_word(&s));
            if (!i && w[0] >= 'a' && w[0] <= 'z') w[0] = (char)(w[0] - 32);
            if (len / 3000 > (size_t)planted) { snprintf(w, sizeof w, NEEDLE); planted++; }
            if (len + strlen(w) + 3 >= n) goto done;
            memcpy(b + len, w, strlen(w));
            len += strlen(w);
            *cp += cps(w);
            b[len++] = (uint8_t)(i + 1 == words ? '.' : r % 9 == 0 ? ',' : ' ');
            if (b[len - 1] == ',') { b[len++] = ' '; (*cp)++; }
            (*cp)++;
        }
        b[len++] = (uint8_t)(sm64(&s) % 5 ? ' ' : '\n');
        (*cp)++;
    }
done:
    while (len < n) { b[len++] = '\n'; (*cp)++; }
    return planted;
}

/* 32-byte records: id, time stamp, kind, flags, a measured value, a name index */
static void gen_records(uint8_t *b, size_t n)
{
    uint64_t s = 0x5253532D72656373ull, t = 1767225600000ull;
    size_t i;
    memset(b, 0, n);
    for (i = 0; i + 32 <= n; i += 32) {
        uint64_t r = sm64(&s);
        uint32_t id = (uint32_t)(i / 32), val = 20000 + (uint32_t)(r % 400), name = (uint32_t)(r >> 20) % 300;
        t += 1000 + (r >> 32) % 50;
        memcpy(b + i, &id, 4);
        memcpy(b + i + 4, &t, 8);
        b[i + 12] = (uint8_t)((r >> 8) % 6);
        b[i + 13] = (uint8_t)(r >> 13 & 3);
        memcpy(b + i + 16, &val, 4);
        memcpy(b + i + 20, &name, 2);
    }
}

static void gen_random(uint8_t *b, size_t n)
{
    uint64_t s = 0x5253532D72616E64ull, v;
    size_t i;
    for (i = 0; i < n; i += 8) { v = sm64(&s); memcpy(b + i, &v, n - i < 8 ? n - i : 8); }
}

/* an array of records with every JSON value type; *want = the tokens written */
static size_t gen_json(char *b, size_t n, jcount *want)
{
    static const char *const notes[] = { "say \\\"hi\\\"\\n", "\\u00e9t\\u00e9 \\/ tab\\there", "café, naïve 日本語", "plain words", "back\\\\slash" };
    uint64_t s = 0x5253532D6A736F6Eull;
    size_t len = 1;
    unsigned id = 0;
    memset(want, 0, sizeof *want);
    b[0] = '[';
    want->arr = 1;
    while (len + 600 < n) {
        uint64_t r = sm64(&s);
        int par = r % 3 == 0;
        const char *w1 = pick_word(&s), *w2 = pick_word(&s), *w3 = pick_word(&s), *w4 = pick_word(&s);
        len += (size_t)snprintf(b + len, n - len,
            "%s{\"id\":%u,\"name\":\"%s %s\",\"email\":\"user%u@example.org\",\"score\":%s%u.%02u,\"ratio\":%ue-%u,"
            "\"active\":%s,\"parent\":", id ? ",\n" : "", id, w1, w2, (unsigned)(r >> 8) % 10000,
            r & 1 ? "-" : "", (unsigned)(r >> 20) % 1000, (unsigned)(r >> 30) % 100, (unsigned)(r >> 40) % 9 + 1, (unsigned)(r >> 44) % 9 + 1,
            r & 2 ? "true" : "false");
        if (par) len += (size_t)snprintf(b + len, n - len, "%u", id / 2);
        else len += (size_t)snprintf(b + len, n - len, "null");
        len += (size_t)snprintf(b + len, n - len, ",\"tags\":[\"%s\",\"%s\"],\"pos\":{\"x\":%d,\"y\":%d},\"note\":\"%s\"}",
                                w3, w4, (int)(r >> 48 & 0xff) - 128, (int)(r >> 56), notes[(r >> 4) % 5]);
        want->obj += 2; want->arr += 1; want->key += 12; want->str += 5;
        want->num += 5 + par; want->lit += 1 + !par;
        id++;
    }
    b[len++] = ']';
    return len;
}

/* ======================================================================
 * the run
 * ====================================================================== */
enum { PART_N = 3 };
typedef struct {
    wk_cfg c;
    wk_result *r;
    volatile int *cancel;
    int cpus[BURN_MAXCPU], ncpus;
    size_t part;
    uint8_t *corpus;                      /* text | records | random, each `part` bytes */
    uint64_t *keys; size_t nkeys; uint64_t key_sum, key_mix;
    char *strbuf, **strs; size_t nstrs;
    uint8_t *json; size_t jlen; jcount jwant;
    long utf8_want, find_want[3];
    uint32_t crc32_want, crc32c_want; uint64_t xxh_want, fnv_want;
    vins prog[VM_LEN]; uint64_t vm_count, vm_hash, vm_ops;
    uint8_t *cdata[WK_NCODEC][PART_N]; size_t clen[WK_NCODEC][PART_N];
} wctx;

static int stopped(wctx *x)
{
    if (x->cancel && *x->cancel) x->r->cancelled = 1;
    return x->r->cancelled;
}

static void set_stage(const char *g, const char *what, int threads)
{
    snprintf(g_stage_buf, sizeof g_stage_buf, "%s: %s, %d thread%s", g, what, threads, threads == 1 ? "" : "s");
    g_stage = g_stage_buf;
}

static long mem_avail_kib(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char line[128];
    long v = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) if (sscanf(line, "MemAvailable: %ld", &v) == 1) break;
    fclose(f);
    return v;
}

static int prepare(wctx *x)
{
    size_t p = x->part, i, sl = 0;
    uint64_t s = 0x5253532D736F7274ull;
    long cp;
    if (!(x->corpus = malloc(PART_N * p))) return 0;
    if (gen_text(x->corpus, p, &cp) != count_naive(x->corpus, p, NEEDLE)) return 0;
    x->utf8_want = cp;
    gen_records(x->corpus + p, p);
    gen_random(x->corpus + 2 * p, p);
    for (i = 0; i < 3; i++) x->find_want[i] = count_naive(x->corpus, p, needles[i]);
    x->crc32_want = crc32_byte(x->corpus, PART_N * p);
    x->crc32c_want = crc32c_table(x->corpus, PART_N * p);
    x->xxh_want = xxh64(x->corpus, PART_N * p, 0);
    x->fnv_want = fnv1a(x->corpus, PART_N * p);
    x->nkeys = p / 8;
    x->nstrs = p / 16;
    x->keys = malloc(x->nkeys * sizeof *x->keys);
    x->strs = malloc(x->nstrs * sizeof *x->strs);
    x->strbuf = malloc(x->nstrs * 40);
    x->json = malloc(p);
    if (!x->keys || !x->strs || !x->strbuf || !x->json) return 0;
    for (i = 0; i < x->nkeys; i++) {
        x->keys[i] = sm64(&s);
        x->key_sum += x->keys[i];
        x->key_mix ^= mix64(x->keys[i]);
    }
    for (i = 0; i < x->nstrs; i++) {   /* "kernel-update-1234": many shared beginnings, like file names */
        uint64_t r = sm64(&s);
        const char *w1 = pick_word(&s), *w2 = pick_word(&s);
        x->strs[i] = x->strbuf + sl;
        sl += (size_t)snprintf(x->strbuf + sl, 40, r % 4 ? "%.12s-%.12s-%u" : "%.12s%.0s%u", w1, w2, (unsigned)(r >> 32) % 5000) + 1;
    }
    x->jlen = gen_json((char *)x->json, p, &x->jwant);
    vm_program(x->prog, VM_LIMIT);
    vm_ref(VM_LIMIT, &x->vm_count, &x->vm_hash);
    {
        uint64_t r[8];
        x->vm_ops = vm_switch(x->prog, r, 1);
        if (r[5] != x->vm_count || r[7] != x->vm_hash) return 0;
    }
    return 1;
}

static void unprepare(wctx *x)
{
    int c, p;
    for (c = 0; c < WK_NCODEC; c++) for (p = 0; p < PART_N; p++) free(x->cdata[c][p]);
    free(x->corpus); free(x->keys); free(x->strs); free(x->strbuf); free(x->json);
}

/* one measurement on n threads */
enum { JK_COMP, JK_DEC, JK_TEST };
typedef struct {
    wctx *x;
    int kind, id, cpu;
    atomic_int *ready, *go;
    pthread_t t;
    double units, ktime;
    int fail, oom;
    char why[96];
} wjob;

typedef struct { uint8_t *b[PART_N + 1]; uint64_t *u, *ut; char **sp; cx_ctx cx; int cx_open; } wstate;

static int st_setup(wjob *j, wstate *s)
{
    wctx *x = j->x;
    size_t p = x->part;
    int i;
    memset(s, 0, sizeof *s);
    if (j->kind != JK_TEST) {
        s->cx_open = cx_open(j->id, &s->cx);
        if (!s->cx_open) return 0;
        for (i = 0; i < PART_N; i++) if (!(s->b[i] = malloc(j->kind == JK_COMP ? cx_bound(j->id, p) : p + 1))) return 0;
        if (j->kind == JK_COMP && !(s->b[PART_N] = malloc(p + 1))) return 0;
        return 1;
    }
    switch (j->id) {
    case WT_SORT_QSORT_U64: case WT_SORT_RADIX_U64:
        return (s->u = malloc(x->nkeys * sizeof *s->u)) && (s->ut = malloc(x->nkeys * sizeof *s->ut));
    case WT_SORT_QSORT_STR: case WT_SORT_MKQS_STR:
        return (s->sp = malloc(x->nstrs * sizeof *s->sp)) != NULL;
    default: return 1;
    }
}

static void st_free(wjob *j, wstate *s)
{
    int i;
    if (s->cx_open) cx_close(j->id, &s->cx);
    for (i = 0; i <= PART_N; i++) free(s->b[i]);
    free(s->u); free(s->ut); free(s->sp);
}

#define FAIL(...) do { snprintf(j->why, sizeof j->why, __VA_ARGS__); j->fail = 1; return 1; } while (0)

/* one pass of the work; *kt = seconds spent in the measured part. 0 ok, 1 check failed */
static int st_iter(wjob *j, wstate *s, double *kt)
{
    wctx *x = j->x;
    size_t p = x->part, i, n;
    double t;
    int k;
    *kt = 0;
    if (j->kind == JK_COMP) {
        for (k = 0; k < PART_N; k++) {
            size_t cl = 0, dl = 0, cap = cx_bound(j->id, p);
            t = now_s();
            if (cx_comp(j->id, &s->cx, x->corpus + k * p, p, s->b[k], cap, &cl)) FAIL("the library reported an error");
            *kt += now_s() - t;
            if (cx_dec(j->id, &s->cx, s->b[k], cl, s->b[PART_N], p + 1, &dl) || dl != p || memcmp(s->b[PART_N], x->corpus + k * p, p))
                FAIL("round trip does not give back the original data");
        }
        j->units += PART_N * (double)p;
        return 0;
    }
    if (j->kind == JK_DEC) {
        for (k = 0; k < PART_N; k++) {
            size_t dl = 0;
            t = now_s();
            if (cx_dec(j->id, &s->cx, x->cdata[j->id][k], x->clen[j->id][k], s->b[k], p + 1, &dl)) FAIL("the library reported an error");
            *kt += now_s() - t;
            if (dl != p || memcmp(s->b[k], x->corpus + k * p, p)) FAIL("decompressed data differs from the original");
        }
        j->units += PART_N * (double)p;
        return 0;
    }
    switch (j->id) {
    case WT_SORT_QSORT_U64: case WT_SORT_RADIX_U64: {
        uint64_t sum = 0, mix = 0;
        n = x->nkeys;
        memcpy(s->u, x->keys, n * sizeof *s->u);
        t = now_s();
        if (j->id == WT_SORT_RADIX_U64) radix_u64(s->u, s->ut, n);
        else qsort(s->u, n, sizeof *s->u, cmp_u64);
        *kt = now_s() - t;
        for (i = 0; i < n; i++) {
            if (i && s->u[i - 1] > s->u[i]) FAIL("not in order");
            sum += s->u[i];
            mix ^= mix64(s->u[i]);
        }
        if (sum != x->key_sum || mix != x->key_mix) FAIL("keys lost or duplicated");
        j->units += (double)n;
        return 0;
    }
    case WT_SORT_QSORT_STR: case WT_SORT_MKQS_STR: {
        uint64_t sum = 0;
        n = x->nstrs;
        memcpy(s->sp, x->strs, n * sizeof *s->sp);
        t = now_s();
        if (j->id == WT_SORT_MKQS_STR) mkqs(s->sp, n, 0);
        else qsort(s->sp, n, sizeof *s->sp, cmp_str);
        *kt = now_s() - t;
        for (i = 0; i < n; i++) {
            if (i && strcmp(s->sp[i - 1], s->sp[i]) > 0) FAIL("not in order");
            sum += mix64((uint64_t)(s->sp[i] - x->strbuf));
        }
        for (i = 0; i < n; i++) sum -= mix64((uint64_t)(x->strs[i] - x->strbuf));
        if (sum) FAIL("strings lost or duplicated");
        j->units += (double)n;
        return 0;
    }
    case WT_CRC32_BYTE: case WT_CRC32_SLICE8: case WT_CRC32C_TABLE: case WT_CRC32C_HW: case WT_XXH64: case WT_FNV1A: {
        uint64_t h, want;
        n = PART_N * p;
        t = now_s();
        switch (j->id) {
        case WT_CRC32_BYTE: h = crc32_byte(x->corpus, n); want = x->crc32_want; break;
        case WT_CRC32_SLICE8: h = crc32_slice8(x->corpus, n); want = x->crc32_want; break;
        case WT_CRC32C_TABLE: h = crc32c_table(x->corpus, n); want = x->crc32c_want; break;
        case WT_CRC32C_HW: h = crc32c_hw(x->corpus, n); want = x->crc32c_want; break;
        case WT_XXH64: h = xxh64(x->corpus, n, 0); want = x->xxh_want; break;
        default: h = fnv1a(x->corpus, n); want = x->fnv_want; break;
        }
        *kt = now_s() - t;
        if (h != want) FAIL("result differs from the known value");
        j->units += (double)n;
        return 0;
    }
    case WT_FIND_MEMMEM: case WT_FIND_BMH: {
        long got[3];
        t = now_s();
        for (k = 0; k < 3; k++) got[k] = j->id == WT_FIND_BMH ? count_bmh(x->corpus, p, needles[k]) : count_memmem(x->corpus, p, needles[k]);
        *kt = now_s() - t;
        for (k = 0; k < 3; k++) if (got[k] != x->find_want[k]) FAIL("found \"%s\" %ld times, should be %ld", needles[k], got[k], x->find_want[k]);
        j->units += 3.0 * (double)p;
        return 0;
    }
    case WT_JSON: {
        jcount c;
        int ok;
        t = now_s();
        ok = json_check(x->json, x->jlen, &c);
        *kt = now_s() - t;
        if (!ok || memcmp(&c, &x->jwant, sizeof c)) FAIL("%s", ok ? "token counts differ from what was written" : "valid document rejected");
        j->units += (double)x->jlen;
        return 0;
    }
    case WT_UTF8: {
        long c;
        t = now_s();
        c = utf8_count(x->corpus, p);
        *kt = now_s() - t;
        if (c != x->utf8_want) FAIL("counted %ld characters, should be %ld", c, x->utf8_want);
        j->units += (double)p;
        return 0;
    }
    default: {
        uint64_t r[8];
        t = now_s();
        if (j->id == WT_VM_THREADED) vm_threaded(x->prog, r);
        else vm_switch(x->prog, r, 0);
        *kt = now_s() - t;
        if (r[5] != x->vm_count || r[7] != x->vm_hash) FAIL("wrong answer from the program");
        j->units += (double)x->vm_ops;
        return 0;
    }
    }
}

static void *job_run(void *arg)
{
    wjob *j = arg;
    wstate s;
    cpu_set_t cs;
    int ok;
    CPU_ZERO(&cs);
    CPU_SET(j->cpu, &cs);
    pthread_setaffinity_np(pthread_self(), sizeof cs, &cs);
    ok = st_setup(j, &s);
    if (!ok) { j->oom = 1; snprintf(j->why, sizeof j->why, "not enough memory"); }
    atomic_fetch_add(j->ready, 1);
    while (!atomic_load(j->go)) sched_yield();   /* all threads start together */
    if (ok) {
        double t0 = now_s(), kt;
        do {
            if (st_iter(j, &s, &kt)) break;
            j->ktime += kt;
        } while (now_s() - t0 < j->x->c.seconds && !(j->x->cancel && *j->x->cancel));
    }
    st_free(j, &s);
    return NULL;
}

/* threads for the N column: what was asked, fewer when each thread's memory would not fit
   in half of the memory that is free */
static int fit_threads(wctx *x, int kind, int id, int want)
{
    static const double codec_mib[WK_NCODEC] = { 1, 1, 1, 2, 4, 64, 1, 12, 100 };
    double per = 0, budget = mem_avail_kib() / 2048.0, p = x->part / 1048576.0;
    int n;
    if (kind == JK_COMP) per = codec_mib[id] + 4 * p * 1.01;
    else if (kind == JK_DEC) per = codec_mib[id] / 4 + 3 * p;
    else if (id <= WT_SORT_RADIX_U64) per = 2 * p;
    else if (id <= WT_SORT_MKQS_STR) per = p / 2;
    else return want;
    if (budget <= 0) return want;
    n = (int)(budget / (per + 1));
    return n < 1 ? 1 : n < want ? n : want;
}

/* 0 ok (rate in units per second), 1 check failed, -1 out of memory */
static int measure(wctx *x, int kind, int id, int nthreads, double *rate, char *why, size_t wn)
{
    wjob *js = calloc((size_t)nthreads, sizeof *js);
    atomic_int ready = 0, go = 0;
    struct timespec ms = { 0, 1000000 };
    int i, rc = 0, started = 0;
    *rate = 0;
    if (!js) { snprintf(why, wn, "not enough memory"); return -1; }
    for (i = 0; i < nthreads; i++) {
        js[i].x = x; js[i].kind = kind; js[i].id = id; js[i].cpu = x->cpus[i % x->ncpus]; js[i].ready = &ready; js[i].go = &go;
    }
    for (i = 0; i < nthreads; i++) {
        if (pthread_create(&js[i].t, NULL, job_run, &js[i])) {
            js[i].oom = 1;
            snprintf(js[i].why, sizeof js[i].why, "could not start a thread");
            break;
        }
        started++;
    }
    while (atomic_load(&ready) < started) nanosleep(&ms, NULL);
    atomic_store(&go, 1);
    for (i = 0; i < started; i++) pthread_join(js[i].t, NULL);
    for (i = 0; i < nthreads; i++) {
        if (js[i].fail && rc != 1) { rc = 1; snprintf(why, wn, "%s", js[i].why); }
        else if (js[i].oom && !rc) { rc = -1; snprintf(why, wn, "%s", js[i].why); }
        if (js[i].ktime > 0) *rate += js[i].units / js[i].ktime;
    }
    free(js);
    return rc;
}

/* fill both columns of one cell */
static void run_cell(wctx *x, wk_cell *cell, int kind, int id, const char *group, const char *what, double scale)
{
    int col, nt;
    char why[96];
    if (x->c.cb) x->c.cb(x->r, 0, x->c.ud);
    for (col = 0; col < 2 && !stopped(x); col++) {
        int rc;
        double v;
        if (col && x->c.single_only) break;
        nt = col ? fit_threads(x, kind, id, x->c.threads) : 1;
        set_stage(group, what, nt);
        rc = measure(x, kind, id, nt, &v, why, sizeof why);
        if (rc == 1) {
            cell->state = WK_ST_FAIL;
            snprintf(cell->note, sizeof cell->note, "%s", why);
            x->r->failed++;
            break;
        }
        if (rc < 0) {   /* a 1-thread result stays; the note says why the N column is empty */
            if (!col) cell->state = WK_ST_SKIP;
            snprintf(cell->note, sizeof cell->note, "%s%.70s", col ? "threads column: " : "", why);
            break;
        }
        if (stopped(x)) break;   /* a cut-short pass is not a result */
        cell->v[col] = v / scale;
        if (col) cell->threads = nt;
        cell->state = WK_ST_OK;
        if (x->c.cb) x->c.cb(x->r, 1, x->c.ud);
    }
    if (cell->state == WK_ST_OK && !x->c.single_only && cell->threads && cell->threads < x->c.threads && !cell->note[0])
        snprintf(cell->note, sizeof cell->note, "%d threads: memory", cell->threads);
}

static void run_codec(wctx *x, int c)
{
    wk_result *r = x->r;
    int lib = wk_codec_lib(c), k;
    size_t p = x->part, tot = 0, cap;
    char what[64];
    cx_ctx cx;
    if (!L.h[lib]) {
        r->comp[c].state = r->dec[c].state = WK_ST_SKIP;
        snprintf(r->comp[c].note, sizeof r->comp[c].note, "not installed");
        snprintf(r->dec[c].note, sizeof r->dec[c].note, "not installed");
        return;
    }
    set_stage("Compression", wk_codec_name[c], 1);
    if (!cx_open(c, &cx)) { r->comp[c].state = WK_ST_SKIP; snprintf(r->comp[c].note, sizeof r->comp[c].note, "not enough memory"); return; }
    cap = cx_bound(c, p);
    for (k = 0; k < PART_N && !stopped(x); k++) {   /* the data the decompress test reads; also gives the ratios */
        if (!(x->cdata[c][k] = malloc(cap)) || cx_comp(c, &cx, x->corpus + k * p, p, x->cdata[c][k], cap, &x->clen[c][k])) {
            r->comp[c].state = r->dec[c].state = WK_ST_FAIL;
            snprintf(r->comp[c].note, sizeof r->comp[c].note, "the library reported an error");
            r->failed++;
            cx_close(c, &cx);
            return;
        }
        r->ratio[c][k] = (double)p / (double)x->clen[c][k];
        tot += x->clen[c][k];
    }
    cx_close(c, &cx);
    if (stopped(x)) return;
    r->ratio[c][WK_PART_MIX] = (double)(PART_N * p) / (double)tot;
    snprintf(what, sizeof what, "%s compress", wk_codec_name[c]);
    run_cell(x, &r->comp[c], JK_COMP, c, "Compression", what, 1e6);
    if (stopped(x)) return;
    snprintf(what, sizeof what, "%s decompress", wk_codec_name[c]);
    run_cell(x, &r->dec[c], JK_DEC, c, "Compression", what, 1e6);
}

/* ======================================================================
 * self-test
 * ====================================================================== */
int wk_selftest(char *msg, size_t mn)
{
    static const uint8_t kat[] = "123456789";
    static const char jdoc[] = " {\"a\":[1,-2.5e+3,\"x\\u00e9\\n\",true,false,null,{}],\"b\":[]} ",
                      utxt[] = "caf\xC3\xA9 \xE6\x97\xA5 \xF0\x9F\x98\x80 plain ascii text";
    static const char *const bad_json[] = { "[\"a\",]", "{\"a\" 1}", "[01]", "[\"\\x\"]", "[1,2", "{\"a\":tru}", "[-]", "[1.]", "[\"a\nb\"]", "" };
    static const char *const bad_utf8[] = { "\xC0\xAF", "\xE0\x80\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xF8\x88\x80\x80\x80", "a\xE3\x81", "\x80" };
    uint8_t s100[100], *buf;
    uint64_t sd = 42, cnt, hsh, rg[8];
    uint64_t a[1000], b[1000], t[1000];
    char *sp[600], *sq[600], sb[600 * 16];
    vins prog[VM_LEN];
    jcount jc;
    size_t i, sl = 0;
    int lib, c, ok;
    long tcp;
    char libs[64] = "";
    pthread_once(&crc_once, crc_tables);
    pthread_once(&lib_once, lib_load);
    for (i = 0; i < sizeof s100; i++) s100[i] = (uint8_t)(i * 7 + 3);
#define CHECK(cond, ...) do { if (!(cond)) { snprintf(msg, mn, __VA_ARGS__); return 0; } } while (0)
    CHECK(crc32_byte(kat, 9) == 0xCBF43926u && crc32_slice8(kat, 9) == 0xCBF43926u && crc32_slice8(s100, 100) == 0xAA316B09u &&
          crc32_byte(s100, 100) == 0xAA316B09u, "CRC32 gives a wrong known answer");
    CHECK(crc32c_table(kat, 9) == 0xE3069283u && crc32c_table(s100, 100) == 0x594B1B65u, "CRC32C gives a wrong known answer");
    CHECK(!have_crc32c_hw() || (crc32c_hw(kat, 9) == 0xE3069283u && crc32c_hw(s100, 100) == 0x594B1B65u), "CRC32C with the CPU instruction gives a wrong known answer");
    CHECK(xxh64(kat, 0, 0) == 0xEF46DB3751D8E999ull && xxh64((const uint8_t *)"abc", 3, 0) == 0x44BC2CF5AD770999ull &&
          xxh64(kat, 9, 0) == 0x8CB841DB40E6AE83ull && xxh64(s100, 100, 0) == 0xA61F8D4C170FE531ull &&
          xxh64((const uint8_t *)"abc", 3, 0x9E3779B97F4A7C15ull) == 0x2ED0F59D6B43AC8Bull, "xxHash64 gives a wrong known answer");
    CHECK(fnv1a(kat, 0) == 0xcbf29ce484222325ull && fnv1a((const uint8_t *)"a", 1) == 0xaf63dc4c8601ec8cull &&
          fnv1a(kat, 9) == 0x06d5573923c6cdfcull, "FNV-1a gives a wrong known answer");
    for (i = 0; i < 1000; i++) a[i] = b[i] = i % 3 ? sm64(&sd) : sm64(&sd) % 7;   /* with duplicates */
    radix_u64(a, t, 1000);
    qsort(b, 1000, sizeof *b, cmp_u64);
    CHECK(!memcmp(a, b, sizeof a), "radix sort differs from qsort");
    for (i = 0; i < 1000; i++) a[i] = 5;
    radix_u64(a, t, 1000);
    CHECK(a[0] == 5 && a[999] == 5, "radix sort breaks equal keys");
    for (i = 0; i < 600; i++) {
        uint64_t r = sm64(&sd);
        sp[i] = sq[i] = sb + sl;
        sl += (size_t)snprintf(sb + sl, 16, "%.*s", (int)(r % 6), &"abcab\0"[r >> 8 & 1]) + 1;   /* short, empty and equal strings */
    }
    mkqs(sp, 600, 0);
    qsort(sq, 600, sizeof *sq, cmp_str);
    for (i = 0; i < 600; i++) CHECK(!strcmp(sp[i], sq[i]), "multikey quicksort differs from qsort");
    {
        static const char hay[] = "aaaaa the cat; the end the";
        static const char *const nd[] = { "aaa", "the", "the end the", "x", "a", "end the!" };
        for (i = 0; i < 6; i++)
            CHECK(count_bmh((const uint8_t *)hay, sizeof hay - 1, nd[i]) == count_naive((const uint8_t *)hay, sizeof hay - 1, nd[i]) &&
                  count_memmem((const uint8_t *)hay, sizeof hay - 1, nd[i]) == count_naive((const uint8_t *)hay, sizeof hay - 1, nd[i]),
                  "text search miscounts \"%s\"", nd[i]);
    }
    CHECK(json_check((const uint8_t *)jdoc, strlen(jdoc), &jc) &&
          jc.obj == 2 && jc.arr == 2 && jc.key == 2 && jc.str == 1 && jc.num == 2 && jc.lit == 3, "JSON check rejects or miscounts a valid document");
    for (i = 0; i < sizeof bad_json / sizeof *bad_json; i++)
        CHECK(!json_check((const uint8_t *)bad_json[i], strlen(bad_json[i]), NULL), "JSON check accepts the broken document '%s'", bad_json[i]);
    CHECK(utf8_count((const uint8_t *)utxt, strlen(utxt)) == 25, "UTF-8 check miscounts valid text");
    for (i = 0; i < sizeof bad_utf8 / sizeof *bad_utf8; i++)
        CHECK(utf8_count((const uint8_t *)bad_utf8[i], strlen(bad_utf8[i])) < 0, "UTF-8 check accepts invalid sequence %zu", i + 1);
    vm_program(prog, 1000);
    vm_ref(1000, &cnt, &hsh);
    CHECK(cnt == 168 && hsh == 0xC6F57458414D4FE4ull, "plain C prime count is wrong");
    vm_switch(prog, rg, 0);
    CHECK(rg[5] == cnt && rg[7] == hsh, "interpreter (switch) gives a wrong answer");
    vm_threaded(prog, rg);
    CHECK(rg[5] == cnt && rg[7] == hsh, "interpreter (threaded) gives a wrong answer");
    CHECK((buf = malloc(4096)) != NULL, "out of memory");
    gen_text(buf, 2048, &tcp);
    gen_random(buf + 2048, 2048);
    ok = 1;
    for (lib = 0; lib < WK_NLIB && ok; lib++) {
        if (!L.h[lib]) continue;
        for (c = 0; c < WK_NCODEC && ok; c++) if (wk_codec_lib(c) == lib && !cx_roundtrip(c, buf, 4096, NULL)) ok = 0;
        if (ok) { size_t l = strlen(libs); snprintf(libs + l, sizeof libs - l, "%s%s", l ? ", " : "", wk_lib_id[lib]); }
        else snprintf(msg, mn, "%s: 4 KB round trip does not give back the original data", wk_codec_name[c - 1]);
    }
    free(buf);
    if (!ok) return 0;
    snprintf(msg, mn, "CRC32, CRC32C%s, xxHash64 and FNV-1a known answers pass; sorts, search, JSON, UTF-8 and interpreter match "
             "reference code; round trip: %s", have_crc32c_hw() ? " (also CPU instruction)" : "", libs[0] ? libs : "no library installed");
    return 1;
#undef CHECK
}

/* ======================================================================
 * rendering
 * ====================================================================== */
static void fmt_v(char *b, size_t n, double v, const char *unit)
{
    if (v <= 0) snprintf(b, n, "-");
    else snprintf(b, n, v >= 100 ? "%.0f %s" : v >= 10 ? "%.1f %s" : "%.2f %s", v, unit);
}

static void render_cell(srs_out *o, const wk_result *r, const char *name, const wk_cell *c, const char *unit, const char *extra)
{
    char a[32], b[32], note[160];
    const char *st = c->state == WK_ST_FAIL ? "check failed" : c->state == WK_ST_SKIP ? "not measured" : NULL;
    snprintf(note, sizeof note, "%s%s%s", extra ? extra : "", extra && extra[0] && c->note[0] ? "; " : "", c->note);
    if (st) {
        so_row(o, 4, (const char *const[]){ name, st, "", note }, (const int[]){ C_VALUE, c->state == WK_ST_FAIL ? C_RED : C_GRAY, C_GRAY,
                                                                                  c->state == WK_ST_FAIL ? C_RED : C_GRAY });
        return;
    }
    fmt_v(a, sizeof a, c->v[0], unit);
    if (r->single_only) snprintf(b, sizeof b, "not run");
    else fmt_v(b, sizeof b, c->v[1], unit);
    so_row(o, 4, (const char *const[]){ name, a, b, note }, (const int[]){ C_VALUE, C_BLACK, C_GREEN, C_GRAY });
}

static void wk_render(srs_out *o, void *cv)
{
    static const int tabs[] = { 290, 400, 520 }, tabs2[] = { 290, 370, 450, 530, 610 }, tabs3[] = { 210 };
    const wk_result *r = cv;
    int g, t, c, lib;
    char h2[32];
    so_siv_header(o, "Everyday work (compression, sorting, checksums, text processing, bytecode interpreter)");
    so_tabs(o, tabs, 3);
    snprintf(h2, sizeof h2, "%d threads", r->threads);
    so_row(o, 4, (const char *const[]){ "Test", "1 thread", h2, "Note" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (g = 0; g < WK_NGROUP; g++) {
        int any = 0;
        if (g == WK_COMPRESS) for (c = 0; c < WK_NCODEC; c++) any |= r->comp[c].state != WK_ST_NOTRUN;
        else for (t = 0; t < WT_N; t++) any |= wk_test_group(t) == g && r->t[t].state != WK_ST_NOTRUN;
        if (!any) continue;
        so_par(o);
        so_f(o, C_TEAL, "%s (%s)", wk_group_name[g], wk_unit[g]);
        so_par(o);
        if (g == WK_COMPRESS) {
            for (c = 0; c < WK_NCODEC; c++) {
                char n1[48], n2[48], ra[48];
                lib = wk_codec_lib(c);
                if (r->comp[c].state == WK_ST_NOTRUN) continue;
                snprintf(n1, sizeof n1, "%s compress", wk_codec_name[c]);
                snprintf(n2, sizeof n2, "%s decompress", wk_codec_name[c]);
                if (!r->libver[lib][0]) {
                    so_row(o, 4, (const char *const[]){ n1, "not installed", "", "" }, (const int[]){ C_VALUE, C_GRAY, C_GRAY, C_GRAY });
                    continue;
                }
                ra[0] = 0;
                if (r->ratio[c][WK_PART_MIX] > 0) snprintf(ra, sizeof ra, "%.2f times smaller", r->ratio[c][WK_PART_MIX]);
                render_cell(o, r, n1, &r->comp[c], "MB/s", ra);
                if (r->dec[c].state != WK_ST_NOTRUN) render_cell(o, r, n2, &r->dec[c], "MB/s", NULL);
            }
        } else
            for (t = 0; t < WT_N; t++) if (wk_test_group(t) == g && r->t[t].state != WK_ST_NOTRUN) render_cell(o, r, wk_test_name[t], &r->t[t], wk_unit[g], NULL);
    }
    for (c = 0; c < WK_NCODEC && r->ratio[c][WK_PART_MIX] <= 0; c++) ;
    if (c < WK_NCODEC) {
        so_par(o);
        so_f(o, C_TEAL, "How much smaller the data gets (original size / compressed size)");
        so_par(o);
        so_tabs(o, tabs2, 5);
        so_row(o, 5, (const char *const[]){ "Codec", "Text", "Records", "Random", "All" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
        for (c = 0; c < WK_NCODEC; c++) {
            char v[WK_NPART][24];
            int k;
            if (r->ratio[c][WK_PART_MIX] <= 0) continue;
            for (k = 0; k < WK_NPART; k++) snprintf(v[k], sizeof v[k], "%.2f", r->ratio[c][k]);
            so_row(o, 5, (const char *const[]){ wk_codec_name[c], v[0], v[1], v[2], v[3] }, (const int[]){ C_VALUE, C_BLACK, C_BLACK, C_BLACK, C_GREEN });
        }
    }
    so_par(o);
    so_tabs(o, tabs3, 1);
    if (r->cancelled) so_kv(o, "Status", C_ORANGE, "Stopped before the end - the missing tests were not measured");
    if (r->run_mask & 1u << WK_COMPRESS)
        for (lib = 0; lib < WK_NLIB; lib++) {
            char k[32];
            snprintf(k, sizeof k, "Library: %s", wk_lib_id[lib]);
            if (r->libver[lib][0]) so_kv(o, k, C_VALUE, r->libver[lib]);
            else so_kvf(o, k, C_GRAY, "not installed (%s)", r->libwhy[lib]);
        }
    so_kv(o, "Self-test", r->self_ok ? C_GREEN : C_RED, r->self);
    if (r->failed) so_kvf(o, "Check", C_RED, "%d result%s failed its check - see the red rows", r->failed, r->failed == 1 ? "" : "s");
    else so_kv(o, "Check", C_GREEN, "every result was checked (round trip, order, known value or count) and is right");
    so_kvf(o, "Test data", C_VALUE, "%ld KB each of text, records and random bytes, generated from a fixed seed", r->part_kib);
    so_kvf(o, "Run", C_GRAY, "%s, %.1f s per measurement", r->when, r->seconds);
    so_kv(o, "Build", C_GRAY, bench_build_tag());
    so_note(o, "1 MB = 1,000,000 bytes, 1 GB = 1,000,000,000 bytes. Compression speeds count the original (uncompressed) bytes. "
               "Mkeys/s = million items sorted per second; Mops/s = million interpreter instructions per second. "
               "Each thread works on its own buffers; the threads column adds them up. Higher is better.");
    so_board_footer(o);
}

/* ======================================================================
 * the benchmark
 * ====================================================================== */
static wk_result g_res;
static int g_have_res;
static atomic_int g_running;

const wk_result *wk_last(void) { return g_have_res ? &g_res : NULL; }

double wk_estimate_seconds(const wk_cfg *cfg)
{
    static const double mbs[WK_NCODEC] = { 60, 25, 10, 300, 200, 4, 500, 12, 3 };   /* rough 1-thread compress speeds */
    wk_cfg c = *cfg;
    double secs, s = 1, mb, cols;
    int i;
    if (!c.run_mask) c.run_mask = (1u << WK_NGROUP) - 1;
    if (!c.codec_mask) c.codec_mask = (1u << WK_NLIB) - 1;
    secs = c.seconds < 0.1 ? 0.5 : c.seconds;
    mb = (c.part_kib < 64 ? 1024 : c.part_kib) * 3 / 1024.0 * 1.048576;
    cols = c.single_only ? 1 : 2;
    if (c.run_mask & 1u << WK_COMPRESS)
        for (i = 0; i < WK_NCODEC; i++) {
            double pass = mb / mbs[i];
            if (!(c.codec_mask & 1u << wk_codec_lib(i))) continue;
            s += pass + cols * (secs > pass ? secs : pass) * 1.3 + cols * secs * 1.2;
        }
    for (i = 0; i < WT_N; i++) if (c.run_mask & 1u << wk_test_group(i)) s += cols * secs * 1.2;
    return s;
}

static void hist_add(wctx *x, int g)
{
    wk_result *r = x->r;
    char params[160], res[600];
    size_t l = 0;
    int i;
    res[0] = 0;
    snprintf(params, sizeof params, "threads=%d seconds=%.1f part=%ldKB%s", r->threads, r->seconds, r->part_kib, r->single_only ? " single" : "");
#define ADD(id, cell) do { if ((cell).state == WK_ST_OK && l + 40 < sizeof res) \
        l += (size_t)snprintf(res + l, sizeof res - l, "%s%s %.4g/%.4g", l ? ", " : "", id, (cell).v[0], (cell).v[1]); } while (0)
    if (g == WK_COMPRESS)
        for (i = 0; i < WK_NCODEC; i++) {
            char id[32];
            snprintf(id, sizeof id, "%s-c", codec_id[i]); ADD(id, r->comp[i]);
            snprintf(id, sizeof id, "%s-d", codec_id[i]); ADD(id, r->dec[i]);
        }
    else for (i = 0; i < WT_N; i++) if (wk_test_group(i) == g) ADD(test_id[i], r->t[i]);
#undef ADD
    if (!l) l = (size_t)snprintf(res, sizeof res, "nothing measured");
    snprintf(res + l, sizeof res - l, " %s%s%s", wk_unit[g], r->failed ? " CHECK-FAILED" : "", r->cancelled ? " STOPPED" : "");
    {
        char test[32];
        snprintf(test, sizeof test, "work-%s", wk_group_id[g]);
        bench_history_add(test, params, res);
    }
}

int bench_work(const wk_cfg *cfg, srs_out *o, volatile int *cancel)
{
    static const double scale[WK_NGROUP] = { 1e6, 1e6, 1e9, 1e9, 1e6 };
    wctx *x;
    wk_result *r = &g_res;
    time_t now = time(NULL);
    cpu_set_t cs;
    int i, g, expected = 0, rc;
    if (burn_active()) { if (o) so_err(o, "Everyday work: not started - CPU Burnin / Stress is running"); return -1; }
    if (!atomic_compare_exchange_strong(&g_running, &expected, 1)) {
        if (o) so_err(o, "Everyday work: not started - it is already running");
        return -1;
    }
    if (!(x = calloc(1, sizeof *x))) { atomic_store(&g_running, 0); return -1; }
    x->c = *cfg;
    x->cancel = cancel;
    x->r = r;
    memset(r, 0, sizeof *r);
    g_have_res = 1;
    if (x->c.seconds <= 0) x->c.seconds = 0.5;
    if (x->c.seconds < 0.1) x->c.seconds = 0.1;
    if (x->c.seconds > 60) x->c.seconds = 60;
    if (x->c.part_kib <= 0) x->c.part_kib = 1024;
    if (x->c.part_kib < 64) x->c.part_kib = 64;
    if (x->c.part_kib > 16384) x->c.part_kib = 16384;
    if (!x->c.run_mask) x->c.run_mask = (1u << WK_NGROUP) - 1;
    if (!x->c.codec_mask) x->c.codec_mask = (1u << WK_NLIB) - 1;
    sched_getaffinity(0, sizeof cs, &cs);
    for (i = 0; i < BURN_MAXCPU && i < CPU_SETSIZE; i++) if (CPU_ISSET(i, &cs)) x->cpus[x->ncpus++] = i;
    if (!x->ncpus) x->cpus[x->ncpus++] = 0;
    if (x->c.threads <= 0 || x->c.threads > x->ncpus) x->c.threads = x->ncpus;
    x->part = (size_t)x->c.part_kib * 1024;
    r->threads = x->c.threads;
    r->single_only = x->c.single_only;
    r->seconds = x->c.seconds;
    r->part_kib = x->c.part_kib;
    r->run_mask = x->c.run_mask;
    r->codec_mask = x->c.codec_mask;
    strftime(r->when, sizeof r->when, "%Y-%m-%d %H:%M:%S", localtime(&now));
    g_stage = "Starting: self-test";
    r->self_ok = wk_selftest(r->self, sizeof r->self);
    for (i = 0; i < WK_NLIB; i++) {
        snprintf(r->libver[i], sizeof r->libver[i], "%s", L.ver[i]);
        snprintf(r->libwhy[i], sizeof r->libwhy[i], "%s", L.h[i] ? "" : L.why[i]);
    }
    g_stage = "Starting: making the test data";
    if (!r->self_ok) rc = 1;
    else if (!prepare(x)) {
        unprepare(x);
        free(x);
        g_stage = "";
        atomic_store(&g_running, 0);
        if (o) so_err(o, "Everyday work: not enough memory for the test data");
        return -1;
    } else {
        for (g = 0; g < WK_NGROUP && !stopped(x); g++) {
            if (!(x->c.run_mask & 1u << g)) continue;
            if (g == WK_COMPRESS) {
                for (i = 0; i < WK_NCODEC && !stopped(x); i++) if (x->c.codec_mask & 1u << wk_codec_lib(i)) run_codec(x, i);
                continue;
            }
            for (i = 0; i < WT_N && !stopped(x); i++) {
                if (wk_test_group(i) != g) continue;
                if ((i == WT_CRC32C_HW && !have_crc32c_hw()) || (i == WT_VM_THREADED && !HAVE_THREADED)) {
                    r->t[i].state = WK_ST_SKIP;
                    snprintf(r->t[i].note, sizeof r->t[i].note, i == WT_VM_THREADED ? "needs a GCC or Clang build" : "the CPU has no CRC32C instruction (SSE4.2)");
                    continue;
                }
                run_cell(x, &r->t[i], JK_TEST, i, wk_group_name[g], wk_test_name[i], scale[g]);
            }
        }
        rc = r->failed ? 1 : r->cancelled ? -2 : 0;
    }
    stopped(x);
    if (r->cancelled && rc == 0) rc = -2;
    g_stage = "";
    bench_save_render("work", wk_render, r, o);
    for (g = 0; g < WK_NGROUP; g++) if (x->c.run_mask & 1u << g) hist_add(x, g);
    unprepare(x);
    free(x);
    atomic_store(&g_running, 0);
    return rc;
}

#else   /* not Linux */
const char *wk_stage(void) { return ""; }
int wk_available(void) { return 0; }
const wk_result *wk_last(void) { return NULL; }
double wk_estimate_seconds(const wk_cfg *c) { (void)c; return 0; }
int wk_selftest(char *msg, size_t mn) { snprintf(msg, mn, "Linux only"); return 0; }
int bench_work(const wk_cfg *c, srs_out *o, volatile int *cancel)
{
    (void)c; (void)cancel;
    if (o) so_err(o, "Everyday work: not available on this system (Linux only)");
    return -1;
}
#endif
