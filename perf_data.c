/* perf_data.c - the sampler behind the Performance tab of the GPU Top window (see perf_data.h).
 *
 * Sources (all readable by a normal user): /proc/stat, /proc/meminfo, /proc/pressure/{cpu,memory,io},
 * /proc/diskstats, /proc/net/dev, /proc/uptime, /proc/loadavg, /proc/<pid>/{stat,statm,io,cmdline,
 * cgroup,environ}, /sys/devices/system/cpu (cpufreq, topology), /sys/class/hwmon (k10temp / coretemp /
 * zenpower, NVMe and drivetemp), /sys/block, /sys/class/net; CPU package power through power_src.c
 * (zenergy / amd_energy / readable powercap, the same sources as the Power & Cost page); GPUs through
 * gpu_top.h with GRBM performance counters off. /proc/<pid>/io of other users' processes is root-only:
 * those rows say so (rd < 0) and the file is not tried again for that process.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "perf_data.h"
#include "power_src.h"

#include <glib.h>

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <math.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ rings */

void pd_ring_push(pd_ring *r, double v)
{
    r->v[r->head] = (float)v;
    r->head = (r->head + 1) % PD_HIST;
    if (r->n < PD_HIST) r->n++;
}

double pd_ring_at(const pd_ring *r, int i)
{
    if (i < 0 || i >= r->n) return NAN;
    return r->v[(r->head - r->n + i + PD_HIST) % PD_HIST];
}

double pd_ring_last(const pd_ring *r) { return r->n ? pd_ring_at(r, r->n - 1) : NAN; }

double pd_ring_max(const pd_ring *r)
{
    double m = NAN;
    for (int i = 0; i < r->n; i++) {
        double v = pd_ring_at(r, i);
        if (!isnan(v) && (isnan(m) || v > m)) m = v;
    }
    return m;
}

/* ------------------------------------------------------------------ files under the (test) root */

static char g_root[512];
static int g_root_set;

void pd_set_root(const char *root)
{
    if (root) { snprintf(g_root, sizeof g_root, "%s", root); g_root_set = 1; }
    else g_root_set = 0;
}

const char *pd_root(void)
{
    if (!g_root_set) {
        const char *e = getenv("SRS_TEST_PROC_ROOT");
        snprintf(g_root, sizeof g_root, "%s", e ? e : "");
        g_root_set = 1;
    }
    return g_root;
}

static int testing(void) { return pd_root()[0] != 0; }

static void pth(char *out, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void pth(char *out, size_t n, const char *fmt, ...)
{
    va_list ap;
    size_t l;
    snprintf(out, n, "%s", pd_root());
    l = strlen(out);
    if (l >= n) return;
    va_start(ap, fmt);
    vsnprintf(out + l, n - l, fmt, ap);
    va_end(ap);
}

/* whole small file into buf (NUL-terminated); returns bytes, -1 on error (errno kept) */
static int rd_all(const char *path, char *buf, int n)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC), got = 0;
    if (fd < 0) return -1;
    while (got < n - 1) {
        ssize_t r = read(fd, buf + got, (size_t)(n - 1 - got));
        if (r < 0) { got = got ? got : -1; break; }
        if (r == 0) break;
        got += (int)r;
    }
    close(fd);
    if (got >= 0) buf[got] = 0;
    else buf[0] = 0;
    return got;
}

static int rd_line(const char *path, char *buf, int n)
{
    int l = rd_all(path, buf, n);
    if (l < 0) return 0;
    while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == ' ')) buf[--l] = 0;
    return 1;
}

static int rd_ll(const char *path, long long *v)
{
    char b[64], *e;
    if (!rd_line(path, b, sizeof b) || !b[0]) return 0;
    *v = strtoll(b, &e, 10);
    return e != b;
}

static int exists(const char *path) { return access(path, F_OK) == 0; }

/* ------------------------------------------------------------------ the sampler */

typedef struct {
    int pid;
    unsigned long long start;
    unsigned gen;
    char name[48], user[32], cmd[240], unit[64], comm[64];
    int uid, is_app, kthread, session, io_denied;
    unsigned long long ticks;      /* utime + stime at the previous sample */
    long long rd, wr;              /* read_bytes / write_bytes at the previous sample, -1 */
    int seen;                      /* samples since first seen */
} pcache;

typedef struct {
    gt_dev *d;
    gt_stat *st;
    int index;
} gslot;

struct pd_sampler {
    pd_snap s;
    int with_gpu, no_procs;
    double t_prev;
    unsigned gen;
    long hz;
    long page;
    /* /proc/stat per CPU (index = order in /proc/stat) */
    unsigned long long *cprev;     /* PD_MAX_CPU+1 rows x 8 fields; row 0 = "cpu" total */
    int have_cprev;
    /* CPU temperature, power */
    char ctemp_path[300];
    psrc *ps;
    int ps_ok;
    /* hwmon drive temperatures */
    int ndtemp;
    char dtemp_path[PD_MAX_TEMP][300];
    char dtemp_label[PD_MAX_TEMP][40];
    /* disk / net previous counters by slot */
    unsigned long long dprev[PD_MAX_DISK][6];   /* rd sectors, wr sectors, io ms, rd ios, wr ios, rw ms */
    int dhave[PD_MAX_DISK];
    unsigned long long nprev[PD_MAX_NET][2];
    int nhave[PD_MAX_NET];
    int rescan;
    /* GPUs */
    gslot gs[PD_MAX_GPU];
    /* processes */
    GHashTable *pc;                /* pid -> pcache */
    GHashTable *gpu_pid;           /* pid -> packed gpu% / vram */
    GHashTable *apps;              /* program basenames of visible desktop entries */
    GHashTable *users;             /* uid -> name */
    int apps_loaded;
    char cpu_list[PD_MAX_CPU];
};

/* ---------------------------------------------------------------- static CPU facts */

static void cpu_static(pd_sampler *s)
{
    char p[400], b[4096];
    FILE *f;
    int pk[PD_MAX_CPU], co[PD_MAX_CPU], n = 0;
    long long v;
    pd_snap *o = &s->s;
    pth(p, sizeof p, "/proc/cpuinfo");
    if ((f = fopen(p, "r"))) {
        while (fgets(b, sizeof b, f)) {
            if (!o->cpu_model[0] && !strncmp(b, "model name", 10) && strchr(b, ':')) {
                char *m = strchr(b, ':') + 1;
                while (*m == ' ' || *m == '\t') m++;
                m[strcspn(m, "\n")] = 0;
                snprintf(o->cpu_model, sizeof o->cpu_model, "%s", m);
            }
            if (!o->virt && !strncmp(b, "flags", 5) && (strstr(b, " svm") || strstr(b, " vmx"))) o->virt = 1;
        }
        fclose(f);
    }
    o->sockets = o->cores = 0;
    for (int c = 0; c < PD_MAX_CPU; c++) {
        long long a, bb;
        pth(p, sizeof p, "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", c);
        if (!rd_ll(p, &a)) continue;
        pth(p, sizeof p, "/sys/devices/system/cpu/cpu%d/topology/core_id", c);
        if (!rd_ll(p, &bb)) bb = c;
        int dup = 0, newpk = 1;
        for (int i = 0; i < n; i++) {
            if (pk[i] == a && co[i] == bb) dup = 1;
            if (pk[i] == a) newpk = 0;
        }
        if (newpk) o->sockets++;
        if (!dup && n < PD_MAX_CPU) { pk[n] = (int)a; co[n] = (int)bb; n++; }
    }
    o->cores = n;
    pth(p, sizeof p, "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    if (!rd_line(p, o->governor, sizeof o->governor)) o->governor[0] = 0;
    pth(p, sizeof p, "/sys/devices/system/cpu/cpu0/cpufreq/scaling_driver");
    if (!rd_line(p, o->freq_driver, sizeof o->freq_driver)) o->freq_driver[0] = 0;
    o->max_mhz = o->base_mhz = 0;
    pth(p, sizeof p, "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
    if (rd_ll(p, &v)) o->max_mhz = v / 1000.0;
    pth(p, sizeof p, "/sys/devices/system/cpu/cpu0/cpufreq/base_frequency");          /* intel_pstate */
    if (rd_ll(p, &v)) o->base_mhz = v / 1000.0;
    else {
        pth(p, sizeof p, "/sys/devices/system/cpu/cpu0/cpufreq/amd_pstate_nominal_freq");   /* newer amd-pstate */
        if (rd_ll(p, &v)) o->base_mhz = v / 1000.0;
    }
}

/* hwmon: CPU temperature (k10temp Tctl, zenpower Tctl/Tdie, coretemp Package id 0) and drive temperatures */
static void hwmon_scan(pd_sampler *s)
{
    char p[400], nm[64], lab[64];
    DIR *d;
    struct dirent *e;
    int prio = 0;
    s->ctemp_path[0] = 0;
    s->ndtemp = 0;
    pth(p, sizeof p, "/sys/class/hwmon");
    if (!(d = opendir(p))) return;
    while ((e = readdir(d))) {
        int pr = 0;
        if (e->d_name[0] == '.') continue;
        pth(p, sizeof p, "/sys/class/hwmon/%s/name", e->d_name);
        if (!rd_line(p, nm, sizeof nm)) continue;
        if (!strcmp(nm, "k10temp") || !strcmp(nm, "zenpower")) pr = 3;
        else if (!strcmp(nm, "coretemp")) pr = 2;
        else if (!strcmp(nm, "cpu_thermal") || !strcmp(nm, "acpitz")) pr = 1;
        if (pr > prio) {
            int found = 0;
            for (int i = 1; i <= 16 && !found; i++) {
                pth(p, sizeof p, "/sys/class/hwmon/%s/temp%d_label", e->d_name, i);
                if (!rd_line(p, lab, sizeof lab)) continue;
                if (!strcmp(lab, "Tctl") || !strcmp(lab, "Tdie") || !strncmp(lab, "Package id", 10)) {
                    pth(s->ctemp_path, sizeof s->ctemp_path, "/sys/class/hwmon/%s/temp%d_input", e->d_name, i);
                    snprintf(s->s.cpu_temp_src, sizeof s->s.cpu_temp_src, "%s %s", nm, lab);
                    found = 1;
                }
            }
            if (!found) {
                pth(s->ctemp_path, sizeof s->ctemp_path, "/sys/class/hwmon/%s/temp1_input", e->d_name);
                if (!exists(s->ctemp_path)) { s->ctemp_path[0] = 0; continue; }
                snprintf(s->s.cpu_temp_src, sizeof s->s.cpu_temp_src, "%s temp1", nm);
            }
            prio = pr;
        }
        if ((!strcmp(nm, "nvme") || !strcmp(nm, "drivetemp")) && s->ndtemp < PD_MAX_TEMP - 4) {
            char link[400], dev[200];
            ssize_t l;
            const char *b;
            pth(p, sizeof p, "/sys/class/hwmon/%s/temp1_input", e->d_name);
            if (!exists(p)) continue;
            snprintf(s->dtemp_path[s->ndtemp], sizeof s->dtemp_path[0], "%s", p);
            pth(link, sizeof link, "/sys/class/hwmon/%s/device", e->d_name);
            l = readlink(link, dev, sizeof dev - 1);
            if (l > 0) { dev[l] = 0; b = strrchr(dev, '/') ? strrchr(dev, '/') + 1 : dev; }
            else b = e->d_name;
            if (!strcmp(nm, "drivetemp")) {   /* the SCSI address -> the disk's name (sda) */
                DIR *bd;
                struct dirent *be;
                pth(link, sizeof link, "/sys/class/hwmon/%s/device/block", e->d_name);
                if ((bd = opendir(link))) {
                    while ((be = readdir(bd))) if (be->d_name[0] != '.') { snprintf(dev, sizeof dev, "%s", be->d_name); b = dev; break; }
                    closedir(bd);
                }
            }
            if (!strcmp(nm, "nvme")) snprintf(s->dtemp_label[s->ndtemp], sizeof s->dtemp_label[0], "%.24s Composite", b);
            else snprintf(s->dtemp_label[s->ndtemp], sizeof s->dtemp_label[0], "%.24s", b);
            s->ndtemp++;
        }
    }
    closedir(d);
}

/* ---------------------------------------------------------------- disks and adapters */

static int skip_disk(const char *n)
{
    static const char *const pre[] = { "loop", "ram", "zram", "dm-", "nbd", "fd", "sr", "md", NULL };
    for (int i = 0; pre[i]; i++) if (!strncmp(n, pre[i], strlen(pre[i]))) return 1;
    return 0;
}

static void disk_scan(pd_sampler *s)
{
    pd_snap *o = &s->s;
    pd_disk old[PD_MAX_DISK];
    unsigned long long oprev[PD_MAX_DISK][6];
    int ohave[PD_MAX_DISK], on = o->ndisk;
    char p[400], b[128];
    DIR *d;
    struct dirent *e;
    char names[64][24];
    int nn = 0;
    memcpy(old, o->disk, sizeof old);
    memcpy(oprev, s->dprev, sizeof oprev);
    memcpy(ohave, s->dhave, sizeof ohave);
    pth(p, sizeof p, "/sys/block");
    if ((d = opendir(p))) {
        while ((e = readdir(d)) && nn < 64) {
            if (e->d_name[0] == '.' || skip_disk(e->d_name)) continue;
            snprintf(names[nn++], sizeof names[0], "%s", e->d_name);
        }
        closedir(d);
    }
    qsort(names, (size_t)nn, sizeof names[0], (int (*)(const void *, const void *))strcmp);
    o->ndisk = 0;
    for (int i = 0; i < nn && o->ndisk < PD_MAX_DISK; i++) {
        pd_disk *k = &o->disk[o->ndisk];
        long long v;
        int j, slot = o->ndisk;
        for (j = 0; j < on; j++) if (!strcmp(old[j].name, names[i])) break;
        if (j < on) {
            *k = old[j];
            memcpy(s->dprev[slot], oprev[j], sizeof s->dprev[0]);
            s->dhave[slot] = ohave[j];
        } else {
            memset(k, 0, sizeof *k);
            s->dhave[slot] = 0;
            snprintf(k->name, sizeof k->name, "%s", names[i]);
        }
        pth(p, sizeof p, "/sys/block/%s/device/model", names[i]);
        if (rd_line(p, b, sizeof b)) {
            char *t = b;
            while (*t == ' ') t++;
            snprintf(k->model, sizeof k->model, "%s", t);
        }
        pth(p, sizeof p, "/sys/block/%s/size", names[i]);
        k->size = rd_ll(p, &v) ? v * 512 : 0;
        if (k->size == 0) continue;   /* empty card readers */
        pth(p, sizeof p, "/sys/block/%s/queue/rotational", names[i]);
        k->rotational = rd_ll(p, &v) ? (int)v : 0;
        pth(p, sizeof p, "/sys/block/%s/removable", names[i]);
        k->removable = rd_ll(p, &v) ? (int)v : 0;
        o->ndisk++;
    }
}

static void net_scan(pd_sampler *s)
{
    pd_snap *o = &s->s;
    pd_net old[PD_MAX_NET];
    unsigned long long oprev[PD_MAX_NET][2];
    int ohave[PD_MAX_NET], on = o->nnet;
    char p[400], b[256], names[64][32];
    int nn = 0;
    FILE *f;
    memcpy(old, o->net, sizeof old);
    memcpy(oprev, s->nprev, sizeof oprev);
    memcpy(ohave, s->nhave, sizeof ohave);
    pth(p, sizeof p, "/proc/net/dev");
    if ((f = fopen(p, "r"))) {
        while (fgets(b, sizeof b, f) && nn < 64) {
            char *c = strchr(b, ':'), *t = b;
            if (!c) continue;
            *c = 0;
            while (*t == ' ') t++;
            if (!strcmp(t, "lo")) continue;
            snprintf(names[nn++], sizeof names[0], "%s", t);
        }
        fclose(f);
    }
    o->nnet = 0;
    for (int i = 0; i < nn && o->nnet < PD_MAX_NET; i++) {
        const char *n = names[i];
        int phys, up, slot = o->nnet, j;
        long long v;
        pd_net *k = &o->net[slot];
        pth(p, sizeof p, "/sys/class/net/%s/device", n);
        phys = exists(p);
        pth(p, sizeof p, "/sys/class/net/%s/operstate", n);
        up = rd_line(p, b, sizeof b) && (!strcmp(b, "up") || !strcmp(b, "unknown"));
        if (!phys && (!up || !strncmp(n, "veth", 4) || !strncmp(n, "docker", 6) || !strncmp(n, "br-", 3) ||
                      !strncmp(n, "virbr", 5) || !strncmp(n, "vnet", 4))) continue;
        for (j = 0; j < on; j++) if (!strcmp(old[j].name, n)) break;
        if (j < on) {
            *k = old[j];
            memcpy(s->nprev[slot], oprev[j], sizeof s->nprev[0]);
            s->nhave[slot] = ohave[j];
        } else {
            memset(k, 0, sizeof *k);
            s->nhave[slot] = 0;
            snprintf(k->name, sizeof k->name, "%s", n);
        }
        k->up = up;
        pth(p, sizeof p, "/sys/class/net/%s/wireless", n);
        if (exists(p)) snprintf(k->kind, sizeof k->kind, "Wi-Fi");
        else {
            pth(p, sizeof p, "/sys/class/net/%s/type", n);
            v = 0;
            rd_ll(p, &v);
            snprintf(k->kind, sizeof k->kind, "%s", v == 65534 ? "Tunnel" : phys ? "Ethernet" : "Virtual");
        }
        pth(p, sizeof p, "/sys/class/net/%s/speed", n);
        k->speed_mbps = rd_ll(p, &v) && v > 0 ? (int)v : -1;
        pth(p, sizeof p, "/sys/class/net/%s/address", n);
        if (!rd_line(p, k->mac, sizeof k->mac)) k->mac[0] = 0;
        {
            char link[400], tgt[300];
            ssize_t l;
            pth(link, sizeof link, "/sys/class/net/%s/device/driver", n);
            l = readlink(link, tgt, sizeof tgt - 1);
            if (l > 0) { tgt[l] = 0; snprintf(k->driver, sizeof k->driver, "%s", strrchr(tgt, '/') ? strrchr(tgt, '/') + 1 : tgt); }
            else k->driver[0] = 0;
        }
        k->ipv4[0] = k->ipv6[0] = 0;
        o->nnet++;
    }
    if (!testing()) {
        struct ifaddrs *ifa, *a;
        if (getifaddrs(&ifa) == 0) {
            for (a = ifa; a; a = a->ifa_next) {
                if (!a->ifa_addr || !a->ifa_name) continue;
                for (int i = 0; i < o->nnet; i++) {
                    pd_net *k = &o->net[i];
                    if (strcmp(k->name, a->ifa_name)) continue;
                    if (a->ifa_addr->sa_family == AF_INET && !k->ipv4[0])
                        inet_ntop(AF_INET, &((struct sockaddr_in *)(void *)a->ifa_addr)->sin_addr, k->ipv4, sizeof k->ipv4);
                    else if (a->ifa_addr->sa_family == AF_INET6 && !k->ipv6[0]) {
                        const struct in6_addr *ad = &((struct sockaddr_in6 *)(void *)a->ifa_addr)->sin6_addr;
                        if (!IN6_IS_ADDR_LINKLOCAL(ad) || !k->ipv6[0]) inet_ntop(AF_INET6, ad, k->ipv6, sizeof k->ipv6);
                    }
                }
            }
            freeifaddrs(ifa);
        }
    }
}

/* ---------------------------------------------------------------- GPUs (gpu_top.h, GRBM off) */

static void gpu_scan(pd_sampler *s)
{
    gt_device list[GT_MAX_DEV];
    int n = gt_device_list(list, GT_MAX_DEV);
    pd_snap *o = &s->s;
    if (n > PD_MAX_GPU) n = PD_MAX_GPU;
    for (int i = 0; i < n; i++) {
        pd_gpu *g = &o->gpu[i];
        gslot *gs = &s->gs[i];
        if (i >= o->ngpu) { memset(g, 0, sizeof *g); gs->index = i; }
        g->dev = list[i];
        if (gs->d) continue;
        /* a runtime-suspended card is not woken just to be shown here (the GPU tab opens it when picked) */
        if (!list[i].active && !getenv("SRS_TEST_GPU_ROOT")) {
            g->open = 0;
            snprintf(g->why, sizeof g->why, "Suspended (not woken)");
            continue;
        }
        {
            gt_opts op;
            char err[160] = "";
            memset(&op, 0, sizeof op);
            op.no_pc = 1;   /* never GRBM register reads from this view */
            op.no_fdinfo = s->no_procs;
            gs->d = gt_open(i, &op, err, sizeof err);
            if (!gs->d) { g->open = 0; snprintf(g->why, sizeof g->why, "%s", err[0] ? err : "Cannot be opened"); continue; }
            if (!gs->st) gs->st = calloc(1, sizeof *gs->st);
            if (!gs->st) { gt_close(gs->d); gs->d = NULL; continue; }
            gt_pc_begin(gs->d);
            g->open = 1;
            g->why[0] = 0;
            {
                const gt_info *in = gt_info_of(gs->d);
                snprintf(g->vram_type, sizeof g->vram_type, "%s", in->vram_type);
                g->max_sclk = in->max_gpu_clk;
                g->max_mclk = in->max_mem_clk;
                snprintf(g->driver_version, sizeof g->driver_version, "%s", in->driver_version[0] ? in->driver_version : in->drm_version);
                if (in->dev.name[0]) snprintf(g->dev.name, sizeof g->dev.name, "%s", in->dev.name);
            }
        }
    }
    if (n > o->ngpu) o->ngpu = n;
}

static int nz(long long v) { return v == GT_NA ? -1 : (int)v; }

static void gpu_sample(pd_sampler *s, double dt)
{
    pd_snap *o = &s->s;
    o->gpu_w = -1;
    o->gpu_busy = o->gpu_temp = -1;
    g_hash_table_remove_all(s->gpu_pid);
    for (int i = 0; i < o->ngpu; i++) {
        pd_gpu *g = &o->gpu[i];
        gslot *gs = &s->gs[i];
        gt_stat *st = gs->st;
        if (!gs->d) {
            pd_ring_push(&g->h_busy, NAN); pd_ring_push(&g->h_vram, NAN);
            pd_ring_push(&g->h_power, NAN); pd_ring_push(&g->h_temp, NAN);
            continue;
        }
        if (gt_finish(gs->d, (int)(dt * 1000 + 0.5), st) != 0) { gt_pc_begin(gs->d); continue; }
        gt_pc_begin(gs->d);
        g->have = 1;
        g->busy = st->gfx != GT_NA ? st->gfx : st->total.gfx != GT_NA ? st->total.gfx : -1;
        g->vram_used = st->vram_used; g->vram_total = st->vram_total;
        g->gtt_used = st->gtt_used; g->gtt_total = st->gtt_total;
        g->sclk = nz(st->sclk); g->mclk = nz(st->mclk);
        g->power = st->power_avg >= 0 ? st->power_avg : st->power_input;
        g->power_cap = nz(st->power_cap);
        g->edge = nz(st->edge); g->junction = nz(st->junction); g->memtemp = nz(st->memtemp);
        g->fan_rpm = nz(st->fan_rpm); g->fan_pct = nz(st->fan_pct);
        g->pcie_gen = nz(st->pcie_gen); g->pcie_width = nz(st->pcie_width);
        snprintf(g->pstate, sizeof g->pstate, "%s", st->pci_power_state);
        g->idle_d3 = st->idle_d3;
        pd_ring_push(&g->h_busy, g->busy >= 0 ? g->busy : NAN);
        pd_ring_push(&g->h_vram, g->vram_total > 0 && g->vram_used >= 0 ? 100.0 * (double)g->vram_used / (double)g->vram_total : NAN);
        pd_ring_push(&g->h_power, g->power >= 0 ? g->power : NAN);
        pd_ring_push(&g->h_temp, g->junction >= 0 ? g->junction : g->edge >= 0 ? g->edge : NAN);
        if (g->power >= 0) o->gpu_w = (o->gpu_w < 0 ? 0 : o->gpu_w) + g->power;
        if (g->busy > o->gpu_busy) o->gpu_busy = g->busy;
        if (g->edge > o->gpu_temp) o->gpu_temp = g->edge;
        for (int k = 0; k < st->nproc; k++) {
            const gt_proc *p = &st->proc[k];
            int e[9] = { p->gfx, p->compute, p->dma, p->dec, p->enc, p->media, p->vcn_jpeg, p->vpe, p->vcn_unified };
            int m = 0;
            gpointer old;
            long long vram = p->vram_kib > 0 ? p->vram_kib : 0;
            for (int j = 0; j < 9; j++) if (e[j] != GT_NA && e[j] > m) m = e[j];
            if (m > 100) m = 100;
            /* packed: low 8 bits = %, the rest = VRAM in KiB */
            old = g_hash_table_lookup(s->gpu_pid, GINT_TO_POINTER(p->pid));
            if (old) {
                guint64 ov = (guint64)(guintptr)old & ~0x80000000000000ull;
                int om = (int)(ov & 0xff);
                if (om > m) m = om;
                vram += (long long)(ov >> 8);
            }
            g_hash_table_insert(s->gpu_pid, GINT_TO_POINTER(p->pid),
                                (gpointer)(guintptr)((((guint64)vram) << 8) | (guint64)m | 0x80000000000000ull));
        }
    }
}

/* ---------------------------------------------------------------- desktop entries (application test) */

static void apps_add_exec(GHashTable *h, const char *exec)
{
    char tok[256];
    const char *p = exec;
    for (;;) {
        int n = 0;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) return;
        if (*p == '"') {
            p++;
            while (*p && *p != '"' && n < (int)sizeof tok - 1) tok[n++] = *p++;
            if (*p == '"') p++;
        } else
            while (*p && *p != ' ' && *p != '\t' && n < (int)sizeof tok - 1) tok[n++] = *p++;
        tok[n] = 0;
        if (!strcmp(tok, "env") || strchr(tok, '=')) continue;
        if (!strcmp(tok, "flatpak") || !strcmp(tok, "sh") || !strcmp(tok, "bash")) return;
        {
            const char *b = strrchr(tok, '/') ? strrchr(tok, '/') + 1 : tok;
            if (*b && !g_hash_table_contains(h, b)) g_hash_table_add(h, g_strdup(b));
        }
        return;
    }
}

static void apps_dir(GHashTable *h, const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char p[1024], b[1024];
    if (!d) return;
    while ((e = readdir(d))) {
        FILE *f;
        int in_main = 0, app = 0, hidden = 0;
        char exec[512] = "", tryexec[256] = "";
        size_t l = strlen(e->d_name);
        if (l < 9 || strcmp(e->d_name + l - 8, ".desktop")) continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (!(f = fopen(p, "r"))) continue;
        while (fgets(b, sizeof b, f)) {
            b[strcspn(b, "\r\n")] = 0;
            if (b[0] == '[') { if (in_main) break; in_main = !strcmp(b, "[Desktop Entry]"); continue; }
            if (!in_main) continue;
            if (!strcmp(b, "Type=Application")) app = 1;
            else if (!strcmp(b, "NoDisplay=true") || !strcmp(b, "Hidden=true")) hidden = 1;
            else if (!strncmp(b, "Exec=", 5)) snprintf(exec, sizeof exec, "%s", b + 5);
            else if (!strncmp(b, "TryExec=", 8)) snprintf(tryexec, sizeof tryexec, "%s", b + 8);
        }
        fclose(f);
        if (!app || hidden) continue;
        if (exec[0]) apps_add_exec(h, exec);
        if (tryexec[0]) apps_add_exec(h, tryexec);
    }
    closedir(d);
}

static void apps_load(pd_sampler *s)
{
    const char *dd = getenv("XDG_DATA_DIRS");
    char p[1024];
    gchar **v;
    snprintf(p, sizeof p, "%s/applications", g_get_user_data_dir());
    apps_dir(s->apps, p);
    v = g_strsplit(dd && *dd ? dd : "/usr/local/share:/usr/share", ":", -1);
    for (int i = 0; v[i]; i++) {
        if (!v[i][0]) continue;
        snprintf(p, sizeof p, "%s/applications", v[i]);
        apps_dir(s->apps, p);
    }
    g_strfreev(v);
    /* shells, interpreters and window-less helpers that ship visible desktop entries but are not applications */
    {
        static const char *const no[] = { "kdeconnectd", "sh", "bash", "zsh", "fish", "dash", "env", "python", "python3",
                                          "perl", "ruby", "node", "java", "wine", "xdg-open", "kioclient5", "kioclient",
                                          "kde-open", "gio", "pkexec", "sudo", "su", NULL };
        for (int i = 0; no[i]; i++) g_hash_table_remove(s->apps, no[i]);
    }
    s->apps_loaded = 1;
}

/* ---------------------------------------------------------------- processes */

static const char *user_name(pd_sampler *s, int uid)
{
    const char *n = g_hash_table_lookup(s->users, GINT_TO_POINTER(uid + 1));
    if (!n) {
        struct passwd *pw = getpwuid((uid_t)uid);
        char b[32];
        if (pw && pw->pw_name) snprintf(b, sizeof b, "%s", pw->pw_name);
        else snprintf(b, sizeof b, "%d", uid);
        n = g_strdup(b);
        g_hash_table_insert(s->users, GINT_TO_POINTER(uid + 1), (gpointer)n);
    }
    return n;
}

static int env_graphical(int pid)
{
    char p[300], b[32768];
    int l;
    pth(p, sizeof p, "/proc/%d/environ", pid);
    l = rd_all(p, b, sizeof b);
    for (int i = 0; i < l; ) {
        const char *e = b + i;
        if (!strncmp(e, "DISPLAY=", 8) || !strncmp(e, "WAYLAND_DISPLAY=", 16)) return 1;
        i += (int)strlen(e) + 1;
    }
    return 0;
}

/* first-sight facts of a process: name, command line, owner, cgroup (session / unit), application or not */
static void proc_first(pd_sampler *s, pcache *c, const char *comm, int tty, unsigned flags, int ppid)
{
    char p[300], b[4096], prog[64] = "";
    int l;
    struct stat stt;
    c->kthread = (flags & 0x00200000u) || ppid == 2 || c->pid == 2;
    snprintf(c->name, sizeof c->name, "%s", comm);
    c->cmd[0] = 0;
    pth(p, sizeof p, "/proc/%d", c->pid);
    c->uid = stat(p, &stt) == 0 ? (int)stt.st_uid : -1;
    snprintf(c->user, sizeof c->user, "%s", c->uid >= 0 ? user_name(s, c->uid) : "?");
    pth(p, sizeof p, "/proc/%d/cmdline", c->pid);
    l = c->kthread ? 0 : rd_all(p, b, sizeof b);
    if (l > 0) {
        int o = 0;
        const char *a0 = b, *bn;
        for (int i = 0; i < l && o < (int)sizeof c->cmd - 1; i++) c->cmd[o++] = (unsigned char)b[i] < ' ' ? ' ' : b[i];
        while (o > 0 && c->cmd[o - 1] == ' ') o--;
        c->cmd[o] = 0;
        bn = strrchr(a0, '/') ? strrchr(a0, '/') + 1 : a0;
        snprintf(prog, sizeof prog, "%.*s", (int)strcspn(bn, " "), bn);
        /* comm is cut at 15 characters: use the program name when it starts the same way */
        if (strlen(comm) >= 15 && !strncmp(bn, comm, strlen(comm))) {
            char t[48];
            snprintf(t, sizeof t, "%.*s", (int)strcspn(bn, " "), bn);
            snprintf(c->name, sizeof c->name, "%s", t);
        }
    } else
        snprintf(c->cmd, sizeof c->cmd, "[%s]", comm);
    c->session = -1;
    c->unit[0] = 0;
    pth(p, sizeof p, "/proc/%d/cgroup", c->pid);
    if (!c->kthread && rd_all(p, b, sizeof b) > 0) {
        char *ln = strstr(b, "0::");
        if (ln) {
            char *e = strchr(ln, '\n'), *sp, *last = NULL, *t;
            if (e) *e = 0;
            if ((sp = strstr(ln, "/session-"))) c->session = atoi(sp + 9);
            for (t = ln; (t = strchr(t, '/')); t++) {
                const char *seg = t + 1;
                size_t sl = strcspn(seg, "/");
                if ((sl > 8 && !strncmp(seg + sl - 8, ".service", 8)) || (sl > 6 && !strncmp(seg + sl - 6, ".scope", 6))) last = (char *)seg;
            }
            if (last) snprintf(c->unit, sizeof c->unit, "%.*s", (int)strcspn(last, "/"), last);
        }
    }
    c->is_app = 0;
    if (!c->kthread && tty == 0 && (g_hash_table_contains(s->apps, c->name) || g_hash_table_contains(s->apps, comm) ||
                                       (prog[0] && g_hash_table_contains(s->apps, prog)))) {
        if ((testing() || c->uid == (int)getuid()) && env_graphical(c->pid)) c->is_app = 1;
    }
    c->rd = c->wr = -1;
    c->io_denied = 0;
    c->ticks = 0;
    c->seen = 0;
}

static gboolean stale(gpointer k, gpointer v, gpointer gen)
{
    (void)k;
    return ((pcache *)v)->gen != GPOINTER_TO_UINT(gen);
}

static void procs_sample(pd_sampler *s, double dt)
{
    pd_snap *o = &s->s;
    char p[300], b[2048];
    DIR *d;
    struct dirent *e;
    double scale = (dt > 0 && o->ncpu > 0) ? 100.0 / (dt * (double)s->hz * (double)o->ncpu) : 0;
    s->gen++;
    o->nproc = o->napps = 0;
    o->nthreads = 0;
    pth(p, sizeof p, "/proc");
    if (!(d = opendir(p))) return;
    while ((e = readdir(d))) {
        int pid, ppid, tty, prio, nice, thr, l;
        unsigned flags;
        unsigned long long ut, stt, start;
        unsigned long long vsz;
        char comm[64], state;
        const char *rp;
        pcache *c;
        pd_proc *r;
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        pid = atoi(e->d_name);
        pth(p, sizeof p, "/proc/%d/stat", pid);
        if ((l = rd_all(p, b, sizeof b)) <= 0) continue;
        {
            char *lp = strchr(b, '('), *rpp = strrchr(b, ')');
            if (!lp || !rpp || rpp < lp) continue;
            snprintf(comm, sizeof comm, "%.*s", (int)(rpp - lp - 1), lp + 1);
            rp = rpp + 2;
        }
        /* state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime stime cutime cstime
           priority nice num_threads itrealvalue starttime vsize rss */
        if (sscanf(rp, "%c %d %*d %*d %d %*d %u %*u %*u %*u %*u %llu %llu %*d %*d %d %d %d %*d %llu %llu",
                   &state, &ppid, &tty, &flags, &ut, &stt, &prio, &nice, &thr, &start, &vsz) != 11) continue;
        c = g_hash_table_lookup(s->pc, GINT_TO_POINTER(pid));
        /* a reused PID (new start time) or an exec() (new comm): facts read again */
        if (c && (c->start != start || strcmp(c->comm, comm))) { g_hash_table_remove(s->pc, GINT_TO_POINTER(pid)); c = NULL; }
        if (!c) {
            if (!s->apps_loaded) apps_load(s);
            c = g_new0(pcache, 1);
            c->pid = pid;
            c->start = start;
            proc_first(s, c, comm, tty, flags, ppid);
            snprintf(c->comm, sizeof c->comm, "%s", comm);
            g_hash_table_insert(s->pc, GINT_TO_POINTER(pid), c);
        }
        c->gen = s->gen;
        if (o->nproc >= o->proc_cap) {
            int nc = o->proc_cap ? o->proc_cap * 2 : 1024;
            pd_proc *np = realloc(o->proc, (size_t)nc * sizeof *np);
            if (!np) break;
            o->proc = np;
            o->proc_cap = nc;
        }
        r = &o->proc[o->nproc++];
        r->pid = pid; r->ppid = ppid; r->uid = c->uid; r->start = start;
        snprintf(r->name, sizeof r->name, "%s", c->name);
        snprintf(r->user, sizeof r->user, "%s", c->user);
        snprintf(r->cmd, sizeof r->cmd, "%s", c->cmd);
        snprintf(r->unit, sizeof r->unit, "%s", c->unit);
        r->state = state;
        r->kthread = c->kthread;
        r->is_app = c->is_app;
        r->session = c->session;
        r->threads = thr; r->nice = nice; r->prio = prio;
        r->vsize = (long long)vsz;
        r->cpu_s = (double)(ut + stt) / (double)s->hz;
        r->cpu = c->seen && ut + stt >= c->ticks ? (double)(ut + stt - c->ticks) * scale : 0;
        if (r->cpu > 100) r->cpu = 100;
        c->ticks = ut + stt;
        r->rss = r->shared = 0;
        pth(p, sizeof p, "/proc/%d/statm", pid);
        if (rd_all(p, b, sizeof b) > 0) {
            unsigned long long sz, res, sh;
            if (sscanf(b, "%llu %llu %llu", &sz, &res, &sh) == 3) { r->rss = (long long)res * s->page; r->shared = (long long)sh * s->page; }
        }
        r->rd = r->wr = -1;
        r->rd_total = r->wr_total = -1;
        if (!c->io_denied && !c->kthread) {
            pth(p, sizeof p, "/proc/%d/io", pid);
            if (rd_all(p, b, sizeof b) > 0) {
                const char *a = strstr(b, "\nread_bytes:"), *w = strstr(b, "\nwrite_bytes:");
                if (a && w) {
                    long long rb = atoll(a + 12), wb = atoll(w + 13);
                    r->rd_total = rb; r->wr_total = wb;
                    if (c->seen && c->rd >= 0 && dt > 0) {
                        r->rd = rb >= c->rd ? (double)(rb - c->rd) / dt : 0;
                        r->wr = wb >= c->wr ? (double)(wb - c->wr) / dt : 0;
                    } else r->rd = r->wr = 0;
                    c->rd = rb; c->wr = wb;
                }
            } else c->io_denied = 1;   /* other users' processes: root-only, not asked again */
        }
        {
            gpointer gv = g_hash_table_lookup(s->gpu_pid, GINT_TO_POINTER(pid));
            if (gv) {
                guint64 v = (guint64)(guintptr)gv & ~0x80000000000000ull;
                r->gpu = (int)(v & 0xff);
                r->vram = (long long)(v >> 8) * 1024;
            } else { r->gpu = -1; r->vram = -1; }
        }
        c->seen++;
        o->nthreads += thr;
        if (r->is_app) o->napps++;
    }
    closedir(d);
    g_hash_table_foreach_remove(s->pc, stale, GUINT_TO_POINTER(s->gen));
    o->nprocs = o->nproc;
}

/* ---------------------------------------------------------------- whole-system counters */

static void cpu_sample(pd_sampler *s)
{
    pd_snap *o = &s->s;
    char p[300], b[256];
    FILE *f;
    int row = 0;
    pth(p, sizeof p, "/proc/stat");
    if (!(f = fopen(p, "r"))) return;
    while (fgets(b, sizeof b, f)) {
        unsigned long long v[8] = { 0 }, tot = 0, dt, *pr;
        int id = -1;
        if (strncmp(b, "cpu", 3)) break;
        if (b[3] != ' ') id = atoi(b + 3);
        if (sscanf(b + 3 + strcspn(b + 3, " "), "%llu %llu %llu %llu %llu %llu %llu %llu",
                   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) < 4) continue;
        if (row > PD_MAX_CPU) break;
        for (int i = 0; i < 8; i++) tot += v[i];
        pr = s->cprev + row * 9;
        dt = s->have_cprev && tot > pr[8] ? tot - pr[8] : 0;
        {
            double us = dt ? 100.0 * (double)((v[0] + v[1]) - (pr[0] + pr[1])) / (double)dt : 0;
            double ks = dt ? 100.0 * (double)((v[2] + v[5] + v[6]) - (pr[2] + pr[5] + pr[6])) / (double)dt : 0;
            if (us < 0) us = 0;
            if (ks < 0) ks = 0;
            if (us + ks > 100) { double k = 100 / (us + ks); us *= k; ks *= k; }
            if (row == 0) { o->cpu_user = us; o->cpu_kernel = ks; o->cpu = us + ks; }
            else {
                o->cpu_id[row - 1] = (short)id;
                o->cu[row - 1] = (float)us;
                o->ck[row - 1] = (float)ks;
                pd_ring_push(&o->hu[row - 1], s->have_cprev ? us : NAN);
                pd_ring_push(&o->hk[row - 1], s->have_cprev ? ks : NAN);
            }
        }
        for (int i = 0; i < 8; i++) pr[i] = v[i];
        pr[8] = tot;
        row++;
    }
    fclose(f);
    o->ncpu = row > 0 ? row - 1 : 0;
    pd_ring_push(&o->h_cpu, s->have_cprev ? o->cpu : NAN);
    pd_ring_push(&o->h_kernel, s->have_cprev ? o->cpu_kernel : NAN);
    s->have_cprev = 1;

    /* clocks */
    {
        double sum = 0, mx = 0;
        int n = 0;
        for (int i = 0; i < o->ncpu; i++) {
            long long khz;
            pth(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", o->cpu_id[i]);
            if (rd_ll(p, &khz) && khz > 0) { sum += khz / 1000.0; n++; if (khz / 1000.0 > mx) mx = khz / 1000.0; }
        }
        o->mhz = n ? sum / n : 0;
        o->mhz_peak = mx;
        pd_ring_push(&o->h_mhz, n ? o->mhz : NAN);
    }
    /* temperature */
    {
        long long mc;
        o->cpu_temp = s->ctemp_path[0] && rd_ll(s->ctemp_path, &mc) ? mc / 1000.0 : NAN;
        pd_ring_push(&o->h_ctemp, o->cpu_temp);
    }
    pth(p, sizeof p, "/proc/loadavg");
    if (rd_line(p, b, sizeof b)) sscanf(b, "%lf %lf %lf", &o->load1, &o->load5, &o->load15);
    pth(p, sizeof p, "/proc/uptime");
    if (rd_line(p, b, sizeof b)) o->uptime = atof(b);
}

static void power_sample(pd_sampler *s, double dt)
{
    pd_snap *o = &s->s;
    o->cpu_w = NAN;
    if (s->ps_ok && s->ps->ncpu) {
        pm_meas m;
        int fl;
        psrc_read(s->ps, dt, &m, &fl);
        o->cpu_w = m.cpu_w;
    }
    pd_ring_push(&o->h_cpu_w, o->cpu_w);
}

static long long mi(const char *b, const char *key)
{
    const char *p = strstr(b, key);
    if (!p) return -1;
    return atoll(p + strlen(key)) * 1024;
}

static void mem_sample(pd_sampler *s)
{
    pd_snap *o = &s->s;
    char p[300], b[8192];
    pth(p, sizeof p, "/proc/meminfo");
    if (rd_all(p, b, sizeof b) <= 0) return;
    o->mem_total = mi(b, "MemTotal:");
    o->mem_free = mi(b, "MemFree:");
    o->mem_avail = mi(b, "MemAvailable:");
    if (o->mem_avail < 0) o->mem_avail = o->mem_free;
    o->mem_buffers = mi(b, "Buffers:");
    o->mem_cached = mi(b, "\nCached:");
    {
        long long sr = mi(b, "SReclaimable:");
        if (sr > 0 && o->mem_cached >= 0) o->mem_cached += sr;
    }
    o->mem_shmem = mi(b, "Shmem:");
    o->mem_slab = mi(b, "\nSlab:");
    o->mem_dirty = mi(b, "Dirty:");
    o->swap_total = mi(b, "SwapTotal:");
    o->swap_used = o->swap_total >= 0 ? o->swap_total - mi(b, "SwapFree:") : -1;
    o->commit = mi(b, "Committed_AS:");
    o->commit_limit = mi(b, "CommitLimit:");
    o->mem_used = o->mem_total - o->mem_avail;
    pd_ring_push(&o->h_mem, o->mem_total > 0 ? 100.0 * (double)o->mem_used / (double)o->mem_total : NAN);
    pd_ring_push(&o->h_swap, o->swap_total > 0 ? 100.0 * (double)o->swap_used / (double)o->swap_total : NAN);
}

static void psi_one(const char *name, double *some, double *full, int *ok)
{
    char p[300], b[512];
    const char *t;
    pth(p, sizeof p, "/proc/pressure/%s", name);
    *some = 0;
    if (full) *full = 0;
    if (rd_all(p, b, sizeof b) <= 0) return;
    *ok = 1;
    if ((t = strstr(b, "some avg10="))) *some = atof(t + 11);
    if (full && (t = strstr(b, "full avg10="))) *full = atof(t + 11);
}

static void psi_sample(pd_sampler *s)
{
    pd_snap *o = &s->s;
    int ok = 0;
    psi_one("cpu", &o->psi_cpu, NULL, &ok);
    psi_one("memory", &o->psi_mem, &o->psi_mem_full, &ok);
    psi_one("io", &o->psi_io, &o->psi_io_full, &ok);
    o->psi_ok = ok;
    pd_ring_push(&o->h_psi, ok ? o->psi_cpu : NAN);
}

static void disk_sample(pd_sampler *s, double dt)
{
    pd_snap *o = &s->s;
    char p[300], b[512];
    FILE *f;
    o->disk_rd = o->disk_wr = o->disk_busy = 0;
    pth(p, sizeof p, "/proc/diskstats");
    if (!(f = fopen(p, "r"))) return;
    while (fgets(b, sizeof b, f)) {
        char nm[64];
        unsigned long long r, rs, rms, w, ws, wms, ioms;
        if (sscanf(b, "%*u %*u %63s %llu %*u %llu %llu %llu %*u %llu %llu %*u %llu",
                   nm, &r, &rs, &rms, &w, &ws, &wms, &ioms) != 8) continue;
        for (int i = 0; i < o->ndisk; i++) {
            pd_disk *k = &o->disk[i];
            unsigned long long *pv = s->dprev[i];
            if (strcmp(k->name, nm)) continue;
            k->rd_total = (long long)rs * 512;
            k->wr_total = (long long)ws * 512;
            if (s->dhave[i] && dt > 0) {
                unsigned long long ios = (r + w) - (pv[3] + pv[4]);
                k->rd = rs >= pv[0] ? (double)(rs - pv[0]) * 512 / dt : 0;
                k->wr = ws >= pv[1] ? (double)(ws - pv[1]) * 512 / dt : 0;
                k->busy = ioms >= pv[2] ? (double)(ioms - pv[2]) / (dt * 10.0) : 0;
                if (k->busy > 100) k->busy = 100;
                k->resp_ms = ios ? (double)((rms + wms) - pv[5]) / (double)ios : 0;
            } else k->rd = k->wr = k->busy = k->resp_ms = 0;
            pv[0] = rs; pv[1] = ws; pv[2] = ioms; pv[3] = r; pv[4] = w; pv[5] = rms + wms;
            s->dhave[i] = 1;
        }
    }
    fclose(f);
    for (int i = 0; i < o->ndisk; i++) {
        pd_disk *k = &o->disk[i];
        pd_ring_push(&k->h_busy, k->busy);
        pd_ring_push(&k->h_rd, k->rd);
        pd_ring_push(&k->h_wr, k->wr);
        o->disk_rd += k->rd;
        o->disk_wr += k->wr;
        if (k->busy > o->disk_busy) o->disk_busy = k->busy;
    }
    pd_ring_push(&o->h_disk, o->disk_busy);
}

static void net_sample(pd_sampler *s, double dt)
{
    pd_snap *o = &s->s;
    char p[300], b[512];
    FILE *f;
    o->net_rx = o->net_tx = 0;
    pth(p, sizeof p, "/proc/net/dev");
    if (!(f = fopen(p, "r"))) return;
    while (fgets(b, sizeof b, f)) {
        char *c = strchr(b, ':'), *t = b;
        unsigned long long rx, tx;
        if (!c) continue;
        *c = 0;
        while (*t == ' ') t++;
        if (sscanf(c + 1, "%llu %*u %*u %*u %*u %*u %*u %*u %llu", &rx, &tx) != 2) continue;
        for (int i = 0; i < o->nnet; i++) {
            pd_net *k = &o->net[i];
            if (strcmp(k->name, t)) continue;
            k->rx_total = (long long)rx;
            k->tx_total = (long long)tx;
            if (s->nhave[i] && dt > 0) {
                k->rx = rx >= s->nprev[i][0] ? (double)(rx - s->nprev[i][0]) / dt : 0;
                k->tx = tx >= s->nprev[i][1] ? (double)(tx - s->nprev[i][1]) / dt : 0;
            } else k->rx = k->tx = 0;
            s->nprev[i][0] = rx; s->nprev[i][1] = tx;
            s->nhave[i] = 1;
        }
    }
    fclose(f);
    for (int i = 0; i < o->nnet; i++) {
        pd_net *k = &o->net[i];
        pd_ring_push(&k->h_rx, k->rx);
        pd_ring_push(&k->h_tx, k->tx);
        o->net_rx += k->rx;
        o->net_tx += k->tx;
    }
    pd_ring_push(&o->h_net, o->net_rx + o->net_tx);
}

static void temp_sample(pd_sampler *s)
{
    pd_snap *o = &s->s;
    pd_temp old[PD_MAX_TEMP];
    int on = o->ntemp, n = 0;
    char lab[PD_MAX_TEMP][40];
    double val[PD_MAX_TEMP];
    memcpy(old, o->temp, sizeof old);
    if (s->ctemp_path[0]) { snprintf(lab[n], sizeof lab[0], "CPU"); val[n++] = o->cpu_temp; }
    for (int i = 0; i < o->ngpu && n < PD_MAX_TEMP; i++) {
        const pd_gpu *g = &o->gpu[i];
        int t = g->junction >= 0 ? g->junction : g->edge;
        if (!g->open) continue;
        if (o->ngpu > 1) snprintf(lab[n], sizeof lab[0], "GPU %d %s", i + 1, g->junction >= 0 ? "Hotspot" : "Edge");
        else snprintf(lab[n], sizeof lab[0], "GPU %s", g->junction >= 0 ? "Hotspot" : "Edge");
        val[n++] = t >= 0 ? t : NAN;
    }
    for (int i = 0; i < s->ndtemp && n < PD_MAX_TEMP; i++) {
        long long mc;
        snprintf(lab[n], sizeof lab[0], "%s", s->dtemp_label[i]);
        val[n++] = rd_ll(s->dtemp_path[i], &mc) ? mc / 1000.0 : NAN;
    }
    for (int i = 0; i < n; i++) {
        pd_temp *t = &o->temp[i];
        int j;
        for (j = 0; j < on; j++) if (!strcmp(old[j].label, lab[i])) break;
        if (j < on) *t = old[j];
        else { memset(t, 0, sizeof *t); snprintf(t->label, sizeof t->label, "%s", lab[i]); }
        t->c = val[i];
        pd_ring_push(&t->h, val[i]);
    }
    o->ntemp = n;
}

/* ---------------------------------------------------------------- public */

pd_sampler *pd_new(int with_gpu)
{
    pd_sampler *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->cprev = calloc((PD_MAX_CPU + 1) * 9, sizeof *s->cprev);
    if (!s->cprev) { free(s); return NULL; }
    s->with_gpu = with_gpu;
    s->hz = sysconf(_SC_CLK_TCK);
    if (s->hz <= 0) s->hz = 100;
    s->page = sysconf(_SC_PAGESIZE);
    if (s->page <= 0) s->page = 4096;
    s->pc = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    s->gpu_pid = g_hash_table_new(g_direct_hash, g_direct_equal);
    s->apps = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    s->users = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    cpu_static(s);
    hwmon_scan(s);
    /* CPU package power: the Power & Cost page's sources; nothing privileged is added here */
    s->s.cpu_w_state = PD_W_NONE;
    if (!testing() && (s->ps = calloc(1, sizeof *s->ps))) {
        psrc_detect(s->ps);
        s->ps->ngpu = 0;            /* GPU power comes from gpu_top.h; do not read the cards twice */
        s->ps_ok = 1;
        if (s->ps->ncpu) {
            s->s.cpu_w_state = PD_W_OK;
            snprintf(s->s.cpu_w_src, sizeof s->s.cpu_w_src, "%s", s->ps->cpu_how);
        } else if (s->ps->rapl_state == 2) {
            s->s.cpu_w_state = PD_W_ROOT;
            snprintf(s->s.cpu_w_src, sizeof s->s.cpu_w_src, "RAPL is root-only (zenergy driver would make it readable)");
        } else
            snprintf(s->s.cpu_w_src, sizeof s->s.cpu_w_src, "no CPU energy counter found");
    } else if (testing())
        snprintf(s->s.cpu_w_src, sizeof s->s.cpu_w_src, "not read in test mode");
    disk_scan(s);
    net_scan(s);
    if (with_gpu) gpu_scan(s);
    return s;
}

void pd_free(pd_sampler *s)
{
    if (!s) return;
    for (int i = 0; i < PD_MAX_GPU; i++) {
        if (s->gs[i].d) gt_close(s->gs[i].d);
        free(s->gs[i].st);
    }
    g_hash_table_destroy(s->pc);
    g_hash_table_destroy(s->gpu_pid);
    g_hash_table_destroy(s->apps);
    g_hash_table_destroy(s->users);
    free(s->ps);
    free(s->cprev);
    free(s->s.proc);
    free(s);
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

const pd_snap *pd_sample(pd_sampler *s, double dt)
{
    pd_snap *o = &s->s;
    double t = now_s();
    if (dt <= 0) dt = s->t_prev > 0 ? t - s->t_prev : 0;
    s->t_prev = t;
    o->dt = dt;
    o->seq++;
    if (++s->rescan >= 5) {
        s->rescan = 0;
        disk_scan(s);
        net_scan(s);
        if (s->with_gpu) gpu_scan(s);
    }
    cpu_sample(s);
    power_sample(s, dt);
    mem_sample(s);
    psi_sample(s);
    disk_sample(s, dt);
    net_sample(s, dt);
    if (s->with_gpu) gpu_sample(s, dt > 0 ? dt : 1.0);
    else { o->gpu_w = -1; o->gpu_busy = o->gpu_temp = -1; }
    pd_ring_push(&o->h_gpu, o->gpu_busy >= 0 ? o->gpu_busy : NAN);
    temp_sample(s);
    if (s->no_procs) {
        o->nproc = o->napps = o->nprocs = o->nthreads = 0;
        g_hash_table_remove_all(s->pc);
    } else procs_sample(s, dt);
    return o;
}

void pd_set_procs(pd_sampler *s, int on)
{
    if (!s || s->no_procs == !on) return;
    s->no_procs = !on;
    /* the per-process GPU walk (DRM fdinfo) is most of a light sampler's cost: off with the process list */
    for (int i = 0; i < PD_MAX_GPU; i++) if (s->gs[i].d) gt_set_no_fdinfo(s->gs[i].d, !on);
}

void pd_snap_copy(pd_snap *dst, const pd_snap *src)
{
    pd_proc *keep = dst->proc;
    int cap = dst->proc_cap;
    if (cap < src->nproc) {
        int nc = src->proc_cap > src->nproc ? src->proc_cap : src->nproc;
        pd_proc *np = realloc(keep, (size_t)(nc ? nc : 1) * sizeof *np);
        if (np) { keep = np; cap = nc; }
    }
    memcpy(dst, src, sizeof *dst);
    dst->proc = keep;
    dst->proc_cap = cap;
    if (cap < src->nproc) dst->nproc = cap;
    if (dst->nproc && keep) memcpy(keep, src->proc, (size_t)dst->nproc * sizeof *keep);
}

void pd_snap_free(pd_snap *s)
{
    free(s->proc);
    s->proc = NULL;
    s->proc_cap = s->nproc = 0;
}

int pd_find(const pd_snap *s, int pid)
{
    for (int i = 0; i < s->nproc; i++) if (s->proc[i].pid == pid) return i;
    return -1;
}

/* ---------------------------------------------------------------- formatting */

void pd_fmt_bytes(double b, char *out, int n)
{
    static const char *const u[] = { "B", "KB", "MB", "GB", "TB", "PB" };
    int i = 0;
    if (b < 0 || isnan(b)) { snprintf(out, (size_t)n, "—"); return; }
    while (b >= 1024 && i < 5) { b /= 1024; i++; }
    if (i == 0) snprintf(out, (size_t)n, "%.0f B", b);
    else snprintf(out, (size_t)n, b < 10 ? "%.2f %s" : b < 100 ? "%.1f %s" : "%.0f %s", b, u[i]);
}

void pd_fmt_rate(double bps, char *out, int n)
{
    char t[32];
    if (bps < 0 || isnan(bps)) { snprintf(out, (size_t)n, "—"); return; }
    pd_fmt_bytes(bps, t, sizeof t);
    snprintf(out, (size_t)n, "%s/s", t);
}

void pd_fmt_bits(double bps, char *out, int n)
{
    double b = bps * 8;
    if (bps < 0 || isnan(bps)) { snprintf(out, (size_t)n, "—"); return; }
    if (b >= 1e9) snprintf(out, (size_t)n, "%.2f Gbps", b / 1e9);
    else if (b >= 1e6) snprintf(out, (size_t)n, "%.1f Mbps", b / 1e6);
    else if (b >= 1e3) snprintf(out, (size_t)n, "%.1f Kbps", b / 1e3);
    else snprintf(out, (size_t)n, "%.0f bps", b);
}

void pd_fmt_mhz(double mhz, char *out, int n)
{
    if (mhz <= 0 || isnan(mhz)) snprintf(out, (size_t)n, "—");
    else if (mhz >= 1000) snprintf(out, (size_t)n, "%.2f GHz", mhz / 1000);
    else snprintf(out, (size_t)n, "%.0f MHz", mhz);
}

void pd_fmt_uptime(double s, char *out, int n)
{
    long t = (long)s;
    snprintf(out, (size_t)n, "%ld:%02ld:%02ld:%02ld", t / 86400, t / 3600 % 24, t / 60 % 60, t % 60);
}

int pd_dump(const pd_snap *s, char *buf, int len)
{
    int o = 0;
#define P(...) do { if (o < len) o += snprintf(buf + o, (size_t)(len - o), __VA_ARGS__); } while (0)
    P("ncpu: %d\ncores: %d\nsockets: %d\nmodel: %s\n", s->ncpu, s->cores, s->sockets, s->cpu_model);
    P("cpu: %.1f\nuser: %.1f\nkernel: %.1f\n", s->cpu, s->cpu_user, s->cpu_kernel);
    for (int i = 0; i < s->ncpu; i++) P("cpu%d: %.1f %.1f\n", s->cpu_id[i], s->cu[i], s->ck[i]);
    P("mhz: %.0f\nmhz_peak: %.0f\nmax_mhz: %.0f\ngovernor: %s\ndriver: %s\n", s->mhz, s->mhz_peak, s->max_mhz, s->governor, s->freq_driver);
    P("cpu_temp: %.1f (%s)\n", s->cpu_temp, s->cpu_temp_src);
    P("mem_total: %lld\nmem_used: %lld\nmem_avail: %lld\nmem_cached: %lld\nswap_used: %lld\n",
      s->mem_total, s->mem_used, s->mem_avail, s->mem_cached, s->swap_used);
    P("psi: %d cpu=%.2f mem=%.2f/%.2f io=%.2f/%.2f\n", s->psi_ok, s->psi_cpu, s->psi_mem, s->psi_mem_full, s->psi_io, s->psi_io_full);
    for (int i = 0; i < s->ndisk; i++)
        P("disk %s: rd=%.0f wr=%.0f busy=%.1f resp=%.2f size=%lld model=%s\n", s->disk[i].name, s->disk[i].rd,
          s->disk[i].wr, s->disk[i].busy, s->disk[i].resp_ms, s->disk[i].size, s->disk[i].model);
    for (int i = 0; i < s->nnet; i++)
        P("net %s: rx=%.0f tx=%.0f kind=%s speed=%d\n", s->net[i].name, s->net[i].rx, s->net[i].tx, s->net[i].kind, s->net[i].speed_mbps);
    for (int i = 0; i < s->ngpu; i++)
        P("gpu %d: open=%d busy=%d vram=%lld/%lld power=%.1f edge=%d junction=%d name=%s\n", i, s->gpu[i].open, s->gpu[i].busy,
          s->gpu[i].vram_used, s->gpu[i].vram_total, s->gpu[i].power, s->gpu[i].edge, s->gpu[i].junction, s->gpu[i].dev.name);
    for (int i = 0; i < s->ntemp; i++) P("temp %s: %.1f\n", s->temp[i].label, s->temp[i].c);
    P("procs: %d\nthreads: %d\napps: %d\n", s->nproc, s->nthreads, s->napps);
    for (int i = 0; i < s->nproc; i++) {
        const pd_proc *p = &s->proc[i];
        P("proc %d: name=%s ppid=%d user=%s state=%c cpu=%.1f rss=%lld rd=%.0f wr=%.0f thr=%d nice=%d gpu=%d app=%d kthread=%d session=%d unit=%s cmd=%s\n",
          p->pid, p->name, p->ppid, p->user, p->state, p->cpu, p->rss, p->rd, p->wr, p->threads, p->nice, p->gpu, p->is_app,
          p->kthread, p->session, p->unit, p->cmd);
    }
#undef P
    return o;
}
