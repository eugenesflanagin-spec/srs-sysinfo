/* sec_core.c - section registry, output helpers, text backend, -save, data helpers. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE   /* strcasestr */
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
#include <pwd.h>

/* ======================================================================
 * output helpers
 * ====================================================================== */
void so_tabs(srs_out *o, const int *px, int n) { o->tabs(o, px, n); }

/* ======================================================================
 * redaction ("-save --redact", GUI Options > Redact identifiers in saved reports)
 *
 * Two layers, applied to every piece of text a producer emits:
 *  1. patterns: MAC addresses keep their vendor prefix (aa:bb:cc:XX:XX:XX), UUIDs become
 *     XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX;
 *  2. values: the identifiers this PC actually has (DMI serials / UUID, disk / NVMe / USB / monitor
 *     serials, WWNs, file-system and partition IDs, GPU unique_id, Thunderbolt unique_id, boot and
 *     machine ID, host name) are collected once and masked wherever they appear.
 * Masking keeps the length (letters and digits become X) so columns stay aligned.
 * ====================================================================== */
int srs_redact;

static char **g_rx;          /* identifier values, longest first */
static int g_nrx, g_caprx;
static void rx_push(const char *b);

static void rx_add(const char *v)
{
    char b[256];
    size_t l, i, digits = 0, same = 1;
    int k;
    static const char *const junk[] = { "default string", "to be filled", "not specified", "not available", "none", "unknown",
                                        "system serial number", "0123456789", "chassis serial", "base board serial", "n/a", NULL };
    if (!v) return;
    while (*v == ' ' || *v == '\t') v++;
    snprintf(b, sizeof b, "%s", v);
    l = strlen(b);
    while (l && (b[l - 1] == ' ' || b[l - 1] == '\n' || b[l - 1] == '\r' || b[l - 1] == '\t')) b[--l] = 0;
    if (l < 5) return;
    for (i = 0; i < l; i++) { digits += isdigit((unsigned char)b[i]) != 0; if (b[i] != b[0]) same = 0; }
    if (same || (digits == l && l < 8)) return;
    {   /* USB root hubs report their PCI address as "serial": that is a location, not an identifier */
        unsigned a, b2, c2, d;
        char e;
        if (sscanf(b, "%4x:%2x:%2x.%1x%c", &a, &b2, &c2, &d, &e) == 4 || strstr(b, "_hcd")) return;
    }
    for (k = 0; junk[k]; k++) if (strcasestr(b, junk[k])) return;
    rx_push(b);
}

static void rx_push(const char *b)
{
    int k;
    for (k = 0; k < g_nrx; k++) if (!strcmp(g_rx[k], b)) return;
    if (g_nrx == g_caprx) {
        char **n = realloc(g_rx, sizeof *g_rx * (size_t)(g_caprx ? g_caprx * 2 : 64));
        if (!n) return;
        g_rx = n;
        g_caprx = g_caprx ? g_caprx * 2 : 64;
    }
    g_rx[g_nrx++] = strdup(b);
}

static void rx_file(const char *path) { char b[256]; if (rd_str(path, b, sizeof b)) rx_add(b); }

static void rx_glob(const char *dir, const char *file)
{
    char **nm, p[1024];
    int n = dir_list(dir, &nm), i;
    for (i = 0; i < n; i++) { snprintf(p, sizeof p, "%s/%s/%s", dir, nm[i], file); rx_file(p); }
    dir_free(nm, n);
}

/* EDID: numeric serial (bytes 12..15) and the 0xFF "monitor serial" descriptor */
static void rx_edid(void)
{
    char **nm, p[1024];
    int n = dir_list("/sys/class/drm", &nm), i, k;
    for (i = 0; i < n; i++) {
        unsigned char e[256];
        size_t got;
        FILE *f;
        snprintf(p, sizeof p, "/sys/class/drm/%s/edid", nm[i]);
        if (!(f = fopen(p, "rb"))) continue;
        got = fread(e, 1, sizeof e, f);
        fclose(f);
        if (got < 128) continue;
        {
            unsigned sn = e[12] | e[13] << 8 | e[14] << 16 | (unsigned)e[15] << 24;
            char b[32];
            if (sn) { snprintf(b, sizeof b, "%08X", sn); rx_add(b); snprintf(b, sizeof b, "%u", sn); rx_add(b); }
        }
        for (k = 54; k + 18 <= 126; k += 18)
            if (!e[k] && !e[k + 1] && e[k + 3] == 0xFF) {
                char b[16];
                int j;
                memcpy(b, e + k + 5, 13);
                b[13] = 0;
                for (j = 0; b[j]; j++) if (b[j] == '\n') b[j] = 0;
                rx_add(b);
            }
    }
    dir_free(nm, n);
}

/* udev database: every *SERIAL*, *UUID* and WWN property (block devices, USB, +dmi:id DIMMs) */
static void rx_udev(void)
{
    char **nm, p[1024];
    int n = dir_list("/run/udev/data", &nm), i;
    for (i = 0; i < n; i++) {
        char *t, *l, *save = NULL;
        snprintf(p, sizeof p, "/run/udev/data/%s", nm[i]);
        if (rd_all(p, &t) < 0) continue;
        for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
            char *eq;
            if (strncmp(l, "E:", 2) || !(eq = strchr(l, '='))) continue;
            *eq = 0;
            /* ID_SERIAL / ID_USB_SERIAL are "Vendor_Model_serial": only their *_SHORT part is the identifier */
            if (strstr(l, "SERIAL_SHORT") || strstr(l, "SERIAL_NUMBER") || strstr(l, "UUID") || strstr(l, "WWN"))
                rx_add(eq + 1);
        }
        free(t);
    }
    dir_free(nm, n);
}

/* root cache: dmidecode "Serial Number:" / "UUID:" / "Asset Tag:" lines */
static void rx_dmidecode(void)
{
    const char *p = so_root_file("dmidecode.txt");
    char *t, *l, *save = NULL;
    if (!p || rd_all(p, &t) < 0) return;
    for (l = strtok_r(t, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char *c = strchr(l, ':');
        while (*l == ' ' || *l == '\t') l++;
        if (c && (!strncmp(l, "Serial Number:", 14) || !strncmp(l, "UUID:", 5) || !strncmp(l, "Asset Tag:", 10))) rx_add(c + 1);
    }
    free(t);
}

static int rx_cmp(const void *a, const void *b) { return (int)strlen(*(char *const *)b) - (int)strlen(*(char *const *)a); }

static void rx_build(void)
{
    static int built;
    static const char *const dmi[] = { "product_serial", "board_serial", "chassis_serial", "product_uuid", "board_asset_tag", "chassis_asset_tag", NULL };
    char p[256], host[256];
    int i;
    if (built) return;
    built = 1;
    for (i = 0; dmi[i]; i++) { snprintf(p, sizeof p, "/sys/class/dmi/id/%s", dmi[i]); rx_file(p); }
    rx_dmidecode();
    rx_glob("/sys/block", "device/serial");
    rx_glob("/sys/block", "device/wwid");
    rx_glob("/sys/class/nvme", "serial");
    rx_glob("/sys/bus/usb/devices", "serial");
    rx_glob("/sys/class/drm", "device/unique_id");
    rx_glob("/sys/bus/thunderbolt/devices", "unique_id");
    rx_glob("/sys/class/power_supply", "serial_number");
    rx_glob("/sys/class/input", "uniq");
    {
        static const char *const dirs[] = { "/dev/disk/by-uuid", "/dev/disk/by-partuuid", NULL };
        int d;
        for (d = 0; dirs[d]; d++) {
            char **nm;
            int n = dir_list(dirs[d], &nm), k;
            for (k = 0; k < n; k++) rx_add(nm[k]);
            dir_free(nm, n);
        }
    }
    rx_udev();
    rx_edid();
    rx_file("/proc/sys/kernel/random/boot_id");
    rx_file("/etc/machine-id");
    if (!gethostname(host, sizeof host) && strlen(host) >= 2 && strcmp(host, "localhost")) {
        /* short host names skip rx_add's length filter; like every value they are only masked as a
           whole word, so "X3D" is masked in "\\\\X3D" but not in "7800X3D" */
        rx_push(host);
    }
    if (g_nrx) qsort(g_rx, (size_t)g_nrx, sizeof *g_rx, rx_cmp);
}

static int hx(char c) { return isxdigit((unsigned char)c) != 0; }
static int an(char c) { return isalnum((unsigned char)c) != 0; }
static void mask(char *p, size_t n) { size_t i; for (i = 0; i < n; i++) if (an(p[i])) p[i] = 'X'; }

/* MAC: six hex pairs with the same ':' or '-' separator, not part of a longer run */
static int is_mac(const char *c, size_t i, size_t l)
{
    char sep;
    int g;
    if (i + 17 > l || !hx(c[i]) || !hx(c[i + 1])) return 0;
    sep = c[i + 2];
    if (sep != ':' && sep != '-') return 0;
    for (g = 0; g < 6; g++) {
        size_t k = i + (size_t)g * 3;
        if (!hx(c[k]) || !hx(c[k + 1])) return 0;
        if (g < 5 && c[k + 2] != sep) return 0;
    }
    if (i && (hx(c[i - 1]) || c[i - 1] == sep)) return 0;
    if (i + 17 < l && (hx(c[i + 17]) || c[i + 17] == sep)) return 0;
    return 1;
}

static int is_uuid(const char *c, size_t i, size_t l)
{
    static const int grp[] = { 8, 4, 4, 4, 12 };
    size_t k = i;
    int g, j;
    if (i + 36 > l || (i && an(c[i - 1]))) return 0;
    for (g = 0; g < 5; g++) {
        for (j = 0; j < grp[g]; j++, k++) if (!hx(c[k])) return 0;
        if (g < 4) { if (c[k] != '-') return 0; k++; }
    }
    return k >= l || !an(c[k]);
}

/* GUIDs that are published constants (GPT partition types, EFI variable vendors, firmware WMI
   interfaces): the same on every PC, so they are not masked */
static char (*g_kg)[37];
static int g_nkg;
static void kg_add(const char *g)
{
    void *n;
    if (strlen(g) < 36 || !(n = realloc(g_kg, sizeof *g_kg * (size_t)(g_nkg + 1)))) return;
    g_kg = n;
    snprintf(g_kg[g_nkg++], 37, "%.36s", g);
}
static void kg_build(void)
{
    static int built;
    static const char *const fixed[] = {
        "C12A7328-F81F-11D2-BA4B-00A0C93EC93B", "0FC63DAF-8483-4772-8E79-3D69D8477DE4", "0657FD6D-A4AB-43C4-84E5-0933C84B4F4F",
        "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", "E3C9E316-0B5C-4DB8-817D-F92DF00215AE", "DE94BBA4-06D1-4D40-A16A-BFD50179D6AC",
        "4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709", "933AC7E1-2EB4-4F13-B844-0E14E2AEF915", "E6D6D379-F507-44C2-A23C-238F2A3DF928",
        "A19D880F-05FC-4D3B-A006-743F0F84911E", "21686148-6449-6E6F-744E-656564454649", "BC13C2FF-59E6-4262-A352-B275FD6F7172",
        "3B8F8425-20E0-4F3B-907F-1A25A76F98E8", "CA7D7CCB-63ED-4C53-861C-1742536059CC", "8BE4DF61-93CA-11D2-AA0D-00E098032B8C",
        "D719B2CB-3D3A-4596-A3BC-DAD00E67656F", "605DAB50-E046-4300-ABB6-3DD810DD8B23", "00000000-0000-0000-0000-000000000000", NULL };
    char **nm;
    int n, i;
    if (built) return;
    built = 1;
    for (i = 0; fixed[i]; i++) kg_add(fixed[i]);
    n = dir_list("/sys/bus/wmi/devices", &nm);
    for (i = 0; i < n; i++) kg_add(nm[i]);
    dir_free(nm, n);
    n = dir_list("/sys/firmware/efi/efivars", &nm);
    for (i = 0; i < n; i++) { size_t l = strlen(nm[i]); if (l > 36) kg_add(nm[i] + l - 36); }
    dir_free(nm, n);
}
static int known_guid(const char *c)
{
    int i;
    kg_build();
    for (i = 0; i < g_nkg; i++) if (!strncasecmp(c, g_kg[i], 36)) return 1;
    return 0;
}

/* masked copy of s (malloc) when redaction is on and something matched, else NULL */
static char *redact_dup(const char *s)
{
    char *c;
    size_t l, i;
    int k, hit = 0;
    if (!srs_redact || !s || !*s) return NULL;
    rx_build();
    l = strlen(s);
    if (!(c = strdup(s))) return NULL;
    for (i = 0; i < l; i++) {
        if (is_mac(c, i, l)) { mask(c + i + 9, 8); i += 16; hit = 1; }
        else if (is_uuid(c, i, l)) { if (!known_guid(c + i)) { mask(c + i, 36); hit = 1; } i += 35; }
    }
    for (k = 0; k < g_nrx; k++) {
        size_t vl = strlen(g_rx[k]);
        char *p = c;
        while ((p = strstr(p, g_rx[k]))) {
            if ((p == c || !an(p[-1])) && !an(p[vl])) { mask(p, vl); hit = 1; }
            p += vl;
        }
    }
    if (!hit) { free(c); return NULL; }
    return c;
}

const char *srs_redact_host(void)
{
    static char h[256];
    char *r;
    if (gethostname(h, sizeof h)) snprintf(h, sizeof h, "localhost");
    if ((r = redact_dup(h))) { snprintf(h, sizeof h, "%s", r); free(r); }
    return h;
}

static void emit(srs_out *o, int c, int b, int m, const char *s)
{
    char *r;
    if (!s || !*s) return;
    r = redact_dup(s);
    o->text(o, c, b, m, r ? r : s);
    free(r);
}

void so_t(srs_out *o, int c, const char *s) { emit(o, c, 0, 0, s); }
void so_b(srs_out *o, int c, const char *s) { emit(o, c, 1, 0, s); }
void so_m(srs_out *o, int c, const char *s) { emit(o, c, 0, 1, s); }
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
        if (*line) emit(o, c, 0, 1, line);
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

/* Root-only data comes from srs-root's cache. Normal runs read ~/.cache/srs/root. When srs-gui /
   sysreport-search itself runs as root (sudo), HOME is usually /root, so also look in the invoking
   user's cache (SUDO_USER / PKEXEC_UID), and if no cache exists anywhere, collect one now (once). */
static int root_dirs(char dirs[3][1100])
{
    int n = 0;
    const char *h = getenv("HOME");
    if (h) snprintf(dirs[n++], 1100, "%s/.cache/srs/root", h);
    if (geteuid() == 0) {
        const char *su = getenv("SUDO_USER"), *pu = getenv("PKEXEC_UID");
        struct passwd *pw = su ? getpwnam(su) : pu ? getpwuid((uid_t)atoi(pu)) : NULL;
        if (pw && pw->pw_dir) snprintf(dirs[n++], 1100, "%s/.cache/srs/root", pw->pw_dir);
    }
    return n;
}

static void root_autocollect(void)
{
    static int done;
    char exe[1024], helper[1100], dirs[3][1100];
    ssize_t l;
    int n, i;
    if (done || geteuid() != 0) return;
    done = 1;
    n = root_dirs(dirs);
    for (i = 0; i < n; i++) {
        char c[1200];
        snprintf(c, sizeof c, "%s/collected.txt", dirs[i]);
        if (path_exists(c)) return;   /* a cache exists: use it (refresh with Menu > Run as root) */
    }
    l = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (l <= 0) return;
    exe[l] = 0;
    if (strrchr(exe, '/')) *strrchr(exe, '/') = 0;
    snprintf(helper, sizeof helper, "%s/srs-root", exe);
    if (!path_exists(helper)) snprintf(helper, sizeof helper, "/usr/bin/srs-root");
    if (!path_exists(helper)) return;
    fprintf(stderr, "srs: running as root with no cached root data - collecting it once with %s ...\n", helper);
    {
        const char *argv[] = { "/bin/sh", helper, NULL };
        char *out = NULL;
        run_out(&out, argv);
        free(out);
    }
}

const char *so_root_file(const char *name)
{
    static char p[1200];
    char dirs[3][1100];
    int n, i;
    root_autocollect();
    n = root_dirs(dirs);
    for (i = 0; i < n; i++) {
        snprintf(p, sizeof p, "%s/%s", dirs[i], name);
        if (path_exists(p)) return p;
    }
    return NULL;
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
    snprintf(host, sizeof host, "%s", srs_redact_host());
    strftime(tbuf, sizeof tbuf, "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(f, "srs-gui - System Report Search V%s %s %s  [%s]%s\n\n", SRS_VERSION, host, tbuf, items ? items : "default",
            srs_redact ? "  (identifiers redacted)" : "");
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

/* ======================================================================
 * device ID on a panel line (GUI double-click -> Device ID Lookup)
 *   VID_046D PID_C52B / VID_046D&PID_C52B          USB
 *   0003:046D:4057.0010 (HID bus:vendor:product)   bus 0003 USB, 0005 Bluetooth (USB IDs)
 *   [1022:14D8]  1022:14d8:1043:8877  10de:2684:a1  PCI, or USB when the item / context is USB
 * PCI slot addresses (0000:03:00.0) are not IDs and are skipped.
 * ====================================================================== */
static int hexn(const char *p, int n) { int i; for (i = 0; i < n; i++) if (!hx(p[i])) return 0; return 1; }

int srs_line_device_id(const char *item, const char *line, int usb_ctx, char *out, size_t sz)
{
    const char *p;
    int usb = usb_ctx || (item && strstr(item, "usb"));
    if ((p = strcasestr(line, "VID_")) && hexn(p + 4, 4)) {
        const char *q = strcasestr(p, "PID_");
        if (q && q - p <= 10 && hexn(q + 4, 4)) { snprintf(out, sz, "id-lookup:usb:%.4s:%.4s", p + 4, q + 4); return 1; }
    }
    for (p = line; *p; p++) {
        int bound = p == line || !an(p[-1]) || p[-1] == '[';
        if (!bound || !hexn(p, 4) || p[4] != ':' || !hexn(p + 5, 4)) continue;
        /* HID device name: bus:vendor:product.instance */
        if (p[9] == ':' && hexn(p + 10, 4) && p[14] == '.') {
            unsigned bus = (unsigned)strtoul(p, NULL, 16);
            if (bus == 3 || bus == 5) { snprintf(out, sz, "id-lookup:usb:%.4s:%.4s", p + 5, p + 10); return 1; }
            if (bus == 0x18 || bus == 0x19) { p += 18; continue; }   /* I2C HID: IDs are ACPI-defined, skip the name */
        }
        /* PCI slot 0000:03:00.0 (domain:bus:dev.fn) - a location, not an ID */
        if (p[7] == ':' && p[10] == '.') continue;
        if (!strncmp(p, "0000", 4)) continue;
        {
            size_t l = 9;
            while (p[l] == ':' && hexn(p + l + 1, 2) && l < 40) { l++; while (hx(p[l])) l++; }
            snprintf(out, sz, "id-lookup:%s%.*s", usb ? "usb:" : "", (int)l, p);
            return 1;
        }
    }
    return 0;
}

