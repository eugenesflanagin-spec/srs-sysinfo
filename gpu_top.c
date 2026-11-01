/* gpu_top.c - GPU Top data layer: device list, sysfs sensors, gpu_metrics, GRBM sampling, per-process
 * fdinfo, and the text / JSON output shared by the CLI and the GUI.
 *
 * A C reimplementation of amdgpu_top (MIT licence, Copyright (c) 2023 Umio Yasuno,
 * https://github.com/Umio-Yasuno/amdgpu_top), crates/libamdgpu_top: same data, same rules
 * (sampling, idle handling, fdinfo arithmetic), same names. The gpu_metrics layouts come from the
 * kernel's kgd_pp_interface.h (MIT) via gpu_top_metrics.inc. NVIDIA: gpu_top_nvml.c; AMD ioctls:
 * gpu_top_amd.c.
 */
#include "gpu_top_int.h"
#include "lists.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ paths and files */

const char *gt_root(void)
{
    const char *r = getenv("SRS_TEST_GPU_ROOT");
    return r && r[0] == '/' ? r : "";
}

int gt_testing(void) { return gt_root()[0] != 0; }

char *gt_p(char *out, size_t n, const char *fmt, ...)
{
    va_list ap;
    size_t l;
    snprintf(out, n, "%s", gt_root());
    l = strlen(out);
    va_start(ap, fmt);
    vsnprintf(out + l, n > l ? n - l : 0, fmt, ap);
    va_end(ap);
    return out;
}

int gt_read(const char *path, char *buf, size_t n)
{
    int fd;
    ssize_t r;
    if (n == 0) return -1;
    buf[0] = 0;
    if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) return -1;
    r = read(fd, buf, n - 1);
    close(fd);
    if (r < 0) { buf[0] = 0; return -1; }
    buf[r] = 0;
    while (r > 0 && (buf[r - 1] == '\n' || buf[r - 1] == ' ')) buf[--r] = 0;
    return 0;
}

int gt_read_ll(const char *path, long long *v)
{
    char b[64], *e;
    if (gt_read(path, b, sizeof b) || !b[0]) return -1;
    errno = 0;
    *v = strtoll(b, &e, 0);
    return errno || e == b ? -1 : 0;
}

long gt_read_bin(const char *path, unsigned char *buf, size_t n)
{
    int fd;
    long tot = 0;
    ssize_t r;
    if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) return -1;
    while ((size_t)tot < n && (r = read(fd, buf + tot, n - tot)) > 0) tot += r;
    close(fd);
    return tot;
}

int gt_readf(char *buf, size_t n, const char *dir, const char *name)
{
    char p[GT_PATH];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    return gt_read(p, buf, n);
}

int gt_readf_ll(long long *v, const char *dir, const char *name)
{
    char p[GT_PATH];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    return gt_read_ll(p, v);
}

void gt_cat(char *buf, size_t len, const char *fmt, ...)
{
    size_t l = strlen(buf);
    va_list ap;
    if (l + 1 >= len) return;
    va_start(ap, fmt);
    vsnprintf(buf + l, len - l, fmt, ap);
    va_end(ap);
}

gt_section *gt_sect_new(gt_info *in, const char *title)
{
    gt_section *s;
    if (in->nsect >= GT_MAX_SECT) return &in->sect[GT_MAX_SECT - 1];
    s = &in->sect[in->nsect++];
    memset(s, 0, sizeof *s);
    snprintf(s->title, sizeof s->title, "%s", title);
    return s;
}

void gt_kv_add(gt_section *s, const char *label, const char *fmt, ...)
{
    va_list ap;
    if (s->n >= GT_MAX_KV) return;
    snprintf(s->kv[s->n].label, sizeof s->kv[0].label, "%s", label);
    va_start(ap, fmt);
    vsnprintf(s->kv[s->n].value, sizeof s->kv[0].value, fmt, ap);
    va_end(ap);
    s->n++;
}

long long gt_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void sleep_ms(int ms)
{
    struct timespec ts;
    if (ms <= 0) return;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) && errno == EINTR) {}
}

const char *gt_vendor_name(gt_vendor v)
{
    return v == GT_VENDOR_AMD ? "AMD" : v == GT_VENDOR_NVIDIA ? "NVIDIA" : v == GT_VENDOR_INTEL ? "Intel" : "Other";
}

const char *gt_pcie_gen_speed(int gen)
{
    static const char *s[] = { "?", "2.5 GT/s", "5.0 GT/s", "8.0 GT/s", "16.0 GT/s", "32.0 GT/s", "64.0 GT/s" };
    return gen >= 1 && gen <= 6 ? s[gen] : s[0];
}

/* ------------------------------------------------------------------ JSON writer */

void gj_init(gt_json *j, char *buf, size_t n)
{
    memset(j, 0, sizeof *j);
    j->b = buf;
    j->n = n;
    j->len = strlen(buf);
    j->first[0] = 1;
}

static void gj_put(gt_json *j, const char *s)
{
    size_t l = strlen(s);
    if (j->len + l + 1 >= j->n) { j->len = j->n ? j->n - 1 : 0; return; }
    memcpy(j->b + j->len, s, l + 1);
    j->len += l;
}

/* the length of a valid UTF-8 sequence at s (1..4), 0 = not valid UTF-8 (a stray byte in a process name, ...) */
static int utf8_len(const unsigned char *s)
{
    int n, i;
    unsigned cp;
    if (s[0] < 0x80) return 1;
    if (s[0] >= 0xC2 && s[0] <= 0xDF) { n = 2; cp = s[0] & 0x1F; }
    else if (s[0] >= 0xE0 && s[0] <= 0xEF) { n = 3; cp = s[0] & 0x0F; }
    else if (s[0] >= 0xF0 && s[0] <= 0xF4) { n = 4; cp = s[0] & 0x07; }
    else return 0;
    for (i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) return 0;
        cp = cp << 6 | (s[i] & 0x3F);
    }
    if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
    return n;
}

/* a JSON string body: quotes, backslashes and control bytes escaped, bytes that are not UTF-8 as U+FFFD */
size_t gt_json_escape(char *out, size_t on, const char *s)
{
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)s;
    if (!on) return 0;
    while (*p && o + 7 < on) {
        int n = utf8_len(p);
        if (*p == '"' || *p == '\\') { out[o++] = '\\'; out[o++] = (char)*p++; }
        else if (*p < 0x20) { o += (size_t)snprintf(out + o, on - o, "\\u%04x", *p); p++; }
        else if (!n) { o += (size_t)snprintf(out + o, on - o, "\\ufffd"); p++; }
        else { while (n--) out[o++] = (char)*p++; }
    }
    out[o] = 0;
    return o;
}

static void gj_esc(gt_json *j, const char *s)
{
    char t[1024];
    gj_put(j, "\"");
    while (*s) {   /* in pieces, so a long string fits the small buffer */
        char part[160];
        size_t k = strlen(s) > 150 ? 150 : strlen(s);
        while (k < strlen(s) && k > 0 && ((unsigned char)s[k] & 0xC0) == 0x80) k--;   /* not inside a UTF-8 sequence */
        if (!k) k = strlen(s) > 150 ? 150 : strlen(s);
        memcpy(part, s, k);
        part[k] = 0;
        gt_json_escape(t, sizeof t, part);
        gj_put(j, t);
        s += k;
    }
    gj_put(j, "\"");
}

static void gj_key(gt_json *j, const char *key)
{
    if (!j->first[j->depth]) gj_put(j, ",");
    j->first[j->depth] = 0;
    if (key) { gj_esc(j, key); gj_put(j, ":"); }
}

void gj_obj(gt_json *j, const char *key)
{
    gj_key(j, key);
    gj_put(j, "{");
    if (j->depth < 31) j->first[++j->depth] = 1;
}

void gj_arr(gt_json *j, const char *key)
{
    gj_key(j, key);
    gj_put(j, "[");
    if (j->depth < 31) j->first[++j->depth] = 1;
}

void gj_end(gt_json *j, char close)
{
    char t[2] = { close, 0 };
    gj_put(j, t);
    if (j->depth > 0) j->depth--;
}

void gj_str(gt_json *j, const char *key, const char *v)
{
    gj_key(j, key);
    if (v) gj_esc(j, v); else gj_put(j, "null");
}

void gj_int(gt_json *j, const char *key, long long v)
{
    char t[32];
    gj_key(j, key);
    if (v == GT_NA) { gj_put(j, "null"); return; }
    snprintf(t, sizeof t, "%lld", v);
    gj_put(j, t);
}

void gj_u64(gt_json *j, const char *key, unsigned long long v)
{
    char t[32];
    gj_key(j, key);
    snprintf(t, sizeof t, "%llu", v);
    gj_put(j, t);
}

void gj_num(gt_json *j, const char *key, double v)
{
    char t[40];
    gj_key(j, key);
    if (v < 0) { gj_put(j, "null"); return; }
    if (v == (double)(long long)v) snprintf(t, sizeof t, "%lld", (long long)v);
    else snprintf(t, sizeof t, "%.3f", v);
    gj_put(j, t);
}

void gj_bool(gt_json *j, const char *key, int v) { gj_key(j, key); gj_put(j, v ? "true" : "false"); }
void gj_null(gt_json *j, const char *key) { gj_key(j, key); gj_put(j, "null"); }
void gj_raw(gt_json *j, const char *key, const char *raw) { gj_key(j, key); gj_put(j, raw); }

void gj_vu(gt_json *j, const char *key, long long v, const char *unit)
{
    if (v == GT_NA) { gj_null(j, key); return; }
    gj_obj(j, key);
    gj_int(j, "value", v);
    gj_str(j, "unit", unit);
    gj_end(j, '}');
}

/* ------------------------------------------------------------------ device names */

/* amdgpu.ids (libdrm's list): "DID,\tRID,\tName" */
static int amdgpu_ids_name(unsigned did, unsigned rid, char *out, size_t n)
{
    char p[GT_PATH], line[256];
    FILE *f;
    int found = -1;
    if (!(f = fopen(gt_p(p, sizeof p, "/usr/share/libdrm/amdgpu.ids"), "r"))) return -1;
    while (fgets(line, sizeof line, f)) {
        unsigned d, r;
        char *name;
        if (line[0] == '#' || sscanf(line, "%x,%x,", &d, &r) != 2) continue;
        if (d != did || r != rid) continue;
        if (!(name = strchr(line, ','))) continue;
        if (!(name = strchr(name + 1, ','))) continue;
        name++;
        while (*name == ' ' || *name == '\t') name++;
        name[strcspn(name, "\r\n")] = 0;
        snprintf(out, n, "%s", name);
        found = 0;
        break;
    }
    fclose(f);
    return found;
}

/* pci.ids: the device's name ("Navi 48 [Radeon RX 9070/9070 XT/9070 GRE]") */
static int pci_ids_name(unsigned ven, unsigned dev, char *out, size_t n)
{
    char p[GT_PATH], line[512];
    const char *path;
    FILE *f;
    int in_ven = 0, found = -1;
    if (gt_testing()) path = gt_p(p, sizeof p, "/usr/share/hwdata/pci.ids");
    else if (!(path = srs_ids_path("pci.ids", p, sizeof p, NULL))) return -1;
    if (!(f = fopen(path, "r"))) return -1;
    while (fgets(line, sizeof line, f)) {
        unsigned v;
        if (line[0] == '#' || line[0] == '\n') continue;
        if (line[0] != '\t') {
            if (in_ven) break;
            if (sscanf(line, "%x", &v) == 1 && v == ven) in_ven = 1;
            continue;
        }
        if (in_ven && line[1] != '\t' && sscanf(line + 1, "%x", &v) == 1 && v == dev) {
            char *nm = line + 1;
            while (*nm && !isspace((unsigned char)*nm)) nm++;
            while (*nm == ' ' || *nm == '\t') nm++;
            nm[strcspn(nm, "\r\n")] = 0;
            snprintf(out, n, "%s", nm);
            found = 0;
            break;
        }
    }
    fclose(f);
    return found;
}

/* ------------------------------------------------------------------ device list */

static int cmp_dev(const void *a, const void *b)
{
    return strcmp(((const gt_device *)a)->pci, ((const gt_device *)b)->pci);
}

int gt_device_list(gt_device *list, int max)
{
    char base[GT_PATH], p[GT_PATH], b[128];
    DIR *dir;
    struct dirent *e;
    int n = 0;
    if (!(dir = opendir(gt_p(base, sizeof base, "/sys/bus/pci/devices")))) return 0;
    while ((e = readdir(dir)) && n < max) {
        gt_device *g;
        long long v;
        DIR *dd;
        struct dirent *de;
        char dev[GT_PATH];
        if (e->d_name[0] == '.') continue;
        snprintf(dev, sizeof dev, "%s/%s", base, e->d_name);
        if (gt_readf_ll(&v, dev, "class") || ((v >> 16) & 0xff) != 0x03) continue;
        g = &list[n];
        memset(g, 0, sizeof *g);
        g->vendor_id = gt_readf_ll(&v, dev, "vendor") ? 0 : (unsigned)v;
        g->device_id = gt_readf_ll(&v, dev, "device") ? 0 : (unsigned)v;
        g->revision_id = gt_readf_ll(&v, dev, "revision") ? 0 : (unsigned)v;
        g->subsys_vendor = gt_readf_ll(&v, dev, "subsystem_vendor") ? 0 : (unsigned)v;
        g->subsys_device = gt_readf_ll(&v, dev, "subsystem_device") ? 0 : (unsigned)v;
        g->vendor = g->vendor_id == 0x1002 ? GT_VENDOR_AMD : g->vendor_id == 0x10de ? GT_VENDOR_NVIDIA :
                    g->vendor_id == 0x8086 ? GT_VENDOR_INTEL : GT_VENDOR_OTHER;
        snprintf(g->pci, sizeof g->pci, "%s", e->d_name);
        snprintf(g->sysfs, sizeof g->sysfs, "/sys/bus/pci/devices/%s", e->d_name);
        snprintf(p, sizeof p, "%s/drm", dev);
        if ((dd = opendir(p))) {
            while ((de = readdir(dd))) {
                if (!strncmp(de->d_name, "card", 4) && isdigit((unsigned char)de->d_name[4]) && !strchr(de->d_name, '-'))
                    snprintf(g->card, sizeof g->card, "/dev/dri/%.20s", de->d_name);
                else if (!strncmp(de->d_name, "renderD", 7))
                    snprintf(g->render, sizeof g->render, "/dev/dri/%.20s", de->d_name);
            }
            closedir(dd);
        }
        /* driver: the link's last part (fixtures store no link: then "amdgpu" for AMD with a render node) */
        {
            char l[GT_PATH];
            ssize_t r;
            snprintf(p, sizeof p, "%s/driver", dev);
            if ((r = readlink(p, l, sizeof l - 1)) > 0) {
                l[r] = 0;
                snprintf(g->driver, sizeof g->driver, "%s", strrchr(l, '/') ? strrchr(l, '/') + 1 : l);
            } else if (gt_readf(b, sizeof b, dev, "srs-driver") == 0) {
                snprintf(g->driver, sizeof g->driver, "%s", b);
            } else if (g->vendor == GT_VENDOR_AMD && g->render[0]) {
                snprintf(g->driver, sizeof g->driver, "amdgpu");
            } else if (g->vendor == GT_VENDOR_NVIDIA) {
                snprintf(g->driver, sizeof g->driver, "nvidia");
            }
        }
        g->active = 1;
        if (gt_readf(b, sizeof b, dev, "power/runtime_status") == 0 && strncmp(b, "active", 6)) g->active = 0;
        if (g->vendor == GT_VENDOR_AMD && amdgpu_ids_name(g->device_id, g->revision_id, g->name, sizeof g->name) == 0) {}
        else if (pci_ids_name(g->vendor_id, g->device_id, b, sizeof b) == 0)
            snprintf(g->name, sizeof g->name, "%s %s", gt_vendor_name(g->vendor), b);
        else if (g->vendor == GT_VENDOR_AMD) snprintf(g->name, sizeof g->name, "AMD Radeon Graphics");
        else snprintf(g->name, sizeof g->name, "%s GPU %04x:%04x", gt_vendor_name(g->vendor), g->vendor_id, g->device_id);
        n++;
    }
    closedir(dir);
    qsort(list, n, sizeof *list, cmp_dev);
    for (int i = 0; i < n; i++) list[i].index = i;
    return n;
}

/* ------------------------------------------------------------------ gpu_metrics */

typedef struct { const char *name; int off, size, count; } gt_mfield;
#include "gpu_top_metrics.inc"

static const char *const M_NAMES[GT_M_COUNT] = {
    "temperature_edge", "temperature_hotspot", "temperature_mem", "temperature_gfx", "temperature_soc",
    "temperature_vrgfx", "temperature_vrsoc", "temperature_vrmem", "temperature_skin",
    "average_cpu_power", "average_soc_power", "average_gfx_power", "average_ipu_power", "average_sys_power",
    "average_gfxclk_frequency", "average_socclk_frequency", "average_uclk_frequency", "average_fclk_frequency",
    "average_vclk_frequency", "average_dclk_frequency", "average_vclk1_frequency", "average_dclk1_frequency",
    "average_ipuclk_frequency", "average_mpipu_frequency",
    "current_gfxclk", "current_socclk", "current_uclk", "current_fclk", "current_vclk", "current_dclk",
    "current_vclk1", "current_dclk1", "current_core_maxfreq", "current_gfx_maxfreq",
    "voltage_gfx", "voltage_soc", "voltage_mem", "fan_pwm", "pcie_link_width", "pcie_link_speed",
    "average_cpu_voltage", "average_soc_voltage", "average_gfx_voltage",
    "average_cpu_current", "average_soc_current", "average_gfx_current",
    "average_dram_reads", "average_dram_writes", "average_ipu_reads", "average_ipu_writes",
    "stapm_power_limit", "current_stapm_power_limit",
    "average_socket_power", "average_apu_power", "average_dgpu_power", "average_all_core_power",
    "average_gfx_activity", "average_umc_activity", "average_mm_activity",
    "current_fan_speed", "energy_accumulator", "throttle_status", "indep_throttle_status", "gfxclk_lock_status",
};
static const char *const MA_NAMES[GT_MA_COUNT] = {
    "temperature_core", "temperature_l3", "current_coreclk", "current_l3clk", "average_core_power",
    "average_temperature_core", "average_temperature_l3", "average_ipu_activity",
};
/* the struct field names that amdgpu_top reads under another name */
static const char *const M_ALIAS[][2] = {
    { "average_vclk0_frequency", "average_vclk_frequency" }, { "average_dclk0_frequency", "average_dclk_frequency" },
    { "current_vclk0", "current_vclk" }, { "current_dclk0", "current_dclk" },
    { "average_vcn_activity", "average_mm_activity" }, { "curr_socket_power", "average_socket_power" },
};

const char *gt_metric_name(int i) { return i >= 0 && i < GT_M_COUNT ? M_NAMES[i] : ""; }
const char *gt_metric_array_name(int i) { return i >= 0 && i < GT_MA_COUNT ? MA_NAMES[i] : ""; }

const char *gt_metric_unit(int i)
{
    const char *n = gt_metric_name(i);
    if (!strncmp(n, "temperature", 11)) return "C";
    if (strstr(n, "power") && !strstr(n, "limit")) return "W";
    if (strstr(n, "power_limit")) return "W";
    if (strstr(n, "clk") || strstr(n, "freq")) return "MHz";
    if (strstr(n, "voltage")) return "mV";
    if (strstr(n, "current") && strstr(n, "average_")) return "mA";
    if (strstr(n, "activity")) return "%";
    if (strstr(n, "reads") || strstr(n, "writes")) return "MB/s";
    if (!strcmp(n, "current_fan_speed")) return "RPM";
    return "";
}

static unsigned long long rd_le(const unsigned char *p, int size)
{
    unsigned long long v = 0;
    for (int i = size - 1; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

/* the throttler bits of amdgpu_smu.h (indep_throttle_status) */
static const struct { int bit; const char *name; } THR[] = {
    { 0, "PPT0" }, { 1, "PPT1" }, { 2, "PPT2" }, { 3, "PPT3" }, { 4, "SPL" }, { 5, "FPPT" }, { 6, "SPPT" }, { 7, "SPPT_APU" },
    { 16, "TDC_GFX" }, { 17, "TDC_SOC" }, { 18, "TDC_MEM" }, { 19, "TDC_VDD" }, { 20, "TDC_CVIP" }, { 21, "EDC_CPU" },
    { 22, "EDC_GFX" }, { 23, "APCC" },
    { 32, "TEMP_GPU" }, { 33, "TEMP_CORE" }, { 34, "TEMP_MEM" }, { 35, "TEMP_EDGE" }, { 36, "TEMP_HOTSPOT" },
    { 37, "TEMP_SOC" }, { 38, "TEMP_VR_GFX" }, { 39, "TEMP_VR_SOC" }, { 40, "TEMP_VR_MEM0" }, { 41, "TEMP_VR_MEM1" },
    { 42, "TEMP_LIQUID0" }, { 43, "TEMP_LIQUID1" }, { 44, "VRHOT0" }, { 45, "VRHOT1" }, { 46, "PROCHOT_CPU" },
    { 47, "PROCHOT_GPU" }, { 56, "PPM" }, { 57, "FIT" },
};

void gt_throttle_names(unsigned long long bits, int hotspot_c, int fix_hotspot, int *n, char out[][16], int max)
{
    /* amdgpu_top: SMU 13.0.0/7 set TEMP_HOTSPOT nearly always; ignore it below 90 C (drm/amd issue 3251) */
    if (fix_hotspot && hotspot_c >= 0 && hotspot_c < 90) bits &= ~(1ULL << 36);
    *n = 0;
    for (int b = 0; b < 64 && *n < max; b++) {
        size_t k;
        const char *nm = "Unknown";
        if (!((bits >> b) & 1)) continue;
        for (k = 0; k < sizeof THR / sizeof THR[0]; k++)
            if (THR[k].bit == b) { nm = THR[k].name; break; }
        snprintf(out[(*n)++], 16, "%s", nm);
    }
}

int gt_metrics_decode(const unsigned char *buf, size_t len, gt_metrics *m)
{
    const gt_mfield *f = NULL;
    size_t k;
    int has_indep = 0;
    unsigned long long indep = 0;
    memset(m, 0, sizeof *m);
    for (k = 0; k < GT_M_COUNT; k++) m->v[k] = GT_NA;
    m->npuclk = m->mpnpuclk = m->npu_power = m->npu_reads = m->npu_writes = GT_NA;
    if (len < 4) return -1;
    m->size = (int)rd_le(buf, 2);
    m->format_rev = buf[2];
    m->content_rev = buf[3];
    for (k = 0; k < sizeof gm_layouts / sizeof gm_layouts[0]; k++)
        if (gm_layouts[k].fmt == m->format_rev && gm_layouts[k].cnt == m->content_rev) {
            if (len < (size_t)gm_layouts[k].size) return -1;
            f = gm_layouts[k].f;
        }
    if (!f) return -1;
    m->present = 1;
    for (; f->name; f++) {
        const char *nm = f->name;
        unsigned long long v;
        int i;
        for (k = 0; k < sizeof M_ALIAS / sizeof M_ALIAS[0]; k++)
            if (!strcmp(nm, M_ALIAS[k][0])) { nm = M_ALIAS[k][1]; break; }
        if (!strcmp(nm, "system_clock_counter")) { m->system_clock_counter = rd_le(buf + f->off, f->size); continue; }
        if (f->count > 1) {
            for (i = 0; i < GT_MA_COUNT; i++) if (!strcmp(nm, MA_NAMES[i])) break;
            if (i == GT_MA_COUNT) continue;
            m->na[i] = f->count > GT_MA_LEN ? GT_MA_LEN : f->count;
            for (int j = 0; j < m->na[i]; j++) m->a[i][j] = (int)rd_le(buf + f->off + j * f->size, f->size);
            continue;
        }
        v = rd_le(buf + f->off, f->size);
        if (!strcmp(nm, "indep_throttle_status")) { has_indep = 1; indep = v; }
        for (i = 0; i < GT_M_COUNT; i++) if (!strcmp(nm, M_NAMES[i])) break;
        if (i == GT_M_COUNT) continue;
        if ((f->size == 2 && v == 0xFFFF) || (f->size == 4 && v == 0xFFFFFFFFULL && i != GT_M_THROTTLE_STATUS)) continue;
        m->v[i] = (long long)v;
    }
    if (has_indep && m->format_rev == 1) {
        char t[GT_MAX_THR][16];
        int n;
        gt_throttle_names(indep, (int)m->v[GT_M_TEMPERATURE_HOTSPOT], 1, &n, t, GT_MAX_THR);
        m->nthr = n;
        memcpy(m->thr, t, sizeof t);
    } else if (has_indep) {
        gt_throttle_names(indep, -1, 0, &m->nthr, m->thr, GT_MAX_THR);
    }
    if (m->format_rev == 3) {   /* NPU (Strix): amdgpu_top GetNpuMetrics */
        m->npuclk = (int)m->v[GT_M_AVERAGE_IPUCLK_FREQUENCY];
        m->mpnpuclk = (int)m->v[GT_M_AVERAGE_MPIPU_FREQUENCY];
        m->npu_power = (int)m->v[GT_M_AVERAGE_IPU_POWER];
        m->npu_reads = (int)m->v[GT_M_AVERAGE_IPU_READS];
        m->npu_writes = (int)m->v[GT_M_AVERAGE_IPU_WRITES];
        m->n_npu_busy = m->na[GT_MA_AVERAGE_IPU_ACTIVITY] > 8 ? 8 : m->na[GT_MA_AVERAGE_IPU_ACTIVITY];
        for (int j = 0; j < m->n_npu_busy; j++) m->npu_busy[j] = m->a[GT_MA_AVERAGE_IPU_ACTIVITY][j];
    }
    return 0;
}

/* 2.5.0: can this card really go to sleep (runtime suspend)? Only then is it worth pausing the readings while it is
   idle (amdgpu_top's D3 drop, meant for laptop and second GPUs). A card with runtime PM off (power/control "on") or
   driving a connected monitor never sleeps; pausing it only blanked the panels ("no gpu_metrics file") at desktop
   idle. Plain sysfs reads: none of them wake the card. */
static int can_sleep(const char *sys)
{
    char p[GT_PATH + 64], b[16] = "";
    FILE *f;
    DIR *dd, *cd;
    struct dirent *e, *c;
    int connected = 0;
    snprintf(p, sizeof p, "%s/power/control", sys);
    if ((f = fopen(p, "r"))) { if (!fgets(b, sizeof b, f)) b[0] = 0; fclose(f); }
    if (strncmp(b, "auto", 4)) return 0;
    snprintf(p, sizeof p, "%s/drm", sys);
    if (!(dd = opendir(p))) return 1;
    while (!connected && (e = readdir(dd))) {
        char cp[GT_PATH + 128];
        if (strncmp(e->d_name, "card", 4)) continue;
        snprintf(cp, sizeof cp, "%s/drm/%s", sys, e->d_name);
        if (!(cd = opendir(cp))) continue;
        while (!connected && (c = readdir(cd))) {
            char sp[GT_PATH + 512], st[24] = "";
            if (strncmp(c->d_name, e->d_name, strlen(e->d_name)) || c->d_name[strlen(e->d_name)] != '-') continue;
            snprintf(sp, sizeof sp, "%s/%s/status", cp, c->d_name);
            if ((f = fopen(sp, "r"))) { if (!fgets(st, sizeof st, f)) st[0] = 0; fclose(f); }
            if (!strncmp(st, "connected", 9)) connected = 1;
        }
        closedir(cd);
    }
    closedir(dd);
    return !connected;
}

static int read_metrics(const char *sys, gt_metrics *m)
{
    unsigned char buf[4096];
    char p[GT_PATH];
    long n;
    snprintf(p, sizeof p, "%s/gpu_metrics", sys);
    if ((n = gt_read_bin(p, buf, sizeof buf)) <= 0) { memset(m, 0, sizeof *m); return -1; }
    return gt_metrics_decode(buf, (size_t)n, m);
}

double gt_metric_shown(const gt_metrics *m, int i, const char **unit)
{
    const char *u = gt_metric_unit(i);
    double v = (double)m->v[i];
    if (m->format_rev == 3 && !strcmp(u, "W")) u = "mW";
    if (m->format_rev >= 2 && (!strcmp(u, "C") || !strcmp(u, "%"))) {   /* APU tables: 0.01 C / 0.01 % */
        if (!(m->format_rev == 3 && i == GT_M_AVERAGE_GFX_ACTIVITY && !m->gfx_act_centi)) v /= 100.0;   /* as gt_finish's Activity */
    }
    if (unit) *unit = u;
    return v;
}

int gt_metrics_text(const gt_metrics *m, char *buf, size_t len)
{
    if (!m->present) { gt_cat(buf, len, "GPU Metrics: Not Supported\n"); return -1; }
    gt_cat(buf, len, "GPU Metrics v%d.%d (%d bytes)\n", m->format_rev, m->content_rev, m->size);
    for (int i = 0; i < GT_M_COUNT; i++) {
        const char *u;
        double sv;
        if (m->v[i] == GT_NA) continue;
        sv = gt_metric_shown(m, i, &u);
        if (i == GT_M_THROTTLE_STATUS || i == GT_M_INDEP_THROTTLE_STATUS || i == GT_M_GFXCLK_LOCK_STATUS)
            gt_cat(buf, len, "    %-28s: 0x%llX\n", M_NAMES[i], (unsigned long long)m->v[i]);
        else if (m->format_rev >= 2 && (!strcmp(u, "C") || !strcmp(u, "%")))   /* APU tables: 0.01 C / 0.01 % (see gt_metric_shown) */
            gt_cat(buf, len, "    %-28s: %.2f %s\n", M_NAMES[i], sv, u);
        else gt_cat(buf, len, "    %-28s: %lld%s%s\n", M_NAMES[i], m->v[i], u[0] ? " " : "", u);
    }
    for (int i = 0; i < GT_MA_COUNT; i++) {
        if (!m->na[i]) continue;
        gt_cat(buf, len, "    %-28s: [", MA_NAMES[i]);
        for (int j = 0; j < m->na[i]; j++) gt_cat(buf, len, "%s%d", j ? ", " : "", m->a[i][j]);
        gt_cat(buf, len, "]\n");
    }
    gt_cat(buf, len, "    %-28s: %llu\n", "system_clock_counter", m->system_clock_counter);
    gt_cat(buf, len, "    %-28s: [", "Throttle Status");
    for (int i = 0; i < m->nthr; i++) gt_cat(buf, len, "%s%s", i ? ", " : "", m->thr[i]);
    gt_cat(buf, len, "]\n");
    return 0;
}

int gt_json_metrics(const gt_metrics *m, char *buf, size_t len)
{
    gt_json j;
    int i;
    gj_init(&j, buf, len);
    if (!m->present) { gj_raw(&j, NULL, "null"); return -1; }
    gj_obj(&j, NULL);
    gj_obj(&j, "header");
    gj_int(&j, "structure_size", m->size);
    gj_int(&j, "format_revision", m->format_rev);
    gj_int(&j, "content_revision", m->content_rev);
    gj_end(&j, '}');
    for (i = 0; i <= GT_M_AVERAGE_ALL_CORE_POWER; i++) gj_int(&j, M_NAMES[i], m->v[i]);
    for (i = 0; i < GT_MA_COUNT; i++) {
        if (!m->na[i]) { gj_null(&j, MA_NAMES[i]); continue; }
        gj_arr(&j, MA_NAMES[i]);
        for (int k = 0; k < m->na[i]; k++) gj_int(&j, NULL, m->a[i][k]);
        gj_end(&j, ']');
    }
    gj_u64(&j, "system_clock_counter", m->system_clock_counter);
    if (m->v[GT_M_INDEP_THROTTLE_STATUS] == GT_NA) gj_null(&j, "Throttle Status");
    else {
        gj_arr(&j, "Throttle Status");
        for (i = 0; i < m->nthr; i++) gj_str(&j, NULL, m->thr[i]);
        gj_end(&j, ']');
    }
    gj_end(&j, '}');
    return 0;
}

/* ------------------------------------------------------------------ pp_dpm_*, PCIe links */

/* "1: 836Mhz *" */
static int dpm_mhz(const char *l)
{
    const char *c = strchr(l, ':');
    return c ? atoi(c + 1) : -1;
}

int gt_dpm_read(const char *sys, const char *file, gt_dpm *out)
{
    char b[2048], *l, *save = NULL;
    memset(out, 0, sizeof *out);
    out->cur = GT_NA;
    snprintf(out->name, sizeof out->name, "%s", !strncmp(file, "pp_dpm_", 7) ? file + 7 : file);
    if (gt_readf(b, sizeof b, sys, file) || !b[0]) return -1;
    for (l = strtok_r(b, "\n", &save); l && out->n < GT_MAX_DPM; l = strtok_r(NULL, "\n", &save)) {
        if (!strcmp(out->name, "pcie")) {   /* "2: 32.0GT/s, x16 1143Mhz *" */
            const char *c = strchr(l, ':'), *x = strstr(l, ", x");
            double gts;
            if (!c || !x) continue;
            gts = atof(c + 1);
            out->mhz[out->n] = gts >= 63 ? 6 : gts >= 31 ? 5 : gts >= 15 ? 4 : gts >= 7 ? 3 : gts >= 4.9 ? 2 : 1;
            out->lanes[out->n] = atoi(x + 3);
        } else {
            out->mhz[out->n] = dpm_mhz(l);
            out->lanes[out->n] = 0;
        }
        if (strchr(l, '*')) out->cur = out->n;
        out->n++;
    }
    return out->n ? 0 : -1;
}

/* amdgpu_top DpmClockRange::from_sysfs */
int gt_dpm_range(const char *sys, const char *file, int *cur, int *min, int *max)
{
    gt_dpm d;
    int first, last;
    if (gt_dpm_read(sys, file, &d) || d.n < 1) return -1;
    first = d.mhz[0];
    last = d.mhz[d.n - 1];
    if (d.n == 2 || d.n == 3) {   /* fine-grained DPM: the '*' line is the current clock */
        int clks[2] = { d.mhz[0], d.n > 1 ? d.mhz[1] : d.mhz[0] };
        *cur = d.cur >= 0 && d.cur < 2 ? clks[d.cur] : last;
        *min = first < last ? first : last;
        *max = first > last ? first : last;
        return 0;
    }
    *cur = d.cur >= 0 ? d.mhz[d.cur] : last;
    *min = first < last ? first : last;
    *max = first > last ? first : last;
    return 0;
}

static int speed_gen(const char *s)
{
    double g = atof(s);
    if (g <= 0) return -1;
    return g >= 63 ? 6 : g >= 31 ? 5 : g >= 15 ? 4 : g >= 7 ? 3 : g >= 4.9 ? 2 : 1;
}

int gt_link_read(const char *dir, int max, int *gen, int *width)
{
    char s[64], w[16];
    if (gt_readf(s, sizeof s, dir, max ? "max_link_speed" : "current_link_speed")) return -1;
    if (gt_readf(w, sizeof w, dir, max ? "max_link_width" : "current_link_width")) return -1;
    if (!strstr(s, "PCIe") || (*gen = speed_gen(s)) < 0) return -1;
    *width = atoi(w);
    return 0;
}

/* ------------------------------------------------------------------ hwmon sensors */

static int hwmon_temp(const char *hw, const char *label, int *cur, int *crit, int *emerg)
{
    char p[64], b[32];
    long long v;
    for (int i = 1; i <= 8; i++) {
        snprintf(p, sizeof p, "temp%d_label", i);
        if (gt_readf(b, sizeof b, hw, p) || strcmp(b, label)) continue;
        snprintf(p, sizeof p, "temp%d_input", i);
        *cur = gt_readf_ll(&v, hw, p) ? GT_NA : (int)(v / 1000);
        if (crit) { snprintf(p, sizeof p, "temp%d_crit", i); *crit = gt_readf_ll(&v, hw, p) ? GT_NA : (int)(v / 1000); }
        if (emerg) { snprintf(p, sizeof p, "temp%d_emergency", i); *emerg = gt_readf_ll(&v, hw, p) ? GT_NA : (int)(v / 1000); }
        return 0;
    }
    *cur = GT_NA;
    if (crit) *crit = GT_NA;
    if (emerg) *emerg = GT_NA;
    return -1;
}

static void find_hwmon(gt_dev *d)
{
    char p[GT_PATH];
    DIR *dir;
    struct dirent *e;
    d->hwmon[0] = 0;
    snprintf(p, sizeof p, "%s/hwmon", d->sys);
    if (!(dir = opendir(p))) return;
    while ((e = readdir(dir)))
        if (!strncmp(e->d_name, "hwmon", 5)) { snprintf(d->hwmon, sizeof d->hwmon, "%s/%s", p, e->d_name); break; }
    closedir(dir);
}

/* the PCIe ports above the GPU (amdgpu_top BUS_INFO::get_system_pcie_port_sysfs_path): go up past AMD's
   own switch ports (vendor 0x1002, class 0x060400), at most twice */
static void find_ports(gt_dev *d)
{
    char tmp[GT_PATH], b[32], prev[GT_PATH];
    snprintf(tmp, sizeof tmp, "%s", d->sys);
    *strrchr(tmp, '/') = 0;
    snprintf(prev, sizeof prev, "%s", d->sys);
    for (int i = 0; i < 2; i++) {
        char v[32];
        if (gt_readf(v, sizeof v, tmp, "vendor") || gt_readf(b, sizeof b, tmp, "class")) break;
        if (strcmp(v, "0x1002") || strcmp(b, "0x060400")) break;
        snprintf(prev, sizeof prev, "%s", tmp);
        *strrchr(tmp, '/') = 0;
    }
    snprintf(d->port_sys, sizeof d->port_sys, "%s", tmp);
    snprintf(d->port_gpu, sizeof d->port_gpu, "%s", prev);
}

static void read_sensors(gt_dev *d, gt_stat *s, int idle)
{
    const char *hw = d->hwmon;
    long long v, cap = -1;
    char b[64];
    int c, mn, mx;
    s->edge = s->junction = s->memtemp = GT_NA;
    s->power_avg = s->power_input = -1;
    s->power_cap = s->fan_rpm = s->fan_pct = GT_NA;
    s->fan_max_rpm = d->info.fan_max_rpm;
    s->pcie_gen = s->pcie_width = GT_NA;
    s->fclk = s->socclk = s->fclk_min = s->fclk_max = s->socclk_min = s->socclk_max = GT_NA;
    s->pci_power_state[0] = s->power_profile[0] = 0;
    if (!d->info.is_apu && d->port_gpu[0] && gt_readf(b, sizeof b, d->port_gpu, "power_state") == 0)
        snprintf(s->pci_power_state, sizeof s->pci_power_state, "%s", b);
    if (idle) return;   /* amdgpu_top Sensors::update_for_idle: leave the sleeping GPU alone */
    if (hw[0]) {
        hwmon_temp(hw, "edge", &s->edge, NULL, NULL);
        hwmon_temp(hw, "junction", &s->junction, NULL, NULL);
        hwmon_temp(hw, "mem", &s->memtemp, NULL, NULL);
        if (gt_readf_ll(&v, hw, "power1_average") == 0) s->power_avg = (double)(v / 1000000);
        if (gt_readf_ll(&v, hw, "power1_input") == 0) s->power_input = (double)(v / 1000000);
        if (gt_readf_ll(&cap, hw, "power1_cap") == 0) s->power_cap = (int)(cap / 1000000);
        if (gt_readf_ll(&v, hw, "fan1_input") == 0) s->fan_rpm = (int)v;
        if (gt_readf_ll(&v, hw, "pwm1") == 0) {
            long long pm = 255;
            gt_readf_ll(&pm, hw, "pwm1_max");
            if (pm > 0) s->fan_pct = (int)(v * 100 / pm);
        }
        if (s->sclk == GT_NA && gt_readf_ll(&v, hw, "freq1_input") == 0) s->sclk = (int)(v / 1000000);
        if (s->mclk == GT_NA && gt_readf_ll(&v, hw, "freq2_input") == 0) s->mclk = (int)(v / 1000000);
        if (s->vddgfx == GT_NA && gt_readf(b, sizeof b, hw, "in0_label") == 0 && !strcmp(b, "vddgfx") &&
            gt_readf_ll(&v, hw, "in0_input") == 0) s->vddgfx = (int)v;
        if (s->vddnb == GT_NA && gt_readf(b, sizeof b, hw, "in1_label") == 0 && !strcmp(b, "vddnb") &&
            gt_readf_ll(&v, hw, "in1_input") == 0) s->vddnb = (int)v;
    }
    if (!d->info.is_apu) {
        if (d->vega10_later) {
            gt_dpm p;
            if (gt_dpm_read(d->sys, "pp_dpm_pcie", &p) == 0 && p.cur >= 0) { s->pcie_gen = p.mhz[p.cur]; s->pcie_width = p.lanes[p.cur]; }
            else gt_link_read(d->sys, 0, &s->pcie_gen, &s->pcie_width);
        } else {
            gt_link_read(d->sys, 0, &s->pcie_gen, &s->pcie_width);
        }
    }
    if (gt_dpm_range(d->sys, "pp_dpm_fclk", &c, &mn, &mx) == 0) { s->fclk = c; s->fclk_min = mn; s->fclk_max = mx; }
    if (gt_dpm_range(d->sys, "pp_dpm_socclk", &c, &mn, &mx) == 0) { s->socclk = c; s->socclk_min = mn; s->socclk_max = mx; }
    /* the power profile: the line with '*' */
    {
        char pp[8192], *l, *save = NULL;
        static const char *names[] = { "BOOTUP_DEFAULT", "3D_FULL_SCREEN", "POWER_SAVING", "VIDEO", "VR", "COMPUTE",
                                       "CUSTOM", "WINDOW_3D", "CAPPED", "UNCAPPED" };
        if (gt_readf(pp, sizeof pp, d->sys, "pp_power_profile_mode") == 0)
            for (l = strtok_r(pp, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
                char *t = l;
                int id;
                while (*t == ' ') t++;
                { char *q = t; while (isdigit((unsigned char)*q)) q++; if (q == t || *q != ' ') continue; }
                id = atoi(t);
                if (id < 0 || id > 9) continue;
                if (l[strlen(l) - 1] == '*' || strstr(l, "*:")) { snprintf(s->power_profile, sizeof s->power_profile, "%s", names[id]); break; }
            }
    }
    if (d->info.is_apu == 1) {   /* the CPU's Tctl (k10temp) */
        char base[GT_PATH], n[32];
        DIR *dir = opendir(gt_p(base, sizeof base, "/sys/class/hwmon"));
        struct dirent *e;
        s->tctl = GT_NA;
        while (dir && (e = readdir(dir))) {
            char hp[GT_PATH];
            if (e->d_name[0] == '.') continue;
            snprintf(hp, sizeof hp, "%s/%s", base, e->d_name);
            if (gt_readf(n, sizeof n, hp, "name") == 0 && !strcmp(n, "k10temp") && gt_readf_ll(&v, hp, "temp1_input") == 0) {
                s->tctl = (int)(v / 1000);
                break;
            }
        }
        if (dir) closedir(dir);
    }
    /* RAS: UMC (memory) error counts */
    {
        char r[GT_PATH];
        long long ce = 0, ue = 0;
        snprintf(r, sizeof r, "%s/ras/umc_err_count", d->sys);
        if (gt_read(r, b, sizeof b) == 0) {
            const char *u = strstr(b, "ue:"), *ccp = strstr(b, "ce:");
            if (u) ue = atoll(u + 3);
            if (ccp) ce = atoll(ccp + 3);
            s->mem_errors = ce + ue;
        }
    }
}

/* ------------------------------------------------------------------ GRBM / GRBM2 */

/* amdgpu_top stat/mod.rs: name, bit of GRBM_STATUS (0x2004) / GRBM_STATUS2 (0x2002) */
typedef struct { const char *name; int bit; } pcidx;
static const pcidx GRBM_INDEX[] = { { "Graphics Pipe", 31 }, { "Texture Pipe", 14 }, { "Shader Export", 20 },
    { "Shader Processor Interpolator", 22 }, { "Primitive Assembly", 25 }, { "Depth Block", 26 }, { "Color Block", 30 },
    { "Vertex Grouper / Tessellator", 17 }, { "Input Assembly", 19 }, { "Work Distributor", 21 }, { NULL, 0 } };
static const pcidx GFX10_GRBM_INDEX[] = { { "Graphics Pipe", 31 }, { "Texture Pipe", 14 }, { "Shader Export", 20 },
    { "Shader Processor Interpolator", 22 }, { "Primitive Assembly", 25 }, { "Depth Block", 26 }, { "Color Block", 30 },
    { "Geometry Engine", 21 }, { NULL, 0 } };
static const pcidx GRBM2_INDEX[] = { { "RunList Controller", 24 }, { "Texture Cache", 25 },
    { "Command Processor -  Fetcher", 28 }, { "Command Processor -  Compute", 29 }, { "Command Processor - Graphics", 30 },
    { NULL, 0 } };
static const pcidx GFX9_GRBM2_INDEX[] = { { "RunList Controller", 24 }, { "Texture Cache", 25 },
    { "Unified Translation Cache Level-2", 15 }, { "Efficiency Arbiter", 16 }, { "Render Backend Memory Interface", 17 },
    { "Command Processor -  Fetcher", 28 }, { "Command Processor -  Compute", 29 }, { "Command Processor - Graphics", 30 },
    { NULL, 0 } };
static const pcidx GFX10_GRBM2_INDEX[] = { { "RunList Controller", 24 }, { "Texture Cache per Pipe", 25 },
    { "Unified Translation Cache Level-2", 15 }, { "Efficiency Arbiter", 16 }, { "Render Backend Memory Interface", 17 },
    { "SDMA", 21 }, { "Command Processor -  Fetcher", 28 }, { "Command Processor -  Compute", 29 },
    { "Command Processor - Graphics", 30 }, { NULL, 0 } };
static const pcidx GFX10_3_GRBM2_INDEX[] = { { "RunList Controller", 26 }, { "Texture Cache per Pipe", 27 },
    { "Unified Translation Cache Level-2", 15 }, { "Efficiency Arbiter", 16 }, { "Render Backend Memory Interface", 17 },
    { "SDMA", 21 }, { "Command Processor -  Fetcher", 28 }, { "Command Processor -  Compute", 29 },
    { "Command Processor - Graphics", 30 }, { NULL, 0 } };
static const pcidx GFX12_GRBM2_INDEX[] = { { "RunList Controller", 26 }, { "Texture Cache per Pipe", 27 },
    { "Unified Translation Cache Level-2", 15 }, { "Efficiency Arbiter", 16 }, { "SDMA", 21 },
    { "Command Processor -  Fetcher", 28 }, { "Command Processor -  Compute", 29 }, { "Command Processor - Graphics", 30 },
    { NULL, 0 } };

static void pc_setup(gt_dev *d)
{
    const char *cc = d->info.chip_class;
    int gfx = 0;
    const pcidx *g1, *g2;
    if (!strncmp(cc, "GFX", 3)) gfx = atoi(cc + 3) * 10 + (strchr(cc, '_') ? atoi(strchr(cc, '_') + 1) : 0);
    g1 = gfx >= 100 ? GFX10_GRBM_INDEX : GRBM_INDEX;
    g2 = gfx >= 120 ? GFX12_GRBM2_INDEX : gfx >= 103 ? GFX10_3_GRBM2_INDEX : gfx >= 100 ? GFX10_GRBM2_INDEX :
         gfx >= 90 ? GFX9_GRBM2_INDEX : GRBM2_INDEX;
    for (d->ngrbm = 0; g1[d->ngrbm].name; d->ngrbm++) { d->grbm_name[d->ngrbm] = g1[d->ngrbm].name; d->grbm_bit[d->ngrbm] = g1[d->ngrbm].bit; }
    for (d->ngrbm2 = 0; g2[d->ngrbm2].name; d->ngrbm2++) { d->grbm2_name[d->ngrbm2] = g2[d->ngrbm2].name; d->grbm2_bit[d->ngrbm2] = g2[d->ngrbm2].bit; }
}

void gt_pc_begin(gt_dev *d)
{
    d->pc_n = 0;
    d->pc_try = 0;
    memset(d->acc1, 0, sizeof d->acc1);
    memset(d->acc2, 0, sizeof d->acc2);
}

void gt_pc_read(gt_dev *d)
{
    unsigned g1, g2;
    /* amdgpu_top update_pc: no reads while idle (dynamic_no_pc) or asleep, and never with --no-pc */
    if (d->opts.no_pc || d->pc_skip || d->to_d3 || d->pc_broken || !d->amd) return;
    d->pc_try++;
    if (gt_amd_read_pc(d, &g1, &g2)) return;
    for (int b = 0; b < 32; b++) { d->acc1[b] += (g1 >> b) & 1; d->acc2[b] += (g2 >> b) & 1; }
    d->pc_n++;
}

/* ------------------------------------------------------------------ fdinfo (per process) */

enum { E_GFX, E_COMPUTE, E_DMA, E_DEC, E_ENC, E_UVD_ENC, E_JPEG, E_VPE, E_N };

typedef struct {
    long long eng[E_N], cyc[E_N], tot[E_N];
    long long vram, gtt, cpu;   /* KiB */
    int has_cyc;
} fdacc;

/* "drm-engine-gfx:\t19646235147 ns" (amdgpu), i915/xe names mapped onto amdgpu's */
static int engine_index(const char *e)
{
    if (!strcmp(e, "gfx") || !strcmp(e, "render") || !strcmp(e, "rcs")) return E_GFX;
    if (!strcmp(e, "compute") || !strcmp(e, "ccs")) return E_COMPUTE;
    if (!strcmp(e, "dma") || !strcmp(e, "copy") || !strcmp(e, "bcs")) return E_DMA;
    if (!strcmp(e, "dec") || !strcmp(e, "video") || !strcmp(e, "vcs")) return E_DEC;
    if (!strcmp(e, "enc")) return E_ENC;
    if (!strcmp(e, "enc_1")) return E_UVD_ENC;
    if (!strcmp(e, "jpeg")) return E_JPEG;
    if (!strcmp(e, "vpe") || !strcmp(e, "video-enhance") || !strcmp(e, "vecs")) return E_VPE;
    return -1;
}

static long long kib_val(const char *s)
{
    long long v = atoll(s);
    if (strstr(s, "MiB")) v <<= 10;
    else if (strstr(s, "GiB")) v <<= 20;
    else if (!strstr(s, "KiB")) v >>= 10;   /* plain bytes */
    return v;
}

/* one fdinfo file; returns the drm-client-id, -1 when it is not a DRM client of this device */
static long long parse_fdinfo(const char *text, const char *pci, fdacc *a)
{
    char line[256];
    const char *p = text;
    long long id = -1;
    long long tvram = 0, tgtt = 0, tcpu = 0;   /* drm-total-*: used when this file has no drm-memory-* lines */
    int ok_dev = 1, seen_mem = 0, seen_total = 0;
    while (*p) {
        size_t l = strcspn(p, "\n");
        char *v;
        if (l >= sizeof line) l = sizeof line - 1;
        memcpy(line, p, l);
        line[l] = 0;
        p += l + (p[l] == '\n');
        if (!(v = strchr(line, ':'))) continue;
        *v++ = 0;
        while (*v == ' ' || *v == '\t') v++;
        if (!strcmp(line, "drm-client-id")) id = atoll(v);
        else if (!strcmp(line, "drm-pdev")) ok_dev = !strcmp(v, pci);
        else if (!strcmp(line, "drm-memory-vram")) { a->vram += kib_val(v); seen_mem = 1; }
        else if (!strcmp(line, "drm-memory-gtt")) { a->gtt += kib_val(v); seen_mem = 1; }
        else if (!strcmp(line, "drm-memory-cpu")) { a->cpu += kib_val(v); seen_mem = 1; }
        else if (!strcmp(line, "drm-total-vram")) { tvram += kib_val(v); seen_total = 1; }
        else if (!strcmp(line, "drm-total-gtt")) { tgtt += kib_val(v); seen_total = 1; }
        else if (!strcmp(line, "drm-total-cpu")) { tcpu += kib_val(v); seen_total = 1; }
        else if (!strncmp(line, "drm-engine-", 11) && strncmp(line + 11, "capacity-", 9)) {
            int e = engine_index(line + 11);
            if (e >= 0) a->eng[e] += atoll(v);
        } else if (!strncmp(line, "drm-cycles-", 11)) {
            int e = engine_index(line + 11);
            if (e >= 0) { a->cyc[e] += atoll(v); a->has_cyc = 1; }
        } else if (!strncmp(line, "drm-total-cycles-", 17)) {
            int e = engine_index(line + 17);
            if (e >= 0) a->tot[e] += atoll(v);
        }
    }
    /* drm-memory-<region> is the deprecated alias of drm-total-<region> (DRM fdinfo docs): amdgpu still prints both,
       but a kernel that drops the old lines must not show every process at 0 MiB (as amdgpu_top) */
    if (!seen_mem && seen_total) { a->vram += tvram; a->gtt += tgtt; a->cpu += tcpu; }
    return ok_dev ? id : -1;
}

static int diff_pct(long long pre, long long cur, long long interval_ns)
{
    if (pre == 0 || cur < pre || interval_ns <= 0) return 0;
    return (int)((cur - pre) * 100 / interval_ns);
}

static double proc_cpu_s(const char *procdir)
{
    char b[1024], *rp;
    unsigned long long ut = 0, st = 0;
    if (gt_readf(b, sizeof b, procdir, "stat") || !(rp = strrchr(b, ')'))) return 0;
    /* fields after ")": state is the 3rd; utime the 14th, stime the 15th */
    if (sscanf(rp + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &ut, &st) != 2) return 0;
    return (double)(ut + st) / 100.0;
}

static int cmp_proc(const void *a, const void *b)
{
    const gt_proc *x = a, *y = b;
    if (x->vram_kib != y->vram_kib) return x->vram_kib < y->vram_kib ? 1 : -1;
    return x->pid - y->pid;
}

static void read_fdinfo(gt_dev *d, gt_stat *s, int interval_ms)
{
    char base[GT_PATH];
    DIR *pd;
    struct dirent *pe;
    long long ids[512];
    int nids = 0, np = 0, has_vcn = d->info.has_vcn > 0, uni = d->info.has_vcn_unified > 0, vpe = d->info.has_vpe > 0;
    long long ivl = (long long)interval_ms * 1000000LL;
    gt_prev nprev[GT_MAX_PROC];
    int nn = 0;
    int next = gt_testing() && d->fixture_reads++ > 0;
    memset(&s->total, 0, sizeof s->total);
    s->nproc = 0;
    if (!d->info.dev.render[0] && !d->info.dev.card[0]) return;
    if (!(pd = opendir(gt_p(base, sizeof base, "/proc")))) return;
    while ((pe = readdir(pd)) && np < GT_MAX_PROC) {
        char pdir[GT_PATH], fdd[GT_PATH], cmd[32];
        DIR *fd;
        struct dirent *fe;
        fdacc a;
        int pid = atoi(pe->d_name), any = 0, kfd = 0;
        gt_proc *pr;
        gt_prev *pv = NULL;
        if (pid <= 1) continue;
        snprintf(pdir, sizeof pdir, "%s/%s", base, pe->d_name);
        /* amdgpu_top: skip systemd's processes (they share a context with the desktop) */
        if (gt_readf(cmd, sizeof cmd, pdir, "cmdline") == 0 &&
            (!strncmp(cmd, "/lib/systemd", 12) || !strncmp(cmd, "/usr/lib/systemd", 16))) continue;
        snprintf(fdd, sizeof fdd, "%s/fd", pdir);
        if (!(fd = opendir(fdd))) continue;
        memset(&a, 0, sizeof a);
        while ((fe = readdir(fd))) {
            char lp[GT_PATH], ln[128], fi[GT_PATH], txt[4096];
            ssize_t r;
            long long id;
            if (fe->d_name[0] == '.') continue;
            snprintf(lp, sizeof lp, "%s/%s", fdd, fe->d_name);
            if ((r = readlink(lp, ln, sizeof ln - 1)) <= 0) continue;
            ln[r] = 0;
            if (!strncmp(ln, "/dev/kfd", 8)) { kfd = 1; continue; }
            if (strcmp(ln, d->info.dev.render) && strcmp(ln, d->info.dev.card)) continue;
            snprintf(fi, sizeof fi, "%s/fdinfo/%s%s", pdir, fe->d_name, "");
            if (next) {   /* tests: the second pass reads fdinfo/N.next when the fixture has one */
                char fn[GT_PATH];
                struct stat sb;
                snprintf(fn, sizeof fn, "%s.next", fi);
                if (stat(fn, &sb) == 0) snprintf(fi, sizeof fi, "%s", fn);
            }
            if (gt_read(fi, txt, sizeof txt)) continue;
            {
                fdacc one;
                memset(&one, 0, sizeof one);
                id = parse_fdinfo(txt, d->info.dev.pci, &one);
                if (id < 0) continue;
                for (int k = 0; k < nids; k++) if (ids[k] == id) { id = -2; break; }
                if (id == -2) continue;   /* the same DRM client through another fd or process */
                if (nids < 512) ids[nids++] = id;
                for (int k = 0; k < E_N; k++) { a.eng[k] += one.eng[k]; a.cyc[k] += one.cyc[k]; a.tot[k] += one.tot[k]; }
                a.vram += one.vram; a.gtt += one.gtt; a.cpu += one.cpu; a.has_cyc |= one.has_cyc;
                any = 1;
            }
        }
        closedir(fd);
        if (!any) continue;
        pr = &s->proc[np++];
        memset(pr, 0, sizeof *pr);
        pr->pid = pid;
        if (gt_readf(pr->name, sizeof pr->name, pdir, "comm")) snprintf(pr->name, sizeof pr->name, "?");
        pr->vram_kib = a.vram; pr->gtt_kib = a.gtt; pr->cpu_kib = a.cpu;
        pr->is_kfd = kfd;
        for (int k = 0; k < d->nprev; k++) if (d->prev[k].pid == pid) { pv = &d->prev[k]; break; }
        {
            int p[E_N];
            double cpu_s = proc_cpu_s(pdir);
            for (int k = 0; k < E_N; k++) {
                if (a.has_cyc && a.tot[k]) p[k] = pv && a.tot[k] > pv->tot[k] ? (int)((a.cyc[k] - pv->cyc[k]) * 100 / (a.tot[k] - pv->tot[k])) : 0;
                else p[k] = pv ? diff_pct(pv->eng[k], a.eng[k], ivl) : 0;
                if (p[k] < 0) p[k] = 0;
            }
            pr->gfx = p[E_GFX]; pr->compute = p[E_COMPUTE]; pr->dma = p[E_DMA];
            pr->vcn_jpeg = has_vcn ? p[E_JPEG] : GT_NA;
            pr->vpe = vpe ? p[E_VPE] : GT_NA;
            /* VCN 4+: one queue for decode and encode (amdgpu_top calc_usage) */
            if (uni) { pr->media = (p[E_JPEG] + p[E_ENC]) / 2; pr->dec = pr->enc = GT_NA; pr->vcn_unified = p[E_DEC] + p[E_ENC]; }
            else if (has_vcn) { pr->dec = (p[E_DEC] + p[E_JPEG]) / 2; pr->enc = p[E_ENC]; pr->media = (p[E_DEC] + p[E_JPEG] + p[E_ENC]) / 3; pr->vcn_unified = GT_NA; }
            else { pr->dec = p[E_DEC]; pr->enc = (p[E_ENC] + p[E_UVD_ENC]) / 2; pr->media = (p[E_DEC] + p[E_ENC] + p[E_UVD_ENC]) / 3; pr->vcn_unified = GT_NA; }
            pr->cpu = pv && interval_ms > 0 ? (int)((cpu_s - pv->cpu_s) * 100.0 / (interval_ms / 1000.0) + 0.999) : 0;
            if (pr->cpu < 0) pr->cpu = 0;
            if (nn < GT_MAX_PROC) {
                gt_prev *q = &nprev[nn++];
                memset(q, 0, sizeof *q);
                q->pid = pid;
                memcpy(q->eng, a.eng, sizeof a.eng);
                memcpy(q->cyc, a.cyc, sizeof a.cyc);
                memcpy(q->tot, a.tot, sizeof a.tot);
                q->cpu_s = cpu_s;
            }
        }
        if (d->info.dev.vendor != GT_VENDOR_AMD) { pr->vcn_unified = GT_NA; pr->vcn_jpeg = GT_NA; }
    }
    closedir(pd);
    memcpy(d->prev, nprev, sizeof nprev[0] * nn);
    d->nprev = nn;
    s->nproc = np;
    qsort(s->proc, np, sizeof s->proc[0], cmp_proc);
    /* "Total fdinfo" */
    s->total.pid = GT_NA;
    snprintf(s->total.name, sizeof s->total.name, "Total");
    s->total.dec = s->total.enc = s->total.vcn_jpeg = s->total.vpe = s->total.vcn_unified = 0;
    for (int i = 0; i < np; i++) {
        gt_proc *p = &s->proc[i];
        s->total.vram_kib += p->vram_kib; s->total.gtt_kib += p->gtt_kib; s->total.cpu_kib += p->cpu_kib;
        s->total.cpu += p->cpu; s->total.gfx += p->gfx; s->total.compute += p->compute; s->total.dma += p->dma;
        s->total.media += p->media;
        if (p->dec != GT_NA) s->total.dec += p->dec;
        if (p->enc != GT_NA) s->total.enc += p->enc;
        if (p->vcn_jpeg != GT_NA) s->total.vcn_jpeg += p->vcn_jpeg;
        if (p->vpe != GT_NA) s->total.vpe += p->vpe;
        if (p->vcn_unified != GT_NA) s->total.vcn_unified += p->vcn_unified;
        s->total.is_kfd |= p->is_kfd;
    }
    if (uni) s->total.dec = s->total.enc = GT_NA;
    if (!has_vcn) s->total.vcn_jpeg = GT_NA;
    if (!vpe) s->total.vpe = GT_NA;
    if (!uni) s->total.vcn_unified = GT_NA;
}

/* ------------------------------------------------------------------ open / update */

/* the Sensors part of Device Info (amdgpu_top dump_info.rs sensors_info), placed after "Memory usage" */
static void add_sensor_section(gt_dev *d)
{
    gt_info *in = &d->info;
    gt_stat *st = calloc(1, sizeof *st);
    gt_section *s;
    int at = in->nsect;
    if (!st || in->nsect >= GT_MAX_SECT) { free(st); return; }
    st->sclk = st->mclk = st->vddgfx = st->vddnb = GT_NA;
    read_sensors(d, st, 0);
    for (int i = 0; i < in->nsect; i++) if (!strcmp(in->sect[i].title, "Memory usage")) { at = i + 1; break; }
    memmove(&in->sect[at + 1], &in->sect[at], sizeof in->sect[0] * (in->nsect - at));
    in->nsect++;
    s = &in->sect[at];
    memset(s, 0, sizeof *s);
    snprintf(s->title, sizeof s->title, "Sensors");
    {
        struct { const char *l; int c, cr, em; } T[3] = { { "Edge Temp.", st->edge, in->edge_crit, in->edge_emerg },
            { "Junction Temp.", st->junction, in->junction_crit, in->junction_emerg }, { "Memory Temp.", st->memtemp, in->mem_crit, in->mem_emerg } };
        for (int i = 0; i < 3; i++) {
            char v[160];
            if (T[i].c == GT_NA) continue;
            snprintf(v, sizeof v, "%3d C (Current)", T[i].c);
            if (T[i].cr != GT_NA) gt_cat(v, sizeof v, ", %3d C (Critical)", T[i].cr);
            if (T[i].em != GT_NA) gt_cat(v, sizeof v, ", %3d C (Emergency)", T[i].em);
            gt_kv_add(s, T[i].l, "%s", v);
        }
    }
    if (st->power_avg >= 0) gt_kv_add(s, "Power (Average)", "%3.0f W", st->power_avg);
    if (st->power_input >= 0) gt_kv_add(s, "Power (Input)", "%3.0f W", st->power_input);
    if (in->power_cap_w != GT_NA) gt_kv_add(s, "Power Cap.", "%3d W (%d-%d W)", in->power_cap_w, in->power_cap_min_w, in->power_cap_max_w);
    if (in->power_cap_default_w != GT_NA) gt_kv_add(s, "Power Cap. (Default)", "%3d W", in->power_cap_default_w);
    if (in->fan_max_rpm != GT_NA) gt_kv_add(s, "Fan RPM (Max)", "%d RPM", in->fan_max_rpm);
    if (st->fan_rpm != GT_NA) gt_kv_add(s, "Fan RPM (Current)", "%d RPM", st->fan_rpm);
    if (in->pcie_dpm_min_gen != GT_NA && in->pcie_dpm_max_gen != GT_NA)
        gt_kv_add(s, "PCIe Link Speed (DPM, Min-Max)", "Gen%dx%-2d - Gen%dx%-2d", in->pcie_dpm_min_gen, in->pcie_dpm_min_width,
                  in->pcie_dpm_max_gen, in->pcie_dpm_max_width);
    else if (in->pcie_dpm_max_gen != GT_NA)
        gt_kv_add(s, "PCIe Link Speed (DPM, Max)", "Gen%dx%-2d", in->pcie_dpm_max_gen, in->pcie_dpm_max_width);
    if (in->pcie_gpu_max_gen != GT_NA) gt_kv_add(s, "PCIe Link Speed (GPU, Max)", "Gen%dx%-2d", in->pcie_gpu_max_gen, in->pcie_gpu_max_width);
    if (in->pcie_sys_max_gen != GT_NA) gt_kv_add(s, "PCIe Link Speed (System, Max)", "Gen%dx%-2d", in->pcie_sys_max_gen, in->pcie_sys_max_width);
    if (st->fclk != GT_NA) gt_kv_add(s, "FCLK (DPM)", "%4dMHz (%4d-%4dMHz)", st->fclk, st->fclk_min, st->fclk_max);
    if (st->socclk != GT_NA) gt_kv_add(s, "SOCCLK (DPM)", "%4dMHz (%4d-%4dMHz)", st->socclk, st->socclk_min, st->socclk_max);
    free(st);
}

gt_dev *gt_open(int index, const gt_opts *opts, char *err, size_t errlen)
{
    gt_device list[GT_MAX_DEV];
    int n = gt_device_list(list, GT_MAX_DEV);
    gt_dev *d;
    char p[GT_PATH], real[PATH_MAX];
    if (err && errlen) err[0] = 0;
    if (index < 0 || index >= n) {
        if (err) snprintf(err, errlen, n ? "no GPU #%d (there %s %d)" : "no GPU found", index, n == 1 ? "is" : "are", n);
        return NULL;
    }
    if (!(d = calloc(1, sizeof *d))) { if (err) snprintf(err, errlen, "out of memory"); return NULL; }
    d->index = index;
    if (opts) d->opts = *opts;
    if (d->opts.pc_samples <= 0) d->opts.pc_samples = 100;
    d->info.dev = list[index];
    if (!realpath(gt_p(p, sizeof p, "%s", list[index].sysfs), real)) snprintf(real, sizeof real, "%s", p);
    snprintf(d->sys, sizeof d->sys, "%s", real);
    find_hwmon(d);
    find_ports(d);
    /* defaults: unknown */
    {
        gt_info *in = &d->info;
        int *na[] = { &in->is_apu, &in->cu, &in->se, &in->sa_per_se, &in->cu_per_sa, &in->rb, &in->rop, &in->min_gpu_clk,
            &in->max_gpu_clk, &in->max_od_gpu_clk, &in->min_mem_clk, &in->max_mem_clk, &in->max_od_mem_clk,
            &in->vram_bit_width, &in->resizable_bar, &in->ecc, &in->l1_kib_per_cu, &in->gl1_kib_per_sa, &in->l2_kib,
            &in->l2_banks, &in->l3_mib, &in->power_cap_w, &in->power_cap_min_w, &in->power_cap_max_w,
            &in->power_cap_default_w, &in->fan_max_rpm, &in->edge_crit, &in->edge_emerg, &in->junction_crit,
            &in->junction_emerg, &in->mem_crit, &in->mem_emerg, &in->pcie_dpm_min_gen, &in->pcie_dpm_min_width,
            &in->pcie_dpm_max_gen, &in->pcie_dpm_max_width, &in->pcie_gpu_max_gen, &in->pcie_gpu_max_width,
            &in->pcie_sys_max_gen, &in->pcie_sys_max_width, &in->has_vcn, &in->has_vcn_unified, &in->has_vpe,
            &in->has_npu, &in->smu_ip[0], &in->smu_ip[1], &in->smu_ip[2] };
        for (size_t k = 0; k < sizeof na / sizeof na[0]; k++) *na[k] = GT_NA;
        in->vram_total = in->vram_usable = in->vis_vram_total = in->gtt_total = in->gtt_usable = GT_NA;
        in->peak_fp32_gflops = in->peak_fp16_gflops = in->peak_fill_gpixs = in->peak_mem_bw_gbs = -1;
    }
    /* hwmon limits every vendor may have */
    if (d->hwmon[0]) {
        long long v;
        int c;
        hwmon_temp(d->hwmon, "edge", &c, &d->info.edge_crit, &d->info.edge_emerg);
        hwmon_temp(d->hwmon, "junction", &c, &d->info.junction_crit, &d->info.junction_emerg);
        hwmon_temp(d->hwmon, "mem", &c, &d->info.mem_crit, &d->info.mem_emerg);
        if (gt_readf_ll(&v, d->hwmon, "power1_cap") == 0) d->info.power_cap_w = (int)(v / 1000000);
        if (gt_readf_ll(&v, d->hwmon, "power1_cap_min") == 0) d->info.power_cap_min_w = (int)(v / 1000000);
        if (gt_readf_ll(&v, d->hwmon, "power1_cap_max") == 0) d->info.power_cap_max_w = (int)(v / 1000000);
        if (gt_readf_ll(&v, d->hwmon, "power1_cap_default") == 0) d->info.power_cap_default_w = (int)(v / 1000000);
        if (gt_readf_ll(&v, d->hwmon, "fan1_max") == 0) d->info.fan_max_rpm = (int)v;
    }
    if (d->info.dev.vendor == GT_VENDOR_AMD && !strcmp(d->info.dev.driver, "amdgpu")) {
        if (gt_amd_open(d, err, errlen)) { gt_close(d); return NULL; }
        pc_setup(d);
        add_sensor_section(d);
    } else if (d->info.dev.vendor == GT_VENDOR_NVIDIA) {
        /* NVML only for a real NVIDIA card (PCI vendor 0x10de): libnvidia-ml.so.1 on this kind of system may be
           ZLUDA's stand-in on an AMD card */
        gt_nv_open(d, err, errlen);   /* without NVML the sysfs data still shows */
        if (err) err[0] = 0;
    }
    if (!d->info.nsect) {   /* other vendors: what sysfs tells */
        gt_section *s = gt_sect_new(&d->info, "Device");
        gt_kv_add(s, "Device Name", "[%s]", d->info.dev.name);
        gt_kv_add(s, "PCI (domain:bus:dev.func)", "%s", d->info.dev.pci);
        gt_kv_add(s, "DeviceID.RevID", "0x%X.0x%X", d->info.dev.device_id, d->info.dev.revision_id);
        gt_kv_add(s, "Driver", "%s", d->info.dev.driver[0] ? d->info.dev.driver : "(none)");
        if (gt_link_read(d->sys, 1, &d->info.pcie_gpu_max_gen, &d->info.pcie_gpu_max_width) == 0)
            gt_kv_add(gt_sect_new(&d->info, "PCIe"), "PCIe Link Speed (GPU, Max)", "Gen%dx%d", d->info.pcie_gpu_max_gen, d->info.pcie_gpu_max_width);
    }
    d->st.seq = 0;
    return d;
}

void gt_close(gt_dev *d)
{
    if (!d) return;
    if (d->amd) gt_amd_close(d);
    if (d->nv) gt_nv_close(d);
    free(d);
}

const gt_info *gt_info_of(const gt_dev *d) { return &d->info; }

void gt_set_no_fdinfo(gt_dev *d, int on) { if (d) d->opts.no_fdinfo = on ? 1 : 0; }

int gt_finish(gt_dev *d, int elapsed_ms, gt_stat *out)
{
    gt_stat *s = &d->st;
    int pc_n = d->pc_n, fd_idle;
    gt_metrics m;
    s->seq = ++d->seq;
    s->t_ms = gt_now_ms();
    s->interval_ms = elapsed_ms;
    s->gfx = s->umc = s->media = GT_NA;
    s->sclk = s->mclk = s->vddgfx = s->vddnb = s->tctl = GT_NA;
    s->mem_errors = s->pcie_tx_kbs = s->pcie_rx_kbs = GT_NA;
    s->enc_util = s->dec_util = GT_NA;
    s->nthr = 0;
    /* GRBM: percent of the samples with the bit set */
    if (d->pc_try > 0 && pc_n == 0) d->pc_broken = 1;
    s->pc_ok = !d->opts.no_pc && d->amd && d->ngrbm > 0 && !d->pc_broken;
    s->ngrbm = d->ngrbm;
    s->ngrbm2 = d->ngrbm2;
    for (int i = 0; i < d->ngrbm; i++) {
        snprintf(s->grbm[i].name, sizeof s->grbm[i].name, "%s", d->grbm_name[i]);
        s->grbm[i].pct = pc_n ? d->acc1[d->grbm_bit[i]] * 100 / pc_n : 0;
    }
    for (int i = 0; i < d->ngrbm2; i++) {
        snprintf(s->grbm2[i].name, sizeof s->grbm2[i].name, "%s", d->grbm2_name[i]);
        s->grbm2[i].pct = pc_n ? d->acc2[d->grbm2_bit[i]] * 100 / pc_n : 0;
    }
    if (d->opts.no_fdinfo) {
        memset(&s->total, 0, sizeof s->total);
        s->nproc = 0;
        fd_idle = 0;          /* unknown without the walk: never drop to D3 on our own */
        d->to_d3 = 0;
        d->pc_skip = 0;
    } else {
        read_fdinfo(d, s, elapsed_ms);
        fd_idle = !s->total.gfx && !s->total.compute && !s->total.media && (s->total.vpe == GT_NA || !s->total.vpe) && !s->total.dma;
    }
    if (d->amd && !d->opts.no_fdinfo) {
        /* amdgpu_top app.rs update(): let an idle dGPU drop to D3hot (no reads that would wake it) */
        int was_idle = d->to_d3, sleepy = !d->info.is_apu && !d->opts.no_drop && can_sleep(d->sys);
        if (sleepy && !s->total.is_kfd && fd_idle && d->seq > 1 && d->prev_all_idle) d->to_d3 = 1;
        else if (was_idle && (!sleepy || !fd_idle || s->total.is_kfd)) d->to_d3 = 0;
        d->pc_skip = fd_idle && !s->total.is_kfd;   /* RDNA 4 shows ~2% GFX from sampling alone: amdgpu_top dynamic_no_pc */
    }
    s->idle_d3 = d->to_d3;
    if (d->amd) gt_amd_update(d, s);        /* memory always; ioctl sensors unless asleep */
    if (d->nv) gt_nv_update(d, s);
    read_sensors(d, s, d->to_d3);
    if (d->nv) gt_nv_sensors(d, s);
    if (d->to_d3) {
        memset(&s->metrics, 0, sizeof s->metrics);
    } else if (d->info.supports_gpu_metrics == 1 || (d->info.dev.vendor == GT_VENDOR_AMD && read_metrics(d->sys, &m) == 0)) {
        if (read_metrics(d->sys, &s->metrics)) memset(&s->metrics, 0, sizeof s->metrics);
        else s->metrics.gfx_act_centi = d->smc_fw_le_5d4600;   /* v3.0: how to read average_gfx_activity (gt_metric_shown) */
    } else {
        memset(&s->metrics, 0, sizeof s->metrics);
    }
    /* activity: gpu_metrics first, then *_busy_percent (not on Raven/Raven2: they always say 100) */
    if (!d->to_d3 && d->info.dev.vendor == GT_VENDOR_AMD) {
        if (s->metrics.present) {
            long long g = s->metrics.v[GT_M_AVERAGE_GFX_ACTIVITY], u = s->metrics.v[GT_M_AVERAGE_UMC_ACTIVITY],
                      me = s->metrics.v[GT_M_AVERAGE_MM_ACTIVITY];
            int div = s->metrics.format_rev == 2 ? 100 : 1;
            s->gfx = g == GT_NA ? GT_NA : (int)(g / div);
            s->umc = u == GT_NA ? GT_NA : (int)(u / div);
            s->media = me == GT_NA ? GT_NA : (int)(me / div);
            if (s->metrics.format_rev == 3 && s->metrics.content_rev == 0) {   /* Strix */
                if (d->smc_fw_le_5d4600 && s->gfx != GT_NA) s->gfx /= 100;
                if (s->media != GT_NA) s->media /= 100;
            }
        } else if (!d->asic_raven) {
            long long v;
            if (gt_readf_ll(&v, d->sys, "gpu_busy_percent") == 0) s->gfx = (int)v;
            if (gt_readf_ll(&v, d->sys, "mem_busy_percent") == 0) s->umc = (int)v;
            if (gt_readf_ll(&v, d->sys, "vcn_busy_percent") == 0) s->media = (int)v;
        }
        if (s->media == GT_NA || s->media == 0) s->media = s->total.media;
        /* SMU 14.0.2/3 have no fan1_input: the fan speed from gpu_metrics */
        if (s->fan_rpm == GT_NA && s->metrics.present && s->metrics.v[GT_M_CURRENT_FAN_SPEED] != GT_NA &&
            d->info.smu_ip[0] == 14 && d->info.smu_ip[1] == 0 && (d->info.smu_ip[2] == 2 || d->info.smu_ip[2] == 3))
            s->fan_rpm = (int)s->metrics.v[GT_M_CURRENT_FAN_SPEED];
        /* Granite Ridge (SMU 13.0.5, 0x13C0) reports ~0 W input power: use the socket power */
        if (d->info.smu_ip[0] == 13 && d->info.smu_ip[2] == 5 && d->info.dev.device_id == 0x13C0 && s->power_input == 0 &&
            s->metrics.v[GT_M_AVERAGE_SOCKET_POWER] != GT_NA) s->power_input = (double)s->metrics.v[GT_M_AVERAGE_SOCKET_POWER];
    } else if (d->info.dev.vendor != GT_VENDOR_AMD && d->info.dev.vendor != GT_VENDOR_NVIDIA) {
        long long v;
        if (gt_readf_ll(&v, d->sys, "gpu_busy_percent") == 0) s->gfx = (int)v;
    }
    d->prev_all_idle = (s->gfx == GT_NA || s->gfx == 0) && (s->umc == GT_NA || s->umc == 0) && (s->media == GT_NA || s->media == 0);
    if (s->metrics.nthr && !s->nthr) {
        s->nthr = s->metrics.nthr;
        for (int i = 0; i < s->nthr; i++) { memcpy(s->thr[i], s->metrics.thr[i], sizeof s->metrics.thr[i]); s->thr[i][sizeof s->metrics.thr[i] - 1] = 0; }
    }
    /* clock levels */
    {
        static const char *f[] = { "pp_dpm_sclk", "pp_dpm_mclk", "pp_dpm_fclk", "pp_dpm_socclk", "pp_dpm_pcie" };
        s->ndpm = 0;
        if (!d->to_d3)
            for (int i = 0; i < 5; i++) if (gt_dpm_read(d->sys, f[i], &s->dpm[s->ndpm]) == 0) s->ndpm++;
        /* no sensor ioctl (tests, other vendors): the current DPM level */
        for (int i = 0; i < s->ndpm; i++) {
            if (s->dpm[i].cur < 0) continue;
            if (s->sclk == GT_NA && !strcmp(s->dpm[i].name, "sclk")) s->sclk = s->dpm[i].mhz[s->dpm[i].cur];
            if (s->mclk == GT_NA && !strcmp(s->dpm[i].name, "mclk")) s->mclk = s->dpm[i].mhz[s->dpm[i].cur];
        }
    }
    s->vram_total = s->vram_total ? s->vram_total : d->info.vram_total;
    /* no back end said how much memory there is (other vendors, NVIDIA without NVML): unknown, not "0 of -1 MiB" */
    if (s->vram_total <= 0) s->vram_total = s->vram_used = s->vis_vram_used = s->vis_vram_total = s->vram_usable = GT_NA;
    if (s->gtt_total <= 0) s->gtt_total = s->gtt_used = s->gtt_usable = GT_NA;
    if (out) memcpy(out, s, sizeof *s);
    return 0;
}

int gt_update(gt_dev *d, int interval_ms, gt_stat *out)
{
    long long t0 = gt_now_ms();
    int n = d->opts.pc_samples > 0 ? d->opts.pc_samples : 100;
    if (interval_ms < 0) interval_ms = 0;
    gt_pc_begin(d);
    if (!d->opts.no_pc && d->amd && !d->pc_skip && !d->to_d3 && !d->pc_broken && interval_ms > 0) {
        int step = interval_ms / n;
        for (int i = 0; i < n; i++) { gt_pc_read(d); sleep_ms(step); }
    } else {
        sleep_ms(interval_ms);
    }
    return gt_finish(d, interval_ms ? (int)(gt_now_ms() - t0) : 0, out);
}

int gt_capture_info(gt_dev *d, const char *file) { return d->amd ? gt_amd_capture(d, file) : -1; }

/* ------------------------------------------------------------------ extra pages */

int gt_drm_info_text(gt_dev *d, char *buf, size_t len)
{
    if (!d->amd) { gt_cat(buf, len, "DRM info: shown for AMD GPUs only\n"); return -1; }
    return gt_amd_drm_info(d, buf, len, NULL);
}

int gt_pp_table_text(gt_dev *d, char *buf, size_t len)
{
    if (!d->amd) { gt_cat(buf, len, "pp_table: not available on this card (AMD Radeon RX 5000 / 6000 / 7000 only)\n"); return -1; }
    return gt_amd_pp_table(d, buf, len);
}

/* XDNA NPU (amdxdna): what sysfs tells about /sys/class/accel */
int gt_xdna_text(char *buf, size_t len)
{
    char base[GT_PATH];
    DIR *dir = opendir(gt_p(base, sizeof base, "/sys/class/accel"));
    struct dirent *e;
    int n = 0;
    while (dir && (e = readdir(dir))) {
        char dev[GT_PATH], b[128], l[GT_PATH], real[PATH_MAX];
        long long v;
        ssize_t r;
        if (strncmp(e->d_name, "accel", 5)) continue;
        snprintf(dev, sizeof dev, "%s/%s/device", base, e->d_name);
        snprintf(l, sizeof l, "%s/driver", dev);
        if ((r = readlink(l, b, sizeof b - 1)) > 0) b[r] = 0; else gt_readf(b, sizeof b, dev, "srs-driver");
        gt_cat(buf, len, "%s/dev/%s\n", n ? "\n" : "", e->d_name);
        gt_cat(buf, len, "    driver     : %s\n", strrchr(b, '/') ? strrchr(b, '/') + 1 : b[0] ? b : "?");
        if (realpath(dev, real)) gt_cat(buf, len, "    pci        : %s\n", strrchr(real, '/') + 1);
        if (gt_readf_ll(&v, dev, "vendor") == 0) {
            long long dv = 0, rv = 0;
            gt_readf_ll(&dv, dev, "device");
            gt_readf_ll(&rv, dev, "revision");
            gt_cat(buf, len, "    ids        : %04llx:%04llx rev %02llx\n", v, dv, rv);
        }
        if (gt_readf(b, sizeof b, dev, "fw_version") == 0) gt_cat(buf, len, "    FW version : %s\n", b);
        if (gt_readf(b, sizeof b, dev, "power_state") == 0) gt_cat(buf, len, "    power state: %s\n", b);
        if (gt_readf(b, sizeof b, dev, "power/runtime_status") == 0) gt_cat(buf, len, "    runtime    : %s\n", b);
        n++;
    }
    if (dir) closedir(dir);
    if (!n) { gt_cat(buf, len, "There are no XDNA NPU devices (no /sys/class/accel).\n"); return -1; }
    return 0;
}

/* ------------------------------------------------------------------ JSON (amdgpu_top -J shapes) */

static void json_vram(gt_json *j, const gt_stat *s)
{
    gj_obj(j, "VRAM");
    gj_vu(j, "Total VRAM", s->vram_total >> 20, "MiB");
    gj_vu(j, "Total VRAM Usage", s->vram_used >> 20, "MiB");
    gj_vu(j, "Total GTT", s->gtt_total >> 20, "MiB");
    gj_vu(j, "Total GTT Usage", s->gtt_used >> 20, "MiB");
    gj_end(j, '}');
}

static void json_sensors(gt_json *j, const gt_dev *d, const gt_stat *s)
{
    const gt_info *in = &d->info;
    double any = s->power_avg >= 0 ? s->power_avg : s->power_input;
    gj_obj(j, "Sensors");
    gj_vu(j, "GFX_SCLK", s->sclk, "MHz");
    gj_vu(j, "GFX_MCLK", s->mclk, "MHz");
    gj_vu(j, "FCLK", s->fclk, "MHz");
    gj_vu(j, "VDDNB", s->vddnb, "mV");
    gj_vu(j, "VDDGFX", s->vddgfx, "mV");
    gj_vu(j, "Fan", s->fan_rpm, "RPM");
    gj_vu(j, "Fan Max", s->fan_max_rpm, "RPM");
    gj_vu(j, "GFX Power", any >= 0 ? (long long)any : GT_NA, "W");
    gj_vu(j, "Average Power", s->power_avg >= 0 ? (long long)s->power_avg : GT_NA, "W");
    gj_vu(j, "Input Power", s->power_input >= 0 ? (long long)s->power_input : GT_NA, "W");
    {
        struct { const char *l; int c, cr, em; } T[3] = { { "Edge", s->edge, in->edge_crit, in->edge_emerg },
            { "Junction", s->junction, in->junction_crit, in->junction_emerg }, { "Memory", s->memtemp, in->mem_crit, in->mem_emerg } };
        for (int i = 0; i < 3; i++) {
            char k[48];
            snprintf(k, sizeof k, "%s Temperature", T[i].l); gj_vu(j, k, T[i].c, "C");
            snprintf(k, sizeof k, "%s Critical Temperature", T[i].l); gj_vu(j, k, T[i].c == GT_NA ? GT_NA : T[i].cr, "C");
            snprintf(k, sizeof k, "%s Emergency Temperature", T[i].l); gj_vu(j, k, T[i].c == GT_NA ? GT_NA : T[i].em, "C");
        }
    }
    gj_vu(j, "CPU Tctl", s->tctl, "C");
    gj_arr(j, "CPU Core freq");
    gj_end(j, ']');
    if (s->pcie_gen == GT_NA) gj_null(j, "PCIe Link Speed");
    else { gj_obj(j, "PCIe Link Speed"); gj_int(j, "gen", s->pcie_gen); gj_int(j, "width", s->pcie_width); gj_end(j, '}'); }
    gj_str(j, "PCI Power State", s->pci_power_state[0] ? s->pci_power_state : NULL);
    gj_str(j, "Power Profile", s->power_profile[0] ? s->power_profile : NULL);
    gj_end(j, '}');
}

static void json_usage(gt_json *j, const char *key, const gt_proc *p, const gt_info *in)
{
    int uni = in->has_vcn_unified > 0, vcn = in->has_vcn > 0, vpe = in->has_vpe > 0;
    gj_obj(j, key);
    gj_vu(j, "VRAM", p->vram_kib >> 10, "MiB");
    gj_vu(j, "GTT", p->gtt_kib >> 10, "MiB");
    gj_vu(j, "CPU", p->cpu, "%");
    gj_vu(j, "GFX", p->gfx, "%");
    gj_vu(j, "Compute", p->compute, "%");
    gj_vu(j, "DMA", p->dma, "%");
    gj_vu(j, "Decode", uni ? GT_NA : p->dec, "%");
    gj_vu(j, "Encode", uni ? GT_NA : p->enc, "%");
    gj_vu(j, "Media", p->media, "%");
    gj_vu(j, "VCN_JPEG", vcn ? p->vcn_jpeg : GT_NA, "%");
    gj_vu(j, "VPE", vpe ? p->vpe : GT_NA, "%");
    gj_vu(j, "VCN_Unified", uni ? p->vcn_unified : GT_NA, "%");
    gj_end(j, '}');
}

static void json_activity(gt_json *j, const gt_stat *s)
{
    gj_obj(j, "gpu_activity");
    gj_vu(j, "GFX", s->gfx, "%");
    gj_vu(j, "Memory", s->umc, "%");
    gj_vu(j, "MediaEngine", s->media, "%");
    gj_end(j, '}');
}

static void json_npu(gt_json *j, const gt_metrics *m)
{
    if (!m->present || m->format_rev != 3) { gj_null(j, "npu_metrics"); return; }
    gj_obj(j, "npu_metrics");
    gj_vu(j, "npuclk_freq", m->npuclk, "MHz");
    gj_vu(j, "npu_power", m->npu_power, "mW");
    gj_vu(j, "mpnpuclk_freq", m->mpnpuclk, "MHz");
    gj_vu(j, "npu_reads", m->npu_reads, "MB/s");
    gj_vu(j, "npu_writes", m->npu_writes, "MB/s");
    gj_obj(j, "npu_busy");
    gj_arr(j, "value");
    for (int i = 0; i < m->n_npu_busy; i++) gj_int(j, NULL, m->npu_busy[i]);
    gj_end(j, ']');
    gj_str(j, "unit", "%");
    gj_end(j, '}');
    gj_end(j, '}');
}

static void json_metrics_member(gt_json *j, const gt_metrics *m)
{
    char *tmp;
    if (!m->present) { gj_null(j, "gpu_metrics"); return; }
    if (!(tmp = malloc(16384))) { gj_null(j, "gpu_metrics"); return; }
    tmp[0] = 0;
    gt_json_metrics(m, tmp, 16384);
    gj_raw(j, "gpu_metrics", tmp);
    free(tmp);
}

static void json_info_min(gt_json *j, gt_dev *d)
{
    const gt_info *in = &d->info;
    gj_str(j, "DeviceName", in->dev.name);
    gj_obj(j, "DevicePath");
    gj_str(j, "render", in->dev.render); gj_str(j, "card", in->dev.card); gj_str(j, "pci", in->dev.pci);
    gj_int(j, "DeviceID", in->dev.device_id); gj_int(j, "RevisionID", in->dev.revision_id); gj_str(j, "DeviceName", in->dev.name);
    gj_end(j, '}');
    gj_str(j, "PCI", in->dev.pci);
    gj_int(j, "DeviceID", in->dev.device_id);
    gj_int(j, "RevisionID", in->dev.revision_id);
    gj_str(j, "Vendor", gt_vendor_name(in->dev.vendor));
    gj_str(j, "Driver", in->dev.driver);
    if (in->driver_version[0]) gj_str(j, "Driver Version", in->driver_version);
    if (in->vram_total != GT_NA) gj_int(j, "VRAM Size", in->vram_total);
    if (in->max_gpu_clk != GT_NA) { gj_obj(j, "GPU Clock"); gj_int(j, "min", in->min_gpu_clk); gj_int(j, "max", in->max_gpu_clk); gj_end(j, '}'); }
    if (in->power_cap_w != GT_NA) {
        gj_obj(j, "Power Cap"); gj_int(j, "current", in->power_cap_w); gj_int(j, "min", in->power_cap_min_w);
        gj_int(j, "max", in->power_cap_max_w); gj_end(j, '}');
    }
}

int gt_json_dump(gt_dev *d, const gt_stat *s, char *buf, size_t len)
{
    gt_json j;
    gj_init(&j, buf, len);
    gj_obj(&j, NULL);
    gj_obj(&j, "amdgpu_top_version");
    gj_int(&j, "major", 0); gj_int(&j, "minor", 11); gj_int(&j, "patch", 5);
    gj_end(&j, '}');
    if (d->amd) gt_amd_json_info(d, &j); else json_info_min(&j, d);
    json_vram(&j, s);
    if (d->hwmon[0] || d->nv) json_sensors(&j, d, s); else gj_null(&j, "Sensors");
    json_metrics_member(&j, &s->metrics);
    json_activity(&j, s);
    json_npu(&j, &s->metrics);
    gj_end(&j, '}');
    return j.len + 1 >= len ? -1 : 0;
}

int gt_json_device(gt_dev *d, const gt_stat *s, int no_pc, char *buf, size_t len)
{
    gt_json j;
    char pid[16];
    gj_init(&j, buf, len);
    gj_obj(&j, NULL);
    gj_obj(&j, "Info");
    if (d->amd) gt_amd_json_info(d, &j); else json_info_min(&j, d);
    gj_end(&j, '}');
    for (int g = 0; g < 2; g++) {
        const gt_pc *pc = g ? s->grbm2 : s->grbm;
        int n = g ? s->ngrbm2 : s->ngrbm;
        if (no_pc || !s->pc_ok) { gj_null(&j, g ? "GRBM2" : "GRBM"); continue; }
        gj_obj(&j, g ? "GRBM2" : "GRBM");
        for (int i = 0; i < n; i++) gj_vu(&j, pc[i].name, pc[i].pct, "%");
        gj_end(&j, '}');
    }
    json_vram(&j, s);
    if (d->hwmon[0] || d->nv) json_sensors(&j, d, s); else gj_null(&j, "Sensors");
    gj_obj(&j, "fdinfo");
    for (int i = 0; i < s->nproc; i++) {
        snprintf(pid, sizeof pid, "%d", s->proc[i].pid);
        gj_obj(&j, pid);
        gj_str(&j, "name", s->proc[i].name);
        json_usage(&j, "usage", &s->proc[i], &d->info);
        gj_end(&j, '}');
    }
    gj_end(&j, '}');
    gj_obj(&j, "xdna_fdinfo");
    gj_end(&j, '}');
    json_usage(&j, "Total fdinfo", &s->total, &d->info);
    json_metrics_member(&j, &s->metrics);
    json_activity(&j, s);
    json_npu(&j, &s->metrics);
    gj_end(&j, '}');
    return j.len + 1 >= len ? -1 : 0;
}
