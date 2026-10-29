/* burner.c - CPU Stress / CPU Burnin engine (see burner.h). */
#define _GNU_SOURCE
#include "burner.h"
#include "bench_gpu.h"   /* gpu_busy(): the CPU burner does not start while a GPU burn-in / benchmark runs */
#include "sec.h"

#include <errno.h>
#include <fcntl.h>
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#define SRS_X86 1
#endif
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int burn_ncpu(void)
{
    long n = sysconf(_SC_NPROCESSORS_CONF);
    return n < 1 ? 1 : n > BURN_MAXCPU ? BURN_MAXCPU : (int)n;
}

/* ======================================================================
 * Whetstone (H.J. Curnow & B.A. Wichmann, 1976; double precision, MWIPS)
 * ====================================================================== */
static volatile double g_sink;

static void pa(double e[5], double t, double t2)
{
    int j;
    for (j = 0; j < 6; j++) {
        e[1] = (e[1] + e[2] + e[3] - e[4]) * t;
        e[2] = (e[1] + e[2] - e[3] + e[4]) * t;
        e[3] = (e[1] - e[2] + e[3] + e[4]) * t;
        e[4] = (-e[1] + e[2] + e[3] + e[4]) / t2;
    }
}

static void p3(double x, double y, double *z, double t, double t2)
{
    double x1 = x, y1 = y;
    x1 = t * (x1 + y1);
    y1 = t * (x1 + y1);
    *z = (x1 + y1) / t2;
}

static void p0(double e[5], int j, int k, int l) { e[j] = e[k]; e[k] = e[l]; e[l] = e[j]; }

double whetstone_mwips(long loop, double *secs)
{
    double t = 0.499975, t1 = 0.50025, t2 = 2.0, x1, x2, x3, x4, x, y, z, e1[5];
    long i, n2 = 12 * loop, n3 = 14 * loop, n4 = 345 * loop, n6 = 210 * loop, n7 = 32 * loop,
         n8 = 899 * loop, n9 = 616 * loop, n11 = 93 * loop;
    volatile int j, k, l;   /* volatile: keep gcc from folding the integer modules away */
    double t0 = now_s(), dt;

    x1 = 1.0; x2 = -1.0; x3 = -1.0; x4 = -1.0;
    for (i = 1; i <= n2; i++) {                       /* module 2: array elements */
        x1 = (x1 + x2 + x3 - x4) * t;
        x2 = (x1 + x2 - x3 + x4) * t;
        x3 = (x1 - x2 + x3 + x4) * t;
        x4 = (-x1 + x2 + x3 + x4) * t;
    }
    e1[1] = 1.0; e1[2] = -1.0; e1[3] = -1.0; e1[4] = -1.0;
    for (i = 1; i <= n3; i++) pa(e1, t, t2);          /* module 3: array as parameter */
    j = 1;
    for (i = 1; i <= n4; i++) {                       /* module 4: conditional jumps */
        j = j == 1 ? 2 : 3;
        j = j > 2 ? 0 : 1;
        j = j < 1 ? 1 : 0;
    }
    j = 1; k = 2; l = 3;
    for (i = 1; i <= n6; i++) {                       /* module 6: integer arithmetic */
        j = j * (k - j) * (l - k);
        k = l * k - (l - j) * k;
        l = (l - k) * (k + j);
        e1[l - 1 >= 1 && l - 1 <= 4 ? l - 1 : 1] = j + k + l;
        e1[k - 1 >= 1 && k - 1 <= 4 ? k - 1 : 1] = j * k * l;
    }
    x = 0.5; y = 0.5;
    for (i = 1; i <= n7; i++) {                       /* module 7: trig functions */
        x = t * atan(t2 * sin(x) * cos(x) / (cos(x + y) + cos(x - y) - 1.0));
        y = t * atan(t2 * sin(y) * cos(y) / (cos(x + y) + cos(x - y) - 1.0));
    }
    x = 1.0; y = 1.0; z = 1.0;
    for (i = 1; i <= n8; i++) p3(x, y, &z, t, t2);    /* module 8: procedure calls */
    j = 1; k = 2; l = 3;
    e1[1] = 1.0; e1[2] = 2.0; e1[3] = 3.0;
    for (i = 1; i <= n9; i++) p0(e1, j, k, l);        /* module 9: array references */
    x = 0.75;
    for (i = 1; i <= n11; i++) x = sqrt(exp(log(x) / t1)); /* module 11: standard functions */
    g_sink = x1 + x2 + x3 + x4 + e1[1] + e1[4] + x + y + z + j + k + l;
    dt = now_s() - t0;
    if (secs) *secs = dt;
    return dt > 0 ? 100.0 * (double)loop / dt / 1000.0 : 0;   /* KIPS = 100*LOOP/t, MWIPS = KIPS/1000 */
}

/* ======================================================================
 * APIC IDs from /proc/cpuinfo, checked against CPUID in the worker
 * ====================================================================== */
static int g_apic[BURN_MAXCPU];

static void load_apic_map(void)
{
    char *txt, *p;
    int cpu = -1;
    memset(g_apic, -1, sizeof g_apic);
    if (rd_all("/proc/cpuinfo", &txt) < 0) return;
    for (p = strtok(txt, "\n"); p; p = strtok(NULL, "\n")) {
        if (!strncmp(p, "processor", 9)) cpu = atoi(strchr(p, ':') ? strchr(p, ':') + 1 : "0");
        else if (!strncmp(p, "apicid", 6) && cpu >= 0 && cpu < BURN_MAXCPU)
            g_apic[cpu] = atoi(strchr(p, ':') ? strchr(p, ':') + 1 : "-1");
    }
    free(txt);
}

static int cpuid_apic(void)
{
#ifdef SRS_X86
    unsigned a, b, c, d;
    if (__get_cpuid_count(0x0B, 0, &a, &b, &c, &d) && b) return (int)d;   /* x2APIC id */
    if (__get_cpuid(1, &a, &b, &c, &d)) return (int)(b >> 24);
#endif
    return -1;   /* non-x86: the sched_getcpu() check still applies */
}

/* ======================================================================
 * workers
 * ====================================================================== */
typedef struct {
    pthread_t tid;
    int idx, cpu, started;
    volatile int run;          /* 1 = busy, 0 = idle (cooling), -1 = exit */
    burn_thread st;
    double sum;
    pthread_mutex_t mu;
} worker;

static worker g_w[BURN_MAXCPU];
static int g_nw;
static burn_cfg g_cfg;
static int g_cpus[BURN_MAXCPU], g_ncpus;
static int g_active;   /* read and written only through the __atomic builtins (seq_cst): it pairs with gpu_claim() in the GPU tests */

static int pin(int cpu)
{
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(cpu, &s);
    return pthread_setaffinity_np(pthread_self(), sizeof s, &s);
}

static void *worker_main(void *arg)
{
    worker *w = arg;
    int swap_i = w->idx;
    pin(w->cpu);
    while (w->run != -1) {
        double secs, r;
        int target = w->cpu, here;
        if (w->run == 0) { usleep(100000); continue; }
        if (g_cfg.mode == BURN_SWAP && g_ncpus > 1) {   /* keep moving between enabled CPUs */
            swap_i = (swap_i + 1) % g_ncpus;
            target = g_cpus[swap_i];
            pin(target);
        }
        r = whetstone_mwips(2000, &secs);
        here = sched_getcpu();
        pthread_mutex_lock(&w->mu);
        w->st.cur_cpu = here;
        w->st.passes++;
        w->st.mwips_last = r;
        w->sum += r;
        w->st.mwips_avg = w->sum / (double)w->st.passes;
        if (w->st.passes == 1 || r < w->st.mwips_min) w->st.mwips_min = r;
        if (r > w->st.mwips_max) w->st.mwips_max = r;
        {   /* affinity check: still on the pinned CPU, and CPUID APIC matches that CPU */
            int apic = cpuid_apic(), ok = here == target && (g_apic[target] < 0 || apic < 0 ||
                       apic == g_apic[target] || (apic & 0xFF) == g_apic[target]);
            w->st.apic_ok = ok;
            if (!ok) w->st.apic_errors++;
        }
        pthread_mutex_unlock(&w->mu);
    }
    return NULL;
}

/* ======================================================================
 * sensor sampling (read-only sysfs/proc)
 * ====================================================================== */
typedef struct { unsigned long long user, sys, idle, total; } cpustat;
static cpustat g_prev[BURN_MAXCPU + 1];
static int g_have_prev;

static int read_stat(cpustat *out, int max)
{
    char *txt, *p;
    int n = 0;
    if (rd_all("/proc/stat", &txt) < 0) return 0;
    for (p = strtok(txt, "\n"); p && n < max; p = strtok(NULL, "\n")) {
        unsigned long long u, ni, s, id, io, irq, sirq, st;
        int idx;
        if (strncmp(p, "cpu", 3)) break;
        idx = p[3] == ' ' ? 0 : atoi(p + 3) + 1;   /* "cpu" total line, then "cpuN" */
        if (sscanf(strchr(p, ' '), " %llu %llu %llu %llu %llu %llu %llu %llu", &u, &ni, &s, &id, &io, &irq, &sirq, &st) < 4) continue;
        if (idx < max) {
            out[idx].user = u + ni;
            out[idx].sys = s + irq + sirq;
            out[idx].idle = id + io;
            out[idx].total = u + ni + s + id + io + irq + sirq + st;
            if (idx + 1 > n) n = idx + 1;
        }
    }
    free(txt);
    return n;
}

/* SRSDEF.ini [SRSDEF] "SIO Sensors=nct6799:AUXTIN1,..." re-enables inputs that are really wired */
static int sio_allowed(const char *chip, const char *label)
{
    static char list[512];
    static int loaded;
    char want[160];
    if (!loaded) {
        char *ini = NULL, *p;
        const char *cfg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
        char path[1024];
        loaded = 1;
        if (cfg) snprintf(path, sizeof path, "%s/srs/SRSDEF.ini", cfg);
        else snprintf(path, sizeof path, "%s/.config/srs/SRSDEF.ini", home ? home : "");
        if (rd_all(path, &ini) >= 0 && (p = strstr(ini, "SIO Sensors="))) {
            size_t l = strcspn(p + 12, "\r\n");
            snprintf(list, sizeof list, ",%.*s,", (int)l, p + 12);
        }
        free(ini);
    }
    snprintf(want, sizeof want, ",%s:%s,", chip, label);
    return *list && strstr(list, want) != NULL;
}

static double hottest_hwmon(const char *const *names, char *which, size_t wsz, int match)
{
    char **hw;
    int n = dir_list("/sys/class/hwmon", &hw), i;
    double best = -1000;
    for (i = 0; i < n; i++) {
        char nm[64], path[512], **f;
        int nf, j, k, hit = 0;
        snprintf(path, sizeof path, "/sys/class/hwmon/%s/name", hw[i]);
        if (!rd_str(path, nm, sizeof nm)) continue;
        for (k = 0; names[k]; k++) if (!strncmp(nm, names[k], strlen(names[k]))) hit = 1;
        if (hit != match) continue;
        if (!match) {   /* "SIO sensor" = Super-I/O / board EC chips only */
            static const char *const sio[] = { "nct", "it87", "it86", "it88", "w83", "f71", "f81", "sch5", "dme1737",
                                               "asus", "asusec", "gigabyte_wmi", "dell_smm", "thinkpad", "lm78", NULL };
            int ok = 0, q;
            for (q = 0; sio[q]; q++) if (!strncmp(nm, sio[q], strlen(sio[q]))) ok = 1;
            if (!ok) continue;
        }
        snprintf(path, sizeof path, "/sys/class/hwmon/%s", hw[i]);
        nf = dir_list(path, &f);
        for (j = 0; j < nf; j++) {
            long v;
            char p2[600];
            if (strncmp(f[j], "temp", 4) || !strstr(f[j], "_input")) continue;
            snprintf(p2, sizeof p2, "/sys/class/hwmon/%s/%s", hw[i], f[j]);
            if (rd_long(p2, &v) && v > 5000 && v < 110000) {
                char lab[64] = "", lp[600];
                snprintf(lp, sizeof lp, "/sys/class/hwmon/%s/%.*slabel", hw[i], (int)(strstr(f[j], "_input") - f[j] + 1), f[j]);
                rd_str(lp, lab, sizeof lab);
                /* Super-I/O chips expose floating thermistor inputs (AUXTINn read 80+ C when unconnected on ASUS
                   boards), CPU copies (PECI/TSI) and dead PCH channels: only real board sensors count */
                if (!match && (!strncmp(lab, "AUXTIN", 6) || !strncmp(lab, "PECI", 4) || !strncmp(lab, "TSI", 3) ||
                               !strncmp(lab, "PCH", 3)) && !sio_allowed(nm, lab))
                    continue;
                if (v / 1000.0 > best) {
                    best = v / 1000.0;
                    snprintf(which, wsz, "%s %s", nm, *lab ? lab : f[j]);
                }
            }
        }
        dir_free(f, nf);
    }
    dir_free(hw, n);
    return best;
}

static void gpu_sample(burn_sample *s)
{
    char **c;
    int n = dir_list("/sys/class/drm", &c), i;
    s->gpu_temp = s->gpu_load = s->gpu_mhz = 0;
    for (i = 0; i < n; i++) {
        char p[512], buf[4096], **h;
        long v;
        int nh, j;
        if (strncmp(c[i], "card", 4) || strchr(c[i], '-')) continue;
        snprintf(p, sizeof p, "/sys/class/drm/%s/device/gpu_busy_percent", c[i]);
        if (!rd_long(p, &v)) continue;
        s->gpu_load = (double)v;
        snprintf(p, sizeof p, "/sys/class/drm/%s/device/pp_dpm_sclk", c[i]);
        {
            char *txt, *line;
            if (rd_all(p, &txt) >= 0) {
                for (line = strtok(txt, "\n"); line; line = strtok(NULL, "\n"))
                    if (strchr(line, '*')) { char *m = strchr(line, ':'); if (m) s->gpu_mhz = atof(m + 1); }
                free(txt);
            }
        }
        snprintf(p, sizeof p, "/sys/class/drm/%s/device/hwmon", c[i]);
        nh = dir_list(p, &h);
        for (j = 0; j < nh; j++) {
            snprintf(buf, sizeof buf, "/sys/class/drm/%s/device/hwmon/%s/temp2_input", c[i], h[j]);   /* junction */
            if (!rd_long(buf, &v)) {
                snprintf(buf, sizeof buf, "/sys/class/drm/%s/device/hwmon/%s/temp1_input", c[i], h[j]);
                if (!rd_long(buf, &v)) continue;
            }
            s->gpu_temp = v / 1000.0;
        }
        dir_free(h, nh);
        break;
    }
    dir_free(c, n);
}

void burn_sample_now(burn_sample *s)
{
    static const char *const cpu_names[] = { "k10temp", "coretemp", "zenpower", NULL };
    cpustat cur[BURN_MAXCPU + 1];
    int n = read_stat(cur, BURN_MAXCPU + 1), i, cnt = 0;
    double mhz = 0;
    memset(s, 0, sizeof *s);
    if (g_have_prev && n) {
        unsigned long long dt = cur[0].total - g_prev[0].total, di = cur[0].idle - g_prev[0].idle;
        s->cpu_load = dt ? 100.0 * (double)(dt - di) / (double)dt : 0;
    }
    for (i = 0; i < burn_ncpu(); i++) {
        long v;
        char p[256];
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
        if (rd_long(p, &v) && v / 1000.0 > mhz) mhz = v / 1000.0;
        cnt++;
    }
    (void)cnt;
    s->cpu_mhz = mhz;
    s->cpu_temp = hottest_hwmon(cpu_names, s->cpu_temp_name, sizeof s->cpu_temp_name, 1);
    s->sio_temp = hottest_hwmon(cpu_names, s->sio_name, sizeof s->sio_name, 0);
    if (s->cpu_temp < -999) s->cpu_temp = 0;
    if (s->sio_temp < -999) s->sio_temp = 0;
    gpu_sample(s);
    if (n) { memcpy(g_prev, cur, sizeof(cpustat) * (size_t)n); g_have_prev = 1; }
}

/* ======================================================================
 * run control
 * ====================================================================== */
static double g_t0, g_phase_t0, g_last_tick;
static int g_state = BURN_IDLE, g_cycle, g_nsamp;
static burn_sample g_sum, g_max, g_roll;
static char g_status[160] = "Idle";
static char g_saved[1100];   /* where this run's summary was saved ("" = not yet) */
static pid_t g_gpu_pid;
static double g_gpu_t0, g_gpu_stop_t;   /* g_gpu_stop_t: when Stop asked the GPU part to end (0 = not stopping) */
static char g_gpu_msg[300], g_gpu_log[600];
static int g_gpu_bad, g_gpu_killed, g_gpu_cooling;
static int g_gpu_want;   /* a burn phase began while the last GPU run was still ending: start it once that one is reaped */
static int g_gpu_exit = -1;   /* the built-in GPU Burnin's last exit code (GPUB_EXIT_*), -1 = none yet */

/* the GPU part's own output goes to ~/.cache/srs/gpu-load.log so errors are not lost */
static void gpu_log_path(void)
{
    const char *x = getenv("XDG_CACHE_HOME"), *h = getenv("HOME");
    char dir[560];
    if (x && *x) snprintf(dir, sizeof dir, "%s", x);
    else snprintf(dir, sizeof dir, "%s/.cache", h && *h ? h : "/tmp");
    mkdir(dir, 0755);
    strncat(dir, "/srs", sizeof dir - strlen(dir) - 1);
    mkdir(dir, 0755);
    snprintf(g_gpu_log, sizeof g_gpu_log, "%s/gpu-load.log", dir);
}

static const char *gpu_name(void) { return g_cfg.gpu_label && *g_cfg.gpu_label ? g_cfg.gpu_label : "GPU load command"; }
static int gpu_configured(void) { return g_cfg.gpu_argv ? g_cfg.gpu_argv[0] != NULL : g_cfg.gpu_cmd && *g_cfg.gpu_cmd; }

/* reap the GPU part if it ended and say why. Never blocks: a GPU program stuck in a driver call must not
   freeze the window. A part asked to stop gets SIGKILL when it has not ended after a grace time. */
static void gpu_check(void)
{
    int ws;
    pid_t r;
    if (!g_gpu_pid) return;
    r = waitpid(g_gpu_pid, &ws, WNOHANG);
    if (r != g_gpu_pid) {
        if (g_gpu_stop_t > 0 && !g_gpu_killed && now_s() - g_gpu_stop_t > (g_cfg.gpu_argv ? 20 : 5)) {
            kill(-g_gpu_pid, SIGKILL);
            g_gpu_killed = 1;
            snprintf(g_gpu_msg, sizeof g_gpu_msg, "%s did not end after Stop and was killed  - see %s", gpu_name(), g_gpu_log);
            g_gpu_bad = 1;
        }
        return;
    }
    g_gpu_pid = 0;
    if (g_gpu_stop_t > 0 && !g_gpu_killed) {   /* asked to stop: that is the expected end */
        g_gpu_stop_t = 0;
        if (g_cfg.gpu_argv && WIFEXITED(ws)) g_gpu_exit = WEXITSTATUS(ws);
        if (g_cfg.gpu_argv && g_gpu_exit == GPUB_EXIT_FAIL) {
            g_gpu_bad = 1;
            snprintf(g_gpu_msg, sizeof g_gpu_msg, "%s: FAIL - compute errors found (see the GPU result below)", gpu_name());
        } else if (g_gpu_cooling)
            snprintf(g_gpu_msg, sizeof g_gpu_msg, "%s paused for the cool phase; it starts again with the next burn phase", gpu_name());
        else snprintf(g_gpu_msg, sizeof g_gpu_msg, "%s stopped with the CPU load", gpu_name());
        return;
    }
    g_gpu_stop_t = 0;
    if (g_gpu_killed) return;   /* the message was set when it was killed */
    if (WIFEXITED(ws)) {
        int rc = WEXITSTATUS(ws);
        double secs = now_s() - g_gpu_t0;
        if (g_cfg.gpu_argv) {   /* the built-in GPU Burnin: its exit code says how the run ended */
            g_gpu_exit = rc;
            g_gpu_bad = rc != GPUB_EXIT_PASS && rc != GPUB_EXIT_STOPPED;
            switch (rc) {
            case GPUB_EXIT_PASS: snprintf(g_gpu_msg, sizeof g_gpu_msg, "%s finished after %.0f s: PASS - no compute errors", gpu_name(), secs); break;
            case GPUB_EXIT_STOPPED: snprintf(g_gpu_msg, sizeof g_gpu_msg, "%s stopped after %.0f s - no compute errors so far", gpu_name(), secs); break;
            case GPUB_EXIT_FAIL: snprintf(g_gpu_msg, sizeof g_gpu_msg, "%s: FAIL - compute errors found (see the GPU result below)", gpu_name()); break;
            case GPUB_EXIT_NOTSTARTED: snprintf(g_gpu_msg, sizeof g_gpu_msg, "%s could not start (the reason is below)", gpu_name()); break;
            case GPUB_EXIT_EARLY: snprintf(g_gpu_msg, sizeof g_gpu_msg, "%s ended early by a safety rule after %.0f s (the reason is below)", gpu_name(), secs); break;
            default: snprintf(g_gpu_msg, sizeof g_gpu_msg, "%s ended with exit code %d after %.0f s  - see %s", gpu_name(), rc, secs, g_gpu_log);
            }
            return;
        }
        g_gpu_bad = rc != 0;
        if (rc == 127) snprintf(g_gpu_msg, sizeof g_gpu_msg, "GPU load command not found (exit 127): %s  - see %s", g_cfg.gpu_cmd, g_gpu_log);
        else if (rc == 126) snprintf(g_gpu_msg, sizeof g_gpu_msg, "GPU load command is not executable (exit 126): %s  - see %s", g_cfg.gpu_cmd, g_gpu_log);
        else if (rc) snprintf(g_gpu_msg, sizeof g_gpu_msg, "GPU load command failed (exit %d) after %.0f s  - see %s", rc, secs, g_gpu_log);
        else snprintf(g_gpu_msg, sizeof g_gpu_msg, "GPU load command finished (exit 0) after %.0f s; the CPU run continues", secs);
    } else if (WIFSIGNALED(ws)) {
        g_gpu_bad = 1;
        snprintf(g_gpu_msg, sizeof g_gpu_msg, "%s was killed by signal %d  - see %s", gpu_name(), WTERMSIG(ws), g_gpu_log);
    }
}

int burn_active(void) { return __atomic_load_n(&g_active, __ATOMIC_SEQ_CST); }
int burn_gpu_running(void) { gpu_check(); return g_gpu_pid != 0; }
/* any thread: is a GPU part process still there? Reads only (no reaping; burn_tick on the main thread reaps it) */
int burn_gpu_pid_set(void) { return __atomic_load_n(&g_gpu_pid, __ATOMIC_SEQ_CST) != 0; }

void burn_cfg_default(burn_cfg *c)
{
    int i;
    memset(c, 0, sizeof *c);
    c->ncpu = burn_ncpu();
    for (i = 0; i < c->ncpu; i++) c->enabled[i] = 1;
    c->mode = BURN_SAME;
    c->burn_s = 15 * 60;
    c->cool_s = 60;
    c->cycles = 1;
    c->temp_limit_c = 90;
    c->cap_s = 750.0 * 3600;
    c->gpu_follow = 1;
}

static void set_run(int v)
{
    int i;
    for (i = 0; i < g_nw; i++) g_w[i].run = v;
}

/* start (on) or stop the GPU part: the custom command through /bin/sh, the built-in GPU Burnin run directly.
   It gets its own process group, so Stop reaches everything it started, and it ends when SRS ends. */
static void gpu_load(int on)
{
    if (on && gpu_configured() && g_gpu_pid) { g_gpu_want = 1; return; }   /* retried by burn_tick */
    if (!on) g_gpu_want = 0;
    if (on && gpu_configured() && !g_gpu_pid) {
        pid_t p, parent = getpid();
        gpu_log_path();
        p = fork();
        if (p == 0) {
            int fd = open(g_gpu_log, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
            setpgid(0, 0);
            prctl(PR_SET_PDEATHSIG, SIGTERM);   /* SRS closed or crashed: the GPU load ends too */
            if (getppid() != parent) _exit(GPUB_EXIT_STOPPED);
            if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
            if (g_cfg.gpu_argv) execv(g_cfg.gpu_argv[0], (char *const *)g_cfg.gpu_argv);
            else execl("/bin/sh", "sh", "-c", g_cfg.gpu_cmd, (char *)NULL);
            _exit(127);
        }
        if (p > 0) {
            g_gpu_pid = p;
            g_gpu_t0 = now_s();
            g_gpu_bad = g_gpu_killed = g_gpu_cooling = 0;
            g_gpu_stop_t = 0;
            if (g_cfg.gpu_argv) snprintf(g_gpu_msg, sizeof g_gpu_msg, "%s running (pid %d)", gpu_name(), (int)p);
            else snprintf(g_gpu_msg, sizeof g_gpu_msg, "GPU load command running (pid %d): %s", (int)p, g_cfg.gpu_cmd);
        } else {
            g_gpu_bad = 1;
            snprintf(g_gpu_msg, sizeof g_gpu_msg, "%s could not be started: %s", gpu_name(), strerror(errno));
        }
    } else if (!on && g_gpu_pid) {
        gpu_check();   /* it may already have ended by itself */
        if (g_gpu_pid && g_gpu_stop_t <= 0) {
            /* the built-in one ends its run cleanly on SIGTERM (result file and history line); gpu_check
               reaps it later and kills it if it hangs */
            kill(-g_gpu_pid, SIGTERM);
            g_gpu_stop_t = now_s();
            g_gpu_cooling = g_state == BURN_COOLING;
            snprintf(g_gpu_msg, sizeof g_gpu_msg, "%s stopping...", gpu_name());
            gpu_check();
        }
    }
}

static void finish(int state, const char *why);

int burn_start(const burn_cfg *c)
{
    int i;
    if (burn_active()) return -1;
    if (gpu_busy()) {   /* not while a GPU burn-in / benchmark runs: both together are too much heat and power */
        char t[120];
        gpu_claim_text(gpu_claim_holder(), t, sizeof t);
        snprintf(g_status, sizeof g_status, "[CPU Burnin] not started: %s", t);
        return -1;
    }
    gpu_check();
    if (g_gpu_pid) {   /* the GPU part of the last run has not ended yet (it gets killed after a grace time) */
        snprintf(g_status, sizeof g_status, "Not started: the GPU part of the last run is still stopping - try again in a few seconds");
        return -1;
    }
    g_cfg = *c;
    if (g_cfg.cycles < 1) g_cfg.cycles = 1;
    load_apic_map();
    g_ncpus = 0;
    for (i = 0; i < c->ncpu && i < BURN_MAXCPU; i++)
        if (c->enabled[i]) g_cpus[g_ncpus++] = i;
    if (!g_ncpus && !gpu_configured()) return -1;   /* no CPU is fine when the GPU part runs on its own */
    g_gpu_exit = -1;
    g_nw = g_ncpus;
    for (i = 0; i < g_nw; i++) {
        memset(&g_w[i], 0, sizeof g_w[i]);
        pthread_mutex_init(&g_w[i].mu, NULL);
        g_w[i].idx = i;
        g_w[i].cpu = g_cpus[i];
        g_w[i].st.cpu = g_cpus[i];
        g_w[i].st.busy = 1;
        g_w[i].run = 1;
        if (pthread_create(&g_w[i].tid, NULL, worker_main, &g_w[i])) { g_nw = i; break; }
        g_w[i].started = 1;
    }
    memset(&g_sum, 0, sizeof g_sum);
    memset(&g_max, 0, sizeof g_max);
    memset(&g_roll, 0, sizeof g_roll);
    g_nsamp = 0;
    g_saved[0] = 0;
    g_cycle = 1;
    g_gpu_msg[0] = 0;
    g_gpu_bad = 0;
    g_t0 = g_phase_t0 = g_last_tick = now_s();
    g_state = BURN_RUNNING;
    __atomic_store_n(&g_active, 1, __ATOMIC_SEQ_CST);
    if (gpu_busy()) {   /* a GPU test took the GPU at the same moment (it checks burn_active() after its claim): it wins */
        finish(BURN_IDLE, "[CPU Burnin] not started: a GPU test is running");
        return -1;
    }
    gpu_load(1);
    if (!g_nw) snprintf(g_status, sizeof g_status, "[Stress & Burnin] GPU only - no CPU load");
    else snprintf(g_status, sizeof g_status, "[CPU Burnin] will use %d worker thread%s", g_nw, g_nw == 1 ? "" : "s");
    return 0;
}

static void finish(int state, const char *why)
{
    int i;
    set_run(-1);
    gpu_load(0);
    if (g_cfg.gpu_argv) {   /* give the built-in GPU Burnin up to 3 s to end cleanly, so its verdict counts in this run's result */
        for (i = 0; i < 60 && g_gpu_pid; i++) { usleep(50000); gpu_check(); }
        if (g_gpu_exit == GPUB_EXIT_FAIL && state != BURN_FAIL) { state = BURN_FAIL; why = "Fail - GPU Burnin found compute errors"; }
    }
    for (i = 0; i < g_nw; i++) if (g_w[i].started) pthread_join(g_w[i].tid, NULL);
    g_state = state;
    __atomic_store_n(&g_active, 0, __ATOMIC_SEQ_CST);
    snprintf(g_status, sizeof g_status, "%s", why);
}

void burn_stop(void)
{
    if (burn_active()) finish(BURN_STOPPED, "Stopped by user");
}

#define ROLL(a, b) ((a) = (g_nsamp <= 1 ? (b) : (a) * 0.9 + (b) * 0.1))
#define MAXV(a, b) do { if ((b) > (a)) (a) = (b); } while (0)

void burn_tick(burn_status *st)
{
    double t = now_s();
    burn_sample s;
    int i, apic_bad = 0;
    burn_sample_now(&s);
    memset(st, 0, sizeof *st);
    gpu_check();
    if (burn_active()) {
        g_nsamp++;
        g_sum.cpu_load += s.cpu_load; g_sum.cpu_mhz += s.cpu_mhz; g_sum.cpu_temp += s.cpu_temp;
        g_sum.sio_temp += s.sio_temp; g_sum.gpu_temp += s.gpu_temp; g_sum.gpu_load += s.gpu_load; g_sum.gpu_mhz += s.gpu_mhz;
        ROLL(g_roll.cpu_load, s.cpu_load); ROLL(g_roll.cpu_temp, s.cpu_temp); ROLL(g_roll.sio_temp, s.sio_temp);
        ROLL(g_roll.gpu_temp, s.gpu_temp); ROLL(g_roll.gpu_load, s.gpu_load); ROLL(g_roll.cpu_mhz, s.cpu_mhz);
        ROLL(g_roll.gpu_mhz, s.gpu_mhz);
        MAXV(g_max.cpu_load, s.cpu_load); MAXV(g_max.cpu_mhz, s.cpu_mhz); MAXV(g_max.cpu_temp, s.cpu_temp);
        MAXV(g_max.sio_temp, s.sio_temp); MAXV(g_max.gpu_temp, s.gpu_temp); MAXV(g_max.gpu_load, s.gpu_load);
        MAXV(g_max.gpu_mhz, s.gpu_mhz);
        for (i = 0; i < g_nw; i++) apic_bad += g_w[i].st.apic_errors;
        /* safety first */
        if (g_cfg.gpu_argv && g_gpu_exit == GPUB_EXIT_FAIL) {
            finish(BURN_FAIL, "Fail - GPU Burnin found compute errors");
        } else if (g_cfg.gpu_argv && g_gpu_exit == GPUB_EXIT_EARLY) {
            finish(BURN_FAIL, "Fail - GPU Burnin ended early by a safety rule (temperature, driver error or a GPU that stopped answering)");
        } else if (!g_nw && !g_gpu_pid) {   /* GPU only: the run is over when the GPU part has ended */
            if (!g_cfg.gpu_argv) finish(g_gpu_bad ? BURN_FAIL : BURN_PASS, g_gpu_bad ? "Fail - the GPU load command failed" : "Pass - the GPU load command finished");
            else if (g_gpu_exit == GPUB_EXIT_PASS) finish(BURN_PASS, "Pass - GPU Burnin finished with no compute errors");
            else if (g_gpu_exit == GPUB_EXIT_STOPPED) finish(BURN_STOPPED, "Stopped - GPU Burnin was stopped");
            else finish(BURN_FAIL, g_gpu_exit == GPUB_EXIT_NOTSTARTED ? "Not run - GPU Burnin could not start" : "Fail - GPU Burnin ended with an error");
        } else if (g_cfg.temp_limit_c > 0 && s.cpu_temp >= g_cfg.temp_limit_c) {
            char why[160];
            snprintf(why, sizeof why, "Fail - %s %.1f C reached the %.0f C limit", s.cpu_temp_name, s.cpu_temp, g_cfg.temp_limit_c);
            finish(BURN_FAIL, why);
        } else if (apic_bad) {
            finish(BURN_FAIL, "Fail - a worker thread ran on the wrong CPU (affinity / APIC check)");
        } else if (t - g_t0 >= g_cfg.cap_s) {
            finish(BURN_PASS, "Pass - time cap reached");
        } else if (g_state == BURN_RUNNING && g_cfg.burn_s > 0 && t - g_phase_t0 >= g_cfg.burn_s) {
            if (g_cycle >= g_cfg.cycles) finish(BURN_PASS, "Pass - Finished");
            else { g_state = BURN_COOLING; g_phase_t0 = t; set_run(0); if (g_cfg.gpu_follow) gpu_load(0); snprintf(g_status, sizeof g_status, "Cooling"); }
        } else if (g_state == BURN_COOLING && t - g_phase_t0 >= g_cfg.cool_s) {
            g_state = BURN_RUNNING; g_phase_t0 = t; g_cycle++; set_run(1); if (g_cfg.gpu_follow) gpu_load(1);
            snprintf(g_status, sizeof g_status, "Burning, cycle %d of %d", g_cycle, g_cfg.cycles);
        }
    }
    if (g_gpu_want && !g_gpu_pid && g_state == BURN_RUNNING) { g_gpu_want = 0; gpu_load(1); }   /* short Cool Time */
    else if (g_gpu_want && g_state != BURN_RUNNING) g_gpu_want = 0;
    st->state = g_state;
    st->cycle = g_cycle;
    st->elapsed = g_nsamp ? t - g_t0 : 0;
    st->phase_left = g_state == BURN_RUNNING ? (g_cfg.burn_s > 0 ? g_cfg.burn_s - (t - g_phase_t0) : -1)
                   : g_state == BURN_COOLING ? g_cfg.cool_s - (t - g_phase_t0) : 0;
    st->remaining = g_cfg.burn_s > 0 && (g_state == BURN_RUNNING || g_state == BURN_COOLING)
                  ? (g_cfg.cycles - g_cycle) * (g_cfg.burn_s + g_cfg.cool_s) + st->phase_left : 0;
    st->cur = s;
    if (g_nsamp) {
        double n = g_nsamp;
        st->avg.cpu_load = g_sum.cpu_load / n; st->avg.cpu_mhz = g_sum.cpu_mhz / n; st->avg.cpu_temp = g_sum.cpu_temp / n;
        st->avg.sio_temp = g_sum.sio_temp / n; st->avg.gpu_temp = g_sum.gpu_temp / n; st->avg.gpu_load = g_sum.gpu_load / n;
        st->avg.gpu_mhz = g_sum.gpu_mhz / n;
    }
    st->roll = g_roll;
    st->max = g_max;
    snprintf(st->status, sizeof st->status, "%s", g_status);
    snprintf(st->gpu_msg, sizeof st->gpu_msg, "%s", g_gpu_msg);
    st->gpu_bad = g_gpu_bad;
    st->gpu_running = g_gpu_pid != 0;
    st->gpu_exit = g_gpu_exit;
    st->nthreads = g_nw;
    for (i = 0; i < g_nw; i++) {
        pthread_mutex_lock(&g_w[i].mu);
        st->th[i] = g_w[i].st;
        st->th[i].busy = g_w[i].run == 1;
        pthread_mutex_unlock(&g_w[i].mu);
    }
    {   /* per-CPU user / kernel % and clock, like SIV's "CPU-n User x% Kernel y% @ f" */
        static cpustat pc[BURN_MAXCPU + 1];
        static int havepc;
        cpustat cc[BURN_MAXCPU + 1];
        int n = read_stat(cc, BURN_MAXCPU + 1);
        for (i = 1; i < n && i - 1 < BURN_MAXCPU; i++) {
            unsigned long long dt = cc[i].total - pc[i].total;
            char p[256];
            long v;
            if (havepc && dt) {
                st->per_cpu_user[i - 1] = 100.0 * (double)(cc[i].user - pc[i].user) / (double)dt;
                st->per_cpu_sys[i - 1] = 100.0 * (double)(cc[i].sys - pc[i].sys) / (double)dt;
            }
            snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i - 1);
            if (rd_long(p, &v)) st->per_cpu_mhz[i - 1] = v / 1000.0;
        }
        if (n) { memcpy(pc, cc, sizeof(cpustat) * (size_t)n); havepc = 1; }
    }
    g_last_tick = t;
}

/* ======================================================================
 * summary text (srs-gui's Burnin / Stress window, its Copy button and the saved file)
 * ====================================================================== */
static void fmt_hms(double s, char *b, size_t n)
{
    long t = s < 0 ? 0 : (long)s;
    snprintf(b, n, "%02ld:%02ld:%02ld", t / 3600, (t / 60) % 60, t % 60);
}

/* The Burnin / Stress summary: status, the Current / Overall Average / Rolling Average / Maximum
   table and the per-thread table. srs-gui shows it live; burn_save_summary writes it to a file. */
void burn_render(srs_out *o, const burn_status *st)
{
    static const int tabs[] = { 200, 330, 460, 590, 720 };
    static const int t2[] = { 70, 130, 200, 270, 350, 440, 530, 620, 710, 790 };
    static const char *const states[] = { "Idle", "Burning", "Cooling", "Pass", "Fail", "Stopped" };
    static const char *const rows[7] = { "CPU Load", "CPU Frequency", "Hottest Core Temp", "Hottest SIO Sensor Temp", "GPU Load", "GPU Temp", "GPU Frequency" };
    static const char *const fmt[7] = { "%.1f%%", "%.0f MHz", "%.1f°C", "%.1f°C", "%.0f%%", "%.1f°C", "%.0f MHz" };
    char a[64], b[64];
    int i;
    double vs[7][4] = {
        { st->cur.cpu_load, st->avg.cpu_load, st->roll.cpu_load, st->max.cpu_load },
        { st->cur.cpu_mhz, st->avg.cpu_mhz, st->roll.cpu_mhz, st->max.cpu_mhz },
        { st->cur.cpu_temp, st->avg.cpu_temp, st->roll.cpu_temp, st->max.cpu_temp },
        { st->cur.sio_temp, st->avg.sio_temp, st->roll.sio_temp, st->max.sio_temp },
        { st->cur.gpu_load, st->avg.gpu_load, st->roll.gpu_load, st->max.gpu_load },
        { st->cur.gpu_temp, st->avg.gpu_temp, st->roll.gpu_temp, st->max.gpu_temp },
        { st->cur.gpu_mhz, st->avg.gpu_mhz, st->roll.gpu_mhz, st->max.gpu_mhz } };
    so_tabs(o, tabs, 5);
    fmt_hms(st->elapsed, a, sizeof a);
    fmt_hms(st->remaining > 0 ? st->remaining : 0, b, sizeof b);
    so_t(o, C_LABEL, "Status"); so_tab(o);
    so_b(o, st->state == BURN_FAIL ? C_RED : st->state == BURN_PASS ? C_GREEN : C_ORANGE, states[st->state >= 0 && st->state <= 5 ? st->state : 0]);
    so_tab(o); so_t(o, C_LABEL, "Elapsed"); so_tab(o); so_t(o, C_BLACK, a); so_tab(o); so_t(o, C_LABEL, "Remaining  "); so_t(o, C_BLACK, b); so_par(o);
    so_t(o, C_GRAY, st->status); so_par(o);
    if (st->gpu_msg[0]) { so_t(o, C_LABEL, "GPU Load  "); so_t(o, st->gpu_bad ? C_RED : C_GRAY, st->gpu_msg); so_par(o); }
    so_par(o);
    so_row(o, 5, (const char *const[]){ "", "Current", "Overall Average", "Rolling Average", "Maximum" }, (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < 7; i++) {
        char c[4][48];
        int k;
        for (k = 0; k < 4; k++) snprintf(c[k], 48, fmt[i], vs[i][k]);
        so_row(o, 5, (const char *const[]){ rows[i], c[0], c[1], c[2], c[3] }, (const int[]){ C_VALUE, C_BLACK, C_TEAL, C_TEAL, C_RED });
    }
    so_f(o, C_GRAY, "Hottest sensors: %s / %s", st->cur.cpu_temp_name, st->cur.sio_name);
    so_par(o); so_par(o);
    so_tabs(o, t2, 10);
    so_row(o, 11, (const char *const[]){ "Thread", "CPU", "On CPU", "User", "Kernel", "Clock", "Whetstone", "Average", "Min", "Max", "Affinity" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < st->nthreads; i++) {
        const burn_thread *t = &st->th[i];
        int cc = t->cur_cpu >= 0 && t->cur_cpu < BURN_MAXCPU ? t->cur_cpu : 0;
        char c[11][32];
        snprintf(c[0], 32, "%d", i);
        snprintf(c[1], 32, "CPU-%d", t->cpu);
        snprintf(c[2], 32, "CPU-%d", t->cur_cpu);
        snprintf(c[3], 32, "%.0f%%", st->per_cpu_user[cc]);
        snprintf(c[4], 32, "%.0f%%", st->per_cpu_sys[cc]);
        snprintf(c[5], 32, "%.0f MHz", st->per_cpu_mhz[cc]);
        snprintf(c[6], 32, "%.0f", t->mwips_last);
        snprintf(c[7], 32, "%.0f", t->mwips_avg);
        snprintf(c[8], 32, "%.0f", t->mwips_min);
        snprintf(c[9], 32, "%.0f", t->mwips_max);
        snprintf(c[10], 32, "%s", t->apic_errors ? "FAIL" : t->passes ? "ok" : "-");
        so_row(o, 11, (const char *const[]){ c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10] },
               (const int[]){ C_GRAY, C_LABEL, C_PURPLE, C_BLACK, C_GRAY, C_BLACK, C_BLACK, C_TEAL, C_GRAY, C_GRAY, t->apic_errors ? C_RED : C_GREEN });
    }
}


/* Save a finished run's summary to ~/.cache/srs/burnin-YYYYMMDD-HHMMSS.txt so runs can be compared. */
int burn_save_summary(const burn_status *st, char *path, size_t n)
{
    const char *h = getenv("HOME");
    char d[1024], name[64];
    time_t now = time(NULL);
    FILE *f;
    srs_out o;
    if (g_saved[0]) { snprintf(path, n, "%s", g_saved); return 0; }   /* once per run, however many windows */
    if (!h || !*h) return -1;
    snprintf(d, sizeof d, "%s/.cache", h);
    mkdir(d, 0755);
    snprintf(d, sizeof d, "%s/.cache/srs", h);
    mkdir(d, 0755);
    strftime(name, sizeof name, "burnin-%Y%m%d-%H%M%S.txt", localtime(&now));
    snprintf(path, n, "%s/%s", d, name);
    if (!(f = fopen(path, "w"))) return -1;
    so_text_init(&o, f);
    burn_render(&o, st);
    if (o.col) fputc('\n', f);
    if (fclose(f)) return -1;
    snprintf(g_saved, sizeof g_saved, "%s", path);
    {
        size_t k = strlen(g_status);
        snprintf(g_status + k, sizeof g_status - k, " - saved to ~/.cache/srs/%s", name);
    }
    return 0;
}
