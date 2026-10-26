/* bench_gpu.h - GPU compute benchmark through Vulkan (bench_gpu.c), new in SRS 2.2.
 * The Vulkan loader (libvulkan.so.1) is opened at run time with dlopen, so SRS has no build or
 * package dependency on Vulkan: without a loader or a Vulkan driver the benchmark says
 * "not available" and everything else works as before.
 *
 * Measures, on one Vulkan device the user picks:
 *   - FP32, FP16, FP64 fused multiply-add and INT32 multiply-add throughput (GFLOPS / GIOPS)
 *   - VRAM read, write and copy bandwidth (GB/s)
 *   - host-to-device (upload) and device-to-host (download) transfer speed (GB/s, PCIe)
 *   - dispatch latency: the round trip of an empty dispatch (us)
 * GPU time comes from Vulkan timestamp queries. Every result is checked against a value the host
 * works out on its own; a wrong result is shown as wrong, never as a speed.
 *
 * Safety: every submission is short (about 25 ms of GPU work), at most one is in flight, each is
 * waited for with a hard timeout, Stop is checked between submissions, and a lost or stuck GPU
 * ends the run cleanly (see bench_gpu.c). Linux only; elsewhere every call says "not available". */
#ifndef BENCH_GPU_H
#define BENCH_GPU_H

#include "sec.h"
#include <stddef.h>

enum { GT_FP32, GT_FP16, GT_FP64, GT_INT32, GT_READ, GT_WRITE, GT_COPY, GT_UPLOAD, GT_DOWNLOAD, GT_LATENCY, GT_N };
extern const char *const gpu_test_id[GT_N];     /* "fp32", "fp16", ... "latency" (CLI --tests words) */
extern const char *const gpu_test_name[GT_N];   /* "FP32 multiply-add", ... (table rows) */

/* device types, as Vulkan reports them */
enum { GPU_OTHER, GPU_INTEGRATED, GPU_DISCRETE, GPU_VIRTUAL, GPU_CPU };

#define GPU_MAXDEV 16
typedef struct {
    char name[256];          /* "AMD Radeon RX 9070 XT (RADV GFX1201)" */
    char driver[300];        /* "radv, Mesa 26.0.0" (driver name + info; "" if the driver does not say) */
    char api[24];            /* Vulkan version the device supports: "1.4.318" */
    const char *type;        /* "discrete GPU", "integrated GPU", "virtual GPU", "software renderer (CPU)", "other" */
    int dtype;               /* GPU_* */
    unsigned vendor_id, device_id;
    char pci[16];            /* "0000:03:00.0" when the driver tells (VK_EXT_pci_bus_info), else "" */
    double vram_bytes;       /* total of the device-local memory heaps */
    double host_bytes;       /* total of the other heaps (system memory the GPU can reach) */
    int heaps;               /* number of memory heaps */
    int subgroup, sg_min, sg_max;   /* subgroup (wave / warp) size; min/max 0 if not reported */
    int cus;                 /* compute units (AMD) or SMs (NVIDIA); 0 = not reported */
    const char *cu_word;     /* "compute units" / "SMs" */
    int fp16, fp64, timestamps;     /* 1 when supported (timestamps on a compute queue) */
} gpu_dev;

typedef struct {
    int device;              /* index into gpu_list(); -1 = default (first discrete GPU, else the first device) */
    double seconds;          /* GPU time per test (default 1.0, 0.1 .. 2.0) */
    unsigned tests;          /* bit (1u << GT_*) per test; 0 = all */
    double mem_mib;          /* buffer size for the memory and transfer tests, MiB; 0 = auto (256, less on small cards) */
    int software_only;       /* tests: refuse any device that is not a software renderer (lavapipe) */
} gpu_cfg;

void gpu_cfg_default(gpu_cfg *c);
/* 1 when the Vulkan loader can be opened (does not touch any GPU); why says why not */
int  gpu_available(char *why, size_t wn);
/* list the Vulkan devices (creates and destroys a Vulkan instance; no device is opened and no work is
   sent). Returns the count, -1 when Vulkan is not available (why says why). */
int  gpu_list(gpu_dev *d, int max, char *why, size_t wn);
int  gpu_default_device(const gpu_dev *d, int n);
/* calls the env helper: implicit Vulkan layers (overlays such as MangoHud) are kept out of the run.
   Sets VK_LOADER_LAYERS_DISABLE=~implicit~ and DISABLE_MANGOHUD=1 unless already set. setenv is not
   thread-safe: call it from the main thread before starting worker threads (cli: before the run;
   GUI: at start-up, or accept that bench_gpu() calls it itself before its first vkCreateInstance). */
void gpu_quiet_layers(void);
const char *gpu_stage(void);
/* one GPU benchmark or GPU burn-in at a time in this process (bench_gpu, bench_gpuburn, bench_gpucompare
   take it); returns 1 when claimed, 0 when another one runs (who says which) */
int  gpu_claim(const char *name, const char **who);
void gpu_release(void);
int  gpu_busy(void);           /* 1 while any GPU benchmark or burn-in holds the claim (the CPU burner can refuse to start) */   /* "FP32 multiply-add" while a run is going on, "" when idle */
/* run, render into o (may be NULL) and ~/.cache/srs/bench-gpu.txt, add a history line.
   0 ok, 1 stopped (cancel), 2 a result was wrong (shown as wrong), -1 not started (why says why),
   -2 the GPU failed during the run (device lost / did not finish in time; results so far shown, why says what) */
int  bench_gpu(const gpu_cfg *c, srs_out *o, volatile int *cancel, char *why, size_t wn);

#endif
