/* perf_data.h - the sampler behind the GPU Top window's Performance tab (a live task-manager view):
 * CPU (total and per logical CPU, user and kernel time), clocks, CPU temperature and package power,
 * memory, pressure stall information, disks, network adapters, GPUs (through the gpu_top.h data
 * layer, GRBM performance counters always off) and every process.
 *
 * One pd_sampler is driven by one thread (pd_sample about once a second); pd_snap_copy() makes a
 * private copy for another thread. Nothing here needs root: what only root may read is reported
 * as such (pd_snap.cpu_w_state, pd_proc.rd < 0), never asked for.
 *
 * Tests: SRS_TEST_PROC_ROOT=/abs/dir reads <dir>/proc/... and <dir>/sys/... instead of /proc and
 * /sys (and <dir>/run/... for logind sessions); pd_set_root() switches it between two samples.
 *
 * SRS is Copyright (C) 2026 by Eugene Flanagin. Licensed under GPL-2.0-or-later. */
#ifndef SRS_PERF_DATA_H
#define SRS_PERF_DATA_H

#include "gpu_top.h"

#define PD_HIST 60                 /* history length: 60 samples = 60 s at 1 s */
#define PD_MAX_CPU 256
#define PD_MAX_DISK 16
#define PD_MAX_NET 12
#define PD_MAX_GPU 8
#define PD_MAX_TEMP 12

/* a fixed ring of the last PD_HIST values (NAN = no value) */
typedef struct { float v[PD_HIST]; int n, head; } pd_ring;
void pd_ring_push(pd_ring *r, double v);
/* i = 0 (oldest) .. r->n - 1 (newest) */
double pd_ring_at(const pd_ring *r, int i);
double pd_ring_last(const pd_ring *r);
double pd_ring_max(const pd_ring *r);

typedef struct {
    int pid, ppid, uid;
    unsigned long long start;      /* start time, clock ticks after boot */
    char name[48];                 /* comm (or the cmdline's program for truncated names) */
    char user[32];
    char state;                    /* R S D Z T I ... */
    char cmd[240];                 /* command line, spaces between arguments ("[kthreadd]" style for kernel threads) */
    double cpu;                    /* % of the whole machine (all logical CPUs = 100 %) */
    double cpu_s;                  /* CPU time used so far, seconds */
    long long rss, vsize, shared;  /* bytes */
    double rd, wr;                 /* disk read / write bytes per second; < 0 = not readable (other user) */
    long long rd_total, wr_total;  /* bytes since start, -1 not readable */
    int threads, nice, prio;
    int gpu;                       /* % (busiest engine, all GPUs), -1 = no GPU use */
    long long vram;                /* bytes of VRAM, -1 none */
    int is_app;                    /* 1 = application (has a desktop entry and a graphical session), 0 background */
    int kthread;
    int session;                   /* logind session from the cgroup path, -1 none */
    char unit[64];                 /* systemd unit from the cgroup path ("" none) */
} pd_proc;

typedef struct {
    char name[24];                 /* "nvme0n1" */
    char model[64];
    long long size;                /* bytes */
    int rotational, removable;
    double rd, wr;                 /* bytes per second */
    double busy;                   /* % of the interval with I/O in flight */
    double resp_ms;                /* average time per completed I/O in the interval */
    long long rd_total, wr_total;
    pd_ring h_busy, h_rd, h_wr;
} pd_disk;

typedef struct {
    char name[32];                 /* "enp5s0" */
    char kind[24];                 /* "Ethernet", "Wi-Fi", "Virtual", ... */
    char driver[32];
    int speed_mbps;                /* link speed, -1 unknown */
    int up;
    char mac[24], ipv4[48], ipv6[64];
    double rx, tx;                 /* bytes per second */
    long long rx_total, tx_total;
    pd_ring h_rx, h_tx;
} pd_net;

typedef struct {
    gt_device dev;
    int open;                      /* 1 = sampled; 0 = suspended (not woken) or could not open */
    char why[96];                  /* when not open */
    int have;                      /* a sample exists */
    int busy;                      /* %, -1 n/a */
    long long vram_used, vram_total, gtt_used, gtt_total;   /* bytes, -1 n/a */
    int sclk, mclk;                /* MHz, -1 */
    double power;                  /* W, < 0 n/a */
    int power_cap;                 /* W, -1 */
    int edge, junction, memtemp;   /* C, -1 */
    int fan_rpm, fan_pct;          /* -1 */
    int pcie_gen, pcie_width;
    char pstate[8];                /* "D0", "D3hot" */
    int idle_d3;
    char vram_type[16];
    int max_sclk, max_mclk;
    char driver_version[64];
    pd_ring h_busy, h_vram, h_power, h_temp;   /* %, % of total VRAM, W, C */
} pd_gpu;

typedef struct {
    char label[40];                /* "CPU (Tctl)", "GPU 1 Hotspot", "nvme0 Composite" */
    double c;                      /* NAN n/a */
    pd_ring h;
} pd_temp;

enum { PD_W_OK = 0, PD_W_ROOT = 1, PD_W_NONE = 2 };

typedef struct {
    unsigned long long seq;
    double dt;                     /* seconds since the previous sample (0 on the first) */
    /* CPU */
    char cpu_model[96];
    int ncpu, cores, sockets, virt;   /* virt: 1 = AMD-V / VT-x flag present */
    char governor[32], freq_driver[32];
    double base_mhz, max_mhz;      /* <= 0 unknown */
    double cpu, cpu_user, cpu_kernel;   /* % of the whole machine */
    short cpu_id[PD_MAX_CPU];      /* the kernel's number of each online logical CPU ("cpuN") */
    float cu[PD_MAX_CPU], ck[PD_MAX_CPU];   /* per logical CPU: user, kernel % */
    pd_ring hu[PD_MAX_CPU], hk[PD_MAX_CPU];
    double mhz;                    /* average current clock of the online CPUs, <= 0 unknown */
    double mhz_peak;               /* the fastest CPU now */
    double cpu_temp;               /* C, NAN n/a */
    char cpu_temp_src[48];
    double cpu_w;                  /* W, NAN n/a */
    int cpu_w_state;               /* PD_W_* */
    char cpu_w_src[64];
    pd_ring h_cpu, h_kernel, h_mhz, h_ctemp, h_cpu_w;
    double load1, load5, load15;
    double uptime;                 /* seconds */
    int nprocs, nthreads;
    /* memory (bytes) */
    long long mem_total, mem_avail, mem_used, mem_cached, mem_buffers, mem_free, mem_shmem, mem_slab,
              mem_dirty, swap_total, swap_used, commit, commit_limit;
    pd_ring h_mem, h_swap;         /* % of total */
    /* pressure stall information (avg10 %), psi_ok = 0 when /proc/pressure is missing */
    int psi_ok;
    double psi_cpu, psi_mem, psi_mem_full, psi_io, psi_io_full;
    pd_ring h_psi;
    /* disks, network, GPUs, temperatures */
    int ndisk, nnet, ngpu, ntemp;
    pd_disk disk[PD_MAX_DISK];
    pd_net net[PD_MAX_NET];
    pd_gpu gpu[PD_MAX_GPU];
    pd_temp temp[PD_MAX_TEMP];
    double disk_rd, disk_wr, disk_busy;   /* sums; busy = busiest disk */
    double net_rx, net_tx;
    double gpu_w;                  /* all sampled GPUs, < 0 n/a */
    int gpu_busy, gpu_temp;        /* busiest GPU %, hottest edge C; -1 n/a */
    pd_ring h_disk, h_net, h_gpu;
    /* processes (heap array; pd_snap_copy / pd_snap_free manage it) */
    int nproc, proc_cap;
    pd_proc *proc;
    int napps;
} pd_snap;

typedef struct pd_sampler pd_sampler;

/* with_gpu = 0: no GPU devices at all (tests of the CPU side) */
pd_sampler *pd_new(int with_gpu);
void pd_free(pd_sampler *s);
/* take one sample; dt <= 0 measures the time since the last one. Returns the sampler's snapshot
   (valid until the next pd_sample / pd_free, owned by the sampler). */
const pd_snap *pd_sample(pd_sampler *s, double dt);
/* skip (0) or do (1, the default) the process walk in pd_sample: a sampler that needs only the
   whole-machine numbers (the tray meters) saves most of its time; nproc / napps stay 0 then */
void pd_set_procs(pd_sampler *s, int on);
/* switch the test root (SRS_TEST_PROC_ROOT) for the following samples; NULL = the environment's */
void pd_set_root(const char *root);
/* the root prefix in use ("" = the real system) */
const char *pd_root(void);

void pd_snap_copy(pd_snap *dst, const pd_snap *src);
void pd_snap_free(pd_snap *s);
/* index of pid in s->proc, -1 */
int pd_find(const pd_snap *s, int pid);

/* formatting helpers shared by the views: "1.5 GB", "12.0 MB/s", "3.20 GHz" */
void pd_fmt_bytes(double b, char *out, int n);
void pd_fmt_rate(double bps, char *out, int n);
void pd_fmt_bits(double bps, char *out, int n);   /* bytes per second shown as bits: "12.3 Mbps" */
void pd_fmt_mhz(double mhz, char *out, int n);
void pd_fmt_uptime(double s, char *out, int n);

/* plain-text dump of a snapshot for tests (one "key: value" per line) */
int pd_dump(const pd_snap *s, char *buf, int len);

#endif
