/* gpu_top_nvml.c - GPU Top, NVIDIA back end: NVML (libnvidia-ml.so.1), loaded at run time with dlopen.
 * Own prototypes for the few NVML calls used; no NVIDIA headers or libraries are needed to build.
 *
 * NVML is opened only for a device whose PCI vendor is 0x10de (gpu_top.c checks it before calling here):
 * on AMD systems libnvidia-ml.so.1 can be ZLUDA's stand-in, which must not be loaded for an AMD card.
 * GRBM-style per-block counters have no NVML equivalent: stat.pc_ok stays 0.
 * Tests: with SRS_TEST_GPU_ROOT set, SRS_TEST_NVML_LIB=/abs/path/libnvml_stub.so (tests/nvml_stub.c) is loaded instead,
 * and nothing at all without it.
 */
#include "gpu_top_int.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *nvdev;
typedef struct { unsigned gpu, memory; } nv_util;
typedef struct { unsigned long long total, free, used; } nv_mem;
typedef struct { unsigned pid; unsigned long long used; unsigned gi, ci; } nv_proc;   /* nvmlProcessInfo_v2_t */

struct gt_nv {
    void *h;
    nvdev dev;
    int (*shutdown)(void);
    int (*name)(nvdev, char *, unsigned);
    int (*clock)(nvdev, int, unsigned *);
    int (*max_clock)(nvdev, int, unsigned *);
    int (*util)(nvdev, nv_util *);
    int (*enc)(nvdev, unsigned *, unsigned *);
    int (*dec)(nvdev, unsigned *, unsigned *);
    int (*mem)(nvdev, nv_mem *);
    int (*power)(nvdev, unsigned *);
    int (*limit)(nvdev, unsigned *);
    int (*limits)(nvdev, unsigned *, unsigned *);
    int (*def_limit)(nvdev, unsigned *);
    int (*temp)(nvdev, int, unsigned *);
    int (*temp_thr)(nvdev, int, unsigned *);
    int (*fan)(nvdev, unsigned *);
    int (*pcie_gen)(nvdev, unsigned *), (*pcie_width)(nvdev, unsigned *);
    int (*pcie_max_gen)(nvdev, unsigned *), (*pcie_max_width)(nvdev, unsigned *);
    int (*pcie_tp)(nvdev, int, unsigned *);
    int (*thr)(nvdev, unsigned long long *);
    int (*procs_c)(nvdev, unsigned *, nv_proc *), (*procs_g)(nvdev, unsigned *, nv_proc *);
    int (*pstate)(nvdev, int *);
};

#define SYM(f, n) (*(void **)&nv->f = dlsym(nv->h, n))

int gt_nv_name(const char *pci, char *name, size_t n)
{
    (void)pci; (void)name; (void)n;
    return -1;   /* the list uses pci.ids; gt_open puts NVML's name in gt_info */
}

int gt_nv_open(gt_dev *d, char *err, size_t errlen)
{
    struct gt_nv *nv;
    const char *lib = getenv("SRS_TEST_NVML_LIB");
    int (*init)(void), (*by_pci)(const char *, nvdev *), (*uuid)(nvdev, char *, unsigned), (*drv)(char *, unsigned),
        (*vbios)(nvdev, char *, unsigned), (*bus)(nvdev, unsigned *), (*cores)(nvdev, unsigned *);
    gt_info *in = &d->info;
    gt_section *s;
    char b[128];
    unsigned u, u2;
    if (in->dev.vendor_id != 0x10de) return -1;   /* never for anything but a real NVIDIA card */
    /* tests (a made-up sysfs): only the stub, never the system's library */
    if (gt_testing() && !(lib && lib[0] == '/')) { if (err) snprintf(err, errlen, "test root without SRS_TEST_NVML_LIB"); return -1; }
    if (!gt_testing()) lib = NULL;
    if (!(nv = calloc(1, sizeof *nv))) return -1;
    nv->h = dlopen(lib ? lib : "libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!nv->h) { if (err) snprintf(err, errlen, "libnvidia-ml.so.1 could not be loaded (NVIDIA driver)"); free(nv); return -1; }
    *(void **)&init = dlsym(nv->h, "nvmlInit_v2");
    *(void **)&by_pci = dlsym(nv->h, "nvmlDeviceGetHandleByPciBusId_v2");
    SYM(shutdown, "nvmlShutdown");
    if (!init || !by_pci || !nv->shutdown || init() != 0) { dlclose(nv->h); free(nv); if (err) snprintf(err, errlen, "NVML did not start"); return -1; }
    if (by_pci(in->dev.pci, &nv->dev) != 0) {
        nv->shutdown(); dlclose(nv->h); free(nv);
        if (err) snprintf(err, errlen, "NVML does not know %s", in->dev.pci);
        return -1;
    }
    SYM(name, "nvmlDeviceGetName"); SYM(clock, "nvmlDeviceGetClockInfo"); SYM(max_clock, "nvmlDeviceGetMaxClockInfo");
    SYM(util, "nvmlDeviceGetUtilizationRates"); SYM(enc, "nvmlDeviceGetEncoderUtilization"); SYM(dec, "nvmlDeviceGetDecoderUtilization");
    SYM(mem, "nvmlDeviceGetMemoryInfo"); SYM(power, "nvmlDeviceGetPowerUsage"); SYM(limit, "nvmlDeviceGetEnforcedPowerLimit");
    SYM(limits, "nvmlDeviceGetPowerManagementLimitConstraints"); SYM(def_limit, "nvmlDeviceGetPowerManagementDefaultLimit");
    SYM(temp, "nvmlDeviceGetTemperature"); SYM(temp_thr, "nvmlDeviceGetTemperatureThreshold"); SYM(fan, "nvmlDeviceGetFanSpeed");
    SYM(pcie_gen, "nvmlDeviceGetCurrPcieLinkGeneration"); SYM(pcie_width, "nvmlDeviceGetCurrPcieLinkWidth");
    SYM(pcie_max_gen, "nvmlDeviceGetMaxPcieLinkGeneration"); SYM(pcie_max_width, "nvmlDeviceGetMaxPcieLinkWidth");
    SYM(pcie_tp, "nvmlDeviceGetPcieThroughput");
    if (!SYM(thr, "nvmlDeviceGetCurrentClocksEventReasons")) SYM(thr, "nvmlDeviceGetCurrentClocksThrottleReasons");
    SYM(procs_c, "nvmlDeviceGetComputeRunningProcesses_v2"); SYM(procs_g, "nvmlDeviceGetGraphicsRunningProcesses_v2");
    SYM(pstate, "nvmlDeviceGetPerformanceState");
    *(void **)&uuid = dlsym(nv->h, "nvmlDeviceGetUUID");
    *(void **)&drv = dlsym(nv->h, "nvmlSystemGetDriverVersion");
    *(void **)&vbios = dlsym(nv->h, "nvmlDeviceGetVbiosVersion");
    *(void **)&bus = dlsym(nv->h, "nvmlDeviceGetMemoryBusWidth");
    *(void **)&cores = dlsym(nv->h, "nvmlDeviceGetNumGpuCores");
    d->nv = nv;

    in->is_apu = 0;
    if (nv->name && nv->name(nv->dev, b, sizeof b) == 0) snprintf(in->dev.name, sizeof in->dev.name, "%s", b);
    if (drv && drv(b, sizeof b) == 0) snprintf(in->driver_version, sizeof in->driver_version, "%s", b);
    if (vbios && vbios(nv->dev, b, sizeof b) == 0) snprintf(in->vbios_ver, sizeof in->vbios_ver, "%s", b);
    if (nv->max_clock && nv->max_clock(nv->dev, 0, &u) == 0) in->max_gpu_clk = (int)u;
    if (nv->max_clock && nv->max_clock(nv->dev, 2, &u) == 0) in->max_mem_clk = (int)u;
    if (bus && bus(nv->dev, &u) == 0) in->vram_bit_width = (int)u;
    {
        nv_mem m;
        if (nv->mem && nv->mem(nv->dev, &m) == 0) in->vram_total = (long long)m.total;
    }
    if (nv->limits && nv->limits(nv->dev, &u, &u2) == 0) { in->power_cap_min_w = (int)(u / 1000); in->power_cap_max_w = (int)(u2 / 1000); }
    if (nv->limit && nv->limit(nv->dev, &u) == 0) in->power_cap_w = (int)(u / 1000);
    if (nv->def_limit && nv->def_limit(nv->dev, &u) == 0) in->power_cap_default_w = (int)(u / 1000);
    if (nv->temp_thr && nv->temp_thr(nv->dev, 0, &u) == 0) in->edge_emerg = (int)u;   /* shutdown */
    if (nv->temp_thr && nv->temp_thr(nv->dev, 1, &u) == 0) in->edge_crit = (int)u;    /* slowdown */
    if (nv->pcie_max_gen && nv->pcie_max_width && nv->pcie_max_gen(nv->dev, &u) == 0 && nv->pcie_max_width(nv->dev, &u2) == 0) {
        in->pcie_gpu_max_gen = (int)u; in->pcie_gpu_max_width = (int)u2;
    }
    gt_link_read(d->port_sys[0] ? d->port_sys : d->sys, 1, &in->pcie_sys_max_gen, &in->pcie_sys_max_width);

    s = gt_sect_new(in, "Device");
    gt_kv_add(s, "Device Name", "[%s]", in->dev.name);
    gt_kv_add(s, "PCI (domain:bus:dev.func)", "%s", in->dev.pci);
    gt_kv_add(s, "DeviceID.RevID", "0x%X.0x%X", in->dev.device_id, in->dev.revision_id);
    if (uuid && uuid(nv->dev, b, sizeof b) == 0) gt_kv_add(s, "UUID", "%s", b);
    s = gt_sect_new(in, "GPU");
    gt_kv_add(s, "GPU Type", "dGPU");
    gt_kv_add(s, "Driver", "nvidia %s", in->driver_version);
    if (cores && cores(nv->dev, &u) == 0) gt_kv_add(s, "CUDA Cores", "%u", u);
    s = gt_sect_new(in, "Clock");
    if (in->max_gpu_clk != GT_NA) gt_kv_add(s, "GPU Clock (Max)", "%d MHz", in->max_gpu_clk);
    if (in->max_mem_clk != GT_NA) gt_kv_add(s, "Memory Clock (Max)", "%d MHz", in->max_mem_clk);
    s = gt_sect_new(in, "VRAM");
    if (in->vram_total != GT_NA) gt_kv_add(s, "VRAM", "%lld MiB", in->vram_total >> 20);
    if (in->vram_bit_width != GT_NA) gt_kv_add(s, "VRAM Bit Width", "%d-bit", in->vram_bit_width);
    s = gt_sect_new(in, "Sensors");
    if (in->power_cap_w != GT_NA) gt_kv_add(s, "Power Cap.", "%3d W (%d-%d W)", in->power_cap_w, in->power_cap_min_w, in->power_cap_max_w);
    if (in->power_cap_default_w != GT_NA) gt_kv_add(s, "Power Cap. (Default)", "%3d W", in->power_cap_default_w);
    if (in->edge_crit != GT_NA) gt_kv_add(s, "GPU Temp. (Slowdown)", "%d C", in->edge_crit);
    if (in->edge_emerg != GT_NA) gt_kv_add(s, "GPU Temp. (Shutdown)", "%d C", in->edge_emerg);
    if (in->pcie_gpu_max_gen != GT_NA) gt_kv_add(s, "PCIe Link Speed (GPU, Max)", "Gen%dx%d", in->pcie_gpu_max_gen, in->pcie_gpu_max_width);
    if (in->pcie_sys_max_gen != GT_NA) gt_kv_add(s, "PCIe Link Speed (System, Max)", "Gen%dx%d", in->pcie_sys_max_gen, in->pcie_sys_max_width);
    if (in->vbios_ver[0]) gt_kv_add(gt_sect_new(in, "VBIOS"), "ver_str", "[%s]", in->vbios_ver);
    gt_kv_add(gt_sect_new(in, "Note"), "GRBM / GRBM2", "not available on NVIDIA (no NVML equivalent)");
    return 0;
}

void gt_nv_close(gt_dev *d)
{
    struct gt_nv *nv = d->nv;
    if (!nv) return;
    if (nv->shutdown) nv->shutdown();
    if (nv->h) dlclose(nv->h);
    free(nv);
    d->nv = NULL;
}

/* clocks_event_reasons bits (nvml.h) */
static const struct { unsigned long long bit; const char *name; } NVTHR[] = {
    { 0x1ULL, "GpuIdle" }, { 0x2ULL, "ApplicationsClocksSetting" }, { 0x4ULL, "SwPowerCap" }, { 0x8ULL, "HwSlowdown" },
    { 0x10ULL, "SyncBoost" }, { 0x20ULL, "SwThermalSlowdown" }, { 0x40ULL, "HwThermalSlowdown" },
    { 0x80ULL, "HwPowerBrakeSlowdown" }, { 0x100ULL, "DisplayClockSetting" },
};

void gt_nv_update(gt_dev *d, gt_stat *s)
{
    struct gt_nv *nv = d->nv;
    unsigned u, p;
    nv_util ut;
    nv_mem m;
    nv_proc pr[64];
    unsigned n;
    if (!nv) return;
    if (nv->clock && nv->clock(nv->dev, 0, &u) == 0) s->sclk = (int)u;
    if (nv->clock && nv->clock(nv->dev, 2, &u) == 0) s->mclk = (int)u;
    if (nv->util && nv->util(nv->dev, &ut) == 0) { s->gfx = (int)ut.gpu; s->umc = (int)ut.memory; }
    if (nv->enc && nv->enc(nv->dev, &u, &p) == 0) s->enc_util = (int)u;
    if (nv->dec && nv->dec(nv->dev, &u, &p) == 0) s->dec_util = (int)u;
    if (s->enc_util != GT_NA || s->dec_util != GT_NA) s->media = s->enc_util > s->dec_util ? s->enc_util : s->dec_util;
    if (nv->mem && nv->mem(nv->dev, &m) == 0) { s->vram_used = (long long)m.used; s->vram_total = (long long)m.total; s->vram_usable = (long long)m.total; }
    s->gtt_used = s->gtt_total = s->vis_vram_used = s->vis_vram_total = 0;
    s->gtt_usable = 0;
    s->nthr = 0;
    if (nv->thr) {
        unsigned long long r;
        if (nv->thr(nv->dev, &r) == 0)
            for (size_t i = 0; i < sizeof NVTHR / sizeof NVTHR[0] && s->nthr < GT_MAX_THR; i++)
                if (r & NVTHR[i].bit) snprintf(s->thr[s->nthr++], sizeof s->thr[0], "%s", NVTHR[i].name);
    }
    /* processes: memory only (NVML has no cheap per-engine time per process) */
    s->nproc = 0;
    memset(&s->total, 0, sizeof s->total);
    snprintf(s->total.name, sizeof s->total.name, "Total");
    s->total.pid = GT_NA;
    for (int pass = 0; pass < 2; pass++) {
        int (*f)(nvdev, unsigned *, nv_proc *) = pass ? nv->procs_g : nv->procs_c;
        n = 64;
        if (!f || f(nv->dev, &n, pr) != 0) continue;
        for (unsigned i = 0; i < n && s->nproc < GT_MAX_PROC; i++) {
            gt_proc *q = NULL;
            char pd[GT_PATH];
            for (int k = 0; k < s->nproc; k++) if (s->proc[k].pid == (int)pr[i].pid) q = &s->proc[k];
            if (q) continue;
            q = &s->proc[s->nproc++];
            memset(q, 0, sizeof *q);
            q->pid = (int)pr[i].pid;
            q->vram_kib = pr[i].used == ~0ULL ? 0 : (long long)(pr[i].used >> 10);
            q->cpu = q->gfx = q->compute = q->dma = q->dec = q->enc = q->media = q->vcn_jpeg = q->vpe = q->vcn_unified = GT_NA;
            q->nvml_mem_only = 1;
            q->is_kfd = pass == 0;   /* a compute (CUDA) process */
            if (gt_readf(q->name, sizeof q->name, gt_p(pd, sizeof pd, "/proc/%d", q->pid), "comm")) snprintf(q->name, sizeof q->name, "?");
            s->total.vram_kib += q->vram_kib;
        }
    }
    s->total.cpu = s->total.gfx = s->total.compute = s->total.dma = s->total.dec = s->total.enc = s->total.media = GT_NA;
    s->total.vcn_jpeg = s->total.vpe = s->total.vcn_unified = GT_NA;
    s->total.nvml_mem_only = 1;
}

/* the sensors gpu_top.c reads from hwmon on AMD come from NVML here; called after read_sensors */
void gt_nv_sensors(gt_dev *d, gt_stat *s)
{
    struct gt_nv *nv = d->nv;
    unsigned u, w;
    if (!nv) return;
    if (nv->temp && nv->temp(nv->dev, 0, &u) == 0) s->edge = (int)u;
    if (nv->power && nv->power(nv->dev, &u) == 0) s->power_input = s->power_avg = u / 1000.0;
    if (nv->limit && nv->limit(nv->dev, &u) == 0) s->power_cap = (int)(u / 1000);
    if (nv->fan && nv->fan(nv->dev, &u) == 0) s->fan_pct = (int)u;
    if (nv->pcie_gen && nv->pcie_width && nv->pcie_gen(nv->dev, &u) == 0 && nv->pcie_width(nv->dev, &w) == 0) { s->pcie_gen = (int)u; s->pcie_width = (int)w; }
    if (nv->pcie_tp && nv->pcie_tp(nv->dev, 0, &u) == 0) s->pcie_tx_kbs = u;
    if (nv->pcie_tp && nv->pcie_tp(nv->dev, 1, &u) == 0) s->pcie_rx_kbs = u;
    if (nv->pstate) {
        int ps;
        if (nv->pstate(nv->dev, &ps) == 0 && ps >= 0 && ps < 32) snprintf(s->power_profile, sizeof s->power_profile, "P%d", ps);
    }
}
