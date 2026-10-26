/* power_src.c - SRS "Power & cost": measured sources (sysfs), clocks and the default parts list.
 *
 * CPU: the zenergy driver (or amd_energy) when present: AMD's RAPL counters, readable by normal
 * users by design (rate-limited). Otherwise powercap RAPL, which Linux 5.10+ keeps root-only
 * (PLATYPUS, CVE-2020-8694/8695/12912): SRS reads it only when it is readable and never loosens it.
 * GPU: amdgpu power1_average / power1_input (PPT), Intel i915 / xe energy1_input, nouveau power1_input.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "power_src.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int rd_str(const char *path, char *out, int sz)
{
    FILE *f = fopen(path, "r");
    int n;
    out[0] = 0;
    if (!f) return 0;
    if (!fgets(out, sz, f)) { fclose(f); return 0; }
    fclose(f);
    n = (int)strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == ' ')) out[--n] = 0;
    return 1;
}

static int rd_dbl(const char *path, double *v)
{
    char b[64], *e;
    if (!rd_str(path, b, sizeof b) || !b[0]) return 0;
    *v = strtod(b, &e);
    return e != b;
}

static void line(psrc *s, const char *fmt, ...)
{
    va_list ap;
    if (s->nlines >= PSRC_LINES) return;
    va_start(ap, fmt);
    vsnprintf(s->lines[s->nlines++], sizeof s->lines[0], fmt, ap);
    va_end(ap);
}

static psrc_chan *add(psrc_chan *arr, int *n, const char *path, const char *label, int counter, double max_w, double range)
{
    psrc_chan *c;
    if (*n >= PSRC_CH) return NULL;
    c = &arr[(*n)++];
    memset(c, 0, sizeof *c);
    snprintf(c->path, sizeof c->path, "%s", path);
    snprintf(c->label, sizeof c->label, "%s", label);
    c->counter = counter;
    c->max_w = max_w;
    c->c.max_range = range;
    c->w = NAN;
    return c;
}

/* hwmon chips with the CPU socket energy counters: zenergy (out-of-tree) or amd_energy */
static int cpu_from_hwmon(psrc *s, const char *drv)
{
    DIR *d = opendir("/sys/class/hwmon");
    struct dirent *e;
    int found = 0;
    if (!d) return 0;
    while ((e = readdir(d))) {
        char p[400], name[64];
        int i;
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "/sys/class/hwmon/%s/name", e->d_name);
        if (!rd_str(p, name, sizeof name) || strcmp(name, drv)) continue;
        for (i = 1; i < 512; i++) {
            char lab[64], in[400], txt[200];
            double v;
            snprintf(p, sizeof p, "/sys/class/hwmon/%s/energy%d_label", e->d_name, i);
            if (!rd_str(p, lab, sizeof lab)) { if (i > 300) break; continue; }
            if (strncmp(lab, "Esocket", 7)) continue;
            snprintf(in, sizeof in, "/sys/class/hwmon/%s/energy%d_input", e->d_name, i);
            if (!rd_dbl(in, &v)) continue;
            snprintf(txt, sizeof txt, "%s %s (%s)", drv, lab, e->d_name);
            if (add(s->cpu, &s->ncpu, in, txt, 1, 1000, 0)) found++;
        }
    }
    closedir(d);
    return found;
}

static void cpu_detect(psrc *s)
{
    DIR *d;
    struct dirent *e;
    int readable = 0, present = 0;
    /* powercap first, only to report its state */
    if ((d = opendir("/sys/class/powercap"))) {
        while ((e = readdir(d))) {
            char p[400], nm[64];
            int fd;
            if (strncmp(e->d_name, "intel-rapl:", 11) || strchr(e->d_name + 11, ':')) continue;   /* packages only */
            snprintf(p, sizeof p, "/sys/class/powercap/%s/name", e->d_name);
            if (!rd_str(p, nm, sizeof nm) || strncmp(nm, "package", 7)) continue;
            present = 1;
            snprintf(p, sizeof p, "/sys/class/powercap/%s/energy_uj", e->d_name);
            fd = open(p, O_RDONLY);
            if (fd >= 0) { readable = 1; close(fd); }
        }
        closedir(d);
    }
    s->rapl_state = !present ? 0 : readable ? 1 : 2;

    if (cpu_from_hwmon(s, "zenergy")) {
        snprintf(s->cpu_how, sizeof s->cpu_how, "measured (zenergy)");
        line(s, "CPU: zenergy driver, socket energy counter: used. The CPU's own RAPL energy counter, made readable for "
                "normal users by the driver in a safe, rate-limited way. On AMD it is a model inside the chip, not a meter: "
                "it is good for changes, and may read lower than a wall meter.");
    } else if (cpu_from_hwmon(s, "amd_energy")) {
        snprintf(s->cpu_how, sizeof s->cpu_how, "measured (amd_energy)");
        line(s, "CPU: amd_energy driver, socket energy counter: used (AMD's RAPL model, see zenergy).");
    } else if (readable && (d = opendir("/sys/class/powercap"))) {
        while ((e = readdir(d))) {
            char p[400], nm[64], txt[200];
            double range = 0;
            if (strncmp(e->d_name, "intel-rapl:", 11) || strchr(e->d_name + 11, ':')) continue;
            snprintf(p, sizeof p, "/sys/class/powercap/%s/name", e->d_name);
            if (!rd_str(p, nm, sizeof nm) || strncmp(nm, "package", 7)) continue;
            snprintf(p, sizeof p, "/sys/class/powercap/%s/max_energy_range_uj", e->d_name);
            rd_dbl(p, &range);
            snprintf(p, sizeof p, "/sys/class/powercap/%s/energy_uj", e->d_name);
            snprintf(txt, sizeof txt, "powercap %s (%s)", e->d_name, nm);
            add(s->cpu, &s->ncpu, p, txt, 1, 1000, range);
        }
        closedir(d);
        snprintf(s->cpu_how, sizeof s->cpu_how, "measured (RAPL)");
        line(s, "CPU: powercap RAPL package counter: used. It is readable on this PC, which stock Linux does not allow "
                "since 5.10 (a local rule or root changed that). SRS never changes this permission. The counter wraps "
                "around every few minutes; SRS unwraps it.");
    }
    if (!s->ncpu) {
        snprintf(s->cpu_how, sizeof s->cpu_how, "estimated (not readable)");
        if (s->rapl_state == 2)
            line(s, "CPU: not measured. The CPU's energy counter (powercap RAPL) is root-only since Linux 5.10 because "
                    "unprivileged energy readings can leak secrets (PLATYPUS). Options: install the zenergy driver (AMD), "
                    "or run SRS as root for a quick check. SRS uses the CPU row of the parts list instead.");
        else
            line(s, "CPU: no energy counter found. SRS uses the CPU row of the parts list instead.");
    } else if (s->rapl_state == 1 && strncmp(s->cpu_how, "measured (RAPL", 14))
        line(s, "CPU: powercap RAPL is also readable here (a local rule or root); not used, because the driver above "
                "gives the same counter. Stock Linux keeps it root-only since 5.10 (PLATYPUS).");
    else if (s->rapl_state == 2)
        line(s, "CPU: powercap RAPL is root-only, as Linux intends (not needed here).");
}

static void gpu_detect(psrc *s)
{
    DIR *d = opendir("/sys/class/drm");
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d))) {
        char p[400], drv[300], base[400], txt[200];
        const char *dn;
        DIR *h;
        struct dirent *he;
        double vram = -1, v;
        ssize_t n;
        if (strncmp(e->d_name, "card", 4) || strchr(e->d_name, '-')) continue;
        snprintf(p, sizeof p, "/sys/class/drm/%s/device/driver", e->d_name);
        n = readlink(p, drv, sizeof drv - 1);
        if (n <= 0) continue;
        drv[n] = 0;
        dn = strrchr(drv, '/') ? strrchr(drv, '/') + 1 : drv;
        snprintf(p, sizeof p, "/sys/class/drm/%s/device/mem_info_vram_total", e->d_name);
        rd_dbl(p, &vram);
        if (!strcmp(dn, "nvidia")) {
            s->dgpu_unread = 1;
            line(s, "GPU %s: NVIDIA proprietary driver: SRS does not read its power yet; the graphics card row of the "
                    "parts list is used instead.", e->d_name);
            continue;
        }
        if (!strcmp(dn, "amdgpu") && vram >= 0 && vram < 1024.0 * 1024 * 1024) {
            line(s, "GPU %s: integrated graphics (amdgpu, %.0f MB carve-out): its power is already part of the CPU "
                    "package, so it is not added again.", e->d_name, vram / 1048576);
            continue;
        }
        snprintf(p, sizeof p, "/sys/class/drm/%s/device/gpu_busy_percent", e->d_name);
        if (access(p, R_OK) == 0 && s->nbusy < PSRC_CH) snprintf(s->busy_path[s->nbusy++], sizeof s->busy_path[0], "%s", p);
        snprintf(base, sizeof base, "/sys/class/drm/%s/device/hwmon", e->d_name);
        if (!(h = opendir(base))) {
            if (!strcmp(dn, "amdgpu") || !strcmp(dn, "nouveau") || !strcmp(dn, "xe")) s->dgpu_unread = 1;
            continue;
        }
        while ((he = readdir(h))) {
            char hp[700];
            if (strncmp(he->d_name, "hwmon", 5)) continue;
            snprintf(hp, sizeof hp, "%s/%s/power1_average", base, he->d_name);
            if (!rd_dbl(hp, &v)) snprintf(hp, sizeof hp, "%s/%s/power1_input", base, he->d_name);
            if (rd_dbl(hp, &v) && (!strcmp(dn, "amdgpu") || !strcmp(dn, "nouveau"))) {
                snprintf(txt, sizeof txt, "%s %s (%s)", dn, strstr(hp, "average") ? "PPT power1_average" : "power1_input", e->d_name);
                add(s->gpu, &s->ngpu, hp, txt, 0, 1500, 0);
                if (!strcmp(dn, "amdgpu"))
                    line(s, "GPU %s: amdgpu reported power (PPT): used. This is the graphics chip's power as the "
                            "driver reports it; the rest of the card (fans, memory, power stages) is estimated "
                            "as a percentage in the parts list.", e->d_name);
                else
                    line(s, "GPU %s: nouveau power reading: used.", e->d_name);
                continue;
            }
            snprintf(hp, sizeof hp, "%s/%s/energy1_input", base, he->d_name);
            if (rd_dbl(hp, &v) && (!strcmp(dn, "i915") || !strcmp(dn, "xe"))) {
                snprintf(txt, sizeof txt, "%s energy1_input (%s)", dn, e->d_name);
                add(s->gpu, &s->ngpu, hp, txt, 1, 1500, 0);
                line(s, "GPU %s: Intel %s card energy counter: used.", e->d_name, dn);
            }
        }
        closedir(h);
    }
    closedir(d);
    if (s->ngpu) {
        int amd = 0, i;
        for (i = 0; i < s->ngpu; i++) if (strstr(s->gpu[i].label, "amdgpu")) amd = 1;
        snprintf(s->gpu_how, sizeof s->gpu_how, amd ? "reported by the driver (amdgpu PPT)" : "reported by the driver");
    } else
        snprintf(s->gpu_how, sizeof s->gpu_how, s->dgpu_unread ? "estimated (not readable)" : "none found");
}

void psrc_detect(psrc *s)
{
    pm_meas m;
    int f;
    memset(s, 0, sizeof *s);
    cpu_detect(s);
    gpu_detect(s);
    line(s, "Everything else (motherboard, memory, drives, fans, lights, the power supply's losses) cannot be "
            "measured from inside the PC: SRS adds a low and a high guess for each part (Setup tab).");
    psrc_read(s, 0, &m, &f);   /* baseline for the counters */
}

void psrc_read(psrc *s, double dt, pm_meas *m, int *flags)
{
    int i, f, fl = 0;
    double cpu = 0, gpu = 0;
    int cpu_ok = 0, gpu_ok = 0;
    for (i = 0; i < s->ncpu + s->ngpu; i++) {
        psrc_chan *c = i < s->ncpu ? &s->cpu[i] : &s->gpu[i - s->ncpu];
        double v;
        if (!rd_dbl(c->path, &v)) { c->w = NAN; continue; }
        if (c->counter) {
            double j = pm_counter_step(&c->c, v, dt, c->max_w, &f);
            fl |= f;
            if (j >= 0 && dt > 0) c->w = j / dt;
            else if (j < 0 && !isnan(c->w)) fl |= PM_F_EST;   /* hold the last reading for this interval */
        } else
            c->w = v / 1e6;
        if (isnan(c->w)) continue;
        if (i < s->ncpu) { cpu += c->w; cpu_ok = 1; }
        else { gpu += c->w; gpu_ok = 1; }
    }
    m->cpu_w = cpu_ok ? cpu : NAN;
    m->gpu_w = gpu_ok ? gpu : NAN;
    *flags = fl;
}

void psrc_clock(pm_clock *c)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    c->mono = (double)ts.tv_sec + ts.tv_nsec / 1e9;
#ifdef CLOCK_BOOTTIME
    clock_gettime(CLOCK_BOOTTIME, &ts);
#endif
    c->boot = (double)ts.tv_sec + ts.tv_nsec / 1e9;
    clock_gettime(CLOCK_REALTIME, &ts);
    c->wall = (double)ts.tv_sec + ts.tv_nsec / 1e9;
    if (!rd_str("/proc/sys/kernel/random/boot_id", c->boot_id, sizeof c->boot_id)) c->boot_id[0] = 0;
}

void psrc_load(psrc *s, double *cpu, double *gpu)
{
    FILE *f = fopen("/proc/stat", "r");
    unsigned long long v[10] = { 0 }, tot = 0, idle;
    int i;
    *cpu = *gpu = NAN;
    if (f) {
        if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) == 8) {
            for (i = 0; i < 8; i++) tot += v[i];
            idle = v[3] + v[4];
            if (s->cpu_total && tot > s->cpu_total)
                *cpu = 100.0 * (1.0 - (double)(idle - s->cpu_idle) / (double)(tot - s->cpu_total));
            s->cpu_total = tot;
            s->cpu_idle = idle;
        }
        fclose(f);
    }
    for (i = 0; i < s->nbusy; i++) {
        double b;
        if (rd_dbl(s->busy_path[i], &b) && (isnan(*gpu) || b > *gpu)) *gpu = b;
    }
}

double psrc_boot_wall(void)
{
    FILE *f = fopen("/proc/stat", "r");
    char b[512];
    double bt = 0;
    if (!f) return 0;
    while (fgets(b, sizeof b, f)) if (!strncmp(b, "btime ", 6)) { bt = atof(b + 6); break; }
    fclose(f);
    return bt;
}

/* ---------------------------------------------------------------- default parts */
static int count_hwmon(const char *name)
{
    DIR *d = opendir("/sys/class/hwmon");
    struct dirent *e;
    int n = 0;
    if (!d) return 0;
    while ((e = readdir(d))) {
        char p[400], nm[64];
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "/sys/class/hwmon/%s/name", e->d_name);
        if (rd_str(p, nm, sizeof nm) && !strcmp(nm, name)) n++;
    }
    closedir(d);
    return n;
}

static int count_fans(void)
{
    DIR *d = opendir("/sys/class/hwmon");
    struct dirent *e;
    int n = 0;
    if (!d) return 0;
    while ((e = readdir(d))) {
        char p[400], nm[64];
        int i;
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "/sys/class/hwmon/%s/name", e->d_name);
        if (!rd_str(p, nm, sizeof nm) || !strcmp(nm, "amdgpu") || !strcmp(nm, "nouveau")) continue;   /* the GPU's own fans are in the card */
        for (i = 1; i < 16; i++) {
            double v;
            snprintf(p, sizeof p, "/sys/class/hwmon/%s/fan%d_input", e->d_name, i);
            if (rd_dbl(p, &v) && v > 100 && v < 20000) n++;
        }
    }
    closedir(d);
    return n;
}

static void count_disks(int *nvme, int *hdd, int *ssd)
{
    DIR *d;
    struct dirent *e;
    *nvme = *hdd = *ssd = 0;
    if ((d = opendir("/sys/class/nvme"))) {
        while ((e = readdir(d))) if (!strncmp(e->d_name, "nvme", 4)) (*nvme)++;
        closedir(d);
    }
    if ((d = opendir("/sys/block"))) {
        while ((e = readdir(d))) {
            char p[400], real[PATH_MAX];   /* realpath() needs PATH_MAX (FORTIFY_SOURCE aborts otherwise) */
            double rem = 0, rot = 0;
            if (strncmp(e->d_name, "sd", 2)) continue;
            snprintf(p, sizeof p, "/sys/block/%s/removable", e->d_name);
            rd_dbl(p, &rem);
            snprintf(p, sizeof p, "/sys/block/%s", e->d_name);
            if (rem > 0 || !realpath(p, real) || strstr(real, "/usb")) continue;   /* USB drives draw from the port */
            snprintf(p, sizeof p, "/sys/block/%s/queue/rotational", e->d_name);
            rd_dbl(p, &rot);
            if (rot > 0) (*hdd)++; else (*ssd)++;
        }
        closedir(d);
    }
}

static int count_monitors(void)
{
    DIR *d = opendir("/sys/class/drm");
    struct dirent *e;
    int n = 0;
    if (!d) return 0;
    while ((e = readdir(d))) {
        char p[400], st[32];
        if (strncmp(e->d_name, "card", 4) || !strchr(e->d_name, '-') || strstr(e->d_name, "Writeback")) continue;
        snprintf(p, sizeof p, "/sys/class/drm/%s/status", e->d_name);
        if (rd_str(p, st, sizeof st) && !strcmp(st, "connected")) n++;
    }
    closedir(d);
    return n;
}

static void put(pm_part *p, int *n, int max, const char *name, int kind, int count, int on, double lo, double hi, const char *how)
{
    pm_part *x;
    if (*n >= max) return;
    x = &p[(*n)++];
    memset(x, 0, sizeof *x);
    snprintf(x->name, sizeof x->name, "%s", name);
    x->kind = kind; x->count = count; x->on = on; x->lo = lo; x->hi = hi;
    snprintf(x->how, sizeof x->how, "%s", how);
}

int psrc_default_parts(const psrc *s, pm_part *p, int max)
{
    int n = 0, ddr5 = count_hwmon("spd5118"), ddr4 = count_hwmon("jc42") + count_hwmon("ee1004");
    int nvme, hdd, ssd, fans = count_fans(), mons = count_monitors();
    char board[128] = "", name[96];
    rd_str("/sys/class/dmi/id/board_name", board, sizeof board);
    count_disks(&nvme, &hdd, &ssd);
    if (strstr(board, "X670") || strstr(board, "X870E"))
        put(p, &n, max, "Motherboard and chipset (two chipset chips)", PK_FIXED, 1, 1, 12, 30, "typical guess: about 7 W per chipset chip plus the board");
    else
        put(p, &n, max, "Motherboard and chipset", PK_FIXED, 1, 1, 8, 25, "typical guess: chipset, network, sound, controllers");
    put(p, &n, max, "CPU power stage losses (VRM)", PK_CPU_VRM, 1, 1, 8, 12, "assumption: 8-12 % of CPU power");
    put(p, &n, max, "CPU (only when it cannot be measured)", PK_CPU_EST, 1, 1, 15, 105, "typical desktop CPU, idle to busy");
    if (s->ngpu)
        put(p, &n, max, "Rest of the graphics card (fans, memory, power stages)", PK_GPU_REST, 1, 1, 5, 30,
            "assumption: the driver's number leaves part of the card out");
    if (s->dgpu_unread || s->ngpu)
        put(p, &n, max, "Graphics card (only when it cannot be measured)", PK_GPU_EST, 1, 1, 15, 250,
            s->ngpu ? "used only if the driver's reading stops; typical card, idle to busy" : "typical graphics card, idle to busy");
    if (ddr5) { snprintf(name, sizeof name, "Memory sticks (DDR5)"); put(p, &n, max, name, PK_FIXED, ddr5, 1, 1.5, 4, "typical guess per stick"); }
    else if (ddr4) put(p, &n, max, "Memory sticks (DDR4)", PK_FIXED, ddr4, 1, 1, 3, "typical guess per stick");
    else put(p, &n, max, "Memory sticks (number not detected)", PK_FIXED, 2, 1, 1, 4, "typical guess per stick; set the number");
    if (nvme) put(p, &n, max, "NVMe SSDs", PK_FIXED, nvme, 1, 0.05, 3, "datasheets: a few mW idle (APST), several W when busy");
    if (ssd) put(p, &n, max, "SATA SSDs", PK_FIXED, ssd, 1, 0.05, 2, "typical: tiny at idle, a few W when busy");
    if (hdd) put(p, &n, max, "Hard disks, spinning (3.5\")", PK_FIXED, hdd, 1, 4.5, 6.7, "spec sheets: 4.5-6.7 W idle, ~11 W busy (7200 rpm)");
    if (fans) {
        snprintf(name, sizeof name, "Fans and pump on the board's headers (%d spinning)", fans);
        put(p, &n, max, name, PK_FIXED, fans, 1, 0.5, 3, "typical 120/140 mm fan; a fan hub on one header can drive several");
    } else
        put(p, &n, max, "Fans (none detected: set the number)", PK_FIXED, 3, 1, 0.5, 3, "typical 120/140 mm fan");
    put(p, &n, max, "USB devices (keyboard, mouse, ...)", PK_FIXED, 1, 1, 0.5, 5, "typical guess");
    put(p, &n, max, "Lighting (RGB)", PK_FIXED, 1, 1, 0, 10, "your entry: none, some or lots");
    if (mons) put(p, &n, max, "Monitors (beside the PC, off by default)", PK_OUTSIDE, mons, 0, 15, 35,
                  "the energy label's on-mode power is best: type it in");
    return n;
}

static const pm_part g_presets[] = {
    { "Case fan 120 mm", PK_FIXED, 1, 1, 0.5, 2.5, "typical: 0.5-2.5 W (e.g. 1.68 W max for a Noctua NF-A12x25)" },
    { "Case fan 140 mm", PK_FIXED, 1, 1, 0.6, 3, "typical guess" },
    { "AIO liquid cooler pump", PK_FIXED, 1, 1, 2, 6, "typical guess" },
    { "AIO liquid cooler (be quiet!, pump + radiator fans)", PK_FIXED, 1, 1, 5, 20, "estimate: pump 3-5 W plus fans" },
    { "Case fan hub with ARGB lighting", PK_FIXED, 1, 1, 2, 5, "typical guess" },
    { "Memory stick DDR5", PK_FIXED, 1, 1, 1.5, 4, "typical guess" },
    { "Memory stick DDR4", PK_FIXED, 1, 1, 1, 3, "typical guess" },
    { "NVMe SSD", PK_FIXED, 1, 1, 0.05, 3, "datasheets" },
    { "SATA SSD", PK_FIXED, 1, 1, 0.05, 2, "typical" },
    { "Hard disk 3.5\" 7200 rpm", PK_FIXED, 1, 1, 4.5, 6.7, "spec sheets (idle)" },
    { "Hard disk 2.5\"", PK_FIXED, 1, 1, 0.7, 2, "typical" },
    { "Chipset chip (AMD Promontory 21)", PK_FIXED, 1, 1, 3, 7, "about 7 W at most per chip" },
    { "Wi-Fi / Bluetooth card", PK_FIXED, 1, 1, 0.5, 3, "typical guess" },
    { "Optical drive (idle)", PK_FIXED, 1, 1, 0.5, 2, "typical guess" },
    { "LED strip (RGB)", PK_FIXED, 1, 1, 2, 10, "depends on length and brightness" },
    { "Sound card / capture card", PK_FIXED, 1, 1, 2, 8, "typical guess" },
    { "Monitor 24\" (beside the PC)", PK_OUTSIDE, 1, 1, 12, 25, "energy label on-mode power" },
    { "Monitor 27\" (beside the PC)", PK_OUTSIDE, 1, 1, 18, 35, "energy label on-mode power" },
    { "Speakers (beside the PC)", PK_OUTSIDE, 1, 1, 2, 10, "typical guess" },
    { "Something else", PK_FIXED, 1, 1, 1, 5, "your entry" },
};
int psrc_preset_count(void) { return (int)(sizeof g_presets / sizeof g_presets[0]); }
const pm_part *psrc_preset(int i) { return i >= 0 && i < psrc_preset_count() ? &g_presets[i] : NULL; }
