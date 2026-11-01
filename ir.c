/* ir.c - IR Blaster: ELK Smart / OCRUSTAR USB infrared dongles (045c:0132 / 0184 / 0195 / 0134).
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later.
 * The codes file, the wording and the behaviour follow IR Remote (Copyright (C) 2026 Eugene Flanagin, MIT).
 *
 * The device protocol below ("elksmart-comm protocol") is a port of elksmart-comm.c from usb-drivers
 * (https://git.janouch.name/p/usb-drivers, commit d48653a), under its own licence:
 *
 *   elksmart-comm.c: ELK Smart infrared dongle tool (for EKX4S and EKX5S-T)
 *
 *   Copyright (c) 2024, Přemysl Eric Janouch <p@janouch.name>
 *
 *   Permission to use, copy, modify, and/or distribute this software for any
 *   purpose with or without fee is hereby granted.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *   WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *   MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 *   SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 *   OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 *   CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Changes made for SRS (marked "SRS:" where they touch the protocol code):
 *   - libusb replaced by the kernel's usbfs ioctls (USBDEVFS_CLAIMINTERFACE / BULK / RELEASEINTERFACE /
 *     RESET) on /dev/bus/usb/BBB/DDD; the interface and its endpoints are read from sysfs. No libusb needed.
 *   - liberty's struct str replaced by a small byte buffer; the VLA in compress_pulses() by malloc.
 *   - the 045c:0132 learn acknowledgement patch (IR Remote's 0001-learn-skip-0132-ack): that dongle answers
 *     the learn command with ff ff ff ff before any infrared data; it is skipped instead of failing.
 *   - learning can be cancelled and has a deadline (upstream waits for ever); a dongle left in learn mode is
 *     reset and asked to identify again, as IR Remote does.
 *   - output goes to message buffers instead of stdout/stderr; the 045c:0134 ID is accepted as well.
 * The packets SRS writes are checked byte for byte against /usr/bin/elksmart-comm by tests/ir-test.sh. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "ir.h"
#include "sec.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/usbdevice_fs.h>
#endif

static const unsigned g_products[] = { 0x0132, 0x0184, 0x0195, 0x0134 };

/* ======================================================================
 * small helpers
 * ====================================================================== */
typedef struct { uint8_t *p; size_t len, cap; int oom; } ibuf;

static void ib_put(ibuf *b, const void *d, size_t n)
{
    if (b->oom) return;
    if (b->len + n > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 256;
        uint8_t *np;
        while (nc < b->len + n) nc *= 2;
        if (!(np = realloc(b->p, nc))) { b->oom = 1; return; }
        b->p = np;
        b->cap = nc;
    }
    if (n) memcpy(b->p + b->len, d, n);
    b->len += n;
}
static void ib_u8(ibuf *b, uint8_t x) { ib_put(b, &x, 1); }
static void ib_u16(ibuf *b, unsigned x) { uint8_t t[2] = { (uint8_t)(x >> 8), (uint8_t)x }; ib_put(b, t, 2); }
static void ib_free(ibuf *b) { free(b->p); memset(b, 0, sizeof *b); }

static void wait_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000 * 1000 };
    while (nanosleep(&ts, &ts) && errno == EINTR) {}
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int rdline(const char *path, char *b, size_t n)
{
    FILE *f = fopen(path, "r");
    size_t l;
    b[0] = 0;
    if (!f) return 0;
    if (!fgets(b, (int)n, f)) b[0] = 0;
    fclose(f);
    l = strlen(b);
    while (l && (b[l - 1] == '\n' || b[l - 1] == '\r' || b[l - 1] == ' ')) b[--l] = 0;
    return 1;
}

static const char *test_mode(void) { const char *t = getenv("SRS_TEST_IR"); return t && *t ? t : NULL; }

static void hexs(char *out, size_t on, const uint8_t *b, size_t n)
{
    size_t i;
    if (!on) return;
    out[0] = 0;
    for (i = 0; i < n && 2 * i + 2 < on; i++) snprintf(out + 2 * i, 3, "%02x", b[i]);
}

/* ======================================================================
 * detection (read-only: sysfs)
 * ====================================================================== */
const char *ir_absent_text(void) { return "No ELK Smart / OCRUSTAR IR blaster found - plug one in."; }

static int name_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

int ir_detect(ir_dongle *d)
{
    const char *t = test_mode(), *root = getenv("SRS_TEST_IR_SYSFS");
    DIR *dir;
    struct dirent *e;
    char **names = NULL;
    int n = 0, cap = 0, i;
    memset(d, 0, sizeof *d);
    d->state = IR_ABSENT;
    if (t && (!strcmp(t, "fake") || !strcmp(t, "noperm"))) {
        d->fake = 1;
        d->product = 0x0132;
        d->state = strcmp(t, "fake") ? IR_NOPERM : IR_READY;
        snprintf(d->node, sizeof d->node, "(test device)");
        snprintf(d->sysdir, sizeof d->sysdir, "(SRS_TEST_IR=%s)", t);
        snprintf(d->name, sizeof d->name, "Test IR Blaster (SRS_TEST_IR=%s)", t);
        return d->state;
    }
    if (t && !strcmp(t, "absent")) return d->state;
    if (!root || !*root) root = "/sys/bus/usb/devices";
    if (!(dir = opendir(root))) return d->state;
    while ((e = readdir(dir))) {
        if (e->d_name[0] == '.' || strchr(e->d_name, ':')) continue;
        if (n == cap) {
            char **nn = realloc(names, sizeof *names * (size_t)(cap = cap ? cap * 2 : 32));
            if (!nn) break;
            names = nn;
        }
        if (!(names[n] = strdup(e->d_name))) break;
        n++;
    }
    closedir(dir);
    if (n) qsort(names, (size_t)n, sizeof *names, name_cmp);
    for (i = 0; i < n && d->state == IR_ABSENT; i++) {
        char p[600], v[64], m[80] = "", pr[80] = "";
        unsigned prod;
        long bus, dev;
        size_t k;
        int ok = 0;
        snprintf(p, sizeof p, "%s/%s/idVendor", root, names[i]);
        if (!rdline(p, v, sizeof v) || strtoul(v, NULL, 16) != IR_VENDOR) continue;
        snprintf(p, sizeof p, "%s/%s/idProduct", root, names[i]);
        if (!rdline(p, v, sizeof v)) continue;
        prod = (unsigned)strtoul(v, NULL, 16);
        for (k = 0; k < sizeof g_products / sizeof g_products[0]; k++) if (g_products[k] == prod) ok = 1;
        if (!ok) continue;
        snprintf(p, sizeof p, "%s/%s/busnum", root, names[i]);
        if (!rdline(p, v, sizeof v)) continue;
        bus = strtol(v, NULL, 10);
        snprintf(p, sizeof p, "%s/%s/devnum", root, names[i]);
        if (!rdline(p, v, sizeof v)) continue;
        dev = strtol(v, NULL, 10);
        snprintf(p, sizeof p, "%s/%s/manufacturer", root, names[i]);
        rdline(p, m, sizeof m);
        snprintf(p, sizeof p, "%s/%s/product", root, names[i]);
        rdline(p, pr, sizeof pr);
        d->product = prod;
        snprintf(d->node, sizeof d->node, "/dev/bus/usb/%03ld/%03ld", bus, dev);
        snprintf(d->sysdir, sizeof d->sysdir, "%s/%s", root, names[i]);
        snprintf(d->name, sizeof d->name, "%s%s%s", m, *m && *pr ? " " : "", *m || *pr ? pr : "ELK Smart IR blaster");
        d->state = access(d->node, R_OK | W_OK) == 0 ? IR_READY : IR_NOPERM;
        d->nonode = d->state == IR_NOPERM && errno == ENOENT;   /* no usbfs node at all: not a permission problem */
    }
    for (i = 0; i < n; i++) free(names[i]);
    free(names);
    return d->state;
}

/* where udev reads rules from, the overriding ones first (a file in /etc or /run replaces the package's one of the same
   name); SRS_TEST_IR_RULES (folders, colon-separated) replaces the list for the tests */
const char *ir_udev_rule_found(char *buf, size_t n)
{
    static const char *const dirs[] = { "/etc/udev/rules.d", "/run/udev/rules.d", "/usr/local/lib/udev/rules.d", "/usr/lib/udev/rules.d",
                                        "/lib/udev/rules.d", NULL };
    const char *t = getenv("SRS_TEST_IR_RULES");
    char list[2048], *dir, *save = NULL;
    int i;
    if (!t || !*t) {
        list[0] = 0;
        for (i = 0; dirs[i]; i++) { if (i) strncat(list, ":", sizeof list - strlen(list) - 1); strncat(list, dirs[i], sizeof list - strlen(list) - 1); }
    } else snprintf(list, sizeof list, "%s", t);
    for (dir = strtok_r(list, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        snprintf(buf, n, "%s/" IR_UDEV_NAME, dir);
        if (access(buf, F_OK) == 0) return buf;
    }
    if (n) buf[0] = 0;
    return NULL;
}

void ir_status_text(const ir_dongle *d, char *buf, size_t n)
{
    char rule[600];
    if (d->state == IR_READY)
        snprintf(buf, n, "Dongle ready: %s (%04x:%04x)", d->name, IR_VENDOR, d->product);
    else if (d->state == IR_NOPERM && d->nonode)
        snprintf(buf, n, "Dongle found (%s, %04x:%04x), but its device file %s is missing (is usbfs / devtmpfs mounted?). "
                         "Unplug and replug the dongle.", d->name, IR_VENDOR, d->product, d->node);
    else if (d->state == IR_NOPERM && ir_udev_rule_found(rule, sizeof rule))
        snprintf(buf, n, "Dongle found (%s, %04x:%04x), but you don't have permission to use it yet. The udev rule is installed "
                         "(%s): unplug and replug the dongle so it applies.", d->name, IR_VENDOR, d->product, rule);
    else if (d->state == IR_NOPERM)
        snprintf(buf, n, "Dongle found (%s, %04x:%04x), but you don't have permission to use it. Install the udev rule (the package's "
                         "%s from elksmart-comm-git or IR Remote, or your own copy in /etc/udev/rules.d), then unplug and replug the dongle.",
                 d->name, IR_VENDOR, d->product, IR_UDEV_RULE);
    else snprintf(buf, n, "%s Supported: %s.", ir_absent_text(), IR_SUPPORTED_IDS);
}

/* ======================================================================
 * elksmart-comm protocol: coding (verbatim port, see the header)
 * ====================================================================== */
typedef struct { unsigned on, off; } pulse;   /* microseconds */

static int pulse_equal(pulse a, pulse b) { return a.on == b.on && a.off == b.off; }

static int read_hex(const char *s, ibuf *out)
{
    static const char *alphabet = "0123456789abcdef";
    out->len = 0;
    for (;;) {
        const char *hi, *lo;
        while (*s && strchr(" \t\n\r\v\f", *s)) s++;
        if (!*s) return 1;
        if (!(hi = strchr(alphabet, tolower((unsigned char)*s++))) || !*s || !*hi ||
            !(lo = strchr(alphabet, tolower((unsigned char)*s++))) || !*lo)
            return 0;
        ib_u8(out, (uint8_t)((hi - alphabet) << 4 | (lo - alphabet)));
    }
}

static size_t decode_learned_direct(const uint8_t *b, size_t b_len, pulse *pulses)
{
    size_t pulses_len = 0, i;
    for (i = 0; i < b_len;) {
        pulse *p = &pulses[pulses_len++];
        while (b[i] == 0xff) {
            p->on += 4080;
            if (++i == b_len) return 0;
        }
        p->on += b[i++] * 16;
        if (i == b_len) break;   /* who cares, presumably it stays off */
        while (b[i] == 0xff) {
            p->off += 4080;
            if (++i == b_len) return 0;
        }
        p->off += b[i++] * 16;
    }
    return pulses_len;
}

static pulse *decode_learned(const ibuf *code, size_t *len, const char **err)
{
    pulse *pulses = calloc(code->len ? code->len : 1, sizeof *pulses);   /* this conveniently has an upper bound */
    if (!pulses) { *err = "out of memory"; return NULL; }
    if (!(*len = decode_learned_direct(code->p, code->len, pulses))) {
        *err = "code ends unexpectedly";
        free(pulses);
        return NULL;
    }
    return pulses;
}

static pulse *encode_nec_byte(pulse *p, uint8_t byte)
{
    int i;
    for (i = 7; i >= 0; i--) { p->on = 550; p->off = ((byte >> i) & 1) ? 1650 : 550; p++; }
    return p;
}

static pulse *encode_nec(const ibuf *code, size_t *len, const char **err)
{
    pulse *pulses, *p;
    size_t i;
    if (code->len % 2) { *err = "NEC transmission format requires pairs"; return NULL; }
    *len = code->len / 2 * (1 /* leader */ + 32 + 1 /* stop */);   /* the timings seem to be rather tolerant */
    if (!(pulses = calloc(*len ? *len : 1, sizeof *pulses))) { *err = "out of memory"; return NULL; }
    p = pulses;
    for (i = 0; i < code->len; i += 2) {
        p->on = 8500; p->off = 4250; p++;
        p = encode_nec_byte(p, code->p[i + 0]);
        p = encode_nec_byte(p, (uint8_t)~code->p[i + 0]);
        p = encode_nec_byte(p, code->p[i + 1]);
        p = encode_nec_byte(p, (uint8_t)~code->p[i + 1]);
        p->on = 550; p->off = 25000; p++;
    }
    return pulses;
}

static void compress_value(unsigned value, ibuf *enc)
{
    if (value <= 2032) {
        uint8_t v = (uint8_t)(value / 16. + .5);   /* "we fix a minor problem in the original Ocrustar algorithm" */
        ib_u8(enc, v > 2 ? v : 2);
    } else {
        do {
            uint8_t v = value & 0x7f;
            if ((value >>= 7)) v |= 0x80;
            ib_u8(enc, v);
        } while (value);
    }
}

static int compress_pulses(const pulse *pulses, size_t len, ibuf *enc)
{
    unsigned *counts = calloc(len ? len : 1, sizeof *counts);   /* SRS: malloc instead of a VLA */
    size_t i, k, top1 = 0, top2;
    pulse p1, p2;
    if (!counts) return 0;
    for (i = 0; i < len; i++)
        for (k = 0; k < len; k++)
            if (pulse_equal(pulses[i], pulses[k])) counts[i]++;
    for (i = 0; i < len; i++)
        if (counts[i] > counts[top1]) top1 = i;
    top2 = top1;
    for (i = 0; i < len; i++)
        if (!pulse_equal(pulses[i], pulses[top1]) && (top2 == top1 || counts[i] > counts[top2])) top2 = i;
    free(counts);
    p1 = pulses[top1];
    p2 = pulses[top2];
    compress_value(p2.on, enc);
    compress_value(p2.off, enc);
    compress_value(p1.on, enc);
    compress_value(p1.off, enc);
    ib_u8(enc, 0xff);
    ib_u8(enc, 0xff);
    ib_u8(enc, 0xff);
    for (i = 0; i < len; i++) {
        if (pulse_equal(pulses[i], p1)) ib_u8(enc, 0);
        else if (pulse_equal(pulses[i], p2)) ib_u8(enc, 1);
        else { compress_value(pulses[i].on, enc); compress_value(pulses[i].off, enc); }
    }
    return 1;
}

/* Huffman ("LLM-reconstructed from the Ocrustar application, untested" upstream) */
typedef struct hnode { struct hnode *parent; unsigned frequency; int bit; } hnode;

static void huffman_push(hnode **heap, size_t *len, hnode *node)
{
    size_t child = (*len)++;   /* equal frequencies must behave like Java's PriorityQueue */
    while (child) {
        size_t parent = (child - 1) / 2;
        if (node->frequency >= heap[parent]->frequency) break;
        heap[child] = heap[parent];
        child = parent;
    }
    heap[child] = node;
}

static hnode *huffman_pop(hnode **heap, size_t *len)
{
    hnode *result = heap[0], *node = heap[--*len];
    size_t parent = 0;
    if (!*len) return result;
    while (parent < *len / 2) {
        size_t child = parent * 2 + 1;
        if (child + 1 < *len && heap[child + 1]->frequency < heap[child]->frequency) child++;
        if (heap[child]->frequency >= node->frequency) break;
        heap[parent] = heap[child];
        parent = child;
    }
    heap[parent] = node;
    return result;
}

static void huffman_encode(const ibuf *input, ibuf *enc)
{
    unsigned frequencies[256];
    hnode nodes[511], *heap[256], *leaves[256];
    size_t nodes_len = 0, heap_len = 0, i, padding, bit = 0;
    uint8_t bits[256], byte = 0;
    memset(frequencies, 0, sizeof frequencies);
    memset(nodes, 0, sizeof nodes);
    memset(leaves, 0, sizeof leaves);
    for (i = 0; i < input->len; i++) frequencies[input->p[i]]++;
    for (i = 0; i < 256; i++) {
        hnode *node;
        if (!frequencies[i]) continue;
        node = leaves[i] = &nodes[nodes_len++];
        node->frequency = frequencies[i];
        huffman_push(heap, &heap_len, node);
    }
    ib_u16(enc, (unsigned)heap_len);
    for (i = 0; i < 256; i++) {
        if (!frequencies[i]) continue;
        ib_u8(enc, (uint8_t)i);
        ib_u16(enc, frequencies[i]);
    }
    while (heap_len > 1) {
        hnode *left = huffman_pop(heap, &heap_len), *right = huffman_pop(heap, &heap_len), *node = &nodes[nodes_len++];
        node->frequency = left->frequency + right->frequency;
        left->parent = right->parent = node;
        left->bit = 0;
        right->bit = 1;
        huffman_push(heap, &heap_len, node);
    }
    padding = enc->len;   /* the padding count precedes the data, so reserve it for now */
    ib_u8(enc, 0);
    for (i = 0; i < input->len; i++) {
        size_t len = 0;
        hnode *node;
        for (node = leaves[input->p[i]]; node->parent; node = node->parent) bits[len++] = (uint8_t)node->bit;
        while (len) {
            byte = (uint8_t)(byte << 1 | bits[--len]);
            if (++bit == 8) { ib_u8(enc, byte); byte = 0; bit = 0; }
        }
    }
    if (bit) {
        if (!enc->oom) enc->p[padding] = (uint8_t)(8 - bit);
        ib_u8(enc, (uint8_t)(byte << (8 - bit)));
    }
}

static uint8_t mangle(uint8_t value)
{
    uint8_t reversed = 0;
    int i;
    for (i = 0; i < 8; i++) { reversed = (uint8_t)((reversed << 1) | (value & 1)); value >>= 1; }
    return (uint8_t)~reversed;
}

static uint8_t checksum(const uint8_t *b, size_t len)
{
    uint32_t sum = 0;
    size_t i;
    for (i = 0; i < len; i++) sum += b[i];
    return mangle((uint8_t)((sum & 0xF0) | ((sum >> 8) & 0x0F)));
}

static const uint8_t c_transmit[] = { 0xff, 0xff, 0xff, 0xff }, c_learn[] = { 0xfe, 0xfe, 0xfe, 0xfe },
                     c_stop[] = { 0xfd, 0xfd, 0xfd, 0xfd }, c_identify[] = { 0xfc, 0xfc, 0xfc, 0xfc };

/* the transmit message for one code (send_transmit's framing, before it is cut into packets) */
static int build_message(const char *hex, int nec, unsigned long frequency, int huffman, ibuf *message, const char **err)
{
    ibuf code = { 0 }, compressed = { 0 };
    pulse *pulses;
    size_t pulses_len = 0;
    int ok = 0;
    if (!read_hex(hex, &code)) { *err = "invalid hex string"; ib_free(&code); return 0; }
    pulses = nec ? encode_nec(&code, &pulses_len, err) : decode_learned(&code, &pulses_len, err);
    ib_free(&code);
    if (!pulses) return 0;
    if (compress_pulses(pulses, pulses_len, &compressed)) {
        if (huffman) {
            ibuf h = { 0 };
            huffman_encode(&compressed, &h);
            ib_free(&compressed);
            compressed = h;
        }
        ib_put(message, c_transmit, sizeof c_transmit);
        frequency += 0x7ffff;
        ib_u8(message, mangle((uint8_t)(frequency >> 8)));
        ib_u8(message, mangle((uint8_t)(frequency >> 16)));
        ib_u8(message, mangle((uint8_t)frequency));
        ib_u8(message, mangle((uint8_t)(compressed.len >> 8)));
        ib_u8(message, mangle((uint8_t)compressed.len));
        ib_put(message, compressed.p, compressed.len);
        ok = !message->oom && !compressed.oom;
        if (!ok) *err = "out of memory";
    } else *err = "out of memory";
    ib_free(&compressed);
    free(pulses);
    return ok;
}

/* the next packet of a message: 62 bytes + checksum, or the shorter rest; 0 at the end */
static size_t next_packet(const ibuf *m, size_t *i, uint8_t *buffer)
{
    size_t chunk = m->len - *i < 62 ? m->len - *i : 62;
    if (!chunk) return 0;
    memcpy(buffer, m->p + *i, chunk);
    *i += chunk;
    if (chunk == 62) buffer[chunk++] = checksum(buffer, 62);
    return chunk;
}

/* "Attempted pulse decode": the first line elksmart-comm prints after a learned code */
static void depulse_hint(const ibuf *code, char *hint, size_t hn)
{
    const char *err;
    size_t len = 0, o = 0;
    pulse *pulses = decode_learned(code, &len, &err), *p, *end;
    if (hn) hint[0] = 0;
    if (!pulses || !hn) { free(pulses); return; }
    p = pulses;
    end = p + len;
    if (p != end && p->on >= 2048 && p->off >= 2048) {
        unsigned bits = 0, nibble = 0;
        for (p++; p != end && !(p->on >= 2048 && p->off >= 2048); p++) {
            nibble = nibble << 1 | (p->off > 2 * p->on);
            if (++bits == 4) {
                if (o + 1 < hn) hint[o++] = "0123456789abcdef"[nibble];
                bits = nibble = 0;
            }
        }
        hint[o] = 0;
    }
    free(pulses);
}

const char *ir_code_problem(const char *hex, int nec)
{
    size_t n = 0;
    const char *s;
    if (!hex) return "The code is empty.";
    if (!strncasecmp(hex, "0x", 2)) hex += 2;
    for (s = hex; *s; s++) {
        if (isspace((unsigned char)*s)) continue;
        if (!isxdigit((unsigned char)*s)) return "Codes may only contain hex digits (0-9, a-f).";
        n++;
    }
    if (!n) return "The code is empty.";
    if (n % 2) return "A hex code needs an even number of digits.";
    if (nec && n % 4) return "NEC codes are address + command pairs: 4 hex digits per press (for example 20df for address 20, command df).";
    return NULL;
}

/* a code as IR Remote stores it: lower case, no spaces, no 0x */
static char *clean_hex(const char *hex)
{
    char *o = malloc(strlen(hex ? hex : "") + 1), *q = o;
    if (!o) return NULL;
    if (hex && !strncasecmp(hex, "0x", 2)) hex += 2;
    for (; hex && *hex; hex++) if (!isspace((unsigned char)*hex)) *q++ = (char)tolower((unsigned char)*hex);
    *q = 0;
    return o;
}

static int g_last_proto = -1;   /* written by a worker thread, read by the window: atomic */
int ir_last_protocol(void) { return __atomic_load_n(&g_last_proto, __ATOMIC_RELAXED); }

char *ir_dry_run(const char *hex, int nec, unsigned long freq, int huffman, char *msg, size_t mn)
{
    ibuf m = { 0 }, out = { 0 };
    const char *err = NULL, *pr = ir_code_problem(hex, nec);
    char *h = NULL;
    uint8_t buffer[64];
    size_t i = 0, len;
    if (huffman < 0) huffman = ir_last_protocol() == 1;
    if (pr) { snprintf(msg, mn, "%s", pr); return NULL; }
    if (!(h = clean_hex(hex)) || !build_message(h, nec, freq ? freq : IR_DEFAULT_FREQ, huffman, &m, &err)) {
        snprintf(msg, mn, "%s", err ? err : "out of memory");
        free(h);
        ib_free(&m);
        return NULL;
    }
    free(h);
    {
        size_t np = 0, nb = 0;
        while ((len = next_packet(&m, &i, buffer))) {
            char line[160];
            memcpy(line, "OUT ", 4);
            hexs(line + 4, sizeof line - 4, buffer, len);
            ib_put(&out, line, strlen(line));
            ib_u8(&out, '\n');
            np++;
            nb += len;
        }
        ib_u8(&out, 0);
        snprintf(msg, mn, "%zu packet%s, %zu bytes, %s protocol - nothing sent", np, np == 1 ? "" : "s", nb, huffman ? "Huffman" : "plain");
    }
    ib_free(&m);
    if (out.oom) { ib_free(&out); snprintf(msg, mn, "out of memory"); return NULL; }
    return (char *)out.p;
}

/* ======================================================================
 * the device: usbfs, or the made-up test device
 * ====================================================================== */
enum { XF_OK = 0, XF_TIMEOUT = 1, XF_ERR = -1 };

typedef struct { uint8_t d[64]; int n, timeout; } fakepkt;

typedef struct {
    int fd, lockfd, fake, claimed, detached;
    uint8_t ep_in, ep_out;
    ir_dongle dg;
    char err[300];
    /* the test device */
    FILE *log;
    fakepkt q[64];
    int qh, qn;
} sess;

static void fake_log(sess *s, const char *what, const uint8_t *b, size_t n)
{
    char line[200];
    if (!s->log) return;
    hexs(line, sizeof line, b, n);
    fprintf(s->log, "%s%s%s\n", what, n ? " " : "", line);
    fflush(s->log);
}

static void fake_push(sess *s, const uint8_t *d, int n, int timeout)
{
    fakepkt *p;
    if (s->qn >= 64) return;
    p = &s->q[(s->qh + s->qn++) % 64];
    p->n = n;
    p->timeout = timeout;
    if (n) memcpy(p->d, d, (size_t)n);
}

/* the canned learned code: NEC-like, address 20 command df (LG power), 9 ms + 4.5 ms leader */
static void fake_learn_code(ibuf *b)
{
    const char *env = getenv("SRS_TEST_IR_LEARN");
    static const uint8_t lead[] = { 0xff, 0xff, 0x35, 0xff, 0x1a };
    uint32_t v = 0x20df10efu;
    int i;
    if (env && *env && strcmp(env, "never")) { read_hex(env, b); return; }
    ib_put(b, lead, sizeof lead);
    for (i = 31; i >= 0; i--) { ib_u8(b, 0x23); ib_u8(b, ((v >> i) & 1) ? 0x69 : 0x23); }
    ib_u8(b, 0x23);
}

static void fake_out(sess *s, const uint8_t *b, int n)
{
    fake_log(s, "OUT", b, (size_t)n);
    if (n == 4 && !memcmp(b, c_identify, 4)) {
        const char *id = getenv("SRS_TEST_IR_IDENT"), *pr = getenv("SRS_TEST_IR_PROTO");
        uint8_t r[6] = { 0xfc, 0xfc, 0xfc, 0xfc, 0x70, 0x01 };
        if (pr && !strcmp(pr, "huffman")) { r[4] = 0x02; r[5] = 0xaa; }
        if (!id || strcmp(id, "none")) fake_push(s, r, 6, 0);
    } else if (n == 4 && !memcmp(b, c_learn, 4)) {
        const char *env = getenv("SRS_TEST_IR_LEARN");
        static const uint8_t ack[] = { 0xff, 0xff, 0xff, 0xff };
        ibuf code = { 0 };
        uint8_t first[64];
        size_t at, k;
        fake_push(s, ack, 4, 0);                 /* the 045c:0132 acknowledgement */
        if (env && !strcmp(env, "never")) return;
        fake_push(s, NULL, 0, 1);                /* a pause while the button is pressed */
        fake_learn_code(&code);
        memcpy(first, c_learn, 4);
        {
            size_t ann = code.len + (getenv("SRS_TEST_IR_SHORT") ? 20 : 0);   /* announce more than it sends */
            first[4] = (uint8_t)(ann >> 8);
            first[5] = (uint8_t)ann;
        }
        at = code.len < 58 ? code.len : 58;
        memcpy(first + 6, code.p, at);
        fake_push(s, first, (int)(6 + at), 0);
        fake_push(s, NULL, 0, 1);                /* "the device seems to queue up its output with pauses" */
        while (at < code.len) {
            k = code.len - at < 64 ? code.len - at : 64;
            fake_push(s, code.p + at, (int)k, 0);
            at += k;
        }
        ib_free(&code);
    }
}

static int xfer_out(sess *s, const uint8_t *b, int n, unsigned timeout)
{
    if (s->fake) { fake_out(s, b, n); return XF_OK; }
#ifdef __linux__
    {
        struct usbdevfs_bulktransfer bt;
        int r;
        memset(&bt, 0, sizeof bt);
        bt.ep = s->ep_out;
        bt.len = (unsigned)n;
        bt.timeout = timeout;
        bt.data = (void *)b;
        r = ioctl(s->fd, USBDEVFS_BULK, &bt);
        if (r >= 0) return XF_OK;
        if (errno == ETIMEDOUT) return XF_TIMEOUT;
        snprintf(s->err, sizeof s->err, "send: %s", strerror(errno));
    }
#else
    (void)b; (void)n; (void)timeout;
#endif
    return XF_ERR;
}

static int xfer_in(sess *s, uint8_t *b, int max, int *got, unsigned timeout)
{
    *got = 0;
    if (s->fake) {
        fakepkt *p;
        if (!s->qn && getenv("SRS_TEST_IR_STREAM")) {   /* a dongle that never stops talking */
            static const uint8_t junk[6] = { 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa };
            memcpy(b, junk, (size_t)(max < 6 ? max : 6));
            *got = max < 6 ? max : 6;
            return XF_OK;
        }
        if (!s->qn) { wait_ms(timeout > 20 ? 20 : timeout); return XF_TIMEOUT; }
        p = &s->q[s->qh];
        s->qh = (s->qh + 1) % 64;
        s->qn--;
        if (p->timeout) { wait_ms(timeout > 20 ? 20 : timeout); return XF_TIMEOUT; }
        memcpy(b, p->d, (size_t)(p->n < max ? p->n : max));
        *got = p->n < max ? p->n : max;
        return XF_OK;
    }
#ifdef __linux__
    {
        struct usbdevfs_bulktransfer bt;
        int r;
        memset(&bt, 0, sizeof bt);
        bt.ep = s->ep_in;
        bt.len = (unsigned)max;
        bt.timeout = timeout;
        bt.data = b;
        r = ioctl(s->fd, USBDEVFS_BULK, &bt);
        if (r >= 0) { *got = r; return XF_OK; }
        if (errno == ETIMEDOUT) return XF_TIMEOUT;
        snprintf(s->err, sizeof s->err, "receive: %s", strerror(errno));
    }
#else
    (void)b; (void)max; (void)timeout;
#endif
    return XF_ERR;
}

/* the same lock file as IR Remote: one program at a time talks to the dongle (SRS window, SRS CLI, IR Remote) */
static int lock_take(double timeout_s)
{
    char p[512];
    const char *run = getenv("XDG_RUNTIME_DIR");
    double t0 = now_s();
    int fd;
    snprintf(p, sizeof p, "%s/irremote-%u.lock", run && *run ? run : "/tmp", (unsigned)getuid());
    if ((fd = open(p, O_RDWR | O_CREAT | O_CLOEXEC, 0600)) < 0) return -2;
    while (flock(fd, LOCK_EX | LOCK_NB)) {
        if (now_s() - t0 > timeout_s) { close(fd); return -1; }
        wait_ms(100);
    }
    return fd;
}

/* interface 0 of the active configuration and its two bulk endpoints, from sysfs */
static int find_endpoints(sess *s)
{
    const char *base = strrchr(s->dg.sysdir, '/');
    char p[700], v[64];
    DIR *d;
    struct dirent *e;
    int found = 0;
    base = base ? base + 1 : s->dg.sysdir;
    if (!(d = opendir(s->dg.sysdir))) { snprintf(s->err, sizeof s->err, "cannot read %s", s->dg.sysdir); return 0; }
    while (!found && (e = readdir(d))) {
        char ifd[600];
        DIR *d2;
        struct dirent *e2;
        int have_in = 0, have_out = 0;
        if (strncmp(e->d_name, base, strlen(base)) || e->d_name[strlen(base)] != ':') continue;
        snprintf(ifd, sizeof ifd, "%s/%s", s->dg.sysdir, e->d_name);
        snprintf(p, sizeof p, "%s/bInterfaceNumber", ifd);
        if (!rdline(p, v, sizeof v) || strtoul(v, NULL, 16) != 0) continue;
        snprintf(p, sizeof p, "%s/bInterfaceClass", ifd);
        rdline(p, v, sizeof v);
        if (strtoul(v, NULL, 16) != 0x02 && strtoul(v, NULL, 16) != 0xff) {
            snprintf(s->err, sizeof s->err, "unexpected USB interface class");
            break;
        }
        snprintf(p, sizeof p, "%s/bNumEndpoints", ifd);
        if (!rdline(p, v, sizeof v) || strtoul(v, NULL, 16) != 2) { snprintf(s->err, sizeof s->err, "unexpected endpoint count"); break; }
        if (!(d2 = opendir(ifd))) continue;
        while ((e2 = readdir(d2))) {
            char t[32] = "", dirn[16] = "", a[16] = "";
            if (strncmp(e2->d_name, "ep_", 3)) continue;
            snprintf(p, sizeof p, "%s/%s/type", ifd, e2->d_name); rdline(p, t, sizeof t);
            snprintf(p, sizeof p, "%s/%s/direction", ifd, e2->d_name); rdline(p, dirn, sizeof dirn);
            snprintf(p, sizeof p, "%s/%s/bEndpointAddress", ifd, e2->d_name); rdline(p, a, sizeof a);
            if (strcmp(t, "Bulk")) continue;
            if (!strcmp(dirn, "in")) { s->ep_in = (uint8_t)strtoul(a, NULL, 16); have_in = 1; }
            else if (!strcmp(dirn, "out")) { s->ep_out = (uint8_t)strtoul(a, NULL, 16); have_out = 1; }
        }
        closedir(d2);
        if (!have_in || !have_out) { snprintf(s->err, sizeof s->err, "USB interface is not bidirectional"); break; }
        snprintf(p, sizeof p, "%s/driver", ifd);
#ifdef __linux__
        if (access(p, F_OK) == 0) {   /* a kernel driver holds it: detach (libusb_detach_kernel_driver) */
            struct usbdevfs_ioctl cmd;
            memset(&cmd, 0, sizeof cmd);
            cmd.ifno = 0;
            cmd.ioctl_code = USBDEVFS_DISCONNECT;
            if (ioctl(s->fd, USBDEVFS_IOCTL, &cmd) < 0) { snprintf(s->err, sizeof s->err, "cannot detach kernel driver: %s", strerror(errno)); break; }
            s->detached = 1;   /* given back in sess_close */
        }
#endif
        found = 1;
    }
    closedir(d);
    if (!found && !*s->err) snprintf(s->err, sizeof s->err, "no USB interface 0 found under %s", s->dg.sysdir);
    return found;
}

static void sess_close(sess *s)
{
#ifdef __linux__
    if (s->claimed) { unsigned ifno = 0; ioctl(s->fd, USBDEVFS_RELEASEINTERFACE, &ifno); }
    if (s->detached && s->fd >= 0) {   /* the kernel driver SRS detached gets the interface back (libusb leaves it detached) */
        struct usbdevfs_ioctl cmd;
        memset(&cmd, 0, sizeof cmd);
        cmd.ifno = 0;
        cmd.ioctl_code = USBDEVFS_CONNECT;
        ioctl(s->fd, USBDEVFS_IOCTL, &cmd);
    }
#endif
    s->claimed = s->detached = 0;
    if (s->fd >= 0) close(s->fd);
    s->fd = -1;
    if (s->log) fclose(s->log);
    s->log = NULL;
}

/* open the dongle (the lock must be held); 0 + s->err when it cannot be used */
static int sess_open(sess *s)
{
    s->fd = -1;
    s->qh = s->qn = 0;
    if (ir_detect(&s->dg) != IR_READY) { ir_status_text(&s->dg, s->err, sizeof s->err); return 0; }
    s->fake = s->dg.fake;
    if (s->fake) {
        const char *lp = getenv("SRS_TEST_IR_LOG");
        if (lp && *lp) s->log = fopen(lp, "a");
        return 1;
    }
#ifdef __linux__
    if ((s->fd = open(s->dg.node, O_RDWR | O_CLOEXEC)) < 0) {
        if (errno == EACCES || errno == EPERM) { s->dg.state = IR_NOPERM; ir_status_text(&s->dg, s->err, sizeof s->err); }
        else snprintf(s->err, sizeof s->err, "couldn't open device: %s", strerror(errno));
        return 0;
    }
    if (!find_endpoints(s)) { sess_close(s); return 0; }
    {
        unsigned ifno = 0;
        if (ioctl(s->fd, USBDEVFS_CLAIMINTERFACE, &ifno) < 0) {
            if (errno == EBUSY) snprintf(s->err, sizeof s->err, "The dongle is in use by another program. Try again.");
            else snprintf(s->err, sizeof s->err, "couldn't claim interface: %s", strerror(errno));
            sess_close(s);
            return 0;
        }
        s->claimed = 1;
    }
    return 1;
#else
    snprintf(s->err, sizeof s->err, "USB access needs Linux");
    return 0;
#endif
}

static int classify_device(const uint8_t *b, int len)
{
    if (len == 6 && !memcmp(b, c_identify, 4) && b[4] == 0x02 && b[5] == 0xaa) return 2;   /* Huffman */
    if (len == 6 && !memcmp(b, c_identify, 4) && b[4] == 0x70 && b[5] == 0x01) return 1;   /* plain */
    if (len == 6 && !memcmp(b, "\xfa\xfa\xfa\xfa", 4)) return 1;
    return 0;
}

/* 1 plain, 2 Huffman, 0 = s->err */
static int send_identify(sess *s)
{
    uint8_t buffer[64];
    int len = 0, attempt = 0, r, proto;
    {   /* flush buffers; SRS: bounded (64 reads or 0.2 s), as a dongle that keeps talking would hold a window's worker for ever */
        double t0 = now_s();
        int k = 0;
        while (k++ < 64 && now_s() - t0 < 0.2 && (r = xfer_in(s, buffer, sizeof buffer, &len, 10)) == XF_OK) {}
    }
    for (;;) {
        if ((r = xfer_out(s, c_identify, 4, 100)) != XF_OK) {
            if (r == XF_TIMEOUT) snprintf(s->err, sizeof s->err, "identify/send: Operation timed out");
            else { char t[300]; snprintf(t, sizeof t, "identify/%s", s->err); snprintf(s->err, sizeof s->err, "%s", t); }
            return 0;
        }
        if ((r = xfer_in(s, buffer, sizeof buffer, &len, 500)) == XF_OK) break;
        if (r != XF_TIMEOUT || ++attempt == 4) {
            if (r == XF_TIMEOUT) snprintf(s->err, sizeof s->err, "identify/recv: Operation timed out");
            else { char t[300]; snprintf(t, sizeof t, "identify/%s", s->err); snprintf(s->err, sizeof s->err, "%s", t); }
            return 0;
        }
    }
    if (!(proto = classify_device(buffer, len))) { snprintf(s->err, sizeof s->err, "device busy or not supported"); return 0; }
    __atomic_store_n(&g_last_proto, proto == 2, __ATOMIC_RELAXED);
    return proto;
}

static int send_message(sess *s, const ibuf *m)
{
    uint8_t buffer[64];
    size_t i = 0, chunk;
    while ((chunk = next_packet(m, &i, buffer))) {
        int r = xfer_out(s, buffer, (int)chunk, 100);
        if (r != XF_OK) { if (r == XF_TIMEOUT) snprintf(s->err, sizeof s->err, "send: Operation timed out"); return 0; }
        wait_ms(2);
    }
    return 1;
}

/* the dongle stays stuck in learn mode until it sees IR: USB reset, then ask it to identify (IR Remote's _reset_locked) */
static int reset_locked(char *msg, size_t mn)
{
    int tries;
    for (tries = 0; tries < 5; tries++) {
        sess s;
        memset(&s, 0, sizeof s);
        s.fd = -1;
        if (ir_detect(&s.dg) != IR_READY) { ir_status_text(&s.dg, msg, mn); return 0; }
        if (s.dg.fake) {
            const char *lp = getenv("SRS_TEST_IR_LOG");
            if (lp && *lp && (s.log = fopen(lp, "a"))) fake_log(&s, "RESET", NULL, 0);
            if (s.log) fclose(s.log);
            s.log = NULL;
            wait_ms(50);
        } else {
#ifdef __linux__
            int fd = open(s.dg.node, O_WRONLY | O_CLOEXEC);
            if (fd >= 0) { ioctl(fd, USBDEVFS_RESET, 0); close(fd); }
#endif
            wait_ms(2000);
        }
        if (sess_open(&s)) {
            int ok = send_identify(&s) != 0;
            sess_close(&s);
            if (ok) { snprintf(msg, mn, "Dongle reset and answering again."); return 1; }
        }
    }
    snprintf(msg, mn, "The dongle still isn't answering. Unplug it and plug it back in.");
    return 0;
}

static int wedged(const char *e) { return strstr(e, "identify/recv") || strstr(e, "device busy or not supported"); }

/* friendlier words for the protocol errors (IR Remote's friendly_error) */
static void friendly(const char *e, char *msg, size_t mn)
{
    if (strstr(e, "claim interface") && strstr(e, "busy")) snprintf(msg, mn, "The dongle is in use by another program. Try again.");
    else if (wedged(e)) snprintf(msg, mn, "The dongle isn't answering. Use \"Reset Dongle\", or unplug it and plug it back in.");
    else if (strstr(e, "unexpected response"))
        snprintf(msg, mn, "The dongle sent something we didn't expect instead of a remote code. Try again, holding the remote within 1 cm of the dongle.");
    else if (strstr(e, "invalid hex string")) snprintf(msg, mn, "That code isn't valid hex.");
    else if (strstr(e, "code ends unexpectedly")) snprintf(msg, mn, "That learned code looks cut off. Learn the button again.");
    else if (strstr(e, "requires pairs")) snprintf(msg, mn, "NEC codes need 4 hex digits per press (address + command).");
    else if (strstr(e, "timed out")) snprintf(msg, mn, "The dongle didn't respond in time. Try again, or reset the dongle.");
    else if (strstr(e, "No such device")) snprintf(msg, mn, "The dongle was unplugged.");
    else snprintf(msg, mn, "%s", e);
}

static const char *busy_text = "the dongle is busy with another IR Remote or SRS action";

int ir_probe(char *msg, size_t mn)
{
    sess s;
    int lk = lock_take(3), ok;
    memset(&s, 0, sizeof s);
    if (lk == -1) { snprintf(msg, mn, "The dongle is busy (IR Remote or another SRS action is using it) - it was not asked. Try again in a moment."); return 0; }
    if (!sess_open(&s)) { snprintf(msg, mn, "%s", s.err); if (lk >= 0) close(lk); return 0; }
    ok = send_identify(&s);
    sess_close(&s);
    if (lk >= 0) close(lk);
    if (ok) snprintf(msg, mn, "Dongle is answering (%s protocol).", ok == 2 ? "Huffman" : "plain");
    else snprintf(msg, mn, "Dongle found but not answering - try \"Reset Dongle\".");
    return ok != 0;
}

int ir_reset(char *msg, size_t mn)
{
    int lk = lock_take(15), ok;
    if (lk == -1) { snprintf(msg, mn, "Couldn't reset: %s.", busy_text); return 0; }
    ok = reset_locked(msg, mn);
    if (lk >= 0) close(lk);
    return ok;
}

int ir_send(const char *hex, int nec, unsigned long freq, int repeat, volatile int *cancel, char *msg, size_t mn)
{
    const char *pr = ir_code_problem(hex, nec), *err = NULL;
    char *h;
    int lk, attempt, ok = 0, i;
    if (pr) { snprintf(msg, mn, "%s", pr); return 0; }
    if (!(h = clean_hex(hex))) { snprintf(msg, mn, "out of memory"); return 0; }
    if (repeat < 1) repeat = 1;
    if (!freq) freq = IR_DEFAULT_FREQ;
    if ((lk = lock_take(15)) == -1) { free(h); snprintf(msg, mn, "Couldn't send: %s.", busy_text); return 0; }
    for (attempt = 0; attempt < 2; attempt++) {
        sess s;
        ibuf m = { 0 };
        int proto;
        memset(&s, 0, sizeof s);
        if (!sess_open(&s)) { snprintf(msg, mn, "%s", s.err); break; }
        if (!(proto = send_identify(&s))) {
            char e[300];
            snprintf(e, sizeof e, "%s", s.err);
            sess_close(&s);
            if (attempt == 0 && wedged(e)) { char rm[200]; reset_locked(rm, sizeof rm); continue; }   /* IR Remote's auto reset */
            friendly(e, msg, mn);
            break;
        }
        if (!build_message(h, nec, freq, proto == 2, &m, &err)) {
            sess_close(&s);
            friendly(err ? err : "out of memory", msg, mn);
            ib_free(&m);
            break;
        }
        for (i = 0; i < repeat; i++) {
            if (cancel && *cancel) break;
            if (!send_message(&s, &m)) break;
            wait_ms(100);
        }
        ib_free(&m);
        if (i < repeat && !(cancel && *cancel)) { char e[300]; snprintf(e, sizeof e, "%s", s.err); sess_close(&s); friendly(e, msg, mn); break; }
        sess_close(&s);
        if (cancel && *cancel && i < repeat) snprintf(msg, mn, "Stopped after %d of %d.", i, repeat);
        else if (repeat > 1) snprintf(msg, mn, "Sent %d times", repeat);
        else snprintf(msg, mn, "Sent");
        ok = i > 0;
        break;
    }
    free(h);
    if (lk >= 0) close(lk);
    return ok;
}

/* recv_learn() with the 045c:0132 acknowledgement patch, a deadline and a cancel flag (SRS) */
static int recv_learn(sess *s, ibuf *data, double deadline, volatile int *cancel, int *why, size_t *want)
{
    static const uint8_t c_ack[] = { 0xff, 0xff, 0xff, 0xff };
    uint8_t buffer[64];
    int len = 0, r;
    size_t size;
    *why = 0;
    memset(buffer, 0, sizeof buffer);
    while ((r = xfer_in(s, buffer, sizeof buffer, &len, 100)) != XF_OK || (len == 4 && !memcmp(buffer, c_ack, 4))) {
        /* some dongles (045c:0132) acknowledge learn mode with ff ff ff ff before any infrared data arrives */
        if (cancel && *cancel) { *why = 2; return 0; }
        if (now_s() > deadline) { *why = 1; return 0; }
        if (r == XF_OK) continue;
        if (r != XF_TIMEOUT) { char t[300]; snprintf(t, sizeof t, "learn/%s", s->err); snprintf(s->err, sizeof s->err, "%s", t); return 0; }
    }
    if (len < 6 || memcmp(buffer, c_learn, 4)) { snprintf(s->err, sizeof s->err, "learn/recv: unexpected response"); return 0; }
    /* "this field might only make sense for a later device ... nonetheless, it does match exactly often enough" */
    size = (size_t)buffer[4] << 8 | buffer[5];
    *want = size;
    ib_put(data, buffer + 6, (size_t)len - 6);
    while (data->len < size) {
        if ((r = xfer_in(s, buffer, sizeof buffer, &len, 100)) == XF_OK) { ib_put(data, buffer, (size_t)len); continue; }
        if (r != XF_TIMEOUT) { char t[300]; snprintf(t, sizeof t, "learn/%s", s->err); snprintf(s->err, sizeof s->err, "%s", t); return 0; }
        if (cancel && *cancel) { *why = 2; return 0; }   /* SRS: upstream waits for ever here */
        if (now_s() > deadline) { *why = 1; return 0; }
    }
    if ((r = xfer_out(s, c_stop, 4, 100)) != XF_OK) {   /* "as far as I know, this doesn't do anything" */
        snprintf(s->err, sizeof s->err, "learn/send: %s", r == XF_TIMEOUT ? "Operation timed out" : s->err);
        return 0;
    }
    return !data->oom;
}

int ir_learn(double timeout_s, volatile int *cancel, char *code, size_t cn, char *hint, size_t hn, char *msg, size_t mn)
{
    sess s;
    ibuf data = { 0 };
    int lk, ok = 0, why = 0, r;
    size_t want = 0;
    char e[300] = "";
    if (cn) code[0] = 0;
    if (hn) hint[0] = 0;
    memset(&s, 0, sizeof s);
    if ((lk = lock_take(5)) == -1) { snprintf(msg, mn, "Couldn't start learning: %s.", busy_text); return 0; }
    if (!sess_open(&s)) { snprintf(msg, mn, "%s", s.err); if (lk >= 0) close(lk); return 0; }
    if (!send_identify(&s)) {
        snprintf(e, sizeof e, "%s", s.err);
        sess_close(&s);
        friendly(e, msg, mn);
        if (lk >= 0) close(lk);
        return 0;
    }
    if ((r = xfer_out(&s, c_learn, 4, 100)) != XF_OK) snprintf(e, sizeof e, "learn/send: %s", r == XF_TIMEOUT ? "Operation timed out" : s.err);
    else if (recv_learn(&s, &data, now_s() + (timeout_s > 0 ? timeout_s : 20), cancel, &why, &want)) ok = 1;
    else if (!why) snprintf(e, sizeof e, "%s", s.err);
    sess_close(&s);
    if (ok) {
        char *hx = malloc(data.len * 2 + 1);
        if (hx) { hexs(hx, data.len * 2 + 1, data.p, data.len); snprintf(code, cn, "%s", hx); free(hx); }
        depulse_hint(&data, hint, hn);
        snprintf(msg, mn, "Got it!");
    } else {
        char rm[200];
        reset_locked(rm, sizeof rm);   /* un-stick it */
        if (why == 2) snprintf(msg, mn, "Learning cancelled.");
        else if (why == 1 && data.len)   /* the dongle said how long the code is, then sent less */
            snprintf(msg, mn, "The code came through incomplete (received %zu of the %zu bytes the dongle announced). Try again - "
                              "hold the remote within 1 cm of the dongle and press the button once.", data.len, want);
        else if (why == 1)
            snprintf(msg, mn, "Nothing was received. Hold the remote within 1 cm of the dongle, aim it straight at the tip, "
                              "press the button (hold it for about a second), then try again.");
        else if (strstr(e, "unexpected response"))
            snprintf(msg, mn, "That didn't come through cleanly. Please try again - hold the remote within 1 cm of the dongle "
                              "and press the button once.");
        else friendly(e, msg, mn);
    }
    ib_free(&data);
    if (lk >= 0) close(lk);
    return ok;
}

/* ======================================================================
 * JSON: a small tree that keeps everything it reads (keys in order, number texts as written)
 * ====================================================================== */
enum { JN, JB, JNUM, JS, JA, JO };
typedef struct jv {
    int t;
    char *s;                /* string value, number text, "true"/"false" */
    size_t n, cap;
    struct jv **a;          /* array items / object values */
    char **k;               /* object keys */
} jv;

static void jv_free(jv *v)
{
    size_t i;
    if (!v) return;
    for (i = 0; i < v->n; i++) { jv_free(v->a[i]); if (v->k) free(v->k[i]); }
    free(v->a);
    free(v->k);
    free(v->s);
    free(v);
}

static jv *jv_new(int t, const char *s)
{
    jv *v = calloc(1, sizeof *v);
    if (!v) return NULL;
    v->t = t;
    if (s && !(v->s = strdup(s))) { free(v); return NULL; }
    return v;
}

static int jv_add(jv *o, const char *key, jv *val)
{
    if (!val) return 0;
    if (o->n == o->cap) {
        size_t nc = o->cap ? o->cap * 2 : 8;
        jv **na = realloc(o->a, nc * sizeof *na);
        char **nk;
        if (!na) return 0;
        o->a = na;
        if (o->t == JO) {
            if (!(nk = realloc(o->k, nc * sizeof *nk))) return 0;
            o->k = nk;
        }
        o->cap = nc;
    }
    if (o->t == JO && !(o->k[o->n] = strdup(key ? key : ""))) return 0;
    o->a[o->n++] = val;
    return 1;
}

static jv *jo_get(const jv *o, const char *key)
{
    size_t i;
    if (!o || o->t != JO) return NULL;
    for (i = o->n; i-- > 0;) if (!strcmp(o->k[i], key)) return o->a[i];   /* the last one wins, like Python */
    return NULL;
}

/* replace the value of key in place (keeping the key order), or add it at the end */
static int jo_set(jv *o, const char *key, jv *val)
{
    size_t i;
    if (!val) return 0;
    for (i = o->n; i-- > 0;)
        if (!strcmp(o->k[i], key)) { jv_free(o->a[i]); o->a[i] = val; return 1; }
    return jv_add(o, key, val);
}

static void ja_del(jv *a, size_t i)
{
    if (i >= a->n) return;
    jv_free(a->a[i]);
    if (a->k) free(a->k[i]);
    memmove(a->a + i, a->a + i + 1, (a->n - i - 1) * sizeof *a->a);
    if (a->k) memmove(a->k + i, a->k + i + 1, (a->n - i - 1) * sizeof *a->k);
    a->n--;
}

static jv *jv_clone(const jv *v)
{
    jv *c = jv_new(v->t, v->s);
    size_t i;
    if (!c) return NULL;
    for (i = 0; i < v->n; i++) if (!jv_add(c, v->k ? v->k[i] : NULL, jv_clone(v->a[i]))) { jv_free(c); return NULL; }
    return c;
}

typedef struct { const char *p; char err[160]; int depth; } jpar;

static void jp_ws(jpar *j) { while (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r') j->p++; }

static void utf8_put(ibuf *b, unsigned c)
{
    if (c < 0x80) ib_u8(b, (uint8_t)c);
    else if (c < 0x800) { ib_u8(b, (uint8_t)(0xc0 | c >> 6)); ib_u8(b, (uint8_t)(0x80 | (c & 0x3f))); }
    else if (c < 0x10000) { ib_u8(b, (uint8_t)(0xe0 | c >> 12)); ib_u8(b, (uint8_t)(0x80 | ((c >> 6) & 0x3f))); ib_u8(b, (uint8_t)(0x80 | (c & 0x3f))); }
    else { ib_u8(b, (uint8_t)(0xf0 | c >> 18)); ib_u8(b, (uint8_t)(0x80 | ((c >> 12) & 0x3f))); ib_u8(b, (uint8_t)(0x80 | ((c >> 6) & 0x3f))); ib_u8(b, (uint8_t)(0x80 | (c & 0x3f))); }
}

static int hex4(const char *p, unsigned *v)
{
    int i;
    *v = 0;
    for (i = 0; i < 4; i++) {
        if (!isxdigit((unsigned char)p[i])) return 0;
        *v = *v << 4 | (unsigned)(isdigit((unsigned char)p[i]) ? p[i] - '0' : (tolower((unsigned char)p[i]) - 'a' + 10));
    }
    return 1;
}

static char *jp_string(jpar *j)
{
    ibuf b = { 0 };
    if (*j->p != '"') { snprintf(j->err, sizeof j->err, "expected a string"); return NULL; }
    j->p++;
    while (*j->p != '"') {
        unsigned char c = (unsigned char)*j->p;
        if (!c || c < 0x20) { snprintf(j->err, sizeof j->err, c ? "control character in a string" : "unterminated string"); ib_free(&b); return NULL; }
        if (c != '\\') { ib_u8(&b, c); j->p++; continue; }
        j->p++;
        switch (*j->p) {
        case '"': ib_u8(&b, '"'); break;
        case '\\': ib_u8(&b, '\\'); break;
        case '/': ib_u8(&b, '/'); break;
        case 'b': ib_u8(&b, '\b'); break;
        case 'f': ib_u8(&b, '\f'); break;
        case 'n': ib_u8(&b, '\n'); break;
        case 'r': ib_u8(&b, '\r'); break;
        case 't': ib_u8(&b, '\t'); break;
        case 'u': {
            unsigned u, u2;
            if (!hex4(j->p + 1, &u)) { snprintf(j->err, sizeof j->err, "bad \\u escape"); ib_free(&b); return NULL; }
            j->p += 4;
            if (u >= 0xd800 && u < 0xdc00 && j->p[1] == '\\' && j->p[2] == 'u' && hex4(j->p + 3, &u2) && u2 >= 0xdc00 && u2 < 0xe000) {
                u = 0x10000 + ((u - 0xd800) << 10) + (u2 - 0xdc00);
                j->p += 6;
            }
            utf8_put(&b, u);
            break;
        }
        default: snprintf(j->err, sizeof j->err, "bad escape in a string"); ib_free(&b); return NULL;
        }
        j->p++;
    }
    j->p++;
    ib_u8(&b, 0);
    if (b.oom) { ib_free(&b); snprintf(j->err, sizeof j->err, "out of memory"); return NULL; }
    return (char *)b.p;
}

static jv *jp_value(jpar *j)
{
    jv *v = NULL;
    jp_ws(j);
    if (++j->depth > 200) { snprintf(j->err, sizeof j->err, "nested too deeply"); return NULL; }
    if (*j->p == '{' || *j->p == '[') {
        int obj = *j->p == '{';
        char close = obj ? '}' : ']';
        if (!(v = jv_new(obj ? JO : JA, NULL))) goto oom;
        j->p++;
        jp_ws(j);
        if (*j->p == close) { j->p++; j->depth--; return v; }
        for (;;) {
            char *key = NULL;
            jv *item;
            jp_ws(j);
            if (obj) {
                if (!(key = jp_string(j))) { jv_free(v); return NULL; }
                jp_ws(j);
                if (*j->p != ':') { free(key); jv_free(v); snprintf(j->err, sizeof j->err, "expected ':'"); return NULL; }
                j->p++;
            }
            if (!(item = jp_value(j))) { free(key); jv_free(v); return NULL; }
            if (!jv_add(v, key, item)) { free(key); jv_free(item); jv_free(v); goto oom; }
            free(key);
            jp_ws(j);
            if (*j->p == ',') { j->p++; continue; }
            if (*j->p == close) { j->p++; break; }
            jv_free(v);
            snprintf(j->err, sizeof j->err, "expected ',' or '%c'", close);
            return NULL;
        }
    } else if (*j->p == '"') {
        char *s = jp_string(j);
        if (!s) return NULL;
        if (!(v = jv_new(JS, NULL))) { free(s); goto oom; }
        v->s = s;
    } else if (!strncmp(j->p, "true", 4) || !strncmp(j->p, "false", 5)) {
        int t = *j->p == 't';
        if (!(v = jv_new(JB, t ? "true" : "false"))) goto oom;
        j->p += t ? 4 : 5;
    } else if (!strncmp(j->p, "null", 4)) {
        if (!(v = jv_new(JN, NULL))) goto oom;
        j->p += 4;
    } else if (*j->p == '-' || isdigit((unsigned char)*j->p)) {
        const char *s = j->p;
        char *t;
        if (*j->p == '-') j->p++;
        if (!isdigit((unsigned char)*j->p)) { snprintf(j->err, sizeof j->err, "bad number"); return NULL; }
        while (isdigit((unsigned char)*j->p)) j->p++;
        if (*j->p == '.') { j->p++; if (!isdigit((unsigned char)*j->p)) { snprintf(j->err, sizeof j->err, "bad number"); return NULL; } while (isdigit((unsigned char)*j->p)) j->p++; }
        if (*j->p == 'e' || *j->p == 'E') {
            j->p++;
            if (*j->p == '+' || *j->p == '-') j->p++;
            if (!isdigit((unsigned char)*j->p)) { snprintf(j->err, sizeof j->err, "bad number"); return NULL; }
            while (isdigit((unsigned char)*j->p)) j->p++;
        }
        if (!(t = strndup(s, (size_t)(j->p - s)))) goto oom;
        if (!(v = jv_new(JNUM, NULL))) { free(t); goto oom; }
        v->s = t;
    } else {
        snprintf(j->err, sizeof j->err, *j->p ? "unexpected '%c'" : "unexpected end of file", *j->p);
        return NULL;
    }
    j->depth--;
    return v;
oom:
    snprintf(j->err, sizeof j->err, "out of memory");
    return NULL;
}

static void jd_str(ibuf *b, const char *s)
{
    ib_u8(b, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        char e[8];
        switch (c) {
        case '"': ib_put(b, "\\\"", 2); break;
        case '\\': ib_put(b, "\\\\", 2); break;
        case '\n': ib_put(b, "\\n", 2); break;
        case '\r': ib_put(b, "\\r", 2); break;
        case '\t': ib_put(b, "\\t", 2); break;
        case '\b': ib_put(b, "\\b", 2); break;
        case '\f': ib_put(b, "\\f", 2); break;
        default:
            if (c < 0x20) { snprintf(e, sizeof e, "\\u%04x", c); ib_put(b, e, 6); }
            else ib_u8(b, c);
        }
    }
    ib_u8(b, '"');
}

/* Python's json.dump(indent=2, ensure_ascii=False): what IR Remote writes */
static void jd_value(ibuf *b, const jv *v, int ind)
{
    size_t i;
    int k;
    switch (v->t) {
    case JN: ib_put(b, "null", 4); return;
    case JB: case JNUM: ib_put(b, v->s, strlen(v->s)); return;
    case JS: jd_str(b, v->s); return;
    default: break;
    }
    if (!v->n) { ib_put(b, v->t == JO ? "{}" : "[]", 2); return; }
    ib_u8(b, v->t == JO ? '{' : '[');
    for (i = 0; i < v->n; i++) {
        ib_put(b, i ? ",\n" : "\n", i ? 2 : 1);
        for (k = 0; k < ind + 2; k++) ib_u8(b, ' ');
        if (v->t == JO) { jd_str(b, v->k[i]); ib_put(b, ": ", 2); }
        jd_value(b, v->a[i], ind + 2);
    }
    ib_u8(b, '\n');
    for (k = 0; k < ind; k++) ib_u8(b, ' ');
    ib_u8(b, v->t == JO ? '}' : ']');
}

/* ======================================================================
 * the codes file
 * ====================================================================== */
struct ir_doc { jv *root; };

const char *ir_codes_path(void)
{
    static char p[1024];
    const char *e = getenv("IRREMOTE_CONFIG"), *x = getenv("XDG_CONFIG_HOME"), *h = getenv("HOME");
    if (e && *e) snprintf(p, sizeof p, "%s", e);
    else if (x && *x) snprintf(p, sizeof p, "%s/irremote/remotes.json", x);
    else snprintf(p, sizeof p, "%s/.config/irremote/remotes.json", h && *h ? h : ".");
    return p;
}

static jv *remotes_of(const ir_doc *d) { jv *r = d && d->root ? jo_get(d->root, "remotes") : NULL; return r && r->t == JA ? r : NULL; }
static jv *remote_at(const ir_doc *d, int r) { jv *a = remotes_of(d); return a && r >= 0 && (size_t)r < a->n ? a->a[r] : NULL; }
static jv *buttons_of(const jv *rem) { jv *b = jo_get(rem, "buttons"); return b && b->t == JA ? b : NULL; }
static const char *jstr(const jv *o, const char *key) { jv *v = jo_get(o, key); return v && v->t == JS ? v->s : ""; }

static int blank(const char *s) { for (; s && *s; s++) if (!isspace((unsigned char)*s)) return 0; return 1; }

ir_doc *ir_doc_new(void)
{
    ir_doc *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    if (!(d->root = jv_new(JO, NULL)) || !jv_add(d->root, "version", jv_new(JNUM, "2")) || !jv_add(d->root, "remotes", jv_new(JA, NULL))) {
        jv_free(d->root);
        free(d);
        return NULL;
    }
    return d;
}

/* IR Remote's normalize() checks: a "remotes" list, every remote and button with a name */
ir_doc *ir_doc_parse(const char *text, char *err, size_t en)
{
    jpar j;
    jv *root, *rs;
    ir_doc *d;
    size_t i, k;
    memset(&j, 0, sizeof j);
    j.p = text;
    if (!(root = jp_value(&j))) { snprintf(err, en, "not valid JSON: %s", j.err); return NULL; }
    jp_ws(&j);
    if (*j.p) { jv_free(root); snprintf(err, en, "not valid JSON: extra data after the end"); return NULL; }
    if (root->t != JO || !(rs = jo_get(root, "remotes")) || rs->t != JA) {
        jv_free(root);
        snprintf(err, en, "not an IR Remote file (missing \"remotes\" list)");
        return NULL;
    }
    for (i = 0; i < rs->n; i++) {
        jv *r = rs->a[i], *bs, *nm = jo_get(r, "name");
        if (r->t != JO || !nm || nm->t != JS || blank(nm->s)) { jv_free(root); snprintf(err, en, "every remote needs a name"); return NULL; }
        bs = jo_get(r, "buttons");
        if (bs && bs->t != JA && bs->t != JN) { jv_free(root); snprintf(err, en, "remote \"%s\": \"buttons\" is not a list", nm->s); return NULL; }
        for (k = 0; bs && bs->t == JA && k < bs->n; k++) {
            jv *b = bs->a[k], *bn = jo_get(b, "name");
            if (b->t != JO || !bn || bn->t != JS || blank(bn->s)) {
                jv_free(root);
                snprintf(err, en, "remote \"%s\": every button needs a name", nm->s);
                return NULL;
            }
        }
    }
    if (!(d = calloc(1, sizeof *d))) { jv_free(root); snprintf(err, en, "out of memory"); return NULL; }
    d->root = root;
    return d;
}

ir_doc *ir_doc_load(const char *path, int *exists, char *err, size_t en)
{
    FILE *f;
    ibuf b = { 0 };
    char tmp[4096];
    size_t n;
    ir_doc *d;
    if (exists) *exists = 0;
    if (!(f = fopen(path, "rb"))) {
        if (errno == ENOENT) return ir_doc_new();
        snprintf(err, en, "cannot read %s: %s", path, strerror(errno));
        return NULL;
    }
    if (exists) *exists = 1;
    while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) ib_put(&b, tmp, n);
    fclose(f);
    ib_u8(&b, 0);
    if (b.oom) { ib_free(&b); snprintf(err, en, "out of memory"); return NULL; }
    if (strlen((char *)b.p) != b.len - 1) { ib_free(&b); snprintf(err, en, "%s: not a text file", path); return NULL; }
    d = ir_doc_parse((char *)b.p + (b.len >= 4 && !memcmp(b.p, "\xef\xbb\xbf", 3) ? 3 : 0), err, en);
    ib_free(&b);
    return d;
}

void ir_doc_free(ir_doc *d) { if (d) { jv_free(d->root); free(d); } }

char *ir_doc_dump(const ir_doc *d)
{
    ibuf b = { 0 };
    jd_value(&b, d->root, 0);
    ib_put(&b, "\n", 2);
    if (b.oom) { ib_free(&b); return NULL; }
    return (char *)b.p;
}

static int copy_file(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b;
    char buf[8192];
    size_t n;
    int ok = 1;
    if (!a) return 0;
    if (!(b = fopen(to, "wbx"))) { fclose(a); return errno == EEXIST; }
    while ((n = fread(buf, 1, sizeof buf, a)) > 0) if (fwrite(buf, 1, n, b) != n) ok = 0;
    fclose(a);
    if (fclose(b)) ok = 0;
    return ok;
}

static int mkdirs(const char *dir)
{
    char p[1024], *s;
    snprintf(p, sizeof p, "%s", dir);
    for (s = p + 1; *s; s++)
        if (*s == '/') { *s = 0; if (mkdir(p, 0755) && errno != EEXIST) return 0; *s = '/'; }
    return !mkdir(p, 0755) || errno == EEXIST;
}

static int doc_write(ir_doc *d, const char *path, int keep_copy, char *err, size_t en);
int ir_doc_save(ir_doc *d, const char *path, char *err, size_t en) { return doc_write(d, path, 1, err, en); }
int ir_doc_export(ir_doc *d, const char *path, char *err, size_t en) { return doc_write(d, path, 0, err, en); }

static int doc_write(ir_doc *d, const char *path, int keep_copy, char *err, size_t en)
{
    char dir[1024], tmp[1100], bak[1100], *txt, *sl;
    jv *ver = jo_get(d->root, "version");
    FILE *f;
    size_t len;
    int ok;
    /* IR Remote's DATA_VERSION is 2; a newer file keeps its number */
    if (!ver || ver->t != JNUM || atof(ver->s) < 2) {
        jv *two = jv_new(JNUM, "2");
        if (ver) { if (!jo_set(d->root, "version", two)) { snprintf(err, en, "out of memory"); return 0; } }
        else if (two && jv_add(d->root, "version", two)) {   /* first, as IR Remote writes it */
            char *k = d->root->k[d->root->n - 1];
            memmove(d->root->a + 1, d->root->a, (d->root->n - 1) * sizeof *d->root->a);
            memmove(d->root->k + 1, d->root->k, (d->root->n - 1) * sizeof *d->root->k);
            d->root->a[0] = two;
            d->root->k[0] = k;
        } else { jv_free(two); snprintf(err, en, "out of memory"); return 0; }
    }
    if (!(txt = ir_doc_dump(d))) { snprintf(err, en, "out of memory"); return 0; }
    snprintf(dir, sizeof dir, "%s", path);
    if ((sl = strrchr(dir, '/'))) { *sl = 0; if (*dir && !mkdirs(dir)) { snprintf(err, en, "cannot create %s: %s", dir, strerror(errno)); free(txt); return 0; } }
    snprintf(bak, sizeof bak, "%s.before-srs", path);
    if (keep_copy && access(path, F_OK) == 0 && access(bak, F_OK) != 0 && !copy_file(path, bak)) {
        snprintf(err, en, "cannot keep a copy of %s as %s: %s - nothing written", path, bak, strerror(errno));
        free(txt);
        return 0;
    }
    {   /* a temporary file of its own (two writers never share it), with the file's mode */
        struct stat st;
        int fd;
        snprintf(tmp, sizeof tmp, "%s.srs-XXXXXX", path);
        if ((fd = mkstemp(tmp)) < 0) { snprintf(err, en, "cannot write %s: %s", tmp, strerror(errno)); free(txt); return 0; }
        fchmod(fd, stat(path, &st) == 0 ? (st.st_mode & 07777) : 0644);
        if (!(f = fdopen(fd, "wb"))) { snprintf(err, en, "cannot write %s: %s", tmp, strerror(errno)); close(fd); unlink(tmp); free(txt); return 0; }
    }
    len = strlen(txt);
    ok = fwrite(txt, 1, len, f) == len;
    ok = !fflush(f) && ok;
    ok = !fsync(fileno(f)) && ok;
    ok = !fclose(f) && ok;
    free(txt);
    if (!ok || rename(tmp, path)) { snprintf(err, en, "cannot write %s: %s", path, strerror(errno)); unlink(tmp); return 0; }
    return 1;
}

int ir_nremotes(const ir_doc *d) { jv *a = remotes_of(d); return a ? (int)a->n : 0; }
const char *ir_remote_name(const ir_doc *d, int r) { jv *x = remote_at(d, r); return x ? jstr(x, "name") : ""; }
int ir_nbuttons(const ir_doc *d, int r) { jv *x = remote_at(d, r), *b = x ? buttons_of(x) : NULL; return b ? (int)b->n : 0; }

int ir_button_at(const ir_doc *d, int r, int b, ir_button *out)
{
    jv *x = remote_at(d, r), *bs = x ? buttons_of(x) : NULL, *bt, *fq;
    memset(out, 0, sizeof *out);
    if (!bs || b < 0 || (size_t)b >= bs->n) return 0;
    bt = bs->a[b];
    out->name = jstr(bt, "name");
    out->type = strcmp(jstr(bt, "type"), "nec") ? "learned" : "nec";
    out->code = jstr(bt, "code");
    out->shortcut = jstr(bt, "shortcut");
    fq = jo_get(bt, "frequency");
    out->freq = fq && (fq->t == JNUM || fq->t == JS) && fq->s && atof(fq->s) > 0 ? (unsigned long)atof(fq->s) : IR_DEFAULT_FREQ;
    return 1;
}

static int find_in(const jv *arr, const char *name)
{
    size_t i;
    if (!arr || !name) return -1;
    for (i = 0; i < arr->n; i++) if (!strcmp(jstr(arr->a[i], "name"), name)) return (int)i;
    for (i = 0; i < arr->n; i++) if (!strcasecmp(jstr(arr->a[i], "name"), name)) return (int)i;
    return -1;
}

int ir_find_remote(const ir_doc *d, const char *name) { return find_in(remotes_of(d), name); }
int ir_find_button(const ir_doc *d, int r, const char *name) { jv *x = remote_at(d, r); return x ? find_in(buttons_of(x), name) : -1; }

static char *trimmed(const char *s)
{
    size_t n;
    while (s && isspace((unsigned char)*s)) s++;
    n = s ? strlen(s) : 0;
    while (n && isspace((unsigned char)s[n - 1])) n--;
    return s ? strndup(s, n) : NULL;
}

int ir_remote_add(ir_doc *d, const char *name)
{
    jv *a = remotes_of(d), *r;
    char *nm = trimmed(name);
    int at;
    if (!a || !nm || !*nm) { free(nm); return -1; }
    if ((at = find_in(a, nm)) >= 0) { free(nm); return at; }
    if (!(r = jv_new(JO, NULL)) || !jv_add(r, "name", jv_new(JS, nm)) || !jv_add(r, "buttons", jv_new(JA, NULL)) || !jv_add(a, NULL, r)) {
        jv_free(r);   /* not added (out of memory) */
        free(nm);
        return -1;
    }
    free(nm);
    return (int)a->n - 1;
}

int ir_remote_rename(ir_doc *d, int r, const char *name)
{
    jv *x = remote_at(d, r);
    char *nm = trimmed(name);
    int at = nm ? find_in(remotes_of(d), nm) : -1, ok = 0;
    if (x && nm && *nm && (at < 0 || at == r)) ok = jo_set(x, "name", jv_new(JS, nm));
    free(nm);
    return ok;
}

int ir_remote_delete(ir_doc *d, int r)
{
    jv *a = remotes_of(d);
    if (!a || r < 0 || (size_t)r >= a->n) return 0;
    ja_del(a, (size_t)r);
    return 1;
}

int ir_button_set(ir_doc *d, int r, const char *name, const char *code, int nec, unsigned long freq)
{
    jv *x = remote_at(d, r), *bs, *b;
    char *nm = trimmed(name), *cd = clean_hex(code ? code : ""), fq[32];
    int at = -1;
    if (!x || !nm || !*nm || !cd) goto out;
    if (!(bs = buttons_of(x))) {
        if (!jo_set(x, "buttons", jv_new(JA, NULL))) goto out;
        bs = buttons_of(x);
    }
    snprintf(fq, sizeof fq, "%lu", freq ? freq : IR_DEFAULT_FREQ);
    if ((at = find_in(bs, nm)) >= 0) {   /* change the code, keep the rest (shortcuts, fields SRS does not know) */
        b = bs->a[at];
        if (!jo_set(b, "type", jv_new(JS, nec ? "nec" : "learned")) || !jo_set(b, "code", jv_new(JS, cd)) ||
            !jo_set(b, "frequency", jv_new(JNUM, fq)))
            at = -1;
        goto out;
    }
    if (!(b = jv_new(JO, NULL)) || !jv_add(b, "name", jv_new(JS, nm)) || !jv_add(b, "type", jv_new(JS, nec ? "nec" : "learned")) ||
        !jv_add(b, "code", jv_new(JS, cd)) || !jv_add(b, "frequency", jv_new(JNUM, fq)) || !jv_add(b, "shortcut", jv_new(JS, "")) ||
        !jv_add(b, "app_shortcut", jv_new(JS, "")) || !jv_add(bs, NULL, b))
        goto out;
    at = (int)bs->n - 1;
out:
    free(nm);
    free(cd);
    return at;
}

int ir_button_rename(ir_doc *d, int r, int b, const char *name)
{
    jv *x = remote_at(d, r), *bs = x ? buttons_of(x) : NULL;
    char *nm = trimmed(name);
    int at = nm ? find_in(bs, nm) : -1, ok = 0;
    if (bs && b >= 0 && (size_t)b < bs->n && nm && *nm && (at < 0 || at == b)) ok = jo_set(bs->a[b], "name", jv_new(JS, nm));
    free(nm);
    return ok;
}

int ir_button_delete(ir_doc *d, int r, int b)
{
    jv *x = remote_at(d, r), *bs = x ? buttons_of(x) : NULL;
    if (!bs || b < 0 || (size_t)b >= bs->n) return 0;
    ja_del(bs, (size_t)b);
    return 1;
}

int ir_doc_merge(ir_doc *base, const ir_doc *extra)
{
    jv *ba = remotes_of(base), *ea = remotes_of(extra);
    size_t i, k, m;
    if (!ba || !ea) return 0;
    for (i = 0; i < ea->n; i++) {
        jv *er = ea->a[i], *eb = buttons_of(er), *tr, *tb, *lay;
        int at = find_in(ba, jstr(er, "name"));
        if (at < 0) { if (!jv_add(ba, NULL, jv_clone(er))) return 0; continue; }
        tr = ba->a[at];
        if ((lay = jo_get(er, "layout")) && lay->t != JN && (!jo_get(tr, "layout") || jo_get(tr, "layout")->t == JN))
            jo_set(tr, "layout", jv_clone(lay));
        if (!eb) continue;
        if (!(tb = buttons_of(tr))) { jo_set(tr, "buttons", jv_new(JA, NULL)); tb = buttons_of(tr); }
        if (!tb) return 0;
        for (k = 0; k < eb->n; k++) {
            int bt = find_in(tb, jstr(eb->a[k], "name"));
            if (bt < 0) { if (!jv_add(tb, NULL, jv_clone(eb->a[k]))) return 0; continue; }
            for (m = 0; m < eb->a[k]->n; m++) jo_set(tb->a[bt], eb->a[k]->k[m], jv_clone(eb->a[k]->a[m]));   /* dict.update */
        }
    }
    return 1;
}

/* ======================================================================
 * [ir-blaster]: read-only (sysfs and the codes file; the dongle is never opened here)
 * ====================================================================== */
static int irremote_installed(char *where, size_t n)
{
    const char *path = getenv("PATH");
    char dirs[4096], *s, *save = NULL;
    snprintf(dirs, sizeof dirs, "%s", path && *path ? path : "/usr/local/bin:/usr/bin:/bin");
    for (s = strtok_r(dirs, ":", &save); s; s = strtok_r(NULL, ":", &save)) {
        char p[4200];
        snprintf(p, sizeof p, "%s/irremote", s);
        if (access(p, X_OK) == 0) { snprintf(where, n, "%s", p); return 1; }
    }
    return 0;
}

void s_ir_blaster(srs_out *o)
{
    static const int tabs[] = { 150 };
    static const int tabs2[] = { 150, 300, 380, 440, 540 };
    ir_dongle d;
    char s[700], where[1100], rule[600];
    const char *path = ir_codes_path();
    ir_doc *doc;
    int exists = 0, r, b, nb = 0;
    char err[300] = "";
    so_tabs(o, tabs, 1);
    so_b(o, C_VALUE, "IR Blaster - ELK Smart / OCRUSTAR USB infrared dongle");
    so_par(o);
    so_par(o);
    ir_detect(&d);
    if (d.state == IR_READY) {
        so_kv(o, "Status", C_GREEN, "Ready");
        snprintf(s, sizeof s, "%s (%04x:%04x)", d.name, IR_VENDOR, d.product);
        so_kv(o, "Dongle", C_VALUE, s);
        so_kv(o, "Device", C_PURPLE, d.node);
    } else if (d.state == IR_NOPERM) {
        so_kv(o, "Status", C_ORANGE, "Found, but no permission to use it");
        snprintf(s, sizeof s, "%s (%04x:%04x) at %s", d.name, IR_VENDOR, d.product, d.node);
        so_kv(o, "Dongle", C_VALUE, s);
        if (d.nonode) snprintf(s, sizeof s, "The device file %s is missing (is usbfs / devtmpfs mounted?) - unplug and replug the dongle", d.node);
        else if (ir_udev_rule_found(rule, sizeof rule)) snprintf(s, sizeof s, "The udev rule is installed (%s): unplug and replug the dongle so it applies", rule);
        else snprintf(s, sizeof s, "Install the udev rule (the package's %s from elksmart-comm-git or IR Remote, or your own copy in "
                                   "/etc/udev/rules.d), then unplug and replug the dongle", IR_UDEV_RULE);
        so_kv(o, "Fix", C_RED, s);
        if (!d.nonode) so_kv(o, "Rule", C_PURPLE, IR_UDEV_LINE);
    } else {
        so_kv(o, "Status", C_ORANGE, "Not connected");
        so_kv(o, "", C_GRAY, ir_absent_text());
    }
    so_kv(o, "Supported", C_VALUE, IR_SUPPORTED_IDS);
    so_kv(o, "Permission rule", C_VALUE, ir_udev_rule_found(rule, sizeof rule) ? rule
                                         : "not installed (looked for " IR_UDEV_NAME " in /etc, /run, /usr/local/lib and /usr/lib udev/rules.d)");
    so_kv(o, "IR Remote app", C_VALUE, irremote_installed(where, sizeof where) ? where : "not installed");
    doc = ir_doc_load(path, &exists, err, sizeof err);
    for (r = 0; doc && r < ir_nremotes(doc); r++) nb += ir_nbuttons(doc, r);
    if (!doc) snprintf(s, sizeof s, "%s - %s", path, err);
    else if (!exists) snprintf(s, sizeof s, "%s (not created yet - learn a code to start)", path);
    else snprintf(s, sizeof s, "%s (%d remote%s, %d code%s; shared with IR Remote)", path, ir_nremotes(doc),
                  ir_nremotes(doc) == 1 ? "" : "s", nb, nb == 1 ? "" : "s");
    so_kv(o, "Codes file", doc ? C_VALUE : C_RED, s);
    if (doc && nb + ir_nremotes(doc) > 0) {
        static const char *const head[] = { "Remote", "Button", "Type", "Bytes", "Frequency", "Shortcut" };
        static const int hc[] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL };
        so_par(o);
        so_tabs(o, tabs2, 5);
        so_row(o, 6, head, hc);
        for (r = 0; r < ir_nremotes(doc); r++) {
            if (!ir_nbuttons(doc, r)) {
                const char *cells[] = { ir_remote_name(doc, r), "(no codes yet)", "", "", "", "" };
                static const int cc[] = { C_VALUE, C_GRAY, C_VALUE, C_VALUE, C_VALUE, C_VALUE };
                so_row(o, 6, cells, cc);
            }
            for (b = 0; b < ir_nbuttons(doc, r); b++) {
                ir_button bt;
                char by[24], fq[32];
                const char *cells[6];
                static const int cc[] = { C_VALUE, C_VALUE, C_PURPLE, C_VALUE, C_VALUE, C_TEAL };
                ir_button_at(doc, r, b, &bt);
                snprintf(by, sizeof by, "%zu", strlen(bt.code) / 2);
                snprintf(fq, sizeof fq, "%lu Hz", bt.freq);
                cells[0] = b ? "" : ir_remote_name(doc, r);
                cells[1] = bt.name;
                cells[2] = *bt.code ? bt.type : "not learned";
                cells[3] = *bt.code ? by : "-";
                cells[4] = fq;
                cells[5] = *bt.shortcut ? bt.shortcut : "";
                so_row(o, 6, cells, cc);
            }
        }
    }
    ir_doc_free(doc);
    so_par(o);
    so_note(o, o->gui ? "Learn, name and send codes on the Remotes view of this tab (srs-gui). Nothing is sent by this report."
                      : "sysreport-search ir --list | --status | --send REMOTE BUTTON | --learn. Nothing is sent by this report.");
}
