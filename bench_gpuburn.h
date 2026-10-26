/* bench_gpuburn.h - GPU Burnin: keep a GPU fully busy with repeated large matrix multiplications and COUNT
 * COMPUTE ERRORS (bench_gpuburn.c), new in SRS 2.2. Inspired by gpu-burn (Ville Timonen, BSD-2-Clause,
 * github.com/wilicc/gpu-burn) and its ROCm port: the first result of the multiplication is the reference,
 * every later result is compared with it on the GPU, and any difference is an error. 0 errors = PASS.
 *
 * Back ends (gpu_backend.h): Vulkan, ROCm / HIP, CUDA, OpenCL (each platform on its own), chosen by the user;
 * "Auto" means Vulkan when it is there. All are opened with dlopen: no build dependency, no extra app.
 *
 * Safety: nothing starts by itself (the caller asks first); the work is cut into short pieces (about 20 ms of
 * GPU time each) so the desktop stays responsive; the run ends by itself on any driver error, device lost,
 * a piece that takes far too long, or a GPU temperature at the limit (junction/hotspot when the card reports
 * one, else the edge temperature). Clocks, voltages and fans are never touched. Stop works at any time. */
#ifndef BENCH_GPUBURN_H
#define BENCH_GPUBURN_H

#include "gpu_backend.h"
#include "sec.h"

#define GPUBURN_MAXDEV 16
#define GPUBURN_ALL (-2)            /* gpuburn_cfg.device: every device of the chosen back end */
#define GPUBURN_AUTO (-1)           /* gpuburn_cfg.backend: Vulkan if it has a device, else the first back end that has one */

/* GPU sensors read from hwmon (amdgpu; best effort for i915, xe, nouveau); -1 = not available */
typedef struct {
    double edge_c, junction_c, mem_c, power_w, clock_mhz, fan_rpm;
} gpu_sensors;

enum { GBS_RUNNING, GBS_DONE, GBS_STOPPED, GBS_ABORTED, GBS_FAILED };   /* per GPU: failed = compute errors found */

typedef struct {
    char label[80], device[256], runtime[200];
    int state;                       /* GBS_* */
    long iterations;                 /* matrix multiplications done */
    long errors;                     /* wrong results found (0 = pass) */
    int n;                           /* matrix side in use */
    double elapsed, gflops, gflops_avg, iter_per_s;
    gpu_sensors now, max;
    char note[240];                  /* why it ended early ("" = ran to the end) */
} gpuburn_dev;

typedef struct {
    int ndev;
    gpuburn_dev dev[GPUBURN_MAXDEV];
    double elapsed, total;           /* seconds so far / planned */
    int running;
} gpuburn_snap;

typedef void (*gpuburn_cb)(const gpuburn_snap *s, void *ud);   /* ~2 times a second, from the thread that called bench_gpuburn */

typedef struct {
    int backend;                     /* GB_*, or GPUBURN_AUTO */
    int device;                      /* index in the back end's list (gpuburn_list), GPUBURN_ALL, or -1 = default (first) */
    int prec;                        /* GP_FP32 (default) or GP_FP64 */
    double seconds;                  /* default 60; 1 .. 86400 */
    double limit_c;                  /* stop at this GPU temperature; default 100 */
    int software_only;               /* tests: refuse anything that is not a software renderer */
    int n_max;                       /* largest matrix side (default 4096; tests use 64) */
    gpuburn_cb cb;
    void *ud;
} gpuburn_cfg;

void gpuburn_cfg_default(gpuburn_cfg *c);
/* the devices of one back end for the pick list (starts that back end's runtime). Returns the count, 0 = none
   (why says why / what to install), -1 = back end missing */
int  gpuburn_list(int backend, gb_info *out, int max, char *why, size_t wn);
const char *gpuburn_stage(void);       /* "GPU Burnin: 12 s of 60 s, 0 errors" while running, "" when idle */
/* run, render into o (may be NULL) and ~/.cache/srs/bench-gpuburn.txt, add a history line.
   0 PASS, 1 stopped by the user (no errors so far), 2 FAIL (compute errors found), -1 not started (why says why),
   -2 ended early by a safety rule (why says which; results so far are shown) */
int  bench_gpuburn(const gpuburn_cfg *c, srs_out *o, volatile int *cancel, char *why, size_t wn);

/* sensors, also usable on their own */
typedef struct gpu_hw gpu_hw;
gpu_hw *gpu_hw_open(const char *pci);                   /* NULL when the card has no hwmon */
void gpu_hw_read(gpu_hw *h, gpu_sensors *s);
void gpu_hw_close(gpu_hw *h);

#endif
