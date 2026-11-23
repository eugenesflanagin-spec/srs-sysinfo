/* kmsg_watch.c - the kernel-log reader behind gpu-burn --watch-kernel (see kmsg_watch.h).
 *
 * The patterns are the driver messages of amdgpu (ring timeout, GPU reset, MODE1 / MODE2 reset, VRAM lost, device lost
 * from the bus, parser error), NVIDIA's "NVRM: Xid" lines and, best effort, i915 / xe. They are in ONE table below,
 * with tests (tests/gpu-burn-json-test.sh, tests/kmsg_fixtures). Kernel wording changes between versions: a driver error
 * line that is not in the table is kept and shown ("other"), never silently dropped.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#define _GNU_SOURCE
#include "kmsg_watch.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct kw {
    int fd;
    int records;                 /* /dev/kmsg: one record per read; a text file: a stream of lines */
    double t0;
    char buf[8192];              /* a text file's unfinished last line */
    size_t len;
    int nev, nother;
    kw_event ev[KW_MAXEV];
    char other[KW_MAXOTHER][200];
    const char *otherp[KW_MAXOTHER];
};

static double kw_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* strstr without regard to case (the drivers write "MODE1 reset" and "GPU mode1 reset"). A needle that starts with a
   letter must start a word: "ring " is not found in "during " */
static const char *has(const char *s, const char *needle)
{
    size_t n = strlen(needle);
    const char *p;
    for (p = s; *p; p++)
        if (!strncasecmp(p, needle, n) && (p == s || !isalpha((unsigned char)needle[0]) || !isalpha((unsigned char)p[-1]))) return p;
    return NULL;
}

/* ---- the pattern table: a line is a GPU line when it has one of the driver words, and then the first row whose
   needles are all in it names the kind ---- */
static const char *const kw_drivers[] = { "amdgpu", "radeon", "[drm", "NVRM", "i915", " xe ", "nouveau", NULL };
static const struct { const char *kind, *a, *b; } kw_rows[] = {
    { "ring-timeout",   "ring ",                         " timeout" },    /* amdgpu / radeon: ring gfx_0.0.0 timeout */
    { "gpu-reset-done", "GPU reset",                     "succeeded" },   /* amdgpu: GPU reset(2) succeeded! */
    { "gpu-reset-done", "GPU reset end",                 NULL },          /* amdgpu: GPU reset end with ret = 0 */
    { "gpu-reset",      "GPU reset begin",               NULL },          /* amdgpu: GPU reset begin! */
    { "gpu-reset",      "MODE1 reset",                   NULL },          /* amdgpu: MODE1 reset / GPU mode1 reset */
    { "gpu-reset",      "MODE2 reset",                   NULL },
    { "gpu-reset",      "VRAM is lost",                  NULL },          /* amdgpu: VRAM is lost due to GPU reset! */
    { "device-lost",    "device lost from bus",          NULL },          /* amdgpu */
    { "parser-error",   "Failed to initialize parser",   NULL },          /* amdgpu, after a reset: a program's old context */
    { "nvidia-xid",     "NVRM: Xid",                     NULL },          /* NVIDIA: every Xid report */
    { "gpu-hang",       "GPU HANG",                      NULL },          /* i915 */
    { "gpu-reset",      "Resetting chip",                NULL },          /* i915 */
    { "gpu-reset",      "Resetting rcs",                 NULL },          /* i915: engine reset */
    { "gpu-reset",      "Engine reset",                  NULL },          /* xe */
};

/* "0000:03:00.0" anywhere in the line (NVIDIA writes "PCI:0000:01:00", with no function number) */
static void find_pci(const char *s, char *out, size_t n)
{
    out[0] = 0;
    for (; *s; s++) {
        int i, ok = 1;
        static const char shape[] = "hhhh:hh:hh";
        for (i = 0; shape[i] && ok; i++) ok = shape[i] == 'h' ? isxdigit((unsigned char)s[i]) != 0 : s[i] == ':';
        if (!ok) continue;
        if (s[10] == '.' && s[11] >= '0' && s[11] <= '7' && !isxdigit((unsigned char)s[12])) i = 12;
        else if (!isxdigit((unsigned char)s[10]) && s[10] != '.') i = 10;
        else continue;
        if ((size_t)i >= n) return;
        memcpy(out, s, (size_t)i);
        out[i] = 0;
        for (i = 0; out[i]; i++) out[i] = (char)tolower((unsigned char)out[i]);
        return;
    }
}

/* the word after a needle, up to a space, comma or end ("ring gfx_0.0.0 timeout" -> gfx_0.0.0) */
static void word_after(const char *s, const char *needle, char *out, size_t n)
{
    const char *p = has(s, needle);
    size_t k = 0;
    out[0] = 0;
    if (!p) return;
    p += strlen(needle);
    while (*p == ' ' || *p == ':' || *p == '(') p++;
    while (*p && *p != ' ' && *p != ',' && *p != ')' && *p != '\n' && k + 1 < n) out[k++] = *p++;
    out[k] = 0;
}

const char *kw_classify(const char *line, char *pci, size_t pn, char *detail, size_t dn)
{
    size_t i;
    int gpu = 0;
    if (pci && pn) pci[0] = 0;
    if (detail && dn) detail[0] = 0;
    for (i = 0; kw_drivers[i]; i++) if (strstr(line, kw_drivers[i])) gpu = 1;
    if (!gpu) return NULL;
    for (i = 0; i < sizeof kw_rows / sizeof kw_rows[0]; i++) {
        const char *k = kw_rows[i].kind;
        if (!has(line, kw_rows[i].a) || (kw_rows[i].b && !has(line, kw_rows[i].b))) continue;
        if (pci && pn) find_pci(line, pci, pn);
        if (detail && dn) {
            if (!strcmp(k, "ring-timeout")) word_after(line, "ring ", detail, dn);
            else if (!strcmp(k, "nvidia-xid")) {   /* NVRM: Xid (PCI:0000:01:00): 79, pid=... */
                const char *p = has(line, "): ");
                if (p) snprintf(detail, dn, "Xid %d", atoi(p + 3));
            } else if (has(line, "MODE1")) snprintf(detail, dn, "MODE1");
            else if (has(line, "MODE2")) snprintf(detail, dn, "MODE2");
        }
        return k;
    }
    return NULL;
}

/* the message part of a line: after "prio,seq,time,flags;" (/dev/kmsg) or after a "[  123.456] " time stamp (dmesg) */
static const char *msg_of(const char *l)
{
    const char *p = l;
    if (isdigit((unsigned char)*p)) {
        const char *semi = strchr(l, ';');
        const char *q;
        for (q = l; semi && q < semi && (isdigit((unsigned char)*q) || *q == ',' || *q == '-' || isalpha((unsigned char)*q)); q++) ;
        if (semi && q == semi) return semi + 1;
    }
    if (*p == '[') {
        const char *e = strchr(p, ']');
        const char *q;
        for (q = p + 1; e && q < e && (isdigit((unsigned char)*q) || *q == '.' || *q == ' '); q++) ;
        if (e && q == e) { p = e + 1; while (*p == ' ') p++; }
    }
    return p;
}

static void take_line(kw *k, const char *raw)
{
    const char *m = msg_of(raw), *kind, *p;
    char pci[16], detail[48];
    kw_event *e, *last = k->nev ? &k->ev[k->nev - 1] : NULL;
    /* the driver names the program it blames on its own line, right after the ring timeout */
    if (last && !last->process[0] && !strcmp(last->kind, "ring-timeout") && strstr(m, "amdgpu") &&
        ((p = has(m, "process ")) || (p = has(m, "Process "))) && has(m, " pid ")) {
        size_t n = 0;
        p += 8;
        if (!strncasecmp(p, "information: process ", 21)) p += 21;
        while (*p && *p != ' ' && n + 1 < sizeof last->process) last->process[n++] = *p++;
        last->process[n] = 0;
        return;
    }
    kind = kw_classify(m, pci, sizeof pci, detail, sizeof detail);
    if (!kind) {
        if (k->nother < KW_MAXOTHER && (strstr(m, "amdgpu") || strstr(m, "NVRM") || strstr(m, "i915") || strstr(m, "[drm")) && strstr(m, "ERROR")) {
            snprintf(k->other[k->nother], sizeof k->other[0], "%.199s", m);
            k->otherp[k->nother] = k->other[k->nother];
            k->nother++;
        }
        return;
    }
    if (has(m, "VRAM is lost")) {   /* part of the reset that is already listed: mark it, no line of its own */
        int i;
        for (i = k->nev - 1; i >= 0; i--)
            if (!strncmp(k->ev[i].kind, "gpu-reset", 9) && (!pci[0] || !k->ev[i].pci[0] || !strcmp(pci, k->ev[i].pci))) { k->ev[i].vram_lost = 1; return; }
    }
    if (!strcmp(kind, "gpu-reset")) {   /* one reset is several lines (begin, MODE1 reset, GPU mode1 reset): one event */
        int i;
        for (i = k->nev - 1; i >= 0; i--) {
            kw_event *r = &k->ev[i];
            if (kw_now() - k->t0 - r->t_s > 10) break;
            if (pci[0] && r->pci[0] && strcmp(pci, r->pci)) continue;
            if (!strcmp(r->kind, "gpu-reset-done")) break;   /* that reset is over: this is a new one */
            if (strcmp(r->kind, "gpu-reset")) continue;
            if (!r->detail[0]) snprintf(r->detail, sizeof r->detail, "%s", detail);
            return;
        }
    }
    /* a line that names no card (a program's lost context after a reset) belongs to the card of the event just before it */
    if (!pci[0] && last && last->pci[0] && kw_now() - k->t0 - last->t_s <= 10) snprintf(pci, sizeof pci, "%s", last->pci);
    if (k->nev >= KW_MAXEV) return;
    e = &k->ev[k->nev++];
    memset(e, 0, sizeof *e);
    e->t_s = kw_now() - k->t0;
    e->kind = kind;
    snprintf(e->pci, sizeof e->pci, "%s", pci);
    snprintf(e->detail, sizeof e->detail, "%s", detail);
    snprintf(e->text, sizeof e->text, "%.199s", m);
    e->vram_lost = has(m, "VRAM is lost") != NULL;
    if (!strcmp(kind, "nvidia-xid") && (p = has(m, "name="))) {   /* NVIDIA names the program in the Xid line itself */
        size_t n = 0;
        p += 5;
        while (*p && *p != ',' && *p != ' ' && n + 1 < sizeof e->process) e->process[n++] = *p++;
        e->process[n] = 0;
        if (!strcmp(e->process, "<unknown>")) e->process[0] = 0;
    }
}

kw *kw_open(char *why, size_t wn)
{
    const char *t = getenv("SRS_TEST_KMSG");
    kw *k = calloc(1, sizeof *k);
    if (why && wn) why[0] = 0;
    if (!k) { if (why) snprintf(why, wn, "out of memory"); return NULL; }
    if (t && *t) {   /* tests: a text file, read from its start (seeking to the end would see nothing) and followed */
        k->fd = open(t, O_RDONLY | O_CLOEXEC);
    } else {
        k->fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        k->records = 1;
        if (k->fd >= 0) lseek(k->fd, 0, SEEK_END);   /* only what the kernel writes from now on */
    }
    if (k->fd < 0) {
        if (why) snprintf(why, wn, "Kernel log not readable (%s: %s): only the test's own result is checked.", t && *t ? t : "/dev/kmsg", strerror(errno));
        free(k);
        return NULL;
    }
    k->t0 = kw_now();
    return k;
}

int kw_poll(kw *k)
{
    char rec[4096];
    ssize_t r;
    int guard = 4096;   /* a flood of kernel lines must not keep the caller here */
    if (!k) return 0;
    if (k->records) {
        while (guard-- > 0) {
            r = read(k->fd, rec, sizeof rec - 1);
            if (r < 0 && errno == EPIPE) continue;   /* the log overran us: the next read gives the oldest line still there */
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) break;                       /* EAGAIN: nothing new */
            rec[r] = 0;
            rec[strcspn(rec, "\n")] = 0;             /* the lines after the first are the record's KEY=value pairs */
            take_line(k, rec);
        }
        return k->nev;
    }
    while (guard-- > 0 && (r = read(k->fd, k->buf + k->len, sizeof k->buf - 1 - k->len)) > 0) {
        char *nl, *s = k->buf;
        k->len += (size_t)r;
        k->buf[k->len] = 0;
        while ((nl = strchr(s, '\n'))) { *nl = 0; take_line(k, s); s = nl + 1; }
        k->len = strlen(s);
        memmove(k->buf, s, k->len + 1);
        if (k->len >= sizeof k->buf - 1) k->len = 0;   /* one line longer than the buffer: drop it */
    }
    return k->nev;
}

int kw_events(const kw *k, const kw_event **ev) { if (ev) *ev = k ? k->ev : NULL; return k ? k->nev : 0; }
int kw_others(const kw *k, const char *const **lines) { if (lines) *lines = k ? k->otherp : NULL; return k ? k->nother : 0; }

int kw_event_for(const kw_event *e, const char *pci)
{
    size_t n;
    if (!pci || !pci[0] || !e->pci[0]) return 1;
    n = strlen(e->pci);                      /* NVIDIA's address has no function number: compare what the line gives */
    return !strncasecmp(e->pci, pci, n < strlen(pci) ? n : strlen(pci));
}

void kw_summary(const kw *k, const char *pci, char *out, size_t n)
{
    static const struct { const char *kind, *words; } w[] = {
        { "ring-timeout", "the GPU stopped answering, ring timeout" }, { "gpu-reset", "GPU reset" }, { "gpu-reset-done", "GPU reset finished" },
        { "device-lost", "the GPU was lost from the bus" }, { "parser-error", "a program's GPU context was lost" },
        { "nvidia-xid", "NVIDIA driver error" }, { "gpu-hang", "GPU hang" } };
    size_t i, o = 0;
    int j, first = 1, reset = 0;
    out[0] = 0;
    if (!k) return;
    for (i = 0; i < sizeof w / sizeof w[0]; i++) {
        const kw_event *hit = NULL;
        int vram = 0;
        for (j = 0; j < k->nev; j++)
            if (!strcmp(k->ev[j].kind, w[i].kind) && kw_event_for(&k->ev[j], pci)) { if (!hit) hit = &k->ev[j]; vram |= k->ev[j].vram_lost; }
            /* "VRAM is lost" that came after "reset succeeded" was marked on the reset-done event: it belongs to the reset */
            else if (!strcmp(w[i].kind, "gpu-reset") && !strcmp(k->ev[j].kind, "gpu-reset-done") && kw_event_for(&k->ev[j], pci)) vram |= k->ev[j].vram_lost;
        if (!hit) continue;
        if (!strcmp(w[i].kind, "gpu-reset")) reset = 1;
        if (!strcmp(w[i].kind, "gpu-reset-done") && reset) continue;   /* said already */
        {
            char in[160] = "";
            size_t l = 0;
            if (hit->detail[0]) l += (size_t)snprintf(in + l, sizeof in - l, "%s", hit->detail);
            if (hit->process[0]) l += (size_t)snprintf(in + l, sizeof in - l, "%sin %s", l ? ", " : "", hit->process);
            if (vram) l += (size_t)snprintf(in + l, sizeof in - l, "%svideo memory wiped", l ? ", " : "");
            o += (size_t)snprintf(out + o, o < n ? n - o : 0, "%s%s%s%s%s", first ? "kernel log: " : ", then ", w[i].words, l ? " (" : "", in, l ? ")" : "");
        }
        first = 0;
        if (o >= n) break;
    }
}

void kw_close(kw *k)
{
    if (!k) return;
    close(k->fd);
    free(k);
}
