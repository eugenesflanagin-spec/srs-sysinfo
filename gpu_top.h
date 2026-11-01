/* gpu_top.h - GPU Top: live per-GPU data (activity, GRBM blocks, VRAM, sensors, gpu_metrics, processes)
 * for the GPU Top window and `sysreport-search gpu-top`.
 *
 * The AMD parts are a C reimplementation of amdgpu_top (MIT licence, Copyright (c) 2023 Umio Yasuno,
 * https://github.com/Umio-Yasuno/amdgpu_top): same data, same order, same JSON names where sensible.
 * NVIDIA cards come from NVML (libnvidia-ml.so.1, loaded at run time, only for PCI vendor 0x10de).
 *
 * The contract between the data layer (gpu_top.c, gpu_top_amd.c, gpu_top_nvml.c) and its users
 * (gpu_top_gui.c, cli.c). Everything here is plain data: the structs hold no pointers, so a copy
 * (memcpy / struct assignment) is a complete snapshot that another thread may read.
 *
 * Threads: one gt_dev must be used by one thread at a time. The GUI runs gt_update() in a worker
 * thread and copies gt_stat out under its own lock.
 *
 * Tests: SRS_TEST_GPU_ROOT=/abs/dir reads every file from that tree instead of / (sys/..., proc/...,
 * usr/share/libdrm/amdgpu.ids) and the AMDGPU ioctl answers from <dir>/sys/.../srs-amdgpu-info.txt and
 * GRBM samples from <dir>/sys/.../srs-grbm.txt; nothing touches the real GPU then.
 * SRS_TEST_NVML_LIB=/abs/libnvml_stub.so loads that library instead of libnvidia-ml.so.1 (only together with
 * SRS_TEST_GPU_ROOT; with a test root and no stub NVML is not loaded at all).
 */
#ifndef GPU_TOP_H
#define GPU_TOP_H

#include <stddef.h>
#include <stdint.h>

#define GT_NA (-1)                 /* "not reported" for every int / long long field below */
#define GT_MAX_DEV 16
#define GT_MAX_PC 16               /* GRBM / GRBM2 blocks */
#define GT_MAX_PROC 128
#define GT_MAX_DPM 16              /* clock levels per pp_dpm_* file */
#define GT_MAX_KV 64               /* rows per info section */
#define GT_MAX_SECT 24             /* info sections */
#define GT_MAX_THR 32              /* throttle reasons */

typedef enum { GT_VENDOR_OTHER = 0, GT_VENDOR_AMD, GT_VENDOR_NVIDIA, GT_VENDOR_INTEL } gt_vendor;

/* ---- device list (no driver calls: sysfs only) ---- */
typedef struct {
    int index;                     /* 0.. in gt_device_list order (sorted by PCI address) */
    gt_vendor vendor;
    unsigned vendor_id, device_id, revision_id, subsys_vendor, subsys_device;
    char pci[16];                  /* "0000:03:00.0" */
    char card[32];                 /* "/dev/dri/card0" ("" when none) */
    char render[32];               /* "/dev/dri/renderD128" */
    char sysfs[96];                /* "/sys/bus/pci/devices/0000:03:00.0" (real path, not the test root) */
    char driver[24];               /* "amdgpu", "nvidia", "i915", "xe", ... */
    char name[96];                 /* marketing name ("AMD Radeon RX 9070 XT"), from amdgpu.ids / NVML / pci.ids */
    int active;                    /* 0 = runtime-suspended (amdgpu_top's "suspended devices"); reading wakes it */
} gt_device;

/* fills list[0..max-1], returns the number found (0 = no GPU). Cheap; may be called again. */
int gt_device_list(gt_device *list, int max);

/* ---- one row of label / value text, and a titled group of them (Device Info panels, --dump text) ---- */
typedef struct { char label[48]; char value[160]; } gt_kv;
typedef struct { char title[48]; int n; gt_kv kv[GT_MAX_KV]; } gt_section;

/* ---- static information (gt_open reads it once) ---- */
typedef struct {
    gt_device dev;
    int is_apu;                    /* 1 APU, 0 dGPU, GT_NA unknown */
    char family[32];               /* "GC 12.0.0" */
    char asic[32];                 /* "GFX1201" */
    char chip_class[16];           /* "GFX12" */
    char gfx_target[16];           /* "gfx1201" */
    int cu, se, sa_per_se, cu_per_sa, rb, rop;    /* GT_NA when unknown */
    int min_gpu_clk, max_gpu_clk, max_od_gpu_clk; /* MHz */
    int min_mem_clk, max_mem_clk, max_od_mem_clk; /* MHz */
    double peak_fp32_gflops, peak_fp16_gflops, peak_fill_gpixs, peak_mem_bw_gbs;   /* <0 = n/a */
    char vram_type[16];            /* "GDDR6", "HBM3", ... */
    int vram_bit_width;
    char vram_vendor[24];
    long long vram_total, vram_usable, vis_vram_total, gtt_total, gtt_usable;      /* bytes */
    int resizable_bar;             /* 1/0/GT_NA */
    int ecc;                       /* 1/0/GT_NA */
    int l1_kib_per_cu, gl1_kib_per_sa, l2_kib, l2_banks, l3_mib;   /* l3 = Infinity Cache */
    int power_cap_w, power_cap_min_w, power_cap_max_w, power_cap_default_w;
    int fan_max_rpm;
    int edge_crit, edge_emerg, junction_crit, junction_emerg, mem_crit, mem_emerg;  /* C */
    int pcie_dpm_min_gen, pcie_dpm_min_width, pcie_dpm_max_gen, pcie_dpm_max_width;
    int pcie_gpu_max_gen, pcie_gpu_max_width, pcie_sys_max_gen, pcie_sys_max_width;
    char vbios_name[64], vbios_pn[64], vbios_ver[64], vbios_date[32];
    int has_vcn, has_vcn_unified, has_vpe, has_npu;
    int supports_gpu_metrics;
    int smu_ip[3];                 /* MP0/MP1 IP version, {GT_NA,..} unknown */
    char drm_version[16];          /* "3.64.0" */
    char driver_version[64];       /* NVIDIA driver / kernel module version */
    /* the full Device Info, in amdgpu_top's order and wording (also what --dump prints):
       "Device", "GPU", "Shader", "Clock", "VRAM", "Memory usage", "Sensors", "PCIe", "Power Profiles",
       "Cache", "Hardware IP", "IP Discovery", "Video Decode", "Video Encode", "Firmware", "VBIOS" */
    int nsect;
    gt_section sect[GT_MAX_SECT];
} gt_info;

/* ---- gpu_metrics (every struct version; names = the kernel's / amdgpu_top's JSON keys) ---- */
enum {
    GT_M_TEMPERATURE_EDGE, GT_M_TEMPERATURE_HOTSPOT, GT_M_TEMPERATURE_MEM, GT_M_TEMPERATURE_GFX,
    GT_M_TEMPERATURE_SOC, GT_M_TEMPERATURE_VRGFX, GT_M_TEMPERATURE_VRSOC, GT_M_TEMPERATURE_VRMEM,
    GT_M_TEMPERATURE_SKIN,
    GT_M_AVERAGE_CPU_POWER, GT_M_AVERAGE_SOC_POWER, GT_M_AVERAGE_GFX_POWER, GT_M_AVERAGE_IPU_POWER,
    GT_M_AVERAGE_SYS_POWER,
    GT_M_AVERAGE_GFXCLK_FREQUENCY, GT_M_AVERAGE_SOCCLK_FREQUENCY, GT_M_AVERAGE_UCLK_FREQUENCY,
    GT_M_AVERAGE_FCLK_FREQUENCY, GT_M_AVERAGE_VCLK_FREQUENCY, GT_M_AVERAGE_DCLK_FREQUENCY,
    GT_M_AVERAGE_VCLK1_FREQUENCY, GT_M_AVERAGE_DCLK1_FREQUENCY, GT_M_AVERAGE_IPUCLK_FREQUENCY,
    GT_M_AVERAGE_MPIPU_FREQUENCY,
    GT_M_CURRENT_GFXCLK, GT_M_CURRENT_SOCCLK, GT_M_CURRENT_UCLK, GT_M_CURRENT_FCLK, GT_M_CURRENT_VCLK,
    GT_M_CURRENT_DCLK, GT_M_CURRENT_VCLK1, GT_M_CURRENT_DCLK1, GT_M_CURRENT_CORE_MAXFREQ,
    GT_M_CURRENT_GFX_MAXFREQ,
    GT_M_VOLTAGE_GFX, GT_M_VOLTAGE_SOC, GT_M_VOLTAGE_MEM, GT_M_FAN_PWM, GT_M_PCIE_LINK_WIDTH,
    GT_M_PCIE_LINK_SPEED,
    GT_M_AVERAGE_CPU_VOLTAGE, GT_M_AVERAGE_SOC_VOLTAGE, GT_M_AVERAGE_GFX_VOLTAGE,
    GT_M_AVERAGE_CPU_CURRENT, GT_M_AVERAGE_SOC_CURRENT, GT_M_AVERAGE_GFX_CURRENT,
    GT_M_AVERAGE_DRAM_READS, GT_M_AVERAGE_DRAM_WRITES, GT_M_AVERAGE_IPU_READS, GT_M_AVERAGE_IPU_WRITES,
    GT_M_STAPM_POWER_LIMIT, GT_M_CURRENT_STAPM_POWER_LIMIT,
    /* u32 values */
    GT_M_AVERAGE_SOCKET_POWER, GT_M_AVERAGE_APU_POWER, GT_M_AVERAGE_DGPU_POWER, GT_M_AVERAGE_ALL_CORE_POWER,
    /* not in amdgpu_top's JSON list but shown by its views */
    GT_M_AVERAGE_GFX_ACTIVITY, GT_M_AVERAGE_UMC_ACTIVITY, GT_M_AVERAGE_MM_ACTIVITY,
    GT_M_CURRENT_FAN_SPEED, GT_M_ENERGY_ACCUMULATOR, GT_M_THROTTLE_STATUS, GT_M_INDEP_THROTTLE_STATUS,
    GT_M_GFXCLK_LOCK_STATUS,
    GT_M_COUNT
};
enum {
    GT_MA_TEMPERATURE_CORE, GT_MA_TEMPERATURE_L3, GT_MA_CURRENT_CORECLK, GT_MA_CURRENT_L3CLK,
    GT_MA_AVERAGE_CORE_POWER, GT_MA_AVERAGE_TEMPERATURE_CORE, GT_MA_AVERAGE_TEMPERATURE_L3,
    GT_MA_AVERAGE_IPU_ACTIVITY,
    GT_MA_COUNT
};
#define GT_MA_LEN 16
typedef struct {
    int present;                   /* 0 = no gpu_metrics file / not decodable */
    int size, format_rev, content_rev;
    long long v[GT_M_COUNT];       /* GT_NA = not in this version or 0xFFFF / 0xFFFFFFFF ("not supported") */
                                   /* raw as the kernel reports: v2.x / v3.0 (APU) temperatures and activities are
                                      in 0.01 C / 0.01 %, v3.0 powers in mW, v1.x / v2.x powers in W */
    int na[GT_MA_COUNT];           /* number of valid entries in a[i] (0 = not in this version) */
    int a[GT_MA_COUNT][GT_MA_LEN];
    unsigned long long system_clock_counter;
    int nthr;                      /* decoded throttle reasons that are on now ("PPT0", "THM_HOTSPOT", ...) */
    char thr[GT_MAX_THR][16];
    /* NPU (gpu_metrics v3.0, Strix): GT_NA when absent */
    int npuclk, mpnpuclk, npu_power, npu_reads, npu_writes, n_npu_busy, npu_busy[8];
    int gfx_act_centi;             /* v3.0 (Strix): 1 = average_gfx_activity is in 0.01 % (SMU firmware <= 0x5d4600, set by gt_update) */
} gt_metrics;
const char *gt_metric_name(int i);       /* "temperature_edge", ... (GT_M_*) */
const char *gt_metric_unit(int i);       /* "C", "mW", "MHz", "mV", "mA", "%", "MB/s", "" */
/* a field as people read it (the same scaling as the Activity numbers): v2.x / v3.0 temperatures and activities in
   0.01 units are divided by 100 (v3.0 GFX activity only on old SMU firmware); *unit = the unit to print (v3.0 power: mW) */
double gt_metric_shown(const gt_metrics *m, int i, const char **unit);
/* s as the body of a JSON string (no quotes): \" \\ and control bytes escaped, invalid UTF-8 as \\ufffd; returns the length */
size_t gt_json_escape(char *out, size_t on, const char *s);
const char *gt_metric_array_name(int i); /* "temperature_core", ... (GT_MA_*) */
/* decode a gpu_metrics blob (exported for tests); returns 0 when the header is understood */
int gt_metrics_decode(const unsigned char *buf, size_t len, gt_metrics *m);

/* ---- GRBM / GRBM2 (percent of samples busy) ---- */
typedef struct { char name[40]; int pct; } gt_pc;

/* ---- one process (fdinfo); percent values are over the last interval ---- */
typedef struct {
    int pid;
    char name[32];
    long long vram_kib, gtt_kib, cpu_kib;   /* drm-memory-* (KiB) */
    int cpu;                       /* % of one CPU (process CPU time) */
    int gfx, compute, dma, dec, enc, media, vcn_jpeg, vpe, vcn_unified;  /* %; GT_NA where the GPU has no such engine */
    int is_kfd;                    /* has /dev/kfd open (ROCm) */
    int nvml_mem_only;             /* NVIDIA: only used memory is known */
} gt_proc;

/* ---- clock levels (pp_dpm_sclk / mclk / fclk / socclk / pcie) ---- */
typedef struct {
    char name[12];                 /* "sclk", "mclk", "fclk", "socclk", "pcie" */
    int n, cur;                    /* cur = index of the '*' level, GT_NA none */
    int mhz[GT_MAX_DPM];           /* pcie: gen in mhz[], width in lanes[] */
    int lanes[GT_MAX_DPM];
} gt_dpm;

/* ---- a full sample (gt_update fills it) ---- */
typedef struct {
    unsigned long long seq;        /* +1 per gt_update */
    long long t_ms;                /* monotonic time of the sample */
    int interval_ms;
    int idle_d3;                   /* dGPU left alone so it can sleep (amdgpu_top's to_d3hot) */
    /* activity (gpu_metrics, else *_busy_percent) */
    int gfx, umc, media;           /* %, GT_NA */
    /* GRBM / GRBM2; pc_ok = 0 when --no-pc, NVIDIA, or the registers cannot be read */
    int pc_ok, ngrbm, ngrbm2;
    gt_pc grbm[GT_MAX_PC], grbm2[GT_MAX_PC];
    /* memory (bytes) */
    long long vram_used, vram_total, vram_usable, vis_vram_used, vis_vram_total, gtt_used, gtt_total, gtt_usable;
    /* sensors */
    int sclk, mclk, fclk, socclk, fclk_min, fclk_max, socclk_min, socclk_max;   /* MHz */
    int vddgfx, vddnb;             /* mV */
    int edge, junction, memtemp;   /* C */
    double power_avg, power_input; /* W, <0 n/a */
    int power_cap;                 /* W */
    int fan_rpm, fan_max_rpm, fan_pct;
    int pcie_gen, pcie_width;      /* current link */
    char pci_power_state[8];       /* "D0", "D3hot" */
    char power_profile[24];
    int tctl;                      /* APU: CPU Tctl in C */
    long long mem_errors;          /* UMC RAS correctable+uncorrectable, GT_NA */
    long long pcie_tx_kbs, pcie_rx_kbs;   /* NVML PCIe throughput; AMD pcie_bw, GT_NA */
    int enc_util, dec_util;        /* NVML %, GT_NA */
    /* throttling (gpu_metrics or NVML clocks_event_reasons) */
    int nthr;
    char thr[GT_MAX_THR][24];
    gt_metrics metrics;
    /* processes and their sum ("Total fdinfo") */
    int nproc;
    gt_proc proc[GT_MAX_PROC];
    gt_proc total;
    /* clock levels */
    int ndpm;
    gt_dpm dpm[5];
} gt_stat;

/* ---- open / update / close ---- */
typedef struct {
    int no_pc;                     /* no GRBM register reads */
    int no_drop;                   /* never let a dGPU go idle (D3) (amdgpu_top AGT_NO_DROP) */
    int pc_samples;                /* register reads per interval (default 100) */
    int no_fdinfo;                 /* no per-process walk (/proc/<pid>/fdinfo): nproc = 0, total = 0. The idle-to-D3 rule
                                      needs that walk, so it is off too (as with no_drop). For light samplers (tray meters). */
} gt_opts;

typedef struct gt_dev gt_dev;
/* opens device `index` of gt_device_list (AMD: libdrm_amdgpu via dlopen; NVIDIA: NVML via dlopen;
   others: sysfs only). NULL + message in err when it cannot. */
gt_dev *gt_open(int index, const gt_opts *opts, char *err, size_t errlen);
void gt_close(gt_dev *d);
/* change gt_opts.no_fdinfo of an open device (takes effect with the next gt_finish / gt_update) */
void gt_set_no_fdinfo(gt_dev *d, int on);
const gt_info *gt_info_of(const gt_dev *d);
/* one sample: reads GRBM pc_samples times spread over interval_ms (or sleeps interval_ms with no_pc),
   then everything else; returns 0 on success. Blocks for about interval_ms. */
int gt_update(gt_dev *d, int interval_ms, gt_stat *out);
/* the same split up, for a caller that times the steps itself (all non-blocking):
   gt_pc_begin(); gt_pc_read() n times; gt_finish(elapsed_ms) */
void gt_pc_begin(gt_dev *d);
void gt_pc_read(gt_dev *d);
int gt_finish(gt_dev *d, int elapsed_ms, gt_stat *out);

/* ---- extra pages (text, already formatted the way amdgpu_top prints them) ----
   each returns 0 and appends to buf (NUL-terminated, truncated at len), or -1 with a one-line reason
   ("not available on this card") in buf. */
int gt_drm_info_text(gt_dev *d, char *buf, size_t len);     /* connectors, modes, planes (--drm-info) */
int gt_pp_table_text(gt_dev *d, char *buf, size_t len);     /* pp_table decode: Navi1x/2x/3x only */
int gt_xdna_text(char *buf, size_t len);                    /* /sys/class/accel NPU (--xdna) */
int gt_metrics_text(const gt_metrics *m, char *buf, size_t len);   /* --gpu-metrics, one "name: value unit" per line */

/* ---- JSON (amdgpu_top's shapes) ---- */
/* appends the "devices"[] element of `amdgpu_top -J` for one device to buf */
int gt_json_device(gt_dev *d, const gt_stat *s, int no_pc, char *buf, size_t len);
/* appends `amdgpu_top -d -J`'s object for one device (Info + static data) */
int gt_json_dump(gt_dev *d, const gt_stat *s, char *buf, size_t len);
int gt_json_metrics(const gt_metrics *m, char *buf, size_t len);

/* tests: write the AMDGPU_INFO answers of an open AMD device to file (the fixture's srs-amdgpu-info.txt) */
int gt_capture_info(gt_dev *d, const char *file);

/* small helpers shared with the GUI */
const char *gt_vendor_name(gt_vendor v);   /* "AMD", "NVIDIA", "Intel", "Other" */
const char *gt_pcie_gen_speed(int gen);    /* 1 -> "2.5 GT/s" ... 6 -> "64.0 GT/s" */

#endif
