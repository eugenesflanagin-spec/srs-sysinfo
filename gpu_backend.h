/* gpu_backend.h - the compute back ends behind GPU Burnin and "GPU Compute: Vulkan vs HIP vs CUDA vs
 * OpenCL" (bench_gpuburn.c, bench_gpucompare.c). Internal to SRS.
 *
 * Four back ends, each opened at run time with dlopen (no build or package dependency):
 *   gpuburn_vk.c   Vulkan compute (gpu_vk.c)               own kernels only
 *   gpuburn_rt.c   ROCm / HIP and NVIDIA CUDA              own kernels (hipRTC / NVRTC) + hipBLAS / cuBLAS
 *   gpuburn_cl.c   OpenCL (every platform is its own entry) own kernels + CLBlast when installed
 * One contract for all: a back end prepares one workload (prep), runs it any number of times (run),
 * and every run says how many results were WRONG; a wrong result is never turned into a speed. */
#ifndef GPU_BACKEND_H
#define GPU_BACKEND_H

#include <stddef.h>

enum { GB_VULKAN, GB_HIP, GB_CUDA, GB_OPENCL, GB_N };
#define GB_LISTMAX 32               /* most devices one list() call returns */
extern const char *const gb_id[GB_N];      /* "vulkan", "hip", "cuda", "opencl" (CLI --backend words) */
extern const char *const gb_name[GB_N];    /* "Vulkan", "ROCm / HIP", "CUDA", "OpenCL" */

enum { GK_GEMM, GK_FMA, GK_COPY, GK_UPLOAD, GK_DOWNLOAD, GK_LATENCY, GK_N };
enum { GP_FP32, GP_FP16, GP_FP64, GP_INT32 };
enum { GV_OWN, GV_LIB };                    /* own kernel / vendor library (hipBLAS, cuBLAS, CLBlast) */

/* what prep returns */
enum { GBP_OK = 0, GBP_UNSUPPORTED = 1, GBP_ERROR = -1, GBP_HUNG = -2, GBP_LOST = -3 };

typedef struct {
    int backend;               /* GB_* */
    int index;                 /* position in this back end's own device list */
    char label[80];            /* column title: "Vulkan", "ROCm / HIP", "CUDA", "CUDA (ZLUDA)", "OpenCL (ROCm)", "OpenCL (Rusticl)" */
    char device[256];          /* device name */
    char runtime[200];         /* driver / runtime version text, recorded in results */
    char pci[16];              /* "0000:03:00.0" when known, else "" */
    char kind[24];             /* "GPU", "CPU", "software renderer" */
    double mem_bytes;          /* device memory, 0 = unknown */
    int is_cpu;                /* a CPU / software device: tests allowed, but marked CPU */
    int has_lib;               /* a vendor GEMM library was found (hipBLAS / cuBLAS / CLBlast) */
    int fp16, fp64;            /* the device can do them (1) / does not say (0) */
    int zluda;                 /* CUDA call translated to AMD by ZLUDA */
} gb_info;

typedef struct gb_ctx gb_ctx;

typedef struct {
    int backend;
    /* how the back end looks on this machine, without starting any GPU runtime where avoidable.
       Returns 1 when its libraries load; hint says what to install when not. */
    int  (*available)(char *hint, size_t hn);
    /* list the devices (this DOES start the runtime: hipInit / cuInit / clGetPlatformIDs / vkCreateInstance) */
    int  (*list)(gb_info *out, int max, char *why, size_t wn);
    int  (*open)(gb_ctx **c, const gb_info *d, int software_only, char *why, size_t wn);
    void (*close)(gb_ctx *c);
    /* prepare a workload. n: GEMM matrix side / FMA grid scale / bytes for COPY, UPLOAD, DOWNLOAD / rounds for LATENCY.
       work_per_run: floating-point operations (GEMM, FMA) or bytes (memory) of ONE run; max_reps: most reps it can take. */
    int  (*prep)(gb_ctx *c, int kind, int prec, int variant, size_t n, double *work_per_run, unsigned *max_reps);
    /* run reps repetitions inside one short submission. secs = device time (host time when no timer).
       wrong = results that did not match (0 = all right). Returns GBP_* (0 ok). */
    int  (*run)(gb_ctx *c, unsigned reps, double *secs, long *wrong);
    void (*release)(gb_ctx *c);          /* free what prep made */
    const char *(*error)(gb_ctx *c);     /* plain-English last error */
} gb_ops;

extern const gb_ops *const gb_table[GB_N];
extern const gb_ops gb_vulkan, gb_hip, gb_cuda, gb_opencl;

/* "library missing" hints, one line each */
extern const char *const gb_hint[GB_N];

/* binding self-test: dlopen + dlsym only, no runtime start. Returns the number of symbols that did not
   resolve (0 = all), -1 when the library is not installed. */
int gb_bind_check(int backend, char *detail, size_t dn);

int gb_rt_bind_check(int cuda, char *detail, size_t dn);     /* gpuburn_rt.c: HIP (0) or CUDA (1) */
int gb_cl_bind_check(char *detail, size_t dn);             /* gpuburn_cl.c */

/* the test mode is on: the caller asked for it (so) or SRS_GPU_SOFTWARE_ONLY=1 is set. Every open() checks this,
   so a test run can never start a real GPU runtime */
int gb_software_only(int so);

/* which device of back end b is the one at PCI address pci (any back end), else -1 */
int gb_find_by_pci(const gb_info *l, int n, const char *pci);

/* ---- kernels that HIP, CUDA and OpenCL share (gpu_kernels.h) ---- */
#endif
