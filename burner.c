/* burner.c - CPU Stress / CPU Burnin engine (see burner.h). */
#define _GNU_SOURCE
#include "burner.h"
#include "sec.h"

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#include <errno.h>
#include <fcntl.h>
#define SRS_X86 1
#endif
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static volatile int g_active;

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
static pid_t g_gpu_pid;
static double g_gpu_t0;
static char g_gpu_msg[300], g_gpu_log[600];
static int g_gpu_bad;

/* the GPU load command's own output goes to ~/.cache/srs/gpu-load.log so errors are not lost */
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

/* reap the GPU load command if it ended by itself and say why */
static void gpu_check(void)
{
    int ws;
    pid_t r;
    if (!g_gpu_pid) return;
    r = waitpid(g_gpu_pid, &ws, WNOHANG);
    if (r != g_gpu_pid) return;
    g_gpu_pid = 0;
    if (WIFEXITED(ws)) {
        int rc = WEXITSTATUS(ws);
        double secs = now_s() - g_gpu_t0;
        g_gpu_bad = rc != 0;
        if (rc == 127) snprintf(g_gpu_msg, sizeof g_gpu_msg, "GPU load command not found (exit 127): %s  - see %s", g_cfg.gpu_cmd, g_gpu_log);
        else if (rc == 126) snprintf(g_gpu_msg, sizeof g_gpu_msg, "GPU load command is not executable (exit 126): %s  - see %s", g_cfg.gpu_cmd, g_gpu_log);
        else if (rc) snprintf(g_gpu_msg, sizeof g_gpu_msg, "GPU load command failed (exit %d) after %.0f s  - see %s", rc, secs, g_gpu_log);
        else snprintf(g_gpu_msg, sizeof g_gpu_msg, "GPU load command finished (exit 0) after %.0f s; the CPU run continues", secs);
    } else if (WIFSIGNALED(ws)) {
        g_gpu_bad = 1;
        snprintf(g_gpu_msg, sizeof g_gpu_msg, "GPU load command was killed by signal %d  - see %s", WTERMSIG(ws), g_gpu_log);
    }
}

int burn_active(void) { return g_active; }

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
}

static void set_run(int v)
{
    int i;
    for (i = 0; i < g_nw; i++) g_w[i].run = v;
}

static void gpu_load(int on)
{
    if (on && g_cfg.gpu_cmd && *g_cfg.gpu_cmd && !g_gpu_pid) {
        pid_t p;
        gpu_log_path();
        p = fork();
        if (p == 0) {
            int fd = open(g_gpu_log, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
            setpgid(0, 0);
            if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
            execl("/bin/sh", "sh", "-c", g_cfg.gpu_cmd, (char *)NULL);
            _exit(127);
        }
        if (p > 0) {
            g_gpu_pid = p;
            g_gpu_t0 = now_s();
            g_gpu_bad = 0;
            snprintf(g_gpu_msg, sizeof g_gpu_msg, "GPU load command running (pid %d): %s", (int)p, g_cfg.gpu_cmd);
        } else {
            g_gpu_bad = 1;
            snprintf(g_gpu_msg, sizeof g_gpu_msg, "GPU load command could not be started: %s", strerror(errno));
        }
    } else if (!on && g_gpu_pid) {
        gpu_check();   /* it may already have ended by itself */
        if (g_gpu_pid) {
            kill(-g_gpu_pid, SIGTERM);
            waitpid(g_gpu_pid, NULL, 0);
            g_gpu_pid = 0;
            snprintf(g_gpu_msg, sizeof g_gpu_msg, "GPU load command stopped with the CPU load");
        }
    }
}

int burn_start(const burn_cfg *c)
{
    int i;
    if (g_active) return -1;
    g_cfg = *c;
    if (g_cfg.cycles < 1) g_cfg.cycles = 1;
    load_apic_map();
    g_ncpus = 0;
    for (i = 0; i < c->ncpu && i < BURN_MAXCPU; i++)
        if (c->enabled[i]) g_cpus[g_ncpus++] = i;
    if (!g_ncpus) return -1;
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
    g_cycle = 1;
    g_gpu_msg[0] = 0;
    g_gpu_bad = 0;
    g_t0 = g_phase_t0 = g_last_tick = now_s();
    g_state = BURN_RUNNING;
    g_active = 1;
    gpu_load(1);
    snprintf(g_status, sizeof g_status, "[CPU Burnin] will use %d worker thread%s", g_nw, g_nw == 1 ? "" : "s");
    return 0;
}

static void finish(int state, const char *why)
{
    int i;
    set_run(-1);
    gpu_load(0);
    for (i = 0; i < g_nw; i++) if (g_w[i].started) pthread_join(g_w[i].tid, NULL);
    g_state = state;
    g_active = 0;
    snprintf(g_status, sizeof g_status, "%s", why);
}

void burn_stop(void)
{
    if (g_active) finish(BURN_STOPPED, "Stopped by user");
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
    if (g_active) {
        g_nsamp++;
        g_sum.cpu_load += s.cpu_load; g_sum.cpu_mhz += s.cpu_mhz; g_sum.cpu_temp += s.cpu_temp;
        g_sum.sio_temp += s.sio_temp; g_sum.gpu_temp += s.gpu_temp; g_sum.gpu_load += s.gpu_load; g_sum.gpu_mhz += s.gpu_mhz;
        ROLL(g_roll.cpu_load, s.cpu_load); ROLL(g_roll.cpu_temp, s.cpu_temp); ROLL(g_roll.sio_temp, s.sio_temp);
        ROLL(g_roll.gpu_temp, s.gpu_temp); ROLL(g_roll.gpu_load, s.gpu_load); ROLL(g_roll.cpu_mhz, s.cpu_mhz);
        MAXV(g_max.cpu_load, s.cpu_load); MAXV(g_max.cpu_mhz, s.cpu_mhz); MAXV(g_max.cpu_temp, s.cpu_temp);
        MAXV(g_max.sio_temp, s.sio_temp); MAXV(g_max.gpu_temp, s.gpu_temp); MAXV(g_max.gpu_load, s.gpu_load);
        MAXV(g_max.gpu_mhz, s.gpu_mhz);
        for (i = 0; i < g_nw; i++) apic_bad += g_w[i].st.apic_errors;
        /* safety first */
        if (g_cfg.temp_limit_c > 0 && s.cpu_temp >= g_cfg.temp_limit_c) {
            char why[160];
            snprintf(why, sizeof why, "Fail - %s %.1f C reached the %.0f C limit", s.cpu_temp_name, s.cpu_temp, g_cfg.temp_limit_c);
            finish(BURN_FAIL, why);
        } else if (apic_bad) {
            finish(BURN_FAIL, "Fail - a worker thread ran on the wrong CPU (affinity / APIC check)");
        } else if (t - g_t0 >= g_cfg.cap_s) {
            finish(BURN_PASS, "Pass - time cap reached");
        } else if (g_state == BURN_RUNNING && g_cfg.burn_s > 0 && t - g_phase_t0 >= g_cfg.burn_s) {
            if (g_cycle >= g_cfg.cycles) finish(BURN_PASS, "Pass - Finished");
            else { g_state = BURN_COOLING; g_phase_t0 = t; set_run(0); gpu_load(0); snprintf(g_status, sizeof g_status, "Cooling"); }
        } else if (g_state == BURN_COOLING && t - g_phase_t0 >= g_cfg.cool_s) {
            g_state = BURN_RUNNING; g_phase_t0 = t; g_cycle++; set_run(1); gpu_load(1);
            snprintf(g_status, sizeof g_status, "Burning, cycle %d of %d", g_cycle, g_cfg.cycles);
        }
    }
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
