/* gpu_tune.h - GPU Tuning, the read-only part (new in SRS 2.6): what a graphics card is set to right now
 * (voltage offset, clock offset, memory clock, power limit), the limits, whether Tux Burner and LACT are there.
 *
 * SRS never writes to a graphics card. Changing a setting is the job of Tux Burner, a separate program whose root
 * service holds the safety limits; this file only READS:
 *   - the kernel's files under /sys (the amdgpu OverDrive table pp_od_clk_voltage, hwmon),
 *   - Tux Burner's service socket, with exactly two read requests ("status" and "gpu_capabilities"); there is no
 *     function here that could send anything else,
 *   - whether LACT is running (a file and a process name) and LACT's saved settings file. LACT's service is never
 *     contacted, its file is never written.
 *
 * Tests: SRS_TEST_SYSFS_ROOT=<dir> is put in front of /sys, /proc, /run, /etc and /usr (a made-up card);
 * SRS_TUX_BURNER_SOCKET=<path> names the socket (a mock server). In a test with no socket named, the real socket is
 * NOT used; and in the GUI tests (their private bus, SRS_TEST_PRIVATE_BUS, or SRS_TEST_ALIGN_DUMP) without a test
 * root nothing real is read at all: not the card, not the service, not LACT's file.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef GPU_TUNE_H
#define GPU_TUNE_H

#include "sec.h"

#include <limits.h>
#include <stdio.h>

#define GTN_MAXGPU 8
#define GTN_NONE INT_MIN            /* a number the card, the service or LACT does not give */

/* Tux Burner's service, as seen from here */
enum { GTD_ABSENT,                  /* no socket: the service is not installed or not running */
       GTD_DOWN,                    /* a socket file, but nobody answers the connection */
       GTD_DENIED,                  /* the service does not serve this user */
       GTD_BAD,                     /* no answer in time, or an answer SRS does not understand */
       GTD_OLD,                     /* answers, but speaks another protocol version or has no tuning support */
       GTD_RUNNING,
       GTD_UNCHECKED };             /* tests: a made-up /sys with no socket named - the real one is not asked */

typedef struct {
    char card[16], pci[16], driver[24], name[128];
    unsigned vendor_id, device_id, sub_vendor, sub_device, revision;
    const char *vendor;             /* "amd", "nvidia", "intel", "other" */
    int asleep;                     /* powered down by the kernel: nothing that would wake it was read */
    int od;                         /* OverDrive table: 1 = read (a voltage offset; RDNA 4: a core clock offset, RDNA 3:
                                       the highest core clock), 2 = an older layout SRS does not handle yet (clock and
                                       voltage points), 0 = none, -1 = there but not readable */
    int vo_mv, sclk_off_mhz, mclk_max_mhz;            /* now on the card (sysfs) */
    int sclk_max_mhz;                                 /* RDNA 3: the highest core clock (it has no offset) */
    int r_vo[2], r_sclk[2], r_mclk[2];                /* the driver's own range (OD_RANGE) */
    int r_sclk_abs[2];                                /* RDNA 3: the range of the highest core clock */
    int cap_w, cap_min_w, cap_max_w, cap_default_w;   /* power limit (hwmon) */
    double edge_c, junction_c, power_w, clock_mhz, vddgfx_mv, fan_rpm;   /* live; -1 = not available */
    /* Tux Burner's view of this card (its gpu_capabilities answer) */
    int tb;                         /* the service lists this card */
    int tb_index;
    char tb_vendor[16];
    int tb_floor_mv;                /* the lowest voltage offset the service will write */
    char tb_floor_source[24], tb_floor_file[160];
    int tb_vo_mv, tb_r_vo[2], tb_sclk_off, tb_mem_off, tb_r_sclk[2], tb_r_mem[2];
    int tb_cap_w, tb_cap_min_w, tb_cap_max_w, tb_cap_default_w;
    int tb_locked;                  /* NVIDIA: the card's clock control is locked */
    /* LACT's saved values for this card: the active profile, else the base section */
    int lact_have;
    int lact_vo, lact_sclk, lact_mclk_max, lact_cap_w;
    int lact_matches;               /* 1 the card is at these values, 0 it is not, -1 nothing to compare */
} gtune_gpu;

typedef struct {
    int cli;                        /* tburn-cli is on PATH */
    char cli_path[300];
    int daemon;                     /* GTD_* */
    char socket[108], msg[240];     /* msg: the service's own words when it refused, or why it could not be asked */
    char version[32], build[32], state[48];
    int proto_major, proto_minor;
    int cap_amd;                    /* "amd-tuning-v1" */
    char missing[160];              /* abilities a search needs that this service does not have yet, "" = none */
    int gpu_count;
} gtune_tb;

typedef struct {
    int running;                    /* LACT's socket file is there and a lact process is alive */
    int config_read;                /* the settings file was read */
    char config[200], sha256[65];
    int has_profile;
    char profile[64];               /* the active profile's name */
    int auto_switch;                /* LACT switches profiles by itself */
} gtune_lact;

typedef struct {
    int ngpu;
    int testmode;                   /* a test without a made-up /sys: the real card, service and LACT were NOT read */
    int asleep_skip;                /* a card is asleep: the service was not asked about the cards (it could wake one) */
    gtune_gpu gpu[GTN_MAXGPU];
    gtune_tb tb;
    gtune_lact lact;
} gtune_status;

/* read everything (never writes; bounded waits). wake: also read a card that is asleep (this wakes it) */
void gtune_read(gtune_status *st, int wake);
/* the card shown by default: the first one with tuning controls, else the first; -1 = no graphics card */
int  gtune_pick(const gtune_status *st);
int  gtune_find_pci(const gtune_status *st, const char *pci);
/* could a setting be changed from here (all the pieces in place)? 0 = no, why says the first thing missing */
int  gtune_can_write(const gtune_status *st, int idx, char *why, size_t n);
/* the page / "gpu-tune status" text: one card (idx) or every card (idx < 0) */
void gtune_render(srs_out *o, const gtune_status *st, int idx);
/* "gpu-tune status --json": one object per card */
void gtune_json(FILE *f, const gtune_status *st, int idx);
/* the [gpu-tuning] item (Benchmarks & Stress > GPU Tuning) */
void gtune_section(srs_out *o);

#endif
