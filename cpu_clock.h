/* cpu_clock.h - the CPU's real base (reference) clock, for PCs run off the standard 100 MHz
 * (BCLK, or eCLK on AM5). See cpu_clock.c. */
#ifndef CPU_CLOCK_H
#define CPU_CLOCK_H

/* TSC rate in MHz, measured over 50 ms (0 on CPUs without a TSC) */
double cpu_clock_tsc_mhz(void);

/* Base clock in MHz: TSC x 100 / ACPI CPPC nominal_freq, measured once per run; 0 = not known
   (no invariant TSC or no CPPC nominal_freq). tsc / nom (may be NULL) get the two inputs. */
double cpu_clock_base_mhz(double *tsc, double *nom);

/* Factor for frequencies the kernel works out at a 100 MHz base (cpuinfo_min/max_freq,
   scaling_min/max_freq, CPPC nominal/lowest freq, amd-pstate / intel_pstate base): base / 100,
   or 1.0 when the base clock is not known or within 0.5 MHz of 100. Never apply it to the current
   clock (scaling_cur_freq, cpuinfo_avg_freq, /proc/cpuinfo "cpu MHz"): that is APERF/MPERF x the
   measured TSC rate, so it already includes the base clock. */
double cpu_clock_ratio(void);

#endif
