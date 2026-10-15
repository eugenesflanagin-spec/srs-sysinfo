/* burner.h - SIV-style CPU Stress / CPU Burnin engine (Linux).
 * Worker threads pinned per CPU: "same" mode runs Whetstone on its CPU and reports
 * the rating, "swap" mode keeps moving between the enabled CPUs. Each thread checks it
 * really runs where it was pinned (sched_getcpu + CPUID APIC ID vs /proc/cpuinfo).
 * A monitor samples /proc/stat, cpufreq, k10temp, Super-I/O hwmon and amdgpu.
 * Safety: nothing starts by itself; a temperature cutoff and a time cap end the run. */
#ifndef BURNER_H
#define BURNER_H

#define BURN_MAXCPU 256

enum { BURN_SAME, BURN_SWAP };
enum { BURN_IDLE, BURN_RUNNING, BURN_COOLING, BURN_PASS, BURN_FAIL, BURN_STOPPED };

typedef struct {
    int ncpu;
    unsigned char enabled[BURN_MAXCPU];
    int mode;                  /* BURN_SAME / BURN_SWAP */
    double burn_s;             /* burn phase length, seconds (0 = until stopped / capped) */
    double cool_s;             /* idle phase between burn phases, seconds */
    int cycles;                /* burn/cool cycles, 0 = 1 */
    double temp_limit_c;       /* Fail when hottest CPU temp exceeds this (0 = off) */
    double cap_s;              /* hard cap on the whole run, seconds */
    const char *gpu_cmd;       /* optional external GPU load command (NULL = none) */
} burn_cfg;

typedef struct {
    int cpu, busy, cur_cpu, apic_ok, apic_errors;
    long passes;
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
    char status[160];
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
void burn_sample_now(burn_sample *s);        /* one sensor sample (no load) */
double whetstone_mwips(long loops, double *secs); /* single run on the calling thread */

#endif
