/* gpu_top_int.h - GPU Top data layer internals shared by gpu_top.c, gpu_top_amd.c and gpu_top_nvml.c.
 * Not for the GUI or the CLI: they use gpu_top.h only.
 * The AMD parts are a C reimplementation of amdgpu_top (MIT licence, Copyright (c) 2023 Umio Yasuno).
 */
#ifndef GPU_TOP_INT_H
#define GPU_TOP_INT_H

#include "gpu_top.h"
#include <stdarg.h>

#define GT_PATH 512

/* the previous fdinfo totals of one process (engine time in ns, or cycles) */
typedef struct {
    int pid, seen;
    long long eng[12], cyc[12], tot[12];
    double cpu_s;
} gt_prev;

struct gt_amd;
struct gt_nv;

struct gt_dev {
    int index;
    gt_opts opts;
    gt_info info;
    char sys[GT_PATH];             /* the device's sysfs folder (test root applied, symlink resolved) */
    char hwmon[GT_PATH];           /* its hwmon folder, "" none */
    char port_gpu[GT_PATH];        /* the GPU's own PCIe port (amdgpu_top get_gpu_pcie_port_bus) */
    char port_sys[GT_PATH];        /* the system's PCIe port */
    int vega10_later, asic_raven, smc_fw_le_5d4600;
    struct gt_amd *amd;
    struct gt_nv *nv;
    /* GRBM / GRBM2 accumulators */
    int pc_n, pc_skip, pc_try, pc_broken;   /* pc_broken: the registers could not be read (amdgpu_top turns them off) */
    int acc1[32], acc2[32];
    int ngrbm, ngrbm2;
    const char *grbm_name[GT_MAX_PC], *grbm2_name[GT_MAX_PC];
    int grbm_bit[GT_MAX_PC], grbm2_bit[GT_MAX_PC];
    /* fdinfo */
    int nprev;
    gt_prev prev[GT_MAX_PROC];
    int to_d3, prev_all_idle;
    unsigned long long seq;
    int fixture_reads;             /* tests: number of fdinfo passes (file.next is used from the second on) */
    gt_stat st;
};

/* --- paths under the test root (SRS_TEST_GPU_ROOT) --- */
const char *gt_root(void);         /* "" normally */
int gt_testing(void);
/* root + printf(fmt); returns out */
char *gt_p(char *out, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int gt_read(const char *path, char *buf, size_t n);          /* whole small file, trailing newline cut; -1 */
int gt_read_ll(const char *path, long long *v);              /* decimal or 0x hex; -1 */
long gt_read_bin(const char *path, unsigned char *buf, size_t n);
int gt_readf(char *buf, size_t n, const char *dir, const char *name);   /* dir/name */
int gt_readf_ll(long long *v, const char *dir, const char *name);
void gt_cat(char *buf, size_t len, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void gt_kv_add(gt_section *s, const char *label, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
gt_section *gt_sect_new(gt_info *in, const char *title);
long long gt_now_ms(void);

/* pp_dpm_* parsing (amdgpu_top DpmClockRange) */
int gt_dpm_read(const char *sys, const char *file, gt_dpm *out);
int gt_dpm_range(const char *sys, const char *file, int *cur, int *min, int *max);
/* PCIe "max_link_speed"/"_width" style files of a folder */
int gt_link_read(const char *dir, int max, int *gen, int *width);

/* JSON writing */
typedef struct { char *b; size_t n, len; int first[32]; int depth; } gt_json;
void gj_init(gt_json *j, char *buf, size_t n);
void gj_obj(gt_json *j, const char *key);     /* key NULL inside an array */
void gj_arr(gt_json *j, const char *key);
void gj_end(gt_json *j, char close);           /* '}' or ']' */
void gj_str(gt_json *j, const char *key, const char *v);   /* v NULL -> null */
void gj_int(gt_json *j, const char *key, long long v);     /* GT_NA -> null */
void gj_u64(gt_json *j, const char *key, unsigned long long v);
void gj_num(gt_json *j, const char *key, double v);       /* <0 -> null */
void gj_bool(gt_json *j, const char *key, int v);
void gj_null(gt_json *j, const char *key);
void gj_vu(gt_json *j, const char *key, long long v, const char *unit);   /* {"value":v,"unit":u} or null */
void gj_raw(gt_json *j, const char *key, const char *raw);

/* --- AMD backend (gpu_top_amd.c) --- */
int gt_amd_open(gt_dev *d, char *err, size_t errlen);
void gt_amd_close(gt_dev *d);
int gt_amd_read_pc(gt_dev *d, unsigned *grbm, unsigned *grbm2);    /* 0 ok */
void gt_amd_update(gt_dev *d, gt_stat *s);                          /* ioctl sensors + memory */
int gt_amd_json_info(gt_dev *d, gt_json *j);                        /* the -d -J "Info" members */
int gt_amd_drm_info(gt_dev *d, char *buf, size_t len, gt_json *j);
int gt_amd_pp_table(gt_dev *d, char *buf, size_t len);
int gt_amd_capture(gt_dev *d, const char *file);                     /* tests: write the ioctl answers */

/* --- NVIDIA backend (gpu_top_nvml.c) --- */
int gt_nv_open(gt_dev *d, char *err, size_t errlen);
void gt_nv_close(gt_dev *d);
void gt_nv_update(gt_dev *d, gt_stat *s);
void gt_nv_sensors(gt_dev *d, gt_stat *s);                         /* after the sysfs sensors */
int gt_nv_name(const char *pci, char *name, size_t n);              /* marketing name, -1 when NVML is absent */

/* shared by the backends */
void gt_throttle_names(unsigned long long bits, int hotspot_c, int fix_hotspot, int *n, char out[][16], int max);
const char *gt_ppfeature_name(int bit);

#endif
