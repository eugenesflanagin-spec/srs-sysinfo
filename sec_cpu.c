/* sec_cpu.c - SIV processor sections on Linux: [cpu] [cpus] [processors] [processor]
 * [features] [hypervisor] [present] [cpu-bits] [cpuid] [cpuid-000] [cpu-clocks] [cpu-speeds]
 * [cpu-volts] [cpu-levels] [cpu-timer] [microcode] [speculation] [numa] [nodes] [group]
 * [turbo-boost] [amd-cpu] [amd-smu] [cpu-smu] [amd-pmt*] [amd-tel-*] [amd-thm-*] [amd-tmp]
 * [amd-fus] [cpu-oc-data] [cpu-tweak] [msrs] [msr-setup] [msrs-ibm] [cpu-export]
 * Sources: CPUID (inline), /proc/cpuinfo, /sys/devices/system/cpu, cpufreq/amd_pstate,
 * cpuidle, /sys/devices/system/node, k10temp/zenergy hwmon, /sys/kernel/ryzen_smu_drv (read). */
#define _GNU_SOURCE
#include "sec.h"
#include "bench.h"

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#include <x86intrin.h>
#define SRS_X86 1
#else
/* non-x86 images: CPUID sections report that CPUID is an x86 instruction */
#define __cpuid_count(l, s, a, b, c, d) ((void)(l), (void)(s), (a) = (b) = (c) = (d) = 0)
static unsigned long long __rdtsc(void) { return 0; }
#endif
#include <math.h>
#include <sched.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define SYSCPU "/sys/devices/system/cpu"

static int ncpu(void) { long n = sysconf(_SC_NPROCESSORS_CONF); return n < 1 ? 1 : (int)n; }

static int arg_cpu(srs_out *o) { return o->arg && *o->arg ? atoi(o->arg) : 0; }

/* run CPUID on a given CPU by pinning the calling thread briefly */
static cpu_set_t g_saved;
static int pin_to(int cpu)
{
    cpu_set_t s;
    sched_getaffinity(0, sizeof g_saved, &g_saved);
    CPU_ZERO(&s);
    CPU_SET(cpu, &s);
    return sched_setaffinity(0, sizeof s, &s);
}
static void unpin(void) { sched_setaffinity(0, sizeof g_saved, &g_saved); }

static void cpuid(unsigned leaf, unsigned sub, unsigned r[4]) { __cpuid_count(leaf, sub, r[0], r[1], r[2], r[3]); }

/* ---------- /proc/cpuinfo ---------- */
typedef struct { char key[64], val[2048]; } kvp;
static int cpuinfo_cpu(int cpu, kvp *out, int max)
{
    char *txt, *p, *save;
    int cur = -1, n = 0;
    if (rd_all("/proc/cpuinfo", &txt) < 0) return 0;
    for (p = strtok_r(txt, "\n", &save); p; p = strtok_r(NULL, "\n", &save)) {
        char *c = strchr(p, ':'), k[64];
        if (!c) continue;
        snprintf(k, sizeof k, "%.*s", (int)(c - p), p);
        while (*k && (k[strlen(k) - 1] == ' ' || k[strlen(k) - 1] == '\t')) k[strlen(k) - 1] = 0;
        if (!strcmp(k, "processor")) cur = atoi(c + 1);
        if (cur == cpu && n < max) {
            snprintf(out[n].key, sizeof out[n].key, "%s", k);
            snprintf(out[n].val, sizeof out[n].val, "%s", c[1] == ' ' ? c + 2 : c + 1);
            n++;
        }
    }
    free(txt);
    return n;
}
static const char *kv_get(kvp *v, int n, const char *k) { int i; for (i = 0; i < n; i++) if (!strcmp(v[i].key, k)) return v[i].val; return ""; }
static int has_flag(const char *flags, const char *f)
{
    size_t l = strlen(f);
    const char *p = flags;
    while ((p = strstr(p, f))) {
        if ((p == flags || p[-1] == ' ') && (p[l] == ' ' || !p[l])) return 1;
        p += l;
    }
    return 0;
}
static char g_flags[4096];
static const char *flags(void)
{
    kvp v[40];
    int n;
    if (*g_flags) return g_flags;
    n = cpuinfo_cpu(0, v, 40);
    snprintf(g_flags, sizeof g_flags, "%s", kv_get(v, n, "flags"));
    return g_flags;
}

static void freq_str(int cpu, const char *file, char *b, size_t sz)
{
    long v;
    char p[256];
    snprintf(p, sizeof p, SYSCPU "/cpu%d/cpufreq/%s", cpu, file);
    if (rd_long(p, &v)) fmt_hz(v * 1000.0, b, sz); else snprintf(b, sz, "-");
}

static long topo(int cpu, const char *f)
{
    long v = -1;
    char p[256];
    snprintf(p, sizeof p, SYSCPU "/cpu%d/topology/%s", cpu, f);
    rd_long(p, &v);
    return v;
}

static int apicid(int cpu) { kvp v[40]; int n = cpuinfo_cpu(cpu, v, 40); const char *a = kv_get(v, n, "apicid"); return *a ? atoi(a) : -1; }

static int cpu_is_primary_thread(int cpu)
{
    char b[64];
    rd_fmt(b, sizeof b, SYSCPU "/cpu%d/topology/thread_siblings_list", cpu);
    return atoi(b) == cpu;
}

/* ======================================================================
 * [cpus] summary
 * ====================================================================== */
static void s_cpus(srs_out *o)
{
    static const int tabs[] = { 170 };
    kvp v[40];
    int n = cpuinfo_cpu(0, v, 40), cores = 0, i, pk = 0;
    char b[256];
    for (i = 0; i < ncpu(); i++) { cores += cpu_is_primary_thread(i); if (topo(i, "physical_package_id") + 1 > pk) pk = (int)topo(i, "physical_package_id") + 1; }
    so_siv_header(o, "CPU Summary");
    so_tabs(o, tabs, 1);
    so_kv(o, "Processor", C_BLACK, kv_get(v, n, "model name"));
    so_kv(o, "Vendor", C_VALUE, kv_get(v, n, "vendor_id"));
    snprintf(b, sizeof b, "%s (0x%X)  Model %s (0x%X)  Stepping %s", kv_get(v, n, "cpu family"), atoi(kv_get(v, n, "cpu family")),
             kv_get(v, n, "model"), atoi(kv_get(v, n, "model")), kv_get(v, n, "stepping"));
    so_kv(o, "Family", C_VALUE, b);
    snprintf(b, sizeof b, "%d package%s, %d cores, %d logical processors (%d per core)", pk, pk == 1 ? "" : "s", cores, ncpu(),
             cores ? ncpu() / cores : 1);
    so_kv(o, "Topology", C_TEAL, b);
    so_kv(o, "Microcode", C_PURPLE, kv_get(v, n, "microcode"));
    so_kv(o, "Address sizes", C_VALUE, kv_get(v, n, "address sizes"));
    so_kv(o, "BogoMIPS", C_VALUE, kv_get(v, n, "bogomips"));
    so_kv(o, "TLB size", C_VALUE, kv_get(v, n, "TLB size"));
    so_kv(o, "Power management", C_VALUE, kv_get(v, n, "power management"));
    rd_str(SYSCPU "/cpu0/cpufreq/scaling_driver", b, sizeof b);
    so_kv(o, "Frequency driver", C_ORANGE, b);
    so_board_footer(o);
}

/* ======================================================================
 * [cpu]  per-CPU table (SIV's xxb CPU table, Linux columns)
 * ====================================================================== */
static void s_cpu(srs_out *o)
{
    static const int tabs[] = { 60, 95, 130, 165, 200, 245, 385, 435, 510, 585, 660, 740, 830, 900 };
    static const char *const h[] = { "CPU", "Node", "Pkg", "Die", "Core", "Thread", "Affinity", "APIC", "Current", "Minimum",
                                     "Maximum", "Governor", "EPP", "Ranking", "Highest" };
    int i;
    so_tabs(o, tabs, 14);
    so_row(o, 15, h, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL,
                                    C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < ncpu(); i++) {
        char c[15][64], node[32] = "0", **nd;
        const char *cells[15];
        int nn = dir_list(SYSCPU "/cpu0", &nd), j;
        unsigned long long mask = i < 64 ? 1ULL << i : 0;
        dir_free(nd, nn);
        {   /* node: /sys/devices/system/cpu/cpuN/nodeM */
            char d[128], **e;
            int ne, k;
            snprintf(d, sizeof d, SYSCPU "/cpu%d", i);
            ne = dir_list(d, &e);
            for (k = 0; k < ne; k++) if (!strncmp(e[k], "node", 4)) snprintf(node, sizeof node, "%s", e[k] + 4);
            dir_free(e, ne);
        }
        snprintf(c[0], 64, "CPU-%d", i);
        snprintf(c[1], 64, "%s", node);
        snprintf(c[2], 64, "%ld", topo(i, "physical_package_id"));
        snprintf(c[3], 64, "%ld", topo(i, "die_id"));
        snprintf(c[4], 64, "%ld", topo(i, "core_id"));
        snprintf(c[5], 64, "%s", cpu_is_primary_thread(i) ? "0" : "1");
        snprintf(c[6], 64, "%016llX", mask);
        snprintf(c[7], 64, "%04X", apicid(i) < 0 ? 0 : apicid(i));
        freq_str(i, "scaling_cur_freq", c[8], 64);
        freq_str(i, "scaling_min_freq", c[9], 64);
        freq_str(i, "scaling_max_freq", c[10], 64);
        rd_fmt(c[11], 64, SYSCPU "/cpu%d/cpufreq/scaling_governor", i);
        rd_fmt(c[12], 64, SYSCPU "/cpu%d/cpufreq/energy_performance_preference", i);
        rd_fmt(c[13], 64, SYSCPU "/cpu%d/cpufreq/amd_pstate_prefcore_ranking", i);
        rd_fmt(c[14], 64, SYSCPU "/cpu%d/cpufreq/amd_pstate_highest_perf", i);
        for (j = 0; j < 15; j++) cells[j] = *c[j] ? c[j] : "-";
        so_row(o, 15, cells, (const int[]){ C_LABEL, C_VALUE, C_VALUE, C_VALUE, C_VALUE, C_VALUE, C_PURPLE, C_PURPLE, C_BLACK,
                                            C_GRAY, C_GRAY, C_TEAL, C_TEAL, C_ORANGE, C_ORANGE });
    }
}

/* ======================================================================
 * [processors]  capabilities + per-CPU APIC/affinity table (SIV layout)
 * ====================================================================== */
static void s_processors(srs_out *o)
{
    static const int tabs[] = { 30, 220, 260, 280, 420, 470, 560, 620 };
    static const int t2[] = { 150, 210, 250, 280, 310, 350, 520, 590, 660 };
    int i, cores = 0, pk = 0;
    char b[256];
    for (i = 0; i < ncpu(); i++) { cores += cpu_is_primary_thread(i); if (topo(i, "physical_package_id") + 1 > pk) pk = (int)topo(i, "physical_package_id") + 1; }
    so_siv_header(o, "CPUs");
    so_tabs(o, tabs, 8);
    so_t(o, C_LABEL, "  Capabilities"); so_par(o); so_par(o);
    so_t(o, C_DEF, "  "); so_check(o, pk > 1); so_t(o, C_VALUE, "Multi-processor System"); so_tab(o); so_f(o, C_BLACK, "%d", pk); so_tab(o); so_t(o, C_PURPLE, "|P| physical package"); so_par(o);
    {
        unsigned char is_e[1024];
        int ne = bench_ecores(is_e, ncpu() > 1024 ? 1024 : ncpu());
        so_t(o, C_DEF, "  "); so_check(o, ne > 0); so_t(o, C_VALUE, "Hybrid-processor System"); so_tab(o); so_f(o, C_BLACK, "%d", ne); so_tab(o);
        so_t(o, C_PURPLE, "|e| efficiency logical processors (/sys/devices/cpu_atom)"); so_par(o);
    }
    so_t(o, C_DEF, "  "); so_check(o, cores > 1); so_t(o, C_VALUE, "Multi-core Processor"); so_tab(o); so_f(o, C_BLACK, "%d", cores); so_tab(o);
    so_t(o, C_PURPLE, "|C| processing cores"); so_tab(o); so_f(o, C_BLACK, "%d  processing cores per package", pk ? cores / pk : cores); so_par(o);
    so_t(o, C_DEF, "  "); so_check(o, ncpu() > cores); so_t(o, C_VALUE, "Multi-threading Cores"); so_tab(o); so_f(o, C_BLACK, "%d", ncpu()); so_tab(o);
    so_t(o, C_PURPLE, "|L| logical processors"); so_tab(o); so_f(o, C_BLACK, "%d  logical processors per core", cores ? ncpu() / cores : 1); so_par(o);
    so_par(o);
    snprintf(b, sizeof b, "    All CPU Cores and Logical processors present on %s", sys_board());
    so_t(o, C_TEAL, b); so_par(o);
    so_t(o, C_TEAL, "    Relationships between the 3-level sub-IDs (package.core.thread), APIC and the Linux CPU number and affinity mask");
    so_par(o); so_par(o);
    so_tabs(o, t2, 9);
    so_row(o, 10, (const char *const[]){ "Processors", "Sub-ID", "APIC", "Node", "Pkg", "N", "CPU Affinity Mask", "Speed", "ACPI", "CPU Device" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < ncpu(); i++) {
        char id[64], ap[16], spd[32], mask[32], acpi[256] = "-", dev[256] = "-", nm[32], pkg[16], num[16], node[16] = "0";
        int prim = cpu_is_primary_thread(i);
        snprintf(nm, sizeof nm, "%sCPU-%d", i == 0 ? "|P|" : prim ? "|C|" : "|L|", i);
        snprintf(id, sizeof id, "%ld.%ld.%d", topo(i, "physical_package_id"), topo(i, "core_id"), prim ? 0 : 1);
        snprintf(ap, sizeof ap, "%04X", apicid(i) < 0 ? 0 : apicid(i));
        snprintf(mask, sizeof mask, "%016llX", i < 64 ? 1ULL << i : 0ULL);
        freq_str(i, "scaling_cur_freq", spd, sizeof spd);
        snprintf(pkg, sizeof pkg, "%ld", topo(i, "physical_package_id"));
        snprintf(num, sizeof num, "%d", i);
        {   /* NUMA node: the cpuN/nodeM link */
            char p[128], **e;
            int ne = (snprintf(p, sizeof p, SYSCPU "/cpu%d", i), dir_list(p, &e)), k;
            for (k = 0; k < ne; k++) if (!strncmp(e[k], "node", 4) && isdigit((unsigned char)e[k][4])) snprintf(node, sizeof node, "%s", e[k] + 4);
            dir_free(e, ne);
        }
        {
            char p[256], link[512];
            ssize_t l;
            snprintf(p, sizeof p, SYSCPU "/cpu%d/firmware_node", i);
            if ((l = readlink(p, link, sizeof link - 1)) > 0) { link[l] = 0; snprintf(acpi, sizeof acpi, "%s", strrchr(link, '/') ? strrchr(link, '/') + 1 : link); }
            snprintf(p, sizeof p, SYSCPU "/cpu%d/firmware_node/path", i);
            rd_str(p, dev, sizeof dev);
        }
        so_row(o, 10, (const char *const[]){ nm, id, ap, node, pkg, num, mask, spd, acpi, dev },
               (const int[]){ prim ? C_LABEL : C_TEAL, C_VALUE, C_PURPLE, C_VALUE, C_VALUE, C_VALUE, C_PURPLE, C_BLACK, C_GRAY, C_GRAY });
    }
    so_board_footer(o);
}

/* ======================================================================
 * caches
 * ====================================================================== */
typedef struct { int level; char type[24], size[24], ways[16], line[16], sets[16], shared[64]; } cacheinfo;
static int caches(int cpu, cacheinfo *c, int max)
{
    int n = 0, i;
    for (i = 0; i < 8 && n < max; i++) {
        char d[256], b[64];
        snprintf(d, sizeof d, SYSCPU "/cpu%d/cache/index%d", cpu, i);
        if (!path_exists(d)) break;
        snprintf(b, sizeof b, "%s/level", d);
        { long v; c[n].level = rd_long(b, &v) ? (int)v : 0; }
        rd_fmt(c[n].type, sizeof c[n].type, "%s/type", d);
        rd_fmt(c[n].size, sizeof c[n].size, "%s/size", d);
        rd_fmt(c[n].ways, sizeof c[n].ways, "%s/ways_of_associativity", d);
        rd_fmt(c[n].line, sizeof c[n].line, "%s/coherency_line_size", d);
        rd_fmt(c[n].sets, sizeof c[n].sets, "%s/number_of_sets", d);
        rd_fmt(c[n].shared, sizeof c[n].shared, "%s/shared_cpu_list", d);
        n++;
    }
    return n;
}

static int count_list(const char *l)   /* "0-7,9" -> 9 */
{
    int n = 0;
    char tmp[256], *t, *save;
    snprintf(tmp, sizeof tmp, "%s", l);
    for (t = strtok_r(tmp, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
        int a, b;
        if (sscanf(t, "%d-%d", &a, &b) == 2) n += b - a + 1; else n++;
    }
    return n;
}

static void cache_name(const cacheinfo *c, char *b, size_t sz)
{
    snprintf(b, sz, "L%d %s Cache", c->level, !strcmp(c->type, "Instruction") ? "Code" : !strcmp(c->type, "Data") ? "Data" : "Unified");
}

/* [processor] (arg = CPU number, default all) */
static void processor_block(srs_out *o, int cpu)
{
    kvp v[40];
    int n = cpuinfo_cpu(cpu, v, 40), nc, k, prim = cpu_is_primary_thread(cpu);
    cacheinfo c[8];
    char spd[32], b[512];
    freq_str(cpu, "scaling_cur_freq", spd, sizeof spd);
    snprintf(b, sizeof b, "CPU-%-3d", cpu);
    so_b(o, C_LABEL, b);
    so_t(o, C_BLACK, kv_get(v, n, "model name"));
    snprintf(b, sizeof b, "   Family 0x%X Model 0x%X Stepping %s  APIC %ld.%ld.%d (%s)", atoi(kv_get(v, n, "cpu family")),
             atoi(kv_get(v, n, "model")), kv_get(v, n, "stepping"), topo(cpu, "physical_package_id"), topo(cpu, "core_id"),
             prim ? 0 : 1, prim ? "core" : "logical");
    so_t(o, C_TEAL, b);
    so_par(o);
    so_t(o, C_DEF, "  "); so_t(o, C_BLACK, spd); so_t(o, C_VALUE, " Processor"); so_par(o);
    if (prim) {
        nc = caches(cpu, c, 8);
        for (k = 0; k < nc; k++) {
            char nm[64];
            int sh = count_list(c[k].shared);
            cache_name(&c[k], nm, sizeof nm);
            so_t(o, C_DEF, "  "); so_t(o, C_VALUE, nm);
            so_tab(o);
            {
                char sz[32], inst[16] = "";
                fmt_bytes(atof(c[k].size) * 1024, sz, sizeof sz);
                if (cpu == 0 && sh) snprintf(inst, sizeof inst, "%d x ", ncpu() / sh);
                snprintf(b, sizeof b, "%s-way %s-byte  %s%s  %s sets  shared by %d CPU%s (%s)", c[k].ways, c[k].line, inst, sz,
                         c[k].sets, sh, sh == 1 ? "" : "s", c[k].shared);
            }
            so_t(o, C_BLACK, b);
            so_par(o);
        }
        {
            const char *f = kv_get(v, n, "flags");
            snprintf(b, sizeof b, "  %s  [%s%s%s%s%s%s]", kv_get(v, n, "vendor_id"), has_flag(f, "mmxext") ? "MMX+ " : "MMX ",
                     has_flag(f, "sse4_2") ? "SSE4.2 " : "", has_flag(f, "nx") ? "XD " : "", has_flag(f, "svm") ? "SVM " : has_flag(f, "vmx") ? "VMX " : "",
                     has_flag(f, "avx512f") ? "AVX512" : has_flag(f, "avx2") ? "AVX2" : "", "");
            so_t(o, C_PURPLE, b);
            so_par(o);
        }
    }
}

static void s_processor(srs_out *o)
{
    static const int tabs[] = { 200 };
    int i;
    so_tabs(o, tabs, 1);
    if (o->arg && *o->arg) { processor_block(o, arg_cpu(o)); return; }
    for (i = 0; i < ncpu(); i++) processor_block(o, i);
}

/* [cpu-levels] - cache levels (SIV "Cache-n" buttons; arg = CPU) + idle states */
static void s_cache(srs_out *o)
{
    static const int tabs[] = { 160, 230, 300, 380, 460, 560 };
    int cpu = arg_cpu(o), n, k;
    cacheinfo c[8];
    char t[96];
    snprintf(t, sizeof t, "Cache Levels of CPU-%d", cpu);
    so_siv_header(o, t);
    so_tabs(o, tabs, 6);
    so_row(o, 7, (const char *const[]){ "Cache", "Size", "Ways", "Line", "Sets", "Shared by", "CPUs" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    n = caches(cpu, c, 8);
    for (k = 0; k < n; k++) {
        char nm[64], sh[16];
        cache_name(&c[k], nm, sizeof nm);
        snprintf(sh, sizeof sh, "%d", count_list(c[k].shared));
        so_row(o, 7, (const char *const[]){ nm, c[k].size, c[k].ways, c[k].line, c[k].sets, sh, c[k].shared },
               (const int[]){ C_VALUE, C_BLACK, C_TEAL, C_TEAL, C_GRAY, C_ORANGE, C_PURPLE });
    }
    so_par(o);
    so_note(o, "Cache latency per level is measured by the Latency tool (Benchmarks & stress > Latency).");
}

/* [cpu-levels]: C-states (cpuidle) */
static void s_cstates(srs_out *o)
{
    static const int tabs[] = { 70, 150, 360, 450, 540, 640 };
    int i, s;
    char drv[64] = "", gov[64] = "";
    so_siv_header(o, "CPU Idle Levels (C-states)");
    rd_str(SYSCPU "/cpuidle/current_driver", drv, sizeof drv);
    rd_str(SYSCPU "/cpuidle/current_governor", gov, sizeof gov);
    so_tabs(o, tabs, 6);
    so_kvf(o, "Driver", C_ORANGE, "%s   governor %s", drv, gov);
    so_par(o);
    so_row(o, 7, (const char *const[]){ "CPU", "State", "Description", "Latency", "Residency", "Usage", "Time" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < ncpu(); i++) {
        for (s = 0; s < 16; s++) {
            char d[128], nm[32], desc[96], lat[32], res[32], use[32], tm[32], cpu[16];
            snprintf(d, sizeof d, SYSCPU "/cpu%d/cpuidle/state%d", i, s);
            if (!path_exists(d)) break;
            rd_fmt(nm, sizeof nm, "%s/name", d);
            rd_fmt(desc, sizeof desc, "%s/desc", d);
            rd_fmt(lat, sizeof lat, "%s/latency", d);
            rd_fmt(res, sizeof res, "%s/residency", d);
            rd_fmt(use, sizeof use, "%s/usage", d);
            rd_fmt(tm, sizeof tm, "%s/time", d);
            { double us = atof(tm); snprintf(tm, sizeof tm, "%.1fs", us / 1e6); }
            strcat(lat, "us"); strcat(res, "us");
            if (s) cpu[0] = 0;
            else snprintf(cpu, sizeof cpu, "CPU-%d", i);
            so_row(o, 7, (const char *const[]){ cpu, nm, desc, lat, res, use, tm },
                   (const int[]){ C_LABEL, C_VALUE, C_GRAY, C_TEAL, C_TEAL, C_BLACK, C_BLACK });
        }
    }
}

/* ======================================================================
 * [features] CPUID decode
 * ====================================================================== */
typedef struct { unsigned leaf, sub; char reg; int bit; const char *name, *desc; } feat;
static const feat feats[] = {
    { 1, 0, 'd', 0, "FPU", "Floating Point Unit" }, { 1, 0, 'd', 1, "VME", "Virtual 8086 Mode Enhancements" },
    { 1, 0, 'd', 2, "DE", "Debugging Extension" }, { 1, 0, 'd', 3, "PSE", "Page Size Extension" },
    { 1, 0, 'd', 4, "TSC", "Time Stamp Counter" }, { 1, 0, 'd', 5, "MSR", "Machine Specific Registers" },
    { 1, 0, 'd', 6, "PAE", "Physical Address Extension" }, { 1, 0, 'd', 7, "MCE", "Machine Check Exception" },
    { 1, 0, 'd', 8, "CX8", "CMPXCHG8B Instruction" }, { 1, 0, 'd', 9, "APIC", "On-chip APIC Hardware" },
    { 1, 0, 'd', 11, "SEP", "Fast System Call" }, { 1, 0, 'd', 12, "MTRR", "Memory Type Range Registers" },
    { 1, 0, 'd', 13, "PGE", "Page Global Enable" }, { 1, 0, 'd', 14, "MCA", "Machine Check Architecture" },
    { 1, 0, 'd', 15, "CMOV", "Conditional Move Instruction" }, { 1, 0, 'd', 16, "PAT", "Page Attribute Table" },
    { 1, 0, 'd', 17, "PSE36", "Page Size Extensions (36-bit)" }, { 1, 0, 'd', 18, "PSN", "Processor Serial Number" },
    { 1, 0, 'd', 19, "CLFSH", "CLFLUSH Instruction" }, { 1, 0, 'd', 21, "DS", "Debug Store" },
    { 1, 0, 'd', 22, "ACPI", "Thermal Monitor and Software Clock Control" }, { 1, 0, 'd', 23, "MMX", "MMX Technology" },
    { 1, 0, 'd', 24, "FXSR", "FXSAVE and FXRSTOR Instructions" }, { 1, 0, 'd', 25, "SSE", "Streaming SIMD Extensions" },
    { 1, 0, 'd', 26, "SSE2", "Streaming SIMD Extensions 2" }, { 1, 0, 'd', 27, "SS", "Self-Snoop" },
    { 1, 0, 'd', 28, "HTT", "Multi-Threading" }, { 1, 0, 'd', 29, "TM", "Thermal Monitor" },
    { 1, 0, 'd', 31, "PBE", "Pending Break Enable" },
    { 1, 0, 'c', 0, "SSE3", "Streaming SIMD Extensions 3" }, { 1, 0, 'c', 1, "PCLMULQDQ", "Carry-less Multiplication" },
    { 1, 0, 'c', 2, "DTES64", "64-bit Debug Store" }, { 1, 0, 'c', 3, "MONITOR", "MONITOR and MWAIT Instructions" },
    { 1, 0, 'c', 4, "DS-CPL", "CPL Qualified Debug Store" }, { 1, 0, 'c', 5, "VMX", "Intel Virtual Machine Extensions" },
    { 1, 0, 'c', 6, "SMX", "Safer Mode Extensions" }, { 1, 0, 'c', 7, "EIST", "Enhanced SpeedStep Technology" },
    { 1, 0, 'c', 8, "TM2", "Thermal Monitor 2" }, { 1, 0, 'c', 9, "SSSE3", "Supplemental SSE3" },
    { 1, 0, 'c', 10, "CNXT-ID", "L1 Context ID" }, { 1, 0, 'c', 11, "SDBG", "Silicon Debug Interface" },
    { 1, 0, 'c', 12, "FMA", "Fused Multiply Add" }, { 1, 0, 'c', 13, "CX16", "CMPXCHG16B Instruction" },
    { 1, 0, 'c', 14, "xTPR", "xTPR Update Control" }, { 1, 0, 'c', 15, "PDCM", "Perfmon and Debug Capability" },
    { 1, 0, 'c', 17, "PCID", "Process Context Identifiers" }, { 1, 0, 'c', 18, "DCA", "Direct Cache Access" },
    { 1, 0, 'c', 19, "SSE4.1", "Streaming SIMD Extensions 4.1" }, { 1, 0, 'c', 20, "SSE4.2", "Streaming SIMD Extensions 4.2" },
    { 1, 0, 'c', 21, "x2APIC", "x2APIC Support" }, { 1, 0, 'c', 22, "MOVBE", "MOVBE Instruction" },
    { 1, 0, 'c', 23, "POPCNT", "POPCNT Instruction" }, { 1, 0, 'c', 24, "TSC-DL", "TSC Deadline Timer" },
    { 1, 0, 'c', 25, "AES", "Advanced Encryption Standard" }, { 1, 0, 'c', 26, "XSAVE", "XSAVE/XRSTOR Instructions" },
    { 1, 0, 'c', 27, "OSXSAVE", "CR4.OSXSAVE Read-Only Copy" }, { 1, 0, 'c', 28, "AVX", "Advanced Vector Extensions" },
    { 1, 0, 'c', 29, "F16C", "Half Precision Convert Instructions" }, { 1, 0, 'c', 30, "RDRAND", "RDRAND Instruction" },
    { 1, 0, 'c', 31, "HVM", "Hypervisor Virtual Machine" },
    { 6, 0, 'a', 0, "DTS", "Digital Thermal Sensor" }, { 6, 0, 'a', 1, "TBT", "Turbo Boost Technology" },
    { 6, 0, 'a', 2, "ARAT", "Always Running APIC Timer" }, { 6, 0, 'c', 0, "PERF", "IA32_MPERF and IA32_APERF" },
    { 7, 0, 'b', 0, "FSGSBASE", "RD/WR FSGSBASE Instructions" }, { 7, 0, 'b', 1, "TSC_ADJ", "IA32_TSC_ADJUST MSR" },
    { 7, 0, 'b', 2, "SGX", "Software Guard Extensions" }, { 7, 0, 'b', 3, "BMI1", "ANDN BEXTR BLSI BLSMK BLSR TZCNT Instructions" },
    { 7, 0, 'b', 4, "HLE", "Hardware Lock Elision" }, { 7, 0, 'b', 5, "AVX2", "Advanced Vector Extensions 2" },
    { 7, 0, 'b', 7, "SMEP", "Supervisor Mode Execution Prevention" }, { 7, 0, 'b', 8, "BMI2", "BZHI MULX PDEP PEXT RORX SARX SHLX SHRX" },
    { 7, 0, 'b', 9, "ERMS", "Enhanced REP MOVSB/STOSB" }, { 7, 0, 'b', 10, "INVPCID", "INVPCID Instruction" },
    { 7, 0, 'b', 11, "RTM", "Restricted Transactional Memory" }, { 7, 0, 'b', 12, "PQM", "Platform QoS Monitoring" },
    { 7, 0, 'b', 15, "PQE", "Platform QoS Enforcement" }, { 7, 0, 'b', 16, "AVX512F", "AVX-512 Foundation" },
    { 7, 0, 'b', 17, "AVX512DQ", "AVX-512 Doubleword and Quadword" }, { 7, 0, 'b', 18, "RDSEED", "RDSEED Instruction" },
    { 7, 0, 'b', 19, "ADX", "Multi-Precision Add-Carry" }, { 7, 0, 'b', 20, "SMAP", "Supervisor Mode Access Prevention" },
    { 7, 0, 'b', 21, "AVX512IFMA", "AVX-512 Integer Fused Multiply-Add" }, { 7, 0, 'b', 23, "CLFLUSHOPT", "CLFLUSHOPT Instruction" },
    { 7, 0, 'b', 24, "CLWB", "Cache Line Write Back" }, { 7, 0, 'b', 26, "AVX512PF", "AVX-512 Prefetch" },
    { 7, 0, 'b', 27, "AVX512ER", "AVX-512 Exponential and Reciprocal" }, { 7, 0, 'b', 28, "AVX512CD", "AVX-512 Conflict Detection" },
    { 7, 0, 'b', 29, "SHA", "SHA Extensions" }, { 7, 0, 'b', 30, "AVX512BW", "AVX-512 Byte and Word" },
    { 7, 0, 'b', 31, "AVX512VL", "AVX-512 Vector Length" },
    { 7, 0, 'c', 1, "AVX512VBMI", "AVX-512 Vector Bit Manipulation" }, { 7, 0, 'c', 2, "UMIP", "User Mode Instruction Prevention" },
    { 7, 0, 'c', 3, "PKU", "Memory Protection Keys" }, { 7, 0, 'c', 4, "OSPKE", "OS has set CR4.PKE" },
    { 7, 0, 'c', 6, "AVX512VBMI2", "AVX-512 Vector Bit Manipulation 2" }, { 7, 0, 'c', 7, "CET_SS", "CET Shadow Stack" },
    { 7, 0, 'c', 8, "GFNI", "Galois Field Instructions" }, { 7, 0, 'c', 9, "VAES", "Vector AES" },
    { 7, 0, 'c', 10, "VPCLMULQDQ", "Vector Carry-less Multiplication" }, { 7, 0, 'c', 11, "AVX512VNNI", "AVX-512 Vector Neural Network" },
    { 7, 0, 'c', 12, "AVX512BITALG", "AVX-512 BITALG Instructions" }, { 7, 0, 'c', 14, "AVX512VPOPCNTDQ", "AVX-512 Vector Population Count" },
    { 7, 0, 'c', 16, "LA57", "5-level Paging" }, { 7, 0, 'c', 22, "RDPID", "RDPID and IA32_TSC_AUX" },
    { 7, 0, 'c', 27, "MOVDIRI", "MOVDIRI Instruction" }, { 7, 0, 'c', 28, "MOVDIR64B", "MOVDIR64B Instruction" },
    { 7, 0, 'd', 2, "AVX512-4VNNIW", "AVX-512 4-register Neural Network" }, { 7, 0, 'd', 3, "AVX512-4FMAPS", "AVX-512 4-register FMA Single" },
    { 7, 0, 'd', 4, "FSRM", "Fast Short REP MOV" }, { 7, 0, 'd', 8, "AVX512-VP2I", "AVX-512 VP2 Intersect" },
    { 7, 0, 'd', 10, "MD_CLEAR", "VERW Clears CPU Buffers" }, { 7, 0, 'd', 14, "SERIALIZE", "SERIALIZE Instruction" },
    { 7, 0, 'd', 15, "HYBRID", "Hybrid Processor" }, { 7, 0, 'd', 18, "PCONFIG", "PCONFIG Instruction" },
    { 7, 0, 'd', 20, "CET_IBT", "CET Indirect Branch Tracking" }, { 7, 0, 'd', 26, "IBRS", "Indirect Branch Restricted Speculation" },
    { 7, 0, 'd', 27, "STIBP", "Single Thread Indirect Branch Predictor" }, { 7, 0, 'd', 28, "L1D_FLUSH", "Level 1 Data Cache Flush" },
    { 7, 0, 'd', 29, "IA32_AC", "IA32_ARCH_CAPABILITIES MSR" }, { 7, 0, 'd', 31, "SSBD", "Speculative Store Bypass Disable" },
    { 7, 1, 'a', 4, "AVX-VNNI", "AVX Vector Neural Network" }, { 7, 1, 'a', 5, "AVX512-BF16", "BFloat16 Vector Neural Network" },
    { 0x80000001, 0, 'c', 0, "LAHF64", "LAHF/SAHF in 64-bit Mode" }, { 0x80000001, 0, 'c', 1, "CMP_LEGACY", "Core Multi-Processing Legacy Mode" },
    { 0x80000001, 0, 'c', 2, "SVM", "AMD Secure Virtual Machine" }, { 0x80000001, 0, 'c', 3, "EXTAPIC", "Extended APIC Space" },
    { 0x80000001, 0, 'c', 4, "CR8L", "LOCK MOV CR0 means MOV CR8" }, { 0x80000001, 0, 'c', 5, "ABM", "Advanced Bit Manipulation (LZCNT)" },
    { 0x80000001, 0, 'c', 6, "SSE4A", "SSE4A Instructions" }, { 0x80000001, 0, 'c', 7, "MISALIGNSSE", "Misaligned SSE Mode" },
    { 0x80000001, 0, 'c', 8, "3DNOWPREF", "PREFETCH and PREFETCHW" }, { 0x80000001, 0, 'c', 9, "OSVW", "OS Visible Workaround" },
    { 0x80000001, 0, 'c', 10, "IBS", "Instruction Based Sampling" }, { 0x80000001, 0, 'c', 11, "XOP", "Extended Operations" },
    { 0x80000001, 0, 'c', 12, "SKINIT", "SKINIT and STGI" }, { 0x80000001, 0, 'c', 13, "WDT", "Watchdog Timer" },
    { 0x80000001, 0, 'c', 15, "LWP", "Light Weight Profiling" }, { 0x80000001, 0, 'c', 16, "FMA4", "4-operand FMA" },
    { 0x80000001, 0, 'c', 17, "TCE", "Translation Cache Extension" }, { 0x80000001, 0, 'c', 21, "TBM", "Trailing Bit Manipulation" },
    { 0x80000001, 0, 'c', 22, "TOPOEXT", "Topology Extensions" }, { 0x80000001, 0, 'c', 23, "PERFCTR_CORE", "Core Performance Counter Extensions" },
    { 0x80000001, 0, 'c', 24, "PERFCTR_NB", "NB Performance Counter Extensions" }, { 0x80000001, 0, 'c', 26, "BPEXT", "Data Breakpoint Extensions" },
    { 0x80000001, 0, 'c', 27, "PTSC", "Performance Time-Stamp Counter" }, { 0x80000001, 0, 'c', 28, "PERFCTR_LLC", "L2I Performance Counter Extensions" },
    { 0x80000001, 0, 'c', 29, "MWAITX", "MONITORX and MWAITX" },
    { 0x80000001, 0, 'd', 11, "SYSCALL", "SYSCALL and SYSRET" }, { 0x80000001, 0, 'd', 20, "NX", "No-Execute Page Protection (XD)" },
    { 0x80000001, 0, 'd', 22, "MMXEXT", "AMD MMX Extensions (MMX+)" }, { 0x80000001, 0, 'd', 25, "FFXSR", "Fast FXSAVE/FXRSTOR" },
    { 0x80000001, 0, 'd', 26, "PAGE1GB", "1GB Pages" }, { 0x80000001, 0, 'd', 27, "RDTSCP", "RDTSCP Instruction" },
    { 0x80000001, 0, 'd', 29, "LM", "Long Mode (AMD64 / x86-64)" }, { 0x80000001, 0, 'd', 30, "3DNOWEXT", "3DNow! Extensions" },
    { 0x80000001, 0, 'd', 31, "3DNOW", "3DNow! Instructions" },
    { 0x80000007, 0, 'd', 8, "INVTSC", "Invariant TSC" }, { 0x80000007, 0, 'd', 9, "CPB", "Core Performance Boost" },
    { 0x80000007, 0, 'd', 12, "PROC_FEEDBACK", "Processor Feedback Interface" },
    { 0x80000008, 0, 'b', 0, "CLZERO", "CLZERO Instruction" }, { 0x80000008, 0, 'b', 4, "RDPRU", "RDPRU Instruction" },
    { 0x80000008, 0, 'b', 9, "WBNOINVD", "WBNOINVD Instruction" }, { 0x80000008, 0, 'b', 12, "AMD_IBPB", "Indirect Branch Prediction Barrier" },
    { 0x80000008, 0, 'b', 14, "AMD_IBRS", "Indirect Branch Restricted Speculation" }, { 0x80000008, 0, 'b', 15, "AMD_STIBP", "Single Thread Indirect Branch Predictor" },
    { 0x80000008, 0, 'b', 24, "AMD_SSBD", "Speculative Store Bypass Disable" }, { 0x80000008, 0, 'b', 26, "AMD_SSB_NO", "Not affected by Speculative Store Bypass" },
    { 0x80000008, 0, 'b', 27, "CPPC", "Collaborative Processor Performance Control" },
    { 0x8000000A, 0, 'd', 0, "NPT", "Nested Paging (AMD RVI)" }, { 0x8000000A, 0, 'd', 3, "NRIPS", "Next RIP Save" },
    { 0x8000000A, 0, 'd', 13, "AVIC", "AMD Virtual Interrupt Controller" }, { 0x8000000A, 0, 'd', 16, "VGIF", "Virtual GIF" },
    { 0x8000001F, 0, 'a', 0, "SME", "Secure Memory Encryption" }, { 0x8000001F, 0, 'a', 1, "SEV", "Secure Encrypted Virtualization" },
    { 0x8000001F, 0, 'a', 3, "SEV-ES", "SEV Encrypted State" }, { 0x8000001F, 0, 'a', 4, "SEV-SNP", "SEV Secure Nested Paging" },
    { 0, 0, 0, 0, NULL, NULL }
};

static int feat_on(const feat *f, unsigned maxb, unsigned maxe)
{
    unsigned r[4];
    if (f->leaf < 0x80000000 ? f->leaf > maxb : f->leaf > maxe) return 0;
    cpuid(f->leaf, f->sub, r);
    return (int)((f->reg == 'a' ? r[0] : f->reg == 'b' ? r[1] : f->reg == 'c' ? r[2] : r[3]) >> f->bit & 1);
}

static void s_features(srs_out *o)
{
    static const int tabs[] = { 110, 420, 530, 840 };
    unsigned r[4], maxb, maxe;
    int i, n = 0, on = 0;
    cpuid(0, 0, r); maxb = r[0];
    cpuid(0x80000000, 0, r); maxe = r[0];
    for (n = 0; feats[n].name; n++) on += feat_on(&feats[n], maxb, maxe);
    so_siv_header(o, "CPUID Features");
    so_tabs(o, tabs, 4);
    so_f(o, C_TEAL, "%d of %d features present on CPU-0  (%s)", on, n, sys_cpu_name());
    so_par(o); so_par(o);
    so_row(o, 4, (const char *const[]){ "Feature", "CPUID Feature Description", "Feature", "CPUID Feature Description" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    so_par(o);
    for (i = 0; i < (n + 1) / 2; i++) {
        int k;
        for (k = 0; k < 2; k++) {
            int idx = i + k * ((n + 1) / 2), f;
            if (idx >= n) break;
            f = feat_on(&feats[idx], maxb, maxe);
            if (k) so_tab(o);
            so_t(o, f ? C_VALUE : C_GRAY, feats[idx].name);
            so_tab(o);
            so_check(o, f);
            so_t(o, f ? C_BLACK : C_GRAY, feats[idx].desc);
        }
        so_par(o);
    }
    so_board_footer(o);
}

/* [present] - SIV lists Windows IsProcessorFeaturePresent(PF_*); Linux equivalents are /proc/cpuinfo flags */
static void s_present(srs_out *o)
{
    static const struct { int pf; const char *name, *flag; } pf[] = {
        { 0, "PF_FLOATING_POINT_PRECISION_ERRATA", "fdiv_bug" }, { 1, "PF_FLOATING_POINT_EMULATED", "-fpu" },
        { 2, "PF_COMPARE_EXCHANGE_DOUBLE", "cx8" }, { 3, "PF_MMX_INSTRUCTIONS_AVAILABLE", "mmx" },
        { 6, "PF_XMMI_INSTRUCTIONS_AVAILABLE", "sse" }, { 7, "PF_3DNOW_INSTRUCTIONS_AVAILABLE", "3dnow" },
        { 8, "PF_RDTSC_INSTRUCTION_AVAILABLE", "tsc" }, { 9, "PF_PAE_ENABLED", "pae" },
        { 10, "PF_XMMI64_INSTRUCTIONS_AVAILABLE", "sse2" }, { 11, "PF_SSE_DAZ_MODE_AVAILABLE", "sse2" },
        { 12, "PF_NX_ENABLED", "nx" }, { 13, "PF_SSE3_INSTRUCTIONS_AVAILABLE", "pni" },
        { 14, "PF_COMPARE_EXCHANGE128", "cx16" }, { 17, "PF_XSAVE_ENABLED", "xsave" },
        { 20, "PF_SECOND_LEVEL_ADDRESS_TRANSLATION", "npt" }, { 21, "PF_VIRT_FIRMWARE_ENABLED", "svm" },
        { 22, "PF_RDWRFSGSBASE_AVAILABLE", "fsgsbase" }, { 23, "PF_FASTFAIL_AVAILABLE", "lm" },
        { 28, "PF_RDRAND_INSTRUCTION_AVAILABLE", "rdrand" }, { 32, "PF_RDTSCP_INSTRUCTION_AVAILABLE", "rdtscp" },
        { 33, "PF_RDPID_INSTRUCTION_AVAILABLE", "rdpid" }, { 35, "PF_MONITORX_INSTRUCTION_AVAILABLE", "mwaitx" },
        { 36, "PF_SSSE3_INSTRUCTIONS_AVAILABLE", "ssse3" }, { 37, "PF_SSE4_1_INSTRUCTIONS_AVAILABLE", "sse4_1" },
        { 38, "PF_SSE4_2_INSTRUCTIONS_AVAILABLE", "sse4_2" }, { 39, "PF_AVX_INSTRUCTIONS_AVAILABLE", "avx" },
        { 40, "PF_AVX2_INSTRUCTIONS_AVAILABLE", "avx2" }, { 41, "PF_AVX512F_INSTRUCTIONS_AVAILABLE", "avx512f" },
        { 42, "PF_ERMS_AVAILABLE", "erms" }, { -1, NULL, NULL } };
    static const int tabs[] = { 60, 400, 520 };
    int i;
    const char *f = flags();
    so_siv_header(o, "Processor Features Present");
    so_tabs(o, tabs, 3);
    so_t(o, C_TEAL, "  CPU-0  "); so_t(o, C_BLACK, sys_cpu_name()); so_par(o); so_par(o);
    so_row(o, 3, (const char *const[]){ "  PF", "Windows feature (SIV)", "Linux flag" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; pf[i].name; i++) {
        int on = pf[i].flag[0] == '-' ? !has_flag(f, pf[i].flag + 1) : has_flag(f, pf[i].flag);
        char n[16];
        snprintf(n, sizeof n, "  |C|%d", pf[i].pf);
        so_t(o, C_GRAY, n); so_tab(o); so_check(o, on); so_t(o, on ? C_BLACK : C_GRAY, pf[i].name); so_tab(o);
        so_t(o, C_PURPLE, pf[i].flag); so_par(o);
    }
    so_board_footer(o);
}

/* [cpu-bits] - SIV shows the registry CPU description + Windows FeatureSet bits; Linux: cpuinfo fields + flags */
static void s_cpu_bits(srs_out *o)
{
    static const int tabs[] = { 160, 280 };
    static const char *const keys[] = { "model name", "vendor_id", "cpu family", "model", "stepping", "microcode", "cpu MHz",
                                        "cache size", "physical id", "siblings", "core id", "cpu cores", "apicid", "initial apicid",
                                        "fpu", "fpu_exception", "cpuid level", "wp", "bugs", "bogomips", "TLB size",
                                        "clflush size", "cache_alignment", "address sizes", "power management", NULL };
    kvp v[40];
    int n = cpuinfo_cpu(arg_cpu(o), v, 40), i;
    so_tabs(o, tabs, 2);
    so_f(o, C_LABEL, "CPU-%d", arg_cpu(o)); so_tab(o); so_t(o, C_TEAL, "/proc/cpuinfo"); so_par(o); so_par(o);
    for (i = 0; keys[i]; i++) { so_t(o, C_DEF, ""); so_tab(o); so_t(o, C_VALUE, keys[i]); so_tab(o); so_t(o, C_BLACK, kv_get(v, n, keys[i])); so_par(o); }
    so_par(o);
    so_tab(o); so_t(o, C_VALUE, "flags"); so_tab(o); so_m(o, C_PURPLE, kv_get(v, n, "flags")); so_par(o);
    so_tab(o); so_t(o, C_VALUE, "vmx flags"); so_tab(o); so_m(o, C_PURPLE, kv_get(v, n, "vmx flags")); so_par(o);
}

/* ======================================================================
 * [cpuid] raw dump (arg = CPU)
 * ====================================================================== */
static int sub_leaves(unsigned leaf)
{
    switch (leaf) {
    case 4: case 0xB: case 0x1F: case 0x8000001D: return 8;
    case 7: return 2;
    case 0xD: return 20;
    case 0xF: case 0x10: case 0x14: case 0x17: case 0x18: case 0x80000020: case 0x80000026: return 4;
    default: return 1;
    }
}

static void s_cpuid(srs_out *o)
{
    static const int tabs[] = { 120, 200, 290, 380, 470, 560 };
    int cpu = arg_cpu(o);
    unsigned r[4], maxb, maxe, l, s;
    char hz[32], t[128];
    if (cpu >= ncpu()) cpu = 0;
    pin_to(cpu);
    cpuid(0, 0, r); maxb = r[0];
    cpuid(0x80000000, 0, r); maxe = r[0];
    freq_str(cpu, "scaling_cur_freq", hz, sizeof hz);
    snprintf(t, sizeof t, "CPU-%d %s  %s", cpu, sys_cpu_name(), hz);
    so_tabs(o, tabs, 6);
    so_t(o, C_LABEL, t); so_par(o); so_par(o);
    so_row(o, 7, (const char *const[]){ "Leaf", "Sub", "EAX", "EBX", "ECX", "EDX", "ASCII" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (l = 0; l <= maxe && l != 0x80000000 + 0x100; l = (l == maxb ? 0x80000000 : l + 1)) {
        if (l > maxb && l < 0x80000000) l = 0x80000000;
        for (s = 0; s < (unsigned)sub_leaves(l); s++) {
            char c[7][24], asc[17];
            int k;
            cpuid(l, s, r);
            if (s && !r[0] && !r[1] && !r[2] && !r[3]) break;
            snprintf(c[0], 24, "%08X", l);
            snprintf(c[1], 24, "%u", s);
            for (k = 0; k < 4; k++) snprintf(c[2 + k], 24, "%08X", r[k]);
            for (k = 0; k < 16; k++) { unsigned char ch = (unsigned char)(r[k / 4] >> ((k % 4) * 8)); asc[k] = ch >= 32 && ch < 127 ? (char)ch : '.'; }
            asc[16] = 0;
            snprintf(c[6], 24, "%s", asc);
            so_row(o, 7, (const char *const[]){ c[0], c[1], c[2], c[3], c[4], c[5], c[6] },
                   (const int[]){ C_PURPLE, C_GRAY, C_BLACK, C_BLACK, C_BLACK, C_BLACK, C_TEAL });
        }
        if (l == maxe) break;
    }
    unpin();
}

static void s_cpuid0(srs_out *o)
{
    static const int tabs[] = { 200 };
    unsigned r[4];
    char vend[13], brand[49];
    int i;
    cpuid(0, 0, r);
    memcpy(vend, &r[1], 4); memcpy(vend + 4, &r[3], 4); memcpy(vend + 8, &r[2], 4); vend[12] = 0;
    so_tabs(o, tabs, 1);
    so_kvf(o, "Maximum standard leaf", C_PURPLE, "%08X", r[0]);
    so_kv(o, "Vendor string", C_BLACK, vend);
    cpuid(0x80000000, 0, r);
    so_kvf(o, "Maximum extended leaf", C_PURPLE, "%08X", r[0]);
    for (i = 0; i < 3; i++) cpuid(0x80000002 + (unsigned)i, 0, (unsigned *)(brand + i * 16));
    brand[48] = 0;
    for (i = 47; i >= 0 && (brand[i] == ' ' || !brand[i]); i--) brand[i] = 0;
    so_kv(o, "Brand string", C_BLACK, brand);
    cpuid(1, 0, r);
    so_kvf(o, "Signature (leaf 1 EAX)", C_TEAL, "%08X  family %u  model %u  stepping %u", r[0],
           ((r[0] >> 8) & 0xF) + ((r[0] >> 20) & 0xFF), ((r[0] >> 4) & 0xF) | (((r[0] >> 16) & 0xF) << 4), r[0] & 0xF);
}

/* ======================================================================
 * clocks, speeds, timer, volts, microcode, speculation, boost, numa
 * ====================================================================== */
static void clocks_vs_stock(srs_out *o);

static void s_cpu_speeds(srs_out *o)
{
    static const int tabs[] = { 70, 160, 250, 340, 430, 520, 620, 760 };
    int i;
    char drv[64] = "", boost[16] = "";
    so_siv_header(o, "CPU Speeds");
    rd_str(SYSCPU "/cpu0/cpufreq/scaling_driver", drv, sizeof drv);
    rd_str(SYSCPU "/cpufreq/boost", boost, sizeof boost);
    so_tabs(o, tabs, 8);
    so_kvf(o, "Driver", C_ORANGE, "%s   boost %s", drv, *boost ? (atoi(boost) ? "on" : "off") : "-");
    so_par(o);
    so_row(o, 9, (const char *const[]){ "CPU", "Current", "Average", "Minimum", "Maximum", "HW Min", "HW Max", "Governor", "EPP" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < ncpu(); i++) {
        char c[9][64];
        const char *cells[9];
        int k;
        snprintf(c[0], 64, "CPU-%d", i);
        freq_str(i, "scaling_cur_freq", c[1], 64);
        freq_str(i, "cpuinfo_avg_freq", c[2], 64);
        freq_str(i, "scaling_min_freq", c[3], 64);
        freq_str(i, "scaling_max_freq", c[4], 64);
        freq_str(i, "cpuinfo_min_freq", c[5], 64);
        freq_str(i, "cpuinfo_max_freq", c[6], 64);
        rd_fmt(c[7], 64, SYSCPU "/cpu%d/cpufreq/scaling_governor", i);
        rd_fmt(c[8], 64, SYSCPU "/cpu%d/cpufreq/energy_performance_preference", i);
        for (k = 0; k < 9; k++) cells[k] = *c[k] ? c[k] : "-";
        so_row(o, 9, cells, (const int[]){ C_LABEL, C_BLACK, C_BLACK, C_GRAY, C_GRAY, C_TEAL, C_TEAL, C_ORANGE, C_ORANGE });
    }
    clocks_vs_stock(o);
}

static double measure_tsc_mhz(void)
{
    struct timespec a, b;
    unsigned long long t0, t1;
    clock_gettime(CLOCK_MONOTONIC_RAW, &a);
    t0 = __rdtsc();
    do clock_gettime(CLOCK_MONOTONIC_RAW, &b); while ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec) < 50e6);
    t1 = __rdtsc();
    return (double)(t1 - t0) / (((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 1e3);
}

static void s_cpu_timer(srs_out *o)
{
    static const int tabs[] = { 200 };
    char cur[64] = "", avail[256] = "", b[128];
    const char *f = flags();
    struct timespec res;
    so_siv_header(o, "CPU Timers");
    so_tabs(o, tabs, 1);
    rd_str("/sys/devices/system/clocksource/clocksource0/current_clocksource", cur, sizeof cur);
    rd_str("/sys/devices/system/clocksource/clocksource0/available_clocksource", avail, sizeof avail);
    so_kv(o, "Clock source", C_ORANGE, cur);
    so_kv(o, "Available", C_VALUE, avail);
    snprintf(b, sizeof b, "%.3f MHz (measured over 50 ms)", measure_tsc_mhz());
    so_kv(o, "TSC frequency", C_BLACK, b);
    so_t(o, C_LABEL, "TSC properties"); so_tab(o);
    so_check(o, has_flag(f, "constant_tsc")); so_t(o, C_VALUE, "constant  ");
    so_check(o, has_flag(f, "nonstop_tsc")); so_t(o, C_VALUE, "nonstop  ");
    so_check(o, has_flag(f, "tsc_known_freq")); so_t(o, C_VALUE, "known freq  ");
    so_check(o, has_flag(f, "rdtscp")); so_t(o, C_VALUE, "RDTSCP");
    so_par(o);
    clock_getres(CLOCK_MONOTONIC, &res);
    so_kvf(o, "CLOCK_MONOTONIC res", C_BLACK, "%ld ns", res.tv_nsec);
    so_kvf(o, "USER_HZ (clock ticks/s)", C_BLACK, "%ld", sysconf(_SC_CLK_TCK));
    {   /* CONFIG_HZ (timer interrupt rate): kernel config from the build tree / /boot, else zcat /proc/config.gz */
        struct utsname u;
        char p[400], *t = NULL, *h;
        const char *const zc[] = { "zcat", "/proc/config.gz", NULL };
        int ok = 0;
        uname(&u);
        snprintf(p, sizeof p, "/usr/lib/modules/%s/build/.config", u.release);
        if (rd_all(p, &t) < 0) { snprintf(p, sizeof p, "/boot/config-%s", u.release); if (rd_all(p, &t) < 0) t = NULL; }
        if (!t && path_exists("/proc/config.gz")) { if (run_out(&t, zc) != 0) { free(t); t = NULL; } }
        if (t && (h = strstr(t, "\nCONFIG_HZ="))) { so_kvf(o, "Kernel HZ (CONFIG_HZ)", C_BLACK, "%ld", atol(h + 11)); ok = 1; }
        if (!ok) so_kv(o, "Kernel HZ (CONFIG_HZ)", C_GRAY, "kernel config not available");
        free(t);
    }
    if (rd_str("/sys/devices/system/clockevents/clockevent0/current_device", b, sizeof b)) so_kv(o, "Clock event", C_VALUE, b);
}

static void s_microcode(srs_out *o)
{
    static const int tabs[] = { 100, 220 };
    int i;
    so_siv_header(o, "CPU Microcode");
    so_tabs(o, tabs, 2);
    so_row(o, 2, (const char *const[]){ "CPU", "Version" }, (const int[]){ C_LABEL, C_LABEL });
    for (i = 0; i < ncpu(); i++) {
        char v[64] = "", n[16];
        rd_fmt(v, sizeof v, SYSCPU "/cpu%d/microcode/version", i);
        snprintf(n, sizeof n, "CPU-%d", i);
        so_row(o, 2, (const char *const[]){ n, *v ? v : "-" }, (const int[]){ C_LABEL, C_PURPLE });
    }
}

static void s_speculation(srs_out *o)
{
    static const int tabs[] = { 260 };
    char **e;
    int n = dir_list(SYSCPU "/vulnerabilities", &e), i;
    so_siv_header(o, "Speculative Execution Mitigations");
    so_tabs(o, tabs, 1);
    for (i = 0; i < n; i++) {
        char v[512], p[256];
        snprintf(p, sizeof p, SYSCPU "/vulnerabilities/%s", e[i]);
        rd_str(p, v, sizeof v);
        so_kv(o, e[i], !strncmp(v, "Not affected", 12) ? C_GREEN : !strncmp(v, "Vulnerable", 10) ? C_RED : C_ORANGE, v);
    }
    dir_free(e, n);
    {
        char cmd[2048] = "";
        rd_str("/proc/cmdline", cmd, sizeof cmd);
        so_par(o);
        so_kv(o, "Kernel command line", C_GRAY, cmd);
    }
}

static void s_turbo(srs_out *o)
{
    static const int tabs[] = { 260 };
    char b[256];
    so_siv_header(o, "Turbo Boost / Core Performance Boost");
    so_tabs(o, tabs, 1);
    so_t(o, C_LABEL, "CPB (CPUID 80000007 EDX bit 9)"); so_tab(o);
    { unsigned r[4]; cpuid(0x80000007, 0, r); so_check(o, (int)(r[3] >> 9 & 1)); }
    so_par(o);
    if (rd_str(SYSCPU "/cpufreq/boost", b, sizeof b)) so_kv(o, "cpufreq boost", atoi(b) ? C_GREEN : C_RED, atoi(b) ? "enabled" : "disabled");
    if (rd_str(SYSCPU "/amd_pstate/status", b, sizeof b)) so_kv(o, "amd_pstate status", C_ORANGE, b);
    if (rd_str(SYSCPU "/amd_pstate/prefcore", b, sizeof b)) so_kv(o, "amd_pstate prefcore", C_ORANGE, b);
    if (rd_str(SYSCPU "/cpu0/cpufreq/amd_pstate_max_freq", b, sizeof b)) { char h[32]; fmt_hz(atof(b) * 1000, h, sizeof h); so_kv(o, "Maximum boost frequency", C_BLACK, h); }
    if (rd_str(SYSCPU "/cpu0/cpufreq/amd_pstate_lowest_nonlinear_freq", b, sizeof b)) { char h[32]; fmt_hz(atof(b) * 1000, h, sizeof h); so_kv(o, "Lowest non-linear frequency", C_BLACK, h); }
    if (rd_str(SYSCPU "/cpu0/cpufreq/cpuinfo_max_freq", b, sizeof b)) { char h[32]; fmt_hz(atof(b) * 1000, h, sizeof h); so_kv(o, "cpuinfo_max_freq", C_BLACK, h); }
}

static void s_amd_cpu(srs_out *o)
{
    static const int tabs[] = { 70, 170, 270, 380, 480, 600 };
    int i;
    char b[128];
    so_siv_header(o, "AMD Ryzen Power Management (amd_pstate / CPPC)");
    so_tabs(o, tabs, 6);
    if (!rd_str(SYSCPU "/amd_pstate/status", b, sizeof b)) { so_err(o, "amd_pstate driver not active"); return; }
    so_kvf(o, "Mode", C_ORANGE, "%s", b);
    so_par(o);
    so_row(o, 7, (const char *const[]){ "CPU", "Highest perf", "Ranking", "Max freq", "Nonlinear", "EPP", "HW prefcore" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < ncpu(); i++) {
        char c[7][64];
        const char *cells[7];
        int k;
        snprintf(c[0], 64, "CPU-%d", i);
        rd_fmt(c[1], 64, SYSCPU "/cpu%d/cpufreq/amd_pstate_highest_perf", i);
        rd_fmt(c[2], 64, SYSCPU "/cpu%d/cpufreq/amd_pstate_prefcore_ranking", i);
        freq_str(i, "amd_pstate_max_freq", c[3], 64);
        freq_str(i, "amd_pstate_lowest_nonlinear_freq", c[4], 64);
        rd_fmt(c[5], 64, SYSCPU "/cpu%d/cpufreq/energy_performance_preference", i);
        rd_fmt(c[6], 64, SYSCPU "/cpu%d/cpufreq/amd_pstate_hw_prefcore", i);
        for (k = 0; k < 7; k++) cells[k] = *c[k] ? c[k] : "-";
        so_row(o, 7, cells, (const int[]){ C_LABEL, C_BLACK, C_ORANGE, C_BLACK, C_GRAY, C_TEAL, C_TEAL });
    }
}

/* AMD SMU via the ryzen_smu driver (read-only attributes) */
#define RSMU "/sys/kernel/ryzen_smu_drv"
static void s_amd_smu(srs_out *o)
{
    static const int tabs[] = { 220 };
    /* "version" is the SMU firmware version (the driver has no smu_version attribute) */
    static const char *const attrs[] = { "drv_version", "codename", "version", "mp1_if_version", "pm_table_version", "pm_table_size", NULL };
    static const char *const labels[] = { "Driver version", "Codename", "SMU firmware version", "MP1 interface version", "PM table version", "PM table size", NULL };
    int i;
    so_siv_header(o, "AMD SMU");
    so_tabs(o, tabs, 1);
    if (!path_exists(RSMU)) { so_err(o, "ryzen_smu driver not loaded (/sys/kernel/ryzen_smu_drv absent)"); return; }
    for (i = 0; attrs[i]; i++) {
        char p[256], v[128] = "";
        snprintf(p, sizeof p, RSMU "/%s", attrs[i]);
        if (!strcmp(attrs[i], "pm_table_version") || !strcmp(attrs[i], "pm_table_size")) {
            unsigned char raw[16];
            FILE *f = fopen(p, "rb");
            size_t n = f ? fread(raw, 1, sizeof raw, f) : 0;
            if (f) fclose(f);
            if (n == 4) { unsigned x; memcpy(&x, raw, 4); snprintf(v, sizeof v, "0x%08X (%u)", x, x); }
            else if (n == 8) { unsigned long long x; memcpy(&x, raw, 8); snprintf(v, sizeof v, "%llu bytes", x); }
        } else rd_str(p, v, sizeof v);
        so_kv(o, labels[i], C_BLACK, *v ? v : "-");
    }
    so_par(o);
    so_note(o, "Read-only: srs-gui never sends SMU commands (rsmu_cmd / mp1_smu_cmd / smn are not touched).");
}

/* ----------------------------------------------------------------------
 * [cpu-clocks] second table (2.2): each clock at stock (the 100 MHz reference clock) next to what
 * is in effect now, for a PC run with another base clock (BCLK / AM5 eCLK, e.g. 105 MHz).
 * Base clock: measured, the way LSA does it - the invariant TSC runs at the nominal clock, and ACPI
 * CPPC nominal_freq is that clock at a 100 MHz reference, so base = TSC x 100 / nominal_freq.
 * Measured once per run (50 ms). Core clocks follow the base clock; cpufreq's limits are worked out
 * at 100 MHz, so they are scaled by the measured ratio. FCLK / UCLK / MCLK: what the SMU reports in
 * its PM table (ryzen_smu, read-only); software cannot measure them, so no difference is claimed.
 * ---------------------------------------------------------------------- */
static double base_clock_mhz(double *tsc_out, double *nom_out)
{
    static int done;
    static double base, tsc, nom;
    if (!done) {
        long n = 0;
        done = 1;
        if (has_flag(flags(), "constant_tsc") && rd_long(SYSCPU "/cpu0/acpi_cppc/nominal_freq", &n) && n > 0) {
            nom = (double)n;
            tsc = measure_tsc_mhz();
            base = tsc * 100.0 / nom;
            if (base < 50 || base > 200) base = 0;
        }
    }
    if (tsc_out) *tsc_out = tsc;
    if (nom_out) *nom_out = nom;
    return base;
}

/* FCLK / UCLK / MCLK in MHz from the PM table: 1 if this table layout is known and the values make
   sense. Known: Zen 4 desktop (Raphael, ryzen_smu codename 20; PM table 0x5401xx): floats 70 / 74 / 78. */
static int smu_fabric_clocks(double *fclk, double *uclk, double *mclk, char *why, size_t wn)
{
    const char *dir = getenv("SRS_RSMU_DIR");
    char p[400], cn[32] = "";
    unsigned ver = 0;
    unsigned char raw[4];
    float t[128];
    size_t got;
    FILE *f;
    if (!dir || !*dir) dir = RSMU;
    snprintf(p, sizeof p, "%s/pm_table", dir);
    if (!path_exists(p)) { snprintf(why, wn, "needs the ryzen_smu driver (%s)", dir); return 0; }
    snprintf(p, sizeof p, "%s/pm_table_version", dir);
    if ((f = fopen(p, "rb"))) { if (fread(raw, 1, 4, f) == 4) memcpy(&ver, raw, 4); fclose(f); }
    snprintf(p, sizeof p, "%s/codename", dir);
    rd_str(p, cn, sizeof cn);
    if (!((ver & 0xFFFF00u) == 0x540100u || (!ver && atoi(cn) == 20))) {
        snprintf(why, wn, "PM table layout not known (codename %s, version 0x%06X)", *cn ? cn : "?", ver);
        return 0;
    }
    snprintf(p, sizeof p, "%s/pm_table", dir);
    if (!(f = fopen(p, "rb"))) { snprintf(why, wn, "pm_table not readable"); return 0; }
    got = fread(t, 1, sizeof t, f);
    fclose(f);
    if (got < 79 * sizeof(float)) { snprintf(why, wn, "pm_table too short"); return 0; }
    *fclk = t[70]; *uclk = t[74]; *mclk = t[78];
    if (!(*fclk >= 400 && *fclk <= 4000 && *uclk >= 400 && *uclk <= 4000 && *mclk >= 400 && *mclk <= 4500 &&
          (fabs(*uclk - *mclk) < 5 || fabs(*uclk * 2 - *mclk) < 5))) {
        snprintf(why, wn, "PM table values out of range (layout differs)");
        return 0;
    }
    return 1;
}

static void cv_row(srs_out *o, const char *name, double stock, double now, int measured, const char *how)
{
    char a[48] = "-", b[48] = "-", d[64] = "-";
    if (stock > 0) snprintf(a, sizeof a, "%.1f MHz", stock);
    if (now > 0) snprintf(b, sizeof b, "%.1f MHz", now);
    if (stock > 0 && now > 0 && measured) snprintf(d, sizeof d, "%+.1f MHz  (%+.1f %%)", now - stock, (now / stock - 1) * 100.0);
    else if (stock > 0 && now > 0) snprintf(d, sizeof d, "not measured");
    so_row(o, 5, (const char *const[]){ name, a, b, d, how },
           (const int[]){ C_LABEL, C_GRAY, C_BLACK, measured && fabs(now - stock) >= 0.5 ? C_ORANGE : C_GRAY, C_GRAY });
}

static void clocks_vs_stock(srs_out *o)
{
    static const int tabs[] = { 150, 270, 390, 560 };
    double tsc = 0, nom = 0, base = base_clock_mhz(&tsc, &nom), r = base > 0 ? base / 100.0 : 0, f = 0, u = 0, m = 0;
    long mx = 0, cur = 0, v;
    char why[200] = "", how[200];
    int i, smu = smu_fabric_clocks(&f, &u, &m, why, sizeof why);
    for (i = 0; i < ncpu(); i++) {
        char p[128];
        snprintf(p, sizeof p, SYSCPU "/cpu%d/cpufreq/cpuinfo_max_freq", i);
        if (rd_long(p, &v) && v > mx) mx = v;
        snprintf(p, sizeof p, SYSCPU "/cpu%d/cpufreq/scaling_cur_freq", i);
        if (rd_long(p, &v) && v > cur) cur = v;
    }
    so_par(o);
    so_tabs(o, tabs, 4);
    so_b(o, C_VALUE, "Stock and in effect now");
    so_par(o);
    so_row(o, 5, (const char *const[]){ "Clock", "Stock", "In effect now", "Difference", "How it is known" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    if (base > 0) snprintf(how, sizeof how, "measured: TSC %.1f MHz x 100 / CPPC nominal %.0f MHz", tsc, nom);
    else snprintf(how, sizeof how, "not measured (needs an invariant TSC and ACPI CPPC nominal_freq)");
    cv_row(o, "Base clock", 100.0, base, base > 0, how);
    cv_row(o, "Core base", nom, nom * r, base > 0, "CPPC nominal frequency x base clock");
    cv_row(o, "Core boost (max)", mx / 1000.0, mx / 1000.0 * r, base > 0, "cpufreq maximum (worked out at 100 MHz) x base clock");
    cv_row(o, "Core now (fastest)", r > 0 ? cur / 1000.0 / r : 0, cur / 1000.0, base > 0, "cpufreq now (APERF/MPERF x the measured TSC rate, so real); stock = same multiplier at 100 MHz");
    if (smu) {
        char rat[64];
        cv_row(o, "FCLK (fabric)", f, f, 0, "SMU PM table (ryzen_smu)");
        cv_row(o, "UCLK (memory controller)", u, u, 0, "SMU PM table (ryzen_smu)");
        cv_row(o, "MCLK (memory)", m, m, 0, "SMU PM table; DDR data rate = 2 x MCLK");
        snprintf(rat, sizeof rat, "DDR-%.0f   UCLK:MCLK %s   FCLK %.0f MHz", m * 2, fabs(u - m) < 5 ? "1:1" : "1:2", f);
        so_par(o);
        so_kv(o, "Memory", C_TEAL, rat);
    } else {
        so_row(o, 5, (const char *const[]){ "FCLK / UCLK / MCLK", "-", "-", "-", why },
               (const int[]){ C_LABEL, C_GRAY, C_GRAY, C_GRAY, C_GRAY });
    }
    so_par(o);
    so_note(o, "Stock = at the standard 100 MHz reference clock. A base clock other than 100 MHz (BCLK, or eCLK on AM5) "
               "speeds up the cores by the same factor.");
    if (smu)
        so_note(o, "FCLK, UCLK and MCLK are the SMU's own figures: software cannot measure them, so no difference is "
                   "claimed. On AM5 an eCLK drives the cores; whether anything else follows it depends on the board's eCLK "
                   "mode, so check these against the BIOS settings.");
}

/* [amd-pmt] PM table layout: version / size / value count (the values themselves are [amd-pmt-0]) */
static void s_amd_pmt_layout(srs_out *o)
{
    static const int tabs[] = { 220 };
    unsigned char raw[8] = { 0 };
    unsigned ver = 0;
    unsigned long long sz = 0;
    FILE *f;
    char codename[64] = "";
    so_siv_header(o, "AMD PM Table layout");
    so_tabs(o, tabs, 1);
    if (!path_exists(RSMU "/pm_table")) { so_err(o, "ryzen_smu pm_table not available"); return; }
    if ((f = fopen(RSMU "/pm_table_version", "rb"))) { if (fread(raw, 1, 4, f) == 4) memcpy(&ver, raw, 4); fclose(f); }
    if ((f = fopen(RSMU "/pm_table_size", "rb"))) { if (fread(raw, 1, 8, f) == 8) memcpy(&sz, raw, 8); fclose(f); }
    rd_str(RSMU "/codename", codename, sizeof codename);
    so_kv(o, "ryzen_smu codename", C_VALUE, codename);
    so_kvf(o, "PM table version", C_BLACK, "0x%06X", ver);
    so_kvf(o, "PM table size", C_BLACK, "%llu bytes (%llu 32-bit values)", sz, sz / 4);
    so_kv(o, "Value type", C_VALUE, "IEEE 754 single-precision floats, little-endian");
    so_par(o);
    so_note(o, "Which value is which depends on the table version and is not published by AMD; see [amd-pmt-0] for the values.");
}

/* [amd-pmt-*] PM table as floats (ryzen_smu pm_table is world-readable on this system) */
static void s_amd_pmt(srs_out *o)
{
    static const int tabs[] = { 80, 160, 280, 360, 440, 560, 640, 720, 840, 920, 1000 };
    unsigned char *buf;
    FILE *f;
    size_t n, i;
    so_siv_header(o, "AMD PM Table (SMU power-management table, raw floats)");
    if (!path_exists(RSMU "/pm_table")) { so_err(o, "ryzen_smu pm_table not available"); return; }
    f = fopen(RSMU "/pm_table", "rb");
    if (!f) { so_err(o, "pm_table not readable as this user"); return; }
    buf = malloc(65536);
    n = buf ? fread(buf, 1, 65536, f) : 0;
    fclose(f);
    so_tabs(o, tabs, 11);
    so_f(o, C_TEAL, "%zu bytes, %zu values; layout depends on pm_table_version (see AMD SMU)", n, n / 4);
    so_par(o); so_par(o);
    for (i = 0; i + 4 <= n; i += 16) {
        size_t k;
        so_f(o, C_PURPLE, "%04zX", i);
        for (k = 0; k < 4 && i + k * 4 + 4 <= n; k++) {
            float v;
            memcpy(&v, buf + i + k * 4, 4);
            so_tab(o);
            so_f(o, C_GRAY, "[%zu]", (i / 4) + k);
            so_tab(o);
            so_f(o, isfinite(v) && fabsf(v) < 1e7f ? C_BLACK : C_GRAY, "%.4g", v);
        }
        so_par(o);
    }
    free(buf);
}

/* [amd-tel-*] package / core power from zenergy (energy counters sampled over 200 ms) */
static void s_amd_tel(srs_out *o)
{
    static const int tabs[] = { 160, 260, 360 };
    char **hw;
    int n = dir_list("/sys/class/hwmon", &hw), i, found = 0;
    so_siv_header(o, "AMD Telemetry (RAPL energy counters)");
    so_tabs(o, tabs, 3);
    for (i = 0; i < n; i++) {
        char nm[64], p[256], **f;
        int nf, j;
        long long e0[64];
        char lab[64][32];
        int ne = 0;
        snprintf(p, sizeof p, "/sys/class/hwmon/%s/name", hw[i]);
        if (!rd_str(p, nm, sizeof nm) || (strcmp(nm, "zenergy") && strcmp(nm, "amd_energy"))) continue;
        found = 1;
        snprintf(p, sizeof p, "/sys/class/hwmon/%s", hw[i]);
        nf = dir_list(p, &f);
        for (j = 0; j < nf && ne < 64; j++) {
            char q[512], v[64];
            if (strncmp(f[j], "energy", 6) || !strstr(f[j], "_input")) continue;
            snprintf(q, sizeof q, "/sys/class/hwmon/%s/%s", hw[i], f[j]);
            if (!rd_str(q, v, sizeof v)) continue;
            e0[ne] = atoll(v);
            snprintf(q, sizeof q, "/sys/class/hwmon/%s/%.*slabel", hw[i], (int)(strstr(f[j], "_input") - f[j] + 1), f[j]);
            if (!rd_str(q, lab[ne], sizeof lab[ne])) snprintf(lab[ne], sizeof lab[ne], "%s", f[j]);
            snprintf(lab[ne] + 24, 8, "%d", j);   /* remember file index */
            ne++;
        }
        usleep(200000);
        so_row(o, 3, (const char *const[]){ "Plane", "Power", "Energy" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL });
        {
            int k, idx = 0;
            for (j = 0; j < nf && idx < ne; j++) {
                char q[512], v[64], pw[32], en[32];
                if (strncmp(f[j], "energy", 6) || !strstr(f[j], "_input")) continue;
                snprintf(q, sizeof q, "/sys/class/hwmon/%s/%s", hw[i], f[j]);
                if (!rd_str(q, v, sizeof v)) continue;
                k = idx++;
                snprintf(pw, sizeof pw, "%.2f W", (double)(atoll(v) - e0[k]) / 1e6 / 0.2);
                snprintf(en, sizeof en, "%.2f kJ", atoll(v) / 1e9);
                so_row(o, 3, (const char *const[]){ lab[k], pw, en }, (const int[]){ C_VALUE, C_BLACK, C_GRAY });
            }
        }
        dir_free(f, nf);
    }
    dir_free(hw, n);
    if (!found) so_err(o, "No zenergy / amd_energy hwmon driver loaded");
}

/* [amd-thm-*] [amd-tmp] k10temp */
static void s_amd_thm(srs_out *o)
{
    static const int tabs[] = { 160, 260 };
    char **hw;
    int n = dir_list("/sys/class/hwmon", &hw), i, found = 0;
    so_siv_header(o, "AMD Thermal (k10temp)");
    so_tabs(o, tabs, 2);
    for (i = 0; i < n; i++) {
        char nm[64], p[256];
        int t;
        snprintf(p, sizeof p, "/sys/class/hwmon/%s/name", hw[i]);
        if (!rd_str(p, nm, sizeof nm) || strcmp(nm, "k10temp")) continue;
        found = 1;
        for (t = 1; t <= 16; t++) {
            char q[256], l[64], v[32];
            snprintf(q, sizeof q, "/sys/class/hwmon/%s/temp%d_input", hw[i], t);
            if (!rd_str(q, v, sizeof v)) continue;
            snprintf(q, sizeof q, "/sys/class/hwmon/%s/temp%d_label", hw[i], t);
            if (!rd_str(q, l, sizeof l)) snprintf(l, sizeof l, "temp%d", t);
            so_kvf(o, l, C_BLACK, "%.1f°C", atof(v) / 1000);
        }
    }
    dir_free(hw, n);
    if (!found) so_err(o, "k10temp not loaded");
    so_par(o);
    so_note(o, "Tctl is the control temperature (may include an offset); TccdN are the per-CCD die sensors.");
}

static void s_amd_fus(srs_out *o)
{
    static const int tabs[] = { 200 };
    so_tabs(o, tabs, 1);
    so_kv(o, "Status", C_ORANGE, "Not read");
    so_kv(o, "Reason", C_GRAY, "Fuse data needs SMN register access (ryzen_smu smn writes); srs-gui stays read-only");
}

static void s_cpu_oc(srs_out *o)
{
    static const int tabs[] = { 200 };
    so_tabs(o, tabs, 1);
    so_kv(o, "Status", C_ORANGE, "Not present on this system");
    so_kv(o, "Reason", C_GRAY, "IA32_OC_MAILBOX is an Intel interface; on AMD see [amd-cpu] and [turbo-boost]");
}

static void s_cpu_tweak(srs_out *o)
{
    static const int tabs[] = { 260 };
    char b[256];
    so_siv_header(o, "CPU Tunables (read-only view)");
    so_tabs(o, tabs, 1);
    if (rd_str(SYSCPU "/cpu0/cpufreq/scaling_governor", b, sizeof b)) so_kv(o, "Governor (cpu0)", C_ORANGE, b);
    if (rd_str(SYSCPU "/cpu0/cpufreq/scaling_available_governors", b, sizeof b)) so_kv(o, "Available governors", C_VALUE, b);
    if (rd_str(SYSCPU "/cpu0/cpufreq/energy_performance_preference", b, sizeof b)) so_kv(o, "EPP (cpu0)", C_ORANGE, b);
    if (rd_str(SYSCPU "/cpu0/cpufreq/energy_performance_available_preferences", b, sizeof b)) so_kv(o, "Available EPP", C_VALUE, b);
    if (rd_str(SYSCPU "/cpufreq/boost", b, sizeof b)) so_kv(o, "Boost", C_ORANGE, atoi(b) ? "on" : "off");
    if (rd_str(SYSCPU "/smt/control", b, sizeof b)) so_kv(o, "SMT control", C_ORANGE, b);
    if (rd_str(SYSCPU "/smt/active", b, sizeof b)) so_kv(o, "SMT active", C_BLACK, b);
    if (rd_str(SYSCPU "/isolated", b, sizeof b)) so_kv(o, "Isolated CPUs", C_BLACK, *b ? b : "none");
    if (rd_str(SYSCPU "/nohz_full", b, sizeof b)) so_kv(o, "nohz_full CPUs", C_BLACK, *b ? b : "none");
    if (rd_str("/proc/sys/kernel/sched_autogroup_enabled", b, sizeof b)) so_kv(o, "sched_autogroup", C_BLACK, b);
    so_par(o);
    so_note(o, "srs-gui shows these; changing them is left to tuned / cpupower / LACT.");
}

static void s_msrs(srs_out *o)
{
    static const int tabs[] = { 200 };
    const char *cache = so_root_file("msrs.txt");
    so_siv_header(o, "Model Specific Registers");
    so_tabs(o, tabs, 1);
    if (cache) { char *t; if (rd_all(cache, &t) >= 0) { so_lines(o, C_BLACK, t); free(t); } return; }
    so_kv(o, "Status", C_ORANGE, "Needs root");
    so_kv(o, "Source", C_PURPLE, "/dev/cpu/*/msr (msr module), read by the srs-root helper (Menu > Run as root)");
}

static void s_msr_setup(srs_out *o)
{
    static const int tabs[] = { 200 };
    so_tabs(o, tabs, 1);
    so_kv(o, "msr module", C_BLACK, path_exists("/dev/cpu/0/msr") ? "loaded (/dev/cpu/N/msr present)" : "not loaded");
    so_kv(o, "cpuid module", C_BLACK, path_exists("/dev/cpu/0/cpuid") ? "loaded" : "not loaded");
    so_kv(o, "Access", C_GRAY, "root only; srs-gui reads MSRs through srs-root, never writes them");
}

static void s_cpu_volts(srs_out *o)
{
    static const int tabs[] = { 200, 300 };
    char **hw;
    int n = dir_list("/sys/class/hwmon", &hw), i;
    so_siv_header(o, "CPU Voltages");
    so_tabs(o, tabs, 2);
    for (i = 0; i < n; i++) {
        char nm[64], p[256];
        int k;
        snprintf(p, sizeof p, "/sys/class/hwmon/%s/name", hw[i]);
        if (!rd_str(p, nm, sizeof nm)) continue;
        if (strncmp(nm, "nct", 3) && strncmp(nm, "it87", 4) && strncmp(nm, "asus", 4) && strcmp(nm, "zenpower")) continue;
        for (k = 0; k < 20; k++) {
            char q[256], l[64] = "", v[32];
            snprintf(q, sizeof q, "/sys/class/hwmon/%s/in%d_input", hw[i], k);
            if (!rd_str(q, v, sizeof v)) continue;
            snprintf(q, sizeof q, "/sys/class/hwmon/%s/in%d_label", hw[i], k);
            rd_str(q, l, sizeof l);
            if (k > 1 && !*l) continue;   /* in0 is Vcore on Nuvoton/ITE boards */
            {
                char key[96];
                snprintf(key, sizeof key, "%s %s", nm, *l ? l : k == 0 ? "in0 (Vcore sense)" : "in1");
                so_kvf(o, key, C_BLACK, "%.3f V", atof(v) / 1000);
            }
        }
    }
    dir_free(hw, n);
    so_par(o);
    so_note(o, "Per-core VID needs SVI telemetry not exposed by Linux drivers; the board's Vcore sense is shown.");
}

/* NUMA */
static void s_numa(srs_out *o)
{
    static const int tabs[] = { 90, 260, 400, 540 };
    char **e;
    int n = dir_list("/sys/devices/system/node", &e), i;
    so_siv_header(o, "NUMA Nodes");
    so_tabs(o, tabs, 4);
    so_row(o, 5, (const char *const[]){ "Node", "CPUs", "Memory", "Free", "Distances" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < n; i++) {
        char p[256], cpus[256] = "", dist[256] = "", mem[64] = "-", fr[64] = "-", *mi;
        if (strncmp(e[i], "node", 4)) continue;
        snprintf(p, sizeof p, "/sys/devices/system/node/%s/cpulist", e[i]); rd_str(p, cpus, sizeof cpus);
        snprintf(p, sizeof p, "/sys/devices/system/node/%s/distance", e[i]); rd_str(p, dist, sizeof dist);
        snprintf(p, sizeof p, "/sys/devices/system/node/%s/meminfo", e[i]);
        if (rd_all(p, &mi) >= 0) {
            char *a = strstr(mi, "MemTotal:"), *b = strstr(mi, "MemFree:");
            if (a) fmt_bytes(atof(a + 9) * 1024, mem, sizeof mem);
            if (b) fmt_bytes(atof(b + 8) * 1024, fr, sizeof fr);
            free(mi);
        }
        so_row(o, 5, (const char *const[]){ e[i], cpus, mem, fr, dist }, (const int[]){ C_LABEL, C_PURPLE, C_BLACK, C_TEAL, C_GRAY });
    }
    dir_free(e, n);
}

/* [group] - Windows processor groups; Linux equivalent: CPU sets */
static void s_group(srs_out *o)
{
    static const int tabs[] = { 200 };
    static const char *const sets[] = { "online", "offline", "possible", "present", "isolated", "nohz_full", "kernel_max", NULL };
    int i;
    so_siv_header(o, "CPU Groups (Linux CPU sets)");
    so_tabs(o, tabs, 1);
    for (i = 0; sets[i]; i++) {
        char p[256], v[256] = "";
        snprintf(p, sizeof p, SYSCPU "/%s", sets[i]);
        rd_str(p, v, sizeof v);
        so_kv(o, sets[i], C_PURPLE, *v ? v : "none");
    }
    {
        cpu_set_t s;
        char b[512] = "";
        int k;
        sched_getaffinity(0, sizeof s, &s);
        for (k = 0; k < ncpu(); k++) if (CPU_ISSET(k, &s)) { size_t l = strlen(b); snprintf(b + l, sizeof b - l, "%s%d", *b ? "," : "", k); }
        so_kv(o, "This process affinity", C_BLACK, b);
    }
}

static void s_hypervisor(srs_out *o)
{
    static const int tabs[] = { 140 };
    const char *f = flags();
    char b[256] = "";
    int groups = 0;
    char **e;
    so_siv_header(o, "Virtualization and Hypervisor Information");
    so_tabs(o, tabs, 1);
    so_t(o, C_LABEL, "  CPU-0"); so_tab(o); so_t(o, C_BLACK, sys_cpu_name()); so_par(o); so_par(o);
    so_t(o, C_DEF, "  "); so_check(o, has_flag(f, "npt") || has_flag(f, "ept")); so_t(o, C_VALUE, "SLAT    Intel EPT or AMD RVI Support"); so_par(o); so_par(o);
    so_t(o, C_DEF, "  "); so_check(o, has_flag(f, "hypervisor")); so_t(o, C_VALUE, "HVM     Running under a hypervisor"); so_par(o); so_par(o);
    so_t(o, C_DEF, "  "); so_check(o, has_flag(f, "vmx")); so_t(o, C_VALUE, "VT-x    Intel Virtualization (VMX) Support"); so_par(o);
    so_t(o, C_DEF, "  "); so_check(o, has_flag(f, "ept")); so_t(o, C_VALUE, "EPT     Intel Extended Page Table Support"); so_par(o); so_par(o);
    so_t(o, C_DEF, "  "); so_check(o, has_flag(f, "svm")); so_t(o, C_VALUE, "AMD-V   AMD Virtualization (SVM) Support"); so_par(o);
    so_t(o, C_DEF, "  "); so_check(o, has_flag(f, "npt")); so_t(o, C_VALUE, "RVI     AMD Rapid Virtualization Indexing Support"); so_par(o);
    so_t(o, C_DEF, "  "); so_check(o, has_flag(f, "avic")); so_t(o, C_VALUE, "AVIC    AMD Virtual Interrupt Controller"); so_par(o);
    so_t(o, C_DEF, "  "); so_check(o, has_flag(f, "sev")); so_t(o, C_VALUE, "SEV     Secure Encrypted Virtualization"); so_par(o);
    so_par(o);
    so_kv(o, "/dev/kvm", C_BLACK, path_exists("/dev/kvm") ? "present (KVM available)" : "absent");
    if ((groups = dir_list("/sys/kernel/iommu_groups", &e)) > 0) { snprintf(b, sizeof b, "%d IOMMU groups", groups); dir_free(e, groups); }
    so_kv(o, "IOMMU", C_BLACK, *b ? b : "disabled / none");
    if (rd_str("/sys/module/kvm_amd/parameters/nested", b, sizeof b)) so_kv(o, "kvm_amd nested", C_BLACK, b);
    so_board_footer(o);
}

/* [cpu-export] - what srs-gui exports about the CPU (same data SIV hands to LCD/OSD tools) */
static void s_cpu_export(srs_out *o)
{
    static const int tabs[] = { 200 };
    int i;
    double maxf = 0;
    so_tabs(o, tabs, 1);
    so_kv(o, "CPU.Name", C_BLACK, sys_cpu_name());
    for (i = 0; i < ncpu(); i++) { long v; char p[128]; snprintf(p, sizeof p, SYSCPU "/cpu%d/cpufreq/scaling_cur_freq", i); if (rd_long(p, &v) && v > maxf) maxf = v; }
    so_kvf(o, "CPU.Clock.Max", C_BLACK, "%.0f MHz", maxf / 1000);
    so_kvf(o, "CPU.Count", C_BLACK, "%d", ncpu());
    so_note(o, "Use \"srs-gui -save=[cpu-export]=FILE\" to write this to a file for other tools.");
}

/* [power-states] - P-states (amd_pstate / CPPC), C-states summary, system sleep states */
static void s_power_states(srs_out *o)
{
    static const int tabs[] = { 260 };
    char b[256], h[32];
    int s;
    so_siv_header(o, "CPU Power States");
    so_tabs(o, tabs, 1);
    so_b(o, C_VALUE, "Performance states (CPPC)"); so_par(o);
    if (rd_str(SYSCPU "/cpu0/cpufreq/amd_pstate_highest_perf", b, sizeof b)) so_kv(o, "Highest perf (cpu0)", C_BLACK, b);
    if (rd_str(SYSCPU "/cpu0/acpi_cppc/nominal_perf", b, sizeof b)) so_kv(o, "Nominal perf", C_BLACK, b);
    if (rd_str(SYSCPU "/cpu0/acpi_cppc/lowest_nonlinear_perf", b, sizeof b)) so_kv(o, "Lowest non-linear perf", C_BLACK, b);
    if (rd_str(SYSCPU "/cpu0/acpi_cppc/lowest_perf", b, sizeof b)) so_kv(o, "Lowest perf", C_BLACK, b);
    if (rd_str(SYSCPU "/cpu0/acpi_cppc/nominal_freq", b, sizeof b)) { fmt_hz(atof(b) * 1e6, h, sizeof h); so_kv(o, "Nominal frequency", C_BLACK, h); }
    if (rd_str(SYSCPU "/cpu0/acpi_cppc/lowest_freq", b, sizeof b)) { fmt_hz(atof(b) * 1e6, h, sizeof h); so_kv(o, "Lowest frequency", C_BLACK, h); }
    if (rd_str(SYSCPU "/cpu0/cpufreq/cpuinfo_max_freq", b, sizeof b)) { fmt_hz(atof(b) * 1e3, h, sizeof h); so_kv(o, "Maximum (boost) frequency", C_BLACK, h); }
    if (rd_str(SYSCPU "/cpu0/cpufreq/cpuinfo_transition_latency", b, sizeof b)) so_kvf(o, "Transition latency", C_GRAY, "%s ns", b);
    so_par(o);
    so_b(o, C_VALUE, "Idle states (cpu0)"); so_par(o);
    for (s = 0; s < 16; s++) {
        char d[128], nm[32], desc[96], lat[32], dis[8] = "0";
        snprintf(d, sizeof d, SYSCPU "/cpu0/cpuidle/state%d", s);
        if (!path_exists(d)) break;
        rd_fmt(nm, sizeof nm, "%s/name", d);
        rd_fmt(desc, sizeof desc, "%s/desc", d);
        rd_fmt(lat, sizeof lat, "%s/latency", d);
        rd_fmt(dis, sizeof dis, "%s/disable", d);
        so_kvf(o, nm, atoi(dis) ? C_GRAY : C_BLACK, "%s  exit latency %s us%s", desc, lat, atoi(dis) ? "  (disabled)" : "");
    }
    so_par(o);
    so_b(o, C_VALUE, "System sleep states"); so_par(o);
    if (rd_str("/sys/power/state", b, sizeof b)) so_kv(o, "Supported", C_BLACK, b);
    if (rd_str("/sys/power/mem_sleep", b, sizeof b)) so_kv(o, "Suspend type", C_BLACK, b);
    if (rd_str("/sys/power/disk", b, sizeof b)) so_kv(o, "Hibernate mode", C_BLACK, b);
}

/* [numa-speeds] - SIV measures per-node memory speed; here: node layout + the last Latency run (never runs it) */
static void s_numa_speeds(srs_out *o)
{
    static const int tabs[] = { 220 };
    char path[1100], *txt;
    const char *h = getenv("HOME");
    so_siv_header(o, "NUMA Memory Speed");
    so_tabs(o, tabs, 1);
    {
        char **e;
        int n = dir_list("/sys/devices/system/node", &e), i, nodes = 0;
        for (i = 0; i < n; i++) nodes += !strncmp(e[i], "node", 4);
        dir_free(e, n);
        so_kvf(o, "NUMA nodes", C_BLACK, "%d%s", nodes, nodes == 1 ? " (uniform memory access - every CPU sees the same latency)" : "");
    }
    snprintf(path, sizeof path, "%s/.cache/srs/bench-latency.txt", h ? h : "");
    if (rd_all(path, &txt) >= 0) {
        so_par(o);
        so_t(o, C_LABEL, "Last Latency run"); so_par(o);
        so_lines(o, C_BLACK, txt);
        free(txt);
    } else {
        so_kv(o, "Memory latency", C_ORANGE, "Not measured yet - Benchmarks & stress > Latency, then Run");
    }
}

const srs_item sec_cpu[] = {
    { "cpus", "hardware", "CPU Summary", B_LINUX, 0, "/proc/cpuinfo", s_cpus, 0, "cpu" },
    { "cpu", "xxb", "CPU Table", B_LINUX, 0, "/sys/devices/system/cpu, cpufreq, amd_pstate", s_cpu, 2000, "cpu" },
    { "processors", "hardware", "Processors", B_LINUX, 0, "/sys/devices/system/cpu/*/topology, /proc/cpuinfo", s_processors, 0, "cpu" },
    { "processor", "hardware", "Processor", B_LINUX, 0, "/proc/cpuinfo, cpu*/cache", s_processor, 0, "cpu" },
    { "features", "hardware", "CPUID Features", B_LINUX, 0, "CPUID", s_features, 0, "cpu" },
    { "present", "hardware", "Processor Features Present", B_LINUX, 0, "/proc/cpuinfo flags", s_present, 0, "cpu" },
    { "cpu-bits", "windows", "CPU Description", B_LINUX, 0, "/proc/cpuinfo", s_cpu_bits, 0, "cpu" },
    { "cpuid", "hardware", "CPUID", B_LINUX, 0, "CPUID (all leaves)", s_cpuid, 0, "cpu" },
    { "cpuid-000", "hardware", "CPUID Leaf 0", B_LINUX, 0, "CPUID", s_cpuid0, 0, "cpu" },
    { "cache", "hardware", "Cache Levels", B_LINUX, 0, "/sys/devices/system/cpu/cpu*/cache", s_cache, 0, "cpu" },
    { "cpu-levels", "hardware", "CPU Idle Levels", B_LINUX, 0, "cpuidle", s_cstates, 0, "cpu" },
    { "cpu-speeds", "hardware", "CPU Speeds", B_LINUX, 0, "cpufreq", s_cpu_speeds, 1000, "cpu" },
    { "cpu-clocks", "hardware", "CPU Clocks", B_LINUX, 0, "cpufreq", s_cpu_speeds, 1000, "cpu" },
    { "cpu-timer", "hardware", "CPU Timers", B_LINUX, 0, "clocksource, TSC", s_cpu_timer, 0, "cpu" },
    { "cpu-volts", "hardware", "CPU Voltages", B_LINUX, 0, "Super-I/O hwmon in0", s_cpu_volts, 1000, "cpu" },
    { "microcode", "hardware", "Microcode", B_LINUX, 0, "/sys/devices/system/cpu/cpu*/microcode", s_microcode, 0, "cpu" },
    { "speculation", "windows", "Speculation Control", B_LINUX, 0, "/sys/devices/system/cpu/vulnerabilities", s_speculation, 0, "cpu" },
    { "turbo-boost", "hardware", "Turbo Boost", B_LINUX, 0, "cpufreq boost, amd_pstate", s_turbo, 0, "cpu" },
    { "amd-cpu", "hardware", "AMD Ryzen Power Management", B_LINUX, 0, "amd_pstate", s_amd_cpu, 0, "cpu" },
    { "amd-smu", "hardware", "AMD SMU", B_LINUX, 0, "/sys/kernel/ryzen_smu_drv", s_amd_smu, 0, "cpu" },
    { "cpu-smu", "hardware", "CPU SMU", B_LINUX, 0, "/sys/kernel/ryzen_smu_drv", s_amd_smu, 0, "cpu" },
    { "amd-pmt", "hardware", "AMD PM Table", B_LINUX, 0, "/sys/kernel/ryzen_smu_drv/pm_table_version; pm_table_size", s_amd_pmt_layout, 0, "cpu" },
    { "amd-pmt-0", "hardware", "AMD PM Table 0", B_LINUX, 0, "/sys/kernel/ryzen_smu_drv/pm_table", s_amd_pmt, 0, "cpu" },
    { "amd-tel-0", "hardware", "AMD Telemetry 0", B_LINUX, 0, "zenergy hwmon", s_amd_tel, 0, "cpu" },
    { "amd-thm-0", "hardware", "AMD Thermal 0", B_LINUX, 0, "k10temp", s_amd_thm, 2000, "cpu" },
    { "amd-tmp", "hardware", "AMD Temperatures", B_LINUX, 0, "k10temp", s_amd_thm, 2000, "cpu" },
    { "amd-fus", "hardware", "AMD Fuses", B_LINUX, 1, "SMN (not read)", s_amd_fus, 0, "cpu" },
    { "cpu-oc-data", "hardware", "CPU OC Data", B_ABSENT, 0, "Intel OC mailbox", s_cpu_oc, 0, "cpu" },
    { "cpu-tweak", "hardware", "CPU Tunables", B_LINUX, 0, "cpufreq, smt, isolcpus", s_cpu_tweak, 0, "cpu" },
    { "msrs", "hardware", "MSRs", B_LINUX, 1, "/dev/cpu/*/msr (root)", s_msrs, 0, "cpu" },
    { "msr-setup", "hardware", "MSR Setup", B_LINUX, 0, "/dev/cpu/*/msr", s_msr_setup, 0, "cpu" },
    { "numa", "hardware", "NUMA", B_LINUX, 0, "/sys/devices/system/node", s_numa, 0, "cpu" },
    { "nodes", "hardware", "Nodes", B_LINUX, 0, "/sys/devices/system/node", s_numa, 0, "cpu" },
    { "group", "hardware", "CPU Groups", B_LINUX, 0, "/sys/devices/system/cpu online/possible/isolated", s_group, 0, "cpu" },
    { "hypervisor", "hardware", "Hypervisor", B_LINUX, 0, "/proc/cpuinfo flags, /dev/kvm, iommu", s_hypervisor, 0, "cpu" },
    { "power-states", "hardware", "CPU power states", B_LINUX, 0, "amd_pstate / acpi_cppc, cpuidle, /sys/power", s_power_states, 0, "cpu" },
    { "numa-speeds", "hardware", "NUMA memory speed", B_LINUX, 0, "/sys/devices/system/node + last Latency run", s_numa_speeds, 0, "cpu" },
    { "cpu-export", "hardware", "CPU Export", B_LINUX, 0, "cpufreq", s_cpu_export, 0, "cpu" },
    { NULL, NULL, NULL, B_LINUX, 0, NULL, NULL, 0, NULL }
};
