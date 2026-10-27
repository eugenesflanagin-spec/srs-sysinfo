/* gpu_vk.c - the GPU benchmarks' shared Vulkan layer (see gpu_vk.h). Written for SRS.
 * Loader opened at run time; every core 1.0/1.1 entry point is taken straight from the loader's
 * exports with dlsym. One device, one queue, one command buffer, at most one submission in flight,
 * every wait bounded by a watchdog. A submission that does not finish in time leaves the device
 * "abandoned": nothing is destroyed or freed afterwards, because the GPU may still be using it. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "gpu_vk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __linux__
#include <dlfcn.h>
#include <pthread.h>

#define VKX(F) \
    F(vki_Result, CreateInstance, (const vki_InstanceCreateInfo *, const void *, vki_Instance *)) \
    F(void, DestroyInstance, (vki_Instance, const void *)) \
    F(vki_Result, EnumerateInstanceVersion, (uint32_t *)) \
    F(vki_Result, EnumeratePhysicalDevices, (vki_Instance, uint32_t *, vki_PhysicalDevice *)) \
    F(void, GetPhysicalDeviceProperties, (vki_PhysicalDevice, vki_PhysicalDeviceProperties *)) \
    F(void, GetPhysicalDeviceProperties2, (vki_PhysicalDevice, vki_PhysicalDeviceProperties2 *)) \
    F(void, GetPhysicalDeviceFeatures2, (vki_PhysicalDevice, vki_PhysicalDeviceFeatures2 *)) \
    F(void, GetPhysicalDeviceMemoryProperties, (vki_PhysicalDevice, vki_PhysicalDeviceMemoryProperties *)) \
    F(void, GetPhysicalDeviceQueueFamilyProperties, (vki_PhysicalDevice, uint32_t *, vki_QueueFamilyProperties *)) \
    F(vki_Result, EnumerateDeviceExtensionProperties, (vki_PhysicalDevice, const char *, uint32_t *, vki_ExtensionProperties *)) \
    F(vki_Result, CreateDevice, (vki_PhysicalDevice, const vki_DeviceCreateInfo *, const void *, vki_Device *)) \
    F(void, DestroyDevice, (vki_Device, const void *)) \
    F(void, GetDeviceQueue, (vki_Device, uint32_t, uint32_t, vki_Queue *)) \
    F(vki_Result, DeviceWaitIdle, (vki_Device)) \
    F(vki_Result, CreateBuffer, (vki_Device, const vki_BufferCreateInfo *, const void *, vki_Buffer *)) \
    F(void, DestroyBuffer, (vki_Device, vki_Buffer, const void *)) \
    F(void, GetBufferMemoryRequirements, (vki_Device, vki_Buffer, vki_MemoryRequirements *)) \
    F(vki_Result, AllocateMemory, (vki_Device, const vki_MemoryAllocateInfo *, const void *, vki_DeviceMemory *)) \
    F(void, FreeMemory, (vki_Device, vki_DeviceMemory, const void *)) \
    F(vki_Result, BindBufferMemory, (vki_Device, vki_Buffer, vki_DeviceMemory, vki_DeviceSize)) \
    F(vki_Result, MapMemory, (vki_Device, vki_DeviceMemory, vki_DeviceSize, vki_DeviceSize, vki_Flags, void **)) \
    F(void, UnmapMemory, (vki_Device, vki_DeviceMemory)) \
    F(vki_Result, CreateShaderModule, (vki_Device, const vki_ShaderModuleCreateInfo *, const void *, vki_ShaderModule *)) \
    F(void, DestroyShaderModule, (vki_Device, vki_ShaderModule, const void *)) \
    F(vki_Result, CreateDescriptorSetLayout, (vki_Device, const vki_DescriptorSetLayoutCreateInfo *, const void *, vki_DescriptorSetLayout *)) \
    F(void, DestroyDescriptorSetLayout, (vki_Device, vki_DescriptorSetLayout, const void *)) \
    F(vki_Result, CreatePipelineLayout, (vki_Device, const vki_PipelineLayoutCreateInfo *, const void *, vki_PipelineLayout *)) \
    F(void, DestroyPipelineLayout, (vki_Device, vki_PipelineLayout, const void *)) \
    F(vki_Result, CreateComputePipelines, (vki_Device, vki_PipelineCache, uint32_t, const vki_ComputePipelineCreateInfo *, const void *, vki_Pipeline *)) \
    F(void, DestroyPipeline, (vki_Device, vki_Pipeline, const void *)) \
    F(vki_Result, CreateDescriptorPool, (vki_Device, const vki_DescriptorPoolCreateInfo *, const void *, vki_DescriptorPool *)) \
    F(void, DestroyDescriptorPool, (vki_Device, vki_DescriptorPool, const void *)) \
    F(vki_Result, AllocateDescriptorSets, (vki_Device, const vki_DescriptorSetAllocateInfo *, vki_DescriptorSet *)) \
    F(void, UpdateDescriptorSets, (vki_Device, uint32_t, const vki_WriteDescriptorSet *, uint32_t, const void *)) \
    F(vki_Result, CreateCommandPool, (vki_Device, const vki_CommandPoolCreateInfo *, const void *, vki_CommandPool *)) \
    F(void, DestroyCommandPool, (vki_Device, vki_CommandPool, const void *)) \
    F(vki_Result, AllocateCommandBuffers, (vki_Device, const vki_CommandBufferAllocateInfo *, vki_CommandBuffer *)) \
    F(vki_Result, BeginCommandBuffer, (vki_CommandBuffer, const vki_CommandBufferBeginInfo *)) \
    F(vki_Result, EndCommandBuffer, (vki_CommandBuffer)) \
    F(vki_Result, ResetCommandBuffer, (vki_CommandBuffer, vki_Flags)) \
    F(void, CmdBindPipeline, (vki_CommandBuffer, int32_t, vki_Pipeline)) \
    F(void, CmdBindDescriptorSets, (vki_CommandBuffer, int32_t, vki_PipelineLayout, uint32_t, uint32_t, const vki_DescriptorSet *, uint32_t, const uint32_t *)) \
    F(void, CmdPushConstants, (vki_CommandBuffer, vki_PipelineLayout, vki_Flags, uint32_t, uint32_t, const void *)) \
    F(void, CmdDispatch, (vki_CommandBuffer, uint32_t, uint32_t, uint32_t)) \
    F(void, CmdPipelineBarrier, (vki_CommandBuffer, vki_Flags, vki_Flags, vki_Flags, uint32_t, const vki_MemoryBarrier *, uint32_t, const void *, uint32_t, const void *)) \
    F(void, CmdCopyBuffer, (vki_CommandBuffer, vki_Buffer, vki_Buffer, uint32_t, const vki_BufferCopy *)) \
    F(void, CmdFillBuffer, (vki_CommandBuffer, vki_Buffer, vki_DeviceSize, vki_DeviceSize, uint32_t)) \
    F(void, CmdResetQueryPool, (vki_CommandBuffer, vki_QueryPool, uint32_t, uint32_t)) \
    F(void, CmdWriteTimestamp, (vki_CommandBuffer, vki_Flags, vki_QueryPool, uint32_t)) \
    F(vki_Result, CreateQueryPool, (vki_Device, const vki_QueryPoolCreateInfo *, const void *, vki_QueryPool *)) \
    F(void, DestroyQueryPool, (vki_Device, vki_QueryPool, const void *)) \
    F(vki_Result, GetQueryPoolResults, (vki_Device, vki_QueryPool, uint32_t, uint32_t, size_t, void *, vki_DeviceSize, vki_Flags)) \
    F(vki_Result, CreateFence, (vki_Device, const vki_FenceCreateInfo *, const void *, vki_Fence *)) \
    F(void, DestroyFence, (vki_Device, vki_Fence, const void *)) \
    F(vki_Result, ResetFences, (vki_Device, uint32_t, const vki_Fence *)) \
    F(vki_Result, WaitForFences, (vki_Device, uint32_t, const vki_Fence *, vki_Bool32, uint64_t)) \
    F(vki_Result, QueueSubmit, (vki_Queue, uint32_t, const vki_SubmitInfo *, vki_Fence))

#define VKX_DECL(r, n, a) static r (*fvk##n) a;
VKX(VKX_DECL)

static void *g_lib;
static int g_load_rc;   /* 0 not tried, 1 ok, -1 failed */
static char g_load_why[200];
static pthread_mutex_t g_load_mu = PTHREAD_MUTEX_INITIALIZER;

double gvk_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

void gpu_quiet_layers(void)
{
    if (!getenv("VK_LOADER_LAYERS_DISABLE")) setenv("VK_LOADER_LAYERS_DISABLE", "~implicit~", 0);
    if (!getenv("DISABLE_MANGOHUD")) setenv("DISABLE_MANGOHUD", "1", 0);
}

int gvk_load(char *why, size_t wn)
{
    pthread_mutex_lock(&g_load_mu);
    if (!g_load_rc) {
        const char *name = getenv("SRS_VULKAN_LIB");   /* tests: point at a missing library */
        g_load_rc = -1;
        gpu_quiet_layers();
        if (!(g_lib = dlopen(name && *name ? name : "libvulkan.so.1", RTLD_NOW | RTLD_LOCAL)))
            snprintf(g_load_why, sizeof g_load_why, "the Vulkan loader (libvulkan.so.1) is not installed");
        else {
            const char *miss = NULL;
#define VKX_LOAD(r, n, a) if (!(*(void **)&fvk##n = dlsym(g_lib, "vk" #n)) && !miss) miss = "vk" #n;
            VKX(VKX_LOAD)
            if (miss) snprintf(g_load_why, sizeof g_load_why, "the Vulkan loader is too old (no %s; Vulkan 1.1 is needed)", miss);
            else g_load_rc = 1;
        }
    }
    if (why && wn) snprintf(why, wn, "%s", g_load_rc > 0 ? "" : g_load_why);
    pthread_mutex_unlock(&g_load_mu);
    return g_load_rc > 0;
}

int gpu_available(char *why, size_t wn) { return gvk_load(why, wn); }

static const char *g_claim;
static pthread_mutex_t g_claim_mu = PTHREAD_MUTEX_INITIALIZER;

int gpu_claim(const char *name, const char **who)
{
    int ok;
    pthread_mutex_lock(&g_claim_mu);
    ok = !g_claim;
    if (ok) g_claim = name;
    if (who) *who = g_claim;
    pthread_mutex_unlock(&g_claim_mu);
    return ok;
}

int gpu_busy(void)
{
    int b;
    pthread_mutex_lock(&g_claim_mu);
    b = g_claim != NULL;
    pthread_mutex_unlock(&g_claim_mu);
    return b;
}

const char *gpu_claim_holder(void)
{
    const char *w;
    pthread_mutex_lock(&g_claim_mu);
    w = g_claim;
    pthread_mutex_unlock(&g_claim_mu);
    return w;
}

void gpu_release(void)
{
    pthread_mutex_lock(&g_claim_mu);
    g_claim = NULL;
    pthread_mutex_unlock(&g_claim_mu);
}

/* a GPU driver call never returned: the claim stays for the rest of the process, under a name that says why */
void gpu_claim_hung(void)
{
    pthread_mutex_lock(&g_claim_mu);
    g_claim = GPU_CLAIM_HUNG;
    pthread_mutex_unlock(&g_claim_mu);
}

void gpu_claim_text(const char *who, char *buf, size_t n)
{
    if (who && !strcmp(who, GPU_CLAIM_HUNG))
        snprintf(buf, n, "a GPU call did not return earlier; restart SRS (or the PC) before the next GPU test");
    else
        snprintf(buf, n, "%s is running; wait for it to finish", who ? who : "another GPU test");
}

const char *gvk_result_str(vki_Result r)
{
    switch (r) {
    case VKI_SUCCESS: return "ok";
    case VKI_TIMEOUT: return "timed out";
    case VKI_ERROR_OUT_OF_HOST_MEMORY: return "out of system memory";
    case VKI_ERROR_OUT_OF_DEVICE_MEMORY: return "out of video memory";
    case VKI_ERROR_INITIALIZATION_FAILED: return "the driver could not start";
    case VKI_ERROR_DEVICE_LOST: return "the GPU stopped answering (device lost)";
    case VKI_ERROR_INCOMPATIBLE_DRIVER: return "no compatible Vulkan driver";
    default: return "Vulkan error";
    }
}

static int make_instance(vki_Instance *inst, char *why, size_t wn)
{
    uint32_t ver = 0;
    vki_ApplicationInfo ai = { VKI_ST_APPLICATION_INFO, NULL, "SRS", 1, "SRS", 1, (1u << 22) | (1u << 12) };
    vki_InstanceCreateInfo ci = { VKI_ST_INSTANCE_CREATE_INFO, NULL, 0, &ai, 0, NULL, 0, NULL };
    vki_Result r;
    char none[8];
    if (!why || !wn) { why = none; wn = sizeof none; }   /* why may be NULL */
    if (!gvk_load(why, wn)) return -1;
    if (fvkEnumerateInstanceVersion(&ver) || ver < ((1u << 22) | (1u << 12))) {
        snprintf(why, wn, "the Vulkan loader supports only Vulkan 1.0; 1.1 is needed");
        return -1;
    }
    if ((r = fvkCreateInstance(&ci, NULL, inst))) {
        snprintf(why, wn, "Vulkan did not start: %s (%d)", gvk_result_str(r), (int)r);
        return -1;
    }
    return 0;
}

static int has_ext(const vki_ExtensionProperties *e, uint32_t n, const char *name)
{
    uint32_t i;
    for (i = 0; i < n; i++) if (!strcmp(e[i].extensionName, name)) return 1;
    return 0;
}

/* properties of one physical device into d; returns the extension list (caller frees) */
static vki_ExtensionProperties *describe(vki_PhysicalDevice p, gpu_dev *d, uint32_t *next, int *qfam, uint32_t *tsbits)
{
    static const char *const tnames[5] = { "other", "integrated GPU", "discrete GPU", "virtual GPU", "software renderer (CPU)" };
    vki_PhysicalDeviceProperties pp;
    vki_PhysicalDeviceProperties2 p2;
    vki_PhysicalDeviceDriverProperties drv;
    vki_PhysicalDevicePCIBusInfoPropertiesEXT pci;
    vki_PhysicalDeviceSubgroupProperties sg;
    vki_PhysicalDeviceSubgroupSizeControlProperties sgc;
    vki_PhysicalDeviceShaderCorePropertiesAMD amd;
    vki_PhysicalDeviceShaderSMBuiltinsPropertiesNV nv;
    vki_PhysicalDeviceFeatures2 f2;
    vki_PhysicalDeviceShaderFloat16Int8Features f16;
    vki_PhysicalDeviceMemoryProperties mp;
    vki_QueueFamilyProperties qf[32];
    vki_ExtensionProperties *ext = NULL;
    uint32_t n = 0, i, nq = 32;
    void *chain = NULL;
    int api12;
    memset(d, 0, sizeof *d);
    fvkGetPhysicalDeviceProperties(p, &pp);
    if (!fvkEnumerateDeviceExtensionProperties(p, NULL, &n, NULL) && n && (ext = calloc(n, sizeof *ext)))
        if (fvkEnumerateDeviceExtensionProperties(p, NULL, &n, ext) < 0) n = 0;
    if (!ext) n = 0;
    *next = n;
    api12 = pp.apiVersion >= ((1u << 22) | (2u << 12));
    memset(&drv, 0, sizeof drv); memset(&pci, 0, sizeof pci); memset(&sg, 0, sizeof sg); memset(&sgc, 0, sizeof sgc);
    memset(&amd, 0, sizeof amd); memset(&nv, 0, sizeof nv);
    sg.sType = VKI_ST_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
    sg.pNext = chain; chain = &sg;
    if (api12 || has_ext(ext, n, "VK_KHR_driver_properties")) { drv.sType = VKI_ST_PHYSICAL_DEVICE_DRIVER_PROPERTIES; drv.pNext = chain; chain = &drv; }
    if (has_ext(ext, n, "VK_EXT_pci_bus_info")) { pci.sType = VKI_ST_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT; pci.pNext = chain; chain = &pci; }
    if (pp.apiVersion >= ((1u << 22) | (3u << 12)) || has_ext(ext, n, "VK_EXT_subgroup_size_control")) {
        sgc.sType = VKI_ST_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES; sgc.pNext = chain; chain = &sgc;
    }
    if (has_ext(ext, n, "VK_AMD_shader_core_properties")) { amd.sType = VKI_ST_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_AMD; amd.pNext = chain; chain = &amd; }
    if (has_ext(ext, n, "VK_NV_shader_sm_builtins")) { nv.sType = VKI_ST_PHYSICAL_DEVICE_SHADER_SM_BUILTINS_PROPERTIES_NV; nv.pNext = chain; chain = &nv; }
    memset(&p2, 0, sizeof p2);
    p2.sType = VKI_ST_PHYSICAL_DEVICE_PROPERTIES_2;
    p2.pNext = chain;
    fvkGetPhysicalDeviceProperties2(p, &p2);
    memset(&f16, 0, sizeof f16);
    memset(&f2, 0, sizeof f2);
    f2.sType = VKI_ST_PHYSICAL_DEVICE_FEATURES_2;
    if (api12 || has_ext(ext, n, "VK_KHR_shader_float16_int8")) { f16.sType = VKI_ST_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES; f2.pNext = &f16; }
    fvkGetPhysicalDeviceFeatures2(p, &f2);
    fvkGetPhysicalDeviceMemoryProperties(p, &mp);
    fvkGetPhysicalDeviceQueueFamilyProperties(p, &nq, qf);

    snprintf(d->name, sizeof d->name, "%s", pp.deviceName);
    if (drv.sType && drv.driverName[0])
        snprintf(d->driver, sizeof d->driver, "%.100s%s%.190s", drv.driverName, drv.driverInfo[0] ? ", " : "", drv.driverInfo);
    snprintf(d->api, sizeof d->api, "%u.%u.%u", pp.apiVersion >> 22, (pp.apiVersion >> 12) & 0x3ff, pp.apiVersion & 0xfff);
    d->dtype = pp.deviceType >= 0 && pp.deviceType <= 4 ? pp.deviceType : 0;
    d->type = tnames[d->dtype];
    d->vendor_id = pp.vendorID;
    d->device_id = pp.deviceID;
    if (pci.sType) snprintf(d->pci, sizeof d->pci, "%04x:%02x:%02x.%x", pci.pciDomain & 0xffff, pci.pciBus & 0xff, pci.pciDevice & 0x1f, pci.pciFunction & 7);
    for (i = 0; i < mp.memoryHeapCount && i < 16; i++) {
        if (mp.memoryHeaps[i].flags & 1) d->vram_bytes += (double)mp.memoryHeaps[i].size;
        else d->host_bytes += (double)mp.memoryHeaps[i].size;
    }
    d->heaps = (int)mp.memoryHeapCount;
    d->subgroup = (int)sg.subgroupSize;
    if (sgc.sType) { d->sg_min = (int)sgc.minSubgroupSize; d->sg_max = (int)sgc.maxSubgroupSize; }
    if (amd.sType && amd.shaderEngineCount) {
        d->cus = (int)(amd.shaderEngineCount * amd.shaderArraysPerEngineCount * amd.computeUnitsPerShaderArray);
        d->cu_word = "compute units";
    } else if (nv.sType && nv.shaderSMCount) {
        d->cus = (int)nv.shaderSMCount;
        d->cu_word = "SMs";
    }
    d->fp16 = f16.sType && f16.shaderFloat16;
    d->fp64 = f2.features.f[VKI_FEATURE_SHADER_FLOAT64] != 0;
    /* the queue: compute + timestamps, a graphics+compute family first (it is the one every driver times well) */
    *qfam = -1;
    *tsbits = 0;
    for (i = 0; i < nq; i++)
        if ((qf[i].queueFlags & 2) && (qf[i].queueFlags & 1) && qf[i].timestampValidBits) { *qfam = (int)i; break; }
    if (*qfam < 0) for (i = 0; i < nq; i++) if ((qf[i].queueFlags & 2) && qf[i].timestampValidBits) { *qfam = (int)i; break; }
    if (*qfam < 0) for (i = 0; i < nq; i++) if (qf[i].queueFlags & 2) { *qfam = (int)i; break; }
    if (*qfam >= 0) *tsbits = qf[*qfam].timestampValidBits;
    d->timestamps = *tsbits > 0 && pp.limits.timestampPeriod > 0;
    return ext;
}

int gpu_list(gpu_dev *d, int max, char *why, size_t wn)
{
    vki_Instance inst;
    vki_PhysicalDevice pd[GPU_MAXDEV];
    uint32_t n = GPU_MAXDEV, i;
    char w[200];
    if (make_instance(&inst, w, sizeof w)) { if (why) snprintf(why, wn, "%s", w); return -1; }
    if (fvkEnumeratePhysicalDevices(inst, &n, pd) < 0) n = 0;
    for (i = 0; i < n && (int)i < max; i++) {
        uint32_t ne, tb;
        int qf;
        free(describe(pd[i], &d[i], &ne, &qf, &tb));
    }
    fvkDestroyInstance(inst, NULL);
    if (why && wn) snprintf(why, wn, "%s", n ? "" : "Vulkan found no device (no Vulkan driver installed for this GPU?)");
    return (int)(n < (uint32_t)max ? n : (uint32_t)max);
}

int gpu_default_device(const gpu_dev *d, int n)
{
    int i;
    for (i = 0; i < n; i++) if (d[i].dtype == GPU_DISCRETE) return i;
    for (i = 0; i < n; i++) if (d[i].dtype == GPU_INTEGRATED) return i;
    return n > 0 ? 0 : -1;
}

/* ---------------------------------------------------------------- device */
int gvk_open(gvk_dev *g, int idx, int software_only, char *why, size_t wn)
{
    vki_PhysicalDevice pd[GPU_MAXDEV];
    vki_ExtensionProperties *ext;
    uint32_t n = GPU_MAXDEV, ne = 0, tsbits = 0;
    int qfam = -1, api12;
    const char *exts[2];
    uint32_t nexts = 0;
    float prio = 1.0f;
    vki_PhysicalDeviceProperties pp;
    vki_PhysicalDeviceFeatures2 f2;
    vki_PhysicalDeviceShaderFloat16Int8Features f16;
    vki_Result r;
    const char *so = getenv("SRS_GPU_SOFTWARE_ONLY");
    char none[8];
    if (!why || !wn) { why = none; wn = sizeof none; }   /* why may be NULL */
    memset(g, 0, sizeof *g);
    g->watchdog_s = 5.0;
    if (so && *so == '1') software_only = 1;
    if (make_instance(&g->inst, why, wn)) return -1;
    if (fvkEnumeratePhysicalDevices(g->inst, &n, pd) < 0 || !n) {
        snprintf(why, wn, "Vulkan found no device");
        goto fail;
    }
    if (idx < 0) {
        gpu_dev all[GPU_MAXDEV];
        uint32_t i;
        for (i = 0; i < n; i++) { uint32_t a, b; int q; free(describe(pd[i], &all[i], &a, &q, &b)); }
        idx = gpu_default_device(all, (int)n);
    }
    if (idx < 0 || (uint32_t)idx >= n) { snprintf(why, wn, "there is no Vulkan device %d (found %u)", idx, n); goto fail; }
    g->phys = pd[idx];
    ext = describe(g->phys, &g->info, &ne, &qfam, &tsbits);
    if (software_only && g->info.dtype != GPU_CPU) {
        snprintf(why, wn, "test mode: %s is not a software renderer, refused", g->info.name);
        free(ext);
        goto fail;
    }
    if (qfam < 0) { snprintf(why, wn, "%s has no compute queue", g->info.name); free(ext); goto fail; }
    fvkGetPhysicalDeviceProperties(g->phys, &pp);
    if (pp.apiVersion < ((1u << 22) | (1u << 12))) { snprintf(why, wn, "%s supports only Vulkan 1.0; 1.1 is needed", g->info.name); free(ext); goto fail; }
    api12 = pp.apiVersion >= ((1u << 22) | (2u << 12));
    g->qfam = (uint32_t)qfam;
    g->ts_bits = tsbits;
    g->ts_period = pp.limits.timestampPeriod;
    g->timestamps = g->info.timestamps;
    g->max_groups = pp.limits.maxComputeWorkGroupCount[0];
    g->max_storage = pp.limits.maxStorageBufferRange;
    memset(&f16, 0, sizeof f16);
    memset(&f2, 0, sizeof f2);
    f2.sType = VKI_ST_PHYSICAL_DEVICE_FEATURES_2;
    if (g->info.fp16) {
        f16.sType = VKI_ST_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;
        f16.shaderFloat16 = 1;
        f2.pNext = &f16;
        if (!api12) exts[nexts++] = "VK_KHR_shader_float16_int8";
    }
    if (g->info.fp64) f2.features.f[VKI_FEATURE_SHADER_FLOAT64] = 1;
    free(ext);
    g->fp16 = g->info.fp16;
    g->fp64 = g->info.fp64;
    {
        vki_DeviceQueueCreateInfo qi = { VKI_ST_DEVICE_QUEUE_CREATE_INFO, NULL, 0, g->qfam, 1, &prio };
        vki_DeviceCreateInfo di = { VKI_ST_DEVICE_CREATE_INFO, &f2, 0, 1, &qi, 0, NULL, nexts, exts, NULL };
        if ((r = fvkCreateDevice(g->phys, &di, NULL, &g->dev))) {
            snprintf(why, wn, "could not open %s: %s (%d)", g->info.name, gvk_result_str(r), (int)r);
            g->dev = NULL;
            goto fail;
        }
    }
    fvkGetDeviceQueue(g->dev, g->qfam, 0, &g->queue);
    fvkGetPhysicalDeviceMemoryProperties(g->phys, &g->mp);
    {
        vki_DescriptorSetLayoutBinding b[GVK_BINDINGS];
        vki_DescriptorSetLayoutCreateInfo lci = { VKI_ST_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, GVK_BINDINGS, b };
        vki_PushConstantRange pr = { 0x20, 0, GVK_PUSH_BYTES };
        vki_PipelineLayoutCreateInfo plci = { VKI_ST_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, NULL, 1, &pr };
        vki_DescriptorPoolSize ps = { 7, 256 * GVK_BINDINGS };
        vki_DescriptorPoolCreateInfo dpci = { VKI_ST_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 256, 1, &ps };
        vki_CommandPoolCreateInfo cpci = { VKI_ST_COMMAND_POOL_CREATE_INFO, NULL, 2, g->qfam };
        vki_CommandBufferAllocateInfo cbai = { VKI_ST_COMMAND_BUFFER_ALLOCATE_INFO, NULL, 0, 0, 1 };
        vki_FenceCreateInfo fci = { VKI_ST_FENCE_CREATE_INFO, NULL, 0 };
        vki_QueryPoolCreateInfo qpci = { VKI_ST_QUERY_POOL_CREATE_INFO, NULL, 0, 2, 2, 0 };
        int i;
        for (i = 0; i < GVK_BINDINGS; i++) { b[i].binding = (uint32_t)i; b[i].descriptorType = 7; b[i].descriptorCount = 1; b[i].stageFlags = 0x20; b[i].pImmutableSamplers = NULL; }
        if ((r = fvkCreateDescriptorSetLayout(g->dev, &lci, NULL, &g->dsl))) goto vkfail;
        plci.pSetLayouts = &g->dsl;
        if ((r = fvkCreatePipelineLayout(g->dev, &plci, NULL, &g->pl))) goto vkfail;
        if ((r = fvkCreateDescriptorPool(g->dev, &dpci, NULL, &g->pool))) goto vkfail;
        if ((r = fvkCreateCommandPool(g->dev, &cpci, NULL, &g->cpool))) goto vkfail;
        cbai.commandPool = g->cpool;
        if ((r = fvkAllocateCommandBuffers(g->dev, &cbai, &g->cb))) goto vkfail;
        if ((r = fvkCreateFence(g->dev, &fci, NULL, &g->fence))) goto vkfail;
        if (g->timestamps && fvkCreateQueryPool(g->dev, &qpci, NULL, &g->qp)) { g->timestamps = 0; g->qp = 0; }
    }
    return 0;
vkfail:
    snprintf(why, wn, "could not set up %s: %s (%d)", g->info.name, gvk_result_str(r), (int)r);
fail:
    gvk_close(g);
    return -1;
}

void gvk_close(gvk_dev *g)
{
    if (g->abandoned) return;   /* GPU work may still be running: leave everything as it is */
    if (g->dev) {
        if (g->lost) fvkDeviceWaitIdle(g->dev);
        if (g->qp) fvkDestroyQueryPool(g->dev, g->qp, NULL);
        if (g->fence) fvkDestroyFence(g->dev, g->fence, NULL);
        if (g->cpool) fvkDestroyCommandPool(g->dev, g->cpool, NULL);
        if (g->pool) fvkDestroyDescriptorPool(g->dev, g->pool, NULL);
        if (g->pl) fvkDestroyPipelineLayout(g->dev, g->pl, NULL);
        if (g->dsl) fvkDestroyDescriptorSetLayout(g->dev, g->dsl, NULL);
        fvkDestroyDevice(g->dev, NULL);
    }
    if (g->inst) fvkDestroyInstance(g->inst, NULL);
    g->dev = NULL;
    g->inst = NULL;
}

/* ---------------------------------------------------------------- buffers, pipelines */
static int pick_type(gvk_dev *g, uint32_t bits, int home)
{
    int i, best = -1, bs = -1000;
    for (i = 0; i < (int)g->mp.memoryTypeCount; i++) {
        vki_Flags f = g->mp.memoryTypes[i].propertyFlags;
        int s = 0;
        if (!(bits & (1u << i))) continue;
        if (home == GVK_DEVICE) s = (f & 1 ? 8 : 0) - (f & 2 ? 2 : 0);
        else {
            if ((f & 6) != 6) continue;   /* host-visible + coherent */
            s = (f & 1 ? -4 : 0) + (home == GVK_READBACK && (f & 8) ? 2 : 0);
        }
        if (s > bs) { bs = s; best = i; }
    }
    return best;
}

int gvk_buf_new(gvk_dev *g, gvk_buf *b, size_t bytes, int home)
{
    vki_BufferCreateInfo bci = { VKI_ST_BUFFER_CREATE_INFO, NULL, 0, bytes, 0x20 | 0x1 | 0x2, 0, 0, NULL };
    vki_MemoryRequirements mr;
    vki_MemoryAllocateInfo mai = { VKI_ST_MEMORY_ALLOCATE_INFO, NULL, 0, 0 };
    vki_Result r;
    int t;
    memset(b, 0, sizeof *b);
    b->size = bytes;
    if ((r = fvkCreateBuffer(g->dev, &bci, NULL, &b->buf))) goto fail;
    fvkGetBufferMemoryRequirements(g->dev, b->buf, &mr);
    if ((t = pick_type(g, mr.memoryTypeBits, home)) < 0) { r = VKI_ERROR_OUT_OF_DEVICE_MEMORY; goto fail; }
    b->device_local = (g->mp.memoryTypes[t].propertyFlags & 1) != 0;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = (uint32_t)t;
    if ((r = fvkAllocateMemory(g->dev, &mai, NULL, &b->mem))) goto fail;
    if ((r = fvkBindBufferMemory(g->dev, b->buf, b->mem, 0))) goto fail;
    if (home != GVK_DEVICE && (r = fvkMapMemory(g->dev, b->mem, 0, bytes, 0, &b->map))) goto fail;
    return 0;
fail:
    snprintf(g->err, sizeof g->err, "could not get %.0f MB of %s memory: %s", (double)bytes / 1e6, home == GVK_DEVICE ? "video" : "host", gvk_result_str(r));
    gvk_buf_free(g, b);
    return -1;
}

void gvk_buf_free(gvk_dev *g, gvk_buf *b)
{
    if (g->abandoned || !g->dev) return;
    if (b->map) fvkUnmapMemory(g->dev, b->mem);
    if (b->buf) fvkDestroyBuffer(g->dev, b->buf, NULL);
    if (b->mem) fvkFreeMemory(g->dev, b->mem, NULL);
    memset(b, 0, sizeof *b);
}

vki_Pipeline gvk_pipeline(gvk_dev *g, const uint32_t *spv, size_t bytes)
{
    vki_ShaderModuleCreateInfo sci = { VKI_ST_SHADER_MODULE_CREATE_INFO, NULL, 0, bytes, spv };
    vki_ComputePipelineCreateInfo cpi;
    vki_ShaderModule sm = 0;
    vki_Pipeline p = 0;
    vki_Result r;
    if ((r = fvkCreateShaderModule(g->dev, &sci, NULL, &sm))) {
        snprintf(g->err, sizeof g->err, "the driver refused a compute shader: %s (%d)", gvk_result_str(r), (int)r);
        return 0;
    }
    memset(&cpi, 0, sizeof cpi);
    cpi.sType = VKI_ST_COMPUTE_PIPELINE_CREATE_INFO;
    cpi.stage.sType = VKI_ST_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage = 0x20;
    cpi.stage.module = sm;
    cpi.stage.pName = "main";
    cpi.layout = g->pl;
    cpi.basePipelineIndex = -1;
    if ((r = fvkCreateComputePipelines(g->dev, 0, 1, &cpi, NULL, &p))) {
        snprintf(g->err, sizeof g->err, "the driver could not compile a compute shader: %s (%d)", gvk_result_str(r), (int)r);
        p = 0;
    }
    fvkDestroyShaderModule(g->dev, sm, NULL);
    return p;
}

void gvk_pipeline_free(gvk_dev *g, vki_Pipeline p)
{
    if (p && g->dev && !g->abandoned) fvkDestroyPipeline(g->dev, p, NULL);
}

vki_DescriptorSet gvk_set(gvk_dev *g, gvk_buf *const *b, int n)
{
    vki_DescriptorSetAllocateInfo ai = { VKI_ST_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, g->pool, 1, &g->dsl };
    vki_DescriptorSet s = 0;
    vki_DescriptorBufferInfo bi[GVK_BINDINGS];
    vki_WriteDescriptorSet w[GVK_BINDINGS];
    int i;
    if (fvkAllocateDescriptorSets(g->dev, &ai, &s)) { snprintf(g->err, sizeof g->err, "out of descriptor sets"); return 0; }
    /* unused bindings point at the first buffer, so every binding is valid */
    for (i = 0; i < GVK_BINDINGS; i++) {
        gvk_buf *x = b[i < n ? i : 0];
        bi[i].buffer = x->buf; bi[i].offset = 0; bi[i].range = x->size;
        memset(&w[i], 0, sizeof w[i]);
        w[i].sType = VKI_ST_WRITE_DESCRIPTOR_SET; w[i].dstSet = s; w[i].dstBinding = (uint32_t)i;
        w[i].descriptorCount = 1; w[i].descriptorType = 7; w[i].pBufferInfo = &bi[i];
    }
    fvkUpdateDescriptorSets(g->dev, GVK_BINDINGS, w, 0, NULL);
    return s;
}

/* ---------------------------------------------------------------- recording and submitting */
int gvk_begin(gvk_dev *g)
{
    vki_CommandBufferBeginInfo bi = { VKI_ST_COMMAND_BUFFER_BEGIN_INFO, NULL, 1, NULL };
    vki_Result r;
    if (g->abandoned || g->lost) return -1;
    if ((r = fvkResetCommandBuffer(g->cb, 0)) || (r = fvkBeginCommandBuffer(g->cb, &bi))) {
        snprintf(g->err, sizeof g->err, "could not record GPU commands: %s", gvk_result_str(r));
        return -1;
    }
    if (g->timestamps) {
        fvkCmdResetQueryPool(g->cb, g->qp, 0, 2);
        fvkCmdWriteTimestamp(g->cb, 0x1, g->qp, 0);
    }
    g->recording = 1;
    return 0;
}

void gvk_dispatch(gvk_dev *g, vki_Pipeline p, vki_DescriptorSet s, const void *push, uint32_t pn, uint32_t groups)
{
    fvkCmdBindPipeline(g->cb, 1, p);
    fvkCmdBindDescriptorSets(g->cb, 1, g->pl, 0, 1, &s, 0, NULL);
    if (push && pn) fvkCmdPushConstants(g->cb, g->pl, 0x20, 0, pn, push);
    fvkCmdDispatch(g->cb, groups, 1, 1);
}

void gvk_dispatch2(gvk_dev *g, vki_Pipeline p, vki_DescriptorSet s, const void *push, uint32_t pn, uint32_t gx, uint32_t gy)
{
    fvkCmdBindPipeline(g->cb, 1, p);
    fvkCmdBindDescriptorSets(g->cb, 1, g->pl, 0, 1, &s, 0, NULL);
    if (push && pn) fvkCmdPushConstants(g->cb, g->pl, 0x20, 0, pn, push);
    fvkCmdDispatch(g->cb, gx, gy, 1);
}

void gvk_barrier(gvk_dev *g)
{
    vki_MemoryBarrier mb = { VKI_ST_MEMORY_BARRIER, NULL, 0x40 | 0x1000, 0x20 | 0x40 | 0x800 | 0x1000 | 0x2000 };
    fvkCmdPipelineBarrier(g->cb, 0x800 | 0x1000, 0x800 | 0x1000 | 0x4000, 0, 1, &mb, 0, NULL, 0, NULL);
}

void gvk_copy(gvk_dev *g, gvk_buf *src, gvk_buf *dst, size_t bytes)
{
    vki_BufferCopy c = { 0, 0, bytes };
    fvkCmdCopyBuffer(g->cb, src->buf, dst->buf, 1, &c);
}

void gvk_fill(gvk_dev *g, gvk_buf *dst, uint32_t value)
{
    fvkCmdFillBuffer(g->cb, dst->buf, 0, (vki_DeviceSize)(dst->size & ~(size_t)3), value);
}

int gvk_submit(gvk_dev *g, double *secs)
{
    vki_SubmitInfo si;
    vki_Result r;
    double t0, limit;
    if (!g->recording) return -1;
    g->recording = 0;
    gvk_barrier(g);
    if (g->timestamps) fvkCmdWriteTimestamp(g->cb, 0x2000, g->qp, 1);
    if ((r = fvkEndCommandBuffer(g->cb))) goto fail;
    if ((r = fvkResetFences(g->dev, 1, &g->fence))) goto fail;
    memset(&si, 0, sizeof si);
    si.sType = VKI_ST_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g->cb;
    t0 = gvk_now();
    if ((r = fvkQueueSubmit(g->queue, 1, &si, g->fence))) goto fail;
    /* wait in 100 ms steps up to the watchdog */
    limit = t0 + g->watchdog_s;
    for (;;) {
        r = fvkWaitForFences(g->dev, 1, &g->fence, 1, 100000000ull);
        if (r != VKI_TIMEOUT) break;
        if (gvk_now() > limit) {
            g->abandoned = 1;
            snprintf(g->err, sizeof g->err, "the GPU did not finish its work within %.0f s; the run was ended and the GPU left alone", g->watchdog_s);
            return -2;
        }
    }
    if (r) goto fail;
    if (secs) *secs = gvk_now() - t0;
    if (g->timestamps && secs) {
        uint64_t ts[2] = { 0, 0 }, mask = g->ts_bits >= 64 ? ~0ull : (1ull << g->ts_bits) - 1;
        if (!(r = fvkGetQueryPoolResults(g->dev, g->qp, 0, 2, sizeof ts, ts, 8, 0x1 | 0x2)))
            *secs = (double)((ts[1] - ts[0]) & mask) * g->ts_period * 1e-9;
        else if (r < 0) goto fail;
    }
    return 0;
fail:
    if (r == VKI_ERROR_DEVICE_LOST) g->lost = 1;
    snprintf(g->err, sizeof g->err, "GPU error: %s (%d)", gvk_result_str(r), (int)r);
    return r == VKI_ERROR_DEVICE_LOST ? -3 : -1;
}

#ifdef GVK_LAYOUT_CHECK
/* build with -DGVK_LAYOUT_CHECK -fsyntax-only: every vki_ type against the real Vulkan header */
#if __has_include(<vulkan/vulkan_core.h>)
#include <vulkan/vulkan_core.h>
#define SAME(a, b) _Static_assert(sizeof(a) == sizeof(b), #a " size")
#define OFF(a, b, f) _Static_assert(offsetof(a, f) == offsetof(b, f), #a "." #f)
SAME(vki_ApplicationInfo, VkApplicationInfo); OFF(vki_ApplicationInfo, VkApplicationInfo, apiVersion);
SAME(vki_InstanceCreateInfo, VkInstanceCreateInfo); OFF(vki_InstanceCreateInfo, VkInstanceCreateInfo, ppEnabledExtensionNames);
SAME(vki_PhysicalDeviceLimits, VkPhysicalDeviceLimits);
OFF(vki_PhysicalDeviceLimits, VkPhysicalDeviceLimits, maxStorageBufferRange);
OFF(vki_PhysicalDeviceLimits, VkPhysicalDeviceLimits, maxComputeWorkGroupCount);
OFF(vki_PhysicalDeviceLimits, VkPhysicalDeviceLimits, minMemoryMapAlignment);
OFF(vki_PhysicalDeviceLimits, VkPhysicalDeviceLimits, timestampComputeAndGraphics);
OFF(vki_PhysicalDeviceLimits, VkPhysicalDeviceLimits, timestampPeriod);
OFF(vki_PhysicalDeviceLimits, VkPhysicalDeviceLimits, nonCoherentAtomSize);
SAME(vki_PhysicalDeviceProperties, VkPhysicalDeviceProperties); OFF(vki_PhysicalDeviceProperties, VkPhysicalDeviceProperties, limits);
OFF(vki_PhysicalDeviceProperties, VkPhysicalDeviceProperties, sparseProperties);
SAME(vki_PhysicalDeviceProperties2, VkPhysicalDeviceProperties2);
SAME(vki_PhysicalDeviceDriverProperties, VkPhysicalDeviceDriverProperties); OFF(vki_PhysicalDeviceDriverProperties, VkPhysicalDeviceDriverProperties, driverInfo);
SAME(vki_PhysicalDevicePCIBusInfoPropertiesEXT, VkPhysicalDevicePCIBusInfoPropertiesEXT);
SAME(vki_PhysicalDeviceSubgroupProperties, VkPhysicalDeviceSubgroupProperties);
SAME(vki_PhysicalDeviceSubgroupSizeControlProperties, VkPhysicalDeviceSubgroupSizeControlProperties);
SAME(vki_PhysicalDeviceShaderCorePropertiesAMD, VkPhysicalDeviceShaderCorePropertiesAMD);
SAME(vki_PhysicalDeviceShaderSMBuiltinsPropertiesNV, VkPhysicalDeviceShaderSMBuiltinsPropertiesNV);
SAME(vki_PhysicalDeviceMemoryProperties, VkPhysicalDeviceMemoryProperties); OFF(vki_PhysicalDeviceMemoryProperties, VkPhysicalDeviceMemoryProperties, memoryHeaps);
SAME(vki_QueueFamilyProperties, VkQueueFamilyProperties);
SAME(vki_PhysicalDeviceFeatures, VkPhysicalDeviceFeatures);
_Static_assert(offsetof(VkPhysicalDeviceFeatures, shaderFloat64) == VKI_FEATURE_SHADER_FLOAT64 * 4, "shaderFloat64 index");
SAME(vki_PhysicalDeviceFeatures2, VkPhysicalDeviceFeatures2);
SAME(vki_PhysicalDeviceShaderFloat16Int8Features, VkPhysicalDeviceShaderFloat16Int8Features);
SAME(vki_ExtensionProperties, VkExtensionProperties);
SAME(vki_DeviceQueueCreateInfo, VkDeviceQueueCreateInfo);
SAME(vki_DeviceCreateInfo, VkDeviceCreateInfo); OFF(vki_DeviceCreateInfo, VkDeviceCreateInfo, pEnabledFeatures);
SAME(vki_BufferCreateInfo, VkBufferCreateInfo); OFF(vki_BufferCreateInfo, VkBufferCreateInfo, pQueueFamilyIndices);
SAME(vki_MemoryRequirements, VkMemoryRequirements);
SAME(vki_MemoryAllocateInfo, VkMemoryAllocateInfo);
SAME(vki_ShaderModuleCreateInfo, VkShaderModuleCreateInfo);
SAME(vki_DescriptorSetLayoutBinding, VkDescriptorSetLayoutBinding);
SAME(vki_DescriptorSetLayoutCreateInfo, VkDescriptorSetLayoutCreateInfo);
SAME(vki_PushConstantRange, VkPushConstantRange);
SAME(vki_PipelineLayoutCreateInfo, VkPipelineLayoutCreateInfo);
SAME(vki_PipelineShaderStageCreateInfo, VkPipelineShaderStageCreateInfo);
SAME(vki_ComputePipelineCreateInfo, VkComputePipelineCreateInfo); OFF(vki_ComputePipelineCreateInfo, VkComputePipelineCreateInfo, basePipelineIndex);
SAME(vki_DescriptorPoolSize, VkDescriptorPoolSize);
SAME(vki_DescriptorPoolCreateInfo, VkDescriptorPoolCreateInfo);
SAME(vki_DescriptorSetAllocateInfo, VkDescriptorSetAllocateInfo);
SAME(vki_DescriptorBufferInfo, VkDescriptorBufferInfo);
SAME(vki_WriteDescriptorSet, VkWriteDescriptorSet); OFF(vki_WriteDescriptorSet, VkWriteDescriptorSet, pBufferInfo);
SAME(vki_CommandPoolCreateInfo, VkCommandPoolCreateInfo);
SAME(vki_CommandBufferAllocateInfo, VkCommandBufferAllocateInfo);
SAME(vki_CommandBufferBeginInfo, VkCommandBufferBeginInfo);
SAME(vki_QueryPoolCreateInfo, VkQueryPoolCreateInfo);
SAME(vki_FenceCreateInfo, VkFenceCreateInfo);
SAME(vki_SubmitInfo, VkSubmitInfo);
SAME(vki_BufferCopy, VkBufferCopy);
SAME(vki_MemoryBarrier, VkMemoryBarrier);
_Static_assert(VKI_ST_PHYSICAL_DEVICE_FEATURES_2 == (int)VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, "sType");
_Static_assert(VKI_ST_PHYSICAL_DEVICE_PROPERTIES_2 == (int)VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, "sType");
_Static_assert(VKI_ST_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES == (int)VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES, "sType");
_Static_assert(VKI_ST_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES == (int)VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES, "sType");
_Static_assert(VKI_ST_PHYSICAL_DEVICE_SHADER_SM_BUILTINS_PROPERTIES_NV == (int)VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SM_BUILTINS_PROPERTIES_NV, "sType");
_Static_assert(VKI_ST_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_AMD == (int)VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_AMD, "sType");
_Static_assert(VKI_ST_PHYSICAL_DEVICE_DRIVER_PROPERTIES == (int)VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES, "sType");
_Static_assert(VKI_ST_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT == (int)VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT, "sType");
_Static_assert(VKI_ST_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES == (int)VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES, "sType");
_Static_assert(VKI_ST_MEMORY_BARRIER == (int)VK_STRUCTURE_TYPE_MEMORY_BARRIER && VKI_ST_COMMAND_BUFFER_BEGIN_INFO == (int)VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, "sType");
_Static_assert(VKI_ST_COMPUTE_PIPELINE_CREATE_INFO == (int)VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO && VKI_ST_PIPELINE_LAYOUT_CREATE_INFO == (int)VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, "sType");
_Static_assert(VKI_ST_DESCRIPTOR_SET_LAYOUT_CREATE_INFO == (int)VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO && VKI_ST_WRITE_DESCRIPTOR_SET == (int)VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, "sType");
_Static_assert(VKI_ST_QUERY_POOL_CREATE_INFO == (int)VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO && VKI_ST_SHADER_MODULE_CREATE_INFO == (int)VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, "sType");
_Static_assert(VKI_ST_COMMAND_POOL_CREATE_INFO == (int)VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO && VKI_ST_COMMAND_BUFFER_ALLOCATE_INFO == (int)VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, "sType");
_Static_assert(VKI_ST_DESCRIPTOR_POOL_CREATE_INFO == (int)VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO && VKI_ST_DESCRIPTOR_SET_ALLOCATE_INFO == (int)VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, "sType");
_Static_assert(VKI_ST_PIPELINE_SHADER_STAGE_CREATE_INFO == (int)VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO && VKI_ST_FENCE_CREATE_INFO == (int)VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, "sType");
#else
#error "GVK_LAYOUT_CHECK needs <vulkan/vulkan_core.h> (vulkan-headers)"
#endif
#endif

#else   /* not Linux */
int gvk_load(char *why, size_t wn) { if (why) snprintf(why, wn, "Linux only"); return 0; }
int gpu_available(char *why, size_t wn) { return gvk_load(why, wn); }
int gpu_list(gpu_dev *d, int max, char *why, size_t wn) { (void)d; (void)max; return gvk_load(why, wn) - 1; }
int gpu_default_device(const gpu_dev *d, int n) { (void)d; (void)n; return -1; }
void gpu_quiet_layers(void) {}
int gpu_claim(const char *name, const char **who) { (void)name; if (who) *who = NULL; return 1; }
void gpu_release(void) {}
int gpu_busy(void) { return 0; }
void gpu_claim_hung(void) {}
const char *gpu_claim_holder(void) { return NULL; }
void gpu_claim_text(const char *who, char *buf, size_t n) { snprintf(buf, n, "%s is running; wait for it to finish", who ? who : "another GPU test"); }
double gvk_now(void) { return (double)time(NULL); }
#endif
