/* power_src.h - SRS "Power & cost": where the measured numbers come from (sysfs), the clocks,
 * and a first guess at the parts list from the detected hardware.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef SRS_POWER_SRC_H
#define SRS_POWER_SRC_H

#include "power_model.h"

#define PSRC_CH 8
#define PSRC_LINES 24
typedef struct {
    char path[320], label[120];
    int counter;          /* 1 = energy counter (uJ), 0 = power reading (uW) */
    double max_w;
    pm_counter c;
    double w;             /* last watts (NAN unknown) */
} psrc_chan;

typedef struct {
    psrc_chan cpu[PSRC_CH], gpu[PSRC_CH];
    int ncpu, ngpu;
    char cpu_how[160], gpu_how[160];   /* short: "zenergy", "amdgpu PPT", "not measured" */
    int dgpu_unread;                   /* a graphics card that SRS cannot read (NVIDIA, ...) */
    int rapl_state;                    /* 0 absent, 1 readable, 2 root-only */
    char lines[PSRC_LINES][400];       /* plain-words status of every source, for the Sources tab */
    int nlines;
    char busy_path[PSRC_CH][320];      /* gpu_busy_percent of the graphics cards */
    int nbusy;
    unsigned long long cpu_total, cpu_idle;
} psrc;

void psrc_detect(psrc *s);
/* read every source; dt = seconds since the last read (for counters). flags gets PM_F_* bits. */
void psrc_read(psrc *s, double dt, pm_meas *m, int *flags);
void psrc_clock(pm_clock *c);
/* how busy the PC was since the last call: CPU % (all cores, /proc/stat) and the busiest graphics
   card's gpu_busy_percent (NAN when unknown). Busy = either one at or above the threshold. */
void psrc_load(psrc *s, double *cpu, double *gpu);
/* boot time as wall-clock seconds (from /proc/stat btime), 0 unknown */
double psrc_boot_wall(void);
/* SRS's guess at the parts this PC has; returns how many were written */
int psrc_default_parts(const psrc *s, pm_part *parts, int max);
/* a list of typical parts to add from the Setup tab */
int psrc_preset_count(void);
const pm_part *psrc_preset(int i);

#endif
