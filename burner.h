/* burner.h - SIV-style CPU Stress / CPU Burnin engine (Linux).
 * Worker threads pinned per CPU: "same" mode runs Whetstone on its CPU and reports
 * the rating, "swap" mode keeps moving between the enabled CPUs. Each thread checks it
 * really runs where it was pinned (sched_getcpu + CPUID APIC ID vs /proc/cpuinfo).
 * A monitor samples /proc/stat, cpufreq, k10temp, Super-I/O hwmon and amdgpu.
 * Safety: nothing starts by itself; a temperature cutoff and a time cap end the run. */
#ifndef BURNER_H
#define BURNER_H

#include <stddef.h>

#define BURN_MAXCPU 256

enum { BURN_SAME, BURN_SWAP };
/* what each CPU worker thread runs (the last three after s-tui's loads, written for SRS); every one but
   Whetstone checks its own results, and a wrong result fails the run */
enum { BURN_WL_WHETSTONE, BURN_WL_MATMUL, BURN_WL_SHA256, BURN_WL_SQRT, BURN_NWL };
extern const char *const burn_wl_id[BURN_NWL];     /* "whetstone", "matmul", "sha256", "sqrt" (CLI words) */
extern const char *const burn_wl_name[BURN_NWL];   /* "Whetstone", "Matrix multiply", ... */
extern const char *const burn_wl_unit[BURN_NWL];   /* "MWIPS", "GFLOPS", "MB/s", "M/s" */
#define BURN_MEM_MB_MAX 65536
#define BURN_DISK_MB_MAX 65536
enum { BURN_IDLE, BURN_RUNNING, BURN_COOLING, BURN_PASS, BURN_FAIL, BURN_STOPPED };

typedef struct {
    int ncpu;
    unsigned char enabled[BURN_MAXCPU];
    int mode;                  /* BURN_SAME / BURN_SWAP */
    int workload;              /* BURN_WL_* run by every CPU worker thread */
    double burn_s;             /* burn phase length, seconds (0 = until stopped / capped) */
    double cool_s;             /* idle phase between burn phases, seconds */
    int cycles;                /* burn/cool cycles, 0 = 1 */
    double temp_limit_c;       /* Fail when hottest CPU temp exceeds this (0 = off) */
    double cap_s;              /* hard cap on the whole run, seconds */
    const char *gpu_cmd;       /* optional external GPU load command, run through /bin/sh (NULL = none) */
    const char *const *gpu_argv; /* the built-in GPU Burnin (srs-gui --gpu-burn-child ...), run directly; wins over gpu_cmd */
    const char *gpu_label;     /* its name in the status lines (NULL = "GPU load command") */
    int gpu_follow;            /* 1: the GPU part pauses in each cool phase (synced to the CPU burn phases); 0: it runs once
                                  from Start for its own time. Either way it ends with the run. No CPU enabled = GPU only. */
    /* side loads (0 workers = off): threads that are not pinned to a CPU and follow the burn / cool phases */
    int mem_workers, mem_mb;   /* memory: each worker writes its own block of mem_mb MB and reads it back, again and again */
    int sync_workers;          /* sync: each worker rewrites a small block of its own file and flushes it to the disk */
    int disk_workers, disk_mb; /* disk write: each worker writes disk_mb MB to its own file, again and again */
    const char *disk_dir;      /* folder for the sync and disk files (NULL or "" = ~/.cache/srs); the files have no
                                  name in the folder (or are deleted at once), so nothing is left behind */
} burn_cfg;

/* exit codes of the built-in GPU Burnin child (srs-gui --gpu-burn-child) */
enum { GPUB_EXIT_PASS = 0, GPUB_EXIT_STOPPED = 10, GPUB_EXIT_FAIL = 11, GPUB_EXIT_NOTSTARTED = 12, GPUB_EXIT_EARLY = 13 };

typedef struct {
    int cpu, busy, cur_cpu, apic_ok, apic_errors;
    long passes;
    long errors;               /* wrong results the workload's own check found (Matrix multiply, SHA-256, Square root) */
    double mwips_last, mwips_avg, mwips_min, mwips_max;
} burn_thread;

typedef struct {
    double cpu_load, cpu_mhz, cpu_temp, sio_temp, gpu_temp, gpu_load, gpu_mhz;
    char cpu_temp_name[48], sio_name[48];
} burn_sample;

typedef struct {
    int state, cycle;
    double elapsed, remaining, phase_left;
    burn_sample cur, avg, roll, max;
    char status[400];
    char gpu_msg[300];         /* GPU load command state ("" = no command): running / finished / failed (exit code) */
    int gpu_bad;               /* 1 when the GPU load command failed or was not found */
    int gpu_running;           /* the GPU part's process is running (or still stopping) */
    int gpu_exit;              /* the built-in GPU Burnin's last exit code (GPUB_EXIT_*), -1 = none yet */
    int workload;              /* BURN_WL_* of this run */
    char load_note[48];        /* code path: "AVX-512", "SHA-NI", ... ("" = none) */
    int mem_workers, sync_workers, disk_workers, mem_mb, disk_mb;
    long mem_passes, mem_errors, sync_count, disk_passes;
    double mem_gb, disk_gb;    /* data written and read back (memory) / written (disk) so far */
    double side_s;             /* seconds the side loads have been running (for the averages) */
    char disk_dir[256];
    int nthreads;
    burn_thread th[BURN_MAXCPU];
    double per_cpu_user[BURN_MAXCPU], per_cpu_sys[BURN_MAXCPU], per_cpu_mhz[BURN_MAXCPU];
} burn_status;

int  burn_ncpu(void);
void burn_cfg_default(burn_cfg *c);
int  burn_start(const burn_cfg *c);          /* 0 ok, -1 already running / no CPUs */
void burn_stop(void);
void burn_tick(burn_status *st);            /* call ~1/s: samples sensors, advances phases */
int  burn_active(void);
int  burn_gpu_pid_set(void);                 /* any thread: a GPU part process is still there (read only, no reaping) */
int  burn_gpu_running(void);                 /* the GPU part (built-in or command) has not ended yet */
void burn_sample_now(burn_sample *s);        /* one sensor sample (no load) */
double whetstone_mwips(long loops, double *secs); /* single run on the calling thread */

struct srs_out;
void burn_render(struct srs_out *o, const burn_status *st);   /* the summary text (window, Copy, saved file) */
int  burn_save_summary(const burn_status *st, char *path, size_t n); /* -> ~/.cache/srs/burnin-YYYYMMDD-HHMMSS.txt; 0 ok */

#endif
