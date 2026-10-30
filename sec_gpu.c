/* sec_gpu.c - SIV graphics / display / audio / input sections on Linux:
 * [graphics] [monitors] [mon-info] [monitor-tree] [gpu-info] [gpu-all] [gpu-clocks] [gpu-temp]
 * [gpu-fans] [gpu-power] [gpu-bios] [gpu-map] [gpu-tree] [gpu-gens] [gpu-reg] [gpu-scf] [gpu-i2c]
 * [gpu-apps] [gpu-api] [gpu-d3d] [d3d-node] [d3d-info] [directx] [gpu-g-sync] [gpu-dump]
 * [gpu-export] [gpu-cuda] [gpu-sli] [gpu-leds] [opencl] [rsr-info] [rtss-sts] [goverlay]
 * [dxd-*] [audio] [sound] [wave] [uaa-dev] [joysticks]
 *
 * Sources: amdgpu sysfs under /sys/class/drm/card<N>/device (read only), the card's hwmon,
 * /sys/class/drm/card<N>-<conn> connectors (EDID decoded here), /usr/share/hwdata/pnp.ids,
 * /usr/share/libdrm/amdgpu.ids, /proc/asound, /proc/bus/input/devices, and read-only query
 * tools (vulkaninfo --summary, glxinfo -B, clinfo, drm_info, wpctl, aplay, lsusb, lsblk).
 * Safety: never writes sysfs; never touches i2c/DDC (bus names only); no GPU load.
 * SMU-backed attributes that are not plain sensors (pp_features, pp_power_profile_mode,
 * gpu_od/...) are deliberately not read. gpu_metrics: only its 4-byte header is decoded. */
#define _GNU_SOURCE
#include "srs.h"
#include "sec.h"

#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define DRM "/sys/class/drm"

/* ======================================================================
 * small helpers
 * ====================================================================== */
static int starts(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }

static void base_name(const char *path, char *b, size_t sz)
{
    char l[1024];
    ssize_t n = readlink(path, l, sizeof l - 1);
    const char *p;
    b[0] = 0;
    if (n <= 0) return;
    l[n] = 0;
    p = strrchr(l, '/');
    snprintf(b, sz, "%s", p ? p + 1 : l);
}

/* run argv under "timeout SECS"; returns rc, -1 when not installed; *out always set (may be "") */
static int run_to(char **out, int secs, const char *const argv[])
{
    const char *a[32];
    char s[16];
    int i = 0, rc;
    snprintf(s, sizeof s, "%d", secs);
    /* child stderr is discarded: "sh -c 'exec "$0" "$@" 2>/dev/null' argv..." (no string interpolation) */
    a[i++] = "timeout";
    a[i++] = s;
    a[i++] = "sh";
    a[i++] = "-c";
    a[i++] = "exec \"$0\" \"$@\" 2>/dev/null";
    while (argv[i - 5] && i < 31) { a[i] = argv[i - 5]; i++; }
    a[i] = NULL;
    rc = run_out(out, a);
    if (!*out) *out = calloc(1, 1);
    return rc;
}

static int gpu_proc_running(const char *name)
{
    char **nm, p[300], c[64];
    int n = dir_list("/proc", &nm), i, found = 0;
    for (i = 0; i < n && !found; i++) {
        if (nm[i][0] < '0' || nm[i][0] > '9') continue;
        snprintf(p, sizeof p, "/proc/%s/comm", nm[i]);
        if (rd_str(p, c, sizeof c) && !strcmp(c, name)) found = 1;
    }
    dir_free(nm, n);
    return found;
}

static int have_cmd(const char *c)
{
    static const char *const dirs[] = { "/usr/bin", "/usr/local/bin", "/bin", "/opt/rocm/bin", NULL };
    char p[512];
    int i;
    for (i = 0; dirs[i]; i++) { snprintf(p, sizeof p, "%s/%s", dirs[i], c); if (access(p, X_OK) == 0) return 1; }
    return 0;
}

/* cached tool outputs (re-run after 10 s) */
typedef struct { const char *const *argv; char *out; time_t t; int rc; } tool_cache;
static const char *tool(tool_cache *c, int secs)
{
    time_t now = time(NULL);
    if (c->out && now - c->t < 10) return c->rc < 0 ? NULL : c->out;
    free(c->out);
    c->out = NULL;
    if (!have_cmd(!strcmp(c->argv[0], "env") ? c->argv[3] : c->argv[0])) { c->rc = -1; c->t = now; c->out = calloc(1, 1); return NULL; }
    c->rc = run_to(&c->out, secs, c->argv);
    c->t = now;
    return c->rc < 0 ? NULL : c->out;
}
/* implicit layers (overlays, frame-gen, MangoHud) are disabled for the query */
static const char *const A_VK[] = { "env", "VK_LOADER_LAYERS_DISABLE=~implicit~", "DISABLE_MANGOHUD=1", "vulkaninfo", "--summary", NULL };
static const char *const A_GLX[] = { "glxinfo", "-B", NULL };
static const char *const A_EGL[] = { "eglinfo", "-B", NULL };
static const char *const A_CL[] = { "clinfo", NULL };
static const char *const A_DRM[] = { "drm_info", NULL };
static tool_cache T_VK = { A_VK, NULL, 0, 0 }, T_GLX = { A_GLX, NULL, 0, 0 }, T_EGL = { A_EGL, NULL, 0, 0 },
                  T_CL = { A_CL, NULL, 0, 0 }, T_DRM = { A_DRM, NULL, 0, 0 };

/* find "key" in text at line start (after whitespace), copy value after the first ':' or '=' */
static int find_val(const char *text, const char *key, char *v, size_t sz)
{
    const char *p = text;
    size_t kl = strlen(key);
    v[0] = 0;
    while (p && *p) {
        const char *s = p, *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        while (*s == ' ' || *s == '\t') s++;
        if (!strncmp(s, key, kl)) {
            const char *c = s + kl;
            while (*c == ' ' || *c == '\t') c++;
            if (*c == ':' || *c == '=') {
                size_t n;
                c++;
                while (*c == ' ' || *c == '\t') c++;
                n = (size_t)((p + l) - c);
                if (n >= sz) n = sz - 1;
                memcpy(v, c, n);
                v[n] = 0;
                while (n && isspace((unsigned char)v[n - 1])) v[--n] = 0;
                return 1;
            }
        }
        p = e ? e + 1 : NULL;
    }
    return 0;
}

/* iterate lines: copies next line into l, returns pointer after it or NULL at end */
static const char *next_line(const char *p, char *l, size_t sz)
{
    const char *e;
    size_t n;
    if (!p || !*p) return NULL;
    e = strchr(p, '\n');
    n = e ? (size_t)(e - p) : strlen(p);
    if (n >= sz) n = sz - 1;
    memcpy(l, p, n);
    l[n] = 0;
    if (n && l[n - 1] == '\r') l[n - 1] = 0;
    return e ? e + 1 : p + strlen(p);
}

static void so_lines_max(srs_out *o, int color, const char *text, int max)
{
    char l[2048];
    int n = 0;
    const char *p = text;
    while ((p = next_line(p, l, sizeof l))) {
        if (n++ >= max) { so_note(o, "... (output truncated)"); break; }
        if (*l) so_m(o, color, l);
        so_par(o);
    }
}

/* remember whole lines in "|a|b|" form; returns 1 if l was already seen */
static int seen_add(char *set, size_t sz, const char *l)
{
    char key[300];
    snprintf(key, sizeof key, "|%s|", l);
    if (strstr(set, key)) return 1;
    if (!*set) snprintf(set, sz, "|");
    if (strlen(set) + strlen(l) + 2 < sz) { strcat(set, l); strcat(set, "|"); }
    return 0;
}

static void mhz(double hz, char *b, size_t sz) { snprintf(b, sz, "%.0f MHz", hz / 1e6); }

/* ======================================================================
 * GPU discovery
 * ====================================================================== */
typedef struct {
    char card[32];        /* card0 */
    char dev[256];        /* /sys/class/drm/card0/device */
    char hwmon[320];      /* .../hwmon/hwmonN or "" */
    char slot[32];        /* 0000:03:00.0 */
    char driver[64];
    unsigned vid, did, svid, sdid, rev;
} gpu_t;

static int gpu_fill(const char *card, gpu_t *g)
{
    char p[512], b[128], **hn;
    int n, i;
    long v;
    memset(g, 0, sizeof *g);
    snprintf(g->card, sizeof g->card, "%s", card);
    snprintf(g->dev, sizeof g->dev, DRM "/%s/device", card);
    snprintf(p, sizeof p, "%s/driver", g->dev);
    base_name(p, g->driver, sizeof g->driver);
    if (!*g->driver) return 0;
    base_name(g->dev, g->slot, sizeof g->slot);
    snprintf(p, sizeof p, "%s/vendor", g->dev); if (rd_long(p, &v)) g->vid = (unsigned)v;
    snprintf(p, sizeof p, "%s/device", g->dev); if (rd_long(p, &v)) g->did = (unsigned)v;
    snprintf(p, sizeof p, "%s/subsystem_vendor", g->dev); if (rd_long(p, &v)) g->svid = (unsigned)v;
    snprintf(p, sizeof p, "%s/subsystem_device", g->dev); if (rd_long(p, &v)) g->sdid = (unsigned)v;
    snprintf(p, sizeof p, "%s/revision", g->dev); if (rd_long(p, &v)) g->rev = (unsigned)v;
    snprintf(p, sizeof p, "%s/hwmon", g->dev);
    n = dir_list(p, &hn);
    for (i = 0; i < n; i++)
        if (starts(hn[i], "hwmon")) { snprintf(g->hwmon, sizeof g->hwmon, "%s/%s", p, hn[i]); break; }
    dir_free(hn, n);
    (void)b;
    return 1;
}

/* list of GPUs (cards without '-'), max 8 */
static int gpus(gpu_t *g, int max)
{
    char **nm = NULL;
    int n = dir_list(DRM, &nm), i, k = 0;
    for (i = 0; i < n && k < max; i++) {
        if (!starts(nm[i], "card") || strchr(nm[i], '-')) continue;
        if (gpu_fill(nm[i], &g[k])) k++;
    }
    dir_free(nm, n);
    return k;
}

/* GPU selected by o->arg ("card0" or "0"), else the first amdgpu, else the first */
static int gpu_sel(srs_out *o, gpu_t *g)
{
    gpu_t all[8];
    int n = gpus(all, 8), i;
    if (!n) return 0;
    if (o->arg && *o->arg) {
        for (i = 0; i < n; i++)
            if (!strcmp(all[i].card, o->arg) || !strcmp(all[i].card + 4, o->arg) || strstr(all[i].slot, o->arg)) { *g = all[i]; return 1; }
    }
    for (i = 0; i < n; i++) if (!strcmp(all[i].driver, "amdgpu")) { *g = all[i]; return 1; }
    *g = all[0];
    return 1;
}

static int gd_str(const gpu_t *g, const char *f, char *b, size_t sz) { return rd_fmt(b, sz, "%s/%s", g->dev, f); }
static int gd_long(const gpu_t *g, const char *f, long *v) { char p[512]; snprintf(p, sizeof p, "%s/%s", g->dev, f); return rd_long(p, v); }
static int hw_long(const gpu_t *g, const char *f, long *v) { char p[512]; if (!*g->hwmon) return 0; snprintf(p, sizeof p, "%s/%s", g->hwmon, f); return rd_long(p, v); }
static int hw_str(const gpu_t *g, const char *f, char *b, size_t sz) { b[0] = 0; if (!*g->hwmon) return 0; return rd_fmt(b, sz, "%s/%s", g->hwmon, f); }

/* marketing name: /usr/share/libdrm/amdgpu.ids "7550,\tC0,\tAMD Radeon RX 9070 XT" */
static void gpu_name(const gpu_t *g, char *b, size_t sz)
{
    char *t, l[512], hx[16], rv[8];
    const char *p;
    snprintf(b, sz, "GPU %04X:%04X", g->vid, g->did);
    if (g->vid == 0x1002 && rd_all("/usr/share/libdrm/amdgpu.ids", &t) > 0) {
        snprintf(hx, sizeof hx, "%04X", g->did);
        snprintf(rv, sizeof rv, "%02X", g->rev);
        p = t;
        while ((p = next_line(p, l, sizeof l))) {
            char *a, *c;
            if (strncasecmp(l, hx, 4) || l[4] != ',') continue;
            a = l + 5; while (*a == ' ' || *a == '\t') a++;
            if (strncasecmp(a, rv, 2)) continue;
            c = strchr(a, ','); if (!c) continue;
            c++; while (*c == ' ' || *c == '\t') c++;
            snprintf(b, sz, "%s", c);
            break;
        }
        free(t);
        return;
    }
    {
        /* fall back to lspci device name */
        const char *a[] = { "lspci", "-mm", "-s", g->slot, NULL };
        char *out;
        if (run_out(&out, a) == 0 && out) {
            char *q = out; int f = 0;
            while ((q = strchr(q, '"'))) {
                char *e = strchr(q + 1, '"');
                if (!e) break;
                if (++f == 3) { snprintf(b, sz, "%.*s", (int)(e - q - 1), q + 1); break; }
                q = e + 1;
            }
        }
        free(out);
    }
}

/* lspci -mm -nn fields: 0 class 1 vendor 2 device 3 svendor 4 sdevice */
static int lspci_fields(const char *slot, char f[5][160])
{
    const char *a[] = { "lspci", "-mm", "-nn", "-s", slot, NULL };
    char *out = NULL, *q;
    int n = 0, i;
    for (i = 0; i < 5; i++) f[i][0] = 0;
    if (run_out(&out, a) != 0 || !out) { free(out); return 0; }
    q = out;
    while (n < 5 && (q = strchr(q, '"'))) {
        char *e = strchr(q + 1, '"');
        if (!e) break;
        snprintf(f[n++], 160, "%.*s", (int)(e - q - 1), q + 1);
        q = e + 1;
    }
    free(out);
    return n;
}

/* GC (graphics core) IP version from ip_discovery */
static int ip_ver(const gpu_t *g, const char *ip, int *maj, int *min, int *rev)
{
    char b[64];
    long v;
    char p[512];
    *maj = *min = *rev = -1;
    snprintf(p, sizeof p, "%s/ip_discovery/die/0/%s/0/major", g->dev, ip); if (!rd_long(p, &v)) return 0; *maj = (int)v;
    snprintf(p, sizeof p, "%s/ip_discovery/die/0/%s/0/minor", g->dev, ip); if (rd_long(p, &v)) *min = (int)v;
    snprintf(p, sizeof p, "%s/ip_discovery/die/0/%s/0/revision", g->dev, ip); if (rd_long(p, &v)) *rev = (int)v;
    (void)b;
    return 1;
}

static const char *gc_family(int maj, int min)
{
    if (maj == 12) return "RDNA 4 (Navi 4x)";
    if (maj == 11 && min >= 5) return "RDNA 3.5 (APU)";
    if (maj == 11) return "RDNA 3 (Navi 3x)";
    if (maj == 10 && min >= 3) return "RDNA 2 (Navi 2x)";
    if (maj == 10) return "RDNA 1 (Navi 1x)";
    if (maj == 9 && min >= 4) return "CDNA (Instinct)";
    if (maj == 9) return "GCN 5 (Vega)";
    if (maj == 8) return "GCN 3/4 (Polaris/Fiji/Tonga)";
    if (maj == 7) return "GCN 2 (Sea/Volcanic Islands)";
    if (maj == 6) return "GCN 1 (Southern Islands)";
    return "unknown";
}

static void gfx_target(const gpu_t *g, char *b, size_t sz)
{
    int ma, mi, rv;
    b[0] = 0;
    if (ip_ver(g, "GC", &ma, &mi, &rv)) snprintf(b, sz, "gfx%d%d%x", ma, mi, rv);
}

/* ---------- DPM tables ---------- */
typedef struct { int n, act; char lv[16][64]; } dpm_t;
static int dpm_read(const gpu_t *g, const char *f, dpm_t *d)
{
    char *t, l[256];
    const char *p;
    char path[512];
    d->n = 0; d->act = -1;
    snprintf(path, sizeof path, "%s/%s", g->dev, f);
    if (rd_all(path, &t) <= 0) { free(t); return 0; }
    p = t;
    while ((p = next_line(p, l, sizeof l)) && d->n < 16) {
        char *c = strchr(l, ':'), *s;
        size_t n;
        if (!c) continue;
        s = c + 1; while (*s == ' ') s++;
        n = strlen(s);
        while (n && (s[n - 1] == ' ')) s[--n] = 0;
        if (n && s[n - 1] == '*') { d->act = d->n; s[--n] = 0; while (n && s[n - 1] == ' ') s[--n] = 0; }
        snprintf(d->lv[d->n++], 64, "%s", s);
    }
    free(t);
    return d->n;
}

static void dpm_rows(srs_out *o, const gpu_t *g, const char *label, const char *f)
{
    dpm_t d;
    int i;
    char b[512] = "";
    if (!dpm_read(g, f, &d)) return;
    for (i = 0; i < d.n; i++) {
        char c[80];
        snprintf(c, sizeof c, "%s%s%s  ", i == d.act ? "[" : "", d.lv[i], i == d.act ? "]" : "");
        strncat(b, c, sizeof b - strlen(b) - 1);
    }
    so_t(o, C_LABEL, label); so_tab(o);
    so_t(o, C_TEAL, d.act >= 0 ? d.lv[d.act] : "-"); so_tab(o);
    so_t(o, C_VALUE, b);
    so_par(o);
}

/* ---------- connectors ---------- */
typedef struct { char name[64]; char conn[48]; char status[24]; char enabled[16]; char dpms[16]; int id; } conn_t;
static int connectors(const gpu_t *g, conn_t *c, int max)
{
    char **nm = NULL, pre[48];
    int n = dir_list(DRM, &nm), i, k = 0;
    snprintf(pre, sizeof pre, "%s-", g->card);
    for (i = 0; i < n && k < max; i++) {
        char p[512];
        long v;
        if (!starts(nm[i], pre)) continue;
        if (strstr(nm[i], "Writeback")) continue;
        memset(&c[k], 0, sizeof c[k]);
        snprintf(c[k].name, sizeof c[k].name, "%s", nm[i]);
        snprintf(c[k].conn, sizeof c[k].conn, "%s", nm[i] + strlen(pre));
        rd_fmt(c[k].status, sizeof c[k].status, DRM "/%s/status", nm[i]);
        rd_fmt(c[k].enabled, sizeof c[k].enabled, DRM "/%s/enabled", nm[i]);
        rd_fmt(c[k].dpms, sizeof c[k].dpms, DRM "/%s/dpms", nm[i]);
        snprintf(p, sizeof p, DRM "/%s/connector_id", nm[i]);
        c[k].id = rd_long(p, &v) ? (int)v : -1;
        k++;
    }
    dir_free(nm, n);
    return k;
}

/* ======================================================================
 * EDID
 * ====================================================================== */
typedef struct {
    int ok, csum_ok, ver, rev, ext;
    char pnp[4], mfg[128], name[64], serial_s[64], text[64];
    unsigned product, serial;
    int week, year, hcm, vcm, digital, bpc, iface;
    double gamma;
    int rng, vmin, vmax, hmin, hmax, pclk_max;
    int dtd, w, h, hmm, vmm;
    double rate, pclk;
    unsigned char ext_tags[8];
} edid_t;

/* hwdata's pnp.ids: the system's, or the AppImage's own copy on systems without hwdata */
static const char *pnp_ids(void)
{
    static char p[1200];
    if (!p[0]) snprintf(p, sizeof p, "%s/pnp.ids", plat_hwdata_dir());
    return p;
}

/* common monitor vendors missing from hwdata's pnp.ids (UEFI PNP registry gaps) */
static const char *const pnp_extra[][2] = { { "BNQ", "BenQ Corporation" }, { "AUS", "ASUSTek Computer Inc" }, { "MSI", "Micro-Star International" },
                                            { "AOC", "AOC International" }, { "GBT", "GIGA-BYTE Technology" }, { "XMI", "Xiaomi" },
                                            { "KTC", "Konka (KTC)" }, { "HKC", "HKC Overseas" }, { NULL, NULL } };

static void pnp_lookup(const char *id, char *b, size_t sz)
{
    char *t, l[256];
    const char *p;
    b[0] = 0;
    if (rd_all(pnp_ids(), &t) <= 0) { free(t); return; }
    p = t;
    while ((p = next_line(p, l, sizeof l)))
        if (!strncmp(l, id, 3) && l[3] == '\t') { snprintf(b, sz, "%s", l + 4); break; }
    free(t);
    if (!*b) {
        int i;
        for (i = 0; pnp_extra[i][0]; i++) if (!strcmp(id, pnp_extra[i][0])) { snprintf(b, sz, "%s", pnp_extra[i][1]); break; }
    }
}

static void edid_str(const unsigned char *d, char *b, size_t sz)
{
    int i, n = 0;
    for (i = 0; i < 13 && d[i] != 0x0a && n < (int)sz - 1; i++) b[n++] = (d[i] >= 32 && d[i] < 127) ? (char)d[i] : '?';
    b[n] = 0;
    while (n && b[n - 1] == ' ') b[--n] = 0;
}

static int edid_parse(const unsigned char *e, int len, edid_t *x)
{
    static const unsigned char hdr[8] = { 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };
    int i, sum = 0;
    memset(x, 0, sizeof *x);
    if (len < 128 || memcmp(e, hdr, 8)) return 0;
    x->ok = 1;
    for (i = 0; i < 128; i++) sum += e[i];
    x->csum_ok = (sum & 0xff) == 0;
    x->pnp[0] = (char)('@' + ((e[8] >> 2) & 0x1f));
    x->pnp[1] = (char)('@' + (((e[8] & 3) << 3) | (e[9] >> 5)));
    x->pnp[2] = (char)('@' + (e[9] & 0x1f));
    x->pnp[3] = 0;
    pnp_lookup(x->pnp, x->mfg, sizeof x->mfg);
    x->product = (unsigned)(e[10] | e[11] << 8);
    x->serial = (unsigned)(e[12] | e[13] << 8 | e[14] << 16 | (unsigned)e[15] << 24);
    x->week = e[16];
    x->year = 1990 + e[17];
    x->ver = e[18];
    x->rev = e[19];
    x->digital = e[20] >> 7;
    if (x->digital) {
        static const int bpcs[8] = { 0, 6, 8, 10, 12, 14, 16, 0 };
        x->bpc = bpcs[(e[20] >> 4) & 7];
        x->iface = e[20] & 0xf;
    }
    x->hcm = e[21];
    x->vcm = e[22];
    x->gamma = e[23] == 0xff ? 0 : (e[23] + 100) / 100.0;
    for (i = 0; i < 4; i++) {
        const unsigned char *d = e + 54 + 18 * i;
        if (d[0] || d[1]) {
            if (!x->dtd) {
                int hb, vb;
                x->dtd = 1;
                x->pclk = (d[0] | d[1] << 8) * 10000.0;
                x->w = d[2] | (d[4] >> 4) << 8;
                hb = d[3] | (d[4] & 0xf) << 8;
                x->h = d[5] | (d[7] >> 4) << 8;
                vb = d[6] | (d[7] & 0xf) << 8;
                x->hmm = d[12] | (d[14] >> 4) << 8;
                x->vmm = d[13] | (d[14] & 0xf) << 8;
                if ((x->w + hb) && (x->h + vb)) x->rate = x->pclk / ((double)(x->w + hb) * (x->h + vb));
            }
            continue;
        }
        switch (d[3]) {
        case 0xfc: edid_str(d + 5, x->name, sizeof x->name); break;
        case 0xff: edid_str(d + 5, x->serial_s, sizeof x->serial_s); break;
        case 0xfe: edid_str(d + 5, x->text, sizeof x->text); break;
        case 0xfd: {
            int f = d[4];
            x->rng = 1;
            x->vmin = d[5] + ((f & 3) == 3 ? 255 : 0);
            x->vmax = d[6] + ((f & 2) ? 255 : 0);
            x->hmin = d[7] + (((f >> 2) & 3) == 3 ? 255 : 0);
            x->hmax = d[8] + (((f >> 2) & 2) ? 255 : 0);
            x->pclk_max = d[9] * 10;
            break;
        }
        }
    }
    x->ext = e[126];
    for (i = 0; i < x->ext && i < 8 && 128 * (i + 2) <= len; i++) x->ext_tags[i] = e[128 * (i + 1)];
    return 1;
}

static int conn_edid(const char *conn_name, edid_t *x)
{
    char p[512], *b;
    int n;
    snprintf(p, sizeof p, DRM "/%s/edid", conn_name);
    n = rd_all(p, &b);
    memset(x, 0, sizeof *x);
    if (n >= 128) edid_parse((unsigned char *)b, n, x);
    free(b);
    return x->ok;
}

static const char *ext_tag_name(int t)
{
    switch (t) {
    case 0x02: return "CTA-861";
    case 0x10: return "VTB";
    case 0x40: return "DI";
    case 0x50: return "LS";
    case 0x60: return "DPVL";
    case 0x70: return "DisplayID";
    case 0xf0: return "Block map";
    case 0xff: return "Manufacturer";
    }
    return "unknown";
}

static const char *iface_name(int i)
{
    switch (i) { case 1: return "DVI"; case 2: return "HDMI-a"; case 3: return "HDMI-b"; case 4: return "MDDI"; case 5: return "DisplayPort"; }
    return "undefined";
}

/* ---------- drm_info parsing (connector modes, vrr, crtc) ---------- */
typedef struct { int id; char type[32]; int vrr_cap; int crtc; char modes[64][48]; int nm; char cur[48]; int vrr_en; } kms_conn;
typedef struct { int id; char mode[48]; int vrr_en; } kms_crtc;
static int kms_parse(kms_conn *c, int maxc, int *nc, kms_crtc *r, int maxr, int *nr)
{
    const char *t = tool(&T_DRM, 10), *p;
    char l[512];
    int sect = 0, inmodes = 0, i, j;   /* sect 1 = connectors, 2 = crtcs */
    kms_conn *cc = NULL;
    kms_crtc *cr = NULL;
    *nc = *nr = 0;
    if (!t || !*t) return 0;
    p = t;
    while ((p = next_line(p, l, sizeof l))) {
        char *s = l;
        /* strip tree glyphs */
        while (*s && !isalnum((unsigned char)*s) && *s != '"') s++;
        if (!strncmp(s, "Node:", 5) && *nc) break;                  /* only first card */
        if (!strcmp(s, "Connectors")) { sect = 1; continue; }
        if (!strcmp(s, "Encoders") || !strcmp(s, "Planes") || !strcmp(s, "Framebuffer size")) { sect = 0; continue; }
        if (!strcmp(s, "CRTCs")) { sect = 2; continue; }
        if (sect == 1 && !strncmp(s, "Connector ", 10) && *nc < maxc) { cc = &c[(*nc)++]; memset(cc, 0, sizeof *cc); cc->id = -1; inmodes = 0; continue; }
        if (sect == 2 && !strncmp(s, "CRTC ", 5) && *nr < maxr) { cr = &r[(*nr)++]; memset(cr, 0, sizeof *cr); continue; }
        if (sect == 1 && cc) {
            if (!strncmp(s, "Object ID: ", 11)) cc->id = atoi(s + 11);
            else if (!strncmp(s, "Type: ", 6)) snprintf(cc->type, sizeof cc->type, "%s", s + 6);
            else if (!strcmp(s, "Modes")) inmodes = 1;
            else if (!strcmp(s, "Properties")) inmodes = 0;
            else if (inmodes && isdigit((unsigned char)*s) && cc->nm < 64) {
                char *sp = strchr(s, ' ');
                snprintf(cc->modes[cc->nm++], 48, "%.*s%s", sp ? (int)(sp - s) : (int)strlen(s), s, strstr(s, "preferred") ? " *" : "");
            } else if (!strncmp(s, "\"vrr_capable\"", 13)) { char *e = strrchr(s, '='); cc->vrr_cap = e ? atoi(e + 1) : 0; }
            else if (!strncmp(s, "\"CRTC_ID\"", 9)) { char *e = strrchr(s, '='); cc->crtc = e ? atoi(e + 1) : 0; }
        }
        if (sect == 2 && cr) {
            if (!strncmp(s, "Object ID: ", 11)) cr->id = atoi(s + 11);
            else if (!strncmp(s, "Mode: ", 6) && !*cr->mode) { char *sp = strchr(s + 6, ' '); snprintf(cr->mode, 48, "%.*s", sp ? (int)(sp - s - 6) : (int)strlen(s + 6), s + 6); }
            else if (!strncmp(s, "\"VRR_ENABLED\"", 13)) { char *e = strrchr(s, '='); cr->vrr_en = e ? atoi(e + 1) : 0; }
        }
    }
    for (i = 0; i < *nc; i++)
        for (j = 0; j < *nr; j++)
            if (c[i].crtc && c[i].crtc == r[j].id) { snprintf(c[i].cur, 48, "%s", r[j].mode); c[i].vrr_en = r[j].vrr_en; }
    return *nc;
}

static kms_conn *kms_find(kms_conn *c, int n, int id) { int i; for (i = 0; i < n; i++) if (c[i].id == id) return &c[i]; return NULL; }

/* ======================================================================
 * [gpu-info]
 * ====================================================================== */
static void s_gpu_info_body(srs_out *o, const gpu_t *g)
{
    char b[512], f[5][160], nm[160], t[64];
    long v, v2;
    int ma, mi, rv;
    gpu_name(g, nm, sizeof nm);
    lspci_fields(g->slot, f);
    so_kv(o, "GPU", C_BLACK, nm);
    so_kv(o, "Chip", C_VALUE, f[2]);
    so_kvf(o, "Device IDs", C_PURPLE, "PCI\\VEN_%04X&DEV_%04X&SUBSYS_%04X%04X&REV_%02X", g->vid, g->did, g->sdid, g->svid, g->rev);
    so_kv(o, "Vendor", C_VALUE, f[1]);
    so_kvf(o, "Board vendor", C_VALUE, "%s (%04X:%04X)", *f[3] ? f[3] : "-", g->svid, g->sdid);
    gfx_target(g, t, sizeof t);
    if (ip_ver(g, "GC", &ma, &mi, &rv)) so_kvf(o, "Architecture", C_TEAL, "%s  GC %d.%d.%d  %s", gc_family(ma, mi), ma, mi, rv, t);
    so_kvf(o, "Kernel driver", C_ORANGE, "%s  (%s)", g->driver, g->card);
    if (gd_long(g, "mem_info_vram_total", &v)) {
        char a[32], c[32];
        gd_long(g, "mem_info_vram_used", &v2);
        fmt_bytes((double)v, a, sizeof a);
        fmt_bytes((double)v2, c, sizeof c);
        gd_str(g, "mem_info_vram_vendor", b, sizeof b);
        so_kvf(o, "VRAM", C_VALUE, "%s  used %s (%.1f%%)  vendor %s", a, c, v ? 100.0 * v2 / v : 0.0, *b ? b : "-");
    }
    if (gd_long(g, "mem_info_vis_vram_total", &v)) { char a[32]; fmt_bytes((double)v, a, sizeof a); so_kvf(o, "CPU-visible VRAM", C_VALUE, "%s%s", a, gd_long(g, "mem_info_vram_total", &v2) && v2 == v ? "  (Resizable BAR active)" : ""); }
    if (gd_long(g, "mem_info_gtt_total", &v)) { char a[32], c[32]; gd_long(g, "mem_info_gtt_used", &v2); fmt_bytes((double)v, a, sizeof a); fmt_bytes((double)v2, c, sizeof c); so_kvf(o, "GTT", C_VALUE, "%s  used %s", a, c); }
    if (gd_str(g, "vbios_version", b, sizeof b)) so_kv(o, "VBIOS", C_PURPLE, b);
    {
        char s1[64] = "", w1[16] = "", s2[64] = "", w2[16] = "";
        gd_str(g, "current_link_speed", s1, sizeof s1); gd_str(g, "current_link_width", w1, sizeof w1);
        gd_str(g, "max_link_speed", s2, sizeof s2); gd_str(g, "max_link_width", w2, sizeof w2);
        so_kvf(o, "PCIe link", C_TEAL, "%s x%s  (max %s x%s)", s1, w1, s2, w2);
    }
    if (gd_str(g, "unique_id", b, sizeof b)) so_kv(o, "Unique ID", C_VALUE, b);
    if (gd_long(g, "boot_vga", &v)) so_kv(o, "Boot VGA", C_VALUE, v ? "Yes (primary)" : "No");
    if (gd_str(g, "power_dpm_force_performance_level", b, sizeof b)) so_kv(o, "Performance level", C_ORANGE, b);
    if (gd_str(g, "power/runtime_status", b, sizeof b)) so_kv(o, "Runtime PM", C_VALUE, b);
    if (gd_str(g, "board_info", b, sizeof b)) so_kv(o, "Board info", C_VALUE, b);
}

static void s_gpu_info(srs_out *o)
{
    static const int tabs[] = { 150 };
    gpu_t g;
    so_siv_header(o, "GPU Information");
    so_tabs(o, tabs, 1);
    if (!gpu_sel(o, &g)) { so_err(o, "No DRM GPU found under /sys/class/drm"); return; }
    s_gpu_info_body(o, &g);
    so_board_footer(o);
}

/* ======================================================================
 * [gpu-clocks]  (live, pure sysfs)
 * ====================================================================== */
static void clocks_body(srs_out *o, const gpu_t *g)
{
    static const int tabs[] = { 150, 290 };
    long v;
    char b[64];
    so_tabs(o, tabs, 2);
    so_b(o, C_LABEL, "Clock"); so_tab(o); so_b(o, C_LABEL, "Current"); so_tab(o); so_b(o, C_LABEL, "DPM levels ([active])"); so_par(o);
    if (hw_long(g, "freq1_input", &v)) { mhz((double)v, b, sizeof b); so_t(o, C_LABEL, "GFX (sclk) sensor"); so_tab(o); so_t(o, C_GREEN, b); so_par(o); }
    if (hw_long(g, "freq2_input", &v)) { mhz((double)v, b, sizeof b); so_t(o, C_LABEL, "Memory (mclk) sensor"); so_tab(o); so_t(o, C_GREEN, b); so_par(o); }
    dpm_rows(o, g, "GFX (sclk)", "pp_dpm_sclk");
    dpm_rows(o, g, "Memory (mclk)", "pp_dpm_mclk");
    dpm_rows(o, g, "Fabric (fclk)", "pp_dpm_fclk");
    dpm_rows(o, g, "SoC (socclk)", "pp_dpm_socclk");
    dpm_rows(o, g, "DCE/DCN (dcefclk)", "pp_dpm_dcefclk");
    dpm_rows(o, g, "PCIe", "pp_dpm_pcie");
    if (gd_long(g, "gpu_busy_percent", &v)) { so_t(o, C_LABEL, "GPU load"); so_tab(o); so_f(o, C_ORANGE, "%ld%%", v); so_par(o); }
    if (gd_long(g, "mem_busy_percent", &v)) { so_t(o, C_LABEL, "Memory load"); so_tab(o); so_f(o, C_ORANGE, "%ld%%", v); so_par(o); }
    if (gd_str(g, "power_dpm_force_performance_level", b, sizeof b)) { so_t(o, C_LABEL, "Performance level"); so_tab(o); so_t(o, C_PURPLE, b); so_par(o); }
}

static void od_table(srs_out *o, const gpu_t *g)
{
    char p[512], *t;
    snprintf(p, sizeof p, "%s/pp_od_clk_voltage", g->dev);
    if (rd_all(p, &t) > 0) { so_par(o); so_b(o, C_VALUE, "OverDrive table (pp_od_clk_voltage, read only)"); so_par(o); so_lines(o, C_VALUE, t); }
    free(t);
}

static void s_gpu_clocks(srs_out *o)
{
    gpu_t g;
    so_siv_header(o, "GPU Clock Speeds");
    if (!gpu_sel(o, &g)) { so_err(o, "No DRM GPU found"); return; }
    clocks_body(o, &g);
    if (!o->gui) od_table(o, &g);
}

/* ======================================================================
 * [gpu-temp] [gpu-fans] [gpu-power]  (live, pure sysfs)
 * ====================================================================== */
static void temp_cell(srs_out *o, const gpu_t *g, const char *f, int color)
{
    long v;
    if (hw_long(g, f, &v) && v > -273000 && v < 1000000) so_f(o, color, "%.1f °C", v / 1000.0); else so_t(o, C_GRAY, "-");
}

static void temps_body(srs_out *o, const gpu_t *g)
{
    static const int tabs[] = { 110, 200, 290, 380 };
    int i;
    so_tabs(o, tabs, 4);
    so_b(o, C_LABEL, "Sensor"); so_tab(o); so_b(o, C_LABEL, "Current"); so_tab(o); so_b(o, C_LABEL, "Critical"); so_tab(o);
    so_b(o, C_LABEL, "Hysteresis"); so_tab(o); so_b(o, C_LABEL, "Emergency"); so_par(o);
    for (i = 1; i <= 8; i++) {
        char f[64], lb[64];
        long v;
        snprintf(f, sizeof f, "temp%d_input", i);
        if (!hw_long(g, f, &v)) continue;
        snprintf(f, sizeof f, "temp%d_label", i);
        if (!hw_str(g, f, lb, sizeof lb)) snprintf(lb, sizeof lb, "temp%d", i);
        so_t(o, C_LABEL, lb); so_tab(o);
        snprintf(f, sizeof f, "temp%d_input", i); temp_cell(o, g, f, C_GREEN); so_tab(o);
        snprintf(f, sizeof f, "temp%d_crit", i); temp_cell(o, g, f, C_RED); so_tab(o);
        snprintf(f, sizeof f, "temp%d_crit_hyst", i); temp_cell(o, g, f, C_VALUE); so_tab(o);
        snprintf(f, sizeof f, "temp%d_emergency", i); temp_cell(o, g, f, C_ORANGE);
        so_par(o);
    }
}

static void s_gpu_temp(srs_out *o)
{
    gpu_t g;
    char b[256];
    so_siv_header(o, "GPU Temperatures");
    if (!gpu_sel(o, &g)) { so_err(o, "No DRM GPU found"); return; }
    if (!*g.hwmon) { so_err(o, "GPU has no hwmon device"); return; }
    temps_body(o, &g);
    if (gd_str(&g, "thermal_throttling_logging", b, sizeof b)) { so_par(o); so_note(o, b); }
}

static void fans_body(srs_out *o, const gpu_t *g)
{
    static const int tabs[] = { 150 };
    long v, mx = 255, mn = 0;
    so_tabs(o, tabs, 1);
    if (hw_long(g, "fan1_input", &v)) so_kvf(o, "Fan 1 speed", C_GREEN, "%ld RPM", v);
    if (hw_long(g, "fan1_target", &v)) so_kvf(o, "Fan 1 target", C_VALUE, "%ld RPM", v);
    if (hw_long(g, "fan1_min", &v)) { long m2 = 0; hw_long(g, "fan1_max", &m2); so_kvf(o, "Fan 1 range", C_VALUE, "%ld - %ld RPM", v, m2); }
    if (hw_long(g, "pwm1", &v)) {
        hw_long(g, "pwm1_max", &mx); hw_long(g, "pwm1_min", &mn);
        so_kvf(o, "PWM duty", C_ORANGE, "%ld / %ld  (%.0f%%)", v, mx, mx ? 100.0 * v / mx : 0.0);
    }
    if (hw_long(g, "pwm1_enable", &v)) so_kv(o, "PWM mode", C_PURPLE, v == 0 ? "0 full speed" : v == 1 ? "1 manual" : v == 2 ? "2 automatic (firmware)" : "other");
    if (hw_long(g, "fan1_enable", &v)) so_kv(o, "Fan enabled", C_VALUE, v ? "Yes" : "No");
}

static void s_gpu_fans(srs_out *o)
{
    gpu_t g;
    so_siv_header(o, "GPU Fans");
    if (!gpu_sel(o, &g)) { so_err(o, "No DRM GPU found"); return; }
    if (!*g.hwmon) { so_err(o, "GPU has no hwmon device"); return; }
    fans_body(o, &g);
    so_par(o);
    {   /* say who controls the fan only when a known fan-control program is actually running */
        static const char *const ctl[] = { "lactd", "coolercontrold", "corectrl", "fancontrol", "amdgpu-fancontrol", "amdgpu-fan", NULL };
        char who[200] = "";
        int i;
        for (i = 0; ctl[i]; i++)
            if (gpu_proc_running(ctl[i])) { size_t l = strlen(who); snprintf(who + l, sizeof who - l, "%s%s", l ? ", " : "", ctl[i]); }
        if (*who) so_kvf(o, "Fan-control software", C_PURPLE, "running: %s", who);
        else so_kv(o, "Fan-control software", C_GRAY, "none found running (driver / firmware fan curve)");
    }
    so_note(o, "Read only: srs-gui never writes fan or PWM settings.");
}

static void gm_header(srs_out *o, const gpu_t *g)
{
    char p[512], *b;
    int n;
    snprintf(p, sizeof p, "%s/gpu_metrics", g->dev);
    n = rd_all(p, &b);
    if (n >= 4) {
        const unsigned char *u = (const unsigned char *)b;
        so_kvf(o, "gpu_metrics", C_PURPLE, "v%u.%u  %u bytes (header decoded only)", u[2], u[3], (unsigned)(u[0] | u[1] << 8));
    }
    free(b);
}

static void power_body(srs_out *o, const gpu_t *g)
{
    static const int tabs[] = { 150 };
    long v, d = 0, mn = 0, mx = 0;
    char b[128];
    so_tabs(o, tabs, 1);
    if (hw_str(g, "power1_label", b, sizeof b)) so_kv(o, "Power sensor", C_VALUE, b);
    if (hw_long(g, "power1_average", &v)) so_kvf(o, "Power (average)", C_GREEN, "%.1f W", v / 1e6);
    if (hw_long(g, "power1_input", &v)) so_kvf(o, "Power (input)", C_GREEN, "%.1f W", v / 1e6);
    if (hw_long(g, "power1_cap", &v)) {
        hw_long(g, "power1_cap_default", &d); hw_long(g, "power1_cap_min", &mn); hw_long(g, "power1_cap_max", &mx);
        so_kvf(o, "Power limit", C_ORANGE, "%.0f W  (default %.0f W, range %.0f - %.0f W)", v / 1e6, d / 1e6, mn / 1e6, mx / 1e6);
    }
    if (hw_long(g, "in0_input", &v)) { hw_str(g, "in0_label", b, sizeof b); so_kvf(o, *b ? b : "in0", C_TEAL, "%ld mV", v); }
    if (hw_long(g, "in1_input", &v)) { hw_str(g, "in1_label", b, sizeof b); so_kvf(o, *b ? b : "in1", C_TEAL, "%ld mV", v); }
    if (gd_long(g, "gpu_busy_percent", &v)) so_kvf(o, "GPU load", C_ORANGE, "%ld%%", v);
    if (gd_str(g, "power_dpm_state", b, sizeof b)) so_kv(o, "DPM state", C_VALUE, b);
    if (gd_str(g, "power_dpm_force_performance_level", b, sizeof b)) so_kv(o, "Performance level", C_PURPLE, b);
    if (gd_str(g, "power/runtime_status", b, sizeof b)) so_kv(o, "Runtime PM", C_VALUE, b);
    if (gd_str(g, "power_state", b, sizeof b)) so_kv(o, "PCI power state", C_VALUE, b);
}

static void s_gpu_power(srs_out *o)
{
    gpu_t g;
    so_siv_header(o, "GPU Power Status");
    if (!gpu_sel(o, &g)) { so_err(o, "No DRM GPU found"); return; }
    power_body(o, &g);
    if (!o->gui) gm_header(o, &g);
}

/* ======================================================================
 * [gpu-bios]
 * ====================================================================== */
static void bios_body(srs_out *o, const gpu_t *g)
{
    char b[256], p[512], **nm;
    int n, i;
    if (gd_str(g, "vbios_version", b, sizeof b)) so_kv(o, "VBIOS version", C_BLACK, b);
    if (gd_str(g, "vbios_build", b, sizeof b)) so_kv(o, "VBIOS build", C_VALUE, b);
    snprintf(p, sizeof p, "%s/rom", g->dev);
    so_kv(o, "Option ROM", C_VALUE, path_exists(p) ? "present (not read)" : "-");
    snprintf(p, sizeof p, "%s/fw_version", g->dev);
    n = dir_list(p, &nm);
    if (n > 0) {
        so_par(o); so_b(o, C_VALUE, "Firmware versions"); so_par(o);
        for (i = 0; i < n; i++) {
            char k[64];
            if (rd_fmt(b, sizeof b, "%s/%s", p, nm[i])) {
                snprintf(k, sizeof k, "%s", nm[i]);
                if (strstr(k, "_fw_version")) *strstr(k, "_fw_version") = 0;
                so_kv(o, k, C_PURPLE, b);
            }
        }
    }
    dir_free(nm, n);
}

/* the driver's cached VBIOS copy (srs-root: debugfs amdgpu_vbios -> vbios-<pci>.bin) */
static unsigned vb16(const unsigned char *p) { return (unsigned)(p[0] | p[1] << 8); }

static void vbios_image(srs_out *o, const gpu_t *g)
{
    static const int itabs[] = { 80, 160, 250, 340, 440 };
    char path[1200], b[128];
    const char *rf;
    unsigned char *d = NULL;
    int n, off, k, nimg = 0, nparts = 0;
    char parts[4][64];
    snprintf(b, sizeof b, "vbios-%s.bin", g->slot);
    rf = so_root_file(b);
    so_par(o);
    so_head(o, "VBIOS image (the driver's copy)");
    if (!rf) {
        if (strcmp(g->driver, "amdgpu")) so_kv(o, "Image", C_GRAY, "only the amdgpu driver keeps a copy SRS can read");
        else if (so_root_file("collected.txt")) so_kv(o, "Image", C_ORANGE, "not in the root cache (debugfs not mounted, or the cache is older): run Menu > Run as root again");
        else so_kv(o, "Image", C_ORANGE, "Needs root: Menu > Run as root (srs-root)");
        return;
    }
    snprintf(path, sizeof path, "%s", rf);   /* so_root_file has one static buffer */
    n = rd_all(path, (char **)&d);
    if (n <= 0) { so_kv(o, "Image", C_ORANGE, "cache file is empty"); free(d); return; }
    fmt_bytes((double)n, b, sizeof b);
    so_kvf(o, "Size", C_VALUE, "%s (%d bytes)", b, n);
    so_kv(o, "Cache", C_GRAY, path);
    if (n < 3 || d[0] != 0x55 || d[1] != 0xAA) {
        so_kvf(o, "Signature", C_RED, "%02X %02X, not 55 AA: not a PCI expansion ROM image", d[0], n > 1 ? d[1] : 0);
        free(d);
        return;
    }
    so_kv(o, "Signature", C_GREEN, "55 AA (PCI expansion ROM)");
    {
        int len = d[2] * 512, sum = 0, i;
        if (len > 0 && len <= n) {
            for (i = 0; i < len; i++) sum += d[i];
            so_kvf(o, "Checksum", sum % 256 ? C_RED : C_GREEN, "%s (first image, %d bytes, sum mod 256 = 0x%02X)", sum % 256 ? "bad" : "valid", len, sum % 256);
        } else so_kvf(o, "Checksum", C_ORANGE, "not checked: first image length %d bytes does not fit the %d-byte copy", len, n);
    }
    /* part number / build strings: printable runs that hold an AMD part number (113-...) or the ATOMBIOS build tag */
    for (off = 0; off < n && nparts < 4; ) {
        int e = off;
        while (e < n && d[e] >= 0x20 && d[e] < 0x7F) e++;
        if (e - off >= 8) {
            char run[64];
            const char *m;
            snprintf(run, sizeof run, "%.*s", e - off > 63 ? 63 : e - off, (const char *)d + off);
            m = strstr(run, "113-");
            if (m || !strncmp(run, "ATOMBIOSBK", 10)) {
                char *t = (char *)(m ? m : run);
                int dup = 0;
                while (*t && t[strlen(t) - 1] == ' ') t[strlen(t) - 1] = 0;
                for (k = 0; k < nparts; k++) if (!strcmp(parts[k], t)) dup = 1;
                if (!dup) snprintf(parts[nparts++], sizeof parts[0], "%s", t);
            }
        }
        off = e + 1;
    }
    for (k = 0; k < nparts; k++) so_kv(o, strncmp(parts[k], "113-", 4) ? "Build string" : "Part number", C_BLACK, parts[k]);
    /* the image chain: each image has a PCI data structure ("PCIR") */
    so_par(o);
    so_tabs(o, itabs, 5);
    so_row(o, 6, (const char *const[]){ "Image", "Offset", "Size", "PCI ID", "Code type", "Last" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (off = 0; off + 0x1A <= n && d[off] == 0x55 && d[off + 1] == 0xAA && nimg < 8; nimg++) {
        unsigned pr = (unsigned)off + vb16(d + off + 0x18);
        char c1[16], c2[16], c3[24], c4[16], c5[24];
        int ilen, ct, last;
        if (pr + 0x18 > (unsigned)n || memcmp(d + pr, "PCIR", 4)) break;
        ilen = (int)vb16(d + pr + 0x10) * 512;
        ct = d[pr + 0x14];
        last = (d[pr + 0x15] & 0x80) != 0;
        snprintf(c1, sizeof c1, "%d", nimg + 1);
        snprintf(c2, sizeof c2, "0x%05X", off);
        fmt_bytes((double)ilen, c3, sizeof c3);
        snprintf(c4, sizeof c4, "%04X:%04X", vb16(d + pr + 4), vb16(d + pr + 6));
        snprintf(c5, sizeof c5, "%s", ct == 0 ? "x86 (legacy)" : ct == 1 ? "Open Firmware" : ct == 3 ? "EFI (GOP)" : "other");
        so_row(o, 6, (const char *const[]){ c1, c2, c3, c4, c5, last ? "yes" : "no" },
               (const int[]){ C_LABEL, C_GRAY, C_VALUE, C_VALUE, C_TEAL, C_GRAY });
        if (last || ilen <= 0) { nimg++; break; }
        off += ilen;
    }
    if (!nimg) { so_note(o, "no PCI data structure (PCIR) found"); }
    so_par(o);
    so_note(o, "Copied once by srs-root from the driver's memory (debugfs amdgpu_vbios); the GPU's ROM is not accessed.");
    free(d);
}

static void s_gpu_bios(srs_out *o)
{
    static const int tabs[] = { 150 };
    gpu_t g;
    so_siv_header(o, "GPU VBIOS Status");
    so_tabs(o, tabs, 1);
    if (!gpu_sel(o, &g)) { so_err(o, "No DRM GPU found"); return; }
    bios_body(o, &g);
    vbios_image(o, &g);
}

/* ======================================================================
 * [gpu-map]
 * ====================================================================== */
static void map_body(srs_out *o, const gpu_t *g)
{
    char p[1024], b[512], real[PATH_MAX];
    long v;
    char *t;
    int i, n;
    char **nm = NULL;
    so_kvf(o, "Bus/Dev/Func", C_BLACK, "%s", g->slot);
    if (realpath(g->dev, real)) so_kv(o, "PCI path", C_VALUE, real + (starts(real, "/sys/devices/") ? 13 : 0));
    snprintf(p, sizeof p, "%s/iommu_group", g->dev);
    base_name(p, b, sizeof b); so_kv(o, "IOMMU group", C_VALUE, b);
    if (gd_long(g, "irq", &v)) so_kvf(o, "IRQ", C_VALUE, "%ld", v);
    if (gd_str(g, "numa_node", b, sizeof b)) so_kv(o, "NUMA node", C_VALUE, b);
    if (gd_str(g, "local_cpulist", b, sizeof b)) so_kv(o, "Local CPUs", C_VALUE, b);
    snprintf(p, sizeof p, "%s/drm", g->dev);
    n = dir_list(p, &nm);
    b[0] = 0;
    for (i = 0; i < n; i++) { strncat(b, nm[i], sizeof b - strlen(b) - 2); strncat(b, " ", sizeof b - strlen(b) - 1); }
    dir_free(nm, n);
    so_kv(o, "DRM nodes", C_TEAL, b);
    so_kv(o, "hwmon", C_TEAL, *g->hwmon ? strrchr(g->hwmon, '/') + 1 : "-");
    /* sibling functions (HDMI audio) */
    {
        char sl[32], pre[32];
        snprintf(pre, sizeof pre, "%.*s", (int)strlen(g->slot) - 1, g->slot);
        n = dir_list("/sys/bus/pci/devices", &nm);
        for (i = 0; i < n; i++) {
            if (!starts(nm[i], pre) || !strcmp(nm[i], g->slot)) continue;
            snprintf(sl, sizeof sl, "%s", nm[i]);
            {
                char f[5][160];
                lspci_fields(sl, f);
                so_kvf(o, "Function", C_VALUE, "%s  %s  %s", sl, f[0], f[2]);
            }
        }
        dir_free(nm, n);
    }
    /* BARs */
    snprintf(p, sizeof p, "%s/resource", g->dev);
    if (rd_all(p, &t) > 0) {
        const char *q = t;
        char l[256];
        i = 0;
        so_par(o); so_b(o, C_VALUE, "Resources"); so_par(o);
        while ((q = next_line(q, l, sizeof l))) {
            unsigned long long s, e, fl;
            if (sscanf(l, "%llx %llx %llx", &s, &e, &fl) == 3 && e) {
                char k[32], sz[32];
                fmt_bytes((double)(e - s + 1), sz, sizeof sz);
                snprintf(k, sizeof k, i < 6 ? "BAR %d" : "ROM", i);
                so_kvf(o, k, C_PURPLE, "0x%llx - 0x%llx  %s  %s%s", s, e, sz, (fl & 0x100) ? "I/O" : "Memory", (fl & 0x2000) ? " prefetchable" : "");
            }
            i++;
        }
    }
    free(t);
}

static void s_gpu_map(srs_out *o)
{
    static const int tabs[] = { 150 };
    gpu_t g;
    so_siv_header(o, "GPU Mappings");
    so_tabs(o, tabs, 1);
    if (!gpu_sel(o, &g)) { so_err(o, "No DRM GPU found"); return; }
    map_body(o, &g);
}

/* ======================================================================
 * [gpu-tree] [monitor-tree]
 * ====================================================================== */
static void tree_gpu(srs_out *o, const gpu_t *g, int with_hw)
{
    conn_t c[16];
    int n = connectors(g, c, 16), i;
    char nm[160], real[PATH_MAX];
    gpu_name(g, nm, sizeof nm);
    so_b(o, C_BLACK, g->card); so_t(o, C_VALUE, "  "); so_t(o, C_VALUE, nm); so_par(o);
    if (with_hw) {
        if (realpath(g->dev, real)) { so_t(o, C_GRAY, "  +- PCI "); so_t(o, C_PURPLE, real + (starts(real, "/sys/devices/") ? 13 : 0)); so_par(o); }
        so_t(o, C_GRAY, "  +- driver "); so_t(o, C_ORANGE, g->driver); so_par(o);
        if (*g->hwmon) { so_t(o, C_GRAY, "  +- "); so_t(o, C_TEAL, strrchr(g->hwmon, '/') + 1); so_par(o); }
        {
            char p[512], **d;
            int k, j;
            snprintf(p, sizeof p, "%s/drm", g->dev);
            k = dir_list(p, &d);
            for (j = 0; j < k; j++) if (!starts(d[j], "card")) { so_t(o, C_GRAY, "  +- /dev/dri/"); so_t(o, C_TEAL, d[j]); so_par(o); }
            dir_free(d, k);
        }
    }
    for (i = 0; i < n; i++) {
        edid_t e;
        int con = !strcmp(c[i].status, "connected");
        so_t(o, C_GRAY, i == n - 1 ? "  \\- " : "  +- ");
        so_t(o, con ? C_GREEN : C_GRAY, c[i].conn);
        so_f(o, C_GRAY, "  %s %s", c[i].status, c[i].enabled);
        so_par(o);
        if (con && conn_edid(c[i].name, &e)) {
            so_t(o, C_GRAY, i == n - 1 ? "       \\- " : "  |    \\- ");
            so_f(o, C_BLACK, "%s %s", e.pnp, *e.name ? e.name : "(no name)");
            so_f(o, C_VALUE, "  %s  %d×%d", e.mfg, e.w, e.h);
            so_par(o);
        }
    }
}

static void s_gpu_tree(srs_out *o)
{
    gpu_t g[8];
    int n = gpus(g, 8), i;
    so_siv_header(o, "GPU Tree");
    if (!n) { so_err(o, "No DRM GPU found"); return; }
    for (i = 0; i < n; i++) { tree_gpu(o, &g[i], 1); so_par(o); }
}

static void s_monitor_tree(srs_out *o)
{
    gpu_t g[8];
    int n = gpus(g, 8), i;
    so_siv_header(o, "Monitor Tree");
    if (!n) { so_err(o, "No DRM GPU found"); return; }
    for (i = 0; i < n; i++) { tree_gpu(o, &g[i], 0); so_par(o); }
}

/* ======================================================================
 * [monitors] [mon-info]
 * ====================================================================== */
static void monitor_detail(srs_out *o, const conn_t *c, const edid_t *e, kms_conn *k)
{
    static const int tabs[] = { 160 };
    int i;
    so_tabs(o, tabs, 1);
    so_b(o, C_VALUE, c->conn); so_f(o, C_GRAY, "  (%s, connector id %d)", c->name, c->id); so_par(o);
    so_kvf(o, "Monitor", C_BLACK, "%s", *e->name ? e->name : "-");
    so_kvf(o, "Manufacturer", C_VALUE, "%s  %s", e->pnp, *e->mfg ? e->mfg : "(not in pnp.ids)");
    so_kvf(o, "Product code", C_PURPLE, "0x%04X  (%s%04X)", e->product, e->pnp, e->product);
    so_kvf(o, "Serial number", C_VALUE, "%s  (numeric 0x%08X)", *e->serial_s ? e->serial_s : "-", e->serial);
    if (e->week && e->week != 0xff) so_kvf(o, "Manufactured", C_VALUE, "%d week %d", e->year, e->week);
    else if (e->week == 0xff) so_kvf(o, "Model year", C_VALUE, "%d", e->year);
    else so_kvf(o, "Manufactured", C_VALUE, "%d", e->year);
    so_kvf(o, "EDID version", C_VALUE, "%d.%d  checksum %s  %d extension%s", e->ver, e->rev, e->csum_ok ? "OK" : "BAD", e->ext, e->ext == 1 ? "" : "s");
    if (e->ext) {
        char b[256] = "";
        for (i = 0; i < e->ext && i < 8; i++) { strncat(b, ext_tag_name(e->ext_tags[i]), sizeof b - strlen(b) - 3); strncat(b, "  ", sizeof b - strlen(b) - 1); }
        so_kv(o, "Extensions", C_VALUE, b);
    }
    so_kvf(o, "Display size", C_VALUE, "%d × %d cm", e->hcm, e->vcm);
    if (e->hmm) so_kvf(o, "Image size", C_VALUE, "%d × %d mm", e->hmm, e->vmm);
    if (e->digital && (e->ver > 1 || e->rev >= 4)) so_kvf(o, "Input", C_VALUE, "Digital, %d bpc, %s", e->bpc, iface_name(e->iface));
    else if (e->digital) so_kv(o, "Input", C_VALUE, "Digital");
    else so_kv(o, "Input", C_VALUE, "Analog");
    if (e->gamma > 0) so_kvf(o, "Gamma", C_VALUE, "%.2f", e->gamma);
    if (e->dtd) so_kvf(o, "Preferred mode", C_TEAL, "%d × %d @ %.2f Hz  (pixel clock %.2f MHz)", e->w, e->h, e->rate, e->pclk / 1e6);
    if (e->rng) {
        so_kvf(o, "Vertical rates", C_VALUE, "%d - %d Hz", e->vmin, e->vmax);
        so_kvf(o, "Horizontal rates", C_VALUE, "%d - %d kHz", e->hmin, e->hmax);
        so_kvf(o, "Max pixel clock", C_VALUE, "%d MHz", e->pclk_max);
    }
    if (*e->text) so_kv(o, "Text", C_VALUE, e->text);
    so_kvf(o, "Status", C_VALUE, "%s, %s, DPMS %s", c->status, c->enabled, c->dpms);
    if (k) {
        if (*k->cur) so_kv(o, "Current mode", C_GREEN, k->cur);
        so_kvf(o, "Adaptive sync", C_ORANGE, "%s%s", k->vrr_cap ? "capable" : "not capable", k->vrr_cap ? (k->vrr_en ? ", enabled" : ", off now") : "");
    }
    so_par(o);
}

static void s_monitors(srs_out *o)
{
    static const int tabs[] = { 63, 280, 350, 455, 520, 610, 710, 800, 880 };
    gpu_t g[8];
    conn_t c[16];
    kms_conn kc[16];
    kms_crtc kr[16];
    int ng = gpus(g, 8), i, j, nc, total = 0, shown = 0, nkc = 0, nkr = 0;
    char host[128] = "";
    gethostname(host, sizeof host);
    kms_parse(kc, 16, &nkc, kr, 16, &nkr);
    for (i = 0; i < ng; i++) {
        nc = connectors(&g[i], c, 16);
        for (j = 0; j < nc; j++) {
            if (strcmp(c[j].status, "connected")) continue;
            total++;
            if (!(o->arg && *o->arg && strcmp(o->arg, c[j].conn) && strcmp(o->arg, c[j].name))) shown++;
        }
    }
    so_tabs(o, tabs, 9);
    so_t(o, C_LABEL, "Monitor"); so_tab(o); so_f(o, C_LABEL, "Showing %d of %d for \\\\%s", shown, total, host); so_tab(o);
    shown = 0;
    so_t(o, C_LABEL, "Device"); so_tab(o); so_t(o, C_LABEL, "Serial"); so_tab(o); so_t(o, C_LABEL, "EDID"); so_tab(o);
    so_t(o, C_LABEL, "Manufacture"); so_tab(o); so_t(o, C_LABEL, "Monitor"); so_tab(o); so_t(o, C_LABEL, "Monitor"); so_tab(o);
    so_t(o, C_LABEL, "Vertical"); so_tab(o); so_t(o, C_LABEL, "Horizontal"); so_par(o);
    so_t(o, C_LABEL, "PnP ID"); so_tab(o); so_t(o, C_LABEL, "Device Description"); so_tab(o); so_t(o, C_LABEL, "Name"); so_tab(o);
    so_t(o, C_LABEL, "Number"); so_tab(o); so_t(o, C_LABEL, "Version"); so_tab(o); so_t(o, C_LABEL, "Year - Week"); so_tab(o);
    so_t(o, C_LABEL, "Display Size"); so_tab(o); so_t(o, C_LABEL, "Resolution"); so_tab(o); so_t(o, C_LABEL, "Rates"); so_tab(o);
    so_t(o, C_LABEL, "Rates"); so_par(o); so_par(o);
    for (i = 0; i < ng; i++) {
        nc = connectors(&g[i], c, 16);
        for (j = 0; j < nc; j++) {
            edid_t e;
            char id[16], cells[10][96];
            const char *cp[10];
            static const int cols[10] = { C_PURPLE, C_BLACK, C_TEAL, C_VALUE, C_VALUE, C_VALUE, C_VALUE, C_GREEN, C_VALUE, C_VALUE };
            int k;
            if (strcmp(c[j].status, "connected")) continue;
            if (o->arg && *o->arg && strcmp(o->arg, c[j].conn) && strcmp(o->arg, c[j].name)) continue;
            if (!conn_edid(c[j].name, &e)) {
                so_t(o, C_VALUE, ""); so_tab(o); so_f(o, C_ORANGE, "%s: no EDID", c[j].conn); so_par(o);
                continue;
            }
            snprintf(id, sizeof id, "%s%04X", e.pnp, e.product);
            snprintf(cells[0], 96, "%s", id);
            snprintf(cells[1], 96, "%s %s", *e.mfg ? e.mfg : e.pnp, e.name);
            snprintf(cells[2], 96, "%s", c[j].conn);
            snprintf(cells[3], 96, "%s", *e.serial_s ? e.serial_s : "-");
            snprintf(cells[4], 96, "%d.%d", e.ver, e.rev);
            if (e.week && e.week != 0xff) snprintf(cells[5], 96, "%d - %d", e.year, e.week); else snprintf(cells[5], 96, "%d", e.year);
            snprintf(cells[6], 96, "%d×%d cm", e.hcm, e.vcm);
            snprintf(cells[7], 96, "%d×%d", e.w, e.h);
            if (e.rng) { snprintf(cells[8], 96, "%d-%d", e.vmin, e.vmax); snprintf(cells[9], 96, "%d-%d", e.hmin, e.hmax); }
            else { snprintf(cells[8], 96, "%.0f", e.rate); snprintf(cells[9], 96, "-"); }
            for (k = 0; k < 10; k++) cp[k] = cells[k];
            so_tabs(o, tabs, 9);
            so_row(o, 10, cp, cols);
            shown++;
        }
    }
    if (!shown) so_note(o, "No connected monitor with EDID found");
    so_par(o);
    for (i = 0; i < ng; i++) {
        nc = connectors(&g[i], c, 16);
        for (j = 0; j < nc; j++) {
            edid_t e;
            if (strcmp(c[j].status, "connected") || !conn_edid(c[j].name, &e)) continue;
            if (o->arg && *o->arg && strcmp(o->arg, c[j].conn) && strcmp(o->arg, c[j].name)) continue;
            monitor_detail(o, &c[j], &e, kms_find(kc, nkc, c[j].id));
        }
    }
    {
        struct stat st;
        char d[32] = "-";
        int cnt = 0;
        char *t;
        if (rd_all(pnp_ids(), &t) > 0) { const char *p = t; while ((p = strchr(p, '\n'))) { cnt++; p++; } }
        free(t);
        if (stat(pnp_ids(), &st) == 0) strftime(d, sizeof d, "%Y-%m-%d", localtime(&st.st_mtime));
        so_f(o, C_GRAY, "Monitor names from %s Date %s (%d)", pnp_ids(), d, cnt);
        so_par(o);
    }
}

static void s_mon_info(srs_out *o)
{
    static const int tabs[] = { 60 };
    struct stat st;
    char d[32] = "-", *t, l[256];
    const char *p;
    int cnt = 0, shown = 0;
    gpu_t g[8];
    conn_t c[16];
    int ng, i, j, nc;
    so_siv_header(o, "Monitor ID Database");
    if (rd_all(pnp_ids(), &t) <= 0) { free(t); so_err(o, "pnp.ids not found (package hwdata)"); return; }
    if (stat(pnp_ids(), &st) == 0) strftime(d, sizeof d, "%Y-%m-%d", localtime(&st.st_mtime));
    p = t; while ((p = strchr(p, '\n'))) { cnt++; p++; }
    so_tabs(o, tabs, 1);
    so_f(o, C_VALUE, "Database %s  Date %s  %d PnP vendor IDs", pnp_ids(), d, cnt); so_par(o); so_par(o);
    so_b(o, C_LABEL, "Connected monitors"); so_par(o);
    ng = gpus(g, 8);
    for (i = 0; i < ng; i++) {
        nc = connectors(&g[i], c, 16);
        for (j = 0; j < nc; j++) {
            edid_t e;
            if (strcmp(c[j].status, "connected") || !conn_edid(c[j].name, &e)) continue;
            so_t(o, C_PURPLE, e.pnp); so_tab(o);
            so_f(o, C_BLACK, "%s  ", *e.mfg ? e.mfg : "(unknown vendor)");
            so_f(o, C_VALUE, "%s%04X %s  on %s", e.pnp, e.product, e.name, c[j].conn);
            so_par(o);
        }
    }
    so_par(o);
    if (o->arg && *o->arg) {
        so_f(o, C_LABEL, "Lookup \"%s\"", o->arg); so_par(o);
        p = t;
        while ((p = next_line(p, l, sizeof l)))
            if (strcasestr(l, o->arg)) {
                char *tb = strchr(l, '\t');
                if (!tb) continue;
                *tb = 0;
                so_t(o, C_PURPLE, l); so_tab(o); so_t(o, C_VALUE, tb + 1); so_par(o);
                if (++shown >= 200) break;
            }
        for (i = 0; pnp_extra[i][0]; i++)
            if (strcasestr(pnp_extra[i][0], o->arg) || strcasestr(pnp_extra[i][1], o->arg)) {
                so_t(o, C_PURPLE, pnp_extra[i][0]); so_tab(o); so_f(o, C_VALUE, "%s  (built-in, not in pnp.ids)", pnp_extra[i][1]); so_par(o);
                shown++;
            }
        if (!shown) so_note(o, "no match");
    } else {
        so_note(o, "Open as mon-info:<text> to search the database (e.g. mon-info:GSM or mon-info:BenQ).");
    }
    free(t);
}

/* ======================================================================
 * [graphics]
 * ====================================================================== */
static void s_graphics(srs_out *o)
{
    static const int tabs[] = { 105, 200 };
    gpu_t g[8];
    kms_conn kc[16];
    kms_crtc kr[16];
    int ng = gpus(g, 8), i, j, nkc = 0, nkr = 0, disp = 0;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    kms_parse(kc, 16, &nkc, kr, 16, &nkr);
    for (i = 0; i < ng; i++) {
        conn_t c[16];
        int nc = connectors(&g[i], c, 16);
        char nm[160], f[5][160], vb[128] = "", vr[32] = "-";
        long v;
        gpu_name(&g[i], nm, sizeof nm);
        lspci_fields(g[i].slot, f);
        gd_str(&g[i], "vbios_version", vb, sizeof vb);
        if (gd_long(&g[i], "mem_info_vram_total", &v)) fmt_bytes((double)v, vr, sizeof vr);
        for (j = 0; j < nc; j++) {
            edid_t e;
            kms_conn *k;
            int m, uniq = 0;
            char res[64] = "-";
            if (strcmp(c[j].status, "connected")) continue;
            conn_edid(c[j].name, &e);
            k = kms_find(kc, nkc, c[j].id);
            so_tabs(o, tabs, 2);
            so_tab(o); so_f(o, C_BLACK, "Display %d  %s  VBIOS %s  %s", disp, vr, vb, nm); so_par(o); so_par(o);
            so_kv(o, "Graphics Card", C_BLACK, nm);
            so_kvf(o, "Monitor", C_VALUE, "%s %s (%s)", e.ok ? e.mfg : "", e.ok ? e.name : "unknown", c[j].conn);
            so_kvf(o, "Graphics Chip", C_VALUE, "%s", f[2]);
            so_kv(o, "Graphics DAC", C_VALUE, "Integrated (digital output)");
            so_kvf(o, "Connector", C_VALUE, "%s  connector id %d  DPMS %s", c[j].conn, c[j].id, c[j].dpms);
            if (k && *k->cur) snprintf(res, sizeof res, "%s", k->cur);
            so_kv(o, "Resolution", C_GREEN, res);
            if (k && k->nm) {
                for (m = 0; m < k->nm; m++) {
                    int dup = 0, q;
                    char a[48], b2[48];
                    snprintf(a, sizeof a, "%s", k->modes[m]); if (strchr(a, '@')) *strchr(a, '@') = 0;
                    for (q = 0; q < m; q++) { snprintf(b2, sizeof b2, "%s", k->modes[q]); if (strchr(b2, '@')) *strchr(b2, '@') = 0; if (!strcmp(a, b2)) dup = 1; }
                    uniq += !dup;
                }
                so_par(o);
                so_t(o, C_LABEL, ""); so_tab(o); so_f(o, C_VALUE, "Unique %d of %d", uniq, k->nm); so_par(o); so_par(o);
                {
                    /* one row per unique resolution, its refresh rates after it ("*" = preferred) */
                    int row = 0;
                    for (m = 0; m < k->nm; m++) {
                        char a[48], b2[48], rates[512] = "";
                        int q, seen = 0;
                        snprintf(a, sizeof a, "%s", k->modes[m]); if (strchr(a, '@')) *strchr(a, '@') = 0;
                        for (q = 0; q < m; q++) { snprintf(b2, sizeof b2, "%s", k->modes[q]); if (strchr(b2, '@')) *strchr(b2, '@') = 0; if (!strcmp(a, b2)) seen = 1; }
                        if (seen) continue;
                        for (q = m; q < k->nm; q++) {
                            const char *at;
                            snprintf(b2, sizeof b2, "%s", k->modes[q]); if (strchr(b2, '@')) *strchr(b2, '@') = 0;
                            if (strcmp(a, b2) || !(at = strchr(k->modes[q], '@'))) continue;
                            if (strstr(rates, at + 1) && !strchr(at, '*')) continue;
                            strncat(rates, at + 1, sizeof rates - strlen(rates) - 3);
                            strncat(rates, "  ", sizeof rates - strlen(rates) - 1);
                        }
                        so_t(o, C_LABEL, row++ == 0 ? "Video Modes" : ""); so_tab(o);
                        so_t(o, C_VALUE, a); so_tab(o);
                        so_t(o, C_TEAL, rates); so_par(o);
                    }
                }
            } else {
                so_note(o, "Mode list needs drm_info (not installed)");
            }
            so_par(o);
            disp++;
        }
        if (!nc || !disp) { so_kv(o, "Graphics Card", C_BLACK, nm); so_kv(o, "Monitor", C_GRAY, "none connected"); }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    so_f(o, C_GRAY, "Total of %d Display%s reported in %.3f Seconds  Monitor names from /usr/share/hwdata/pnp.ids", disp, disp == 1 ? "" : "s",
         (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9);
    so_par(o);
}

/* ======================================================================
 * [gpu-all]
 * ====================================================================== */
static void s_gpu_all(srs_out *o)
{
    static const int tabs[] = { 150 };
    gpu_t g;
    so_siv_header(o, "GPU All Details");
    if (!gpu_sel(o, &g)) { so_err(o, "No DRM GPU found"); return; }
    so_tabs(o, tabs, 1); so_head(o, "Information"); s_gpu_info_body(o, &g); so_par(o);
    so_head(o, "Clocks"); clocks_body(o, &g); od_table(o, &g); so_par(o);
    so_head(o, "Temperatures"); temps_body(o, &g); so_par(o);
    so_head(o, "Fans"); fans_body(o, &g); so_par(o);
    so_head(o, "Power"); power_body(o, &g); gm_header(o, &g); so_par(o);
    so_tabs(o, tabs, 1); so_head(o, "VBIOS / firmware"); bios_body(o, &g); so_par(o);
    so_tabs(o, tabs, 1); so_head(o, "Mapping"); map_body(o, &g); so_par(o);
    {
        const char *a[] = { "lspci", "-vv", "-nn", "-s", g.slot, NULL };
        char *out = NULL;
        so_head(o, "lspci -vv (unprivileged)");
        if (run_to(&out, 10, a) >= 0 && *out) so_lines_max(o, C_VALUE, out, 200); else so_note(o, "lspci not available");
        free(out);
    }
    so_board_footer(o);
}

/* ======================================================================
 * [gpu-gens]
 * ====================================================================== */
static void s_gpu_gens(srs_out *o)
{
    static const int tabs[] = { 120, 220, 300 };
    gpu_t g;
    char p[512], **nm, t[32];
    int n, i, ma, mi, rv;
    so_siv_header(o, "GPU Generations");
    if (!gpu_sel(o, &g)) { so_err(o, "No DRM GPU found"); return; }
    so_tabs(o, tabs, 3);
    gfx_target(&g, t, sizeof t);
    if (ip_ver(&g, "GC", &ma, &mi, &rv)) {
        so_kvf(o, "Generation", C_BLACK, "%s", gc_family(ma, mi));
        so_kvf(o, "GFX IP", C_TEAL, "%d.%d.%d  (LLVM target %s)", ma, mi, rv, t);
    } else so_kv(o, "Generation", C_GRAY, "no ip_discovery data (pre-Navi or non-AMD GPU)");
    so_par(o);
    snprintf(p, sizeof p, "%s/ip_discovery/die/0", g.dev);
    n = dir_list(p, &nm);
    if (n > 0) {
        so_b(o, C_LABEL, "IP block"); so_tab(o); so_b(o, C_LABEL, "Version"); so_tab(o); so_b(o, C_LABEL, "Instances"); so_tab(o); so_b(o, C_LABEL, "HW ID"); so_par(o);
        for (i = 0; i < n; i++) {
            char b[64], q[600];
            long hw = -1, ni = 0;
            int k = 0;
            char **ins;
            if (!isalpha((unsigned char)nm[i][0]) || !strcmp(nm[i], "num_ips")) continue;
            if (!ip_ver(&g, nm[i], &ma, &mi, &rv)) continue;
            snprintf(q, sizeof q, "%s/%s/0/hw_id", p, nm[i]); rd_long(q, &hw);
            snprintf(q, sizeof q, "%s/%s", p, nm[i]);
            k = dir_list(q, &ins); ni = k > 0 ? k : 0; dir_free(ins, k);
            snprintf(b, sizeof b, "%d.%d.%d", ma, mi, rv);
            so_t(o, C_PURPLE, nm[i]); so_tab(o); so_t(o, C_VALUE, b); so_tab(o); so_f(o, C_VALUE, "%ld", ni); so_tab(o); so_f(o, C_GRAY, "%ld", hw); so_par(o);
        }
    }
    dir_free(nm, n);
}

/* ======================================================================
 * [gpu-reg]
 * ====================================================================== */
static void s_gpu_reg(srs_out *o)
{
    static const int tabs[] = { 200 };
    gpu_t g;
    char p[512], *t, b[256], **nm;
    int n, i;
    so_siv_header(o, "GPU Registry (driver registration)");
    if (!gpu_sel(o, &g)) { so_err(o, "No DRM GPU found"); return; }
    so_tabs(o, tabs, 1);
    snprintf(p, sizeof p, "%s/uevent", g.dev);
    if (rd_all(p, &t) > 0) {
        const char *q = t;
        char l[256];
        so_b(o, C_VALUE, "uevent"); so_par(o);
        while ((q = next_line(q, l, sizeof l))) { char *e = strchr(l, '='); if (!e) continue; *e = 0; so_kv(o, l, C_VALUE, e + 1); }
    }
    free(t);
    snprintf(p, sizeof p, "%s/driver/module", g.dev); base_name(p, b, sizeof b);
    so_kv(o, "Module", C_ORANGE, b);
    if (rd_str("/sys/module/amdgpu/srcversion", b, sizeof b)) so_kv(o, "Module srcversion", C_VALUE, b);
    {
        struct utsname u;
        if (!uname(&u)) so_kv(o, "Kernel", C_VALUE, u.release);
    }
    so_par(o);
    snprintf(p, sizeof p, "/sys/module/%s/parameters", g.driver);
    n = dir_list(p, &nm);
    if (n > 0) {
        so_b(o, C_VALUE, "Module parameters"); so_f(o, C_GRAY, "  (%s)", p); so_par(o);
        for (i = 0; i < n; i++) {
            if (rd_fmt(b, sizeof b, "%s/%s", p, nm[i])) so_kv(o, nm[i], C_PURPLE, b);
        }
    }
    dir_free(nm, n);
}

/* ======================================================================
 * [gpu-scf]  (AMD Scalable Control/Data Fabric view)
 * ====================================================================== */
static void s_gpu_scf(srs_out *o)
{
    static const int tabs[] = { 150, 290 };
    gpu_t g;
    int ma, mi, rv;
    so_siv_header(o, "GPU SCF (Scalable Control / Data Fabric)");
    if (!gpu_sel(o, &g)) { so_err(o, "No DRM GPU found"); return; }
    so_tabs(o, tabs, 2);
    if (ip_ver(&g, "DF", &ma, &mi, &rv)) so_kvf(o, "Data Fabric IP", C_TEAL, "%d.%d.%d", ma, mi, rv);
    if (ip_ver(&g, "MP1", &ma, &mi, &rv)) so_kvf(o, "SMU (MP1) IP", C_TEAL, "%d.%d.%d", ma, mi, rv);
    if (ip_ver(&g, "NBIF", &ma, &mi, &rv)) so_kvf(o, "NBIF IP", C_TEAL, "%d.%d.%d", ma, mi, rv);
    if (ip_ver(&g, "MMHUB", &ma, &mi, &rv)) so_kvf(o, "MMHUB IP", C_TEAL, "%d.%d.%d", ma, mi, rv);
    so_par(o);
    dpm_rows(o, &g, "Fabric (fclk)", "pp_dpm_fclk");
    dpm_rows(o, &g, "SoC (socclk)", "pp_dpm_socclk");
    dpm_rows(o, &g, "PCIe", "pp_dpm_pcie");
}

/* ======================================================================
 * [gpu-i2c]  (names only - never probed)
 * ====================================================================== */
static void s_gpu_i2c(srs_out *o)
{
    static const int tabs[] = { 120, 340 };
    gpu_t g;
    char **nm = NULL, p[512], b[256];
    int n, i;
    so_siv_header(o, "GPU I2C Devices");
    if (!gpu_sel(o, &g)) { so_err(o, "No DRM GPU found"); return; }
    so_tabs(o, tabs, 2);
    so_b(o, C_LABEL, "Bus"); so_tab(o); so_b(o, C_LABEL, "Name"); so_tab(o); so_b(o, C_LABEL, "Owner"); so_par(o);
    n = dir_list("/sys/bus/i2c/devices", &nm);
    for (i = 0; i < n; i++) {
        char real[PATH_MAX], own[256] = "";
        if (!starts(nm[i], "i2c-")) continue;
        snprintf(p, sizeof p, "/sys/bus/i2c/devices/%s", nm[i]);
        if (!realpath(p, real)) continue;
        rd_fmt(b, sizeof b, "%s/name", p);
        if (strstr(real, g.slot)) {
            /* which connector is this bus the DDC of? */
            gpu_t *gp = &g;
            conn_t c[16];
            int nc = connectors(gp, c, 16), k;
            snprintf(own, sizeof own, "%s", g.card);
            for (k = 0; k < nc; k++) {
                char d[512], dn[64];
                snprintf(d, sizeof d, DRM "/%s/ddc", c[k].name);
                base_name(d, dn, sizeof dn);
                if (!strcmp(dn, nm[i])) snprintf(own, sizeof own, "%s DDC %s (%s)", g.card, c[k].conn, c[k].status);
            }
            so_t(o, C_GREEN, nm[i]);
        } else {
            char *s = strstr(real, "/devices/");
            snprintf(own, sizeof own, "%s", s ? s + 9 : real);
            so_t(o, C_GRAY, nm[i]);
        }
        so_tab(o); so_t(o, C_VALUE, b); so_tab(o); so_t(o, C_GRAY, own); so_par(o);
    }
    dir_free(nm, n);
    so_par(o);
    so_note(o, "Bus names only from /sys/bus/i2c - DDC/I2C is never opened or probed (probing GPU DDC/I2C buses can hang some GPU firmware).");
}

/* ======================================================================
 * [gpu-apps]  (processes with the GPU open, from /proc/<pid>/fdinfo)
 * ====================================================================== */
typedef struct { int pid; long client; char comm[64]; long vram, gtt; double gfx, comp, dma, dec, enc; } gapp;
static void s_gpu_apps(srs_out *o)
{
    static const int tabs[] = { 60, 210, 290, 380, 470, 560, 650 };
    char **pids;
    int np = dir_list("/proc", &pids), i, n = 0, denied = 0, k;
    gapp *a = calloc(512, sizeof *a);
    so_siv_header(o, "GPU Applications");
    if (!a) { dir_free(pids, np); return; }
    for (i = 0; i < np && n < 512; i++) {
        char p[256], **fds;
        int nf, j;
        if (!isdigit((unsigned char)pids[i][0])) continue;
        snprintf(p, sizeof p, "/proc/%s/fd", pids[i]);
        nf = dir_list(p, &fds);
        if (nf < 0) { denied++; continue; }
        for (j = 0; j < nf && n < 512; j++) {
            char l[256], tgt[256], *t;
            ssize_t r;
            snprintf(l, sizeof l, "/proc/%s/fd/%s", pids[i], fds[j]);
            r = readlink(l, tgt, sizeof tgt - 1);
            if (r <= 0) continue;
            tgt[r] = 0;
            if (!starts(tgt, "/dev/dri/")) continue;
            snprintf(l, sizeof l, "/proc/%s/fdinfo/%s", pids[i], fds[j]);
            if (rd_all(l, &t) > 0 && strstr(t, "drm-client-id")) {
                char v[64];
                long cid;
                find_val(t, "drm-client-id", v, sizeof v);
                cid = atol(v);
                for (k = 0; k < n; k++) if (a[k].pid == atoi(pids[i]) && a[k].client == cid) break;
                if (k == n) {
                    gapp *x = &a[n++];
                    x->pid = atoi(pids[i]);
                    x->client = cid;
                    rd_fmt(x->comm, sizeof x->comm, "/proc/%s/comm", pids[i]);
                    if (find_val(t, "drm-memory-vram", v, sizeof v)) x->vram = atol(v);
                    if (find_val(t, "drm-memory-gtt", v, sizeof v)) x->gtt = atol(v);
                    if (find_val(t, "drm-engine-gfx", v, sizeof v)) x->gfx = atof(v) / 1e9;
                    if (find_val(t, "drm-engine-compute", v, sizeof v)) x->comp = atof(v) / 1e9;
                    if (find_val(t, "drm-engine-dma", v, sizeof v)) x->dma = atof(v) / 1e9;
                    if (find_val(t, "drm-engine-dec", v, sizeof v)) x->dec = atof(v) / 1e9;
                    if (find_val(t, "drm-engine-enc", v, sizeof v)) x->enc = atof(v) / 1e9;
                }
            }
            free(t);
        }
        dir_free(fds, nf);
    }
    dir_free(pids, np);
    so_tabs(o, tabs, 7);
    {
        static const char *const h[] = { "PID", "Process", "VRAM", "GTT", "GFX s", "Compute s", "DMA s", "Video s" };
        static const int hc[] = { C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL };
        so_row(o, 8, h, hc);
    }
    for (k = 0; k < n; k++) {
        char c[8][64];
        const char *cp[8];
        static const int cc[] = { C_PURPLE, C_BLACK, C_VALUE, C_VALUE, C_TEAL, C_TEAL, C_TEAL, C_TEAL };
        int q;
        snprintf(c[0], 64, "%d", a[k].pid);
        snprintf(c[1], 64, "%s", a[k].comm);
        fmt_bytes(a[k].vram * 1024.0, c[2], 64);
        fmt_bytes(a[k].gtt * 1024.0, c[3], 64);
        snprintf(c[4], 64, "%.1f", a[k].gfx);
        snprintf(c[5], 64, "%.1f", a[k].comp);
        snprintf(c[6], 64, "%.1f", a[k].dma);
        snprintf(c[7], 64, "%.1f", a[k].dec + a[k].enc);
        for (q = 0; q < 8; q++) cp[q] = c[q];
        so_row(o, 8, cp, cc);
    }
    so_par(o);
    so_f(o, C_GRAY, "%d GPU client%s visible; %d process%s not inspectable without root (e.g. kwin_wayland).", n, n == 1 ? "" : "s", denied, denied == 1 ? "" : "es");
    so_par(o);
    free(a);
}

/* ======================================================================
 * [gpu-api]
 * ====================================================================== */
static void s_gpu_api(srs_out *o)
{
    static const struct { const char *lib, *what; } L[] = {
        { "libdrm_amdgpu.so.1", "libdrm amdgpu (kernel UAPI wrapper)" },
        { "libdrm.so.2", "libdrm core" },
        { "libvulkan.so.1", "Vulkan loader" },
        { "libGL.so.1", "OpenGL (GLVND)" },
        { "libEGL.so.1", "EGL" },
        { "libgbm.so.1", "GBM" },
        { "libOpenCL.so.1", "OpenCL ICD loader" },
        { "libva.so.2", "VA-API video" },
        { "libvdpau.so.1", "VDPAU video" },
        { "libamdhip64.so", "ROCm HIP runtime" },
        { "libhsa-runtime64.so.1", "ROCm HSA runtime" },
        { "librocm_smi64.so", "ROCm SMI" },
        { "libamd_smi.so", "AMD SMI" },
        { "libcuda.so.1", "CUDA driver API (ZLUDA on AMD)" },
        { "libnvidia-ml.so.1", "NVIDIA NVML" },
        { "libatiadlxx.so", "AMD ADL (Windows-only API)" },
        { NULL, NULL } };
    static const int tabs[] = { 190, 420 };
    const char *a[] = { "ldconfig", "-p", NULL };
    const char *a2[] = { "/usr/bin/ldconfig", "-p", NULL };
    char *out = NULL;
    int i;
    so_siv_header(o, "GPU API Routines");
    if (run_out(&out, a) < 0 || !out || !*out) { free(out); run_out(&out, a2); }
    so_tabs(o, tabs, 2);
    so_b(o, C_LABEL, "Library"); so_tab(o); so_b(o, C_LABEL, "API"); so_tab(o); so_b(o, C_LABEL, "Path"); so_par(o);
    for (i = 0; L[i].lib; i++) {
        char needle[128], path[512] = "";
        const char *q;
        snprintf(needle, sizeof needle, "\t%s ", L[i].lib);
        if (out && (q = strstr(out, needle))) {
            const char *ar = strstr(q, "=> ");
            if (ar) { const char *e = strchr(ar, '\n'); snprintf(path, sizeof path, "%.*s", e ? (int)(e - ar - 3) : (int)strlen(ar + 3), ar + 3); }
        }
        if (!*path) {
            static const char *const dirs[] = { "/opt/rocm/lib", "/usr/lib", NULL };
            int d;
            for (d = 0; dirs[d] && !*path; d++) { char p[512]; snprintf(p, sizeof p, "%s/%s", dirs[d], L[i].lib); if (path_exists(p)) snprintf(path, sizeof path, "%s", p); }
        }
        so_check(o, *path != 0);
        so_t(o, *path ? C_BLACK : C_GRAY, L[i].lib); so_tab(o);
        so_t(o, C_VALUE, L[i].what); so_tab(o);
        so_t(o, *path ? C_PURPLE : C_GRAY, *path ? path : "not found");
        so_par(o);
    }
    free(out);
}

/* ======================================================================
 * Vulkan / GL helpers
 * ====================================================================== */
typedef struct { char name[160], type[64], api[32], drv[64], info[160], vid[16], did[16]; } vkdev;
static int vk_devices(vkdev *d, int max, char *inst, size_t isz)
{
    const char *t = tool(&T_VK, 15), *p;
    char l[512];
    int n = -1;
    if (inst) inst[0] = 0;
    if (!t) return -1;
    if (inst) find_val(t, "Vulkan Instance Version", inst, isz);
    p = strstr(t, "\nDevices:");
    if (!p) return 0;
    while ((p = next_line(p, l, sizeof l))) {
        char *s = l, *eq;
        while (*s == ' ' || *s == '\t') s++;
        if (!strncmp(s, "GPU", 3) && s[strlen(s) - 1] == ':') { if (n + 1 >= max) break; n++; memset(&d[n], 0, sizeof d[n]); continue; }
        if (n < 0 || !(eq = strchr(s, '='))) continue;
        {
            char k[64], *v = eq + 1;
            size_t kl = (size_t)(eq - s);
            while (kl && s[kl - 1] == ' ') kl--;
            snprintf(k, sizeof k, "%.*s", (int)kl, s);
            while (*v == ' ') v++;
            if (!strcmp(k, "deviceName")) snprintf(d[n].name, sizeof d[n].name, "%s", v);
            else if (!strcmp(k, "deviceType")) snprintf(d[n].type, sizeof d[n].type, "%s", starts(v, "PHYSICAL_DEVICE_TYPE_") ? v + 21 : v);
            else if (!strcmp(k, "apiVersion")) snprintf(d[n].api, sizeof d[n].api, "%s", v);
            else if (!strcmp(k, "driverName")) snprintf(d[n].drv, sizeof d[n].drv, "%s", v);
            else if (!strcmp(k, "driverInfo")) snprintf(d[n].info, sizeof d[n].info, "%s", v);
            else if (!strcmp(k, "vendorID")) snprintf(d[n].vid, sizeof d[n].vid, "%s", v);
            else if (!strcmp(k, "deviceID")) snprintf(d[n].did, sizeof d[n].did, "%s", v);
        }
    }
    return n + 1;
}

static void vk_rows(srs_out *o)
{
    vkdev d[8];
    char inst[64];
    int n = vk_devices(d, 8, inst, sizeof inst), i;
    if (n < 0) { so_kv(o, "Vulkan", C_GRAY, "vulkaninfo not installed"); return; }
    so_kv(o, "Vulkan instance", C_TEAL, inst);
    for (i = 0; i < n; i++) {
        char k[32];
        snprintf(k, sizeof k, "Vulkan device %d", i);
        so_kvf(o, k, C_BLACK, "%s", d[i].name);
        so_kvf(o, "", C_VALUE, "%s  API %s  driver %s  %s", d[i].type, d[i].api, d[i].drv, d[i].info);
    }
}

static void gl_rows(srs_out *o)
{
    const char *t = tool(&T_GLX, 10);
    char v[256];
    if (t && *t && strstr(t, "OpenGL")) {
        if (find_val(t, "OpenGL renderer string", v, sizeof v)) so_kv(o, "OpenGL renderer", C_BLACK, v);
        if (find_val(t, "OpenGL core profile version string", v, sizeof v)) so_kv(o, "OpenGL core", C_TEAL, v);
        if (find_val(t, "OpenGL version string", v, sizeof v)) so_kv(o, "OpenGL compat", C_VALUE, v);
        if (find_val(t, "OpenGL ES profile version string", v, sizeof v)) so_kv(o, "OpenGL ES", C_VALUE, v);
        if (find_val(t, "Video memory", v, sizeof v)) so_kv(o, "GL video memory", C_VALUE, v);
        if (find_val(t, "direct rendering", v, sizeof v)) so_kv(o, "Direct rendering", C_VALUE, v);
        return;
    }
    t = tool(&T_EGL, 10);
    if (t && *t) {
        if (find_val(t, "OpenGL core profile renderer", v, sizeof v)) so_kv(o, "OpenGL renderer", C_BLACK, v);
        if (find_val(t, "OpenGL core profile version", v, sizeof v)) so_kv(o, "OpenGL core", C_TEAL, v);
        if (find_val(t, "OpenGL ES profile version", v, sizeof v)) so_kv(o, "OpenGL ES", C_VALUE, v);
        if (find_val(t, "EGL version string", v, sizeof v)) so_kv(o, "EGL", C_VALUE, v);
        return;
    }
    so_kv(o, "OpenGL", C_GRAY, "glxinfo / eglinfo not installed (mesa-utils)");
}

static void translation_layers(srs_out *o)
{
    static const char *const P[][2] = {
        { "/usr/share/dxvk", "DXVK (D3D8/9/10/11 -> Vulkan)" },
        { "/usr/share/vkd3d-proton", "VKD3D-Proton (D3D12 -> Vulkan)" },
        { "/usr/lib/wine/x86_64-windows/d3d12.dll", "wine vkd3d (D3D12)" },
        { "/usr/lib/wine/x86_64-windows/wined3d.dll", "WineD3D (D3D -> OpenGL/Vulkan)" },
        { NULL, NULL } };
    const char *home = getenv("HOME");
    char p[512];
    int i;
    for (i = 0; P[i][0]; i++) { so_check(o, path_exists(P[i][0])); so_t(o, C_VALUE, P[i][1]); so_tab(o); so_t(o, C_GRAY, P[i][0]); so_par(o); }
    if (home) {
        static const char *const H[][2] = { { ".local/share/dxvk", "DXVK (user)" }, { ".local/share/vkd3d-proton", "VKD3D-Proton (user)" },
                                            { ".local/share/Steam/compatibilitytools.d", "Steam compat tools (Proton)" }, { NULL, NULL } };
        for (i = 0; H[i][0]; i++) { snprintf(p, sizeof p, "%s/%s", home, H[i][0]); so_check(o, path_exists(p)); so_t(o, C_VALUE, H[i][1]); so_tab(o); so_t(o, C_GRAY, p); so_par(o); }
    }
}

/* ======================================================================
 * [d3d-info] [directx] [gpu-d3d] [d3d-node] [dxd-gpu] [dxd-sum] [dxd-inf] [dxd-fil]
 * ====================================================================== */
static void s_d3d_info(srs_out *o)
{
    static const int tabs[] = { 150 };
    so_siv_header(o, "3D API Information (Direct3D equivalent: Vulkan / OpenGL)");
    so_tabs(o, tabs, 1);
    vk_rows(o);
    so_par(o);
    gl_rows(o);
}

static void s_directx(srs_out *o)
{
    static const int tabs[] = { 150, 420 };
    const char *t;
    char v[256];
    vkdev d[8];
    char inst[64];
    int n;
    so_siv_header(o, "DirectX Information (Linux graphics API versions)");
    so_tabs(o, tabs, 2);
    n = vk_devices(d, 8, inst, sizeof inst);
    so_kv(o, "Vulkan instance", C_TEAL, n < 0 ? "vulkaninfo not installed" : inst);
    if (n > 0) so_kvf(o, "Vulkan device API", C_TEAL, "%s  (%s)", d[0].api, d[0].name);
    t = tool(&T_GLX, 10);
    if (t && find_val(t, "OpenGL core profile version string", v, sizeof v)) so_kv(o, "OpenGL", C_VALUE, v);
    if (t && find_val(t, "OpenGL ES profile version string", v, sizeof v)) so_kv(o, "OpenGL ES", C_VALUE, v);
    t = tool(&T_EGL, 10);
    if (t && find_val(t, "EGL version string", v, sizeof v)) so_kv(o, "EGL", C_VALUE, v);
    t = tool(&T_CL, 20);
    if (t && find_val(t, "Platform Version", v, sizeof v)) so_kv(o, "OpenCL", C_VALUE, v);
    so_kv(o, "DirectX", C_ORANGE, "Windows API - on Linux provided by the translation layers below");
    so_par(o);
    translation_layers(o);
}

static void s_gpu_d3d(srs_out *o)
{
    static const int tabs[] = { 150 };
    gpu_t g[8];
    int n = gpus(g, 8), i;
    so_siv_header(o, "GPU 3D Status");
    so_tabs(o, tabs, 1);
    for (i = 0; i < n; i++) {
        char nm[160], a[32], b[32];
        long v, u;
        gpu_name(&g[i], nm, sizeof nm);
        so_b(o, C_BLACK, g[i].card); so_t(o, C_VALUE, "  "); so_t(o, C_BLACK, nm); so_par(o);
        if (gd_long(&g[i], "gpu_busy_percent", &v)) so_kvf(o, "3D/GFX busy", C_ORANGE, "%ld%%", v);
        if (gd_long(&g[i], "mem_busy_percent", &v)) so_kvf(o, "Memory busy", C_ORANGE, "%ld%%", v);
        if (gd_long(&g[i], "mem_info_vram_total", &v) && gd_long(&g[i], "mem_info_vram_used", &u)) {
            fmt_bytes((double)u, a, sizeof a); fmt_bytes((double)v, b, sizeof b);
            so_kvf(o, "Dedicated memory", C_VALUE, "%s of %s", a, b);
        }
        if (gd_long(&g[i], "mem_info_gtt_total", &v) && gd_long(&g[i], "mem_info_gtt_used", &u)) {
            fmt_bytes((double)u, a, sizeof a); fmt_bytes((double)v, b, sizeof b);
            so_kvf(o, "Shared memory (GTT)", C_VALUE, "%s of %s", a, b);
        }
        so_par(o);
    }
    vk_rows(o);
}

static void s_d3d_node(srs_out *o)
{
    static const int tabs[] = { 120, 220, 300 };
    gpu_t g;
    long v;
    int ma, mi, rv, i;
    static const char *const ips[][2] = { { "GC", "3D / Compute (graphics core)" }, { "SDMA0", "Copy (system DMA)" }, { "SDMA1", "Copy (system DMA 1)" },
                                          { "VCN", "Video decode/encode" }, { "UVD", "Video codec (VCN, discovered as UVD)" }, { "VCE", "Video encode" }, { "JPEG", "JPEG decode" },
                                          { "DMU", "Display (DCN)" }, { "MP0", "Security (PSP)" }, { "MP1", "Power (SMU)" }, { NULL, NULL } };
    so_siv_header(o, "GPU Engine Nodes");
    if (!gpu_sel(o, &g)) { so_err(o, "No DRM GPU found"); return; }
    so_tabs(o, tabs, 3);
    so_b(o, C_LABEL, "Engine"); so_tab(o); so_b(o, C_LABEL, "IP version"); so_tab(o); so_b(o, C_LABEL, "Instances"); so_tab(o); so_b(o, C_LABEL, "Function"); so_par(o);
    for (i = 0; ips[i][0]; i++) {
        char p[512], **ins;
        int k;
        if (!ip_ver(&g, ips[i][0], &ma, &mi, &rv)) continue;
        snprintf(p, sizeof p, "%s/ip_discovery/die/0/%s", g.dev, ips[i][0]);
        k = dir_list(p, &ins); dir_free(ins, k);
        so_t(o, C_PURPLE, ips[i][0]); so_tab(o); so_f(o, C_VALUE, "%d.%d.%d", ma, mi, rv); so_tab(o); so_f(o, C_VALUE, "%d", k > 0 ? k : 0); so_tab(o);
        so_t(o, C_VALUE, ips[i][1]); so_par(o);
    }
    so_par(o);
    if (gd_long(&g, "gpu_busy_percent", &v)) { so_t(o, C_LABEL, "GFX busy"); so_tab(o); so_f(o, C_GREEN, "%ld%%", v); so_par(o); }
    if (gd_long(&g, "mem_busy_percent", &v)) { so_t(o, C_LABEL, "Memory busy"); so_tab(o); so_f(o, C_GREEN, "%ld%%", v); so_par(o); }
    so_note(o, "Per-process engine time: see [gpu-apps].");
}

static void s_dxd_gpu(srs_out *o)
{
    static const int tabs[] = { 170 };
    gpu_t g[8];
    int n = gpus(g, 8), i, j;
    so_siv_header(o, "Display Devices (dxdiag equivalent)");
    so_tabs(o, tabs, 1);
    for (i = 0; i < n; i++) {
        char nm[160], f[5][160], b[128], t[32];
        conn_t c[16];
        int nc;
        long v;
        gpu_name(&g[i], nm, sizeof nm);
        lspci_fields(g[i].slot, f);
        so_kv(o, "Card name", C_BLACK, nm);
        so_kv(o, "Manufacturer", C_VALUE, f[1]);
        so_kv(o, "Chip type", C_VALUE, f[2]);
        so_kv(o, "DAC type", C_VALUE, "Integrated digital");
        so_kvf(o, "Device type", C_VALUE, "%s  (%s)", f[0], g[i].slot);
        so_kvf(o, "Device key", C_PURPLE, "PCI\\VEN_%04X&DEV_%04X&SUBSYS_%04X%04X&REV_%02X", g[i].vid, g[i].did, g[i].sdid, g[i].svid, g[i].rev);
        if (gd_long(&g[i], "mem_info_vram_total", &v)) { fmt_bytes((double)v, b, sizeof b); so_kv(o, "Dedicated memory", C_VALUE, b); }
        if (gd_long(&g[i], "mem_info_gtt_total", &v)) { fmt_bytes((double)v, b, sizeof b); so_kv(o, "Shared memory", C_VALUE, b); }
        nc = connectors(&g[i], c, 16);
        for (j = 0; j < nc; j++) {
            edid_t e;
            if (strcmp(c[j].status, "connected")) continue;
            conn_edid(c[j].name, &e);
            so_kvf(o, "Monitor", C_VALUE, "%s: %s %s", c[j].conn, e.ok ? e.mfg : "", e.ok ? e.name : "(no EDID)");
        }
        so_kv(o, "Driver", C_ORANGE, g[i].driver);
        gfx_target(&g[i], t, sizeof t);
        if (*t) so_kv(o, "Shader target", C_VALUE, t);
        so_par(o);
    }
    so_b(o, C_VALUE, "User-space drivers"); so_par(o);
    vk_rows(o);
    gl_rows(o);
}

static void mem_total(char *b, size_t sz)
{
    char *t, v[64];
    b[0] = 0;
    if (rd_all("/proc/meminfo", &t) > 0 && find_val(t, "MemTotal", v, sizeof v)) { fmt_bytes(atof(v) * 1024.0, b, sz); }
    free(t);
}

static void s_dxd_inf(srs_out *o)
{
    static const int tabs[] = { 170 };
    char b[256], host[128] = "";
    struct utsname u;
    char *t, v[64];
    time_t now = time(NULL);
    so_siv_header(o, "System Information (dxdiag equivalent)");
    so_tabs(o, tabs, 1);
    strftime(b, sizeof b, "%A, %d %B %Y, %H:%M:%S", localtime(&now));
    so_kv(o, "Current date/time", C_VALUE, b);
    gethostname(host, sizeof host);
    so_kv(o, "Computer name", C_BLACK, host);
    so_kv(o, "Operating system", C_VALUE, sys_os_name());
    if (!uname(&u)) so_kvf(o, "Kernel", C_VALUE, "%s %s %s", u.sysname, u.release, u.machine);
    rd_str("/sys/class/dmi/id/sys_vendor", b, sizeof b); so_kv(o, "System manufacturer", C_VALUE, b);
    rd_str("/sys/class/dmi/id/board_name", b, sizeof b); so_kv(o, "System model", C_VALUE, b);
    so_kv(o, "BIOS", C_VALUE, sys_board());
    so_kv(o, "Processor", C_VALUE, sys_cpu_name());
    mem_total(b, sizeof b); so_kv(o, "Memory", C_VALUE, b);
    if (rd_all("/proc/meminfo", &t) > 0 && find_val(t, "SwapTotal", v, sizeof v)) { fmt_bytes(atof(v) * 1024.0, b, sizeof b); so_kv(o, "Page file (swap)", C_VALUE, b); }
    free(t);
    {
        vkdev d[8];
        char inst[64];
        int n = vk_devices(d, 8, inst, sizeof inst);
        so_kvf(o, "DirectX version", C_ORANGE, "n/a on Linux; Vulkan %s", n < 0 ? "not installed" : inst);
    }
}

static void s_dxd_sum(srs_out *o)
{
    static const int tabs[] = { 170 };
    gpu_t g[8];
    int n = gpus(g, 8), i, nmon = 0, j;
    char b[256], *t;
    so_siv_header(o, "DirectX Diagnostic Summary (Linux equivalent)");
    so_tabs(o, tabs, 1);
    so_kv(o, "Operating system", C_VALUE, sys_os_name());
    so_kv(o, "Processor", C_VALUE, sys_cpu_name());
    mem_total(b, sizeof b); so_kv(o, "Memory", C_VALUE, b);
    for (i = 0; i < n; i++) {
        char nm[160];
        conn_t c[16];
        int nc = connectors(&g[i], c, 16);
        long v;
        gpu_name(&g[i], nm, sizeof nm);
        if (gd_long(&g[i], "mem_info_vram_total", &v)) fmt_bytes((double)v, b, sizeof b); else snprintf(b, sizeof b, "-");
        so_kvf(o, "Display adapter", C_BLACK, "%s  %s  (%s)", nm, b, g[i].driver);
        for (j = 0; j < nc; j++) nmon += !strcmp(c[j].status, "connected");
    }
    so_kvf(o, "Monitors", C_VALUE, "%d connected", nmon);
    vk_rows(o);
    gl_rows(o);
    if (rd_all("/proc/asound/cards", &t) > 0) {
        const char *p = t;
        char l[256];
        int k = 0;
        while ((p = next_line(p, l, sizeof l))) if (l[0] == ' ' && isdigit((unsigned char)l[1])) k++;
        so_kvf(o, "Sound cards", C_VALUE, "%d (ALSA)", k);
    }
    free(t);
    if (rd_all("/proc/bus/input/devices", &t) > 0) {
        const char *p = t;
        int k = 0;
        while ((p = strstr(p, "\nI:"))) { k++; p++; }
        so_kvf(o, "Input devices", C_VALUE, "%d", k + (starts(t, "I:") ? 1 : 0));
    }
    free(t);
}

static void s_dxd_fil(srs_out *o)
{
    static const int tabs[] = { 230 };
    char **nm = NULL;
    int n, i;
    so_siv_header(o, "Graphics Runtime Files");
    so_tabs(o, tabs, 1);
    so_b(o, C_VALUE, "Vulkan ICDs (/usr/share/vulkan/icd.d)"); so_par(o);
    n = dir_list("/usr/share/vulkan/icd.d", &nm);
    for (i = 0; i < n; i++) {
        char p[512], *t, v[256] = "", api[64] = "";
        snprintf(p, sizeof p, "/usr/share/vulkan/icd.d/%s", nm[i]);
        if (rd_all(p, &t) > 0) {
            char *q = strstr(t, "\"library_path\"");
            if (q && (q = strchr(q + 14, '"'))) { char *e = strchr(q + 1, '"'); if (e) snprintf(v, sizeof v, "%.*s", (int)(e - q - 1), q + 1); }
            q = strstr(t, "\"api_version\"");
            if (q && (q = strchr(q + 13, '"'))) { char *e = strchr(q + 1, '"'); if (e) snprintf(api, sizeof api, "%.*s", (int)(e - q - 1), q + 1); }
        }
        free(t);
        so_t(o, C_BLACK, nm[i]); so_tab(o); so_f(o, C_VALUE, "%s  API %s", v, api); so_par(o);
    }
    dir_free(nm, n);
    so_par(o);
    so_b(o, C_VALUE, "OpenCL ICDs (/etc/OpenCL/vendors)"); so_par(o);
    n = dir_list("/etc/OpenCL/vendors", &nm);
    for (i = 0; i < n; i++) { char b[256]; rd_fmt(b, sizeof b, "/etc/OpenCL/vendors/%s", nm[i]); so_t(o, C_BLACK, nm[i]); so_tab(o); so_t(o, C_VALUE, b); so_par(o); }
    dir_free(nm, n);
    so_par(o);
    if (have_cmd("pacman")) {
        const char *a[] = { "pacman", "-Q", "mesa", "lib32-mesa", "vulkan-radeon", "vulkan-icd-loader", "libdrm", "libglvnd", "ocl-icd", "opencl-mesa",
                            "rocm-opencl-runtime", "dxvk-bin", "dxvk-mingw-git", "vkd3d", "vkd3d-proton-bin", "linux-firmware-amdgpu", "linux-firmware", NULL };
        char *out = NULL;
        so_b(o, C_VALUE, "Packages (pacman -Q)"); so_par(o);
        run_to(&out, 10, a);
        if (out && *out) {
            const char *p = out;
            char l[256];
            char prev[512] = "";
            while ((p = next_line(p, l, sizeof l))) {
                char *sp = strchr(l, ' ');
                if (!sp || seen_add(prev, sizeof prev, l)) continue;   /* pacman -Q prints a provider once per name */
                *sp = 0; so_t(o, C_BLACK, l); so_tab(o); so_t(o, C_PURPLE, sp + 1); so_par(o);
            }
        }
        free(out);
    }
    so_par(o);
    so_b(o, C_VALUE, "Translation layers"); so_par(o);
    translation_layers(o);
}

/* ======================================================================
 * [opencl]
 * ====================================================================== */
static void s_opencl(srs_out *o)
{
    static const int tabs[] = { 200 };
    static const char *const keys[] = { "Device Type", "Board name", "Name", "Vendor", "Max compute units", "Max clock frequency", "Global memory size",
                                        "Max memory allocation", "Local memory size", "Version", "Device OpenCL C version", "Driver version", "Profile", NULL };
    const char *t, *p;
    char l[1024], pver[128] = "";
    int i;
    so_siv_header(o, "OpenCL Information");
    so_tabs(o, tabs, 1);
    t = tool(&T_CL, 20);
    if (!t) { so_kv(o, "clinfo", C_GRAY, "not installed"); return; }
    if (!*t) { so_kv(o, "clinfo", C_ORANGE, "no output (no OpenCL platform?)"); return; }
    p = t;
    while ((p = next_line(p, l, sizeof l))) {
        char *s = l, *c, k[128], v[512];
        size_t kl;
        int j;
        /* two layouts: ROCm's clinfo "  Name:\t\t\t value"; the cross-vendor clinfo (Oblomov, often /usr/bin/clinfo)
           "  Device Name        value" with no colon, its own key names mapped onto ROCm's below */
        static const char *const alias[][2] = { { "Device Name", "Name" }, { "Device Vendor", "Vendor" }, { "Device Version", "Version" },
            { "Device Board Name (AMD)", "Board name" }, { "Device OpenCL C Version", "Device OpenCL C version" }, { "Driver Version", "Driver version" },
            { "Device Profile", "Profile" }, { "Device Type", "Device Type" }, { NULL, NULL } };
        while (*s == ' ' || *s == '\t') s++;
        c = strchr(s, ':');
        if (c && (c[1] == '\t' || c[1] == ' ' || !c[1])) {
            kl = (size_t)(c - s);
            c++;
        } else {   /* no "key:": the key ends at the first run of two or more spaces */
            char *g = strstr(s, "  ");
            if (!g) continue;
            kl = (size_t)(g - s);
            c = g;
        }
        while (kl && (s[kl - 1] == ' ' || s[kl - 1] == '\t')) kl--;
        snprintf(k, sizeof k, "%.*s", (int)kl, s);
        while (*c == ' ' || *c == '\t') c++;
        snprintf(v, sizeof v, "%s", c);
        for (j = 0; alias[j][0]; j++) if (!strcmp(k, alias[j][0])) { snprintf(k, sizeof k, "%s", alias[j][1]); break; }
        if (!strcmp(k, "Number of platforms")) { so_kv(o, "Platforms", C_TEAL, v); continue; }
        if (!strcmp(k, "Platform Version")) { snprintf(pver, sizeof pver, "%s", v); continue; }
        if (!strcmp(k, "Platform Name")) {
            char nx[256];

            next_line(p, nx, sizeof nx);
            if (strstr(nx, "Number of devices")) { so_par(o); so_b(o, C_BLACK, v); so_par(o); }
            else {
                if (!*pver) {   /* the cross-vendor clinfo lists the version after the name: look a few lines on */
                    const char *q = p;
                    char ln[512], *g;
                    for (j = 0; j < 6 && (q = next_line(q, ln, sizeof ln)); j++)
                        if ((g = strstr(ln, "Platform Version"))) {
                            g += 16;
                            while (*g == ' ' || *g == '\t' || *g == ':') g++;
                            snprintf(pver, sizeof pver, "%s", g);
                            break;
                        }
                }
                if (*pver) so_kvf(o, "Platform", C_PURPLE, "%s  (%s)", v, pver);
                else so_kv(o, "Platform", C_PURPLE, v);
                pver[0] = 0;
            }
            continue;
        }
        if (!strcmp(k, "Platform Vendor")) continue;
        if (!strcmp(k, "Number of devices")) { so_kv(o, "Devices", C_TEAL, v); continue; }
        for (i = 0; keys[i]; i++) if (!strcmp(k, keys[i])) { so_kv(o, k, !strcmp(k, "Name") || !strcmp(k, "Board name") ? C_BLACK : C_VALUE, v); break; }
    }
    so_par(o);
    so_note(o, "Source: clinfo (installable client driver query, read only). ICDs: /etc/OpenCL/vendors.");
}

/* ======================================================================
 * [gpu-g-sync]
 * ====================================================================== */
static void s_gpu_gsync(srs_out *o)
{
    static const int tabs[] = { 110, 330, 440, 560 };
    gpu_t g[8];
    kms_conn kc[16];
    kms_crtc kr[16];
    int ng = gpus(g, 8), i, j, nkc = 0, nkr = 0;
    so_siv_header(o, "GPU G-Sync / Adaptive Sync (VRR)");
    if (!kms_parse(kc, 16, &nkc, kr, 16, &nkr)) so_note(o, "drm_info not installed or failed - VRR properties unavailable; showing EDID ranges only");
    so_tabs(o, tabs, 4);
    so_b(o, C_LABEL, "Connector"); so_tab(o); so_b(o, C_LABEL, "Monitor"); so_tab(o); so_b(o, C_LABEL, "VRR capable"); so_tab(o);
    so_b(o, C_LABEL, "VRR enabled"); so_tab(o); so_b(o, C_LABEL, "EDID range"); so_par(o);
    for (i = 0; i < ng; i++) {
        conn_t c[16];
        int nc = connectors(&g[i], c, 16);
        for (j = 0; j < nc; j++) {
            edid_t e;
            kms_conn *k = kms_find(kc, nkc, c[j].id);
            int con = !strcmp(c[j].status, "connected");
            if (!con) continue;
            conn_edid(c[j].name, &e);
            so_t(o, C_BLACK, c[j].conn); so_tab(o);
            so_t(o, C_VALUE, e.ok ? e.name : "-"); so_tab(o);
            so_t(o, k ? (k->vrr_cap ? C_GREEN : C_GRAY) : C_GRAY, k ? (k->vrr_cap ? "Yes" : "No") : "?"); so_tab(o);
            so_t(o, k ? (k->vrr_en ? C_GREEN : C_GRAY) : C_GRAY, k ? (k->vrr_en ? "Yes" : "No (compositor off/idle)") : "?"); so_tab(o);
            if (e.rng) so_f(o, C_TEAL, "%d - %d Hz", e.vmin, e.vmax); else so_t(o, C_GRAY, "-");
            so_par(o);
        }
    }
    so_par(o);
    so_note(o, "vrr_capable is the kernel connector property; VRR_ENABLED is set per CRTC by the compositor (KWin: Adaptive sync).");
}

/* ======================================================================
 * [gpu-dump] (needs root for the full config space)
 * ====================================================================== */
static void s_gpu_dump(srs_out *o)
{
    gpu_t g;
    char p[512], *b;
    int n, i;
    const char *rf;
    so_siv_header(o, "GPU Register / Config Dump");
    if (!gpu_sel(o, &g)) { so_err(o, "No DRM GPU found"); return; }
    snprintf(p, sizeof p, "%s/config", g.dev);
    n = rd_all(p, &b);
    so_f(o, C_VALUE, "PCI config space of %s  (%d bytes readable unprivileged)", g.slot, n < 0 ? 0 : n); so_par(o); so_par(o);
    for (i = 0; i < n && i < 4096; i += 16) {
        char l[128];
        int k, w = snprintf(l, sizeof l, "%03X: ", i);
        for (k = 0; k < 16 && i + k < n; k++) w += snprintf(l + w, sizeof l - (size_t)w, "%02X ", (unsigned char)b[i + k]);
        so_m(o, C_VALUE, l); so_par(o);
    }
    free(b);
    so_par(o);
    rf = so_root_file("pci-all.txt");
    b = NULL;
    if (rf && rd_all(rf, &b) > 0) {
        char key[40];
        char *s, *e;
        snprintf(key, sizeof key, "\n%s ", g.slot);       /* lspci -D: "0000:03:00.0 " */
        s = !strncmp(b, key + 1, strlen(key) - 1) ? b : strstr(b, key);
        if (!s) {
            snprintf(key, sizeof key, "\n%s ", g.slot + 5); /* plain lspci: "03:00.0 " */
            s = !strncmp(b, key + 1, strlen(key) - 1) ? b : strstr(b, key);
        }
        if (s && *s == '\n') s++;
        if (s) {
            e = strstr(s, "\n\n");
            if (e) *e = 0;
            so_b(o, C_VALUE, "Full config space (srs-root cache: lspci -vvvnnxxx)"); so_par(o);
            so_lines_max(o, C_VALUE, s, 400);
        } else so_note(o, "GPU not found in root cache pci-all.txt");
        free(b);
    } else {
        free(b);
        so_t(o, C_ORANGE, "Needs root: Menu > Run as root (srs-root)"); so_par(o);
        so_note(o, "Extended config space (256-4095) and amdgpu debugfs registers need root; MMIO registers are never read.");
    }
}

/* ======================================================================
 * [gpu-export]
 * ====================================================================== */
static void s_gpu_export(srs_out *o)
{
    static const int tabs[] = { 160, 300 };
    gpu_t g;
    char **nm = NULL;
    int n, i;
    so_siv_header(o, "GPU Export (sensor sources)");
    if (!gpu_sel(o, &g)) { so_err(o, "No DRM GPU found"); return; }
    so_tabs(o, tabs, 2);
    so_b(o, C_LABEL, "Sensor"); so_tab(o); so_b(o, C_LABEL, "Value"); so_tab(o); so_b(o, C_LABEL, "Source"); so_par(o);
    n = *g.hwmon ? dir_list(g.hwmon, &nm) : -1;
    for (i = 0; i < n; i++) {
        char p[512], lb[64] = "", pre[32], v[64];
        long x;
        const char *u = strstr(nm[i], "_input") ? strstr(nm[i], "_input") : strstr(nm[i], "_average");
        if (!u) continue;
        snprintf(pre, sizeof pre, "%.*s", (int)(u - nm[i]), nm[i]);
        snprintf(p, sizeof p, "%s/%s", g.hwmon, nm[i]);
        if (!rd_long(p, &x)) continue;
        rd_fmt(lb, sizeof lb, "%s/%s_label", g.hwmon, pre);
        if (starts(pre, "temp")) snprintf(v, sizeof v, "%.1f °C", x / 1000.0);
        else if (starts(pre, "fan")) snprintf(v, sizeof v, "%ld RPM", x);
        else if (starts(pre, "power")) snprintf(v, sizeof v, "%.1f W", x / 1e6);
        else if (starts(pre, "in")) snprintf(v, sizeof v, "%ld mV", x);
        else if (starts(pre, "freq")) snprintf(v, sizeof v, "%.0f MHz", x / 1e6);
        else snprintf(v, sizeof v, "%ld", x);
        so_f(o, C_BLACK, "%s%s%s", pre, *lb ? " " : "", lb); so_tab(o); so_t(o, C_GREEN, v); so_tab(o); so_m(o, C_GRAY, p); so_par(o);
    }
    dir_free(nm, n);
    {
        long v;
        char p[512];
        if (gd_long(&g, "gpu_busy_percent", &v)) { snprintf(p, sizeof p, "%s/gpu_busy_percent", g.dev); so_t(o, C_BLACK, "GPU load"); so_tab(o); so_f(o, C_GREEN, "%ld%%", v); so_tab(o); so_m(o, C_GRAY, p); so_par(o); }
        if (gd_long(&g, "mem_info_vram_used", &v)) { char b[32]; snprintf(p, sizeof p, "%s/mem_info_vram_used", g.dev); fmt_bytes((double)v, b, sizeof b); so_t(o, C_BLACK, "VRAM used"); so_tab(o); so_t(o, C_GREEN, b); so_tab(o); so_m(o, C_GRAY, p); so_par(o); }
    }
    so_par(o);
    so_note(o, "These read-only sources can be exported with srs -save or polled by any monitor (e.g. MangoHud, CoolerControl).");
}

/* ======================================================================
 * absent / windows-only with Linux equivalents
 * ====================================================================== */
static void s_gpu_cuda(srs_out *o)
{
    static const int tabs[] = { 200 };
    gpu_t g[8];
    int n = gpus(g, 8), i, nv = 0;
    so_siv_header(o, "GPU CUDA Information");
    so_tabs(o, tabs, 1);
    for (i = 0; i < n; i++) nv += g[i].vid == 0x10de;
    so_kvf(o, "NVIDIA GPUs", nv ? C_GREEN : C_GRAY, "%d", nv);
    so_kv(o, "/dev/nvidia0", C_VALUE, path_exists("/dev/nvidia0") ? "present" : "absent");
    so_kv(o, "nvidia-smi", C_VALUE, have_cmd("nvidia-smi") ? "installed" : "not installed");
    so_kv(o, "libcuda.so.1", C_VALUE, path_exists("/usr/lib/libcuda.so.1") ? "/usr/lib/libcuda.so.1 (ZLUDA or NVIDIA)" : "not found in /usr/lib");
    so_par(o);
    so_b(o, C_VALUE, "Nearest on AMD: ROCm / HIP"); so_par(o);
    so_kv(o, "ROCm", C_VALUE, path_exists("/opt/rocm") ? "/opt/rocm" : "not installed");
    so_kv(o, "HIP runtime", C_VALUE, path_exists("/opt/rocm/lib/libamdhip64.so") ? "/opt/rocm/lib/libamdhip64.so" : "not found");
    so_kv(o, "/dev/kfd", C_VALUE, path_exists("/dev/kfd") ? "present (compute enabled)" : "absent");
    for (i = 0; i < n; i++) { char t[32]; gfx_target(&g[i], t, sizeof t); if (*t) so_kv(o, "HIP target", C_TEAL, t); }
}

static void s_gpu_sli(srs_out *o)
{
    static const int tabs[] = { 200 };
    gpu_t g[8];
    int n = gpus(g, 8), i;
    so_siv_header(o, "GPU SLI Topologies");
    so_tabs(o, tabs, 1);
    so_kvf(o, "GPUs", C_VALUE, "%d", n);
    for (i = 0; i < n; i++) { char nm[160]; gpu_name(&g[i], nm, sizeof nm); so_kvf(o, g[i].card, C_BLACK, "%s  (%s)", nm, g[i].slot); }
    so_kv(o, "SLI / CrossFire", C_GRAY, n > 1 ? "multiple GPUs present; Linux has no SLI/CrossFire link (PRIME / Vulkan device groups instead)" : "not applicable - single GPU");
}

static void s_gpu_leds(srs_out *o)
{
    static const int tabs[] = { 200 };
    gpu_t g;
    memset(&g, 0, sizeof g);
    so_siv_header(o, "GPU LEDs");
    so_tabs(o, tabs, 1);
    if (gpu_sel(o, &g)) so_kvf(o, "Board", C_VALUE, "%04X:%04X (subsystem %04X:%04X)", g.vid, g.did, g.svid, g.sdid);
    {   /* Linux exposes GPU RGB only through LED class devices (or OpenRGB, which needs I2C access) */
        char **nm, p[400], real[600];
        int n = dir_list("/sys/class/leds", &nm), i, found = 0;
        for (i = 0; i < n; i++) {
            ssize_t l;
            snprintf(p, sizeof p, "/sys/class/leds/%s/device", nm[i]);
            l = readlink(p, real, sizeof real - 1);
            if (l <= 0) continue;
            real[l] = 0;
            if (*g.slot && strstr(real, g.slot)) { so_kv(o, "LED device", C_GREEN, nm[i]); found++; }
        }
        dir_free(nm, n);
        if (!found) so_kv(o, "RGB controller", C_GRAY, "none exposed by the kernel for this GPU");
        so_kv(o, "Checked", C_PURPLE, "/sys/class/leds devices belonging to the GPU (GPU I2C buses are never probed)");
        so_note(o, "Many GPU RGB controllers sit on the GPU's I2C bus and are only reachable with tools such as OpenRGB.");
    }
}

static void s_rsr_info(srs_out *o)
{
    static const int tabs[] = { 200 };
    so_siv_header(o, "RSR Information (Radeon Super Resolution)");
    so_tabs(o, tabs, 1);
    so_kv(o, "RSR", C_ORANGE, "Windows-only (Adrenalin driver feature)");
    so_kv(o, "Linux equivalent", C_PURPLE, "gamescope -F fsr, WINE_FULLSCREEN_FSR (Proton-GE/wine-tkg), vkBasalt CAS");
    so_kv(o, "gamescope", C_VALUE, have_cmd("gamescope") ? "installed" : "not installed");
    so_kv(o, "vkBasalt", C_VALUE, have_cmd("vkbasalt") || path_exists("/usr/share/vulkan/implicit_layer.d/vkBasalt.json") ? "installed" : "not installed");
    so_kv(o, "WINE_FULLSCREEN_FSR", C_VALUE, getenv("WINE_FULLSCREEN_FSR") ? getenv("WINE_FULLSCREEN_FSR") : "(not set)");
}

static void s_rtss_sts(srs_out *o)
{
    static const int tabs[] = { 200 };
    char p[512];
    const char *home = getenv("HOME");
    so_siv_header(o, "RTSS Status");
    so_tabs(o, tabs, 1);
    so_kv(o, "RTSS", C_GRAY, "RivaTuner Statistics Server is Windows-only - not present");
    so_kv(o, "Nearest", C_PURPLE, "MangoHud (overlay + fps_limit)");
    so_kv(o, "mangohud", C_VALUE, have_cmd("mangohud") ? "installed" : "not installed");
    so_kv(o, "MANGOHUD env", C_VALUE, getenv("MANGOHUD") ? getenv("MANGOHUD") : "(not set)");
    if (home) {
        char *t, v[128];
        snprintf(p, sizeof p, "%s/.config/MangoHud/MangoHud.conf", home);
        if (rd_all(p, &t) > 0) {
            so_kv(o, "MangoHud.conf", C_VALUE, p);
            if (find_val(t, "fps_limit", v, sizeof v)) so_kv(o, "fps_limit", C_TEAL, v);
        }
        free(t);
    }
}

static void s_goverlay(srs_out *o)
{
    static const int tabs[] = { 200 };
    const char *home = getenv("HOME");
    char p[512], *t;
    so_siv_header(o, "GOverlay / MangoHud Status");
    so_tabs(o, tabs, 1);
    so_kv(o, "goverlay", C_VALUE, have_cmd("goverlay") ? "installed" : "not installed");
    so_kv(o, "mangohud", C_VALUE, have_cmd("mangohud") ? "installed" : "not installed");
    if (have_cmd("pacman")) {
        const char *a[] = { "pacman", "-Q", "goverlay", "goverlay-git", "mangohud", "mangohud-git", "lib32-mangohud", "vkbasalt", NULL };
        char *out = NULL;
        run_to(&out, 10, a);
        if (out && *out) {
            const char *q = out;
            char l[256];
            char prev[512] = "";
            while ((q = next_line(q, l, sizeof l))) {
                char *sp = strchr(l, ' ');
                if (!sp || seen_add(prev, sizeof prev, l)) continue;
                *sp = 0; so_kv(o, l, C_PURPLE, sp + 1);
            }
        }
        free(out);
    }
    so_kv(o, "MANGOHUD env", C_VALUE, getenv("MANGOHUD") ? getenv("MANGOHUD") : "(not set - not injected globally)");
    if (!home) return;
    snprintf(p, sizeof p, "%s/.config/goverlay/goverlay.conf", home);
    if (rd_all(p, &t) > 0) {
        char v[128];
        so_kv(o, "goverlay.conf", C_VALUE, p);
        if (find_val(t, "Version", v, sizeof v)) so_kv(o, "GOverlay version", C_TEAL, v);
    }
    free(t);
    snprintf(p, sizeof p, "%s/.config/MangoHud/MangoHud.conf", home);
    if (rd_all(p, &t) > 0) {
        const char *q = t;
        char l[256];
        int k = 0;
        so_par(o);
        so_b(o, C_VALUE, "MangoHud.conf (active lines)"); so_f(o, C_GRAY, "  %s", p); so_par(o);
        while ((q = next_line(q, l, sizeof l)) && k < 80) {
            char *e, *s = l;
            while (*s == ' ') s++;
            if (!*s || *s == '#') continue;
            e = strchr(s, '=');
            if (e) {
                *e = 0;
                if (strcasestr(s, "token") || strcasestr(s, "key") || strcasestr(s, "secret") || strcasestr(s, "pass") || strcasestr(s, "auth") || strcasestr(s, "cookie"))
                    so_kv(o, s, C_GRAY, "********");
                else so_kv(o, s, C_VALUE, e + 1);
            } else so_kv(o, s, C_GREEN, "on");
            k++;
        }
    }
    free(t);
}

/* ======================================================================
 * audio: [audio] [sound] [wave] [uaa-dev] [dxd-aud] [dxd-mic]
 * ====================================================================== */
static void asound_cards(srs_out *o)
{
    char *t;
    const char *p;
    char l[512], l2[512];
    if (rd_all("/proc/asound/cards", &t) <= 0) { free(t); so_note(o, "/proc/asound/cards not available (no ALSA)"); return; }
    p = t;
    while ((p = next_line(p, l, sizeof l))) {
        int n;
        char id[64], drv[64], name[256];
        if (sscanf(l, " %d [%63[^]]]: %63s - %255[^\n]", &n, id, drv, name) == 4) {
            char *s = id + strlen(id);
            while (s > id && s[-1] == ' ') *--s = 0;
            so_f(o, C_PURPLE, "card%d", n); so_tab(o); so_t(o, C_BLACK, name); so_tab(o); so_f(o, C_VALUE, "%s  [%s]", drv, id); so_par(o);
            if ((p = next_line(p, l2, sizeof l2))) { char *q = l2; while (*q == ' ') q++; so_tab(o); so_t(o, C_GRAY, q); so_par(o); }
        }
    }
    free(t);
}

static void s_audio(srs_out *o)
{
    static const int tabs[] = { 60, 320 };
    char *out = NULL;
    so_siv_header(o, "Audio Devices");
    so_tabs(o, tabs, 2);
    asound_cards(o);
    so_par(o);
    if (have_cmd("wpctl")) {
        const char *a[] = { "wpctl", "status", NULL };
        if (run_to(&out, 5, a) >= 0 && *out) {
            const char *p = strstr(out, "\nAudio");
            char l[512];
            int k = 0;
            so_b(o, C_VALUE, "PipeWire (wpctl status)"); so_par(o);
            while (p && (p = next_line(p, l, sizeof l)) && k < 80) {
                if (!strncmp(l, "Video", 5) || !strncmp(l, "Settings", 8)) break;
                if (*l) { so_m(o, C_VALUE, l); so_par(o); k++; }
            }
        }
        free(out);
    } else if (have_cmd("pactl")) {
        const char *a[] = { "pactl", "info", NULL };
        if (run_to(&out, 5, a) >= 0 && *out) { so_b(o, C_VALUE, "pactl info"); so_par(o); so_lines_max(o, C_VALUE, out, 40); }
        free(out);
    }
}

static void s_sound(srs_out *o)
{
    static const int tabs[] = { 60, 320 };
    char *t;
    so_siv_header(o, "Sound Devices");
    so_tabs(o, tabs, 2);
    asound_cards(o);
    so_par(o);
    if (rd_all("/proc/asound/devices", &t) > 0) { so_b(o, C_VALUE, "ALSA device nodes (/proc/asound/devices)"); so_par(o); so_lines(o, C_VALUE, t); }
    free(t);
    if (rd_all("/proc/asound/version", &t) > 0) { so_par(o); so_note(o, t); }
    free(t);
}

static void pcm_list(srs_out *o, int capture)
{
    char *t;
    const char *p;
    char l[512];
    if (rd_all("/proc/asound/pcm", &t) <= 0) { free(t); so_note(o, "no PCM devices"); return; }
    p = t;
    while ((p = next_line(p, l, sizeof l))) {
        int c, d;
        char id[128] = "", nm[128] = "";
        char st[128] = "closed", path[128];
        if (sscanf(l, "%d-%d: %127[^:]: %127[^:]", &c, &d, id, nm) < 2) continue;
        if (!strstr(l, capture ? "capture" : "playback")) continue;
        snprintf(path, sizeof path, "/proc/asound/card%d/pcm%d%c/sub0/hw_params", c, d, capture ? 'c' : 'p');
        if (rd_str(path, st, sizeof st)) {
            if (strcmp(st, "closed")) {
                char *all;
                if (rd_all(path, &all) > 0) {
                    char f[64] = "", r[64] = "", ch[64] = "";
                    find_val(all, "format", f, sizeof f); find_val(all, "rate", r, sizeof r); find_val(all, "channels", ch, sizeof ch);
                    snprintf(st, sizeof st, "open: %s %sch %s", f, ch, r);
                }
                free(all);
            }
        }
        so_f(o, C_PURPLE, "hw:%d,%d", c, d); so_tab(o);
        so_t(o, C_BLACK, nm); so_tab(o);
        so_t(o, strcmp(st, "closed") ? C_GREEN : C_GRAY, st); so_par(o);
    }
    free(t);
}

static void s_wave(srs_out *o)
{
    static const int tabs[] = { 70, 290 };
    so_siv_header(o, "Wave Devices");
    so_tabs(o, tabs, 2);
    so_b(o, C_VALUE, "Wave out (playback)"); so_par(o);
    pcm_list(o, 0);
    so_par(o);
    so_b(o, C_VALUE, "Wave in (capture)"); so_par(o);
    pcm_list(o, 1);
}

static void s_dxd_aud(srs_out *o)
{
    static const int tabs[] = { 70, 290 };
    char *out = NULL;
    so_siv_header(o, "Sound Devices (dxdiag equivalent)");
    so_tabs(o, tabs, 2);
    so_b(o, C_VALUE, "Playback"); so_par(o);
    pcm_list(o, 0);
    so_par(o);
    so_b(o, C_VALUE, "Capture"); so_par(o);
    pcm_list(o, 1);
    so_par(o);
    if (have_cmd("pactl")) {
        const char *a[] = { "pactl", "list", "short", "sinks", NULL };
        const char *b[] = { "pactl", "get-default-sink", NULL };
        if (run_to(&out, 5, a) >= 0 && *out) { so_b(o, C_VALUE, "Sound server sinks (pactl list short sinks)"); so_par(o); so_lines_max(o, C_VALUE, out, 40); }
        free(out); out = NULL;
        if (run_to(&out, 5, b) >= 0 && *out) { so_kv(o, "Default sink", C_GREEN, strtok(out, "\n")); }
        free(out);
    }
}

static void s_uaa_dev(srs_out *o)
{
    static const int tabs[] = { 180 };
    char **cards;
    int nc = dir_list("/proc/asound", &cards), i, any = 0;
    so_siv_header(o, "UAA (HD Audio) Devices");
    so_tabs(o, tabs, 1);
    for (i = 0; i < nc; i++) {
        char p[256], **f;
        int nf, j;
        if (!starts(cards[i], "card") || !isdigit((unsigned char)cards[i][4])) continue;
        snprintf(p, sizeof p, "/proc/asound/%s", cards[i]);
        nf = dir_list(p, &f);
        for (j = 0; j < nf; j++) {
            char q[512], *t, v[256];
            static const char *const keys[] = { "Codec", "Address", "AFG Function Id", "Vendor Id", "Subsystem Id", "Revision Id", NULL };
            int k;
            if (!starts(f[j], "codec#")) continue;
            snprintf(q, sizeof q, "%s/%s", p, f[j]);
            if (rd_all(q, &t) > 0) {
                any++;
                so_b(o, C_BLACK, cards[i]); so_t(o, C_VALUE, " "); so_b(o, C_BLACK, f[j]); so_par(o);
                for (k = 0; keys[k]; k++) if (find_val(t, keys[k], v, sizeof v)) so_kv(o, keys[k], k == 0 ? C_BLACK : C_PURPLE, v);
            }
            free(t);
        }
        for (j = 0; j < nf; j++) {
            char q[512], *t, mn[128] = "", ct[64] = "", pr[16] = "";
            if (!starts(f[j], "eld#")) continue;
            snprintf(q, sizeof q, "%s/%s", p, f[j]);
            if (rd_all(q, &t) > 0) {
                const char *s0 = strstr(t, "monitor_present");
                if (s0) { s0 += 15; while (*s0 == '\t' || *s0 == ' ') s0++; snprintf(pr, sizeof pr, "%.*s", (int)strcspn(s0, "\n"), s0); }
                if (atoi(pr) == 1) {
                    /* ELD lines are "key<TAB>value" */
                    const char *s = strstr(t, "monitor_name");
                    if (s) { s += 12; while (*s == '\t' || *s == ' ') s++; snprintf(mn, sizeof mn, "%.*s", (int)strcspn(s, "\n"), s); }
                    s = strstr(t, "connection_type");
                    if (s) { s += 15; while (*s == '\t' || *s == ' ') s++; snprintf(ct, sizeof ct, "%.*s", (int)strcspn(s, "\n"), s); }
                    so_kvf(o, f[j], C_GREEN, "%s  (%s, ELD)", mn, ct);
                }
            }
            free(t);
        }
        dir_free(f, nf);
        so_par(o);
    }
    dir_free(cards, nc);
    {
        char **s;
        int ns = dir_list("/sys/class/sound", &s), k;
        for (k = 0; k < ns; k++) {
            char v[128], c[128], id[64];
            if (!starts(s[k], "hwC")) continue;
            rd_fmt(v, sizeof v, "/sys/class/sound/%s/vendor_name", s[k]);
            rd_fmt(c, sizeof c, "/sys/class/sound/%s/chip_name", s[k]);
            rd_fmt(id, sizeof id, "/sys/class/sound/%s/vendor_id", s[k]);
            so_kvf(o, s[k], C_TEAL, "%s %s  (%s)", v, c, id);
            any++;
        }
        dir_free(s, ns);
    }
    if (!any) so_note(o, "No HD Audio codecs found");
}

static void s_dxd_mic(srs_out *o)
{
    char *t, *out = NULL;
    so_siv_header(o, "MIDI Ports (DirectMusic equivalent)");
    if (rd_all("/proc/asound/seq/clients", &t) > 0) {
        const char *p = t;
        char l[256];
        so_b(o, C_VALUE, "ALSA sequencer clients (/proc/asound/seq/clients)"); so_par(o);
        while ((p = next_line(p, l, sizeof l))) {
            const char *s = l;
            while (*s == ' ') s++;
            if (!*s || strstr(s, "pool") || starts(s, "Pool") || starts(s, "Cells") || starts(s, "Peak") || starts(s, "Alloc") || starts(s, "cur ") || starts(s, "max ")) continue;
            so_m(o, starts(s, "Client ") ? C_BLACK : starts(s, "Port ") ? C_TEAL : C_VALUE, l); so_par(o);
        }
    }
    else if (have_cmd("aconnect")) {
        const char *a[] = { "aconnect", "-l", NULL };
        if (run_to(&out, 5, a) >= 0 && *out) { so_b(o, C_VALUE, "aconnect -l"); so_par(o); so_lines_max(o, C_VALUE, out, 200); }
        free(out);
    } else so_note(o, "ALSA sequencer not available");
    free(t);
}

/* ======================================================================
 * input: [joysticks] [dxd-inp] [dxd-usb] [dxd-gam] [dxd-ps2]
 * ====================================================================== */
typedef struct { char name[128], phys[128], handlers[128], bus[64]; } indev;
static int input_devs(indev *d, int max)
{
    char *t;
    const char *p;
    char l[512];
    int n = -1;
    if (rd_all("/proc/bus/input/devices", &t) <= 0) { free(t); return 0; }
    p = t;
    while ((p = next_line(p, l, sizeof l))) {
        if (starts(l, "I:")) { if (n + 1 >= max) break; n++; memset(&d[n], 0, sizeof d[n]); snprintf(d[n].bus, sizeof d[n].bus, "%s", l + 3); }
        else if (n >= 0 && starts(l, "N: Name=")) { char *s = l + 8; size_t k; if (*s == '"') s++; k = strlen(s); if (k && s[k - 1] == '"') s[k - 1] = 0; snprintf(d[n].name, sizeof d[n].name, "%s", s); }
        else if (n >= 0 && starts(l, "P: Phys=")) snprintf(d[n].phys, sizeof d[n].phys, "%s", l + 8);
        else if (n >= 0 && starts(l, "H: Handlers=")) snprintf(d[n].handlers, sizeof d[n].handlers, "%s", l + 12);
    }
    free(t);
    return n + 1;
}

static void s_joysticks(srs_out *o)
{
    static const int tabs[] = { 70, 330, 450 };
    char **nm = NULL;
    int n = dir_list("/sys/class/input", &nm), i, k = 0;
    indev d[96];
    int nd;
    so_siv_header(o, "Joysticks");
    so_tabs(o, tabs, 3);
    so_b(o, C_LABEL, "Device"); so_tab(o); so_b(o, C_LABEL, "Name"); so_tab(o); so_b(o, C_LABEL, "VID:PID"); so_tab(o); so_b(o, C_LABEL, "Bus"); so_par(o);
    for (i = 0; i < n; i++) {
        char name[128] = "", v[16] = "", p[16] = "", b[16] = "";
        if (!starts(nm[i], "js")) continue;
        rd_fmt(name, sizeof name, "/sys/class/input/%s/device/name", nm[i]);
        rd_fmt(v, sizeof v, "/sys/class/input/%s/device/id/vendor", nm[i]);
        rd_fmt(p, sizeof p, "/sys/class/input/%s/device/id/product", nm[i]);
        rd_fmt(b, sizeof b, "/sys/class/input/%s/device/id/bustype", nm[i]);
        so_f(o, C_PURPLE, "/dev/input/%s", nm[i]); so_tab(o); so_t(o, C_BLACK, name); so_tab(o); so_f(o, C_VALUE, "%s:%s", v, p); so_tab(o);
        so_t(o, C_VALUE, !strcmp(b, "0003") ? "USB" : !strcmp(b, "0005") ? "Bluetooth" : b); so_par(o);
        k++;
    }
    dir_free(nm, n);
    if (!k) so_note(o, "No joystick (/dev/input/js*) devices");
    so_par(o);
    nd = input_devs(d, 96);
    so_b(o, C_VALUE, "Game controllers in /proc/bus/input/devices"); so_par(o);
    for (i = 0; i < nd; i++)
        if (strstr(d[i].handlers, "js")) { so_t(o, C_PURPLE, ""); so_tab(o); so_t(o, C_BLACK, d[i].name); so_tab(o); so_t(o, C_VALUE, d[i].handlers); so_par(o); }
    so_note(o, "Event devices are listed only - never opened.");
}

static void s_dxd_inp(srs_out *o)
{
    static const int tabs[] = { 290, 510 };
    indev d[96];
    int n = input_devs(d, 96), i;
    so_siv_header(o, "Input Devices (DirectInput equivalent)");
    so_tabs(o, tabs, 2);
    so_b(o, C_LABEL, "Name"); so_tab(o); so_b(o, C_LABEL, "Bus / Vendor / Product"); so_tab(o); so_b(o, C_LABEL, "Handlers"); so_par(o);
    for (i = 0; i < n; i++) { so_t(o, C_BLACK, d[i].name); so_tab(o); so_t(o, C_VALUE, d[i].bus); so_tab(o); so_t(o, C_TEAL, d[i].handlers); so_par(o); }
    so_par(o);
    so_f(o, C_GRAY, "%d input devices from /proc/bus/input/devices", n); so_par(o);
}

static void s_dxd_usb(srs_out *o)
{
    char *out = NULL;
    const char *a[] = { "lsusb", "-t", NULL };
    so_siv_header(o, "USB Device Tree (DirectInput USB root)");
    if (have_cmd("lsusb") && run_to(&out, 10, a) >= 0 && *out) so_lines_max(o, C_VALUE, out, 300);
    else so_note(o, "lsusb not installed (usbutils)");
    free(out);
    {
        indev d[96];
        int n = input_devs(d, 96), i;
        static const int tabs[] = { 290 };
        so_par(o);
        so_tabs(o, tabs, 1);
        so_b(o, C_VALUE, "USB HID input devices"); so_par(o);
        for (i = 0; i < n; i++) if (strstr(d[i].phys, "usb")) { so_t(o, C_BLACK, d[i].name); so_tab(o); so_t(o, C_GRAY, d[i].phys); so_par(o); }
    }
}

static void s_dxd_gam(srs_out *o)
{
    static const int tabs[] = { 200 };
    char **nm = NULL;
    int n = dir_list("/sys/bus/gameport/devices", &nm), i;
    so_siv_header(o, "DirectInput Gameports");
    so_tabs(o, tabs, 1);
    if (n < 0) so_kv(o, "Gameport bus", C_GRAY, "not present (/sys/bus/gameport absent - no gameport driver loaded)");
    else if (!n) so_kv(o, "Gameport devices", C_GRAY, "none");
    for (i = 0; i < n; i++) so_kv(o, nm[i], C_VALUE, "gameport");
    dir_free(nm, n);
}

static void s_dxd_ps2(srs_out *o)
{
    static const int tabs[] = { 120 };
    char **nm = NULL;
    int n = dir_list("/sys/bus/serio/devices", &nm), i;
    so_siv_header(o, "DirectInput PS/2 Devices");
    so_tabs(o, tabs, 1);
    if (n <= 0) so_kv(o, "PS/2 (serio)", C_GRAY, "no PS/2 ports or devices (/sys/bus/serio/devices empty)");
    for (i = 0; i < n; i++) {
        char d[128] = "", dr[128] = "", p[512];
        rd_fmt(d, sizeof d, "/sys/bus/serio/devices/%s/description", nm[i]);
        snprintf(p, sizeof p, "/sys/bus/serio/devices/%s/driver", nm[i]);
        base_name(p, dr, sizeof dr);
        so_kvf(o, nm[i], C_VALUE, "%s  driver %s", d, *dr ? dr : "-");
    }
    dir_free(nm, n);
}

/* ======================================================================
 * [dxd-dev] [dxd-dsk] [dxd-ftr] [dxd-ply]
 * ====================================================================== */
static void s_dxd_dev(srs_out *o)
{
    char *out = NULL;
    const char *a[] = { "lspci", "-nn", NULL };
    so_siv_header(o, "System Devices (dxdiag equivalent)");
    if (run_to(&out, 10, a) >= 0 && *out) so_lines_max(o, C_VALUE, out, 400);
    else so_note(o, "lspci not installed (pciutils)");
    free(out);
}

static void s_dxd_dsk(srs_out *o)
{
    char *out = NULL;
    const char *a[] = { "lsblk", "-e7", "-o", "NAME,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINT", NULL };
    so_siv_header(o, "Logical Disks (dxdiag equivalent)");
    if (run_to(&out, 10, a) >= 0 && *out) so_lines_max(o, C_VALUE, out, 300);
    else so_note(o, "lsblk not available");
    free(out);
}

static void s_dxd_ftr(srs_out *o)
{
    static const char *const dirs[] = { "/usr/lib/gstreamer-1.0", "/usr/lib32/gstreamer-1.0", NULL };
    int d;
    so_siv_header(o, "Media Filters (DirectShow equivalent: GStreamer plugins)");
    for (d = 0; dirs[d]; d++) {
        char **nm = NULL;
        int n = dir_list(dirs[d], &nm), i, k = 0;
        if (n < 0) continue;
        so_b(o, C_VALUE, dirs[d]); so_par(o);
        for (i = 0; i < n; i++) {
            char b[128];
            size_t l = strlen(nm[i]);
            if (l < 4 || strcmp(nm[i] + l - 3, ".so")) continue;
            {
                const char *bn = starts(nm[i], "libgst") ? nm[i] + 6 : nm[i];
                snprintf(b, sizeof b, "%.*s", (int)(strlen(bn) - 3), bn);
            }
            so_t(o, C_BLACK, b); so_par(o);
            k++;
        }
        so_f(o, C_GRAY, "%d plugins", k); so_par(o); so_par(o);
        dir_free(nm, n);
    }
    so_note(o, "Plugin files only; gst-inspect-1.0 is not run (slow, rebuilds its registry).");
}

static void s_dxd_ply(srs_out *o)
{
    static const int tabs[] = { 200 };
    char **nm = NULL;
    int n = dir_list("/sys/class/net", &nm), i;
    so_siv_header(o, "DirectPlay Adapters");
    so_tabs(o, tabs, 1);
    so_kv(o, "DirectPlay", C_ORANGE, "Windows-only (deprecated DirectX networking)");
    so_kv(o, "Nearest", C_PURPLE, "network interfaces");
    for (i = 0; i < n; i++) {
        char st[32] = "", mac[32] = "";
        rd_fmt(st, sizeof st, "/sys/class/net/%s/operstate", nm[i]);
        rd_fmt(mac, sizeof mac, "/sys/class/net/%s/address", nm[i]);
        so_kvf(o, nm[i], C_VALUE, "%s  %s", st, mac);
    }
    dir_free(nm, n);
}

/* ======================================================================
 * table
 * ====================================================================== */
const srs_item sec_gpu[] = {
    { "graphics", "windows", "Graphics", B_LINUX, 0, "/sys/class/drm; drm_info; EDID", s_graphics, 0, "gpu" },
    { "monitors", "windows", "Monitors", B_LINUX, 0, "/sys/class/drm/card*-*/edid (decoded); pnp.ids; drm_info", s_monitors, 0, "gpu" },
    { "mon-info", "hardware", "Monitor ID database", B_LINUX, 0, "/usr/share/hwdata/pnp.ids", s_mon_info, 0, "gpu" },
    { "monitor-tree", "hardware", "Monitor tree", B_LINUX, 0, "/sys/class/drm", s_monitor_tree, 0, "gpu" },
    { "gpu-info", "hardware", "GPU information", B_LINUX, 0, "/sys/class/drm/card*/device; amdgpu.ids; lspci", s_gpu_info, 0, "gpu" },
    { "gpu-all", "hardware", "GPU all details", B_LINUX, 0, "/sys/class/drm/card*/device; hwmon; lspci -vv", s_gpu_all, 0, "gpu" },
    { "gpu-clocks", "hardware", "GPU clock speeds", B_LINUX, 0, "pp_dpm_sclk/mclk/fclk/socclk/dcefclk/pcie; hwmon freq*", s_gpu_clocks, 1000, "gpu" },
    { "gpu-temp", "hardware", "GPU temperatures", B_LINUX, 0, "card hwmon temp*_input/crit/emergency", s_gpu_temp, 1000, "gpu" },
    { "gpu-fans", "hardware", "GPU fans", B_LINUX, 0, "card hwmon fan1_*/pwm1 (read only)", s_gpu_fans, 1000, "gpu" },
    { "gpu-power", "hardware", "GPU power status", B_LINUX, 0, "card hwmon power1_*/in*; gpu_metrics header", s_gpu_power, 1000, "gpu" },
    { "gpu-bios", "hardware", "GPU VBIOS status", B_LINUX, 0, "vbios_version; fw_version/*; vbios-<pci>.bin cache (debugfs amdgpu_vbios, srs-root)", s_gpu_bios, 0, "gpu" },
    { "gpu-map", "hardware", "GPU mapping", B_LINUX, 0, "/sys/bus/pci/devices; resource; iommu_group", s_gpu_map, 0, "gpu" },
    { "gpu-tree", "hardware", "GPU tree", B_LINUX, 0, "/sys/class/drm", s_gpu_tree, 0, "gpu" },
    { "gpu-gens", "hardware", "GPU generations", B_LINUX, 0, "/sys/class/drm/card*/device/ip_discovery", s_gpu_gens, 0, "gpu" },
    { "gpu-reg", "hardware", "GPU registry", B_LINUX, 0, "device/uevent; driver; /sys/module/amdgpu/parameters", s_gpu_reg, 0, "gpu" },
    { "gpu-scf", "hardware", "GPU SCF", B_LINUX, 0, "ip_discovery DF/MP1; pp_dpm_fclk/socclk/pcie", s_gpu_scf, 0, "gpu" },
    { "gpu-i2c", "hardware", "GPU I2C buses", B_LINUX, 0, "/sys/bus/i2c/devices/i2c-*/name (names only)", s_gpu_i2c, 0, "gpu" },
    { "gpu-apps", "hardware", "GPU applications", B_LINUX, 0, "/proc/*/fdinfo (drm-client stats; other users' processes need root)", s_gpu_apps, 2000, "gpu" },
    { "gpu-api", "hardware", "GPU API routines", B_LINUX, 0, "ldconfig -p", s_gpu_api, 0, "gpu" },
    { "gpu-d3d", "hardware", "GPU D3D status", B_LINUX, 0, "gpu_busy_percent; mem_info_*; vulkaninfo --summary", s_gpu_d3d, 0, "gpu" },
    { "d3d-node", "hardware", "GPU D3D engine nodes", B_LINUX, 0, "ip_discovery; gpu_busy_percent", s_d3d_node, 1000, "gpu" },
    { "d3d-info", "hardware", "Direct3D info", B_LINUX, 0, "vulkaninfo --summary; glxinfo -B", s_d3d_info, 0, "gpu" },
    { "directx", "hardware", "DirectX information", B_LINUX, 0, "vulkaninfo; glxinfo; eglinfo; clinfo; DXVK/VKD3D paths", s_directx, 0, "gpu" },
    { "gpu-g-sync", "hardware", "GPU G-Sync / VRR", B_LINUX, 0, "drm_info vrr_capable/VRR_ENABLED; EDID range limits", s_gpu_gsync, 0, "gpu" },
    { "gpu-dump", "hardware", "GPU register dump", B_LINUX, 1, "device/config (64 bytes); root cache pci-all.txt", s_gpu_dump, 0, "gpu" },
    { "gpu-export", "hardware", "GPU export", B_LINUX, 0, "card hwmon; gpu_busy_percent", s_gpu_export, 0, "gpu" },
    { "gpu-cuda", "hardware", "CUDA information", B_ABSENT, 0, "n/a (no NVIDIA GPU; nvidia-smi absent); nearest ROCm/HIP", s_gpu_cuda, 0, "gpu" },
    { "gpu-sli", "hardware", "GPU SLI topology", B_ABSENT, 0, "n/a (single GPU, no SLI/CrossFire)", s_gpu_sli, 0, "gpu" },
    { "gpu-leds", "hardware", "GPU LEDs", B_ABSENT, 0, "/sys/class/leds (LED devices of the GPU)", s_gpu_leds, 0, "gpu" },
    { "opencl", "hardware", "OpenCL", B_LINUX, 0, "clinfo", s_opencl, 0, "gpu" },
    { "rsr-info", "hardware", "RSR info", B_WINONLY, 0, "none; nearest gamescope FSR", s_rsr_info, 0, "gpu" },
    { "rtss-sts", "windows", "RTSS status", B_ABSENT, 0, "n/a (RivaTuner); nearest mangohud", s_rtss_sts, 0, "gpu" },
    { "goverlay", "hardware", "GOverlay status", B_LINUX, 0, "goverlay; mangohud; ~/.config/MangoHud", s_goverlay, 0, "gpu" },
    { "dxd-gpu", "hardware", "DirectX GPU displays", B_LINUX, 0, "/sys/class/drm; vulkaninfo --summary", s_dxd_gpu, 0, "gpu" },
    { "dxd-sum", "hardware", "DirectX summary", B_LINUX, 0, "vulkaninfo --summary; glxinfo -B", s_dxd_sum, 0, "gpu" },
    { "dxd-inf", "hardware", "DirectX system information", B_LINUX, 0, "uname; DMI; /proc/meminfo", s_dxd_inf, 0, "gpu" },
    { "dxd-fil", "hardware", "DirectX files", B_LINUX, 0, "/usr/share/vulkan/icd.d; /etc/OpenCL/vendors; pacman -Q", s_dxd_fil, 0, "gpu" },
    { "dxd-dev", "hardware", "DirectX system devices", B_LINUX, 0, "lspci -nn", s_dxd_dev, 0, "gpu" },
    { "dxd-dsk", "hardware", "DirectX logical disks", B_LINUX, 0, "lsblk", s_dxd_dsk, 0, "gpu" },
    { "dxd-ftr", "hardware", "DirectShow filters", B_LINUX, 0, "/usr/lib/gstreamer-1.0", s_dxd_ftr, 0, "gpu" },
    { "dxd-aud", "hardware", "DirectX sound devices", B_LINUX, 0, "/proc/asound/pcm; pactl list sinks", s_dxd_aud, 0, "gpu" },
    { "dxd-mic", "hardware", "DirectMusic ports", B_LINUX, 0, "/proc/asound/seq/clients; aconnect -l", s_dxd_mic, 0, "gpu" },
    { "dxd-inp", "hardware", "DirectInput devices", B_LINUX, 0, "/proc/bus/input/devices", s_dxd_inp, 0, "gpu" },
    { "dxd-usb", "hardware", "DirectInput USB root", B_LINUX, 0, "lsusb -t; /proc/bus/input/devices", s_dxd_usb, 0, "gpu" },
    { "dxd-gam", "hardware", "DirectInput gameports", B_ABSENT, 0, "/sys/bus/gameport (absent)", s_dxd_gam, 0, "gpu" },
    { "dxd-ps2", "hardware", "DirectInput PS/2 devices", B_ABSENT, 0, "/sys/bus/serio/devices (empty)", s_dxd_ps2, 0, "gpu" },
    { "dxd-ply", "hardware", "DirectPlay adapters", B_WINONLY, 0, "none; nearest ip link", s_dxd_ply, 0, "gpu" },
    { "audio", "hardware", "Audio devices", B_LINUX, 0, "/proc/asound/cards; wpctl status", s_audio, 0, "gpu" },
    { "sound", "hardware", "Sound devices", B_LINUX, 0, "/proc/asound/cards; /proc/asound/devices", s_sound, 0, "gpu" },
    { "wave", "hardware", "Wave devices", B_LINUX, 0, "/proc/asound/pcm; card*/pcm*/sub0/hw_params", s_wave, 0, "gpu" },
    { "uaa-dev", "hardware", "UAA (HD Audio) devices", B_LINUX, 0, "/proc/asound/card*/codec#*; eld#*; /sys/class/sound/hwC*", s_uaa_dev, 0, "gpu" },
    { "joysticks", "hardware", "Joysticks", B_LINUX, 0, "/sys/class/input/js*; /proc/bus/input/devices", s_joysticks, 0, "gpu" },
    { NULL, NULL, NULL, B_LINUX, 0, NULL, NULL, 0, NULL }
};
