/* gpu_vk.h - the GPU benchmarks' shared Vulkan layer (gpu_vk.c), internal to SRS: bench_gpu.c,
 * gpuburn_vk.c (GPU Burnin / GPU Compute compare) use it.
 *
 * The Vulkan loader is opened with dlopen("libvulkan.so.1"), so SRS needs no Vulkan headers to
 * build and no Vulkan package to run. The few Vulkan types used are declared here with a vki_
 * prefix, copied field for field from the Khronos Vulkan-Headers (vulkan_core.h, Apache-2.0 OR
 * MIT); building gpu_vk.c with -DGVK_LAYOUT_CHECK checks every one of them against the real
 * header when it is installed (tests/bench-gpu-test.sh does). */
#ifndef GPU_VK_H
#define GPU_VK_H

#include "bench_gpu.h"
#include <stddef.h>
#include <stdint.h>

typedef uint32_t vki_Bool32;
typedef uint64_t vki_DeviceSize;
typedef uint32_t vki_Flags;
typedef int32_t vki_Result;

typedef struct vki_Instance_T *vki_Instance;
typedef struct vki_PhysicalDevice_T *vki_PhysicalDevice;
typedef struct vki_Device_T *vki_Device;
typedef struct vki_Queue_T *vki_Queue;
typedef struct vki_CommandBuffer_T *vki_CommandBuffer;
/* non-dispatchable handles: pointers on 64-bit, uint64_t on 32-bit (as VK_DEFINE_NON_DISPATCHABLE_HANDLE) */
#if UINTPTR_MAX > 0xffffffffu
#define VKI_ND(n) typedef struct n##_T *n
#else
#define VKI_ND(n) typedef uint64_t n
#endif
VKI_ND(vki_Buffer); VKI_ND(vki_DeviceMemory); VKI_ND(vki_ShaderModule); VKI_ND(vki_Pipeline);
VKI_ND(vki_PipelineLayout); VKI_ND(vki_DescriptorSetLayout); VKI_ND(vki_DescriptorPool);
VKI_ND(vki_DescriptorSet); VKI_ND(vki_CommandPool); VKI_ND(vki_Fence); VKI_ND(vki_QueryPool);
VKI_ND(vki_PipelineCache); VKI_ND(vki_Sampler); VKI_ND(vki_BufferView); VKI_ND(vki_Semaphore);

enum {
    VKI_SUCCESS = 0, VKI_NOT_READY = 1, VKI_TIMEOUT = 2, VKI_INCOMPLETE = 5,
    VKI_ERROR_OUT_OF_HOST_MEMORY = -1, VKI_ERROR_OUT_OF_DEVICE_MEMORY = -2, VKI_ERROR_INITIALIZATION_FAILED = -3,
    VKI_ERROR_DEVICE_LOST = -4, VKI_ERROR_INCOMPATIBLE_DRIVER = -9
};
enum {
    VKI_ST_APPLICATION_INFO = 0, VKI_ST_INSTANCE_CREATE_INFO = 1, VKI_ST_DEVICE_QUEUE_CREATE_INFO = 2,
    VKI_ST_DEVICE_CREATE_INFO = 3, VKI_ST_SUBMIT_INFO = 4, VKI_ST_MEMORY_ALLOCATE_INFO = 5,
    VKI_ST_FENCE_CREATE_INFO = 8, VKI_ST_QUERY_POOL_CREATE_INFO = 11, VKI_ST_BUFFER_CREATE_INFO = 12,
    VKI_ST_SHADER_MODULE_CREATE_INFO = 16, VKI_ST_PIPELINE_SHADER_STAGE_CREATE_INFO = 18,
    VKI_ST_COMPUTE_PIPELINE_CREATE_INFO = 29, VKI_ST_PIPELINE_LAYOUT_CREATE_INFO = 30,
    VKI_ST_DESCRIPTOR_SET_LAYOUT_CREATE_INFO = 32, VKI_ST_DESCRIPTOR_POOL_CREATE_INFO = 33,
    VKI_ST_DESCRIPTOR_SET_ALLOCATE_INFO = 34, VKI_ST_WRITE_DESCRIPTOR_SET = 35,
    VKI_ST_COMMAND_POOL_CREATE_INFO = 39, VKI_ST_COMMAND_BUFFER_ALLOCATE_INFO = 40,
    VKI_ST_COMMAND_BUFFER_BEGIN_INFO = 42, VKI_ST_MEMORY_BARRIER = 46,
    VKI_ST_PHYSICAL_DEVICE_FEATURES_2 = 1000059000, VKI_ST_PHYSICAL_DEVICE_PROPERTIES_2 = 1000059001,
    VKI_ST_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES = 1000082000,
    VKI_ST_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES = 1000094000,
    VKI_ST_PHYSICAL_DEVICE_SHADER_SM_BUILTINS_PROPERTIES_NV = 1000154001,
    VKI_ST_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_AMD = 1000185000,
    VKI_ST_PHYSICAL_DEVICE_DRIVER_PROPERTIES = 1000196000,
    VKI_ST_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT = 1000212000,
    VKI_ST_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES = 1000225000
};

typedef struct { uint32_t width, height, depth; } vki_Extent3D;
typedef struct {
    int32_t sType; const void *pNext; const char *pApplicationName; uint32_t applicationVersion;
    const char *pEngineName; uint32_t engineVersion; uint32_t apiVersion;
} vki_ApplicationInfo;
typedef struct {
    int32_t sType; const void *pNext; vki_Flags flags; const vki_ApplicationInfo *pApplicationInfo;
    uint32_t enabledLayerCount; const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount; const char *const *ppEnabledExtensionNames;
} vki_InstanceCreateInfo;
typedef struct {
    uint32_t maxImageDimension1D, maxImageDimension2D, maxImageDimension3D, maxImageDimensionCube, maxImageArrayLayers;
    uint32_t maxTexelBufferElements, maxUniformBufferRange, maxStorageBufferRange, maxPushConstantsSize;
    uint32_t maxMemoryAllocationCount, maxSamplerAllocationCount;
    vki_DeviceSize bufferImageGranularity, sparseAddressSpaceSize;
    uint32_t maxBoundDescriptorSets, maxPerStageDescriptorSamplers, maxPerStageDescriptorUniformBuffers;
    uint32_t maxPerStageDescriptorStorageBuffers, maxPerStageDescriptorSampledImages, maxPerStageDescriptorStorageImages;
    uint32_t maxPerStageDescriptorInputAttachments, maxPerStageResources, maxDescriptorSetSamplers;
    uint32_t maxDescriptorSetUniformBuffers, maxDescriptorSetUniformBuffersDynamic, maxDescriptorSetStorageBuffers;
    uint32_t maxDescriptorSetStorageBuffersDynamic, maxDescriptorSetSampledImages, maxDescriptorSetStorageImages;
    uint32_t maxDescriptorSetInputAttachments, maxVertexInputAttributes, maxVertexInputBindings;
    uint32_t maxVertexInputAttributeOffset, maxVertexInputBindingStride, maxVertexOutputComponents;
    uint32_t maxTessellationGenerationLevel, maxTessellationPatchSize, maxTessellationControlPerVertexInputComponents;
    uint32_t maxTessellationControlPerVertexOutputComponents, maxTessellationControlPerPatchOutputComponents;
    uint32_t maxTessellationControlTotalOutputComponents, maxTessellationEvaluationInputComponents;
    uint32_t maxTessellationEvaluationOutputComponents, maxGeometryShaderInvocations, maxGeometryInputComponents;
    uint32_t maxGeometryOutputComponents, maxGeometryOutputVertices, maxGeometryTotalOutputComponents;
    uint32_t maxFragmentInputComponents, maxFragmentOutputAttachments, maxFragmentDualSrcAttachments;
    uint32_t maxFragmentCombinedOutputResources, maxComputeSharedMemorySize, maxComputeWorkGroupCount[3];
    uint32_t maxComputeWorkGroupInvocations, maxComputeWorkGroupSize[3], subPixelPrecisionBits, subTexelPrecisionBits;
    uint32_t mipmapPrecisionBits, maxDrawIndexedIndexValue, maxDrawIndirectCount;
    float maxSamplerLodBias, maxSamplerAnisotropy;
    uint32_t maxViewports, maxViewportDimensions[2];
    float viewportBoundsRange[2];
    uint32_t viewportSubPixelBits;
    size_t minMemoryMapAlignment;
    vki_DeviceSize minTexelBufferOffsetAlignment, minUniformBufferOffsetAlignment, minStorageBufferOffsetAlignment;
    int32_t minTexelOffset; uint32_t maxTexelOffset; int32_t minTexelGatherOffset; uint32_t maxTexelGatherOffset;
    float minInterpolationOffset, maxInterpolationOffset;
    uint32_t subPixelInterpolationOffsetBits, maxFramebufferWidth, maxFramebufferHeight, maxFramebufferLayers;
    vki_Flags framebufferColorSampleCounts, framebufferDepthSampleCounts, framebufferStencilSampleCounts;
    vki_Flags framebufferNoAttachmentsSampleCounts;
    uint32_t maxColorAttachments;
    vki_Flags sampledImageColorSampleCounts, sampledImageIntegerSampleCounts, sampledImageDepthSampleCounts;
    vki_Flags sampledImageStencilSampleCounts, storageImageSampleCounts;
    uint32_t maxSampleMaskWords;
    vki_Bool32 timestampComputeAndGraphics;
    float timestampPeriod;
    uint32_t maxClipDistances, maxCullDistances, maxCombinedClipAndCullDistances, discreteQueuePriorities;
    float pointSizeRange[2], lineWidthRange[2], pointSizeGranularity, lineWidthGranularity;
    vki_Bool32 strictLines, standardSampleLocations;
    vki_DeviceSize optimalBufferCopyOffsetAlignment, optimalBufferCopyRowPitchAlignment, nonCoherentAtomSize;
} vki_PhysicalDeviceLimits;
typedef struct {
    vki_Bool32 residencyStandard2DBlockShape, residencyStandard2DMultisampleBlockShape, residencyStandard3DBlockShape;
    vki_Bool32 residencyAlignedMipSize, residencyNonResidentStrict;
} vki_PhysicalDeviceSparseProperties;
typedef struct {
    uint32_t apiVersion, driverVersion, vendorID, deviceID;
    int32_t deviceType;
    char deviceName[256];
    uint8_t pipelineCacheUUID[16];
    vki_PhysicalDeviceLimits limits;
    vki_PhysicalDeviceSparseProperties sparseProperties;
} vki_PhysicalDeviceProperties;
typedef struct { int32_t sType; void *pNext; vki_PhysicalDeviceProperties properties; } vki_PhysicalDeviceProperties2;
typedef struct { uint8_t major, minor, subminor, patch; } vki_ConformanceVersion;
typedef struct {
    int32_t sType; void *pNext; int32_t driverID; char driverName[256]; char driverInfo[256];
    vki_ConformanceVersion conformanceVersion;
} vki_PhysicalDeviceDriverProperties;
typedef struct { int32_t sType; void *pNext; uint32_t pciDomain, pciBus, pciDevice, pciFunction; } vki_PhysicalDevicePCIBusInfoPropertiesEXT;
typedef struct {
    int32_t sType; void *pNext; uint32_t subgroupSize; vki_Flags supportedStages, supportedOperations;
    vki_Bool32 quadOperationsInAllStages;
} vki_PhysicalDeviceSubgroupProperties;
typedef struct {
    int32_t sType; void *pNext; uint32_t minSubgroupSize, maxSubgroupSize, maxComputeWorkgroupSubgroups;
    vki_Flags requiredSubgroupSizeStages;
} vki_PhysicalDeviceSubgroupSizeControlProperties;
typedef struct {
    int32_t sType; void *pNext;
    uint32_t shaderEngineCount, shaderArraysPerEngineCount, computeUnitsPerShaderArray, simdPerComputeUnit;
    uint32_t wavefrontsPerSimd, wavefrontSize, sgprsPerSimd, minSgprAllocation, maxSgprAllocation;
    uint32_t sgprAllocationGranularity, vgprsPerSimd, minVgprAllocation, maxVgprAllocation, vgprAllocationGranularity;
} vki_PhysicalDeviceShaderCorePropertiesAMD;
typedef struct { int32_t sType; void *pNext; uint32_t shaderSMCount, shaderWarpsPerSM; } vki_PhysicalDeviceShaderSMBuiltinsPropertiesNV;
typedef struct { vki_Flags propertyFlags; uint32_t heapIndex; } vki_MemoryType;
typedef struct { vki_DeviceSize size; vki_Flags flags; } vki_MemoryHeap;
typedef struct {
    uint32_t memoryTypeCount; vki_MemoryType memoryTypes[32];
    uint32_t memoryHeapCount; vki_MemoryHeap memoryHeaps[16];
} vki_PhysicalDeviceMemoryProperties;
typedef struct { vki_Flags queueFlags; uint32_t queueCount, timestampValidBits; vki_Extent3D minImageTransferGranularity; } vki_QueueFamilyProperties;
#define VKI_FEATURE_COUNT 55
#define VKI_FEATURE_SHADER_FLOAT64 39   /* index of shaderFloat64 in VkPhysicalDeviceFeatures */
typedef struct { vki_Bool32 f[VKI_FEATURE_COUNT]; } vki_PhysicalDeviceFeatures;
typedef struct { int32_t sType; void *pNext; vki_PhysicalDeviceFeatures features; } vki_PhysicalDeviceFeatures2;
typedef struct { int32_t sType; void *pNext; vki_Bool32 shaderFloat16, shaderInt8; } vki_PhysicalDeviceShaderFloat16Int8Features;
typedef struct { char extensionName[256]; uint32_t specVersion; } vki_ExtensionProperties;
typedef struct {
    int32_t sType; const void *pNext; vki_Flags flags; uint32_t queueFamilyIndex, queueCount; const float *pQueuePriorities;
} vki_DeviceQueueCreateInfo;
typedef struct {
    int32_t sType; const void *pNext; vki_Flags flags; uint32_t queueCreateInfoCount;
    const vki_DeviceQueueCreateInfo *pQueueCreateInfos; uint32_t enabledLayerCount; const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount; const char *const *ppEnabledExtensionNames; const vki_PhysicalDeviceFeatures *pEnabledFeatures;
} vki_DeviceCreateInfo;
typedef struct {
    int32_t sType; const void *pNext; vki_Flags flags; vki_DeviceSize size; vki_Flags usage; int32_t sharingMode;
    uint32_t queueFamilyIndexCount; const uint32_t *pQueueFamilyIndices;
} vki_BufferCreateInfo;
typedef struct { vki_DeviceSize size, alignment; uint32_t memoryTypeBits; } vki_MemoryRequirements;
typedef struct { int32_t sType; const void *pNext; vki_DeviceSize allocationSize; uint32_t memoryTypeIndex; } vki_MemoryAllocateInfo;
typedef struct { int32_t sType; const void *pNext; vki_Flags flags; size_t codeSize; const uint32_t *pCode; } vki_ShaderModuleCreateInfo;
typedef struct {
    uint32_t binding; int32_t descriptorType; uint32_t descriptorCount; vki_Flags stageFlags; const vki_Sampler *pImmutableSamplers;
} vki_DescriptorSetLayoutBinding;
typedef struct {
    int32_t sType; const void *pNext; vki_Flags flags; uint32_t bindingCount; const vki_DescriptorSetLayoutBinding *pBindings;
} vki_DescriptorSetLayoutCreateInfo;
typedef struct { vki_Flags stageFlags; uint32_t offset, size; } vki_PushConstantRange;
typedef struct {
    int32_t sType; const void *pNext; vki_Flags flags; uint32_t setLayoutCount; const vki_DescriptorSetLayout *pSetLayouts;
    uint32_t pushConstantRangeCount; const vki_PushConstantRange *pPushConstantRanges;
} vki_PipelineLayoutCreateInfo;
typedef struct {
    int32_t sType; const void *pNext; vki_Flags flags; vki_Flags stage; vki_ShaderModule module; const char *pName;
    const void *pSpecializationInfo;
} vki_PipelineShaderStageCreateInfo;
typedef struct {
    int32_t sType; const void *pNext; vki_Flags flags; vki_PipelineShaderStageCreateInfo stage; vki_PipelineLayout layout;
    vki_Pipeline basePipelineHandle; int32_t basePipelineIndex;
} vki_ComputePipelineCreateInfo;
typedef struct { int32_t type; uint32_t descriptorCount; } vki_DescriptorPoolSize;
typedef struct {
    int32_t sType; const void *pNext; vki_Flags flags; uint32_t maxSets, poolSizeCount; const vki_DescriptorPoolSize *pPoolSizes;
} vki_DescriptorPoolCreateInfo;
typedef struct {
    int32_t sType; const void *pNext; vki_DescriptorPool descriptorPool; uint32_t descriptorSetCount;
    const vki_DescriptorSetLayout *pSetLayouts;
} vki_DescriptorSetAllocateInfo;
typedef struct { vki_Buffer buffer; vki_DeviceSize offset, range; } vki_DescriptorBufferInfo;
typedef struct {
    int32_t sType; const void *pNext; vki_DescriptorSet dstSet; uint32_t dstBinding, dstArrayElement, descriptorCount;
    int32_t descriptorType; const void *pImageInfo; const vki_DescriptorBufferInfo *pBufferInfo; const vki_BufferView *pTexelBufferView;
} vki_WriteDescriptorSet;
typedef struct { int32_t sType; const void *pNext; vki_Flags flags; uint32_t queueFamilyIndex; } vki_CommandPoolCreateInfo;
typedef struct {
    int32_t sType; const void *pNext; vki_CommandPool commandPool; int32_t level; uint32_t commandBufferCount;
} vki_CommandBufferAllocateInfo;
typedef struct { int32_t sType; const void *pNext; vki_Flags flags; const void *pInheritanceInfo; } vki_CommandBufferBeginInfo;
typedef struct {
    int32_t sType; const void *pNext; vki_Flags flags; int32_t queryType; uint32_t queryCount; vki_Flags pipelineStatistics;
} vki_QueryPoolCreateInfo;
typedef struct { int32_t sType; const void *pNext; vki_Flags flags; } vki_FenceCreateInfo;
typedef struct {
    int32_t sType; const void *pNext; uint32_t waitSemaphoreCount; const vki_Semaphore *pWaitSemaphores;
    const vki_Flags *pWaitDstStageMask; uint32_t commandBufferCount; const vki_CommandBuffer *pCommandBuffers;
    uint32_t signalSemaphoreCount; const vki_Semaphore *pSignalSemaphores;
} vki_SubmitInfo;
typedef struct { vki_DeviceSize srcOffset, dstOffset, size; } vki_BufferCopy;
typedef struct { int32_t sType; const void *pNext; vki_Flags srcAccessMask, dstAccessMask; } vki_MemoryBarrier;

/* ---------------------------------------------------------------- SRS side */
enum { GVK_DEVICE, GVK_UPLOAD, GVK_READBACK };   /* buffer homes: VRAM, host memory for uploads / for reading back */

typedef struct {
    vki_Buffer buf;
    vki_DeviceMemory mem;
    size_t size;
    void *map;            /* host buffers: mapped for the buffer's life */
    int device_local;     /* the memory type chosen is device-local */
} gvk_buf;

typedef struct gvk_dev {
    gpu_dev info;
    vki_Instance inst;
    vki_PhysicalDevice phys;
    vki_Device dev;
    vki_Queue queue;
    uint32_t qfam, ts_bits;
    double ts_period;     /* ns per timestamp tick */
    int timestamps;       /* 1 = GPU timestamps on this queue, 0 = host timing */
    int fp16, fp64;       /* enabled on the device */
    uint32_t max_groups, max_storage;
    vki_PhysicalDeviceMemoryProperties mp;
    vki_DescriptorSetLayout dsl;
    vki_PipelineLayout pl;
    vki_DescriptorPool pool;
    vki_CommandPool cpool;
    vki_CommandBuffer cb;
    vki_Fence fence;
    vki_QueryPool qp;
    int recording;
    int abandoned;        /* GPU work timed out: nothing is destroyed (it may still be in use) */
    int lost;             /* VK_ERROR_DEVICE_LOST seen */
    double watchdog_s;    /* longest wait for one submission (default 5 s) */
    char err[200];        /* last error, plain English */
} gvk_dev;

#define GVK_BINDINGS 4      /* storage buffers 0..3 in every pipeline */
#define GVK_PUSH_BYTES 64

int  gvk_load(char *why, size_t wn);          /* open the loader once; 1 ok */
const char *gvk_result_str(vki_Result r);     /* "the GPU stopped answering (device lost)" ... */
/* open device idx (index into gpu_list order); software_only (or env SRS_GPU_SOFTWARE_ONLY=1) refuses
   any device that is not a CPU (software) renderer. 0 ok, -1 why says why */
int  gvk_open(gvk_dev *g, int idx, int software_only, char *why, size_t wn);
void gvk_close(gvk_dev *g);
int  gvk_buf_new(gvk_dev *g, gvk_buf *b, size_t bytes, int home);
void gvk_buf_free(gvk_dev *g, gvk_buf *b);
vki_Pipeline gvk_pipeline(gvk_dev *g, const uint32_t *spv, size_t bytes);   /* 0 on failure (g->err) */
void gvk_pipeline_free(gvk_dev *g, vki_Pipeline p);
vki_DescriptorSet gvk_set(gvk_dev *g, gvk_buf *const *b, int n);            /* bindings 0..n-1; 0 on failure */
/* recording one submission: begin (with a start timestamp), commands, then gvk_submit */
int  gvk_begin(gvk_dev *g);
void gvk_dispatch(gvk_dev *g, vki_Pipeline p, vki_DescriptorSet s, const void *push, uint32_t pn, uint32_t groups);
void gvk_dispatch2(gvk_dev *g, vki_Pipeline p, vki_DescriptorSet s, const void *push, uint32_t pn, uint32_t gx, uint32_t gy);
void gvk_barrier(gvk_dev *g);                 /* shader / transfer writes visible to later shaders, copies and the host */
void gvk_copy(gvk_dev *g, gvk_buf *src, gvk_buf *dst, size_t bytes);
void gvk_fill(gvk_dev *g, gvk_buf *dst, uint32_t value);
/* end, submit, wait (watchdog). secs = GPU time between the timestamps (host time without them).
   0 ok; -1 Vulkan error; -2 did not finish in time (device abandoned); -3 device lost. g->err says what. */
int  gvk_submit(gvk_dev *g, double *secs);
double gvk_now(void);

#endif
