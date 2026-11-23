/* gpu_tune.c - GPU Tuning, the read-only part (see gpu_tune.h): the card's settings from /sys, Tux Burner's
 * service through its socket (two read requests, nothing else), LACT's state and saved values, and the text / JSON
 * of "sysreport-search gpu-tune status" and the GPU Tuning tab.
 *
 * Written from the description of Tux Burner's protocol (one JSON object per line on a Unix socket); no Tux Burner
 * code is used here. Tux Burner is a separate program (GPL-3.0-or-later); SRS only talks to it.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#define _GNU_SOURCE
#include "gpu_tune.h"
#include "bench.h"
#include "srs.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define TB_SOCKET "/run/tux-burnerd.sock"
#define TB_URL "https://github.com/eugenesflanagin-spec/tux-burner"
#define TB_WAIT_MS 800               /* the longest SRS waits for one answer of the service */
#define TB_REPLY_MAX (256 * 1024)

/* ------------------------------------------------------------------ files */
static const char *t_root(void)
{
    const char *r = getenv("SRS_TEST_SYSFS_ROOT");
    return r && r[0] == '/' && strlen(r) < 200 ? r : "";
}

static char *t_path(char *out, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static char *t_path(char *out, size_t n, const char *fmt, ...)
{
    va_list ap;
    size_t l;
    snprintf(out, n, "%s", t_root());
    l = strlen(out);
    va_start(ap, fmt);
    vsnprintf(out + l, n > l ? n - l : 0, fmt, ap);
    va_end(ap);
    return out;
}

/* a small file, whole (malloc), NUL-terminated; NULL = missing or unreadable (*err = errno) */
static char *t_slurp(const char *path, size_t max, size_t *len, int *err)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOCTTY);
    char *b;
    size_t n = 0;
    ssize_t r;
    if (err) *err = 0;
    if (fd < 0) { if (err) *err = errno; return NULL; }
    if (!(b = malloc(max + 1))) { close(fd); return NULL; }
    while (n < max && (r = read(fd, b + n, max - n)) > 0) n += (size_t)r;
    close(fd);
    b[n] = 0;
    if (len) *len = n;
    return b;
}

static int t_line(const char *path, char *out, size_t n)
{
    char *b = t_slurp(path, 255, NULL, NULL);
    out[0] = 0;
    if (!b) return 0;
    b[strcspn(b, "\n")] = 0;
    snprintf(out, n, "%s", b);
    free(b);
    return 1;
}

static int t_long(const char *path, long *v)
{
    char b[64], *e;
    if (!t_line(path, b, sizeof b) || !b[0]) return 0;
    errno = 0;
    *v = strtol(b, &e, 0);
    return !errno && e != b;
}

/* a decimal number, read the same way in every locale (GTK sets the user's, where strtod may want a comma).
   Returns the end of the number, or s when there is none */
static const char *t_num(const char *s, double *out)
{
    const char *p = s;
    double v = 0, scale = 0.1;
    int neg = 0, digits = 0;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
    for (; isdigit((unsigned char)*p); p++, digits++) v = v * 10 + (*p - '0');
    if (*p == '.') for (p++; isdigit((unsigned char)*p); p++, digits++, scale /= 10) v += (*p - '0') * scale;
    if (!digits) return s;
    if (*p == 'e' || *p == 'E') {   /* an exponent: rare in these answers, read it anyway */
        const char *q = p + 1;
        int en = 0, ex = 0;
        if (*q == '-') { en = 1; q++; } else if (*q == '+') q++;
        if (isdigit((unsigned char)*q)) {
            for (; isdigit((unsigned char)*q) && ex < 400; q++) ex = ex * 10 + (*q - '0');
            while (ex-- > 0) v = en ? v / 10 : v * 10;
            p = q;
        }
    }
    *out = neg ? -v : v;
    return p;
}

/* ------------------------------------------------------------------ a small JSON reader (the service's answers) */
static const char *js_ws(const char *p) { while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++; return p; }

static const char *js_skip_str(const char *p)
{
    if (*p != '"') return NULL;
    for (p++; *p && *p != '"'; p++) if (*p == '\\' && p[1]) p++;
    return *p == '"' ? p + 1 : NULL;
}

/* the end of one value; NULL = broken JSON */
static const char *js_skip(const char *p, int depth)
{
    p = js_ws(p);
    if (depth > 40) return NULL;
    if (*p == '"') return js_skip_str(p);
    if (*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';
        int obj = *p == '{';
        p = js_ws(p + 1);
        if (*p == close) return p + 1;
        for (;;) {
            if (obj) {
                if (!(p = js_skip_str(js_ws(p)))) return NULL;
                p = js_ws(p);
                if (*p != ':') return NULL;
                p++;
            }
            if (!(p = js_skip(p, depth + 1))) return NULL;
            p = js_ws(p);
            if (*p == ',') { p++; continue; }
            return *p == close ? p + 1 : NULL;
        }
    }
    if (!*p) return NULL;
    while (*p && *p != ',' && *p != '}' && *p != ']' && !isspace((unsigned char)*p)) p++;   /* number, true, false, null */
    return p;
}

/* the value of a key of the object at v; NULL = not an object or no such key */
static const char *js_key(const char *v, const char *key)
{
    size_t kl = strlen(key);
    if (!v) return NULL;
    v = js_ws(v);
    if (*v != '{') return NULL;
    v = js_ws(v + 1);
    while (*v == '"') {
        const char *ke = js_skip_str(v), *val;
        int hit;
        if (!ke) return NULL;
        hit = (size_t)(ke - v - 2) == kl && !strncmp(v + 1, key, kl);
        val = js_ws(ke);
        if (*val != ':') return NULL;
        val = js_ws(val + 1);
        if (hit) return val;
        if (!(v = js_skip(val, 0))) return NULL;
        v = js_ws(v);
        if (*v == ',') v = js_ws(v + 1);
    }
    return NULL;
}

/* "a.b.c" below the object at v */
static const char *js_path(const char *v, const char *path)
{
    char k[64];
    while (v && *path) {
        size_t n = strcspn(path, ".");
        if (n >= sizeof k) return NULL;
        memcpy(k, path, n);
        k[n] = 0;
        v = js_key(v, k);
        path += n + (path[n] == '.');
    }
    return v;
}

static const char *js_at(const char *v, int i)   /* element i of the array at v */
{
    if (!v) return NULL;
    v = js_ws(v);
    if (*v != '[') return NULL;
    v = js_ws(v + 1);
    while (*v && *v != ']') {
        if (i-- == 0) return v;
        if (!(v = js_skip(v, 0))) return NULL;
        v = js_ws(v);
        if (*v == ',') v = js_ws(v + 1);
    }
    return NULL;
}

static int js_num(const char *v, int *out)   /* a number, rounded; GTN_NONE for null or anything else */
{
    const char *e;
    double d = 0;
    *out = GTN_NONE;
    if (!v) return 0;
    v = js_ws(v);
    if (*v != '-' && !isdigit((unsigned char)*v)) return 0;
    e = t_num(v, &d);
    if (e == v || d > 1e9 || d < -1e9) return 0;
    *out = (int)(d < 0 ? d - 0.5 : d + 0.5);
    return 1;
}

static int js_text(const char *v, char *out, size_t n)   /* a string value, simple escapes undone */
{
    size_t o = 0;
    out[0] = 0;
    if (!v) return 0;
    v = js_ws(v);
    if (*v != '"') return 0;
    for (v++; *v && *v != '"' && o + 1 < n; v++) {
        if (*v == '\\' && v[1]) {
            v++;
            if (*v == 'n' || *v == 't' || *v == 'r') out[o++] = ' ';
            else if (*v == 'u') { out[o++] = '?'; if (strlen(v) >= 5) v += 4; }
            else out[o++] = *v;
        } else if ((unsigned char)*v >= 0x20) out[o++] = *v;
    }
    out[o] = 0;
    return 1;
}

static int js_true(const char *v) { return v && !strncmp(js_ws(v), "true", 4); }

/* is word one of the strings of the array at v */
static int js_has(const char *v, const char *word)
{
    int i;
    const char *e;
    char s[64];
    for (i = 0; (e = js_at(v, i)); i++) if (js_text(e, s, sizeof s) && !strcmp(s, word)) return 1;
    return 0;
}

/* ------------------------------------------------------------------ Tux Burner's socket: two read requests, no more */
enum { TBQ_STATUS, TBQ_CAPABILITIES };

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Ask the service one of its two read questions. The request text is built HERE and only here: this function cannot
   send a write method, and nothing else in SRS opens the socket.
   0 = an answer line is in *reply (malloc); -1 no socket; -2 nobody answers the connection; -3 no answer in time */
static int tb_ask(const char *sock, int question, int gpu_index, char **reply)
{
    struct sockaddr_un a;
    struct stat st;
    struct pollfd pf;
    char req[80], *buf;
    size_t len = 0, want;
    long long end;
    int fd, n;
    *reply = NULL;
    if (question == TBQ_STATUS) snprintf(req, sizeof req, "{\"method\":\"status\"}\n");
    else if (question == TBQ_CAPABILITIES && gpu_index >= 0 && gpu_index < 64) snprintf(req, sizeof req, "{\"method\":\"gpu_capabilities\",\"gpu_index\":%d}\n", gpu_index);
    else return -3;
    if (strlen(sock) >= sizeof a.sun_path) return -1;
    if (stat(sock, &st) || !S_ISSOCK(st.st_mode)) return -1;
    if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)) < 0) return -2;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", sock);
    if (connect(fd, (struct sockaddr *)&a, sizeof a)) { close(fd); return -2; }
    end = now_ms() + TB_WAIT_MS;
    want = strlen(req);
    pf.fd = fd;
    while (len < want) {   /* MSG_NOSIGNAL: a service that answers "not allowed" and hangs up must not kill SRS */
        ssize_t w = send(fd, req + len, want - len, MSG_NOSIGNAL);
        if (w > 0) { len += (size_t)w; continue; }
        if (w < 0 && errno == EPIPE) break;   /* it may have answered before hanging up: read what is there */
        if (w < 0 && errno != EAGAIN && errno != EINTR) { close(fd); return -3; }
        pf.events = POLLOUT;
        if ((n = (int)(end - now_ms())) <= 0 || poll(&pf, 1, n) <= 0) { close(fd); return -3; }
    }
    if (!(buf = malloc(TB_REPLY_MAX + 1))) { close(fd); return -3; }
    len = 0;
    for (;;) {
        ssize_t r = recv(fd, buf + len, TB_REPLY_MAX - len, 0);
        if (r > 0) {
            len += (size_t)r;
            if (memchr(buf, '\n', len) || len >= TB_REPLY_MAX) break;
            continue;
        }
        if (r == 0) break;
        if (errno != EAGAIN && errno != EINTR) break;
        pf.events = POLLIN;
        if ((n = (int)(end - now_ms())) <= 0 || poll(&pf, 1, n) <= 0) break;
    }
    close(fd);
    buf[len] = 0;
    if (!memchr(buf, '\n', len)) { free(buf); return -3; }   /* no whole line: too slow, too long or cut off */
    buf[strcspn(buf, "\n")] = 0;
    *reply = buf;
    return 0;
}

/* the abilities a search will need that today's service may not have yet (design: T2, T3, T4, T5) */
static const struct { const char *cap, *words; } tb_needs[] = {
    { "amd-lact-gate-v1", "the LACT check inside the service" },
    { "amd-sclk-ceiling-v1", "a clock ceiling" },
    { "gpu-reset-events-v1", "the reset watch" },
    { "tuning-session-v1", "tuning sessions with automatic return" },
};

static void tb_find_cli(gtune_tb *tb)
{
    static const char *const names[] = { "tburn-cli", "tux-burner-cli" };
    const char *path = getenv("PATH"), *p;
    size_t k;
    if (!path) return;
    for (k = 0; k < 2 && !tb->cli; k++)
        for (p = path; *p; ) {
            size_t n = strcspn(p, ":");
            char f[300];
            if (n && n < 240) {
                snprintf(f, sizeof f, "%.*s/%s", (int)n, p, names[k]);
                if (!access(f, X_OK)) { tb->cli = 1; snprintf(tb->cli_path, sizeof tb->cli_path, "%s", f); break; }
            }
            p += n + (p[n] == ':');
        }
}

static void pair(const char *v, int out[2])
{
    out[0] = out[1] = GTN_NONE;
    js_num(js_at(v, 0), &out[0]);
    js_num(js_at(v, 1), &out[1]);
}

/* the same card? The service writes AMD addresses as the kernel does and NVIDIA's with an 8-digit domain */
static int same_pci(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    if (la < 7 || lb < 7) return 0;
    return !strcasecmp(a + (la > 12 ? la - 12 : 0), b + (lb > 12 ? lb - 12 : 0));
}

static void tb_read(gtune_status *st)
{
    static long long quiet_until;   /* after an answer that did not come: leave the service alone for a while */
    gtune_tb *tb = &st->tb;
    const char *env = getenv("SRS_TUX_BURNER_SOCKET"), *res, *caps;
    char *line = NULL, err[200];
    size_t k;
    int rc, i, count;
    tb_find_cli(tb);
    tb->proto_major = tb->proto_minor = GTN_NONE;
    /* a test must name its mock socket; the real one is never used from a test. A test is: a made-up /sys, the
       private session bus of the GUI tests (tests/private-bus.sh), or the all-items column dump */
    if (!(env && *env) && (t_root()[0] || getenv("SRS_TEST_PRIVATE_BUS") || getenv("SRS_TEST_ALIGN_DUMP"))) {
        tb->daemon = GTD_UNCHECKED;
        snprintf(tb->msg, sizeof tb->msg, "test mode: no socket named (SRS_TUX_BURNER_SOCKET), the real service is not asked");
        return;
    }
    snprintf(tb->socket, sizeof tb->socket, "%s", env && *env ? env : TB_SOCKET);
    if (quiet_until && now_ms() < quiet_until) {
        tb->daemon = GTD_BAD;
        snprintf(tb->msg, sizeof tb->msg, "Tux Burner's service did not answer a moment ago; SRS asks again in half a minute");
        return;
    }
    rc = tb_ask(tb->socket, TBQ_STATUS, 0, &line);
    if (rc == -1) { tb->daemon = GTD_ABSENT; return; }
    if (rc == -2) { tb->daemon = GTD_DOWN; return; }
    if (rc) {
        tb->daemon = GTD_BAD;
        quiet_until = now_ms() + 30000;
        snprintf(tb->msg, sizeof tb->msg, "Tux Burner's service did not answer within %.1f s", TB_WAIT_MS / 1000.0);
        return;
    }
    if (!js_true(js_key(line, "ok"))) {
        js_text(js_key(line, "error"), err, sizeof err);
        tb->daemon = strstr(err, "not allowed") ? GTD_DENIED : GTD_BAD;
        snprintf(tb->msg, sizeof tb->msg, "%s", err[0] ? err : "an answer SRS does not understand");
        free(line);
        return;
    }
    res = js_key(line, "result");
    js_text(js_key(res, "version"), tb->version, sizeof tb->version);
    js_text(js_key(res, "build"), tb->build, sizeof tb->build);
    js_text(js_key(res, "state"), tb->state, sizeof tb->state);
    js_num(js_key(res, "protocol_major"), &tb->proto_major);
    js_num(js_key(res, "protocol_minor"), &tb->proto_minor);
    caps = js_key(res, "capabilities");
    tb->cap_amd = js_has(caps, "amd-tuning-v1");
    for (k = 0; k < sizeof tb_needs / sizeof tb_needs[0]; k++)
        if (!js_has(caps, tb_needs[k].cap)) {
            size_t l = strlen(tb->missing);
            snprintf(tb->missing + l, sizeof tb->missing - l, "%s%s", l ? "," : "", tb_needs[k].cap);
        }
    free(line);
    if (!res || tb->proto_major != 2 || !js_has(caps, "gpu-capabilities-v1")) {
        tb->daemon = GTD_OLD;
        if (tb->proto_major == GTN_NONE) snprintf(tb->msg, sizeof tb->msg, "the service's answer names no protocol version");
        else snprintf(tb->msg, sizeof tb->msg, "the service speaks protocol %d.%d; this SRS knows protocol 2", tb->proto_major, tb->proto_minor == GTN_NONE ? 0 : tb->proto_minor);
        return;
    }
    tb->daemon = GTD_RUNNING;
    /* the service's view of each card it knows, matched to SRS's cards by PCI address. The service numbers the cards
       its own way, so SRS cannot ask about one card only: while ANY card is asleep it asks about none, because the
       question could wake the sleeping one (--wake reads everything) */
    for (i = 0; i < st->ngpu; i++) if (st->gpu[i].asleep) st->asleep_skip = 1;
    if (st->asleep_skip) return;
    count = 1;
    for (i = 0; i < count && i < GTN_MAXGPU; i++) {
        char pci[40], vendor[16];
        const char *r;
        int g, c2, any = 0;
        for (g = 0; g < st->ngpu; g++) if (!st->gpu[g].asleep && !st->gpu[g].tb) any = 1;
        if (!any) break;
        if (tb_ask(tb->socket, TBQ_CAPABILITIES, i, &line)) { quiet_until = now_ms() + 30000; break; }
        if (!js_true(js_key(line, "ok"))) { free(line); break; }   /* no such card, or none at all */
        r = js_key(line, "result");
        if (js_num(js_key(r, "gpu_count"), &c2) && c2 > count) count = c2;
        tb->gpu_count = count;
        js_text(js_path(r, "identity.pci_bus_id"), pci, sizeof pci);
        js_text(js_key(r, "vendor"), vendor, sizeof vendor);
        for (g = 0; g < st->ngpu; g++) {
            gtune_gpu *u = &st->gpu[g];
            const char *pl = js_key(r, "power_limits");
            if (u->tb || u->asleep || !same_pci(pci, u->pci)) continue;
            u->tb = 1;
            u->tb_index = i;
            snprintf(u->tb_vendor, sizeof u->tb_vendor, "%s", vendor);
            u->tb_locked = js_true(js_key(r, "locked_clock_control"));
            js_num(js_key(r, "vddgfx_floor_mv"), &u->tb_floor_mv);
            js_text(js_key(r, "vddgfx_floor_source"), u->tb_floor_source, sizeof u->tb_floor_source);
            js_text(js_key(r, "vddgfx_floor_file"), u->tb_floor_file, sizeof u->tb_floor_file);
            js_num(js_path(r, "voltage_offset.current_mv"), &u->tb_vo_mv);
            pair(js_path(r, "voltage_offset.range_mv"), u->tb_r_vo);
            pair(js_path(r, "clock_offset_ranges_mhz.gpc"), u->tb_r_sclk);
            pair(js_path(r, "clock_offset_ranges_mhz.memory"), u->tb_r_mem);
            js_num(js_path(r, "clock_offsets_mhz.gpc"), &u->tb_sclk_off);
            js_num(js_path(r, "clock_offsets_mhz.memory"), &u->tb_mem_off);
            js_num(js_key(pl, "power_limit_w"), &u->tb_cap_w);
            js_num(js_key(pl, "power_limit_min_w"), &u->tb_cap_min_w);
            js_num(js_key(pl, "power_limit_max_w"), &u->tb_cap_max_w);
            js_num(js_key(pl, "power_limit_default_w"), &u->tb_cap_default_w);
            if (strcmp(vendor, "amd")) u->tb_floor_mv = GTN_NONE;   /* the voltage floor is an AMD OverDrive rule */
            break;
        }
        free(line);
    }
}

/* ------------------------------------------------------------------ the card (sysfs) */
static int num_after(const char *s, int *out)   /* "  -110mV" / "1: 1375MHz" -> the number before the unit */
{
    char *e;
    long v;
    while (*s == ' ' || *s == '\t') s++;
    v = strtol(s, &e, 10);
    if (e == s) return 0;
    *out = (int)v;
    return 1;
}

/* amdgpu's OverDrive table.
   RDNA 4:  OD_SCLK_OFFSET: / 300Mhz        OD_MCLK: / 0: 676Mhz / 1: 1375MHz       OD_VDDGFX_OFFSET: / -110mV
            OD_RANGE: / SCLK_OFFSET: -500Mhz 1000Mhz / MCLK: 97Mhz 1500Mhz / VDDGFX_OFFSET: -200mv 0mv
   RDNA 3:  OD_SCLK: / 0: 500Mhz / 1: 2900Mhz (lowest and highest core clock, no offset)   OD_MCLK: ...
            OD_VDDGFX_OFFSET: / 0mV         OD_RANGE: / SCLK: 500Mhz 5000Mhz / MCLK: ... / VDDGFX_OFFSET: -450mv 0mv
   Older cards list voltage points (OD_VDDC_CURVE:) and have no voltage offset: recognised, not handled yet. */
static void od_parse(gtune_gpu *g, const char *text)
{
    char sec[32] = "", line[160];
    const char *p = text;
    int newer = 0, older = 0;
    while (*p) {
        size_t n = strcspn(p, "\n");
        snprintf(line, sizeof line, "%.*s", (int)(n < sizeof line - 1 ? n : sizeof line - 1), p);
        p += n + (p[n] == '\n');
        if (!strncmp(line, "OD_", 3) && strchr(line, ':') && !strchr(line, ' ')) {
            snprintf(sec, sizeof sec, "%.*s", (int)strcspn(line, ":"), line);
            if (!strcmp(sec, "OD_SCLK") || !strcmp(sec, "OD_VDDC_CURVE")) older = 1;
            continue;
        }
        if (!strcmp(sec, "OD_SCLK")) { if (!strncmp(line, "1:", 2)) num_after(line + 2, &g->sclk_max_mhz); }
        else if (!strcmp(sec, "OD_SCLK_OFFSET")) { if (num_after(line, &g->sclk_off_mhz)) newer = 1; }
        else if (!strcmp(sec, "OD_VDDGFX_OFFSET")) { if (num_after(line, &g->vo_mv)) newer = 1; }
        else if (!strcmp(sec, "OD_MCLK")) { if (!strncmp(line, "1:", 2)) num_after(line + 2, &g->mclk_max_mhz); }
        else if (!strcmp(sec, "OD_RANGE")) {
            int *r = !strncmp(line, "SCLK_OFFSET:", 12) ? g->r_sclk : !strncmp(line, "MCLK:", 5) ? g->r_mclk : !strncmp(line, "VDDGFX_OFFSET:", 14) ? g->r_vo :
                     !strncmp(line, "SCLK:", 5) ? g->r_sclk_abs : NULL;
            const char *q = strchr(line, ':');
            char *e;
            if (r && q) {
                long a = strtol(q + 1, &e, 10), b;
                if (e != q + 1) {
                    while (*e && !isspace((unsigned char)*e)) e++;   /* the unit */
                    q = e;
                    b = strtol(q, &e, 10);
                    if (e != q) { r[0] = (int)a; r[1] = (int)b; }
                }
            }
        }
    }
    g->od = newer ? 1 : older ? 2 : 0;   /* a voltage or clock offset = a table SRS reads; only points = the older kind */
    if (g->od != 1) g->sclk_max_mhz = g->r_sclk_abs[0] = g->r_sclk_abs[1] = GTN_NONE;
}

static int uw_to_w(const char *path)
{
    long v;
    return t_long(path, &v) && v >= 0 ? (int)((v + 500000) / 1000000) : GTN_NONE;
}

static void hwmon_read(gtune_gpu *g, const char *dev)
{
    char dir[400], p[520], lb[32];
    DIR *d;
    struct dirent *e;
    long v;
    int i;
    g->edge_c = g->junction_c = g->power_w = g->clock_mhz = g->vddgfx_mv = g->fan_rpm = -1;
    snprintf(dir, sizeof dir, "%s/hwmon", dev);
    if (!(d = opendir(dir))) return;
    dir[0] = 0;
    while ((e = readdir(d))) if (!strncmp(e->d_name, "hwmon", 5)) { snprintf(dir, sizeof dir, "%s/hwmon/%.60s", dev, e->d_name); break; }
    closedir(d);
    if (!dir[0]) return;
#define HW(f) (snprintf(p, sizeof p, "%s/%s", dir, f), p)
    g->cap_w = uw_to_w(HW("power1_cap"));
    g->cap_min_w = uw_to_w(HW("power1_cap_min"));
    g->cap_max_w = uw_to_w(HW("power1_cap_max"));
    g->cap_default_w = uw_to_w(HW("power1_cap_default"));
    for (i = 1; i <= 4; i++) {
        char f[24];
        snprintf(f, sizeof f, "temp%d_label", i);
        if (!t_line(HW(f), lb, sizeof lb)) continue;
        snprintf(f, sizeof f, "temp%d_input", i);
        if (!t_long(HW(f), &v)) continue;
        if (!strcmp(lb, "edge")) g->edge_c = (double)v / 1000.0;
        else if (!strcmp(lb, "junction")) g->junction_c = (double)v / 1000.0;
    }
    if (t_long(HW("power1_average"), &v) || t_long(HW("power1_input"), &v)) g->power_w = (double)v / 1e6;
    if (t_long(HW("freq1_input"), &v)) g->clock_mhz = (double)v / 1e6;
    if (t_long(HW("in0_input"), &v)) g->vddgfx_mv = (double)v;
    if (t_long(HW("fan1_input"), &v)) g->fan_rpm = (double)v;
#undef HW
}

/* the card's name: libdrm's amdgpu.ids ("7550,\tC0,\tAMD Radeon RX 9070 XT"), else pci.ids, else the IDs */
static void card_name(gtune_gpu *g)
{
    static const char *const vn[] = { "AMD", "NVIDIA", "Intel", "Graphics card" };
    char p[400], *t;
    int k = g->vendor_id == 0x1002 ? 0 : g->vendor_id == 0x10de ? 1 : g->vendor_id == 0x8086 ? 2 : 3;
    snprintf(g->name, sizeof g->name, "%s %s(%04x:%04x)", vn[k], k == 3 ? "" : "graphics card ", g->vendor_id, g->device_id);
    if (g->vendor_id == 0x1002 && (t = t_slurp(t_path(p, sizeof p, "/usr/share/libdrm/amdgpu.ids"), 512 * 1024, NULL, NULL))) {
        char *l = t;
        while (l && *l) {
            char *nl = strchr(l, '\n'), *a, *c;
            if (nl) *nl = 0;
            if (strtoul(l, &a, 16) == g->device_id && a == l + 4 && *a == ',') {
                a++;
                while (*a == ' ' || *a == '\t') a++;
                if (strtoul(a, &c, 16) == g->revision && *c == ',') {
                    c++;
                    while (*c == ' ' || *c == '\t') c++;
                    if (*c) { snprintf(g->name, sizeof g->name, "%s", c); free(t); return; }
                }
            }
            l = nl ? nl + 1 : NULL;
        }
        free(t);
    }
    if (!t_root()[0]) {   /* pci.ids, the same list the rest of SRS uses (not in tests: the names there are made up) */
        static srs_db db;
        static int loaded;
        srs_result r;
        char id[16], err[80];
        if (!loaded) loaded = srs_db_load(&db, BUS_PCI) ? -1 : 1;
        snprintf(id, sizeof id, "%04x:%04x", g->vendor_id, g->device_id);
        if (loaded == 1 && !srs_resolve(&db, id, MODE_TREE, &r, err, sizeof err) && r.matched >= 0 && r.resolved[0]) snprintf(g->name, sizeof g->name, "%.127s", r.resolved);
    }
}

static int card_cmp(const void *a, const void *b) { return atoi((const char *)a + 4) - atoi((const char *)b + 4); }

static void cards_read(gtune_status *st, int wake)
{
    char dir[300], names[32][16], p[520], dev[400], line[64];
    DIR *d;
    struct dirent *e;
    int n = 0, i;
    if (!(d = opendir(t_path(dir, sizeof dir, "/sys/class/drm")))) return;
    while ((e = readdir(d)) && n < 32) {
        const char *s = e->d_name + 4;
        if (strncmp(e->d_name, "card", 4) || !*s || strlen(e->d_name) > 9) continue;
        while (isdigit((unsigned char)*s)) s++;
        if (*s) continue;   /* card0-DP-1 is a connector, not a card */
        snprintf(names[n++], sizeof names[0], "%s", e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof names[0], card_cmp);
    for (i = 0; i < n && st->ngpu < GTN_MAXGPU; i++) {
        gtune_gpu *g = &st->gpu[st->ngpu];
        char *ue, *od;
        long v;
        int err;
        snprintf(dev, sizeof dev, "%s/%s/device", dir, names[i]);
        snprintf(p, sizeof p, "%s/vendor", dev);
        if (!t_long(p, &v)) continue;   /* a firmware framebuffer and the like: no PCI device behind it */
        memset(g, 0, sizeof *g);
        g->vendor_id = (unsigned)v;
        snprintf(g->card, sizeof g->card, "%s", names[i]);
        snprintf(p, sizeof p, "%s/device", dev);            if (t_long(p, &v)) g->device_id = (unsigned)v;
        snprintf(p, sizeof p, "%s/subsystem_vendor", dev);  if (t_long(p, &v)) g->sub_vendor = (unsigned)v;
        snprintf(p, sizeof p, "%s/subsystem_device", dev);  if (t_long(p, &v)) g->sub_device = (unsigned)v;
        snprintf(p, sizeof p, "%s/revision", dev);          if (t_long(p, &v)) g->revision = (unsigned)v;
        snprintf(p, sizeof p, "%s/uevent", dev);
        if ((ue = t_slurp(p, 2048, NULL, NULL))) {
            char *l = ue;
            while (l && *l) {
                char *nl = strchr(l, '\n');
                if (nl) *nl = 0;
                if (!strncmp(l, "DRIVER=", 7)) snprintf(g->driver, sizeof g->driver, "%s", l + 7);
                else if (!strncmp(l, "PCI_SLOT_NAME=", 14)) snprintf(g->pci, sizeof g->pci, "%s", l + 14);
                l = nl ? nl + 1 : NULL;
            }
            free(ue);
        }
        g->vendor = g->vendor_id == 0x1002 ? "amd" : g->vendor_id == 0x10de ? "nvidia" : g->vendor_id == 0x8086 ? "intel" : "other";
        g->vo_mv = g->sclk_off_mhz = g->mclk_max_mhz = g->cap_w = g->cap_min_w = g->cap_max_w = g->cap_default_w = GTN_NONE;
        g->sclk_max_mhz = g->r_sclk_abs[0] = g->r_sclk_abs[1] = GTN_NONE;
        g->r_vo[0] = g->r_vo[1] = g->r_sclk[0] = g->r_sclk[1] = g->r_mclk[0] = g->r_mclk[1] = GTN_NONE;
        g->tb_floor_mv = g->tb_vo_mv = g->tb_sclk_off = g->tb_mem_off = g->tb_cap_w = g->tb_cap_min_w = g->tb_cap_max_w = g->tb_cap_default_w = GTN_NONE;
        g->tb_r_vo[0] = g->tb_r_vo[1] = g->tb_r_sclk[0] = g->tb_r_sclk[1] = g->tb_r_mem[0] = g->tb_r_mem[1] = GTN_NONE;
        g->lact_vo = g->lact_sclk = g->lact_mclk_max = g->lact_cap_w = GTN_NONE;
        g->lact_matches = -1;
        g->edge_c = g->junction_c = g->power_w = g->clock_mhz = g->vddgfx_mv = g->fan_rpm = -1;
        card_name(g);
        snprintf(p, sizeof p, "%s/power/runtime_status", dev);
        g->asleep = t_line(p, line, sizeof line) && !strcmp(line, "suspended") && !wake;
        st->ngpu++;
        if (g->asleep) continue;   /* reading its sensors or its OverDrive table would wake it */
        hwmon_read(g, dev);
        snprintf(p, sizeof p, "%s/pp_od_clk_voltage", dev);
        if ((od = t_slurp(p, 16384, NULL, &err))) { od_parse(g, od); free(od); }
        else if (err == EACCES || err == EPERM) g->od = -1;
    }
}

/* ------------------------------------------------------------------ LACT: is it running, and what has it saved */
static int lact_process(void)
{
    char dir[300], p[400], comm[32];
    DIR *d = opendir(t_path(dir, sizeof dir, "/proc"));
    struct dirent *e;
    int hit = 0;
    if (!d) return 0;
    while (!hit && (e = readdir(d))) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        snprintf(p, sizeof p, "%s/%.40s/comm", dir, e->d_name);
        if (t_line(p, comm, sizeof comm) && (!strcmp(comm, "lact") || !strncmp(comm, "lactd", 5))) hit = 1;
    }
    closedir(d);
    return hit;
}

static void unquote(char *s)
{
    size_t n;
    while (*s == ' ') memmove(s, s + 1, strlen(s));
    n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\r')) s[--n] = 0;
    if (n >= 2 && ((s[0] == '\'' && s[n - 1] == '\'') || (s[0] == '"' && s[n - 1] == '"'))) { memmove(s, s + 1, n - 2); s[n - 2] = 0; }
}

/* LACT's settings file is YAML written by a program: nested maps by indentation, "key: value" lines. This reads only
   that shape (no lists, no flow style); anything else simply yields no values. LACT keeps settings in the top-level
   "gpus:" map and in "profiles.<name>.gpus"; "current_profile" names the one in force. A selected profile with no entry
   for this card means the base values are NOT in force, so nothing is shown for it. The card's key ends with its PCI
   address ("1002:7550-1EAE:8811-0000:03:00.0"). */
static void lact_parse(gtune_status *st, const char *text)
{
    gtune_lact *L = &st->lact;
    int pass;
    for (pass = 0; pass < 2; pass++) {
        char key[8][96], line[400];
        int ind[8], depth = 0;
        const char *p = text;
        while (*p) {
            size_t n = strcspn(p, "\n");
            char *c, *val, k[96];
            int in = 0, g;
            snprintf(line, sizeof line, "%.*s", (int)(n < sizeof line - 1 ? n : sizeof line - 1), p);
            p += n + (p[n] == '\n');
            while (line[in] == ' ') in++;
            if (!line[in] || line[in] == '#' || line[in] == '-') continue;
            if (line[in] == '\'' || line[in] == '"') {   /* a quoted key may hold a colon */
                char *q = strchr(line + in + 1, line[in]);
                c = q ? strchr(q, ':') : NULL;
            } else {
                c = line + in;   /* the key ends at ": " or at a colon that ends the line (the card's key has colons inside) */
                while ((c = strchr(c, ':')) && c[1] && c[1] != ' ') c++;
            }
            if (!c) continue;
            *c = 0;
            val = c + 1;
            snprintf(k, sizeof k, "%.95s", line + in);
            unquote(k);
            unquote(val);
            while (depth && ind[depth - 1] >= in) depth--;
            if (depth >= 8) continue;
            ind[depth] = in;
            snprintf(key[depth], sizeof key[0], "%s", k);
            depth++;
            if (!pass) {
                if (depth == 1 && !strcmp(k, "current_profile") && *val && strcmp(val, "null") && strcmp(val, "~")) { L->has_profile = 1; snprintf(L->profile, sizeof L->profile, "%s", val); }
                if (depth == 1 && !strcmp(k, "auto_switch_profiles") && !strcmp(val, "true")) L->auto_switch = 1;
                continue;
            }
            if (!*val) continue;
            {   /* gpus.<card>.<setting...>  or  profiles.<active>.gpus.<card>.<setting...> */
                int base = L->has_profile ? 2 : 0;
                const char *name;
                if (L->has_profile && (depth < 5 || strcmp(key[0], "profiles") || strcmp(key[1], L->profile))) continue;
                if (depth < base + 3 || strcmp(key[base], "gpus")) continue;
                for (g = 0; g < st->ngpu; g++) {
                    gtune_gpu *u = &st->gpu[g];
                    size_t kl = strlen(key[base + 1]), pl = strlen(u->pci);
                    int *dst = NULL;
                    if (!pl || kl < pl || strcasecmp(key[base + 1] + kl - pl, u->pci)) continue;
                    name = key[base + 2];
                    if (depth == base + 3) dst = !strcmp(name, "voltage_offset") ? &u->lact_vo : !strcmp(name, "max_memory_clock") ? &u->lact_mclk_max : !strcmp(name, "power_cap") ? &u->lact_cap_w : NULL;
                    else if (depth == base + 4 && !strcmp(name, "gpu_clock_offsets") && !strcmp(key[base + 3], "0")) dst = &u->lact_sclk;
                    if (dst) {
                        double d = 0;
                        if (t_num(val, &d) != val) { *dst = (int)(d < 0 ? d - 0.5 : d + 0.5); u->lact_have = 1; }
                    }
                }
            }
        }
    }
}

static void lact_read(gtune_status *st)
{
    gtune_lact *L = &st->lact;
    char p[400], *t;
    struct stat sb;
    size_t len = 0;
    int g;
    L->running = !stat(t_path(p, sizeof p, "/run/lactd.sock"), &sb) && lact_process();
    t_path(L->config, sizeof L->config, "/etc/lact/config.yaml");
    if ((t = t_slurp(L->config, 1024 * 1024, &len, NULL))) {
        L->config_read = 1;
        sha256_hex(t, len, L->sha256);
        lact_parse(st, t);
        free(t);
    }
    snprintf(L->config, sizeof L->config, "/etc/lact/config.yaml");   /* shown without the test folder */
    for (g = 0; g < st->ngpu; g++) {
        gtune_gpu *u = &st->gpu[g];
        int same = 1, any = 0;
        if (!u->lact_have || u->od != 1) continue;
#define CMP(l, c) if ((l) != GTN_NONE && (c) != GTN_NONE) { any = 1; if ((l) != (c)) same = 0; }
        CMP(u->lact_vo, u->vo_mv) CMP(u->lact_sclk, u->sclk_off_mhz) CMP(u->lact_mclk_max, u->mclk_max_mhz) CMP(u->lact_cap_w, u->cap_w)
#undef CMP
        if (any) u->lact_matches = same;
    }
}

/* ------------------------------------------------------------------ everything */
void gtune_read(gtune_status *st, int wake)
{
    memset(st, 0, sizeof *st);
    /* SRS's own GUI tests (their private bus, the all-items column dump) show this page too. Unless such a test brings
       a made-up /sys, nothing real is read: not the card, not Tux Burner's service, not LACT's settings file */
    if (!t_root()[0] && (getenv("SRS_TEST_PRIVATE_BUS") || getenv("SRS_TEST_ALIGN_DUMP"))) {
        st->testmode = 1;
        st->tb.daemon = GTD_UNCHECKED;
        st->tb.proto_major = st->tb.proto_minor = GTN_NONE;
        snprintf(st->tb.msg, sizeof st->tb.msg, "test mode: the real graphics card, Tux Burner's service and LACT are not read");
        return;
    }
    cards_read(st, wake);
    tb_read(st);
    lact_read(st);
}

static int has_controls(const gtune_gpu *g) { return g->od == 1 || g->tb; }

int gtune_pick(const gtune_status *st)
{
    int i;
    for (i = 0; i < st->ngpu; i++) if (has_controls(&st->gpu[i])) return i;
    return st->ngpu ? 0 : -1;
}

int gtune_find_pci(const gtune_status *st, const char *pci)
{
    int i;
    for (i = 0; i < st->ngpu; i++) if (!strcasecmp(st->gpu[i].pci, pci)) return i;
    return -1;
}

/* is ability k missing for this card? The abilities named amd-... are about AMD cards only */
static int missing_for(const gtune_tb *tb, const gtune_gpu *g, size_t k)
{
    if (!strstr(tb->missing, tb_needs[k].cap)) return 0;
    return strncmp(tb_needs[k].cap, "amd-", 4) || !g || !strcmp(g->tb ? g->tb_vendor : g->vendor, "amd");
}

/* the missing abilities, in words ("" = none) */
static void missing_words(const gtune_tb *tb, const gtune_gpu *g, char *out, size_t n)
{
    size_t k, o = 0;
    out[0] = 0;
    for (k = 0; k < sizeof tb_needs / sizeof tb_needs[0]; k++)
        if (missing_for(tb, g, k) && o < n) o += (size_t)snprintf(out + o, n - o, "%s%s", o ? ", " : "", tb_needs[k].words);
}

int gtune_can_write(const gtune_status *st, int idx, char *why, size_t n)
{
    const gtune_tb *tb = &st->tb;
    const gtune_gpu *g = idx >= 0 && idx < st->ngpu ? &st->gpu[idx] : NULL;
    char m[200];
    why[0] = 0;
    if (!g) snprintf(why, n, "no graphics card found");
    else if (g->asleep) snprintf(why, n, "the card is asleep (powered down by the kernel)");
    else if (!tb->cli) snprintf(why, n, "Tux Burner is not installed (tburn-cli is not on PATH)");
    else if (tb->daemon == GTD_ABSENT || tb->daemon == GTD_DOWN) snprintf(why, n, "Tux Burner's service is not running");
    else if (tb->daemon == GTD_DENIED) snprintf(why, n, "Tux Burner's service does not serve this user: %s", tb->msg);
    else if (tb->daemon == GTD_OLD) snprintf(why, n, "Tux Burner's service is a version this SRS cannot use: %s", tb->msg);
    else if (tb->daemon != GTD_RUNNING) snprintf(why, n, "%s", tb->msg[0] ? tb->msg : "Tux Burner's service could not be asked");
    else if (st->asleep_skip) snprintf(why, n, "a graphics card is asleep, so Tux Burner's service was not asked about the cards");
    else if (!g->tb) snprintf(why, n, "Tux Burner's service has no controls for this card");
    else if (!strcmp(g->tb_vendor, "amd") && !tb->cap_amd) snprintf(why, n, "this Tux Burner has no AMD tuning support");
    else { missing_words(tb, g, m, sizeof m); if (m[0]) snprintf(why, n, "needs a newer Tux Burner for: %s", m); }
    return !why[0];
}

/* ------------------------------------------------------------------ text (the tab and "gpu-tune status") */
static void val(char *b, size_t n, int v, const char *unit, int sign)
{
    if (v == GTN_NONE) snprintf(b, n, "not available");
    else snprintf(b, n, sign && v ? "%+d %s" : "%d %s", v, unit);
}

static void range(char *b, size_t n, const int r[2], const char *unit, int sign)
{
    if (r[0] == GTN_NONE || r[1] == GTN_NONE) snprintf(b, n, "not available");
    else {   /* signs on offsets, none on a plain 0 */
        char lo[24], hi[24];
        snprintf(lo, sizeof lo, sign && r[0] ? "%+d" : "%d", r[0]);
        snprintf(hi, sizeof hi, sign && r[1] ? "%+d" : "%d", r[1]);
        snprintf(b, n, "%s .. %s %s", lo, hi, unit);
    }
}

static const char *tb_state_words(const char *s)
{
    if (!strcmp(s, "idle")) return "idle";
    if (!strncmp(s, "runtime_profile_running", 23)) return "a Tux Burner profile is active";
    if (!strncmp(s, "auto_uv_scan_running", 20)) return "a search is running";
    if (!strncmp(s, "profile_verification_running", 28)) return "a profile check is running";
    return s;
}

/* one line each: Tux Burner, LACT */
static void render_programs(srs_out *o, const gtune_status *st)
{
    const gtune_tb *tb = &st->tb;
    const gtune_lact *L = &st->lact;
    char b[400];
    switch (tb->daemon) {
    case GTD_RUNNING: snprintf(b, sizeof b, "%s running (%s)", tb->version[0] ? tb->version : "version not given", tb_state_words(tb->state)); break;
    case GTD_ABSENT: snprintf(b, sizeof b, "%s", tb->cli ? "installed; its service is not running" : "not installed"); break;
    case GTD_DOWN: snprintf(b, sizeof b, "installed; its service does not answer (not running)"); break;
    case GTD_DENIED: snprintf(b, sizeof b, "its service does not serve this user"); break;
    case GTD_OLD: snprintf(b, sizeof b, "%s running, but a version this SRS cannot use", tb->version[0] ? tb->version : "a service is"); break;
    case GTD_UNCHECKED: snprintf(b, sizeof b, "not asked (test mode)"); break;
    default: snprintf(b, sizeof b, "its service gave no usable answer"); break;
    }
    so_kv(o, "Tux Burner", tb->daemon == GTD_RUNNING ? C_GREEN : tb->daemon == GTD_ABSENT && !tb->cli ? C_GRAY : C_ORANGE, b);
    if (L->running && L->has_profile) snprintf(b, sizeof b, "running, active profile \"%s\"%s", L->profile, L->auto_switch ? " (LACT switches profiles by itself)" : "");
    else if (L->running) snprintf(b, sizeof b, "running%s", L->config_read ? ", no profile selected (its base settings apply)" : "");
    else snprintf(b, sizeof b, "%s", L->config_read ? "not running (settings file found)" : "not running");
    so_kv(o, "LACT", L->running ? C_ORANGE : C_GRAY, b);
}

static void render_card(srs_out *o, const gtune_status *st, int idx)
{
    static const int tabs[] = { 250 };
    const gtune_gpu *g = &st->gpu[idx];
    const gtune_tb *tb = &st->tb;
    const gtune_lact *L = &st->lact;
    char b[500], a[80], c[240];
    so_tabs(o, tabs, 1);
    so_head(o, g->name);
    so_kvf(o, "Card", C_VALUE, "%s, PCI %s, driver %s", g->card, g->pci[0] ? g->pci : "-", g->driver[0] ? g->driver : "none");
    render_programs(o, st);
    if (g->tb) so_kvf(o, "Settings Back End", C_VALUE, "Tux Burner %s", !strcmp(g->tb_vendor, "amd") ? "AMD (OverDrive)" : !strcmp(g->tb_vendor, "nvidia") ? "NVIDIA" : g->tb_vendor);
    so_par(o);

    if (g->asleep) {
        so_note(o, "This card is asleep (powered down by the kernel to save energy). SRS did not wake it, so its settings are not "
                   "shown. Start a program on it, or use: sysreport-search gpu-tune status --wake");
        so_par(o);
        return;
    }

    so_b(o, C_VALUE, "Now On The Card"); so_par(o);
    if (g->od == 1) {
        val(b, sizeof b, g->vo_mv, "mV", 1);
        so_kv(o, "  Voltage Offset", C_VALUE, b);
        if (g->sclk_off_mhz != GTN_NONE || g->sclk_max_mhz == GTN_NONE) { val(b, sizeof b, g->sclk_off_mhz, "MHz", 1); so_kv(o, "  Core Clock Offset", C_VALUE, b); }
        if (g->sclk_max_mhz != GTN_NONE) { val(b, sizeof b, g->sclk_max_mhz, "MHz", 0); so_kv(o, "  Core Clock (Highest)", C_VALUE, b); }
        val(b, sizeof b, g->mclk_max_mhz, "MHz", 0);
        so_kv(o, "  Memory Clock (Highest)", C_VALUE, b);
    } else if (g->tb) {   /* no table of its own to read (NVIDIA): the values the service reports */
        val(b, sizeof b, g->tb_sclk_off, "MHz", 1);
        so_kv(o, "  Core Clock Offset", C_VALUE, b);
        val(b, sizeof b, g->tb_mem_off, "MHz", 1);
        so_kv(o, "  Memory Clock Offset", C_VALUE, b);
        if (g->tb_vo_mv != GTN_NONE) { val(b, sizeof b, g->tb_vo_mv, "mV", 1); so_kv(o, "  Voltage Offset", C_VALUE, b); }
        if (g->tb_locked) so_kv(o, "  Clock Control", C_ORANGE, "locked by the card");
    }
    if (g->cap_w != GTN_NONE || g->tb_cap_w != GTN_NONE) {
        val(b, sizeof b, g->cap_w != GTN_NONE ? g->cap_w : g->tb_cap_w, "W", 0);
        so_kv(o, "  Power Limit", C_VALUE, b);
    }
    if (g->od != 1 && !g->tb) {
        if (g->od == 2) so_note(o, "  This card uses the older OverDrive table (clock and voltage points). SRS does not support it yet.");
        else if (g->od == -1) so_note(o, "  The card's OverDrive table is there but cannot be read by this user.");
        else if (!strcmp(g->vendor, "amd"))
            so_note(o, "  This card has no OverDrive controls SRS can use here. On AMD cards OverDrive is switched on with the kernel "
                       "option amdgpu.ppfeaturemask; without it the driver shows no tuning table.");
        else if (!strcmp(g->vendor, "nvidia"))
            so_note(o, "  NVIDIA cards have no settings SRS can read by itself. With Tux Burner's service running, its NVIDIA back end "
                       "reports them here.");
        else so_note(o, "  Tuning this kind of card is not supported yet. The page stays read-only.");
    }
    if (L->running && g->lact_have) {
        if (g->lact_matches == 1) snprintf(b, sizeof b, "LACT%s%s%s (the card is at LACT's saved values)", L->has_profile ? " profile \"" : "", L->has_profile ? L->profile : "", L->has_profile ? "\"" : "");
        else if (g->lact_matches == 0) snprintf(b, sizeof b, "not LACT's saved values right now (see LACT's Saved Values below)");
        else b[0] = 0;
        if (b[0]) so_kv(o, "  Set By", g->lact_matches == 1 ? C_VALUE : C_ORANGE, b);
    }
    so_par(o);

    if (g->edge_c >= 0 || g->junction_c >= 0 || g->power_w >= 0 || g->clock_mhz >= 0) {
        so_b(o, C_VALUE, "Readings Right Now"); so_par(o);
        if (g->junction_c >= 0 || g->edge_c >= 0) {
            if (g->edge_c >= 0) snprintf(a, sizeof a, "%.0f C edge", g->edge_c); else a[0] = 0;
            if (g->junction_c >= 0) snprintf(c, sizeof c, "%s%.0f C hotspot", a[0] ? ", " : "", g->junction_c); else c[0] = 0;
            so_kvf(o, "  Temperature", C_VALUE, "%s%s", a, c);
        }
        if (g->power_w >= 0) so_kvf(o, "  Power Draw", C_VALUE, "%.0f W", g->power_w);
        if (g->clock_mhz >= 0) so_kvf(o, "  Core Clock", C_VALUE, "%.0f MHz", g->clock_mhz);
        if (g->vddgfx_mv >= 0) so_kvf(o, "  Core Voltage", C_VALUE, "%.0f mV", g->vddgfx_mv);
        if (g->fan_rpm >= 0) so_kvf(o, "  Fan", C_VALUE, "%.0f RPM", g->fan_rpm);
        so_par(o);
    }

    if (g->od == 1 || g->tb) {
        const int *rv = g->od == 1 ? g->r_vo : g->tb_r_vo, *rs = g->od == 1 ? g->r_sclk : g->tb_r_sclk;
        int cmin = g->cap_min_w != GTN_NONE ? g->cap_min_w : g->tb_cap_min_w, cmax = g->cap_max_w != GTN_NONE ? g->cap_max_w : g->tb_cap_max_w;
        int cdef = g->cap_default_w != GTN_NONE ? g->cap_default_w : g->tb_cap_default_w;
        so_b(o, C_VALUE, "Limits"); so_par(o);
        if (g->tb_floor_mv != GTN_NONE) {
            so_kvf(o, "  Lowest Allowed Voltage Offset", C_VALUE, "%+d mV (Tux Burner's floor%s%s; change it in Tux Burner)", g->tb_floor_mv,
                   g->tb_floor_source[0] ? ", " : "", !strcmp(g->tb_floor_source, "default") ? "its default" : g->tb_floor_source);
        } else if (g->od == 1) so_kv(o, "  Lowest Allowed Voltage Offset", C_GRAY, "kept by Tux Burner (not known without its service)");
        if (rv[0] != GTN_NONE) { range(b, sizeof b, rv, "mV", 1); so_kv(o, "  Driver Range, Voltage Offset", C_VALUE, b); }
        if (rs[0] != GTN_NONE) { range(b, sizeof b, rs, "MHz", 1); so_kv(o, "  Driver Range, Core Clock Offset", C_VALUE, b); }
        if (g->od == 1 && g->r_sclk_abs[0] != GTN_NONE) { range(b, sizeof b, g->r_sclk_abs, "MHz", 0); so_kv(o, "  Driver Range, Core Clock", C_VALUE, b); }
        if (g->od == 1 && g->r_mclk[0] != GTN_NONE) { range(b, sizeof b, g->r_mclk, "MHz", 0); so_kv(o, "  Driver Range, Memory Clock", C_VALUE, b); }
        else if (g->od != 1 && g->tb_r_mem[0] != GTN_NONE) { range(b, sizeof b, g->tb_r_mem, "MHz", 1); so_kv(o, "  Driver Range, Memory Clock Offset", C_VALUE, b); }
        if (cmin != GTN_NONE && cmax != GTN_NONE) {
            if (cdef != GTN_NONE) so_kvf(o, "  Driver Range, Power Limit", C_VALUE, "%d .. %d W (factory setting %d W)", cmin, cmax, cdef);
            else so_kvf(o, "  Driver Range, Power Limit", C_VALUE, "%d .. %d W", cmin, cmax);
        }
        so_note(o, "  The driver range is what the card's driver accepts, not what is safe. Tux Burner keeps its own, tighter limits.");
        so_par(o);
    }

    if (g->lact_have) {
        snprintf(b, sizeof b, "LACT's Saved Values (%s%s%s)", L->has_profile ? "profile \"" : "base settings", L->has_profile ? L->profile : "", L->has_profile ? "\"" : "");
        so_b(o, C_VALUE, b); so_par(o);
        if (g->lact_vo != GTN_NONE) { val(b, sizeof b, g->lact_vo, "mV", 1); so_kv(o, "  Voltage Offset", g->vo_mv != GTN_NONE && g->vo_mv != g->lact_vo ? C_ORANGE : C_VALUE, b); }
        if (g->lact_sclk != GTN_NONE) { val(b, sizeof b, g->lact_sclk, "MHz", 1); so_kv(o, "  Core Clock Offset", g->sclk_off_mhz != GTN_NONE && g->sclk_off_mhz != g->lact_sclk ? C_ORANGE : C_VALUE, b); }
        if (g->lact_mclk_max != GTN_NONE) { val(b, sizeof b, g->lact_mclk_max, "MHz", 0); so_kv(o, "  Memory Clock (Highest)", g->mclk_max_mhz != GTN_NONE && g->mclk_max_mhz != g->lact_mclk_max ? C_ORANGE : C_VALUE, b); }
        if (g->lact_cap_w != GTN_NONE) { val(b, sizeof b, g->lact_cap_w, "W", 0); so_kv(o, "  Power Limit", g->cap_w != GTN_NONE && g->cap_w != g->lact_cap_w ? C_ORANGE : C_VALUE, b); }
        if (g->lact_matches == 0) so_t(o, C_ORANGE, "  The card is not at these values right now: something else changed it after LACT applied them."), so_par(o);
        so_note(o, "  Read from LACT's settings file; SRS never changes that file and never contacts LACT's service.");
        so_par(o);
    }
    if (L->running) {
        so_t(o, C_ORANGE, "LACT is running and owns this card's settings. SRS will not change LACT's saved settings.");
        so_par(o);
        so_par(o);
    }

    so_b(o, C_VALUE, "Changing Settings"); so_par(o);
    so_note(o, "This page only shows. SRS itself never writes to a graphics card.");
    if (!tb->cli && (tb->daemon == GTD_ABSENT || tb->daemon == GTD_UNCHECKED)) {
        so_t(o, C_DEF, "Changing GPU settings needs Tux Burner, a separate free program that holds the safety limits. Reading works without it.");
        so_par(o);
        so_kv(o, "  Install (Arch Linux)", C_VALUE, "the package tux-burner from the AUR");
        so_kv(o, "  Install (Other Systems)", C_VALUE, TB_URL);
    } else if (tb->daemon == GTD_ABSENT || tb->daemon == GTD_DOWN) {
        so_t(o, C_ORANGE, "Tux Burner's service is not running. Install it from Tux Burner (one password prompt) or run:");
        so_par(o);
        so_m(o, C_VALUE, "  sudo tux-burner-cli --install-systemd-service"); so_par(o);
        so_note(o, "SRS does not install the service itself.");
    } else if (tb->daemon == GTD_DENIED) {
        snprintf(b, sizeof b, "Tux Burner's service answered: \"%s\".", tb->msg);
        so_t(o, C_ORANGE, b); so_par(o);
        so_t(o, C_DEF, "The service serves only the user it was installed for. To let this user in, install the service again from "
                       "Tux Burner while logged in as this user:");
        so_par(o);
        so_m(o, C_VALUE, "  sudo tux-burner-cli --install-systemd-service"); so_par(o);
    } else if (tb->daemon == GTD_OLD) {
        snprintf(b, sizeof b, "This Tux Burner cannot be used by this SRS: %s. Install matching versions of both.", tb->msg);
        so_t(o, C_ORANGE, b); so_par(o);
    } else if (tb->daemon == GTD_BAD) {
        so_t(o, C_ORANGE, tb->msg[0] ? tb->msg : "Tux Burner's service gave no usable answer."); so_par(o);
    } else if (tb->daemon == GTD_RUNNING) {
        if (!tb->cli) so_t(o, C_ORANGE, "Tux Burner's service is running, but its command line tool (tburn-cli) is not on PATH."), so_par(o);
        if (st->asleep_skip)
            so_note(o, "A graphics card in this PC is asleep, so Tux Burner's service was not asked about the cards (the question "
                       "could wake it). To ask anyway: sysreport-search gpu-tune status --wake");
        else if (!g->tb) so_note(o, "Tux Burner's service has no controls for this card.");
        missing_words(tb, g, c, sizeof c);
        if (g->tb && c[0]) {
            snprintf(b, sizeof b, "Searches from SRS need a newer Tux Burner for: %s.", c);
            so_t(o, C_DEF, b); so_par(o);
        }
        so_note(o, "Controls for trying a setting, searching for a safe undervolt or overclock and saving profiles come in a later "
                   "version of SRS. Until then use Tux Burner's own window.");
    }
    so_par(o);
}

void gtune_render(srs_out *o, const gtune_status *st, int idx)
{
    int i;
    so_siv_header(o, "GPU Tuning (read-only: shows what the card is set to; nothing here changes it)");
    if (!st->ngpu) {
        static const int tabs[] = { 250 };
        so_tabs(o, tabs, 1);
        if (st->testmode) so_note(o, "Test mode: the real graphics card, Tux Burner's service and LACT are not read.");
        else so_note(o, "No graphics card found (nothing under /sys/class/drm with a PCI device).");
        so_par(o);
        render_programs(o, st);
        return;
    }
    for (i = 0; i < st->ngpu; i++) if (idx < 0 || idx == i) render_card(o, st, i);
    if (idx >= 0 && st->ngpu > 1) {
        char b[160];
        snprintf(b, sizeof b, "%d more graphics card%s: --all shows every card, --pci ADDR one of them.", st->ngpu - 1, st->ngpu == 2 ? "" : "s");
        so_note(o, b);
    }
    so_board_footer(o);
}

void gtune_section(srs_out *o)
{
    gtune_status *st = calloc(1, sizeof *st);
    if (!st) return;
    gtune_read(st, 0);
    gtune_render(o, st, -1);
    free(st);
}

/* ------------------------------------------------------------------ JSON ("gpu-tune status --json") */
static void j_str(FILE *f, const char *s)
{
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    fputc('"', f);
    for (; *p; p++) {
        if (*p == '"' || *p == '\\') { fputc('\\', f); fputc(*p, f); }
        else if (*p < 0x20) fprintf(f, "\\u%04x", *p);
        else fputc(*p, f);
    }
    fputc('"', f);
}
static void j_int(FILE *f, const char *k, int v) { if (v == GTN_NONE) fprintf(f, "\"%s\":null", k); else fprintf(f, "\"%s\":%d", k, v); }
static void j_pair(FILE *f, const char *k, int a, int b) { if (a == GTN_NONE || b == GTN_NONE) fprintf(f, "\"%s\":null", k); else fprintf(f, "\"%s\":[%d,%d]", k, a, b); }
static void j_dbl(FILE *f, const char *k, double v) { if (v < 0) fprintf(f, "\"%s\":null", k); else fprintf(f, "\"%s\":%.0f", k, v); }
static void j_kstr(FILE *f, const char *k, const char *s) { fprintf(f, "\"%s\":", k); if (s && *s) j_str(f, s); else fputs("null", f); }

static void json_card(FILE *f, const gtune_status *st, int idx)
{
    static const char *const dstate[] = { "not-running", "not-answering", "not-allowed", "no-answer", "unusable-version", "running", "not-checked" };
    const gtune_gpu *g = &st->gpu[idx];
    const gtune_tb *tb = &st->tb;
    const gtune_lact *L = &st->lact;
    char why[300];
    size_t k;
    int can = gtune_can_write(st, idx, why, sizeof why), first = 1, od = g->od == 1;
    fputs("{\"gpu\":{", f);
    j_kstr(f, "name", g->name); fputc(',', f);
    j_kstr(f, "pci", g->pci); fputc(',', f);
    j_kstr(f, "driver", g->driver); fputc(',', f);
    j_kstr(f, "card", g->card);
    fprintf(f, ",\"vendor\":\"%s\",\"asleep\":%s,\"overdrive\":\"%s\",\"backend\":", g->vendor, g->asleep ? "true" : "false",
            g->od == 1 ? "offsets" : g->od == 2 ? "older-table" : g->od == -1 ? "not-readable" : "none");   /* offsets: a voltage offset */
    if (g->tb) fprintf(f, "\"tux-burner-%s\"", g->tb_vendor); else fputs("null", f);
    fputs("},\"now\":{", f);
    j_int(f, "voltage_offset_mv", od ? g->vo_mv : g->tb_vo_mv); fputc(',', f);
    j_int(f, "sclk_offset_mhz", od ? g->sclk_off_mhz : g->tb_sclk_off); fputc(',', f);
    j_int(f, "sclk_max_mhz", g->sclk_max_mhz); fputc(',', f);
    j_int(f, "mclk_max_mhz", g->mclk_max_mhz); fputc(',', f);
    j_int(f, "mclk_offset_mhz", od ? GTN_NONE : g->tb_mem_off); fputc(',', f);
    j_int(f, "power_cap_w", g->cap_w != GTN_NONE ? g->cap_w : g->tb_cap_w);
    fputs("},\"live\":{", f);
    j_dbl(f, "edge_c", g->edge_c); fputc(',', f);
    j_dbl(f, "junction_c", g->junction_c); fputc(',', f);
    j_dbl(f, "power_w", g->power_w); fputc(',', f);
    j_dbl(f, "clock_mhz", g->clock_mhz); fputc(',', f);
    j_dbl(f, "vddgfx_mv", g->vddgfx_mv); fputc(',', f);
    j_dbl(f, "fan_rpm", g->fan_rpm);
    fputs("},\"ranges\":{", f);
    j_pair(f, "voltage_offset_mv", od ? g->r_vo[0] : g->tb_r_vo[0], od ? g->r_vo[1] : g->tb_r_vo[1]); fputc(',', f);
    j_pair(f, "sclk_offset_mhz", od ? g->r_sclk[0] : g->tb_r_sclk[0], od ? g->r_sclk[1] : g->tb_r_sclk[1]); fputc(',', f);
    j_pair(f, "sclk_mhz", g->r_sclk_abs[0], g->r_sclk_abs[1]); fputc(',', f);
    j_pair(f, "mclk_mhz", g->r_mclk[0], g->r_mclk[1]); fputc(',', f);
    j_pair(f, "mclk_offset_mhz", od ? GTN_NONE : g->tb_r_mem[0], od ? GTN_NONE : g->tb_r_mem[1]); fputc(',', f);
    j_pair(f, "power_cap_w", g->cap_min_w != GTN_NONE ? g->cap_min_w : g->tb_cap_min_w, g->cap_max_w != GTN_NONE ? g->cap_max_w : g->tb_cap_max_w); fputc(',', f);
    j_int(f, "power_cap_default_w", g->cap_default_w != GTN_NONE ? g->cap_default_w : g->tb_cap_default_w);
    /* suggest_floor_mv and sclk_ceiling_mhz: Tux Burner does not report them yet, so they are null (never a guess) */
    fputs("},\"limits\":{", f);
    j_int(f, "voltage_floor_mv", g->tb_floor_mv); fputc(',', f);
    j_kstr(f, "voltage_floor_source", g->tb_floor_mv != GTN_NONE ? g->tb_floor_source : ""); fputc(',', f);
    fputs("\"suggest_floor_mv\":null,\"sclk_ceiling_mhz\":null},\"tux_burner\":{", f);
    fprintf(f, "\"installed\":%s,", tb->cli ? "true" : "false");
    j_kstr(f, "cli", tb->cli_path);
    fprintf(f, ",\"daemon\":\"%s\",", dstate[tb->daemon >= 0 && tb->daemon <= GTD_UNCHECKED ? tb->daemon : GTD_BAD]);
    j_kstr(f, "version", tb->version); fputc(',', f);
    j_kstr(f, "build", tb->build); fputc(',', f);
    j_kstr(f, "state", tb->state); fputc(',', f);
    if (tb->proto_major == GTN_NONE) fputs("\"protocol\":null,", f);
    else fprintf(f, "\"protocol\":\"%d.%d\",", tb->proto_major, tb->proto_minor == GTN_NONE ? 0 : tb->proto_minor);
    j_kstr(f, "message", tb->msg);
    fprintf(f, ",\"serves_this_card\":%s,\"missing_capabilities\":[", g->tb ? "true" : "false");
    for (k = 0; k < sizeof tb_needs / sizeof tb_needs[0]; k++)
        if (missing_for(tb, g, k)) { fprintf(f, "%s\"%s\"", first ? "" : ",", tb_needs[k].cap); first = 0; }
    fprintf(f, "]},\"lact\":{\"running\":%s,", L->running ? "true" : "false");
    j_kstr(f, "active_profile", L->has_profile ? L->profile : ""); fputc(',', f);
    if (g->lact_have) {
        fputs("\"active_values\":{", f);
        j_int(f, "voltage_offset_mv", g->lact_vo); fputc(',', f);
        j_int(f, "sclk_offset_mhz", g->lact_sclk); fputc(',', f);
        j_int(f, "mclk_max_mhz", g->lact_mclk_max); fputc(',', f);
        j_int(f, "power_cap_w", g->lact_cap_w);
        fputs("},", f);
    } else fputs("\"active_values\":null,", f);
    fprintf(f, "\"matches_card\":%s,", g->lact_matches == 1 ? "true" : g->lact_matches == 0 ? "false" : "null");
    j_kstr(f, "config_sha256", L->config_read ? L->sha256 : "");
    fprintf(f, "},\"can_write\":%s,\"why_not\":", can ? "true" : "false");
    j_str(f, why);
    fputc('}', f);
}

void gtune_json(FILE *f, const gtune_status *st, int idx)
{
    int i, first = 1;
    if (idx >= 0) { json_card(f, st, idx); fputc('\n', f); return; }
    fputc('[', f);
    for (i = 0; i < st->ngpu; i++) { if (!first) fputc(',', f); first = 0; json_card(f, st, i); }
    fputs("]\n", f);
}
