/* bench_os.c - OS latency benchmark: system calls, context switches, thread / process start,
 * wake-ups, timers, page faults and file metadata, each as a distribution of single samples
 * (see bench_os.h). Written for SRS.
 *
 * How it measures:
 *  - every sample is timed with clock_gettime(CLOCK_MONOTONIC) (the vDSO: no system call);
 *    operations far below a microsecond are timed in batches and divided, and the output says so
 *  - CPUs are taken from this thread's own affinity mask (taskset / cgroup cpusets are honoured);
 *    the mask is restored afterwards
 *  - read-only towards the system: no root, no real-time priority, no PR_SET_TIMERSLACK, nothing
 *    written to /proc or /sys. The only files are its own, in a temporary folder under
 *    ~/.cache/srs that is removed on every path (and folders left by a killed run are removed
 *    the next time)
 *  - fork() is safe from the GTK GUI: the children only read, write, set their own affinity and
 *    _exit(); every child is waited for, also after Stop or an error */
#define _GNU_SOURCE
#include "bench.h"
#include "bench_os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *const grp_name[OS_NGROUP] = { "System calls", "Context switches", "Starting threads and processes",
                                                 "Wake-up latency", "Timers", "Memory mapping and page faults", "File metadata" };
static const char *const grp_id[OS_NGROUP] = { "syscall", "switch", "spawn", "wake", "timer", "memory", "files" };
const char *os_group_name(int g) { return g >= 0 && g < OS_NGROUP ? grp_name[g] : ""; }
const char *os_group_id(int g) { return g >= 0 && g < OS_NGROUP ? grp_id[g] : ""; }

void os_cfg_default(os_cfg *c)
{
    memset(c, 0, sizeof *c);
    c->samples = 20000;
    c->jitter_secs = 3.0;
}

void os_fmt_ns(char *b, unsigned long n, double ns)
{
    if (ns <= 0) snprintf(b, n, ns < 0 ? "-" : "0 ns");
    else if (ns < 10) snprintf(b, n, "%.1f ns", ns);
    else if (ns < 1000) snprintf(b, n, "%.0f ns", ns);
    else if (ns < 10000) snprintf(b, n, "%.2f µs", ns / 1e3);
    else if (ns < 100000) snprintf(b, n, "%.1f µs", ns / 1e3);
    else if (ns < 1e6) snprintf(b, n, "%.0f µs", ns / 1e3);
    else snprintf(b, n, "%.2f ms", ns / 1e6);
}

#ifdef __linux__
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SYS_futex
#define SYS_futex SYS_futex_time64
#endif
#ifndef MADV_NOHUGEPAGE
#define MADV_NOHUGEPAGE 15
#endif
#ifndef MADV_HUGEPAGE
#define MADV_HUGEPAGE 14
#endif

extern char **environ;

#define SC_BATCH 32           /* system calls per sample */
#define PF_BATCH 16           /* 4 KiB first-touch faults per sample */
#define PF_REGION (32u << 20) /* 4 KiB fault region, mapped fresh when used up */
#define HP_SIZE (2u << 20)
#define HP_PER_REGION 32

static const char *volatile g_os_stage = "";
static char g_stage_buf[128];
const char *os_stage(void) { return g_os_stage; }
int os_available(void) { return 1; }

static void set_stage(const char *g, const char *what)
{
    snprintf(g_stage_buf, sizeof g_stage_buf, "%s: %s", g, what);
    g_os_stage = g_stage_buf;
}

static inline int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static inline void cpu_relax(void)
{
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    __asm__ volatile("yield");
#endif
}

static void pin_self(int cpu)
{
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(cpu, &s);
    pthread_setaffinity_np(pthread_self(), sizeof s, &s);
}

static const char *cache_dir(void)
{
    static char d[1024];
    const char *h = getenv("HOME");
    snprintf(d, sizeof d, "%s/.cache", h ? h : "/tmp");
    mkdir(d, 0755);
    snprintf(d, sizeof d, "%s/.cache/srs", h ? h : "/tmp");
    mkdir(d, 0755);
    return d;
}

static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

/* "0-3,8,10-11" -> set */
static void parse_cpulist(const char *s, cpu_set_t *set)
{
    CPU_ZERO(set);
    while (s && *s) {
        char *e;
        long a = strtol(s, &e, 10), z = a, i;
        if (e == s) break;
        if (*e == '-') z = strtol(e + 1, &e, 10);
        for (i = a; i <= z && i < CPU_SETSIZE; i++) if (i >= 0) CPU_SET((int)i, set);
        s = *e == ',' ? e + 1 : NULL;
    }
}

/* ======================================================================
 * the run's state and the statistics
 * ====================================================================== */
typedef struct {
    const os_cfg *c;
    os_result *r;
    volatile int *cancel;
    double *s;                 /* sample buffer (ns) */
    long cap;
    char key[OS_MAXROWS][16];  /* short ids for the history line */
    cpu_set_t saved;           /* the caller's affinity, restored after every pinned test */
} octx;

static int stopped(octx *x) { if (x->cancel && *x->cancel) x->r->cancelled = 1; return x->r->cancelled; }

static long scaled(octx *x, long div, long min)
{
    long n = x->c->samples / div;
    if (n < min) n = min;
    if (n > x->cap) n = x->cap;
    return n;
}

static void unpin(octx *x) { pthread_setaffinity_np(pthread_self(), sizeof x->saved, &x->saved); }

static os_row *new_row(octx *x, int g, const char *key, const char *name, const char *detail)
{
    os_row *w;
    if (x->r->nrows >= OS_MAXROWS) return NULL;
    w = &x->r->row[x->r->nrows];
    memset(w, 0, sizeof *w);
    w->group = g;
    snprintf(w->name, sizeof w->name, "%s", name);
    snprintf(w->detail, sizeof w->detail, "%s", detail ? detail : "");
    snprintf(x->key[x->r->nrows], sizeof x->key[0], "%s", key);
    return w;
}

static void done_row(octx *x, os_row *w)
{
    if (!w) return;
    if (x->c->cb) x->c->cb(w, x->r->nrows, x->c->ud);
    x->r->nrows++;
}

static void skip_row(octx *x, int g, const char *key, const char *name, const char *why)
{
    os_row *w = new_row(x, g, key, name, why);
    if (!w) return;
    w->skipped = 1;
    w->med = w->p95 = w->p99 = w->worst = w->best = w->mean = -1;
    done_row(x, w);
}

/* sort the n samples in x->s and finish the row; n == 0 (stopped before the first sample) = skipped */
static void stat_row(octx *x, int g, const char *key, const char *name, const char *detail, long n)
{
    os_row *w;
    double sum = 0;
    long i;
    if (n <= 0) { if (!stopped(x)) skip_row(x, g, key, name, "no samples (the operation failed)"); return; }
    if (!(w = new_row(x, g, key, name, detail))) return;
    qsort(x->s, (size_t)n, sizeof *x->s, cmp_d);
    for (i = 0; i < n; i++) sum += x->s[i];
    w->n = n;
    w->best = x->s[0];
    w->med = x->s[n / 2];
    w->p95 = x->s[(long)((double)n * 0.95) < n ? (long)((double)n * 0.95) : n - 1];
    w->p99 = x->s[(long)((double)n * 0.99) < n ? (long)((double)n * 0.99) : n - 1];
    w->worst = x->s[n - 1];
    w->mean = sum / (double)n;
    done_row(x, w);
}

/* spin until *v >= want; yields after a short spin so a partner on the same CPU can run.
   0 = timed out, quit or stopped */
static int wait_ge(_Atomic int *v, int want, octx *x, int64_t timeout_ns)
{
    int64_t t0 = 0;
    unsigned i;
    for (i = 0;; i++) {
        if (atomic_load_explicit(v, memory_order_acquire) >= want) return 1;
        if ((i & 255) == 255) {
            int64_t t = now_ns();
            if (!t0) t0 = t;
            else if (t - t0 > timeout_ns) return 0;
            if (x && x->cancel && *x->cancel) return 0;
        }
        if (i > 2000) sched_yield(); else cpu_relax();
    }
}

static void sleep_ns(long ns)
{
    struct timespec d = { ns / 1000000000L, ns % 1000000000L };
    while (clock_nanosleep(CLOCK_MONOTONIC, 0, &d, &d) == EINTR) ;
}

/* ======================================================================
 * the system around the numbers: scheduler, preemption, tick, clock source, THP
 * ====================================================================== */
static void boot_config(const char *key, char *out, size_t n)
{
    struct utsname u;
    char path[400], l[256];
    size_t kl = strlen(key);
    FILE *f;
    out[0] = 0;
    if (uname(&u)) return;
    snprintf(path, sizeof path, "/boot/config-%s", u.release);
    if (!(f = fopen(path, "r"))) {   /* Arch and others: only the kernel headers package has it */
        snprintf(path, sizeof path, "/lib/modules/%s/build/.config", u.release);
        if (!(f = fopen(path, "r"))) return;
    }
    while (fgets(l, sizeof l, f))
        if (!strncmp(l, key, kl) && l[kl] == '=') { snprintf(out, n, "%s", l + kl + 1); out[strcspn(out, "\n")] = 0; break; }
    fclose(f);
}

int os_sched_info(char *name, size_t nn, char *detail, size_t dn)
{
    char st[64] = "", ops[128] = "", sw[16] = "", bore[16] = "", fair[64], cfgb[16];
    struct utsname u;
    int maj = 0, min = 0, known = 1;
    if (!uname(&u)) sscanf(u.release, "%d.%d", &maj, &min);
    boot_config("CONFIG_SCHED_BORE", cfgb, sizeof cfgb);
    if (rd_str("/proc/sys/kernel/sched_bore", bore, sizeof bore))
        snprintf(fair, sizeof fair, bore[0] == '0' ? "EEVDF (BORE built in, switched off)" : "BORE (EEVDF + burst score)");
    else if (cfgb[0] == 'y') snprintf(fair, sizeof fair, "BORE (EEVDF + burst score)");
    else if (maj > 6 || (maj == 6 && min >= 6)) snprintf(fair, sizeof fair, "EEVDF");
    else if (maj) snprintf(fair, sizeof fair, "CFS");
    else { snprintf(fair, sizeof fair, "unknown"); known = 0; }
    if (rd_str("/sys/kernel/sched_ext/state", st, sizeof st) && !strcmp(st, "enabled") &&
        rd_str("/sys/kernel/sched_ext/root/ops", ops, sizeof ops) && ops[0]) {
        char base[64];
        int all;
        snprintf(base, sizeof base, "%.63s", ops);
        base[strcspn(base, "_")] = 0;   /* "lavd_1.1.2_x86_64_..." -> "lavd" */
        all = !rd_str("/sys/kernel/sched_ext/switch_all", sw, sizeof sw) || sw[0] != '0';
        snprintf(name, nn, "scx_%s (sched_ext, %s)", base, all ? "all normal tasks" : "only tasks that asked for it");
        snprintf(detail, dn, "ops \"%s\" (/sys/kernel/sched_ext/root/ops); %s otherwise: %s", ops,
                 all ? "the built-in scheduler" : "everything else uses", fair);
        return 1;
    }
    snprintf(name, nn, "%s", fair);
    if (bore[0]) snprintf(detail, dn, "kernel.sched_bore = %s; no sched_ext scheduler loaded", bore);
    else snprintf(detail, dn, "from the kernel version %d.%d%s; no sched_ext scheduler loaded", maj, min, cfgb[0] ? " and its config" : "");
    return known;
}

static void sys_info(os_result *r)
{
    struct utsname u;
    char *cmd = NULL, *p;
    os_sched_info(r->sched, sizeof r->sched, r->sched_detail, sizeof r->sched_detail);
    r->preempt[0] = 0;
    if (!uname(&u))
        snprintf(r->preempt, sizeof r->preempt, "%s", strstr(u.version, "PREEMPT_RT") ? "PREEMPT_RT (real-time)" :
                 strstr(u.version, "PREEMPT_DYNAMIC") ? "PREEMPT_DYNAMIC" : strstr(u.version, "PREEMPT") ? "PREEMPT (full)" : "voluntary / none");
    if (rd_all("/proc/cmdline", &cmd) > 0 && (p = strstr(cmd, "preempt="))) {   /* the mode PREEMPT_DYNAMIC was booted with */
        char m[24];
        if (sscanf(p + 8, "%23s", m) == 1) {
            size_t l = strlen(r->preempt);
            snprintf(r->preempt + l, sizeof r->preempt - l, ", preempt=%s", m);
        }
    }
    free(cmd);
    boot_config("CONFIG_HZ", r->hz, sizeof r->hz);
    rd_str("/sys/devices/system/clocksource/clocksource0/current_clocksource", r->clocksource, sizeof r->clocksource);
    if (!rd_str("/sys/kernel/mm/transparent_hugepage/enabled", r->thp, sizeof r->thp)) snprintf(r->thp, sizeof r->thp, "not available");
    r->timer_slack_ns = prctl(PR_GET_TIMERSLACK, 0, 0, 0, 0);
}

/* CPU a = first allowed CPU; b = first allowed CPU on another core, preferring one in a's L3 */
static void pick_cpus(octx *x)
{
    cpu_set_t sib, l3;
    char b[256];
    int a = -1, i, bb = -1, bl3 = -1, any = -1;
    for (i = 0; i < CPU_SETSIZE; i++) if (CPU_ISSET(i, &x->saved)) { a = i; break; }
    x->r->cpu_a = a < 0 ? 0 : a;
    x->r->cpu_b = -1;
    if (a < 0) return;
    CPU_ZERO(&sib);
    CPU_SET(a, &sib);
    if (rd_fmt(b, sizeof b, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", a)) parse_cpulist(b, &sib);
    CPU_ZERO(&l3);
    for (i = 0; i < 8; i++) {
        char lv[8];
        if (!rd_fmt(lv, sizeof lv, "/sys/devices/system/cpu/cpu%d/cache/index%d/level", a, i)) break;
        if (!strcmp(lv, "3") && rd_fmt(b, sizeof b, "/sys/devices/system/cpu/cpu%d/cache/index%d/shared_cpu_list", a, i)) parse_cpulist(b, &l3);
    }
    for (i = 0; i < CPU_SETSIZE; i++) {
        if (i == a || !CPU_ISSET(i, &x->saved)) continue;
        if (any < 0) any = i;
        if (CPU_ISSET(i, &sib)) continue;
        if (bb < 0) bb = i;
        if (bl3 < 0 && CPU_ISSET(i, &l3)) bl3 = i;
    }
    x->r->cpu_b = bl3 >= 0 ? bl3 : bb >= 0 ? bb : any;   /* any = only an SMT sibling is allowed */
    x->r->b_same_l3 = bl3 >= 0 || (x->r->cpu_b >= 0 && CPU_ISSET(x->r->cpu_b, &l3));
    x->r->b_smt = x->r->cpu_b >= 0 && CPU_ISSET(x->r->cpu_b, &sib);
}

/* ======================================================================
 * 1. system calls
 * ====================================================================== */
static volatile long g_sink;

static void sc_loop(int kind, int fd)
{
    struct timespec ts[2];   /* two: the raw system call on 32-bit x86 writes the old 8-byte layout */
    char c;
    int i;
    switch (kind) {
    case 0: for (i = 0; i < SC_BATCH; i++) g_sink = syscall(SYS_getpid); break;
    case 1: for (i = 0; i < SC_BATCH; i++) clock_gettime(CLOCK_MONOTONIC, ts); break;
    case 2: for (i = 0; i < SC_BATCH; i++) g_sink = syscall(SYS_clock_gettime, CLOCK_MONOTONIC, ts); break;
    case 3: for (i = 0; i < SC_BATCH; i++) if (read(fd, &c, 1) != 1) break; break;
    default: for (i = 0; i < SC_BATCH; i++) sched_yield(); break;
    }
}

static void t_syscalls(octx *x)
{
    static const char *const nm[5] = { "getpid() via syscall()", "clock_gettime through the vDSO", "clock_gettime as a system call",
                                       "read() 1 byte from /dev/zero", "sched_yield() (nothing else to run)" };
    static const char *const key[5] = { "getpid", "vdso", "clockgettime", "read", "yield" };
    long n = scaled(x, 1, 50), i;
    int fd = open("/dev/zero", O_RDONLY | O_CLOEXEC), k;
    char det[96];
    pin_self(x->r->cpu_a);
    snprintf(det, sizeof det, "CPU %d; batch of %d calls per sample", x->r->cpu_a, SC_BATCH);
    for (k = 0; k < 5 && !stopped(x); k++) {
        set_stage(grp_name[OS_G_SYSCALL], nm[k]);
        if (k == 3 && fd < 0) { skip_row(x, OS_G_SYSCALL, key[k], nm[k], "cannot open /dev/zero"); continue; }
        for (i = 0; i < n / 10 + 10; i++) sc_loop(k, fd);   /* warm-up */
        for (i = 0; i < n && !stopped(x); i++) {
            int64_t t0 = now_ns();
            sc_loop(k, fd);
            x->s[i] = (double)(now_ns() - t0) / SC_BATCH;
        }
        if (!stopped(x)) stat_row(x, OS_G_SYSCALL, key[k], nm[k], det, i);
    }
    if (fd >= 0) close(fd);
    unpin(x);
}

/* ======================================================================
 * 2. context switches: 1-byte pipe ping-pong; one sample = half a round trip
 * ====================================================================== */
typedef struct { int rfd, wfd, cpu; } echo_arg;

static void echo_loop(int rfd, int wfd)
{
    char c;
    for (;;) {
        if (read(rfd, &c, 1) != 1 || c == 'q') break;   /* 'q' or the other side closed: done */
        if (write(wfd, &c, 1) != 1) break;
    }
}

static void *echo_thread(void *p)
{
    echo_arg *a = p;
    if (a->cpu >= 0) pin_self(a->cpu);
    echo_loop(a->rfd, a->wfd);
    return NULL;
}

/* procs: echo side is a forked child; cpu_echo < 0 = same CPU as the caller */
static void pingpong(octx *x, int procs, int cpu_echo, const char *key, const char *name, const char *detail)
{
    int to[2], from[2], ok = 1, made = 0;
    long n = scaled(x, 1, 50), i, got = 0;
    pthread_t t;
    pid_t pid = -1;
    echo_arg a;
    char c = 'x';
    if (pipe2(to, O_CLOEXEC)) { skip_row(x, OS_G_SWITCH, key, name, "pipe() failed"); return; }
    if (pipe2(from, O_CLOEXEC)) { close(to[0]); close(to[1]); skip_row(x, OS_G_SWITCH, key, name, "pipe() failed"); return; }
    pin_self(x->r->cpu_a);
    a.rfd = to[0]; a.wfd = from[1]; a.cpu = cpu_echo;
    if (procs) {
        pid = fork();
        if (pid == 0) {   /* child: async-signal-safe calls only */
            prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);
            if (cpu_echo >= 0) { cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu_echo, &s); sched_setaffinity(0, sizeof s, &s); }
            close(to[1]); close(from[0]);
            echo_loop(to[0], from[1]);
            _exit(0);
        }
        ok = made = pid > 0;
    } else ok = made = pthread_create(&t, NULL, echo_thread, &a) == 0;
    if (ok) {
        for (i = 0; i < 500 && ok; i++)   /* warm-up: thread start, pinning, clocks */
            ok = write(to[1], &c, 1) == 1 && read(from[0], &c, 1) == 1;
        for (i = 0; i < n && ok && !stopped(x); i++) {
            int64_t t0 = now_ns();
            ok = write(to[1], &c, 1) == 1 && read(from[0], &c, 1) == 1;
            if (ok) x->s[got++] = (double)(now_ns() - t0) / 2;
        }
    }
    c = 'q';
    if (made) g_sink = write(to[1], &c, 1);   /* the echo side ends on 'q', or on end of file below */
    close(to[1]);
    if (made && procs) while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) ;
    if (made && !procs) pthread_join(t, NULL);
    close(to[0]); close(from[0]); close(from[1]);
    unpin(x);
    if (stopped(x)) return;
    if (!ok && !got) { skip_row(x, OS_G_SWITCH, key, name, procs ? "fork() failed" : "pthread_create() failed"); return; }
    stat_row(x, OS_G_SWITCH, key, name, detail, got);
}

static void t_switch(octx *x)
{
    sigset_t sp, old;
    char d1[96], d2[96];
    struct timespec zero = { 0, 0 };
    /* a dead partner must give EPIPE, not kill the GUI with SIGPIPE */
    sigemptyset(&sp);
    sigaddset(&sp, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &sp, &old);
    snprintf(d1, sizeof d1, "both on CPU %d; half a round trip", x->r->cpu_a);
    if (x->r->cpu_b >= 0)
        snprintf(d2, sizeof d2, "CPU %d <-> CPU %d (%s); half a round trip", x->r->cpu_a, x->r->cpu_b,
                 x->r->b_smt ? "same core, SMT" : x->r->b_same_l3 ? "same L3" : "other L3");
    set_stage(grp_name[OS_G_SWITCH], "two threads, same CPU");
    pingpong(x, 0, x->r->cpu_a, "sw-thr-same", "Pipe ping-pong, 2 threads, same CPU", d1);
    if (!stopped(x)) {
        set_stage(grp_name[OS_G_SWITCH], "two threads, different cores");
        if (x->r->cpu_b >= 0) pingpong(x, 0, x->r->cpu_b, "sw-thr-diff", "Pipe ping-pong, 2 threads, 2 cores", d2);
        else skip_row(x, OS_G_SWITCH, "sw-thr-diff", "Pipe ping-pong, 2 threads, 2 cores", "only one CPU is allowed for this program");
    }
    if (!stopped(x)) {
        set_stage(grp_name[OS_G_SWITCH], "two processes, same CPU");
        pingpong(x, 1, x->r->cpu_a, "sw-proc-same", "Pipe ping-pong, 2 processes, same CPU", d1);
    }
    if (!stopped(x)) {
        set_stage(grp_name[OS_G_SWITCH], "two processes, different cores");
        if (x->r->cpu_b >= 0) pingpong(x, 1, x->r->cpu_b, "sw-proc-diff", "Pipe ping-pong, 2 processes, 2 cores", d2);
        else skip_row(x, OS_G_SWITCH, "sw-proc-diff", "Pipe ping-pong, 2 processes, 2 cores", "only one CPU is allowed for this program");
    }
    while (sigtimedwait(&sp, NULL, &zero) > 0) ;   /* drop a SIGPIPE that may be pending */
    pthread_sigmask(SIG_SETMASK, &old, NULL);
}

/* ======================================================================
 * 3. thread create + join, fork + wait, posix_spawn + wait (not pinned: the scheduler places them)
 * ====================================================================== */
static void *empty_thread(void *p) { return p; }

static long vm_rss_kib(void)
{
    char *s = NULL, *p;
    long v = 0;
    if (rd_all("/proc/self/status", &s) > 0 && (p = strstr(s, "VmRSS:"))) v = atol(p + 6);
    free(s);
    return v;
}

static void t_spawn(octx *x)
{
    long n = scaled(x, 10, 20), i, got;
    const char *truep = access("/bin/true", X_OK) == 0 ? "/bin/true" : access("/usr/bin/true", X_OK) == 0 ? "/usr/bin/true" : NULL;
    char det[96];
    unpin(x);
    set_stage(grp_name[OS_G_SPAWN], "pthread_create + pthread_join");
    for (i = 0, got = 0; i < n + 10 && !stopped(x); i++) {
        pthread_t t;
        int64_t t0 = now_ns();
        if (pthread_create(&t, NULL, empty_thread, NULL)) break;
        pthread_join(t, NULL);
        if (i >= 10) x->s[got++] = (double)(now_ns() - t0);   /* first 10 = warm-up */
    }
    if (stopped(x)) return;
    stat_row(x, OS_G_SPAWN, "thread", "Thread create + join (empty thread)", "scheduler's choice of CPU", got);

    set_stage(grp_name[OS_G_SPAWN], "fork + waitpid");
    x->r->rss_kib = vm_rss_kib();
    for (i = 0, got = 0; i < n + 5 && !stopped(x); i++) {
        int64_t t0 = now_ns();
        pid_t p = fork();
        if (p == 0) _exit(0);
        if (p < 0) break;
        while (waitpid(p, NULL, 0) < 0 && errno == EINTR) ;
        if (i >= 5) x->s[got++] = (double)(now_ns() - t0);
    }
    if (stopped(x)) return;
    snprintf(det, sizeof det, "child exits at once; this program uses %.0f MB RAM", x->r->rss_kib / 1024.0);
    stat_row(x, OS_G_SPAWN, "fork", "fork() + waitpid()", det, got);

    set_stage(grp_name[OS_G_SPAWN], "posix_spawn /bin/true + waitpid");
    if (!truep) { skip_row(x, OS_G_SPAWN, "spawn", "posix_spawn(true) + waitpid()", "no /bin/true or /usr/bin/true"); return; }
    for (i = 0, got = 0; i < n + 5 && !stopped(x); i++) {
        char *argv[2] = { (char *)"true", NULL };
        pid_t p;
        int64_t t0 = now_ns();
        if (posix_spawn(&p, truep, NULL, NULL, argv, environ)) break;
        while (waitpid(p, NULL, 0) < 0 && errno == EINTR) ;
        if (i >= 5) x->s[got++] = (double)(now_ns() - t0);
    }
    if (stopped(x)) return;
    snprintf(det, sizeof det, "%s: start, load, run, exit", truep);
    stat_row(x, OS_G_SPAWN, "spawn", "posix_spawn(true) + waitpid()", det, got);
}

/* ======================================================================
 * 4. wake-up latency: from the wake call until the sleeping thread runs again
 * ====================================================================== */
typedef struct {
    int word;                  /* futex word / condition flag */
    _Atomic int go, ready, got, quit;
    int64_t t_wake;
    int eagain, use_cond, cpu;
    pthread_mutex_t mu;
    pthread_cond_t cv;
} wk;

static long futex_(int *u, int op, int v) { return syscall(SYS_futex, u, op, v, NULL, NULL, 0); }

static void *wk_main(void *p)
{
    wk *w = p;
    int k;
    if (w->cpu >= 0) pin_self(w->cpu);
    for (k = 1;; k++) {
        if (!wait_ge(&w->go, k, NULL, INT64_MAX) || atomic_load(&w->quit)) return NULL;
        if (atomic_load(&w->quit)) return NULL;
        if (w->use_cond) {
            pthread_mutex_lock(&w->mu);
            atomic_store(&w->ready, k);
            while (!w->word) pthread_cond_wait(&w->cv, &w->mu);
            w->t_wake = now_ns();
            pthread_mutex_unlock(&w->mu);
        } else {
            int first = 1;
            w->eagain = 0;
            atomic_store(&w->ready, k);
            while (!__atomic_load_n(&w->word, __ATOMIC_ACQUIRE)) {
                if (futex_(&w->word, FUTEX_WAIT_PRIVATE, 0) == -1 && errno == EAGAIN && first) w->eagain = 1;
                first = 0;
            }
            w->t_wake = now_ns();
        }
        atomic_store(&w->got, k);
    }
}

/* one wake-up test: cpu_main / cpu_wait < 0 = not pinned */
static void t_wake1(octx *x, int use_cond, int cpu_main, int cpu_wait, const char *key, const char *name, const char *detail)
{
    wk *w = calloc(1, sizeof *w);
    pthread_t t;
    long n = scaled(x, 10, 20), i, got = 0, discard = 0;
    int ok = 1;
    char det[96];
    if (!w) return;
    w->use_cond = use_cond;
    w->cpu = cpu_wait;
    pthread_mutex_init(&w->mu, NULL);
    pthread_cond_init(&w->cv, NULL);
    if (cpu_main >= 0) pin_self(cpu_main); else unpin(x);
    if (pthread_create(&t, NULL, wk_main, w)) { free(w); unpin(x); skip_row(x, OS_G_WAKE, key, name, "pthread_create() failed"); return; }
    for (i = 1; i <= n + 20 && ok && !stopped(x); i++) {
        int64_t t0;
        if (use_cond) { pthread_mutex_lock(&w->mu); w->word = 0; pthread_mutex_unlock(&w->mu); }
        else __atomic_store_n(&w->word, 0, __ATOMIC_RELEASE);
        atomic_store(&w->go, (int)i);
        if (!(ok = wait_ge(&w->ready, (int)i, x, 1000000000LL))) break;
        sleep_ns(30000);   /* let the waiter get into the kernel and fall asleep */
        if (use_cond) {
            pthread_mutex_lock(&w->mu);   /* only possible once the waiter sits in pthread_cond_wait */
            t0 = now_ns();
            w->word = 1;
            pthread_cond_signal(&w->cv);
            pthread_mutex_unlock(&w->mu);
        } else {
            t0 = now_ns();
            __atomic_store_n(&w->word, 1, __ATOMIC_RELEASE);
            futex_(&w->word, FUTEX_WAKE_PRIVATE, 1);
        }
        if (!(ok = wait_ge(&w->got, (int)i, x, 1000000000LL))) break;
        if (i <= 20) continue;   /* warm-up */
        if (w->eagain) discard++;
        else x->s[got++] = (double)(w->t_wake - t0);
    }
    /* wherever the waiter is, let it go and end */
    atomic_store(&w->quit, 1);
    pthread_mutex_lock(&w->mu);
    __atomic_store_n(&w->word, 1, __ATOMIC_RELEASE);
    pthread_cond_broadcast(&w->cv);
    pthread_mutex_unlock(&w->mu);
    futex_(&w->word, FUTEX_WAKE_PRIVATE, 1);
    atomic_store(&w->go, INT32_MAX);
    pthread_join(t, NULL);
    pthread_mutex_destroy(&w->mu);
    pthread_cond_destroy(&w->cv);
    free(w);
    unpin(x);
    if (stopped(x)) return;
    if (discard) snprintf(det, sizeof det, "%s; %ld not asleep yet, left out", detail, discard);
    else snprintf(det, sizeof det, "%s", detail);
    if (!ok && !got) { skip_row(x, OS_G_WAKE, key, name, "the waiting thread did not answer within 1 s"); return; }
    stat_row(x, OS_G_WAKE, key, name, det, got);
}

static void t_wake(octx *x)
{
    char d[96];
    if (x->r->cpu_b >= 0) snprintf(d, sizeof d, "waker CPU %d, sleeper CPU %d", x->r->cpu_a, x->r->cpu_b);
    set_stage(grp_name[OS_G_WAKE], "futex wake, different cores");
    if (x->r->cpu_b >= 0) t_wake1(x, 0, x->r->cpu_a, x->r->cpu_b, "futex-diff", "Futex wake, 2 cores", d);
    else skip_row(x, OS_G_WAKE, "futex-diff", "Futex wake, 2 cores", "only one CPU is allowed for this program");
    if (stopped(x)) return;
    set_stage(grp_name[OS_G_WAKE], "futex wake, scheduler's choice of CPU");
    t_wake1(x, 0, -1, -1, "futex-any", "Futex wake, not pinned", "the scheduler places both threads");
    if (stopped(x)) return;
    set_stage(grp_name[OS_G_WAKE], "condition variable, different cores");
    if (x->r->cpu_b >= 0) t_wake1(x, 1, x->r->cpu_a, x->r->cpu_b, "cond-diff", "Condition variable signal, 2 cores", d);
    else skip_row(x, OS_G_WAKE, "cond-diff", "Condition variable signal, 2 cores", "only one CPU is allowed for this program");
}

/* ======================================================================
 * 5. timers: sleep overshoot and a periodic 1 ms tick
 * ====================================================================== */
static void t_timer(octx *x)
{
    static const long req[2] = { 50000, 1000000 };
    static const char *const nm[2] = { "clock_nanosleep 50 µs: overshoot", "clock_nanosleep 1 ms: overshoot" };
    static const char *const key[2] = { "sleep50us", "sleep1ms" };
    long i, got;
    int k;
    char det[96];
    unpin(x);
    snprintf(det, sizeof det, "time past the requested sleep; timer slack %ld µs", x->r->timer_slack_ns / 1000);
    for (k = 0; k < 2 && !stopped(x); k++) {
        long n = scaled(x, k ? 40 : 10, k ? 10 : 20);
        set_stage(grp_name[OS_G_TIMER], nm[k]);
        for (i = 0, got = 0; i < n + 3 && !stopped(x); i++) {
            struct timespec d = { 0, req[k] };
            int64_t t0 = now_ns();
            clock_nanosleep(CLOCK_MONOTONIC, 0, &d, NULL);
            if (i >= 3) x->s[got++] = (double)(now_ns() - t0 - req[k]);
        }
        if (!stopped(x)) stat_row(x, OS_G_TIMER, key[k], nm[k], det, got);
    }
    if (stopped(x)) return;
    {   /* periodic tick: absolute deadlines every 1 ms, so lateness does not add up */
        long n = (long)(x->c->jitter_secs * 1000), missed = 0;
        int64_t next = now_ns() + 1000000;
        char st[96];
        if (n > x->cap) n = x->cap;
        if (n < 10) n = 10;
        for (i = 0, got = 0; i < n && !stopped(x); i++) {
            struct timespec d = { (time_t)(next / 1000000000LL), (long)(next % 1000000000LL) };
            int64_t t;
            if ((i & 255) == 0) {
                snprintf(st, sizeof st, "periodic 1 ms tick, %.0f of %.0f s", i / 1000.0, n / 1000.0);
                set_stage(grp_name[OS_G_TIMER], st);
            }
            while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &d, NULL) == EINTR) ;
            t = now_ns();
            x->s[got++] = (double)(t - next);
            next += 1000000;
            if (t > next) { missed += (t - next) / 1000000 + 1; next = t + 1000000; }   /* fell behind: start again from now */
        }
        if (stopped(x)) return;
        snprintf(det, sizeof det, "%ld ticks in %.1f s; %ld missed (over 1 ms late)", got, got / 1000.0, missed);
        stat_row(x, OS_G_TIMER, "tick1ms", "Periodic 1 ms tick: lateness", det, got);
    }
}

/* ======================================================================
 * 6. memory: first-touch page faults, mmap / munmap
 * ====================================================================== */
/* AnonHugePages (kB) of the mapping that starts at addr, from /proc/self/smaps; -1 = not found.
   A mapping's header line starts with its hex address; its field lines ("AnonHugePages:") with a capital. */
static long vma_huge_kb(void *addr)
{
    FILE *f = fopen("/proc/self/smaps", "r");
    char l[512], want[32];
    int in = 0;
    long kb = -1;
    size_t wl;
    if (!f) return -1;
    snprintf(want, sizeof want, "%lx-", (unsigned long)(uintptr_t)addr);
    wl = strlen(want);
    while (fgets(l, sizeof l, f)) {
        int header = (l[0] >= '0' && l[0] <= '9') || (l[0] >= 'a' && l[0] <= 'f');
        if (header) {
            if (in) break;
            in = !strncmp(l, want, wl);
        } else if (in && !strncmp(l, "AnonHugePages:", 14)) { kb = atol(l + 14); break; }
    }
    fclose(f);
    return kb;
}

static void t_memory(octx *x)
{
    long n = scaled(x, 1, 50), i, got = 0;
    char det[96];
    pin_self(x->r->cpu_a);
    /* 4 KiB pages: THP switched off for the region, PF_BATCH new pages per sample */
    set_stage(grp_name[OS_G_MEMORY], "first touch of 4 KiB pages");
    while (got < n && !stopped(x)) {
        unsigned char *p = mmap(NULL, PF_REGION, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        size_t off;
        if (p == MAP_FAILED) break;
        madvise(p, PF_REGION, MADV_NOHUGEPAGE);
        for (off = 0; off + PF_BATCH * 4096u <= PF_REGION && got < n && !stopped(x); off += PF_BATCH * 4096u) {
            int64_t t0 = now_ns();
            int j;
            for (j = 0; j < PF_BATCH; j++) ((volatile unsigned char *)p)[off + (size_t)j * 4096u] = 1;
            x->s[got++] = (double)(now_ns() - t0) / PF_BATCH;
        }
        munmap(p, PF_REGION);
    }
    if (stopped(x)) { unpin(x); return; }
    snprintf(det, sizeof det, "CPU %d; first write, zeroed page; batch of %d", x->r->cpu_a, PF_BATCH);
    stat_row(x, OS_G_MEMORY, "fault4k", "Page fault, 4 KiB page", det, got);

    /* transparent huge pages: 2 MiB-aligned region, madvise(MADV_HUGEPAGE), one fault per 2 MiB */
    set_stage(grp_name[OS_G_MEMORY], "first touch of 2 MiB transparent huge pages");
    if (strstr(x->r->thp, "[never]") || !strchr(x->r->thp, '[')) {
        skip_row(x, OS_G_MEMORY, "fault2m", "Page fault, 2 MiB huge page", "transparent huge pages are switched off");
    } else {
        long nh = scaled(x, 40, 8), faults = 0, huge_kb = 0;
        got = 0;
        while (got < nh && !stopped(x)) {
            size_t len = (size_t)HP_SIZE * (HP_PER_REGION + 1);
            unsigned char *raw = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0), *p;
            int j;
            long kb;
            if (raw == MAP_FAILED) break;
            p = (unsigned char *)(((uintptr_t)raw + HP_SIZE - 1) & ~(uintptr_t)(HP_SIZE - 1));
            madvise(p, (size_t)HP_SIZE * HP_PER_REGION, MADV_HUGEPAGE);
            for (j = 0; j < HP_PER_REGION && got < nh && !stopped(x); j++) {
                int64_t t0 = now_ns();
                ((volatile unsigned char *)p)[(size_t)j * HP_SIZE] = 1;
                x->s[got++] = (double)(now_ns() - t0);
                faults++;
            }
            if ((kb = vma_huge_kb(p)) > 0) huge_kb += kb;
            munmap(raw, len);
        }
        if (stopped(x)) { unpin(x); return; }
        snprintf(det, sizeof det, "first write; %ld of %ld got a huge page; /512 = per 4 KiB", huge_kb / 2048, faults);
        stat_row(x, OS_G_MEMORY, "fault2m", "Page fault, 2 MiB huge page", det, got);
    }
    if (stopped(x)) { unpin(x); return; }

    /* mmap and munmap of 64 KiB that is never touched: only the address-space bookkeeping */
    set_stage(grp_name[OS_G_MEMORY], "mmap / munmap 64 KiB");
    {
        double *mu = malloc((size_t)n * sizeof *mu);
        long gm = 0;
        if (mu) {
            for (i = 0; i < n + 50 && !stopped(x); i++) {
                int64_t t0 = now_ns(), t1;
                void *p = mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                t1 = now_ns();
                if (p == MAP_FAILED) break;
                munmap(p, 65536);
                if (i >= 50) { x->s[gm] = (double)(t1 - t0); mu[gm++] = (double)(now_ns() - t1); }
            }
            if (!stopped(x)) {
                snprintf(det, sizeof det, "CPU %d; 64 KiB anonymous, never touched", x->r->cpu_a);
                stat_row(x, OS_G_MEMORY, "mmap", "mmap() 64 KiB", det, gm);
                memcpy(x->s, mu, (size_t)gm * sizeof *mu);
                stat_row(x, OS_G_MEMORY, "munmap", "munmap() 64 KiB", det, gm);
            }
            free(mu);
        }
    }
    unpin(x);
}

/* ======================================================================
 * 7. file metadata: create / stat / unlink of small files in our own temporary folder
 * ====================================================================== */
static int our_file(const char *n) { int i; if (n[0] != 'f' || strlen(n) != 7) return 0; for (i = 1; i < 7; i++) if (n[i] < '0' || n[i] > '9') return 0; return 1; }

/* remove a test folder: only our own "f000000" files, then the folder (left alone if anything else
   is in it). O_NOFOLLOW: a symlink in its place is never followed. */
static void rm_testdir(const char *dir)
{
    int fd = open(dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    DIR *d = fd >= 0 ? fdopendir(fd) : NULL;
    struct dirent *e;
    if (d) {
        while ((e = readdir(d))) if (our_file(e->d_name)) unlinkat(fd, e->d_name, 0);
        closedir(d);   /* closes fd too */
    } else if (fd >= 0) close(fd);
    if (fd >= 0) rmdir(dir);
}

/* folders left by a run that was killed: "srs-os-<pid>-XXXXXX" whose process is gone */
static void rm_stale(const char *cache)
{
    DIR *d = opendir(cache);
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d))) {
        int pid, len = 0;
        char p[PATH_MAX];
        if (sscanf(e->d_name, "srs-os-%d-%*6[A-Za-z0-9]%n", &pid, &len) < 1 || len != (int)strlen(e->d_name) || pid <= 0) continue;
        snprintf(p, sizeof p, "/proc/%d", pid);
        if (pid == (int)getpid() || access(p, F_OK) == 0) continue;   /* that run is still going */
        if (snprintf(p, sizeof p, "%s/%s", cache, e->d_name) < (int)sizeof p) rm_testdir(p);
    }
    closedir(d);
}

static void t_files(octx *x)
{
    char dir[PATH_MAX], nm[16], det[96];
    const char *why = NULL;
    long n = scaled(x, 10, 20), i;
    int dfd = -1, err = 0;
    double *st = NULL, *ul = NULL;
    static const char data[100] = "SRS OS latency test file";
    storage_where *w = calloc(1, sizeof *w);
    set_stage(grp_name[OS_G_FILES], "create / stat / unlink of small files");
    if (n > 999999) n = 999999;   /* names are f000000 .. f999999 */
    if (snprintf(dir, sizeof dir, "%s/srs-os-%d-XXXXXX", cache_dir(), (int)getpid()) >= (int)sizeof dir) {
        skip_row(x, OS_G_FILES, "create", "Create small file", "the cache folder path is too long");
        goto out_nodir;
    }
    if (!w || !(st = malloc((size_t)n * sizeof *st)) || !(ul = malloc((size_t)n * sizeof *ul))) {
        skip_row(x, OS_G_FILES, "create", "Create small file", "out of memory");
        goto out_nodir;
    }
    if (!mkdtemp(dir)) { skip_row(x, OS_G_FILES, "create", "Create small file", "cannot create a folder in ~/.cache/srs"); goto out_nodir; }
    if ((dfd = open(dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) < 0) { err = errno; why = "open the test folder"; goto out; }
    storage_probe(dir, w);
    snprintf(x->r->tmp_fs, sizeof x->r->tmp_fs, "%s", w->fstype);
    snprintf(x->r->tmp_mount, sizeof x->r->tmp_mount, "%.*s", (int)sizeof x->r->tmp_mount - 1, w->mount);
    /* names relative to the folder (openat / fstatat / unlinkat): one path step per call */
    /* create: openat(O_CREAT | O_EXCL) + 100-byte write + close */
    for (i = 0; i < n && !stopped(x); i++) {
        int64_t t0;
        int fd;
        snprintf(nm, sizeof nm, "f%06u", (unsigned)i);
        t0 = now_ns();
        fd = openat(dfd, nm, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd < 0) { err = errno; why = "create"; break; }
        if (write(fd, data, sizeof data) != (ssize_t)sizeof data) { err = errno ? errno : ENOSPC; why = "write"; close(fd); break; }
        close(fd);
        x->s[i] = (double)(now_ns() - t0);
    }
    if (stopped(x) || why) goto out;
    for (i = 0; i < n && !stopped(x); i++) {
        struct stat sb;
        int64_t t0;
        snprintf(nm, sizeof nm, "f%06u", (unsigned)i);
        t0 = now_ns();
        if (fstatat(dfd, nm, &sb, 0)) { err = errno; why = "stat"; break; }
        st[i] = (double)(now_ns() - t0);
    }
    if (stopped(x) || why) goto out;
    for (i = 0; i < n && !stopped(x); i++) {
        int64_t t0;
        snprintf(nm, sizeof nm, "f%06u", (unsigned)i);
        t0 = now_ns();
        if (unlinkat(dfd, nm, 0)) { err = errno; why = "unlink"; break; }
        ul[i] = (double)(now_ns() - t0);
    }
    if (stopped(x) || why) goto out;
    snprintf(det, sizeof det, "%ld files in one folder on %.24s; no fsync", n, x->r->tmp_fs[0] ? x->r->tmp_fs : "?");
    stat_row(x, OS_G_FILES, "create", "Create small file (open+write+close)", det, n);
    memcpy(x->s, st, (size_t)n * sizeof *st);
    stat_row(x, OS_G_FILES, "stat", "stat() of a file", det, n);
    memcpy(x->s, ul, (size_t)n * sizeof *ul);
    stat_row(x, OS_G_FILES, "unlink", "unlink() a file", det, n);
out:
    if (why && !stopped(x)) {
        snprintf(det, sizeof det, "%s failed: %s", why, strerror(err));
        skip_row(x, OS_G_FILES, "create", "Create small file (open+write+close)", det);
    }
    if (dfd >= 0) close(dfd);
    rm_testdir(dir);   /* every path: whatever was made is removed */
out_nodir:
    free(st);
    free(ul);
    free(w);
}

/* ======================================================================
 * rendering
 * ====================================================================== */
typedef struct { const os_cfg *c; os_result *r; octx *x; char when[32]; } orctx;

static void os_render(srs_out *o, void *cv)
{
    static const int tabs[] = { 270, 345, 420, 495, 570, 645, 715 }, tabs2[] = { 210 };
    orctx *c = cv;
    os_result *r = c->r;
    int i, g = -1;
    char sl[32];
    so_siv_header(o, "OS latency (system calls, context switches, process start, wake-ups, timers, page faults, file metadata)");
    so_tabs(o, tabs, 7);
    so_row(o, 8, (const char *const[]){ "Test", "Median", "95%", "99%", "Worst", "Best", "Samples", "How" },
           (const int[]){ C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL, C_LABEL });
    for (i = 0; i < r->nrows; i++) {
        os_row *w = &r->row[i];
        char m[24], a[24], b[24], wo[24], be[24], ns[24];
        if (w->group != g) {
            g = w->group;
            so_b(o, C_TEAL, grp_name[g]);
            so_par(o);
        }
        if (w->skipped) {
            so_row(o, 8, (const char *const[]){ w->name, "-", "", "", "", "", "", w->detail },
                   (const int[]){ C_VALUE, C_GRAY, C_GRAY, C_GRAY, C_GRAY, C_GRAY, C_GRAY, C_GRAY });
            continue;
        }
        os_fmt_ns(m, sizeof m, w->med); os_fmt_ns(a, sizeof a, w->p95); os_fmt_ns(b, sizeof b, w->p99);
        os_fmt_ns(wo, sizeof wo, w->worst); os_fmt_ns(be, sizeof be, w->best);
        snprintf(ns, sizeof ns, "%ld", w->n);
        so_row(o, 8, (const char *const[]){ w->name, m, a, b, wo, be, ns, w->detail },
               (const int[]){ C_VALUE, C_GREEN, C_BLACK, C_BLACK, w->worst > 20 * w->med ? C_ORANGE : C_BLACK, C_BLACK, C_GRAY, C_GRAY });
    }
    so_par(o);
    so_tabs(o, tabs2, 1);
    if (r->cancelled) so_kv(o, "Status", C_ORANGE, "Stopped before the end - the missing tests were not measured");
    so_kv(o, "CPU scheduler", C_VALUE, r->sched[0] ? r->sched : "unknown");
    if (r->sched_detail[0]) so_kv(o, "Scheduler detail", C_GRAY, r->sched_detail);
    so_kv(o, "Preemption", C_VALUE, r->preempt[0] ? r->preempt : "unknown");
    if (r->hz[0]) so_kvf(o, "Timer tick", C_VALUE, "%s Hz (CONFIG_HZ)", r->hz);
    else so_kv(o, "Timer tick", C_VALUE, "not readable (no kernel config in /boot or the kernel headers)");
    so_kv(o, "Clock source", C_VALUE, r->clocksource[0] ? r->clocksource : "unknown");
    os_fmt_ns(sl, sizeof sl, (double)r->timer_slack_ns);
    so_kvf(o, "Timer slack", C_VALUE, "%s (this thread; the kernel may wake a sleeper this much later to save power)", sl);
    so_kv(o, "Transparent huge pages", C_VALUE, r->thp);
    if (r->cpu_b >= 0) so_kvf(o, "CPUs used", C_VALUE, "CPU %d (same CPU tests), CPU %d (two-core tests, %s)", r->cpu_a, r->cpu_b,
                              r->b_smt ? "only the other thread of the same core is allowed" :
                              r->b_same_l3 ? "shares the L3 cache" : "no shared L3 found");
    else so_kvf(o, "CPUs used", C_VALUE, "CPU %d (only one CPU allowed)", r->cpu_a);
    if (r->tmp_fs[0]) so_kvf(o, "File test folder", C_VALUE, "temporary folder in ~/.cache/srs (%s, mounted at %s), removed afterwards", r->tmp_fs, r->tmp_mount);
    so_kvf(o, "Samples", C_VALUE, "%ld base (slow tests use a tenth to a fortieth), 1 ms tick %.1f s", c->c->samples, c->c->jitter_secs);
    so_kvf(o, "Run time", C_VALUE, "%.1f s", r->seconds);
    so_kv(o, "Run", C_GRAY, c->when);
    so_kv(o, "Build", C_GRAY, bench_build_tag());
    so_note(o, "Every sample is one operation (or a batch, divided - see How). Median = typical; 95% and 99% = 19 in 20 and 99 in 100 "
               "samples were this fast or faster; Worst = the single slowest. 1 µs = 1,000 ns, 1 ms = 1,000 µs. A pipe round trip is two hand-overs, "
               "so the figure shown is half of it. The tick and sleep rows show how late a sleeping program is woken: lower and steadier "
               "is a more responsive desktop. Core-to-core latency is the IPI test, memory latency the Latency test.");
    so_board_footer(o);
}

static const os_row *find_key(const octx *x, const char *k)
{
    int i;
    for (i = 0; i < x->r->nrows; i++) if (!strcmp(x->key[i], k) && !x->r->row[i].skipped) return &x->r->row[i];
    return NULL;
}

static void hist_part(char *res, size_t rn, const octx *x, const char *k, const char *label, int p99)
{
    const os_row *w = find_key(x, k);
    char v[24];
    size_t l = strlen(res);
    if (!w || l + 2 >= rn) return;
    os_fmt_ns(v, sizeof v, p99 ? w->p99 : w->med);
    snprintf(res + l, rn - l, "%s%s %s%s", l ? ", " : "", label, v, p99 ? " (99%)" : "");
}

/* ======================================================================
 * the benchmark
 * ====================================================================== */
int bench_os(const os_cfg *cfg, os_result *res, srs_out *o, volatile int *cancel)
{
    os_cfg c = *cfg;
    octx x;
    orctx rc;
    os_result *r = res ? res : calloc(1, sizeof *r);
    time_t now = time(NULL);
    int64_t t0 = now_ns();
    char params[320], hist[400];
    int g;
    if (!r) return -1;
    memset(r, 0, sizeof *r);
    if (c.samples <= 0) c.samples = 20000;
    if (c.samples < 100) c.samples = 100;
    if (c.samples > 1000000) c.samples = 1000000;
    if (c.jitter_secs <= 0) c.jitter_secs = 3.0;
    if (c.jitter_secs < 0.1) c.jitter_secs = 0.1;
    if (c.jitter_secs > 60) c.jitter_secs = 60;
    if (!c.run_mask) c.run_mask = (1u << OS_NGROUP) - 1;
    memset(&x, 0, sizeof x);
    x.c = &c;
    x.r = r;
    x.cancel = cancel;
    x.cap = c.samples;
    if (x.cap < (long)(c.jitter_secs * 1000) + 16) x.cap = (long)(c.jitter_secs * 1000) + 16;
    if (!(x.s = malloc((size_t)x.cap * sizeof *x.s))) { if (!res) free(r); return -1; }
    pthread_getaffinity_np(pthread_self(), sizeof x.saved, &x.saved);
    set_stage("Starting", "reading the scheduler and timer settings");
    rm_stale(cache_dir());   /* test folders of a run that was killed, whatever groups run now */
    sys_info(r);
    pick_cpus(&x);
    for (g = 0; g < OS_NGROUP && !stopped(&x); g++) {
        if (!(c.run_mask & (1u << g))) continue;
        switch (g) {
        case OS_G_SYSCALL: t_syscalls(&x); break;
        case OS_G_SWITCH: t_switch(&x); break;
        case OS_G_SPAWN: t_spawn(&x); break;
        case OS_G_WAKE: t_wake(&x); break;
        case OS_G_TIMER: t_timer(&x); break;
        case OS_G_MEMORY: t_memory(&x); break;
        case OS_G_FILES: t_files(&x); break;
        }
    }
    unpin(&x);
    stopped(&x);
    r->seconds = (double)(now_ns() - t0) / 1e9;
    g_os_stage = "";
    rc.c = &c; rc.r = r; rc.x = &x;
    strftime(rc.when, sizeof rc.when, "%Y-%m-%d %H:%M:%S", localtime(&now));
    bench_save_render("os-latency", os_render, &rc, o);
    {
        char groups[96] = "";
        size_t l;
        if (c.run_mask == (1u << OS_NGROUP) - 1) snprintf(groups, sizeof groups, "all");
        else for (g = 0; g < OS_NGROUP; g++)
            if (c.run_mask & (1u << g)) { l = strlen(groups); snprintf(groups + l, sizeof groups - l, "%s%s", l ? "," : "", grp_id[g]); }
        snprintf(params, sizeof params, "samples=%ld tick=%.1fs groups=%s sched=%s", c.samples, c.jitter_secs, groups, r->sched);
    }
    hist[0] = 0;
    hist_part(hist, sizeof hist, &x, "getpid", "getpid", 0);
    hist_part(hist, sizeof hist, &x, "sw-thr-same", "switch same-CPU", 0);
    hist_part(hist, sizeof hist, &x, "sw-thr-diff", "2-core", 0);
    hist_part(hist, sizeof hist, &x, "thread", "thread", 0);
    hist_part(hist, sizeof hist, &x, "fork", "fork", 0);
    hist_part(hist, sizeof hist, &x, "spawn", "spawn", 0);
    hist_part(hist, sizeof hist, &x, "futex-diff", "futex wake", 0);
    hist_part(hist, sizeof hist, &x, "tick1ms", "1ms tick late", 1);
    hist_part(hist, sizeof hist, &x, "fault4k", "4K fault", 0);
    hist_part(hist, sizeof hist, &x, "create", "file create", 0);
    if (r->cancelled) { size_t l = strlen(hist); snprintf(hist + l, sizeof hist - l, " STOPPED"); }
    bench_history_add("os-latency", params, hist[0] ? hist : "nothing measured");
    free(x.s);
    g = r->cancelled ? 1 : 0;
    if (!res) free(r);
    return g;
}

#else   /* not Linux */
const char *os_stage(void) { return ""; }
int os_available(void) { return 0; }
int os_sched_info(char *name, size_t nn, char *detail, size_t dn)
{
    if (nn) snprintf(name, nn, "unknown");
    if (dn) snprintf(detail, dn, "Linux only");
    return 0;
}
int bench_os(const os_cfg *c, os_result *r, srs_out *o, volatile int *cancel)
{
    (void)c; (void)cancel;
    if (r) memset(r, 0, sizeof *r);
    if (o) so_err(o, "OS latency: not available on this system (Linux only)");
    return -1;
}
#endif
