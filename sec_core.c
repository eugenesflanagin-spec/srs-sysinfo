/* sec_core.c - section registry, output helpers, text backend, -save, data helpers. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "sec.h"
#include "srs.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ======================================================================
 * output helpers
 * ====================================================================== */
void so_tabs(srs_out *o, const int *px, int n) { o->tabs(o, px, n); }
void so_t(srs_out *o, int c, const char *s) { if (s && *s) o->text(o, c, 0, 0, s); }
void so_b(srs_out *o, int c, const char *s) { if (s && *s) o->text(o, c, 1, 0, s); }
void so_m(srs_out *o, int c, const char *s) { if (s && *s) o->text(o, c, 0, 1, s); }
void so_tab(srs_out *o) { o->tab(o); }
void so_par(srs_out *o) { o->par(o); }

void so_f(srs_out *o, int c, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    so_t(o, c, buf);
}

void so_kv(srs_out *o, const char *k, int c, const char *v)
{
    so_t(o, C_LABEL, k);
    so_tab(o);
    so_t(o, c, v && *v ? v : "-");
    so_par(o);
}

void so_kvf(srs_out *o, const char *k, int c, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    so_kv(o, k, c, buf);
}

void so_row(srs_out *o, int n, const char *const *cells, const int *colors)
{
    int i;
    for (i = 0; i < n; i++) {
        if (i) so_tab(o);
        so_t(o, colors ? colors[i] : C_VALUE, cells[i] ? cells[i] : "");
    }
    so_par(o);
}

void so_head(srs_out *o, const char *title) { so_b(o, C_VALUE, title); so_par(o); so_par(o); }
void so_note(srs_out *o, const char *s) { so_t(o, C_GRAY, s); so_par(o); }
void so_err(srs_out *o, const char *s) { so_t(o, C_RED, s); so_par(o); }

void so_lines(srs_out *o, int c, const char *text)
{
    const char *p = text;
    char line[2048];
    while (p && *p) {
        const char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l >= sizeof line) l = sizeof line - 1;
        memcpy(line, p, l);
        line[l] = 0;
        if (l && line[l - 1] == '\r') line[l - 1] = 0;
        if (*line) o->text(o, c, 0, 1, line);
        so_par(o);
        p = e ? e + 1 : NULL;
    }
}

void so_check(srs_out *o, int on)
{
    if (o->gui) o->text(o, on ? C_GREEN : C_GRAY, 0, 0, on ? "\u2611 " : "\u2610 ");
    else o->text(o, C_DEF, 0, 0, on ? "[=]" : "[_]");
}

const char *sys_os_name(void)
{
    static char b[256];
    char name[128] = "Linux", rel[128] = "", *txt, *p;
    if (*b) return b;
    if (rd_all("/etc/os-release", &txt) >= 0) {
        for (p = strtok(txt, "\n"); p; p = strtok(NULL, "\n"))
            if (!strncmp(p, "PRETTY_NAME=", 12)) { snprintf(name, sizeof name, "%s", p + 12 + (p[12] == '"')); name[strcspn(name, "\"")] = 0; }
        free(txt);
    }
    rd_str("/proc/sys/kernel/osrelease", rel, sizeof rel);
    snprintf(b, sizeof b, "%s %s", name, rel);
    return b;
}

const char *sys_board(void)
{
    static char b[512];
    char v[128] = "", n[128] = "", bv[64] = "", bd[64] = "";
    if (*b) return b;
    rd_str("/sys/class/dmi/id/board_vendor", v, sizeof v);
    rd_str("/sys/class/dmi/id/board_name", n, sizeof n);
    rd_str("/sys/class/dmi/id/bios_version", bv, sizeof bv);
    rd_str("/sys/class/dmi/id/bios_date", bd, sizeof bd);
    snprintf(b, sizeof b, "%s %s BIOS %s %s", v, n, bv, bd);
    return b;
}

const char *sys_cpu_name(void)
{
    static char b[160];
    char *txt, *p;
    if (*b) return b;
    if (rd_all("/proc/cpuinfo", &txt) >= 0) {
        for (p = strtok(txt, "\n"); p; p = strtok(NULL, "\n"))
            if (!strncmp(p, "model name", 10) && strchr(p, ':')) { snprintf(b, sizeof b, "%s", strchr(p, ':') + 2); break; }
        free(txt);
    }
    return b;
}

void so_siv_header(srs_out *o, const char *what)
{
    char host[128] = "localhost", buf[600];
    gethostname(host, sizeof host);
    snprintf(buf, sizeof buf, "srs-gui - %s on \\\\%s - %s", what, host, sys_os_name());
    so_t(o, C_VALUE, buf);
    so_par(o);
    so_par(o);
}

void so_board_footer(srs_out *o) { so_par(o); so_t(o, C_GRAY, sys_board()); so_par(o); }

/* ======================================================================
 * text backend: tab stops in px -> character columns (7 px per char)
 * ====================================================================== */
static void tx_text(srs_out *o, int c, int b, int m, const char *s)
{
    (void)c; (void)b; (void)m;
    fputs(s, (FILE *)o->ctx);
    for (; *s; s++)
        if ((*s & 0xC0) != 0x80) o->col++;   /* count UTF-8 code points */
}

static void tx_tab(srs_out *o)
{
    /* the n-th tab on a line goes to the n-th stop (column order is kept even when a cell overflows) */
    int target = o->tabn < o->ntabs ? o->tabpx[o->tabn] / 7 : o->col + 2;
    o->tabn++;
    if (target <= o->col) target = o->col + 2;
    while (o->col < target) { fputc(' ', (FILE *)o->ctx); o->col++; }
}

static void tx_par(srs_out *o) { fputs("\n", (FILE *)o->ctx); o->col = 0; o->tabn = 0; }

static void tx_tabs(srs_out *o, const int *px, int n)
{
    int i;
    o->ntabs = n > 32 ? 32 : n;
    for (i = 0; i < o->ntabs; i++) o->tabpx[i] = px[i];
}

void so_text_init(srs_out *o, void *f)
{
    memset(o, 0, sizeof *o);
    o->text = tx_text; o->tab = tx_tab; o->par = tx_par; o->tabs = tx_tabs;
    o->ctx = f;
}

/* ======================================================================
 * data helpers
 * ====================================================================== */
static void trim(char *s)
{
    size_t n = strlen(s), i = 0;
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, n - i + 1);
}

int rd_str(const char *path, char *buf, size_t sz)
{
    FILE *f = fopen(path, "rb");
    buf[0] = 0;
    if (!f) return 0;
    if (!fgets(buf, (int)sz, f)) { fclose(f); buf[0] = 0; return 0; }
    fclose(f);
    trim(buf);
    return 1;
}

int rd_all(const char *path, char **out)
{
    FILE *f = fopen(path, "rb");
    size_t n = 0, cap = 4096;
    char *b;
    *out = NULL;
    if (!f) return -1;
    b = malloc(cap);
    if (!b) { fclose(f); return -1; }
    for (;;) {
        size_t r;
        if (n + 1024 > cap) {
            char *nb = realloc(b, cap * 2);
            if (!nb) break;
            b = nb; cap *= 2;
        }
        r = fread(b + n, 1, cap - n - 1, f);
        if (!r) break;
        n += r;
    }
    fclose(f);
    b[n] = 0;
    *out = b;
    return (int)n;
}

int rd_long(const char *path, long *v)
{
    char b[64], *end;
    if (!rd_str(path, b, sizeof b) || !*b) return 0;
    *v = strtol(b, &end, 0);
    return end != b;
}

int rd_fmt(char *buf, size_t sz, const char *fmt, ...)
{
    char path[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(path, sizeof path, fmt, ap);
    va_end(ap);
    return rd_str(path, buf, sz);
}

int run_out(char **out, const char *const argv[])
{
    size_t cap = 1 << 20;
    char *b = malloc(cap);
    int rc;
    *out = NULL;
    if (!b) return -1;
    rc = plat_run_capture(argv, b, cap);
    *out = b;
    return rc;
}

int dir_list(const char *dir, char ***names) { return plat_listdir(dir, names); }
void dir_free(char **names, int n) { if (n > 0) plat_freelist(names, n); }
int path_exists(const char *path) { struct stat st; return stat(path, &st) == 0; }

const char *so_root_file(const char *name)
{
    static char p[1100];
    const char *h = getenv("HOME");
    if (!h) return NULL;
    snprintf(p, sizeof p, "%s/.cache/srs/root/%s", h, name);
    return path_exists(p) ? p : NULL;
}

void fmt_bytes(double v, char *buf, size_t sz)
{
    static const char *u[] = { "B", "KB", "MB", "GB", "TB", "PB" };
    int i = 0;
    while (v >= 1024 && i < 5) { v /= 1024; i++; }
    if (i == 0 || v == (double)(long long)v) snprintf(buf, sz, "%.0f%s", v, u[i]);   /* 32KB, 96MB */
    else snprintf(buf, sz, "%.2f%s", v, u[i]);
}

void fmt_hz(double hz, char *buf, size_t sz)
{
    if (hz >= 1e9) snprintf(buf, sz, "%.2fGHz", hz / 1e9);
    else if (hz >= 1e6) snprintf(buf, sz, "%.0fMHz", hz / 1e6);
    else if (hz >= 1e3) snprintf(buf, sz, "%.0fKHz", hz / 1e3);
    else snprintf(buf, sz, "%.0fHz", hz);
}

/* ======================================================================
 * registry
 * ====================================================================== */
static const srs_item *const *groups(void)
{
    static const srs_item *const g[] = { sec_tools, sec_cpu, sec_machine, sec_sensors, sec_storage, sec_network,
                                            sec_os, sec_gpu, sec_bus, sec_siv, NULL };
    return g;
}

const srs_item *srs_section_find(const char *idarg)
{
    const srs_item *const *g;
    const srs_item *s;
    char id[64];
    snprintf(id, sizeof id, "%.*s", (int)strcspn(idarg, ":"), idarg);
    for (g = groups(); *g; g++)
        for (s = *g; s->id; s++)
            if (!strcmp(s->id, id)) return s;
    for (s = sec_stubs; s->id; s++)
        if (!strcmp(s->id, id)) return s;
    return NULL;
}

static const srs_item **g_order;
static int g_norder;

static void build_order(void)
{
    const srs_item *const *g;
    const srs_item *s;
    int cap = 1024;
    if (g_order) return;
    g_order = malloc(sizeof *g_order * (size_t)cap);
    if (!g_order) return;
    for (s = sec_stubs; s->id; s++)              /* SIV order: the full item list */
        if (g_norder < cap) g_order[g_norder++] = srs_section_find(s->id);
    for (g = groups(); *g; g++)                   /* extras not in SIV's list */
        for (s = *g; s->id; s++) {
            int i, seen = 0;
            for (i = 0; i < g_norder; i++) if (!strcmp(g_order[i]->id, s->id)) { seen = 1; break; }
            if (!seen && g_norder < cap) g_order[g_norder++] = s;
        }
}

int srs_section_count(void) { build_order(); return g_norder; }
const srs_item *srs_section_at(int i) { build_order(); return i >= 0 && i < g_norder ? g_order[i] : NULL; }

void srs_section_render(const srs_item *s, srs_out *o)
{
    static const int tabs[] = { 160 };
    if (s->fn) { s->fn(o); return; }
    so_tabs(o, tabs, 1);
    so_b(o, C_VALUE, s->title); so_par(o); so_par(o);
    switch (s->bucket) {
    case B_ABSENT:
        so_kv(o, "Status", C_ORANGE, "Not present on this system");
        so_kv(o, "Checked", C_PURPLE, s->linux_src);
        break;
    case B_WINONLY:
        so_kv(o, "Status", C_ORANGE, "Windows-only item in SIV");
        so_kv(o, "Linux equivalent", C_PURPLE, s->linux_src && strcmp(s->linux_src, "none") ? s->linux_src : "none");
        break;
    default:
        so_kv(o, "Status", C_RED, "Not implemented yet");
        so_kv(o, "Source", C_PURPLE, s->linux_src);
        break;
    }
}

/* SIV's default -save set, in SIV's order (from SIV64X -save) */
static const char *const default_save[] = {
#include "sec_default_save.inc"
    NULL
};

static int save_one(FILE *f, const char *id)
{
    const srs_item *s = srs_section_find(id);
    srs_out o;
    if (!s) { fprintf(f, "[%s]  [unknown]\n\nNo such item\n\n\n", id); return 1; }
    fprintf(f, "[%s]  [%s]\n\n", s->id, s->category);
    so_text_init(&o, f);
    o.arg = strchr(id, ':') ? strchr(id, ':') + 1 : NULL;
    srs_section_render(s, &o);
    if (o.col) fputc('\n', f);
    fputs("\n\n", f);
    return 0;
}

int srs_save(const char *items, const char *file)
{
    FILE *f;
    int bad = 0, i;
    char host[256] = "localhost", tbuf[64];
    time_t now = time(NULL);
    if (!file || !*file || !strcasecmp(file, "STDOUT")) f = stdout;
    else if (!(f = fopen(file, "w"))) { perror(file); return 2; }
    gethostname(host, sizeof host);
    strftime(tbuf, sizeof tbuf, "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(f, "srs-gui - System Report Search V%s %s %s  [%s]\n\n", SRS_VERSION, host, tbuf, items ? items : "default");
    if (items && *items) {
        const char *p = items;
        if (!strcasecmp(items, "[all]")) {
            for (i = 0; i < srs_section_count(); i++) bad += save_one(f, srs_section_at(i)->id);
        } else {
            while ((p = strchr(p, '['))) {
                const char *e = strchr(p, ']');
                char id[64];
                if (!e) break;
                snprintf(id, sizeof id, "%.*s", (int)(e - p - 1), p + 1);
                bad += save_one(f, id);
                p = e + 1;
            }
        }
    } else {
        for (i = 0; default_save[i]; i++) bad += save_one(f, default_save[i]);
    }
    if (f != stdout) fclose(f);
    return bad ? 1 : 0;
}
