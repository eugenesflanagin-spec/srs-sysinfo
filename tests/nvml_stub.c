/* nvml_stub.c - a stand-in for libnvidia-ml.so.1 for tests/gpu-top-test.sh: the NVML calls GPU Top makes,
 * answering fixed values for one made-up card at PCI 0000:01:00.0. Never talks to hardware.
 * Build: gcc -shared -fPIC -o libnvml_stub.so tests/nvml_stub.c ; use: SRS_TEST_NVML_LIB=/abs/libnvml_stub.so */
#include <stdio.h>
#include <string.h>

typedef void *nvdev;
typedef struct { unsigned gpu, memory; } nv_util;
typedef struct { unsigned long long total, free, used; } nv_mem;
typedef struct { unsigned pid; unsigned long long used; unsigned gi, ci; } nv_proc;

static int inited;
static char card;

int nvmlInit_v2(void) { inited = 1; return 0; }
int nvmlShutdown(void) { inited = 0; return 0; }
int nvmlDeviceGetHandleByPciBusId_v2(const char *pci, nvdev *d)
{
    if (!inited) return 1;
    if (strcasecmp(pci, "0000:01:00.0")) return 6;   /* NVML_ERROR_NOT_FOUND */
    *d = &card;
    return 0;
}
int nvmlDeviceGetName(nvdev d, char *b, unsigned n) { (void)d; snprintf(b, n, "NVIDIA GeForce RTX 4090 (stub)"); return 0; }
int nvmlDeviceGetUUID(nvdev d, char *b, unsigned n) { (void)d; snprintf(b, n, "GPU-00000000-1111-2222-3333-444444444444"); return 0; }
int nvmlSystemGetDriverVersion(char *b, unsigned n) { snprintf(b, n, "999.99"); return 0; }
int nvmlDeviceGetVbiosVersion(nvdev d, char *b, unsigned n) { (void)d; snprintf(b, n, "95.02.18.80.5F"); return 0; }
int nvmlDeviceGetMemoryBusWidth(nvdev d, unsigned *v) { (void)d; *v = 384; return 0; }
int nvmlDeviceGetNumGpuCores(nvdev d, unsigned *v) { (void)d; *v = 16384; return 0; }
int nvmlDeviceGetClockInfo(nvdev d, int t, unsigned *v) { (void)d; *v = t == 0 ? 2520 : t == 2 ? 10501 : 1000; return 0; }
int nvmlDeviceGetMaxClockInfo(nvdev d, int t, unsigned *v) { (void)d; *v = t == 0 ? 3105 : t == 2 ? 10501 : 2000; return 0; }
int nvmlDeviceGetUtilizationRates(nvdev d, nv_util *u) { (void)d; u->gpu = 87; u->memory = 41; return 0; }
int nvmlDeviceGetEncoderUtilization(nvdev d, unsigned *u, unsigned *p) { (void)d; *u = 12; *p = 167000; return 0; }
int nvmlDeviceGetDecoderUtilization(nvdev d, unsigned *u, unsigned *p) { (void)d; *u = 5; *p = 167000; return 0; }
int nvmlDeviceGetMemoryInfo(nvdev d, nv_mem *m)
{
    (void)d;
    m->total = 24ULL << 30; m->used = 6ULL << 30; m->free = m->total - m->used;
    return 0;
}
int nvmlDeviceGetPowerUsage(nvdev d, unsigned *v) { (void)d; *v = 312500; return 0; }
int nvmlDeviceGetEnforcedPowerLimit(nvdev d, unsigned *v) { (void)d; *v = 450000; return 0; }
int nvmlDeviceGetPowerManagementLimitConstraints(nvdev d, unsigned *a, unsigned *b) { (void)d; *a = 150000; *b = 600000; return 0; }
int nvmlDeviceGetPowerManagementDefaultLimit(nvdev d, unsigned *v) { (void)d; *v = 450000; return 0; }
int nvmlDeviceGetTemperature(nvdev d, int s, unsigned *v) { (void)d; (void)s; *v = 66; return 0; }
int nvmlDeviceGetTemperatureThreshold(nvdev d, int t, unsigned *v) { (void)d; *v = t == 0 ? 92 : 87; return 0; }
int nvmlDeviceGetFanSpeed(nvdev d, unsigned *v) { (void)d; *v = 55; return 0; }
int nvmlDeviceGetCurrPcieLinkGeneration(nvdev d, unsigned *v) { (void)d; *v = 4; return 0; }
int nvmlDeviceGetCurrPcieLinkWidth(nvdev d, unsigned *v) { (void)d; *v = 16; return 0; }
int nvmlDeviceGetMaxPcieLinkGeneration(nvdev d, unsigned *v) { (void)d; *v = 4; return 0; }
int nvmlDeviceGetMaxPcieLinkWidth(nvdev d, unsigned *v) { (void)d; *v = 16; return 0; }
int nvmlDeviceGetPcieThroughput(nvdev d, int c, unsigned *v) { (void)d; *v = c == 0 ? 1234 : 5678; return 0; }
int nvmlDeviceGetCurrentClocksEventReasons(nvdev d, unsigned long long *r) { (void)d; *r = 0x4 | 0x20; return 0; }
int nvmlDeviceGetPerformanceState(nvdev d, int *p) { (void)d; *p = 2; return 0; }
int nvmlDeviceGetComputeRunningProcesses_v2(nvdev d, unsigned *n, nv_proc *p)
{
    (void)d;
    if (*n < 1) { *n = 1; return 7; }
    p[0].pid = 1001; p[0].used = 2ULL << 30; p[0].gi = p[0].ci = 0;
    *n = 1;
    return 0;
}
int nvmlDeviceGetGraphicsRunningProcesses_v2(nvdev d, unsigned *n, nv_proc *p)
{
    (void)d;
    if (*n < 2) { *n = 2; return 7; }
    p[0].pid = 1001; p[0].used = 2ULL << 30;   /* the same process: listed once */
    p[1].pid = 1002; p[1].used = 512ULL << 20;
    *n = 2;
    return 0;
}
