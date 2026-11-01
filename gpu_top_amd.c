/* gpu_top_amd.c - GPU Top, AMD back end: the AMDGPU_INFO queries (libdrm_amdgpu, loaded at run time with
 * dlopen; SRS has no link-time dependency on it), the Device Info sections, GRBM / GRBM2 register reads,
 * DRM connector info (libdrm, dlopen) and the pp_table decode.
 *
 * A C reimplementation of amdgpu_top (MIT licence, Copyright (c) 2023 Umio Yasuno,
 * https://github.com/Umio-Yasuno/amdgpu_top) and of the parts of its libdrm_amdgpu_sys crate it uses
 * (MIT, same author): ASIC tables, cache sizes, peak rates and the output order follow theirs.
 * Struct layouts follow the kernel's amdgpu_drm.h / drm_mode.h (MIT) and the SMU pptable headers.
 *
 * Tests (SRS_TEST_GPU_ROOT): nothing is opened; the ioctl answers come from <device>/srs-amdgpu-info.txt
 * (written by `sysreport-search gpu-top --capture-info`) and GRBM samples from <device>/srs-grbm.txt.
 */
#include "gpu_top_int.h"

#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* ------------------------------------------------------------------ kernel structs (amdgpu_drm.h) */

typedef struct {
    uint32_t device_id, chip_rev, external_rev, pci_rev, family, num_shader_engines, num_shader_arrays_per_engine,
             gpu_counter_freq;
    uint64_t max_engine_clock, max_memory_clock;
    uint32_t cu_active_number, cu_ao_mask, cu_bitmap[4][4], enabled_rb_pipes_mask, num_rb_pipes, num_hw_gfx_contexts,
             pcie_gen;
    uint64_t ids_flags, virtual_address_offset, virtual_address_max;
    uint32_t virtual_address_alignment, pte_fragment_size, gart_page_size, ce_ram_size, vram_type, vram_bit_width,
             vce_harvest_config, gc_double_offchip_lds_buf;
    uint64_t prim_buf_gpu_addr, pos_buf_gpu_addr, cntl_sb_buf_gpu_addr, param_buf_gpu_addr;
    uint32_t prim_buf_size, pos_buf_size, cntl_sb_buf_size, param_buf_size, wave_front_size,
             num_shader_visible_vgprs, num_cu_per_sh, num_tcc_blocks, gs_vgt_table_depth, gs_prim_buffer_depth,
             max_gs_waves_per_vgt, pcie_num_lanes, cu_ao_bitmap[4][4];
    uint64_t high_va_offset, high_va_max;
    uint32_t pa_sc_tile_steering_override;
    uint64_t tcc_disabled_mask, min_engine_clock, min_memory_clock;
    uint32_t tcp_cache_size, num_sqc_per_wgp, sqc_data_cache_size, sqc_inst_cache_size, gl1c_cache_size,
             gl2c_cache_size;
    uint64_t mall_size;
    uint32_t enabled_rb_pipes_mask_hi, shadow_size, shadow_alignment, csa_size, csa_alignment, userq_ip_mask, pad;
} amd_devinfo;

typedef struct { uint64_t total, usable, usage, max_alloc; } amd_heap;
typedef struct { amd_heap vram, vis, gtt; } amd_meminfo;
typedef struct { uint32_t major, minor; uint64_t caps; uint32_t ib_start, ib_size, rings, ipd_ver; } amd_hwip;
typedef struct { uint32_t valid, w, h, pixels, level, pad; } amd_codec;
typedef struct { uint8_t name[64], pn[64]; uint32_t version, pad; uint8_t ver[32], date[32]; } amd_vbios;
typedef struct {
    uint64_t ret_ptr;
    uint32_t ret_size, query;
    uint32_t a, b, c, d;           /* the query's union */
} amd_info_req;
#define AMD_IOCTL_INFO _IOW('d', 0x45, amd_info_req)

struct amd_drm_version { int major, minor, patch; size_t name_len; char *name; size_t date_len; char *date; size_t desc_len; char *desc; };
#define AMD_IOCTL_VERSION _IOWR('d', 0x00, struct amd_drm_version)

enum { INFO_HW_IP_INFO = 0x02, INFO_HW_IP_COUNT = 0x03, INFO_FW_VERSION = 0x0e, INFO_VRAM_USAGE = 0x10, INFO_GTT_USAGE = 0x11,
       INFO_VRAM_GTT = 0x14, INFO_READ_MMR_REG = 0x15, INFO_DEV_INFO = 0x16, INFO_VIS_VRAM_USAGE = 0x17, INFO_MEMORY = 0x19,
       INFO_VBIOS = 0x1B, INFO_SENSOR = 0x1D, INFO_VIDEO_CAPS = 0x21 };
#define GRBM_OFFSET 0x2004
#define GRBM2_OFFSET 0x2002

typedef void *amdev;
typedef struct {
    void *h;
    int (*init)(int, uint32_t *, uint32_t *, amdev *);
    int (*deinit)(amdev);
    int (*query_info)(amdev, unsigned, unsigned, void *);
    int (*hw_ip_count)(amdev, unsigned, uint32_t *);
    int (*hw_ip_info)(amdev, unsigned, unsigned, amd_hwip *);
    int (*fw_version)(amdev, unsigned, unsigned, unsigned, uint32_t *, uint32_t *);
    int (*sensor)(amdev, unsigned, unsigned, void *);
    int (*read_mm)(amdev, unsigned, unsigned, uint32_t, uint32_t, uint32_t *);
} amd_lib;

#define FW_MAX 32
struct gt_amd {
    amd_lib lib;
    amdev dev;
    int fd, fixture;
    int asic;                      /* ASIC_* below */
    amd_devinfo di;
    amd_meminfo mem;
    int nhw;
    struct { int type, count; amd_hwip ip; } hw[10];
    int nfw;
    struct { int type, inst, idx; uint32_t ver, feat; } fw[FW_MAX];
    int vcap_ok[2];
    amd_codec vcap[2][8];
    int vbios_ok;
    char vb_name[64], vb_pn[64], vb_ver[32], vb_date[32];
    int drm[3];
    /* IP discovery (die 0..) */
    int nip;
    struct { int die, hwid, ninst, maj, min, rev, harvest, nbase; unsigned base[8]; } ip[96];
    char gfx_target[16];
    int nprofiles;
    char profiles[12][20];
    unsigned ppfeature;
    int ppfeature_ok;
    /* tests: GRBM samples */
    int ngr, gri;
    int d3_mem_done;
    unsigned gr1[256], gr2[256];
};

/* ------------------------------------------------------------------ ASIC tables (libdrm_amdgpu_sys asic.rs) */

enum {
    ASIC_UNKNOWN, ASIC_TAHITI, ASIC_PITCAIRN, ASIC_VERDE, ASIC_OLAND, ASIC_HAINAN, ASIC_BONAIRE, ASIC_KAVERI, ASIC_KABINI,
    ASIC_HAWAII, ASIC_TONGA, ASIC_ICELAND, ASIC_CARRIZO, ASIC_FIJI, ASIC_STONEY, ASIC_POLARIS10, ASIC_POLARIS11,
    ASIC_POLARIS12, ASIC_VEGAM, ASIC_VEGA10, ASIC_VEGA12, ASIC_VEGA20, ASIC_RAVEN, ASIC_RAVEN2, ASIC_RENOIR,
    ASIC_ARCTURUS, ASIC_ALDEBARAN, ASIC_GFX940, ASIC_NAVI10, ASIC_NAVI12, ASIC_NAVI14, ASIC_GFX1013, ASIC_NAVI21,
    ASIC_NAVI22, ASIC_VANGOGH, ASIC_NAVI23, ASIC_NAVI24, ASIC_REMBRANDT, ASIC_GFX1036, ASIC_GFX1100, ASIC_GFX1101,
    ASIC_GFX1102, ASIC_GFX1103_R1, ASIC_GFX1103_R2, ASIC_GFX1103_R1X, ASIC_GFX1103_R2X, ASIC_GFX1150, ASIC_GFX1151,
    ASIC_GFX1152, ASIC_GFX1153, ASIC_GFX1200, ASIC_GFX1201
};
static const char *const ASIC_NAMES[] = {
    "Unknown", "Tahiti", "Pitcairn", "Verde", "Oland", "Hainan", "Bonaire", "Kaveri", "Kabini", "Hawaii", "Tonga",
    "Iceland", "Carrizo", "Fiji", "Stoney", "Polaris10", "Polaris11", "Polaris12", "VegaM", "Vega10", "Vega12",
    "Vega20", "Raven", "Raven2", "Renoir", "Arcturus/MI100", "Aldebaran/MI200", "GFX940/MI300", "Navi10", "Navi12",
    "Navi14", "GFX1013/Cyan Skillfish", "Sienna Cichlid/Navi21", "Navy Flounder/Navi22", "VanGogh",
    "Dimgrey Cavefish/Navi23", "Beige Goby/Navi24", "Yellow Carp/Rembrandt", "GFX1036/Raphael/Mendocino/Granite Ridge",
    "GFX1100/Navi31", "GFX1101/Navi32", "GFX1102/Navi33", "GFX1103_R1/Phoenix1", "GFX1103_R2/Phoenix2",
    "GFX1103_R1X/Hawk Point1", "GFX1103_R2X/Hawk Point2", "GFX1150/Strix Point", "GFX1151/Strix Halo", "GFX1152",
    "GFX1153", "GFX1200", "GFX1201"
};

static int asic_get(unsigned family, unsigned rev)
{
#define IN(a, b) (rev >= (a) && rev < (b))
    switch (family) {
    case 110: return IN(0x05, 0x14) ? ASIC_TAHITI : IN(0x15, 0x28) ? ASIC_PITCAIRN : IN(0x29, 0x3C) ? ASIC_VERDE :
                     IN(0x3C, 0x46) ? ASIC_OLAND : IN(0x46, 0xFF) ? ASIC_HAINAN : ASIC_UNKNOWN;
    case 120: return IN(0x14, 0x28) ? ASIC_BONAIRE : IN(0x28, 0x3C) ? ASIC_HAWAII : ASIC_UNKNOWN;
    case 125: return IN(0x01, 0x81) ? ASIC_KAVERI : IN(0x81, 0xFF) ? ASIC_KABINI : ASIC_UNKNOWN;
    case 130: return IN(0x01, 0x14) ? ASIC_ICELAND : IN(0x14, 0x28) ? ASIC_TONGA : IN(0x3C, 0x50) ? ASIC_FIJI :
                     IN(0x50, 0x5A) ? ASIC_POLARIS10 : IN(0x5A, 0x64) ? ASIC_POLARIS11 : IN(0x64, 0x6E) ? ASIC_POLARIS12 :
                     IN(0x6E, 0xFF) ? ASIC_VEGAM : ASIC_UNKNOWN;
    case 135: return ASIC_CARRIZO;   /* amdgpu_top has no CZ mapping (Unknown); Carrizo/Stoney are close enough */
    case 141: return IN(0x01, 0x14) ? ASIC_VEGA10 : IN(0x14, 0x28) ? ASIC_VEGA12 : IN(0x28, 0x32) ? ASIC_VEGA20 :
                     IN(0x32, 0x3C) ? ASIC_ARCTURUS : IN(0x3C, 0x46) ? ASIC_ALDEBARAN : IN(0x46, 0xFF) ? ASIC_GFX940 : ASIC_UNKNOWN;
    case 142: return IN(0x01, 0x81) ? ASIC_RAVEN : IN(0x81, 0x90) ? ASIC_RAVEN2 : IN(0x91, 0xFF) ? ASIC_RENOIR : ASIC_UNKNOWN;
    case 143: return IN(0x01, 0x0A) ? ASIC_NAVI10 : IN(0x0A, 0x14) ? ASIC_NAVI12 : IN(0x14, 0x28) ? ASIC_NAVI14 :
                     IN(0x28, 0x32) ? ASIC_NAVI21 : IN(0x32, 0x3C) ? ASIC_NAVI22 : IN(0x3C, 0x46) ? ASIC_NAVI23 :
                     IN(0x46, 0x50) ? ASIC_NAVI24 : IN(0x82, 0x86) ? ASIC_GFX1013 : ASIC_UNKNOWN;
    case 144: return ASIC_VANGOGH;
    case 145: return IN(0x01, 0x10) ? ASIC_GFX1100 : IN(0x10, 0x20) ? ASIC_GFX1102 : IN(0x20, 0xFF) ? ASIC_GFX1101 : ASIC_UNKNOWN;
    case 146: return ASIC_REMBRANDT;
    case 148: return IN(0x01, 0x80) ? ASIC_GFX1103_R1 : IN(0x80, 0xC0) ? ASIC_GFX1103_R2 : IN(0xC0, 0xF0) ? ASIC_GFX1103_R1X :
                     IN(0xF0, 0xFF) ? ASIC_GFX1103_R2X : ASIC_UNKNOWN;
    case 149: case 151: return ASIC_GFX1036;
    case 150: return IN(0x01, 0x40) ? ASIC_GFX1150 : IN(0x40, 0x50) ? ASIC_GFX1152 : IN(0x50, 0x80) ? ASIC_GFX1153 :
                     IN(0xC0, 0xFF) ? ASIC_GFX1151 : ASIC_UNKNOWN;
    case 152: return IN(0x40, 0x50) ? ASIC_GFX1200 : IN(0x50, 0xFF) ? ASIC_GFX1201 : ASIC_UNKNOWN;
    }
    return ASIC_UNKNOWN;
#undef IN
}

static const char *family_name(unsigned f)
{
    switch (f) {
    case 110: return "Southern Islands (SI)"; case 120: return "Sea Islands (CI)"; case 125: return "Kaveri (KV)";
    case 130: return "Volcanic Islands/Polaris (VI)"; case 135: return "Carrizo (CZ)"; case 141: return "Arctic Islands (AI)";
    case 142: return "Raven (RV)"; case 143: return "Navi (NV)"; case 144: return "VanGogh (VGH)"; case 145: return "GC 11.0.0";
    case 146: return "Yellow Carp (YC)"; case 148: return "GC 11.0.1"; case 149: return "GC 10.3.6"; case 151: return "GC 10.3.7";
    case 150: return "GC 11.5.0"; case 152: return "GC 12.0.0";
    }
    return "Unknown";
}

/* chip class as a number: GFX6 = 60 ... GFX10_3 = 103, GFX11_5 = 115, GFX12 = 120 */
static int chip_class(int a)
{
    return a >= ASIC_GFX1200 ? 120 : a >= ASIC_GFX1150 ? 115 : a >= ASIC_GFX1100 ? 110 : a >= ASIC_NAVI21 ? 103 :
           a >= ASIC_NAVI10 ? 100 : a >= ASIC_VEGA10 ? 90 : a >= ASIC_TONGA ? 80 : a >= ASIC_BONAIRE ? 70 :
           a >= ASIC_TAHITI ? 60 : 0;
}

static int rbplus_allowed(int a)
{
    int has = a == ASIC_STONEY || a >= ASIC_VEGA10;
    return has && (a == ASIC_STONEY || a == ASIC_VEGA12 || a == ASIC_RAVEN || a == ASIC_RAVEN2 || a == ASIC_RENOIR || a >= ASIC_NAVI21);
}

static unsigned l1_size(int a) { return a >= ASIC_GFX1100 ? 32 * 1024 : 16 * 1024; }
static unsigned gl1_size(int a) { return a >= ASIC_GFX1200 ? 0 : a >= ASIC_GFX1100 ? 256 * 1024 : a >= ASIC_NAVI10 ? 128 * 1024 : 0; }

static unsigned l2_per_block(int a)
{
    switch (a) {
    case ASIC_TAHITI: case ASIC_PITCAIRN: case ASIC_OLAND: case ASIC_HAWAII: case ASIC_KABINI: case ASIC_TONGA:
    case ASIC_STONEY: case ASIC_RAVEN2: return 64 * 1024;
    case ASIC_VERDE: case ASIC_HAINAN: case ASIC_BONAIRE: case ASIC_KAVERI: case ASIC_ICELAND: case ASIC_CARRIZO:
    case ASIC_FIJI: case ASIC_POLARIS12: case ASIC_VEGAM: return 128 * 1024;
    case ASIC_REMBRANDT: case ASIC_GFX1201: return 512 * 1024;
    }
    return 256 * 1024;
}

static unsigned l3_mb_per_channel(int a)
{
    switch (a) {
    case ASIC_NAVI21: case ASIC_NAVI22: return 8;
    case ASIC_NAVI23: case ASIC_NAVI24: case ASIC_GFX1100: case ASIC_GFX1101: case ASIC_GFX1102: case ASIC_GFX1200:
    case ASIC_GFX1201: return 4;
    case ASIC_GFX1151: return 2;
    }
    return 0;
}

/* kfd_device.c: gfx_target_version (major, minor, stepping) */
static int gfx_target(const struct gt_amd *a, int *ma, int *mi, int *st)
{
    static const struct { int asic, v[3]; } T[] = {
        { ASIC_KAVERI, { 7, 0, 0 } }, { ASIC_CARRIZO, { 8, 0, 1 } }, { ASIC_HAWAII, { 7, 0, 1 } }, { ASIC_TONGA, { 8, 0, 2 } },
        { ASIC_FIJI, { 8, 0, 3 } }, { ASIC_POLARIS10, { 8, 0, 3 } }, { ASIC_POLARIS11, { 8, 0, 3 } }, { ASIC_POLARIS12, { 8, 0, 3 } },
        { ASIC_VEGAM, { 8, 0, 3 } }, { ASIC_VEGA10, { 9, 0, 0 } }, { ASIC_RAVEN, { 9, 0, 2 } }, { ASIC_RAVEN2, { 9, 0, 2 } },
        { ASIC_RENOIR, { 9, 0, 12 } }, { ASIC_VEGA20, { 9, 0, 6 } }, { ASIC_ARCTURUS, { 9, 0, 8 } }, { ASIC_ALDEBARAN, { 9, 0, 10 } },
        { ASIC_NAVI10, { 10, 1, 0 } }, { ASIC_NAVI12, { 10, 1, 1 } }, { ASIC_NAVI14, { 10, 1, 2 } }, { ASIC_GFX1013, { 10, 1, 3 } },
        { ASIC_NAVI21, { 10, 3, 0 } }, { ASIC_NAVI22, { 10, 3, 1 } }, { ASIC_VANGOGH, { 10, 3, 3 } }, { ASIC_NAVI23, { 10, 3, 2 } },
        { ASIC_NAVI24, { 10, 3, 4 } }, { ASIC_REMBRANDT, { 10, 3, 5 } }, { ASIC_GFX1036, { 10, 3, 6 } }, { ASIC_GFX1100, { 11, 0, 0 } },
        { ASIC_GFX1101, { 11, 0, 1 } }, { ASIC_GFX1102, { 11, 0, 2 } }, { ASIC_GFX1103_R1, { 11, 0, 3 } }, { ASIC_GFX1103_R2, { 11, 0, 3 } },
        { ASIC_GFX1150, { 11, 5, 0 } }, { ASIC_GFX1151, { 11, 5, 1 } }, { ASIC_GFX1152, { 11, 5, 2 } }, { ASIC_GFX1153, { 11, 5, 3 } },
        { ASIC_GFX1200, { 12, 0, 0 } }, { ASIC_GFX1201, { 12, 0, 1 } },
    };
    if (a->asic == ASIC_GFX940) {
        *ma = 9; *mi = 4;
        *st = a->di.chip_rev > 1 ? 2 : (a->di.ids_flags & 1) ? 0 : 1;
        return 0;
    }
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++)
        if (T[i].asic == a->asic) { *ma = T[i].v[0]; *mi = T[i].v[1]; *st = T[i].v[2]; return 0; }
    return -1;
}

static const char *vram_type_name(unsigned t)
{
    static const char *n[] = { "Unknown", "GDDR1", "DDR2", "GDDR3", "GDDR4", "GDDR5", "HBM", "DDR3", "DDR4", "GDDR6", "DDR5",
                               "LPDDR4", "LPDDR5" };
    return t < sizeof n / sizeof n[0] ? n[t] : "Unknown";
}

static unsigned long long peak_bw_mbs(unsigned type, unsigned long long khz, unsigned width)
{
    unsigned ops = (type == 2 || type == 7 || type == 8 || type == 10 || type == 6 || type == 11) ? 2 : type == 5 ? 4 :
                   type == 12 ? 8 : type == 9 ? 16 : 1;
    return (khz / 1000) * ops * width / 8;
}

static const char *hwip_name(int t)
{
    static const char *n[] = { "GFX", "COMPUTE", "DMA", "UVD", "VCE", "UVD_ENC", "VCN_DEC", "VCN_ENC", "VCN_JPEG", "VPE" };
    return t >= 0 && t < 10 ? n[t] : "?";
}

static const char *fw_name(int t)
{
    switch (t) {
    case 0x1: return "VCE"; case 0x2: return "UVD"; case 0x3: return "GMC"; case 0x4: return "GFX_ME"; case 0x5: return "GFX_PFP";
    case 0x6: return "GFX_CE"; case 0x7: return "GFX_RLC"; case 0x8: return "GFX_MEC"; case 0xa: return "SMC"; case 0xb: return "SDMA";
    case 0xc: return "SOS"; case 0xd: return "ASD"; case 0xe: return "VCN"; case 0xf: return "GFX_RLC_RESTORE_LIST_CNTL";
    case 0x10: return "GFX_RLC_RESTORE_LIST_GPM_MEM"; case 0x11: return "GFX_RLC_RESTORE_LIST_SRM_MEM"; case 0x12: return "DMCU";
    case 0x13: return "TA"; case 0x14: return "DMCUB"; case 0x15: return "TOC"; case 0x16: return "CAP"; case 0x17: return "GFX_RLCP";
    case 0x18: return "GFX_RLCV"; case 0x19: return "MES_KIQ"; case 0x1a: return "MES"; case 0x1b: return "IMU"; case 0x1c: return "VPE";
    }
    return "?";
}

static const char *const CODECS[8] = { "MPEG2", "MPEG4", "VC1", "MPEG4_AVC", "HEVC", "JPEG", "VP9", "AV1" };

/* soc15_hw_ip.h */
static const char *hwid_name(int id, char *buf, size_t n)
{
    static const struct { int id; const char *n; } T[] = {
        { 1, "MP1" }, { 2, "MP2" }, { 3, "THM" }, { 4, "SMUIO" }, { 5, "FUSE" }, { 6, "CLKA" }, { 10, "PWR" }, { 11, "GC" },
        { 12, "UVD" }, { 13, "AUDIO_AZ" }, { 14, "ACP" }, { 15, "DCI" }, { 271, "DMU" }, { 16, "DCO" }, { 272, "DIO" },
        { 17, "XDMA" }, { 18, "DCEAZ" }, { 274, "DAZ" }, { 19, "SDPMUX" }, { 20, "NTB" }, { 21, "VPE" }, { 24, "IOHC" },
        { 28, "L2IMU" }, { 32, "VCE" }, { 34, "MMHUB" }, { 35, "ATHUB" }, { 36, "DBGU_NBIO" }, { 37, "DFX" }, { 38, "DBGU0" },
        { 39, "DBGU1" }, { 40, "OSSSYS" }, { 41, "HDP" }, { 42, "SDMA0" }, { 43, "SDMA1" }, { 44, "ISP" }, { 45, "DBGU_IO" },
        { 46, "DF" }, { 47, "CLKB" }, { 48, "FCH" }, { 49, "DFX_DAP" }, { 50, "L1IMU_PCIE" }, { 51, "L1IMU_NBIF" },
        { 52, "L1IMU_IOAGR" }, { 53, "L1IMU3" }, { 54, "L1IMU4" }, { 55, "L1IMU5" }, { 56, "L1IMU6" }, { 57, "L1IMU7" },
        { 58, "L1IMU8" }, { 59, "L1IMU9" }, { 60, "L1IMU10" }, { 61, "L1IMU11" }, { 62, "L1IMU12" }, { 63, "L1IMU13" },
        { 64, "L1IMU14" }, { 65, "L1IMU15" }, { 66, "WAFLC" }, { 67, "FCH_USB_PD" }, { 68, "SDMA2" }, { 69, "SDMA3" },
        { 70, "PCIE" }, { 80, "PCS" }, { 89, "DDCL" }, { 90, "SST" }, { 91, "LSDMA" }, { 100, "IOAGR" }, { 108, "NBIF" },
        { 124, "IOAPIC" }, { 128, "SYSTEMHUB" }, { 144, "NTBCCP" }, { 150, "UMC" }, { 168, "SATA" }, { 170, "USB" },
        { 176, "CCXSEC" }, { 200, "XGMI" }, { 216, "XGBE" }, { 255, "MP0" },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) if (T[i].id == id) return T[i].n;
    snprintf(buf, n, "Unknown(%d)", id);
    return buf;
}

static const char *const PPFEATURE[] = {
    "PP_SCLK_DPM_MASK", "PP_MCLK_DPM_MASK", "PP_PCIE_DPM_MASK", "PP_SCLK_DEEP_SLEEP_MASK", "PP_POWER_CONTAINMENT_MASK",
    "PP_UVD_HANDSHAKE_MASK", "PP_SMC_VOLTAGE_CONTROL_MASK", "PP_VBI_TIME_SUPPORT_MASK", "PP_ULV_MASK",
    "PP_ENABLE_GFX_CG_THRU_SMU", "PP_CLOCK_STRETCH_MASK", "PP_OD_FUZZY_FAN_CONTROL_MASK", "PP_SOCCLK_DPM_MASK",
    "PP_DCEFCLK_DPM_MASK", "PP_OVERDRIVE_MASK", "PP_GFXOFF_MASK", "PP_ACG_MASK", "PP_STUTTER_MODE", "PP_AVFS_MASK",
    "PP_GFX_DCS_MASK",
};
const char *gt_ppfeature_name(int bit) { return bit >= 0 && bit < 20 ? PPFEATURE[bit] : NULL; }

/* ------------------------------------------------------------------ the queries */

static int raw_info(struct gt_amd *a, uint32_t query, void *out, uint32_t size, uint32_t q0, uint32_t q1)
{
    amd_info_req r;
    memset(&r, 0, sizeof r);
    r.ret_ptr = (uint64_t)(uintptr_t)out;
    r.ret_size = size;
    r.query = query;
    r.a = q0;
    r.b = q1;
    return a->fd >= 0 && ioctl(a->fd, AMD_IOCTL_INFO, &r) == 0 ? 0 : -1;
}

static int lib_load(amd_lib *l)
{
    memset(l, 0, sizeof *l);
    if (!(l->h = dlopen("libdrm_amdgpu.so.1", RTLD_NOW | RTLD_LOCAL))) return -1;
    *(void **)&l->init = dlsym(l->h, "amdgpu_device_initialize");
    *(void **)&l->deinit = dlsym(l->h, "amdgpu_device_deinitialize");
    *(void **)&l->query_info = dlsym(l->h, "amdgpu_query_info");
    *(void **)&l->hw_ip_count = dlsym(l->h, "amdgpu_query_hw_ip_count");
    *(void **)&l->hw_ip_info = dlsym(l->h, "amdgpu_query_hw_ip_info");
    *(void **)&l->fw_version = dlsym(l->h, "amdgpu_query_firmware_version");
    *(void **)&l->sensor = dlsym(l->h, "amdgpu_query_sensor_info");
    *(void **)&l->read_mm = dlsym(l->h, "amdgpu_read_mm_registers");
    if (!l->init || !l->deinit || !l->query_info) { dlclose(l->h); l->h = NULL; return -1; }
    return 0;
}

/* AMDGPU_INFO_FW_* in amdgpu_top's order (app_device_info.rs get_fw_versions) */
static const int FW_LIST[] = { 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x12, 0x13,
                               0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c };

static int query_hw(struct gt_amd *a)
{
    uint32_t major = 0, minor = 0;
    if (a->lib.init(a->fd, &major, &minor, &a->dev)) return -1;
    if (a->lib.query_info(a->dev, INFO_DEV_INFO, sizeof a->di, &a->di)) return -1;
    a->lib.query_info(a->dev, INFO_MEMORY, sizeof a->mem, &a->mem);
    for (int t = 0; t < 10; t++) {
        uint32_t c = 0;
        if (!a->lib.hw_ip_count || a->lib.hw_ip_count(a->dev, (unsigned)t, &c) || c == 0) continue;
        a->hw[a->nhw].type = t;
        a->hw[a->nhw].count = (int)c;
        if (a->lib.hw_ip_info && a->lib.hw_ip_info(a->dev, (unsigned)t, 0, &a->hw[a->nhw].ip) == 0) a->nhw++;
    }
    if (a->lib.fw_version) {
        for (size_t i = 0; i < sizeof FW_LIST / sizeof FW_LIST[0] && a->nfw < FW_MAX; i++) {
            uint32_t v = 0, f = 0;
            if (a->lib.fw_version(a->dev, (unsigned)FW_LIST[i], 0, 0, &v, &f)) continue;
            a->fw[a->nfw].type = FW_LIST[i]; a->fw[a->nfw].inst = 0; a->fw[a->nfw].idx = 0;
            a->fw[a->nfw].ver = v; a->fw[a->nfw].feat = f; a->nfw++;
        }
        {   /* MEC2 */
            uint32_t v = 0, f = 0;
            if (a->nfw < FW_MAX && a->lib.fw_version(a->dev, 0x8, 0, 1, &v, &f) == 0) {
                a->fw[a->nfw].type = 0x8; a->fw[a->nfw].inst = 0; a->fw[a->nfw].idx = 1;
                a->fw[a->nfw].ver = v; a->fw[a->nfw].feat = f; a->nfw++;
            }
        }
    }
    for (int c = 0; c < 2; c++) a->vcap_ok[c] = raw_info(a, INFO_VIDEO_CAPS, a->vcap[c], sizeof a->vcap[c], (uint32_t)c, 0) == 0;
    {
        amd_vbios vb;
        memset(&vb, 0, sizeof vb);
        if (raw_info(a, INFO_VBIOS, &vb, sizeof vb, 3, 0) == 0) {
            a->vbios_ok = 1;
            snprintf(a->vb_name, sizeof a->vb_name, "%.63s", (char *)vb.name);
            snprintf(a->vb_pn, sizeof a->vb_pn, "%.63s", (char *)vb.pn);
            snprintf(a->vb_ver, sizeof a->vb_ver, "%.31s", (char *)vb.ver);
            snprintf(a->vb_date, sizeof a->vb_date, "%.31s", (char *)vb.date);
        }
    }
    {
        struct amd_drm_version v;
        memset(&v, 0, sizeof v);
        if (ioctl(a->fd, AMD_IOCTL_VERSION, &v) == 0) { a->drm[0] = v.major; a->drm[1] = v.minor; a->drm[2] = v.patch; }
    }
    return 0;
}

/* tests: the answers saved by gt_amd_capture */
static int load_fixture(struct gt_amd *a, const char *sys)
{
    char p[GT_PATH], line[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/srs-amdgpu-info.txt", sys);
    if (!(f = fopen(p, "r"))) return -1;
    while (fgets(line, sizeof line, f)) {
        char k[64];
        unsigned long long v[8] = { 0 };
        char *s;
        line[strcspn(line, "\n")] = 0;
        if (sscanf(line, "%63s", k) != 1 || k[0] == '#') continue;
        s = line + strlen(k);
        while (*s == ' ') s++;
        if (!strncmp(k, "vbios.", 6)) {
            char *dst = !strcmp(k + 6, "name") ? a->vb_name : !strcmp(k + 6, "pn") ? a->vb_pn : !strcmp(k + 6, "ver") ? a->vb_ver : a->vb_date;
            size_t dn = !strcmp(k + 6, "name") || !strcmp(k + 6, "pn") ? 64 : 32;
            snprintf(dst, dn, "%s", s);
            a->vbios_ok = 1;
            continue;
        }
        sscanf(s, "%lli %lli %lli %lli %lli %lli %lli %lli", (long long *)&v[0], (long long *)&v[1], (long long *)&v[2],
               (long long *)&v[3], (long long *)&v[4], (long long *)&v[5], (long long *)&v[6], (long long *)&v[7]);
#define DI(name) else if (!strcmp(k, "dev." #name)) a->di.name = v[0]
        if (0) {}
        DI(device_id); DI(chip_rev); DI(external_rev); DI(pci_rev); DI(family); DI(num_shader_engines);
        DI(num_shader_arrays_per_engine); DI(max_engine_clock); DI(max_memory_clock); DI(cu_active_number);
        DI(num_rb_pipes); DI(ids_flags); DI(vram_type); DI(vram_bit_width); DI(num_tcc_blocks); DI(tcc_disabled_mask);
        DI(min_engine_clock); DI(min_memory_clock); DI(tcp_cache_size); DI(gl1c_cache_size); DI(gl2c_cache_size);
        DI(mall_size); DI(num_cu_per_sh); DI(pcie_gen); DI(pcie_num_lanes); DI(wave_front_size);
#undef DI
        else if (!strcmp(k, "mem.vram")) { a->mem.vram.total = v[0]; a->mem.vram.usable = v[1]; a->mem.vram.usage = v[2]; a->mem.vram.max_alloc = v[3]; }
        else if (!strcmp(k, "mem.vis")) { a->mem.vis.total = v[0]; a->mem.vis.usable = v[1]; a->mem.vis.usage = v[2]; a->mem.vis.max_alloc = v[3]; }
        else if (!strcmp(k, "mem.gtt")) { a->mem.gtt.total = v[0]; a->mem.gtt.usable = v[1]; a->mem.gtt.usage = v[2]; a->mem.gtt.max_alloc = v[3]; }
        else if (!strcmp(k, "hwip") && a->nhw < 10) {
            a->hw[a->nhw].type = (int)v[0]; a->hw[a->nhw].count = (int)v[1];
            a->hw[a->nhw].ip.major = (uint32_t)v[2]; a->hw[a->nhw].ip.minor = (uint32_t)v[3]; a->hw[a->nhw].ip.rings = (uint32_t)v[4];
            a->nhw++;
        } else if (!strcmp(k, "fw") && a->nfw < FW_MAX) {
            a->fw[a->nfw].type = (int)v[0]; a->fw[a->nfw].inst = (int)v[1]; a->fw[a->nfw].idx = (int)v[2];
            a->fw[a->nfw].ver = (uint32_t)v[3]; a->fw[a->nfw].feat = (uint32_t)v[4]; a->nfw++;
        } else if (!strcmp(k, "vcap") && v[0] < 2 && v[1] < 8) {
            a->vcap_ok[v[0]] = 1;
            a->vcap[v[0]][v[1]].valid = (uint32_t)v[2]; a->vcap[v[0]][v[1]].w = (uint32_t)v[3]; a->vcap[v[0]][v[1]].h = (uint32_t)v[4];
        } else if (!strcmp(k, "drm")) { a->drm[0] = (int)v[0]; a->drm[1] = (int)v[1]; a->drm[2] = (int)v[2]; }
    }
    fclose(f);
    snprintf(p, sizeof p, "%s/srs-grbm.txt", sys);
    if ((f = fopen(p, "r"))) {
        unsigned g1, g2;
        while (a->ngr < 256 && fscanf(f, "%x %x", &g1, &g2) == 2) { a->gr1[a->ngr] = g1; a->gr2[a->ngr] = g2; a->ngr++; }
        fclose(f);
    }
    return 0;
}

int gt_amd_capture(gt_dev *d, const char *file)
{
    struct gt_amd *a = d->amd;
    FILE *f;
    if (!a || a->fixture) return -1;
    if (!(f = fopen(file, "w"))) return -1;
    fprintf(f, "# AMDGPU_INFO answers for GPU Top tests (sysreport-search gpu-top --capture-info)\n");
#define DI(name) fprintf(f, "dev." #name " %llu\n", (unsigned long long)a->di.name)
    DI(device_id); DI(chip_rev); DI(external_rev); DI(pci_rev); DI(family); DI(num_shader_engines);
    DI(num_shader_arrays_per_engine); DI(max_engine_clock); DI(max_memory_clock); DI(cu_active_number);
    DI(num_rb_pipes); DI(ids_flags); DI(vram_type); DI(vram_bit_width); DI(num_tcc_blocks); DI(tcc_disabled_mask);
    DI(min_engine_clock); DI(min_memory_clock); DI(tcp_cache_size); DI(gl1c_cache_size); DI(gl2c_cache_size);
    DI(mall_size); DI(num_cu_per_sh); DI(pcie_gen); DI(pcie_num_lanes); DI(wave_front_size);
#undef DI
    fprintf(f, "mem.vram %llu %llu %llu %llu\n", (unsigned long long)a->mem.vram.total, (unsigned long long)a->mem.vram.usable,
            (unsigned long long)a->mem.vram.usage, (unsigned long long)a->mem.vram.max_alloc);
    fprintf(f, "mem.vis %llu %llu %llu %llu\n", (unsigned long long)a->mem.vis.total, (unsigned long long)a->mem.vis.usable,
            (unsigned long long)a->mem.vis.usage, (unsigned long long)a->mem.vis.max_alloc);
    fprintf(f, "mem.gtt %llu %llu %llu %llu\n", (unsigned long long)a->mem.gtt.total, (unsigned long long)a->mem.gtt.usable,
            (unsigned long long)a->mem.gtt.usage, (unsigned long long)a->mem.gtt.max_alloc);
    for (int i = 0; i < a->nhw; i++)
        fprintf(f, "hwip %d %d %u %u %u\n", a->hw[i].type, a->hw[i].count, a->hw[i].ip.major, a->hw[i].ip.minor, a->hw[i].ip.rings);
    for (int i = 0; i < a->nfw; i++)
        fprintf(f, "fw %d %d %d %u %u\n", a->fw[i].type, a->fw[i].inst, a->fw[i].idx, a->fw[i].ver, a->fw[i].feat);
    for (int c = 0; c < 2; c++)
        if (a->vcap_ok[c])
            for (int k = 0; k < 8; k++) fprintf(f, "vcap %d %d %u %u %u\n", c, k, a->vcap[c][k].valid, a->vcap[c][k].w, a->vcap[c][k].h);
    if (a->vbios_ok)
        fprintf(f, "vbios.name %s\nvbios.pn %s\nvbios.ver %s\nvbios.date %s\n", a->vb_name, a->vb_pn, a->vb_ver, a->vb_date);
    fprintf(f, "drm %d %d %d\n", a->drm[0], a->drm[1], a->drm[2]);
    return fclose(f) ? -1 : 0;
}

/* ------------------------------------------------------------------ sysfs extras */

static void read_ip_discovery(gt_dev *d, struct gt_amd *a)
{
    char base[GT_PATH];
    for (int die = 0; die < 8; die++) {
        char dp[GT_PATH];
        int ids[128], nid = 0;
        DIR *dir;
        struct dirent *e;
        snprintf(dp, sizeof dp, "%s/ip_discovery/die/%d", d->sys, die);
        if (!(dir = opendir(dp))) break;
        while ((e = readdir(dir)) && nid < 128) if (isdigit((unsigned char)e->d_name[0])) ids[nid++] = atoi(e->d_name);
        closedir(dir);
        for (int i = 0; i < nid; i++) for (int j = i + 1; j < nid; j++) if (ids[j] < ids[i]) { int t = ids[i]; ids[i] = ids[j]; ids[j] = t; }
        for (int i = 0; i < nid && a->nip < 96; i++) {
            long long v;
            int ninst = 0;
            snprintf(base, sizeof base, "%s/%d", dp, ids[i]);
            for (int k = 0; k < 64; k++) {
                char ip[GT_PATH];
                snprintf(ip, sizeof ip, "%s/%d", base, k);
                if (gt_readf_ll(&v, ip, "major")) break;
                ninst++;
            }
            if (!ninst) continue;
            {
                char ip[GT_PATH], b[256];
                snprintf(ip, sizeof ip, "%s/0", base);
                a->ip[a->nip].die = die;
                a->ip[a->nip].hwid = ids[i];
                a->ip[a->nip].ninst = ninst;
                a->ip[a->nip].maj = gt_readf_ll(&v, ip, "major") ? 0 : (int)v;
                a->ip[a->nip].min = gt_readf_ll(&v, ip, "minor") ? 0 : (int)v;
                a->ip[a->nip].rev = gt_readf_ll(&v, ip, "revision") ? 0 : (int)v;
                a->ip[a->nip].harvest = gt_readf_ll(&v, ip, "harvest") ? 0 : (int)v;
                a->ip[a->nip].nbase = gt_readf_ll(&v, ip, "num_base_addresses") ? 0 : (int)v;
                if (gt_readf(b, sizeof b, ip, "base_addr") == 0) {
                    char *s = b, *e2;
                    int n = 0;
                    while (n < 8 && *s) { unsigned long x = strtoul(s, &e2, 16); if (e2 == s) break; a->ip[a->nip].base[n++] = (unsigned)x; s = e2; while (*s && isspace((unsigned char)*s)) s++; }
                }
                a->nip++;
            }
        }
    }
}

static int od_range(const char *s, const char *key, int *mn, int *mx)
{
    const char *l = strstr(s, key);
    if (!l) return -1;
    l += strlen(key);
    return sscanf(l, " %d%*[MHzhz] %d", mn, mx) == 2 ? 0 : -1;
}

/* gfx_target_version from the KFD topology (when the ASIC table has none) */
static void kfd_target(gt_dev *d, char *out, size_t n)
{
    char base[GT_PATH];
    const char *r = strrchr(d->info.dev.render, 'D');
    int minor = r ? atoi(r + 1) : -1;
    DIR *dir = opendir(gt_p(base, sizeof base, "/sys/class/kfd/kfd/topology/nodes"));
    struct dirent *e;
    while (dir && (e = readdir(dir))) {
        char np[GT_PATH], b[4096], *t;
        long long gv = -1;
        if (e->d_name[0] == '.') continue;
        snprintf(np, sizeof np, "%s/%s", base, e->d_name);
        if (gt_readf(b, sizeof b, np, "properties")) continue;
        if ((t = strstr(b, "drm_render_minor ")) && atoi(t + 17) == minor && (t = strstr(b, "gfx_target_version ")))
            gv = atoll(t + 19);
        if (gv > 0) { snprintf(out, n, "gfx%lld%lld%llx", gv / 10000, (gv / 100) % 100, gv % 100); break; }
    }
    if (dir) closedir(dir);
}

/* ------------------------------------------------------------------ open: Device Info */

static void fmt_pad(gt_section *s, const char *label, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void fmt_pad(gt_section *s, const char *label, const char *fmt, ...)
{
    char v[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(v, sizeof v, fmt, ap);
    va_end(ap);
    gt_kv_add(s, label, "%s", v);
}

int gt_amd_open(gt_dev *d, char *err, size_t errlen)
{
    struct gt_amd *a = calloc(1, sizeof *a);
    gt_info *in = &d->info;
    gt_section *s;
    char b[4096];
    int cc, maxcu, mincu, rop, cu_group, ma, mi, st;
    unsigned long long bw;
    if (!a) return -1;
    a->fd = -1;
    d->amd = a;
    if (gt_testing()) {
        a->fixture = 1;
        if (load_fixture(a, d->sys)) { if (err) snprintf(err, errlen, "test root: no srs-amdgpu-info.txt for %s", in->dev.pci); return -1; }
    } else {
        if (lib_load(&a->lib)) { if (err) snprintf(err, errlen, "libdrm_amdgpu.so.1 could not be loaded (install libdrm)"); return -1; }
        if (!in->dev.render[0] || (a->fd = open(in->dev.render, O_RDWR | O_CLOEXEC)) < 0) {
            if (err) snprintf(err, errlen, "cannot open %s", in->dev.render[0] ? in->dev.render : "the render node");
            return -1;
        }
        if (query_hw(a)) { if (err) snprintf(err, errlen, "AMDGPU_INFO queries failed on %s", in->dev.render); return -1; }
    }
    a->asic = asic_get(a->di.family, a->di.external_rev);
    cc = chip_class(a->asic);
    d->vega10_later = a->asic >= ASIC_VEGA10;
    d->asic_raven = a->asic == ASIC_RAVEN || a->asic == ASIC_RAVEN2;
    in->is_apu = (a->di.ids_flags & 1) ? 1 : 0;
    snprintf(in->family, sizeof in->family, "%s", family_name(a->di.family));
    snprintf(in->asic, sizeof in->asic, "%s", ASIC_NAMES[a->asic]);
    if (cc == 103) snprintf(in->chip_class, sizeof in->chip_class, "GFX10_3");
    else if (cc == 115) snprintf(in->chip_class, sizeof in->chip_class, "GFX11_5");
    else if (cc) snprintf(in->chip_class, sizeof in->chip_class, "GFX%d", cc / 10);
    else snprintf(in->chip_class, sizeof in->chip_class, "Unknown");
    if (gfx_target(a, &ma, &mi, &st) == 0) snprintf(in->gfx_target, sizeof in->gfx_target, "gfx%d%d%x", ma, mi, st);
    else kfd_target(d, in->gfx_target, sizeof in->gfx_target);
    in->cu = (int)a->di.cu_active_number;
    in->se = (int)a->di.num_shader_engines;
    in->sa_per_se = (int)a->di.num_shader_arrays_per_engine;
    cu_group = cc >= 100 ? 2 : 1;
    {
        unsigned max_sa = a->di.num_shader_engines * a->di.num_shader_arrays_per_engine;
        maxcu = max_sa ? (int)((a->di.cu_active_number + max_sa * cu_group - 1) / (max_sa * cu_group) * cu_group) : 0;
        mincu = max_sa ? (int)(a->di.cu_active_number / (max_sa * cu_group) * cu_group) : 0;
    }
    in->cu_per_sa = maxcu;
    in->rb = (int)a->di.num_rb_pipes;
    rop = (int)a->di.num_rb_pipes * (rbplus_allowed(a->asic) ? 8 : 4);
    in->rop = rop;
    /* clocks: pp_dpm_sclk / mclk, else the info's max (kHz) */
    {
        int c, mn, mx;
        if (gt_dpm_range(d->sys, "pp_dpm_sclk", &c, &mn, &mx) == 0) { in->min_gpu_clk = mn; in->max_gpu_clk = mx; }
        else { in->min_gpu_clk = 0; in->max_gpu_clk = (int)(a->di.max_engine_clock / 1000); }
        if (gt_dpm_range(d->sys, "pp_dpm_mclk", &c, &mn, &mx) == 0) { in->min_mem_clk = mn; in->max_mem_clk = mx; }
        else { in->min_mem_clk = 0; in->max_mem_clk = (int)(a->di.max_memory_clock / 1000); }
    }
    if (!in->is_apu && gt_readf(b, sizeof b, d->sys, "pp_od_clk_voltage") == 0) {
        int mn, mx;
        if (cc == 120 && od_range(b, "SCLK_OFFSET:", &mn, &mx) == 0) in->max_od_gpu_clk = mx + in->max_gpu_clk;
        else if (cc >= 100 && cc < 120 && od_range(b, "\nSCLK:", &mn, &mx) == 0) in->max_od_gpu_clk = mx;
        if (cc >= 100 && od_range(b, "\nMCLK:", &mn, &mx) == 0) in->max_od_mem_clk = mx;
    }
    in->peak_fp32_gflops = (double)(a->di.cu_active_number * (cc >= 110 ? 128u : 64u) * 2u * (unsigned)(a->di.max_engine_clock / 1000) / 1000u);
    in->peak_fp16_gflops = a->asic >= ASIC_VEGA10 ? in->peak_fp32_gflops * 2 : in->peak_fp32_gflops;
    in->peak_fill_gpixs = (double)(rop * in->max_gpu_clk / 1000);
    bw = peak_bw_mbs(a->di.vram_type, a->di.max_memory_clock, a->di.vram_bit_width);
    in->peak_mem_bw_gbs = (double)(bw / 1000);
    snprintf(in->vram_type, sizeof in->vram_type, "%s", vram_type_name(a->di.vram_type));
    in->vram_bit_width = (int)a->di.vram_bit_width;
    if (gt_readf(b, sizeof b, d->sys, "mem_info_vram_vendor") == 0) snprintf(in->vram_vendor, sizeof in->vram_vendor, "%s", b);
    in->vram_total = (long long)a->mem.vram.total;
    in->vram_usable = (long long)a->mem.vram.usable;
    in->vis_vram_total = (long long)a->mem.vis.total;
    in->gtt_total = (long long)a->mem.gtt.total;
    in->gtt_usable = (long long)a->mem.gtt.usable;
    in->resizable_bar = a->mem.vram.total * 9 / 10 <= a->mem.vis.total;
    {
        char r[GT_PATH];
        snprintf(r, sizeof r, "%s/ras/umc_err_count", d->sys);
        in->ecc = access(r, R_OK) == 0;
    }
    {   /* caches: from the firmware on Strix Halo and RDNA 4, else from the ASIC tables */
        int fw = a->asic == ASIC_GFX1151 || a->asic >= ASIC_GFX1200;
        unsigned tcc = a->di.num_tcc_blocks - (unsigned)__builtin_popcountll(a->di.tcc_disabled_mask);
        in->l2_banks = (int)tcc;
        in->l1_kib_per_cu = (int)((fw ? a->di.tcp_cache_size << 10 : l1_size(a->asic)) >> 10);
        in->gl1_kib_per_sa = (int)((fw ? a->di.gl1c_cache_size << 10 : gl1_size(a->asic)) >> 10);
        in->l2_kib = (int)((fw ? a->di.gl2c_cache_size << 10 : tcc * l2_per_block(a->asic)) >> 10);
        in->l3_mib = fw ? (int)(a->di.mall_size >> 20) : (int)(tcc * l3_mb_per_channel(a->asic));
    }
    {
        char lv[GT_PATH];
        if (gt_readf(lv, sizeof lv, d->sys, "vbios_version") == 0 && !a->vbios_ok) snprintf(a->vb_ver, sizeof a->vb_ver, "%.31s", lv);
    }
    snprintf(in->vbios_name, sizeof in->vbios_name, "%s", a->vb_name);
    snprintf(in->vbios_pn, sizeof in->vbios_pn, "%s", a->vb_pn);
    snprintf(in->vbios_ver, sizeof in->vbios_ver, "%s", a->vb_ver);
    snprintf(in->vbios_date, sizeof in->vbios_date, "%s", a->vb_date);
    in->has_vcn = in->has_vcn_unified = in->has_vpe = 0;
    for (int i = 0; i < a->nhw; i++) {
        if (a->hw[i].type == 6) in->has_vcn = 1;
        if (a->hw[i].type == 7 && a->hw[i].ip.major >= 4) in->has_vcn_unified = 1;
        if (a->hw[i].type == 9) in->has_vpe = 1;
    }
    read_ip_discovery(d, a);
    for (int i = 0; i < a->nip; i++) if (a->ip[i].die == 0 && a->ip[i].hwid == 21) in->has_vpe = 1;
    for (int i = 0; i < a->nip; i++)
        if (a->ip[i].die == 0 && (a->ip[i].hwid == 1 || a->ip[i].hwid == 255)) {
            in->smu_ip[0] = a->ip[i].maj; in->smu_ip[1] = a->ip[i].min; in->smu_ip[2] = a->ip[i].rev;
            break;
        }
    in->has_npu = in->is_apu && (a->asic == ASIC_GFX1103_R1 || a->asic == ASIC_GFX1103_R1X || a->asic >= ASIC_GFX1150);
    {
        char gm[GT_PATH];
        snprintf(gm, sizeof gm, "%s/gpu_metrics", d->sys);
        in->supports_gpu_metrics = access(gm, R_OK) == 0;
    }
    for (int i = 0; i < a->nfw; i++) if (a->fw[i].type == 0xa) d->smc_fw_le_5d4600 = a->fw[i].ver <= 0x5d4600;
    if (a->drm[0] || a->drm[1]) snprintf(in->drm_version, sizeof in->drm_version, "%d.%d.%d", a->drm[0], a->drm[1], a->drm[2]);
    /* power profiles the card offers */
    if (gt_readf(b, sizeof b, d->sys, "pp_power_profile_mode") == 0) {
        static const char *names[] = { "BOOTUP_DEFAULT", "3D_FULL_SCREEN", "POWER_SAVING", "VIDEO", "VR", "COMPUTE",
                                       "CUSTOM", "WINDOW_3D", "CAPPED", "UNCAPPED" };
        char *l, *save = NULL;
        for (l = strtok_r(b, "\n", &save); l && a->nprofiles < 12; l = strtok_r(NULL, "\n", &save)) {
            char *t = l;
            int id;
            while (*t == ' ') t++;
            { char *q = t; while (isdigit((unsigned char)*q)) q++; if (q == t || *q != ' ') continue; }
            id = atoi(t);
            if (id >= 0 && id <= 9) snprintf(a->profiles[a->nprofiles++], 20, "%s", names[id]);
        }
    }
    {
        char p[GT_PATH];
        long long v;
        if (gt_read_ll(gt_p(p, sizeof p, "/sys/module/amdgpu/parameters/ppfeaturemask"), &v) == 0) { a->ppfeature = (unsigned)v; a->ppfeature_ok = 1; }
    }
    /* PCIe links (dGPU) */
    if (!in->is_apu) {
        gt_dpm p;
        if (d->vega10_later) {
            if (gt_dpm_read(d->sys, "pp_dpm_pcie", &p) == 0) {
                in->pcie_dpm_min_gen = p.mhz[0]; in->pcie_dpm_min_width = p.lanes[0];
                in->pcie_dpm_max_gen = p.mhz[p.n - 1]; in->pcie_dpm_max_width = p.lanes[p.n - 1];
                if (p.mhz[0] > p.mhz[p.n - 1]) {
                    in->pcie_dpm_min_gen = p.mhz[p.n - 1]; in->pcie_dpm_min_width = p.lanes[p.n - 1];
                    in->pcie_dpm_max_gen = p.mhz[0]; in->pcie_dpm_max_width = p.lanes[0];
                }
            }
            gt_link_read(d->port_gpu, 1, &in->pcie_gpu_max_gen, &in->pcie_gpu_max_width);
            gt_link_read(d->port_sys, 1, &in->pcie_sys_max_gen, &in->pcie_sys_max_width);
        } else {
            if (gt_dpm_read(d->sys, "pp_dpm_pcie", &p) == 0) { in->pcie_dpm_min_gen = p.mhz[0]; in->pcie_dpm_min_width = p.lanes[0]; }
            gt_link_read(d->sys, 1, &in->pcie_dpm_max_gen, &in->pcie_dpm_max_width);
            in->pcie_gpu_max_gen = in->pcie_dpm_max_gen; in->pcie_gpu_max_width = in->pcie_dpm_max_width;
            gt_link_read(d->port_sys, 1, &in->pcie_sys_max_gen, &in->pcie_sys_max_width);
        }
    }

    /* ---- the Device Info sections, in amdgpu_top --dump's order ---- */
    s = gt_sect_new(in, "Device");
    fmt_pad(s, "Device Name", "[%s]", in->dev.name);
    fmt_pad(s, "PCI (domain:bus:dev.func)", "%s", in->dev.pci);
    fmt_pad(s, "DeviceID.RevID", "0x%X.0x%X", a->di.device_id, a->di.pci_rev);
    if (in->gfx_target[0]) fmt_pad(s, "gfx_target_version", "%s", in->gfx_target);
    s = gt_sect_new(in, "GPU");
    fmt_pad(s, "GPU Type", "%s", in->is_apu ? "APU" : "dGPU");
    fmt_pad(s, "Family", "%s", in->family);
    fmt_pad(s, "ASIC Name", "%s", in->asic);
    fmt_pad(s, "Chip Class", "%s", in->chip_class);
    s = gt_sect_new(in, "Shader");
    fmt_pad(s, "Shader Engine (SE)", "%3d", in->se);
    fmt_pad(s, "Shader Array (SA/SH) per SE", "%3d", in->sa_per_se);
    if (maxcu != mincu) { fmt_pad(s, "CU per SA[0]", "%3d", maxcu); fmt_pad(s, "CU per SA[1]", "%3d", mincu); }
    else fmt_pad(s, "CU per SA", "%3d", maxcu);
    fmt_pad(s, "Total Compute Unit", "%3d", in->cu);
    fmt_pad(s, rbplus_allowed(a->asic) ? "RenderBackendPlus (RB+)" : "RenderBackend (RB)", "%3d (%d ROPs)", in->rb, rop);
    fmt_pad(s, "Peak Pixel Fill-Rate", "%3d GP/s", rop * in->max_gpu_clk / 1000);
    s = gt_sect_new(in, "Clock");
    fmt_pad(s, "GPU Clock", "%d-%d MHz", in->min_gpu_clk, in->max_gpu_clk);
    if (in->max_od_gpu_clk != GT_NA) fmt_pad(s, "GPU Clock (OD Max)", "%d MHz", in->max_od_gpu_clk);
    fmt_pad(s, "Peak FP32", "%.0f GFLOPS", in->peak_fp32_gflops);
    fmt_pad(s, "Peak FP16", "%.0f GFLOPS", in->peak_fp16_gflops);
    s = gt_sect_new(in, "VRAM");
    fmt_pad(s, "VRAM Type", "%s", in->vram_type);
    fmt_pad(s, "VRAM Bit Width", "%d-bit", in->vram_bit_width);
    if (in->vram_vendor[0]) fmt_pad(s, "VRAM Vendor", "%s", in->vram_vendor);
    fmt_pad(s, "Memory Clock", "%d-%d MHz", in->min_mem_clk, in->max_mem_clk);
    if (in->max_od_mem_clk != GT_NA) fmt_pad(s, "Memory Clock (OD Max)", "%d MHz", in->max_od_mem_clk);
    fmt_pad(s, "Peak Memory BW", "%.0f GB/s", in->peak_mem_bw_gbs);
    fmt_pad(s, "ResizableBAR", "%s", in->resizable_bar ? "Enabled" : "Disabled");
    fmt_pad(s, "ECC Memory", "%s", in->ecc ? "supported" : "not supported");
    s = gt_sect_new(in, "Memory usage");
    fmt_pad(s, "VRAM", "usage %5llu MiB, total %5llu MiB (usable %5llu MiB)", (unsigned long long)(a->mem.vram.usage >> 20),
            (unsigned long long)(a->mem.vram.total >> 20), (unsigned long long)(a->mem.vram.usable >> 20));
    fmt_pad(s, "CPU-Visible VRAM", "usage %5llu MiB, total %5llu MiB (usable %5llu MiB)", (unsigned long long)(a->mem.vis.usage >> 20),
            (unsigned long long)(a->mem.vis.total >> 20), (unsigned long long)(a->mem.vis.usable >> 20));
    fmt_pad(s, "GTT", "usage %5llu MiB, total %5llu MiB (usable %5llu MiB)", (unsigned long long)(a->mem.gtt.usage >> 20),
            (unsigned long long)(a->mem.gtt.total >> 20), (unsigned long long)(a->mem.gtt.usable >> 20));
    /* the Sensors section is added by gt_amd_sensor_section after the first sample (gpu_top.c adds it) */
    s = gt_sect_new(in, "Power Profiles");
    for (int i = 0; i < a->nprofiles; i++) fmt_pad(s, i ? "" : "Supported Power Profiles", "%s", a->profiles[i]);
    s = gt_sect_new(in, "Cache");
    fmt_pad(s, "L1 Cache (per CU)", "%4d KiB", in->l1_kib_per_cu);
    if (in->gl1_kib_per_sa > 0) fmt_pad(s, "GL1 Cache (per SA/SH)", "%4d KiB", in->gl1_kib_per_sa);
    fmt_pad(s, "L2 Cache", "%4d KiB (%d Banks)", in->l2_kib, in->l2_banks);
    if (in->l3_mib > 0) fmt_pad(s, "L3 Cache", "%4d MiB", in->l3_mib);
    s = gt_sect_new(in, "Hardware IP");
    for (int i = 0; i < a->nhw; i++)
        fmt_pad(s, hwip_name(a->hw[i].type), "count: %d, ver: %2u.%u, queues: %d", a->hw[i].count, a->hw[i].ip.major,
                a->hw[i].ip.minor, __builtin_popcount(a->hw[i].ip.rings));
    if (a->nip) {
        s = gt_sect_new(in, "IP Discovery");
        for (int i = 0; i < a->nip; i++) {
            char nb[24], lab[48];
            snprintf(lab, sizeof lab, "%s%s", a->ip[i].die ? "die1 " : "", hwid_name(a->ip[i].hwid, nb, sizeof nb));
            fmt_pad(s, lab, "num: %d, ver: %3d.%d.%d", a->ip[i].ninst, a->ip[i].maj, a->ip[i].min, a->ip[i].rev);
        }
    }
    s = gt_sect_new(in, "Firmware");
    for (int i = 0; i < a->nfw; i++) {
        if (!a->fw[i].ver) continue;
        char hx[16];
        snprintf(hx, sizeof hx, "0x%X", a->fw[i].ver);
        fmt_pad(s, a->fw[i].type == 0x8 && a->fw[i].idx == 1 ? "GFX_MEC2" : fw_name(a->fw[i].type), "feature: %3u, ver: %10s",
                a->fw[i].feat, hx);
    }
    if (a->vcap_ok[0] && a->vcap_ok[1]) {
        s = gt_sect_new(in, "Video Caps");
        for (int k = 0; k < 8; k++) {
            char dc[32], ec[32];
            if (a->vcap[0][k].valid) snprintf(dc, sizeof dc, "%ux%u", a->vcap[0][k].w, a->vcap[0][k].h); else snprintf(dc, sizeof dc, "N/A");
            if (a->vcap[1][k].valid) snprintf(ec, sizeof ec, "%ux%u", a->vcap[1][k].w, a->vcap[1][k].h); else snprintf(ec, sizeof ec, "N/A");
            fmt_pad(s, CODECS[k], "%12s (Decode), %12s (Encode)", dc, ec);
        }
    }
    if (a->vbios_ok) {
        s = gt_sect_new(in, "VBIOS");
        fmt_pad(s, "name", "[%s]", a->vb_name);
        fmt_pad(s, "pn", "[%s]", a->vb_pn);
        fmt_pad(s, "ver_str", "[%s]", a->vb_ver);
        fmt_pad(s, "date", "[%s]", a->vb_date);
    }
    if (a->ppfeature_ok) {
        s = gt_sect_new(in, "pp_feature_mask");
        for (int i = 0; i < 20; i++) if ((a->ppfeature >> i) & 1) fmt_pad(s, "", "%s", PPFEATURE[i]);
    }
    return 0;
}

void gt_amd_close(gt_dev *d)
{
    struct gt_amd *a = d->amd;
    if (!a) return;
    if (a->dev && a->lib.deinit) a->lib.deinit(a->dev);
    if (a->fd >= 0) close(a->fd);
    if (a->lib.h) dlclose(a->lib.h);
    free(a);
    d->amd = NULL;
}

int gt_amd_read_pc(gt_dev *d, unsigned *grbm, unsigned *grbm2)
{
    struct gt_amd *a = d->amd;
    uint32_t v1 = 0, v2 = 0;
    if (a->fixture) {
        if (!a->ngr) return -1;
        *grbm = a->gr1[a->gri % a->ngr];
        *grbm2 = a->gr2[a->gri % a->ngr];
        a->gri++;
        return 0;
    }
    if (!a->lib.read_mm || !a->dev) return -1;
    if (a->lib.read_mm(a->dev, GRBM_OFFSET, 1, 0xffffffff, 0, &v1)) return -1;
    if (a->lib.read_mm(a->dev, GRBM2_OFFSET, 1, 0xffffffff, 0, &v2)) return -1;
    *grbm = v1;
    *grbm2 = v2;
    return 0;
}

void gt_amd_update(gt_dev *d, gt_stat *s)
{
    struct gt_amd *a = d->amd;
    s->vram_total = (long long)a->mem.vram.total;
    s->vis_vram_total = (long long)a->mem.vis.total;
    s->gtt_total = (long long)a->mem.gtt.total;
    s->vram_usable = (long long)a->mem.vram.usable;
    s->gtt_usable = (long long)a->mem.gtt.usable;
    if (a->fixture) {
        long long v;
        s->vram_used = gt_readf_ll(&v, d->sys, "mem_info_vram_used") ? (long long)a->mem.vram.usage : v;
        s->vis_vram_used = gt_readf_ll(&v, d->sys, "mem_info_vis_vram_used") ? (long long)a->mem.vis.usage : v;
        s->gtt_used = gt_readf_ll(&v, d->sys, "mem_info_gtt_used") ? (long long)a->mem.gtt.usage : v;
        return;   /* sclk / mclk / voltages: hwmon in gpu_top.c */
    }
    /* amdgpu_top: one memory update when the dGPU goes idle, then no ioctl at all (each one wakes the GPU) */
    if (d->to_d3 && a->d3_mem_done) return;
    a->d3_mem_done = d->to_d3;
    {
        uint64_t u = 0;
        uint64_t vg[3] = { 0, 0, 0 };
        if (a->lib.query_info(a->dev, INFO_VRAM_USAGE, sizeof u, &u) == 0) s->vram_used = (long long)u;
        if (a->lib.query_info(a->dev, INFO_VIS_VRAM_USAGE, sizeof u, &u) == 0) s->vis_vram_used = (long long)u;
        if (a->lib.query_info(a->dev, INFO_GTT_USAGE, sizeof u, &u) == 0) s->gtt_used = (long long)u;
        if (a->lib.query_info(a->dev, INFO_VRAM_GTT, sizeof vg, vg) == 0) { s->vram_usable = (long long)vg[0]; s->gtt_usable = (long long)vg[2]; }
    }
    if (d->to_d3 || !a->lib.sensor) return;
    {
        uint32_t v;
        if (a->lib.sensor(a->dev, 1, sizeof v, &v) == 0) s->sclk = (int)v;
        if (a->lib.sensor(a->dev, 2, sizeof v, &v) == 0) s->mclk = (int)v;
        if (a->lib.sensor(a->dev, 6, sizeof v, &v) == 0) s->vddnb = (int)v;
        if (a->lib.sensor(a->dev, 7, sizeof v, &v) == 0) s->vddgfx = (int)v;
    }
}

/* ------------------------------------------------------------------ -d -J: the "Info" members */

int gt_amd_json_info(gt_dev *d, gt_json *j)
{
    struct gt_amd *a = d->amd;
    gt_info *in = &d->info;
    int maxcu = in->cu_per_sa, mincu;
    unsigned max_sa = a->di.num_shader_engines * a->di.num_shader_arrays_per_engine;
    int cu_group = chip_class(a->asic) >= 100 ? 2 : 1;
    char nb[24];
    mincu = max_sa ? (int)(a->di.cu_active_number / (max_sa * cu_group) * cu_group) : 0;
    if (a->drm[0] || a->drm[1]) {
        gj_obj(j, "drm_version");
        gj_int(j, "major", a->drm[0]); gj_int(j, "minor", a->drm[1]); gj_int(j, "patchlevel", a->drm[2]);
        gj_end(j, '}');
    } else gj_null(j, "drm_version");
    {   /* amdgpu_top get_rocm_version: /opt/rocm/.info/version */
        char p[GT_PATH], v[64];
        if (gt_read(gt_p(p, sizeof p, "/opt/rocm/.info/version"), v, sizeof v) == 0 && v[0]) {
            char *dash = strchr(v, '-');
            if (dash) *dash = 0;
            gj_str(j, "ROCm Version", v);
        } else gj_null(j, "ROCm Version");
    }
    gj_str(j, "DeviceName", in->dev.name);
    gj_obj(j, "DevicePath");
    gj_str(j, "render", in->dev.render); gj_str(j, "card", in->dev.card); gj_str(j, "pci", in->dev.pci);
    gj_int(j, "DeviceID", in->dev.device_id); gj_int(j, "RevisionID", in->dev.revision_id); gj_str(j, "DeviceName", in->dev.name);
    gj_end(j, '}');
    gj_str(j, "PCI", in->dev.pci);
    gj_int(j, "DeviceID", a->di.device_id);
    gj_int(j, "RevisionID", a->di.pci_rev);
    gj_str(j, "GPU Type", in->is_apu ? "APU" : "dGPU");
    gj_str(j, "GPU Family", in->family);
    gj_str(j, "ASIC Name", in->asic);
    gj_str(j, "Chip Class", in->chip_class);
    gj_str(j, "gfx_target_version", in->gfx_target[0] ? in->gfx_target : NULL);
    gj_int(j, "Shader Engine", in->se);
    gj_int(j, "Shader Array per Shader Engine", in->sa_per_se);
    gj_obj(j, "CU per Shader Array"); gj_int(j, "min", mincu); gj_int(j, "max", maxcu); gj_end(j, '}');
    gj_int(j, "Total Compute Unit", in->cu);
    gj_int(j, "RenderBackend", in->rb);
    gj_str(j, "RenderBackend Type", rbplus_allowed(a->asic) ? "RB Plus" : "RB");
    gj_int(j, "Total ROP", in->rop);
    gj_obj(j, "GPU Clock"); gj_int(j, "min", in->min_gpu_clk); gj_int(j, "max", in->max_gpu_clk); gj_end(j, '}');
    gj_str(j, "VRAM Type", in->vram_type);
    gj_int(j, "VRAM Bit width", in->vram_bit_width);
    gj_str(j, "VRAM Vendor", in->vram_vendor[0] ? in->vram_vendor : NULL);
    gj_obj(j, "Memory Clock"); gj_int(j, "min", in->min_mem_clk); gj_int(j, "max", in->max_mem_clk); gj_end(j, '}');
    gj_bool(j, "ResizableBAR", in->resizable_bar);
    gj_u64(j, "VRAM Size", a->mem.vram.total);
    gj_u64(j, "VRAM Usage Size", a->mem.vram.usage);
    gj_u64(j, "GTT Size", a->mem.gtt.total);
    gj_u64(j, "GTT Usage Size", a->mem.gtt.usage);
    gj_int(j, "L1 Cache per CU", (long long)in->l1_kib_per_cu << 10);
    gj_int(j, "GL1 Cache per Shader Array", (long long)in->gl1_kib_per_sa << 10);
    gj_int(j, "L2 Cache", (long long)in->l2_kib << 10);
    gj_int(j, "L3 Cache", (long long)in->l3_mib << 20);
    if (in->power_cap_w != GT_NA) {
        gj_obj(j, "Power Cap");
        gj_int(j, "current", in->power_cap_w); gj_int(j, "min", in->power_cap_min_w); gj_int(j, "max", in->power_cap_max_w);
        gj_end(j, '}');
    } else gj_null(j, "Power Cap");
    if (a->vbios_ok) {
        gj_obj(j, "VBIOS");
        gj_str(j, "name", a->vb_name); gj_str(j, "pn", a->vb_pn); gj_str(j, "ver_str", a->vb_ver); gj_str(j, "date", a->vb_date);
        gj_end(j, '}');
    } else gj_null(j, "VBIOS");
    if (a->vcap_ok[0] && a->vcap_ok[1]) {
        gj_obj(j, "Video Caps");
        for (int k = 0; k < 8; k++) {
            gj_obj(j, CODECS[k]);
            for (int c = 0; c < 2; c++) {
                if (!a->vcap[c][k].valid) { gj_null(j, c ? "Encode" : "Decode"); continue; }
                gj_obj(j, c ? "Encode" : "Decode");
                gj_int(j, "width", a->vcap[c][k].w); gj_int(j, "height", a->vcap[c][k].h);
                gj_end(j, '}');
            }
            gj_end(j, '}');
        }
        gj_end(j, '}');
    } else gj_null(j, "Video Caps");
    if (in->is_apu) gj_null(j, "PCIe Link");
    else {
        struct { const char *k; int g, w; } L[4] = { { "min_dpm_link", in->pcie_dpm_min_gen, in->pcie_dpm_min_width },
            { "max_dpm_link", in->pcie_dpm_max_gen, in->pcie_dpm_max_width }, { "max_gpu_link", in->pcie_gpu_max_gen, in->pcie_gpu_max_width },
            { "max_system_link", in->pcie_sys_max_gen, in->pcie_sys_max_width } };
        gj_obj(j, "PCIe Link");
        for (int i = 0; i < 4; i++) {
            if (L[i].g == GT_NA) { gj_null(j, L[i].k); continue; }
            gj_obj(j, L[i].k); gj_int(j, "gen", L[i].g); gj_int(j, "width", L[i].w); gj_end(j, '}');
        }
        gj_end(j, '}');
    }
    gj_arr(j, "Power Profiles");
    for (int i = 0; i < a->nprofiles; i++) gj_str(j, NULL, a->profiles[i]);
    gj_end(j, ']');
    gj_arr(j, "pp_feature_mask");
    for (int i = 0; i < 20 && a->ppfeature_ok; i++) if ((a->ppfeature >> i) & 1) gj_str(j, NULL, PPFEATURE[i]);
    gj_end(j, ']');
    gj_null(j, "NPU");
    gj_vu(j, "Peak FP32", (long long)in->peak_fp32_gflops, "GFLOPS");
    gj_vu(j, "Peak Pixel Fill-Rate", (long long)in->peak_fill_gpixs, "GP/s");
    gj_vu(j, "Peak Memory Bandwidth", (long long)in->peak_mem_bw_gbs, "GB/s");
    gj_arr(j, "Hardware IP info");
    for (int i = 0; i < a->nhw; i++) {
        gj_obj(j, NULL);
        gj_str(j, "ip_type", hwip_name(a->hw[i].type)); gj_int(j, "ip_count", a->hw[i].count);
        gj_int(j, "major", a->hw[i].ip.major); gj_int(j, "minor", a->hw[i].ip.minor);
        gj_int(j, "queues", __builtin_popcount(a->hw[i].ip.rings));
        gj_end(j, '}');
    }
    gj_end(j, ']');
    gj_arr(j, "Firmware info");
    for (int i = 0; i < a->nfw; i++) {
        if (!a->fw[i].ver) continue;
        gj_obj(j, NULL);
        gj_str(j, "fw_type", fw_name(a->fw[i].type)); gj_int(j, "ip_instance", a->fw[i].inst); gj_int(j, "index", a->fw[i].idx);
        gj_int(j, "version", a->fw[i].ver); gj_int(j, "feature", a->fw[i].feat);
        gj_end(j, '}');
    }
    gj_end(j, ']');
    gj_arr(j, "IP Discovery table");
    for (int die = 0, i = 0; i < a->nip; die++) {
        gj_obj(j, NULL);
        gj_int(j, "die_id", die);
        gj_arr(j, "ip_hw_ids");
        for (; i < a->nip && a->ip[i].die == die; i++) {
            const char *hn = hwid_name(a->ip[i].hwid, nb, sizeof nb);
            gj_obj(j, NULL);
            gj_str(j, "hw_id", hn);
            gj_arr(j, "instances");
            gj_obj(j, NULL);
            gj_str(j, "hw_id", hn); gj_int(j, "num_instance", 0); gj_int(j, "major", a->ip[i].maj); gj_int(j, "minor", a->ip[i].min);
            gj_int(j, "revision", a->ip[i].rev); gj_int(j, "harvest", a->ip[i].harvest);
            gj_int(j, "num_base_addresses", a->ip[i].nbase);
            gj_arr(j, "base_address");
            for (int k = 0; k < a->ip[i].nbase && k < 8; k++) gj_int(j, NULL, a->ip[i].base[k]);
            gj_end(j, ']');
            gj_end(j, '}');
            gj_end(j, ']');
            gj_end(j, '}');
        }
        gj_end(j, ']');
        gj_end(j, '}');
    }
    gj_end(j, ']');
    return 0;
}

/* ------------------------------------------------------------------ DRM connectors (libdrm.so.2, dlopen) */

typedef struct { uint32_t clock; uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew, vdisplay, vsync_start, vsync_end,
                 vtotal, vscan; uint32_t vrefresh, flags, type; char name[32]; } mm_mode;
typedef struct { int count_fbs; uint32_t *fbs; int count_crtcs; uint32_t *crtcs; int count_connectors; uint32_t *connectors;
                 int count_encoders; uint32_t *encoders; uint32_t min_w, max_w, min_h, max_h; } mm_res;
typedef struct { uint32_t connector_id, encoder_id, connector_type, connector_type_id; int connection; uint32_t mm_w, mm_h;
                 int subpixel; int count_modes; mm_mode *modes; int count_props; uint32_t *props; uint64_t *prop_values;
                 int count_encoders; uint32_t *encoders; } mm_conn;
typedef struct { uint64_t value; char name[32]; } mm_enum;
typedef struct { uint32_t prop_id, flags; char name[32]; int count_values; uint64_t *values; int count_enums; mm_enum *enums;
                 int count_blobs; uint32_t *blob_ids; } mm_prop;
typedef struct { uint32_t crtc_id, buffer_id, x, y, width, height; int mode_valid; mm_mode mode; int gamma_size; } mm_crtc;

static double mode_refresh(const mm_mode *m)
{
    return m->htotal && m->vtotal ? (double)m->clock * 1000.0 / ((double)m->htotal * m->vtotal) : 0;
}

static const char *conn_type(unsigned t)
{
    static const char *n[] = { "Unknown", "VGA", "DVI-I", "DVI-D", "DVI-A", "Composite", "SVIDEO", "LVDS", "Component",
        "DIN", "DisplayPort", "HDMI-A", "HDMI-B", "TV", "eDP", "Virtual", "DSI", "DPI", "Writeback", "SPI", "USB" };
    return t < sizeof n / sizeof n[0] ? n[t] : "Unknown";
}

int gt_amd_drm_info(gt_dev *d, char *buf, size_t len, gt_json *j)
{
    void *h;
    mm_res *(*get_res)(int);
    void (*free_res)(mm_res *);
    mm_conn *(*get_conn)(int, uint32_t);
    void (*free_conn)(mm_conn *);
    mm_prop *(*get_prop)(int, uint32_t);
    void (*free_prop)(mm_prop *);
    mm_crtc *(*get_crtc)(int, uint32_t);
    void (*free_crtc)(mm_crtc *);
    mm_res *r;
    int fd;
    if (gt_testing()) {   /* tests: the text captured with the fixture */
        char p[GT_PATH], t[16384];
        snprintf(p, sizeof p, "%s/srs-drm-info.txt", d->sys);
        if (gt_read(p, t, sizeof t)) { gt_cat(buf, len, "DRM info: not available (no srs-drm-info.txt in the test root)\n"); return -1; }
        gt_cat(buf, len, "%s\n", t);
        return 0;
    }
    if (!d->info.dev.card[0]) { gt_cat(buf, len, "DRM info: the GPU has no display node\n"); return -1; }
    if (!(h = dlopen("libdrm.so.2", RTLD_NOW | RTLD_LOCAL))) { gt_cat(buf, len, "DRM info: libdrm.so.2 could not be loaded\n"); return -1; }
    *(void **)&get_res = dlsym(h, "drmModeGetResources");
    *(void **)&free_res = dlsym(h, "drmModeFreeResources");
    /* ...Current: no forced probe of the displays (no monitor blink) */
    *(void **)&get_conn = dlsym(h, "drmModeGetConnectorCurrent");
    *(void **)&free_conn = dlsym(h, "drmModeFreeConnector");
    *(void **)&get_prop = dlsym(h, "drmModeGetProperty");
    *(void **)&free_prop = dlsym(h, "drmModeFreeProperty");
    *(void **)&get_crtc = dlsym(h, "drmModeGetCrtc");
    *(void **)&free_crtc = dlsym(h, "drmModeFreeCrtc");
    if (!get_res || !free_res || !get_conn || !free_conn || !get_prop || !free_prop || !get_crtc || !free_crtc ||
        (fd = open(d->info.dev.card, O_RDONLY | O_CLOEXEC)) < 0) {
        gt_cat(buf, len, "DRM info: cannot open %s\n", d->info.dev.card);
        dlclose(h);
        return -1;
    }
    if (!(r = get_res(fd))) { gt_cat(buf, len, "DRM info: no mode resources on %s\n", d->info.dev.card); close(fd); dlclose(h); return -1; }
    gt_cat(buf, len, "\nNode: \"%s\"\n", d->info.dev.card);
    if (j) { gj_obj(j, NULL); gj_str(j, "Node", d->info.dev.card); gj_arr(j, "Connectors"); }
    for (int i = 0; i < r->count_connectors; i++) {
        mm_conn *c = get_conn(fd, r->connectors[i]);
        int last = i == r->count_connectors - 1;
        const char *bar = last ? " " : "\xe2\x94\x82";
        if (!c) continue;
        gt_cat(buf, len, "%s\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80" "Connector %u (%s-%u), %s\n", last ? "\xe2\x94\x94" : "\xe2\x94\x9c",
               c->connector_id, conn_type(c->connector_type), c->connector_type_id,
               c->connection == 1 ? "connected" : c->connection == 2 ? "disconnected" : "unknown");
        if (j) {
            gj_obj(j, NULL);
            gj_int(j, "id", c->connector_id); gj_str(j, "type", conn_type(c->connector_type)); gj_int(j, "type_id", c->connector_type_id);
            gj_str(j, "connection", c->connection == 1 ? "Connected" : c->connection == 2 ? "Disconnected" : "Unknown");
            gj_obj(j, "Properties");
        }
        for (int p = 0; p < c->count_props; p++) {
            mm_prop *pr = get_prop(fd, c->props[p]);
            if (!pr) continue;
            if (!strcmp(pr->name, "CRTC_ID") && c->prop_values[p]) {
                mm_crtc *cr = get_crtc(fd, (uint32_t)c->prop_values[p]);
                if (cr) {
                    gt_cat(buf, len, "%s    \xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80" "CRTC: %ux%u@%.4f\n", bar, cr->mode.hdisplay,
                           cr->mode.vdisplay, mode_refresh(&cr->mode));
                    free_crtc(cr);
                }
            }
            free_prop(pr);
        }
        if (c->count_modes) {
            gt_cat(buf, len, "%s    \xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80Modes\n", bar);
            for (int m = 0; m < c->count_modes; m++)
                gt_cat(buf, len, "%s    \xe2\x94\x82    %s\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80%ux%u@%.2f%s%s\n", bar,
                       m == c->count_modes - 1 ? "\xe2\x94\x94" : "\xe2\x94\x9c", c->modes[m].hdisplay, c->modes[m].vdisplay,
                       mode_refresh(&c->modes[m]), (c->modes[m].type & 8) ? " preferred" : "", (c->modes[m].type & 64) ? " driver" : "");
        }
        for (int p = 0; p < c->count_props; p++) {
            mm_prop *pr = get_prop(fd, c->props[p]);
            char extra[1024] = "";
            int lp = p == c->count_props - 1;
            if (!pr) continue;
            if (pr->flags & (1 << 4)) snprintf(extra, sizeof extra, ", blob");
            else if (pr->flags & (1 << 1)) {
                gt_cat(extra, sizeof extra, ", values: [");
                for (int v = 0; v < pr->count_values; v++) gt_cat(extra, sizeof extra, "%s%llu", v ? ", " : "", (unsigned long long)pr->values[v]);
                gt_cat(extra, sizeof extra, "]");
            } else if (pr->flags & (1 << 3)) {
                gt_cat(extra, sizeof extra, ", enums: [");
                for (int v = 0; v < pr->count_enums; v++)
                    gt_cat(extra, sizeof extra, "%s\"%s\"=%llu", v ? ", " : "", pr->enums[v].name, (unsigned long long)pr->enums[v].value);
                gt_cat(extra, sizeof extra, "]");
            }
            gt_cat(buf, len, "%s    %s\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\"%s\", id = %u, value: %llu%s\n", bar,
                   lp ? "\xe2\x94\x94" : "\xe2\x94\x9c", pr->name, pr->prop_id, (unsigned long long)c->prop_values[p], extra);
            if (j) {
                gj_obj(j, pr->name);
                gj_int(j, "id", pr->prop_id); gj_int(j, "flags", pr->flags); gj_u64(j, "value", c->prop_values[p]);
                gj_str(j, "type", (pr->flags & (1 << 4)) ? "Blob" : (pr->flags & (1 << 1)) ? "Range" : (pr->flags & (1 << 3)) ? "Enum" : "Unknown");
                gj_arr(j, "values");
                if (pr->flags & (1 << 1)) for (int v = 0; v < pr->count_values; v++) gj_u64(j, NULL, pr->values[v]);
                gj_end(j, ']');
                gj_arr(j, "enums");
                if (pr->flags & (1 << 3)) for (int v = 0; v < pr->count_enums; v++) {
                    gj_obj(j, NULL); gj_str(j, "name", pr->enums[v].name); gj_u64(j, "value", pr->enums[v].value); gj_end(j, '}');
                }
                gj_end(j, ']');
                gj_end(j, '}');
            }
            free_prop(pr);
        }
        if (j) { gj_end(j, '}'); gj_end(j, '}'); }
        free_conn(c);
    }
    if (j) { gj_end(j, ']'); gj_end(j, '}'); }
    free_res(r);
    close(fd);
    dlclose(h);
    return 0;
}

/* ------------------------------------------------------------------ pp_table (Navi1x / 2x / 3x) */

static unsigned rd(const unsigned char *b, int off, int size)
{
    unsigned v = 0;
    for (int i = size - 1; i >= 0; i--) v = (v << 8) | b[off + i];
    return v;
}

int gt_amd_pp_table(gt_dev *d, char *buf, size_t len)
{
    static const char *const S11_0[] = { "GFXCLKFMAX", "GFXCLKFMIN", "VDDGFXCURVEFREQ_P1", "VDDGFXCURVEVOLTAGE_P1",
        "VDDGFXCURVEFREQ_P2", "VDDGFXCURVEVOLTAGE_P2", "VDDGFXCURVEFREQ_P3", "VDDGFXCURVEVOLTAGE_P3", "UCLKFMAX",
        "POWERPERCENTAGE", "FANRPMMIN", "FANRPMACOUSTICLIMIT", "FANTARGETTEMPERATURE", "OPERATINGTEMPMAX", "ACTIMING",
        "FAN_ZERO_RPM_CONTROL", "AUTOUVENGINE", "AUTOOCENGINE", "AUTOOCMEMORY" };
    static const char *const S11_7[] = { "GFXCLKFMAX", "GFXCLKFMIN", "CUSTOM_GFX_VF_CURVE_A", "CUSTOM_GFX_VF_CURVE_B",
        "CUSTOM_GFX_VF_CURVE_C", "CUSTOM_CURVE_VFT_FMIN", "UCLKFMIN", "UCLKFMAX", "POWERPERCENTAGE", "FANRPMMIN",
        "FANRPMACOUSTICLIMIT", "FANTARGETTEMPERATURE", "OPERATINGTEMPMAX", "ACTIMING", "FAN_ZERO_RPM_CONTROL",
        "AUTOUVENGINE", "AUTOOCENGINE", "AUTOOCMEMORY", "FAN_CURVE_TEMPERATURE_1", "FAN_CURVE_SPEED_1",
        "FAN_CURVE_TEMPERATURE_2", "FAN_CURVE_SPEED_2", "FAN_CURVE_TEMPERATURE_3", "FAN_CURVE_SPEED_3",
        "FAN_CURVE_TEMPERATURE_4", "FAN_CURVE_SPEED_4", "FAN_CURVE_TEMPERATURE_5", "FAN_CURVE_SPEED_5",
        "AUTO_FAN_ACOUSTIC_LIMIT", "POWER_MODE" };
    static const char *const S13[] = { "GFXCLKFMAX", "GFXCLKFMIN", "UCLKFMIN", "UCLKFMAX", "POWERPERCENTAGE", "FANRPMMIN",
        "FANRPMACOUSTICLIMIT", "FANTARGETTEMPERATURE", "OPERATINGTEMPMAX", "ACTIMING", "FAN_ZERO_RPM_CONTROL",
        "AUTOUVENGINE", "AUTOOCENGINE", "AUTOOCMEMORY", "FAN_CURVE_TEMPERATURE_1", "FAN_CURVE_SPEED_1",
        "FAN_CURVE_TEMPERATURE_2", "FAN_CURVE_SPEED_2", "FAN_CURVE_TEMPERATURE_3", "FAN_CURVE_SPEED_3",
        "FAN_CURVE_TEMPERATURE_4", "FAN_CURVE_SPEED_4", "FAN_CURVE_TEMPERATURE_5", "FAN_CURVE_SPEED_5",
        "AUTO_FAN_ACOUSTIC_LIMIT", "POWER_MODE", "PER_ZONE_GFX_VOLTAGE_OFFSET_POINT_1", "PER_ZONE_GFX_VOLTAGE_OFFSET_POINT_2",
        "PER_ZONE_GFX_VOLTAGE_OFFSET_POINT_3", "PER_ZONE_GFX_VOLTAGE_OFFSET_POINT_4", "PER_ZONE_GFX_VOLTAGE_OFFSET_POINT_5",
        "PER_ZONE_GFX_VOLTAGE_OFFSET_POINT_6" };
    unsigned char b[65536];
    char p[GT_PATH];
    long n;
    int fr, kind, *smu = d->info.smu_ip;
    const char *const *names;
    int nnames, od, maxoff, minoff, shut;
    const char *what;
    gt_cat(buf, len, "%s (%s, 0x%X:0x%X)\n", d->info.dev.name, d->info.dev.pci, d->info.dev.device_id, d->info.dev.revision_id);
    if (smu[0] != GT_NA) gt_cat(buf, len, "    SMU (MP1) version: %d.%d.%d\n", smu[0], smu[1], smu[2]);
    snprintf(p, sizeof p, "%s/pp_table", d->sys);
    if ((n = gt_read_bin(p, b, sizeof b)) < 4) { gt_cat(buf, len, "    pp_table: not available on this card (no pp_table file)\n"); return -1; }
    fr = b[2];
    /* which table: by SMU version (amdgpu_top decode_with_smu_version), else by the header's format revision */
    if (smu[0] == 11 && (smu[2] == 0 || smu[2] == 5 || smu[2] == 9) && smu[1] == 0) kind = 110;
    else if (smu[0] == 11 && smu[1] == 0 && (smu[2] == 7 || smu[2] == 11 || smu[2] == 12 || smu[2] == 13)) kind = 117;
    else if (smu[0] == 13 && smu[1] == 0 && (smu[2] == 0 || smu[2] == 10)) kind = 130;
    else if (smu[0] == 13 && smu[1] == 0 && smu[2] == 7) kind = 137;
    else if (smu[0] != GT_NA) kind = 0;
    else kind = (fr == 12 || fr == 14) ? 110 : (fr == 15 || fr == 16 || fr == 18 || fr == 19) ? 117 : fr == 20 ? 130 : 0;
    if (!kind) {
        gt_cat(buf, len, "    pp_table: not available on this card (decoded for RX 5000 / 6000 / 7000 only; header v%d.%d, %u bytes)\n",
               b[2], b[3], rd(b, 0, 2));
        return -1;
    }
    if ((long)rd(b, 0, 2) > n) { gt_cat(buf, len, "    pp_table: shorter than its header says\n"); return -1; }
    if (kind == 110) { what = "smu_11_0_powerplay_table (Navi 10/12/14)"; names = S11_0; nnames = 19; od = 182; maxoff = 226; minoff = 354; shut = 32; }
    else if (kind == 117) { what = "smu_11_0_7_powerplay_table (Navi 21-24)"; names = S11_7; nnames = 30; od = 182; maxoff = 226; minoff = 482; shut = 28; }
    else { what = kind == 130 ? "smu_13_0_0_powerplay_table (Navi 31)" : "smu_13_0_7_powerplay_table (Navi 32/33)";
           names = S13; nnames = 32; od = 211; maxoff = 255; minoff = 511; shut = 29; }
    if (minoff + 4 * nnames > n) { gt_cat(buf, len, "    pp_table: too short for %s\n", what); return -1; }
    {
        int t = kind >= 130;   /* 13.0.x has one byte of padding after table_revision */
        gt_cat(buf, len, "    from sysfs: %s\n", what);
        gt_cat(buf, len, "        header: structuresize %u, format_revision %u, content_revision %u\n", rd(b, 0, 2), b[2], b[3]);
        gt_cat(buf, len, "        table_revision          : %u\n", b[4]);
        gt_cat(buf, len, "        table_size              : %u\n", rd(b, 5 + t, 2));
        gt_cat(buf, len, "        golden_pp_id            : %u\n", rd(b, 7 + t, 4));
        gt_cat(buf, len, "        golden_revision         : %u\n", rd(b, 11 + t, 4));
        gt_cat(buf, len, "        format_id               : %u\n", rd(b, 15 + t, 2));
        gt_cat(buf, len, "        platform_caps           : 0x%X\n", rd(b, 17 + t, 4));
        gt_cat(buf, len, "        thermal_controller_type : %u\n", b[21 + t]);
        gt_cat(buf, len, "        small_power_limit1      : %u W\n", rd(b, 22 + t, 2));
        gt_cat(buf, len, "        small_power_limit2      : %u W\n", rd(b, 24 + t, 2));
        gt_cat(buf, len, "        boost_power_limit       : %u W\n", rd(b, 26 + t, 2));
        if (kind == 110) {
            gt_cat(buf, len, "        od_turbo_power_limit    : %u W\n", rd(b, 28, 2));
            gt_cat(buf, len, "        od_power_save_power_limit: %u W\n", rd(b, 30, 2));
        }
        gt_cat(buf, len, "        software_shutdown_temp  : %u C\n", rd(b, shut, 2));
        if (kind < 130) {
            unsigned cnt = rd(b, 50, 4);
            gt_cat(buf, len, "        power_saving_clock      : revision %u, count %u\n", b[46], cnt);
            for (unsigned i = 0; i < cnt && i < 16; i++)
                gt_cat(buf, len, "            [%2u] min %6u, max %6u\n", i, rd(b, 54 + 64 + 4 * i, 4), rd(b, 54 + 4 * i, 4));
        }
        gt_cat(buf, len, "        overdrive_table         : revision %u, feature_count %u, setting_count %u\n", b[od],
               rd(b, od + 4, 4), rd(b, od + 8, 4));
        gt_cat(buf, len, "            cap:");
        for (unsigned i = 0; i < rd(b, od + 4, 4) && i < 32; i++) gt_cat(buf, len, " %u", b[od + 12 + i]);
        gt_cat(buf, len, "\n");
        for (int i = 0; i < nnames && (unsigned)i < rd(b, od + 8, 4); i++)
            gt_cat(buf, len, "            %-36s: min %8d, max %8d\n", names[i], (int)rd(b, minoff + 4 * i, 4), (int)rd(b, maxoff + 4 * i, 4));
    }
    return 0;
}
