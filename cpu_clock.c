/* cpu_clock.c - the CPU's real base (reference) clock (2.6.0).
 * The kernel works out the cpufreq limits (cpuinfo_max_freq, scaling_max_freq, CPPC nominal_freq,
 * the amd-pstate / intel_pstate base) at a 100 MHz base clock, so on a PC run with another base
 * clock (BCLK, or eCLK on AM5, e.g. 104 MHz) they are that much too low. The current clock is
 * different: APERF/MPERF x the measured TSC rate, so it is already real.
 * Base clock: the invariant TSC runs at the nominal clock, and CPPC nominal_freq is that clock at a
 * 100 MHz base, so base = TSC x 100 / nominal_freq (measured once over 50 ms; same method as the
 * [cpu-clocks] page). SRS_BASE_CLOCK_MHZ overrides it (tests and fixtures: never mix a fake /sys
 * with this PC's TSC). Thread-safe: the Performance page samples on its own thread. */
#define _GNU_SOURCE
#include "cpu_clock.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#define CPU_CLOCK_TSC 1
#endif

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static double g_base, g_tsc, g_nom;

double cpu_clock_tsc_mhz(void)
{
#ifdef CPU_CLOCK_TSC
    struct timespec a, b;
    unsigned long long t0, t1;
    double ns;
    clock_gettime(CLOCK_MONOTONIC_RAW, &a);
    t0 = __rdtsc();
    do {
        clock_gettime(CLOCK_MONOTONIC_RAW, &b);
        ns = (b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec);
    } while (ns < 50e6);
    t1 = __rdtsc();
    return (double)(t1 - t0) / (ns / 1e3);
#else
    return 0;
#endif
}

#ifdef CPU_CLOCK_TSC
static int constant_tsc(void)
{
    char line[8192];
    int yes = 0;
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "flags", 5)) { yes = strstr(line, " constant_tsc") != NULL; break; }
    fclose(f);
    return yes;
}
#endif

static void init(void)
{
    const char *e = getenv("SRS_BASE_CLOCK_MHZ");
    if (e && *e) {
        double b = atof(e);
        g_base = b >= 50 && b <= 200 ? b : 0;
        return;
    }
#ifdef CPU_CLOCK_TSC
    {
        FILE *f = fopen("/sys/devices/system/cpu/cpu0/acpi_cppc/nominal_freq", "r");
        long n = 0;
        if (f) { if (fscanf(f, "%ld", &n) != 1) n = 0; fclose(f); }
        if (n > 0 && constant_tsc()) {
            g_nom = (double)n;
            g_tsc = cpu_clock_tsc_mhz();
            g_base = g_tsc * 100.0 / g_nom;
            if (g_base < 50 || g_base > 200) g_base = 0;
        }
    }
#endif
}

double cpu_clock_base_mhz(double *tsc, double *nom)
{
    pthread_once(&g_once, init);
    if (tsc) *tsc = g_tsc;
    if (nom) *nom = g_nom;
    return g_base;
}

double cpu_clock_ratio(void)
{
    double b = cpu_clock_base_mhz(NULL, NULL);
    return b > 0 && fabs(b - 100.0) >= 0.5 ? b / 100.0 : 1.0;
}
