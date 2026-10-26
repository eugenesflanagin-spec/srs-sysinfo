/* bench_os.h - OS latency benchmark: how quickly the operating system answers a program.
 * Written for SRS (original code). Linux only; other systems get a "not available" stub.
 *
 * Measured, each as many single samples with median / 95% / 99% / worst (and best, mean):
 *  - system call cost: getpid via syscall(), clock_gettime through the vDSO and as a real
 *    system call, a 1-byte read of /dev/zero, sched_yield
 *  - context switch: pipe ping-pong between two threads and two processes, same CPU and two
 *    different cores (same L3 when possible)
 *  - thread create + join, fork + wait, posix_spawn of /bin/true + wait
 *  - wake-up latency: futex wake and condition-variable signal, until the sleeper runs
 *  - timers: clock_nanosleep overshoot at 50 µs and 1 ms, and the lateness of a periodic 1 ms
 *    tick over a few seconds (scheduler responsiveness), with the CPU scheduler that is running
 *    (sched_ext ops such as scx_lavd, else BORE / EEVDF / CFS) and this thread's timer slack
 *  - memory: first-touch page faults (4 KiB pages, transparent huge pages when enabled),
 *    mmap and munmap
 *  - files: create, stat and unlink of small files in a temporary folder under ~/.cache/srs
 *    (removed again on every path, also after Stop)
 * Core-to-core cache latency (bench_ipi) and memory latency (bench_latency) are separate tests.
 * Nothing is changed on the system: no root, no real-time priority, no timer-slack change. */
#ifndef BENCH_OS_H
#define BENCH_OS_H

#include "sec.h"

/* test groups (bits of os_cfg.run_mask) */
enum { OS_G_SYSCALL, OS_G_SWITCH, OS_G_SPAWN, OS_G_WAKE, OS_G_TIMER, OS_G_MEMORY, OS_G_FILES, OS_NGROUP };
#define OS_MAXROWS 40

typedef struct {
    char name[64];        /* "getpid() via syscall()" */
    char detail[96];      /* "CPU 2 <-> CPU 4", "batch of 32 calls per sample", why skipped ... */
    int group;            /* OS_G_* */
    int skipped;          /* 1 = not measured (detail says why) */
    long n;               /* samples */
    double med, p95, p99, worst, best, mean;   /* ns per operation */
} os_row;

typedef struct os_result {
    os_row row[OS_MAXROWS];
    int nrows;
    int cancelled;
    /* the system the numbers belong to */
    char sched[96];       /* "scx_lavd (sched_ext, all tasks)", "BORE (EEVDF + burst score)", "EEVDF" ... */
    char sched_detail[160];   /* raw ops name, sched_bore value, where it was read */
    char preempt[48];     /* "PREEMPT_DYNAMIC" (from uname -v) */
    char hz[24];          /* "1000" from /boot/config-<release> or the kernel headers, "" = not readable */
    char clocksource[32]; /* "tsc" */
    char thp[48];         /* "[always] madvise never" */
    char tmp_fs[64];      /* file system of the temporary folder ("btrfs") */
    char tmp_mount[256];
    long timer_slack_ns;  /* prctl(PR_GET_TIMERSLACK) of the measuring thread */
    long rss_kib;         /* VmRSS when fork was measured (fork cost grows with it) */
    int cpu_a, cpu_b;     /* CPUs used for "same CPU" (a) and "different cores" (a, b); b = -1 none */
    int b_same_l3;        /* 1 when cpu_b shares the L3 cache with cpu_a */
    int b_smt;            /* 1 when cpu_b is cpu_a's SMT sibling (no other core allowed) */
    double seconds;       /* wall time of the run */
} os_result;

/* called after every finished row (live table); may run on the benchmark's thread */
typedef void (*os_cb)(const os_row *r, int index, void *ud);

typedef struct {
    unsigned run_mask;    /* bit OS_G_*; 0 = every group */
    long samples;         /* base sample count (default 20000; 100 .. 1000000); slow tests use a fraction */
    double jitter_secs;   /* periodic 1 ms tick test length (default 3 s; 0.1 .. 60) */
    os_cb cb;
    void *ud;
} os_cfg;

void os_cfg_default(os_cfg *c);
int  os_available(void);                  /* 1 on Linux */
const char *os_stage(void);               /* "Context switch: two processes, different cores" while running */
const char *os_group_name(int g);         /* "System calls", "Context switches", ... */
const char *os_group_id(int g);           /* "syscall", "switch", "spawn", "wake", "timer", "memory", "files" */
/* the running CPU scheduler (also filled into os_result by bench_os); returns 1 when known */
int  os_sched_info(char *name, size_t nn, char *detail, size_t dn);
/* run, render into o (may be NULL) and ~/.cache/srs/bench-os-latency.txt, add a history line;
   r (may be NULL) receives every row. 0 ok, 1 stopped (partial result shown), -1 not available
   or out of memory */
int  bench_os(const os_cfg *c, os_result *r, srs_out *o, volatile int *cancel);
/* "52 ns", "1.84 µs", "112 µs", "1.06 ms" */
void os_fmt_ns(char *b, unsigned long n, double ns);

#endif
